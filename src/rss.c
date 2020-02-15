/*
 * Copyright (C) 2020 whatdoineed2do/Ray <whatdoineed2do @ gmail com>
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

#include <event2/event.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif

#include "rss.h"
#include "conffile.h"
#include "db.h"
#include "library.h"
#include "listener.h"
#include "logger.h"
#include "library/filescanner.h"


static struct event_base *evbase_rss = NULL;
static struct commands_base *cmdbase = NULL;
static struct timeval rss_sync_interval = { 60, 0 };
static struct event *rss_syncev = NULL;


static pthread_t  rss_tid = -1;
const char *pl_dir = NULL;


// relevant fields from playlist tbl
struct rss_file_item {
  char *title;
  char *file;
  time_t lastupd;
  struct rss_file_item *next;
};

static void
free_rfi(struct rss_file_item* rfi)
{
  if (!rfi) return;

  struct rss_file_item *tmp;
  while (rfi)
  {
    tmp = rfi->next;
    free(rfi->file);
    free(rfi->title);
    free(rfi);
    rfi = tmp;
  }
}

// should only be called by rfi_add() and if you are getting a new list
static struct rss_file_item*
rfi_alloc()
{
  struct rss_file_item *obj = malloc(sizeof(struct rss_file_item));
  memset(obj, 0, sizeof(struct rss_file_item));
  return obj;
}

// returns the newly alloc'd / added item at end of list
static struct rss_file_item*
rfi_add(struct rss_file_item* head)
{
  struct rss_file_item *curr = head;
  while (curr->next)
    curr = curr->next;

  curr->next = rfi_alloc();
  return curr->next;
}


/* Thread: rss */
static void
rss_sync(int *retval)
{
  struct query_params query_params;
  struct db_playlist_info dbpli;
  struct rss_file_item*  rfi = NULL;
  struct rss_file_item*  head = NULL;
  time_t  now;
  int ret = 0;

  *retval = 0;

  DPRINTF(E_INFO, L_RSS, "refreshing RSS feeds\n");
  memset(&query_params, 0, sizeof(struct query_params));

  query_params.type = Q_PL;
  query_params.sort = S_PLAYLIST;
  query_params.filter = db_mprintf("(f.type = %d)", PL_RSS);

  // TODO - how to do this in FD framework???
  while (library_is_scanning())
  {
    DPRINTF(E_LOG, L_RSS, "DB scan in progress, waiting\n");
    sleep(10);
  }

  ret = db_query_start(&query_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RSS, "Failed to find current RSS feeds from db\n");
      *retval = ret;
      goto error;
    }

  while (((ret = db_query_fetch_pl(&query_params, &dbpli)) == 0) && (dbpli.id))
    {
      if (!rfi) 
      {
	rfi = rfi_alloc();
	head = rfi;
      }
      else
	rfi = rfi_add(rfi);

      rfi->title = strdup(dbpli.title);
      rfi->file  = strdup(dbpli.path);
      rfi->lastupd = atol(dbpli.db_timestamp);
    }
  db_query_end(&query_params);
  time(&now);

  library_set_scanning(true);
  rfi = head;
  while (rfi)
  {
    if (now < rfi->lastupd + rss_sync_interval.tv_sec)
      {
	DPRINTF(E_DBG, L_RSS, "Skipping %s  last update: %s", rfi->title, ctime(&(rfi->lastupd)));
      }
    else
      {
	DPRINTF(E_DBG, L_RSS, "Sync'ing %s  last update: %s", rfi->title, ctime(&(rfi->lastupd)));
        db_transaction_begin();
	scan_rss(rfi->file, time(&now), false);
        db_transaction_end();
      }
    rfi = rfi->next;
  }
  library_set_scanning(false);

 error:
  free(query_params.filter);
  free_rfi(head);

  evtimer_add(rss_syncev, &rss_sync_interval);
}

static enum command_state
rss_sync_cmd(void *arg, int *retval)
{
  rss_sync(retval);
  return COMMAND_END;
}

static void
rss_sync_cb(int fd, short what, void *arg)
{
  commands_exec_async(cmdbase, rss_sync_cmd, NULL);
}

static void *
rss(void *arg)
{
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RSS, "Error: DB init failed\n");

      pthread_exit(NULL);
    }
  event_base_dispatch(evbase_rss);

  db_perthread_deinit();

  pthread_exit(NULL);
}

int
rss_init()
{
  int ret;

  rss_sync_interval.tv_sec = cfg_getint(cfg_getsec(cfg, "rss"), "sync_period");
  if (rss_sync_interval.tv_sec < 60) 
    {
      DPRINTF(E_LOG, L_RSS, "RSS 'sync_period' too low, defaulting to 60seconds\n");
      rss_sync_interval.tv_sec = 60;
    }

  pl_dir = cfg_getstr(cfg_getsec(cfg, "library"), "default_playlist_directory");
  if (access(pl_dir, W_OK) < 0)
    {
      DPRINTF(E_LOG, L_RSS, "Config playlist dir is not writable, will disable saving of new RSS feeds\n");
      pl_dir = NULL;
    }

  evbase_rss = event_base_new();
  if (!evbase_rss)
    {
      DPRINTF(E_LOG, L_RSS, "Could not create an event base\n");
      goto evbase_fail;
    }
  rss_syncev= evtimer_new(evbase_rss, rss_sync_cb, NULL);
  if (!rss_syncev)
    {
      DPRINTF(E_LOG, L_RSS, "Could not create an event timer\n");
      goto evnew_fail;
    }

  cmdbase = commands_base_new(evbase_rss, NULL);

  DPRINTF(E_INFO, L_RSS, "RSS thread init\n");

  ret = pthread_create(&rss_tid, NULL, rss, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RSS, "Could not spawn RSS thread: %s\n", strerror(errno));
      goto thread_fail;
    }

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(rss_tid, "rss");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(rss_tid, "rss");
#endif

  evtimer_add(rss_syncev, &rss_sync_interval);
  
  return 0;

 thread_fail:
 evnew_fail:
  event_free(rss_syncev);
  event_base_free(evbase_rss);
  evbase_rss = NULL;

 evbase_fail:
  return -1;
}

void
rss_deinit()
{
  int ret;

  commands_base_destroy(cmdbase);
  ret = pthread_join(rss_tid, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_RSS, "Could not join RSS thread: %s\n", strerror(errno));
      return;
    }
  event_free(rss_syncev);
  event_base_free(evbase_rss);
}

int
rss_feed_create(const char *name, const char* url)
{
  char path[PATH_MAX];
  int fd;
  int len;
  int ret;

  if (!pl_dir || !name || !url)
    return -1;

  if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
    return -1;

  ret = snprintf(path, sizeof(path), "%s/%s.rss_url", pl_dir, name);
  if (ret < 0 || ret > sizeof(path))
    {
      DPRINTF(E_LOG, L_RSS, "Unable to create file path RSS feed: '%s' under '%s'\n", name, pl_dir);
      return -1;
    }
  DPRINTF(E_DBG, L_RSS, "Create RSS feed: '%s' as '%s' with url: %s\n", name, path, url);

  if ((fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, 0644)) < 0)
    {
      DPRINTF(E_LOG, L_RSS, "Unable to create file for RSS feed: '%s'\n", path);
      return -1;
    }

  ret = 0;
  len = strlen(url);
  if (write(fd, url, len) != len)
    {
      DPRINTF(E_LOG, L_RSS, "Failed to create RSS feed:'%s' : '%s'\n", path, strerror(errno));
      unlink(path);
      ret = -1;
    }

  close(fd);
  return ret;
}
