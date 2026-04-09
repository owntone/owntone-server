#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "filescanner.h"
#include "config.h"
#include "misc.h"
#include "logger.h"

#ifdef HAVE_LIBMOUNT
#include <libmount/libmount.h>

static struct libmnt_monitor *mountwatch_monitor;
static struct libmnt_table *mountwatch_table;

// path is allocated if a change is found
static int
compare_tables(char **path, struct libmnt_table *old_tab, struct libmnt_table *new_tab)
{
  struct libmnt_iter *iter;
  struct libmnt_fs *fs;
  const char *target;

  *path = NULL;

  CHECK_NULL(L_SCAN, iter = mnt_new_iter(MNT_ITER_FORWARD));

  // Find new mounts (in new_tab but not in old_tab)
  mnt_reset_iter(iter, MNT_ITER_FORWARD);
  while (mnt_table_next_fs(new_tab, iter, &fs) == 0)
    {
      target = mnt_fs_get_target(fs);
      if (!target || mnt_table_find_target(old_tab, target, MNT_ITER_FORWARD))
	continue;

      *path = strdup(target);
      return MOUNTWATCH_MOUNT;
    }

  // Find removed mounts (in old_tab but not in new_tab)
  mnt_reset_iter(iter, MNT_ITER_FORWARD);
  while (mnt_table_next_fs(old_tab, iter, &fs) == 0)
    {
      target = mnt_fs_get_target(fs);
      if (!target || mnt_table_find_target(new_tab, target, MNT_ITER_FORWARD))
	continue;

      *path = strdup(target);
      return MOUNTWATCH_UNMOUNT;
    }

  mnt_free_iter(iter);
  return MOUNTWATCH_NONE;
}

int
mountwatch_event_get(char **path)
{
  struct libmnt_table *newtable = NULL;
  enum mountwatch_event event;
  int ret;

  *path = NULL;

  ret = mnt_monitor_event_cleanup(mountwatch_monitor);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Monitor cleanup failed: %s\n", strerror(-ret));
      goto error;
    }

  CHECK_NULL(L_SCAN, newtable = mnt_new_table());

  ret = mnt_table_parse_mtab(newtable, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Failed to reload mount table: %s\n", strerror(-ret));
      goto error;
    }

  event = compare_tables(path, mountwatch_table, newtable);

  mnt_unref_table(mountwatch_table);
  mountwatch_table = newtable;

  return event;

 error:
  if (newtable)
    mnt_unref_table(newtable);

  return MOUNTWATCH_ERR;
}

void
mountwatch_deinit(void)
{
  mnt_unref_table(mountwatch_table);
  mountwatch_table = NULL;

  mnt_unref_monitor(mountwatch_monitor);
  mountwatch_monitor = NULL;
}

int
mountwatch_init(void)
{
  int ret, fd;

  CHECK_NULL(L_SCAN, mountwatch_monitor = mnt_new_monitor());

  ret = mnt_monitor_enable_kernel(mountwatch_monitor, 1);
  if (ret < 0)
    goto error;

  fd = mnt_monitor_get_fd(mountwatch_monitor);
  if (fd < 0)
    goto error;

  mountwatch_table = mnt_new_table();
  if (!mountwatch_table)
    goto error;

  ret = mnt_table_parse_mtab(mountwatch_table, NULL);
  if (ret < 0)
    goto error;

  return fd;

 error:
  DPRINTF(E_LOG, L_SCAN, "Error initializing libmount, mount/unmount events won't be detected\n");
  mountwatch_deinit();
  errno = -ret; //TODO
  return -1;
}
#else
int
mountwatch_event_get(char **path)
{
  *path = NULL;
  return MOUNTWATCH_NONE;
}

void
mountwatch_deinit(void)
{
  return;
}

int
mountwatch_init(void)
{
  DPRINTF(E_LOG, L_SCAN, "No libmount on this platform, mount/unmount events won't be detected\n");
  return -1;
}
#endif
