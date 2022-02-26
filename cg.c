/*
 *	Process Isolator -- Control Groups
 *
 *	(c) 2012-2016 Martin Mares <mj@ucw.cz>
 *	(c) 2012-2014 Bernard Blackham <bernard@blackham.com.au>
 */

#include "isolate.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct cg_controller_desc {
  const char *name;
  int optional;
};

static char cg_name[256];
static char cg_parent_name[256];

#define CG_BUFSIZE 1024

static void cg_makepath(char *buf, size_t len, const char *attr) {
  snprintf(buf, len, "%s/%s/%s", cf_cg_root, cg_name, attr);
}

static int cg_read(const char *attr, char *buf) {
  char path[256];
  cg_makepath(path, sizeof(path), attr);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    die("Cannot read %s: %m", path);
  }

  int n = read(fd, buf, CG_BUFSIZE);
  if (n < 0) {
    die("Cannot read %s: %m", path);
  }
  if (n >= CG_BUFSIZE - 1)
    die("Attribute %s too long", path);
  if (n > 0 && buf[n - 1] == '\n')
    n--;
  buf[n] = 0;

  if (verbose > 1)
    msg("CG: Read %s = <%s>\n", attr, buf);

  return 1;
}

static void __attribute__((format(printf, 2, 3)))
cg_write(const char *attr, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char buf[CG_BUFSIZE];
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  if (n >= CG_BUFSIZE)
    die("cg_write: Value for attribute %s is too long", attr);

  if (verbose > 1)
    msg("CG: Write %s = %s", attr, buf);

  char path[256];
  cg_makepath(path, sizeof(path), attr);

  int fd = open(path, O_WRONLY | O_TRUNC);
  if (fd < 0) {
    die("Cannot write %s: %m", path);
  }

  int written = write(fd, buf, n);
  if (written < 0) {
    die("Cannot set %s to %s: %m", path, buf);
  }
  if (written != n)
    die("Short write to %s (%d out of %d bytes)", path, written, n);
}

void cg_init(void) {
  if (!cg_enable)
    return;

  if (!dir_exists(cf_cg_root))
    die("Control group filesystem at %s not mounted", cf_cg_root);

  if (cf_cg_parent) {
    snprintf(cg_name, sizeof(cg_name), "%s/box-%d", cf_cg_parent, box_id);
    snprintf(cg_parent_name, sizeof(cg_parent_name), "%s", cf_cg_parent);
  } else {
    snprintf(cg_name, sizeof(cg_name), "box-%d", box_id);
    strcpy(cg_parent_name, ".");
  }
  msg("Using control group %s under parent %s\n", cg_name, cg_parent_name);
}

void cg_prepare(void) {
  if (!cg_enable)
    return;

  struct stat st;
  char path[256], subpath[256];

  cg_makepath(path, sizeof(path), "");
  cg_makepath(subpath, sizeof(subpath), "runner/");

  if (stat(subpath, &st) >= 0 || errno != ENOENT) {
    msg("Control group %s already exists, trying to empty it.\n", subpath);
    if (rmdir(subpath) < 0)
      die("Failed to reset control group %s: %m", subpath);
  }

  if (stat(path, &st) >= 0 || errno != ENOENT) {
    msg("Control group %s already exists, trying to empty it.\n", path);
    if (rmdir(path) < 0)
      die("Failed to reset control group %s: %m", path);
  }
  if (stat(path, &st) >= 0 || errno != ENOENT) {
    msg("Control group %s already exists, trying to empty it.\n", path);
    if (rmdir(path) < 0)
      die("Failed to reset control group %s: %m", path);
  }

  if (mkdir(path, 0777) < 0)
    die("Failed to create control group %s: %m", path);

  if (mkdir(subpath, 0777) < 0)
    die("Failed to create control group %s: %m", subpath);

  cg_write("cgroup.subtree_control", "+memory +cpu +cpuset");

  // set up allowed cpus and memory nodes.
  struct cf_per_box *cf = cf_current_box();
  if (cf->cpus)
    cg_write("cpuset.cpus", "%s", cf->cpus);
  if (cf->mems)
    cg_write("cpuset.mems", "%s", cf->mems);
}

void cg_enter(void) {
  if (!cg_enable)
    return;

  msg("Entering control group %s\n", cg_name);

  cg_write("runner/cgroup.procs", "0\n");

  if (cg_memory_limit) {
    cg_write("memory.max", "%lld\n", (long long)cg_memory_limit << 10);
    cg_write("memory.swap.max", "0\n");
  }
}

int cg_get_run_time_ms(void) {
  if (!cg_enable)
    return 0;

  char buf[CG_BUFSIZE];
  cg_read("cpu.stat", buf);

  char *s = buf;
  unsigned long long usage_usec;
  while (s) {
    if (sscanf(s, "usage_usec %llu", &usage_usec) == 1) {
      return usage_usec / 1000;
    }
    s = strchr(s, '\n');
    if (s)
      s++;
  }
  return 0;
}

void cg_stats(void) {
  if (!cg_enable)
    return;
}

void cg_remove(void) {
  if (!cg_enable)
    return;

  char path[256], subpath[256];
  cg_makepath(path, sizeof(path), "");
  cg_makepath(subpath, sizeof(subpath), "runner/");

  if (rmdir(subpath) < 0)
    die("Cannot remove control group %s: %m", subpath);
  if (rmdir(path) < 0)
    die("Cannot remove control group %s: %m", path);
}
