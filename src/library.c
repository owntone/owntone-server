/*
 * Copyright (C) 2015 Christian Meffert <christian.meffert@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unictype.h>
#include <uninorm.h>
#include <unistr.h>

#include <event2/event.h>

#include "library.h"
#include "cache.h"
#include "commands.h"
#include "conffile.h"
#include "db.h"
#include "logger.h"
#include "misc.h"
#include "listener.h"
#include "player.h"

struct playlist_add_param
{
  const char *vp_playlist;
  const char *vp_item;
};

static struct commands_base *cmdbase;
static pthread_t tid_library;

struct event_base *evbase_lib;

extern struct library_source filescanner;
#ifdef HAVE_SPOTIFY_H
extern struct library_source spotifyscanner;
#endif

static struct library_source *sources[] = {
    &filescanner,
#ifdef HAVE_SPOTIFY_H
    &spotifyscanner,
#endif
    NULL
};

/* Flag for aborting scan on exit */
static bool scan_exit;

/* Flag for scan in progress */
static bool scanning;

// After being told by db that the library was updated through
// library_update_trigger(), wait 5 seconds before notifying listeners
// of LISTENER_DATABASE. This is to catch bulk updates like automated
// tag editing, music file imports/renames.  This way multiple updates
// are collected for a single update notification (useful to avoid
// repeated library reads from clients).
//
// Note: this update delay does not apply to library scans.  The scans
// use the flag `scanning` for deferring update notifcations.
static struct timeval library_update_wait = { 5, 0 };
static struct event *updateev;

// Counts the number of changes made to the database between to DATABASE
// event notifications
static unsigned int deferred_update_notifications;
static short deferred_update_events;

static bool
handle_deferred_update_notifications(void)
{
  bool ret = (deferred_update_notifications > 0);

  if (ret)
    {
      DPRINTF(E_DBG, L_LIB, "Database changed (%d changes)\n", deferred_update_notifications);

      deferred_update_notifications = 0;
      db_admin_setint64(DB_ADMIN_DB_UPDATE, (int64_t) time(NULL));
    }

  return ret;
}

void
library_add_media(struct media_file_info *mfi)
{
  if (!mfi->path || !mfi->fname)
    {
      DPRINTF(E_LOG, L_LIB, "Ignoring media file with missing values (path='%s', fname='%s', data_kind='%d')\n",
	      mfi->path, mfi->fname, mfi->data_kind);
      return;
    }

  if (!mfi->directory_id || !mfi->virtual_path)
    {
      // Missing informations for virtual_path and directory_id (may) lead to misplaced appearance in mpd clients
      DPRINTF(E_WARN, L_LIB, "Media file with missing values (path='%s', directory='%d', virtual_path='%s')\n",
	      mfi->path, mfi->directory_id, mfi->virtual_path);
    }

  if (mfi->id == 0)
    db_file_add(mfi);
  else
    db_file_update(mfi);
}

int
library_queue_add(const char *path, int position, int *count, int *new_item_id)
{
  struct player_status status;
  int i;
  int ret;

  DPRINTF(E_DBG, L_LIB, "Add items for path '%s' to the queue\n", path);

  player_get_status(&status);

  ret = LIBRARY_PATH_INVALID;
  for (i = 0; sources[i] && ret == LIBRARY_PATH_INVALID; i++)
    {
      if (sources[i]->disabled || !sources[i]->queue_add)
        {
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support queue_add\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->queue_add(path, position, status.shuffle, status.item_id, count, new_item_id);

      if (ret == LIBRARY_OK)
	{
	  DPRINTF(E_DBG, L_LIB, "Items for path '%s' from library source '%s' added to the queue\n", path, sources[i]->name);
	  break;
	}
    }

  if (ret != LIBRARY_OK)
    DPRINTF(E_LOG, L_LIB, "Failed to add items for path '%s' to the queue (%d)\n", path, ret);

  return ret;
}

int
library_add_playlist_info(const char *path, const char *title, const char *virtual_path, enum pl_type type, int parent_pl_id, int dir_id)
{
  struct playlist_info *pli;
  int plid;
  int ret;

  pli = db_pl_fetch_bypath(path);
  if (pli)
    {
      DPRINTF(E_DBG, L_LIB, "Playlist found ('%s', link %s), updating\n", title, path);

      plid = pli->id;

      pli->type = type;
      free(pli->title);
      pli->title = strdup(title);
      if (pli->virtual_path)
	free(pli->virtual_path);
      pli->virtual_path = safe_strdup(virtual_path);
      pli->directory_id = dir_id;

      ret = db_pl_update(pli);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LIB, "Error updating playlist ('%s', link %s)\n", title, path);

	  free_pli(pli, 0);
	  return -1;
	}

      db_pl_clear_items(plid);
    }
  else
    {
      DPRINTF(E_DBG, L_LIB, "Adding playlist ('%s', link %s)\n", title, path);

      pli = (struct playlist_info *)malloc(sizeof(struct playlist_info));
      if (!pli)
	{
	  DPRINTF(E_LOG, L_LIB, "Out of memory\n");

	  return -1;
	}

      memset(pli, 0, sizeof(struct playlist_info));

      pli->type = type;
      pli->title = strdup(title);
      pli->path = strdup(path);
      pli->virtual_path = safe_strdup(virtual_path);
      pli->parent_id = parent_pl_id;
      pli->directory_id = dir_id;

      ret = db_pl_add(pli, &plid);
      if ((ret < 0) || (plid < 1))
	{
	  DPRINTF(E_LOG, L_LIB, "Error adding playlist ('%s', link %s, ret %d, plid %d)\n", title, path, ret, plid);

	  free_pli(pli, 0);
	  return -1;
	}
    }

  free_pli(pli, 0);
  return plid;
}

static void
purge_cruft(time_t start)
{
  DPRINTF(E_DBG, L_LIB, "Purging old library content\n");
  db_purge_cruft(start);
  db_groups_cleanup();
  db_queue_cleanup();

  DPRINTF(E_DBG, L_LIB, "Purging old artwork content\n");
  cache_artwork_purge_cruft(start);
}

static enum command_state
rescan(void *arg, int *ret)
{
  time_t starttime;
  time_t endtime;
  int i;

  DPRINTF(E_LOG, L_LIB, "Library rescan triggered\n");
  listener_notify(LISTENER_UPDATE);
  starttime = time(NULL);

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->rescan)
	{
	  DPRINTF(E_INFO, L_LIB, "Rescan library source '%s'\n", sources[i]->name);
	  sources[i]->rescan();
	}
      else
	{
	  DPRINTF(E_INFO, L_LIB, "Library source '%s' is disabled\n", sources[i]->name);
	}
    }

  purge_cruft(starttime);

  DPRINTF(E_DBG, L_LIB, "Running post library scan jobs\n");
  db_hook_post_scan();

  endtime = time(NULL);
  DPRINTF(E_LOG, L_LIB, "Library rescan completed in %.f sec (%d changes)\n", difftime(endtime, starttime), deferred_update_notifications);
  scanning = false;

  if (handle_deferred_update_notifications())
    listener_notify(LISTENER_UPDATE | LISTENER_DATABASE);
  else
    listener_notify(LISTENER_UPDATE);

  *ret = 0;
  return COMMAND_END;
}

static enum command_state
fullrescan(void *arg, int *ret)
{
  time_t starttime;
  time_t endtime;
  int i;

  DPRINTF(E_LOG, L_LIB, "Library full-rescan triggered\n");
  listener_notify(LISTENER_UPDATE);
  starttime = time(NULL);

  player_playback_stop();
  db_queue_clear(0);
  db_purge_all(); // Clears files, playlists, playlistitems, inotify and groups

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->fullrescan)
	{
	  DPRINTF(E_INFO, L_LIB, "Full-rescan library source '%s'\n", sources[i]->name);
	  sources[i]->fullrescan();
	}
      else
	{
	  DPRINTF(E_INFO, L_LIB, "Library source '%s' is disabled\n", sources[i]->name);
	}
    }

  endtime = time(NULL);
  DPRINTF(E_LOG, L_LIB, "Library full-rescan completed in %.f sec (%d changes)\n", difftime(endtime, starttime), deferred_update_notifications);
  scanning = false;

  if (handle_deferred_update_notifications())
    listener_notify(LISTENER_UPDATE | LISTENER_DATABASE);
  else
    listener_notify(LISTENER_UPDATE);

  *ret = 0;
  return COMMAND_END;
}

/*
 * Callback to notify listeners of database changes
 */
static void
update_trigger_cb(int fd, short what, void *arg)
{
  if (handle_deferred_update_notifications())
    {
      listener_notify(deferred_update_events);
      deferred_update_events = 0;
    }
}

static enum command_state
update_trigger(void *arg, int *retval)
{
  short *events = arg;

  ++deferred_update_notifications;
  deferred_update_events |= *events;

  // Only add the timer event if the update occurred outside a (init-/re-/fullre-) scan.
  // The scanning functions take care of notifying clients of database changes directly
  // after the scan finished.
  if (!scanning)
    evtimer_add(updateev, &library_update_wait);

  *retval = 0;
  return COMMAND_END;
}


/* --------------------------- LIBRARY INTERFACE -------------------------- */

void
library_rescan()
{
  if (scanning)
    {
      DPRINTF(E_INFO, L_LIB, "Scan already running, ignoring request to trigger a new init scan\n");
      return;
    }

  scanning = true; // TODO Guard "scanning" with a mutex
  commands_exec_async(cmdbase, rescan, NULL);
}

void
library_fullrescan()
{
  if (scanning)
    {
      DPRINTF(E_INFO, L_LIB, "Scan already running, ignoring request to trigger a new full rescan\n");
      return;
    }

  scanning = true; // TODO Guard "scanning" with a mutex
  commands_exec_async(cmdbase, fullrescan, NULL);
}

static void
initscan()
{
  time_t starttime;
  time_t endtime;
  bool clear_queue_disabled;
  int i;

  scanning = true;
  starttime = time(NULL);
  listener_notify(LISTENER_UPDATE);

  // Only clear the queue if enabled (default) in config
  clear_queue_disabled = cfg_getbool(cfg_getsec(cfg, "mpd"), "clear_queue_on_stop_disable");
  if (!clear_queue_disabled)
    {
      db_queue_clear(0);
    }

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->initscan)
	sources[i]->initscan();
    }

  if (! (cfg_getbool(cfg_getsec(cfg, "library"), "filescan_disable")))
    {
      purge_cruft(starttime);

      DPRINTF(E_DBG, L_LIB, "Running post library scan jobs\n");
      db_hook_post_scan();
    }

  endtime = time(NULL);
  DPRINTF(E_LOG, L_LIB, "Library init scan completed in %.f sec (%d changes)\n", difftime(endtime, starttime), deferred_update_notifications);

  scanning = false;

  if (handle_deferred_update_notifications())
    listener_notify(LISTENER_UPDATE | LISTENER_DATABASE);
  else
    listener_notify(LISTENER_UPDATE);
}

/*
 * @return true if scan is running, otherwise false
 */
bool
library_is_scanning()
{
  return scanning;
}

/*
 * @param is_scanning true if scan is running, otherwise false
 */
void
library_set_scanning(bool is_scanning)
{
  scanning = is_scanning;
}

/*
 * @return true if a running scan should be aborted due to imminent shutdown, otherwise false
 */
bool
library_is_exiting()
{
  return scan_exit;
}

/*
 * Trigger for sending the DATABASE event
 *
 * Needs to be called, if an update to the database (library tables) occurred. The DATABASE event
 * is emitted with the delay 'library_update_wait'. It is safe to call this function from any thread.
 */
void
library_update_trigger(short update_events)
{
  short *events;
  int ret;

  pthread_t current_thread = pthread_self();
  if (pthread_equal(current_thread, tid_library))
    {
      // We are already running in the library thread, it is safe to directly call update_trigger
      update_trigger(&update_events, &ret);
    }
  else
    {
      events = malloc(sizeof(short));
      *events = update_events;
      commands_exec_async(cmdbase, update_trigger, events);
    }
}

static enum command_state
playlist_add(void *arg, int *retval)
{
  struct playlist_add_param *param = arg;
  int i;
  int ret = LIBRARY_ERROR;

  DPRINTF(E_DBG, L_LIB, "Adding item '%s' to playlist '%s'\n", param->vp_item, param->vp_playlist);

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->disabled || !sources[i]->playlist_add)
	{
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support playlist_add\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->playlist_add(param->vp_playlist, param->vp_item);

      if (ret == LIBRARY_OK)
	{
	  DPRINTF(E_DBG, L_LIB, "Adding item '%s' to playlist '%s' with library source '%s'\n", param->vp_item, param->vp_playlist, sources[i]->name);
	  listener_notify(LISTENER_STORED_PLAYLIST);
	  break;
	}
    }

  *retval = ret;
  return COMMAND_END;
}

int
library_playlist_add(const char *vp_playlist, const char *vp_item)
{
  struct playlist_add_param param;

  if (library_is_scanning())
    return -1;

  param.vp_playlist = vp_playlist;
  param.vp_item = vp_item;
  return commands_exec_sync(cmdbase, playlist_add, NULL, &param);
}

static enum command_state
playlist_remove(void *arg, int *retval)
{
  const char *virtual_path;
  int i;
  int ret = LIBRARY_ERROR;

  virtual_path = arg;

  DPRINTF(E_DBG, L_LIB, "Removing playlist at path '%s'\n", virtual_path);

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->disabled || !sources[i]->playlist_remove)
	{
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support playlist_remove\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->playlist_remove(virtual_path);

      if (ret == LIBRARY_OK)
	{
	  DPRINTF(E_DBG, L_LIB, "Removing playlist '%s' with library source '%s'\n", virtual_path, sources[i]->name);
	  listener_notify(LISTENER_STORED_PLAYLIST);
	  break;
	}
    }

  *retval = ret;
  return COMMAND_END;
}

int
library_playlist_remove(char *virtual_path)
{
  if (library_is_scanning())
    return -1;

  return commands_exec_sync(cmdbase, playlist_remove, NULL, virtual_path);
}

static enum command_state
queue_save(void *arg, int *retval)
{
  const char *virtual_path;
  int i;
  int ret = LIBRARY_ERROR;

  virtual_path = arg;

  DPRINTF(E_DBG, L_LIB, "Saving queue to path '%s'\n", virtual_path);

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->disabled || !sources[i]->queue_save)
	{
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support queue_save\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->queue_save(virtual_path);

      if (ret == LIBRARY_OK)
	{
	  DPRINTF(E_DBG, L_LIB, "Saving queue to path '%s' with library source '%s'\n", virtual_path, sources[i]->name);
	  listener_notify(LISTENER_STORED_PLAYLIST);
	  break;
	}
    }

  *retval = ret;
  return COMMAND_END;
}

int
library_queue_save(char *path)
{
  if (library_is_scanning())
    return -1;

  return commands_exec_sync(cmdbase, queue_save, NULL, path);
}

/*
 * Execute the function 'func' with the given argument 'arg' in the library thread.
 *
 * The pointer passed as argument is freed in the library thread after func returned.
 *
 * @param func The function to be executed
 * @param arg Argument passed to func
 * @return 0 if triggering the function execution succeeded, -1 on failure.
 */
int
library_exec_async(command_function func, void *arg)
{
  return commands_exec_async(cmdbase, func, arg);
}

static void *
library(void *arg)
{
  int ret;

#ifdef __linux__
  struct sched_param param;

  /* Lower the priority of the thread so forked-daapd may still respond
   * during library scan on low power devices. Param must be 0 for the SCHED_BATCH
   * policy.
   */
  memset(&param, 0, sizeof(struct sched_param));
  ret = pthread_setschedparam(pthread_self(), SCHED_BATCH, &param);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_LIB, "Warning: Could not set thread priority to SCHED_BATCH\n");
    }
#endif

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LIB, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  initscan();

  event_base_dispatch(evbase_lib);

  if (!scan_exit)
    DPRINTF(E_FATAL, L_LIB, "Scan event loop terminated ahead of time!\n");

  db_hook_post_scan();
  db_perthread_deinit();

  pthread_exit(NULL);
}

/* Thread: main */
int
library_init(void)
{
  int i;
  int ret;

  scan_exit = false;
  scanning = false;

  CHECK_NULL(L_LIB, evbase_lib = event_base_new());
  CHECK_NULL(L_LIB, updateev = evtimer_new(evbase_lib, update_trigger_cb, NULL));

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->init)
	continue;

      ret = sources[i]->init();
      if (ret < 0)
	sources[i]->disabled = 1;
    }

  CHECK_NULL(L_LIB, cmdbase = commands_base_new(evbase_lib, NULL));

  CHECK_ERR(L_LIB, pthread_create(&tid_library, NULL, library, NULL));

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_library, "library");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_library, "library");
#endif

  return 0;
}

/* Thread: main */
void
library_deinit()
{
  int i;
  int ret;

  scan_exit = true;
  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_library, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_LIB, "Could not join library thread: %s\n", strerror(errno));

      return;
    }

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->deinit && !sources[i]->disabled)
      sources[i]->deinit();
    }

  event_base_free(evbase_lib);
}
