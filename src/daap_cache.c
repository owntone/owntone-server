/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

#include "conffile.h"
#include "logger.h"
#include "httpd_daap.h"
#include "db.h"
#include "daap_cache.h"

/* The DAAP cache will cache raw daap replies for queries added with
 * daapcache_add(). Only some query types are supported.
 * You can't add queries where the canonical reply is not HTTP_OK, because
 * daap_request will use that as default for cache replies.
 *
 */

struct daapcache_command;

typedef int (*cmd_func)(struct daapcache_command *cmd);

struct daapcache_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;

  int nonblock;

  struct {
    char *query;
    char *ua;
    int msec;
    struct evbuffer *evbuf;
  } arg;

  int ret;
};

/* --- Globals --- */
// daapcache thread
static pthread_t tid_daapcache;

// Event base, pipes and events
struct event_base *evbase_daapcache;
static int g_exit_pipe[2];
static int g_cmd_pipe[2];
static struct event *g_exitev;
static struct event *g_cmdev;
static struct event *g_cacheev;

static int g_initialized;

// Global cache database handle
static sqlite3 *g_db_hdl;
static char *g_db_path;

// After being triggered wait 5 seconds before rebuilding daapcache
static struct timeval g_wait = { 5, 0 };

// The user may configure a threshold (in msec), and queries slower than
// that will have their reply cached
static int g_cfg_threshold;

/* --------------------------------- HELPERS ------------------------------- */

/* The purpose of this function is to remove transient tags from a request 
 * url (query), eg remove session-id=xxx
 */
static void
remove_tag(char *in, const char *tag)
{
  char *s;
  char *e;

  s = strstr(in, tag);
  if (!s)
    return;

  e = strchr(s, '&');
  if (e)
    memmove(s, (e + 1), strlen(e + 1) + 1);
  else if (s > in)
    *(s - 1) = '\0';
}

/* ---------------------------- COMMAND EXECUTION -------------------------- */

static void
command_init(struct daapcache_command *cmd)
{
  memset(cmd, 0, sizeof(struct daapcache_command));

  pthread_mutex_init(&cmd->lck, NULL);
  pthread_cond_init(&cmd->cond, NULL);
}

static void
command_deinit(struct daapcache_command *cmd)
{
  pthread_cond_destroy(&cmd->cond);
  pthread_mutex_destroy(&cmd->lck);
}

static int
send_command(struct daapcache_command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      DPRINTF(E_LOG, L_DCACHE, "BUG: cmd->func is NULL!\n");
      return -1;
    }

  ret = write(g_cmd_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not send command: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

static int
sync_command(struct daapcache_command *cmd)
{
  int ret;

  pthread_mutex_lock(&cmd->lck);

  ret = send_command(cmd);
  if (ret < 0)
    {
      pthread_mutex_unlock(&cmd->lck);
      return -1;
    }

  pthread_cond_wait(&cmd->cond, &cmd->lck);
  pthread_mutex_unlock(&cmd->lck);

  ret = cmd->ret;

  return ret;
}

static int
nonblock_command(struct daapcache_command *cmd)
{
  int ret;

  ret = send_command(cmd);
  if (ret < 0)
    return -1;

  return 0;
}

static void
thread_exit(void)
{
  int dummy = 42;

  DPRINTF(E_DBG, L_DCACHE, "Killing daapcache thread\n");

  if (write(g_exit_pipe[1], &dummy, sizeof(dummy)) != sizeof(dummy))
    DPRINTF(E_LOG, L_DCACHE, "Could not write to exit fd: %s\n", strerror(errno));
}


/* --------------------------------- MAIN --------------------------------- */
/*                              Thread: daapcache                              */

static int
daapcache_create(void)
{
#define T_REPLIES						\
  "CREATE TABLE IF NOT EXISTS replies ("			\
  "   id                 INTEGER PRIMARY KEY NOT NULL,"		\
  "   query              VARCHAR(4096) NOT NULL,"		\
  "   reply              BLOB"					\
  ");"
#define T_QUERIES						\
  "CREATE TABLE IF NOT EXISTS queries ("			\
  "   id                 INTEGER PRIMARY KEY NOT NULL,"		\
  "   query              VARCHAR(4096) UNIQUE NOT NULL,"	\
  "   user_agent         VARCHAR(1024),"			\
  "   msec               INTEGER DEFAULT 0,"			\
  "   timestamp          INTEGER DEFAULT 0"			\
  ");"
#define I_QUERY							\
  "CREATE INDEX IF NOT EXISTS idx_query ON replies (query);"
  char *errmsg;
  int ret;

  // A fresh start
  unlink(g_db_path);

  // Create db
  ret = sqlite3_open(g_db_path, &g_db_hdl);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DCACHE, "Could not create database: %s\n", sqlite3_errmsg(g_db_hdl));

      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create reply cache table
  ret = sqlite3_exec(g_db_hdl, T_REPLIES, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DCACHE, "Error creating reply cache table: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create query table (the queries for which we will generate and cache replies)
  ret = sqlite3_exec(g_db_hdl, T_QUERIES, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DCACHE, "Error creating query table: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create index
  ret = sqlite3_exec(g_db_hdl, I_QUERY, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DCACHE, "Error creating query index: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  DPRINTF(E_DBG, L_DCACHE, "Cache created\n");

  return 0;
#undef T_CACHE
#undef I_QUERY
}

static void
daapcache_destroy(void)
{
  sqlite3_stmt *stmt;

  if (!g_db_hdl)
    return;

  /* Tear down anything that's in flight */
  while ((stmt = sqlite3_next_stmt(g_db_hdl, 0)))
    sqlite3_finalize(stmt);

  sqlite3_close(g_db_hdl);

  unlink(g_db_path);

  DPRINTF(E_DBG, L_DCACHE, "Cache destroyed\n");
}

/* Adds the reply (stored in evbuf) to the cache */
static int
daapcache_reply_add(const char *query, struct evbuffer *evbuf)
{
#define Q_TMPL "INSERT INTO replies (query, reply) VALUES (?, ?);"
  sqlite3_stmt *stmt;
  unsigned char *data;
  size_t datlen;
  int ret;

  datlen = evbuffer_get_length(evbuf);
  data = evbuffer_pullup(evbuf, -1);

  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, data, datlen, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error stepping query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      sqlite3_finalize(stmt);
      return -1;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error finalizing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

  DPRINTF(E_DBG, L_DCACHE, "Wrote cache reply, size %d\n", datlen);

  return 0;
#undef Q_TMPL
}

/* Adds the query to the list of queries for which we will build and cache a reply */
static int
daapcache_query_add(struct daapcache_command *cmd)
{
#define Q_TMPL "INSERT OR REPLACE INTO queries (user_agent, query, msec, timestamp) VALUES ('%q', '%q', %d, %" PRIi64 ");"
#define Q_CLEANUP "DELETE FROM queries WHERE id NOT IN (SELECT id FROM queries ORDER BY timestamp DESC LIMIT 20);"
  char *query;
  char *errmsg;
  int ret;

  if (!cmd->arg.ua)
    {
      DPRINTF(E_LOG, L_DCACHE, "Couldn't add slow query to cache, unknown user-agent\n");

      free(cmd->arg.query);
      return -1;
    }

  // Currently we are only able to pre-build and cache these reply types
  if ( (strncmp(cmd->arg.query, "/databases/1/containers/", strlen("/databases/1/containers/")) != 0) &&
       (strncmp(cmd->arg.query, "/databases/1/groups?", strlen("/databases/1/groups?")) != 0) &&
       (strncmp(cmd->arg.query, "/databases/1/items?", strlen("/databases/1/items?")) != 0) &&
       (strncmp(cmd->arg.query, "/databases/1/browse/", strlen("/databases/1/browse/")) != 0) )
    return -1;

  remove_tag(cmd->arg.query, "session-id");
  remove_tag(cmd->arg.query, "revision-number");

  query = sqlite3_mprintf(Q_TMPL, cmd->arg.ua, cmd->arg.query, cmd->arg.msec, (int64_t)time(NULL));
  if (!query)
    {
      DPRINTF(E_LOG, L_DCACHE, "Out of memory making query string.\n");

      return -1;
    }

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error adding query to query list: %s\n", errmsg);

      sqlite3_free(query);
      sqlite3_free(errmsg);
      return -1;
    }

  sqlite3_free(query);

  DPRINTF(E_INFO, L_DCACHE, "Slow query (%d ms) added to cache: '%s' (user-agent: '%s')\n", cmd->arg.msec, cmd->arg.query, cmd->arg.ua);

  free(cmd->arg.ua);
  free(cmd->arg.query);

  // Limits the size of the cache to only contain replies for 20 most recent queries
  ret = sqlite3_exec(g_db_hdl, Q_CLEANUP, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error cleaning up query list before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  daapcache_trigger();

  return 0;
#undef Q_CLEANUP
#undef Q_TMPL
}

/* Gets a reply from the cache */
static int
daapcache_query_get(struct daapcache_command *cmd)
{
#define Q_TMPL "SELECT reply FROM replies WHERE query = ?;"
  sqlite3_stmt *stmt;
  char *query;
  int datlen;
  int ret;

  cmd->arg.evbuf = NULL;

  query = cmd->arg.query;
  remove_tag(query, "session-id");
  remove_tag(query, "revision-number");

  // Look in the DB
  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      free(query);
      return -1;
    }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)  
    {
      if (ret != SQLITE_DONE)
	DPRINTF(E_LOG, L_DCACHE, "Error stepping query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      goto error;
    }

  datlen = sqlite3_column_bytes(stmt, 0);

  cmd->arg.evbuf = evbuffer_new();
  if (!cmd->arg.evbuf)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create reply evbuffer\n");
      goto error;
    }

  ret = evbuffer_add(cmd->arg.evbuf, sqlite3_column_blob(stmt, 0), datlen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DCACHE, "Out of memory for reply evbuffer\n");
      evbuffer_free(cmd->arg.evbuf);
      cmd->arg.evbuf = NULL;
      goto error;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    DPRINTF(E_LOG, L_DCACHE, "Error finalizing query for getting cache: %s\n", sqlite3_errmsg(g_db_hdl));

  DPRINTF(E_INFO, L_DCACHE, "Cache hit: %s\n", query);

  free(query);

  return 0;

 error:
  sqlite3_finalize(stmt);
  free(query);
  return -1;  
#undef Q_TMPL
}

/* Here we actually update the cache by asking httpd_daap for responses
 * to the queries set for caching
 */
static void
daapcache_update_cb(int fd, short what, void *arg)
{
  sqlite3_stmt *stmt;
  struct evbuffer *evbuf;
  char *errmsg;
  char *query;
  int ret;

  DPRINTF(E_INFO, L_DCACHE, "Timeout reached, time to update DAAP cache\n");

  ret = sqlite3_exec(g_db_hdl, "DELETE FROM replies;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error clearing reply cache before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      return;
    }

  ret = sqlite3_prepare_v2(g_db_hdl, "SELECT user_agent, query FROM queries;", -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error preparing for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return;
    }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      query = strdup((char *)sqlite3_column_text(stmt, 1));

      evbuf = daap_reply_build(query, (char *)sqlite3_column_text(stmt, 0));
      if (!evbuf)
	{
	  DPRINTF(E_LOG, L_DCACHE, "Error building DAAP reply for query: %s\n", query);
	  free(query);
	  continue;
	}

      daapcache_reply_add(query, evbuf);

      free(query);
      evbuffer_free(evbuf);
    }

  if (ret != SQLITE_DONE)
    DPRINTF(E_LOG, L_DCACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));

  sqlite3_finalize(stmt);

  DPRINTF(E_INFO, L_DCACHE, "DAAP cache updated\n");
}

/* This function will just set a timer, which when it times out will trigger
 * the actual daapcache update. The purpose is to avoid avoid daapcache updates when
 * the database is busy, eg during a library scan.
 */
static int
daapcache_update_timer(struct daapcache_command *cmd)
{
  if (!g_cacheev)
    return -1;

  evtimer_add(g_cacheev, &g_wait);

  return 0;
}

static void *
daapcache(void *arg)
{
  int ret;

  ret = daapcache_create();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error: Cache create failed\n");
      pthread_exit(NULL);
    }

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error: DB init failed\n");
      daapcache_destroy();

      pthread_exit(NULL);
    }

  g_initialized = 1;

  event_base_dispatch(evbase_daapcache);

  if (g_initialized)
    {
      DPRINTF(E_LOG, L_DCACHE, "daapcache event loop terminated ahead of time!\n");
      g_initialized = 0;
    }

  db_perthread_deinit();

  daapcache_destroy();

  pthread_exit(NULL);
}

static void
exit_cb(int fd, short what, void *arg)
{
  int dummy;
  int ret;

  ret = read(g_exit_pipe[0], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_DCACHE, "Error reading from exit pipe\n");

  event_base_loopbreak(evbase_daapcache);

  g_initialized = 0;

  event_add(g_exitev, NULL);
}

static void
command_cb(int fd, short what, void *arg)
{
  struct daapcache_command *cmd;
  int ret;

  ret = read(g_cmd_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not read command! (read %d): %s\n", ret, (ret < 0) ? strerror(errno) : "-no error-");
      goto readd;
    }

  if (cmd->nonblock)
    {
      cmd->func(cmd);

      free(cmd);
      goto readd;
    }

  pthread_mutex_lock(&cmd->lck);

  ret = cmd->func(cmd);
  cmd->ret = ret;

  pthread_cond_signal(&cmd->cond);
  pthread_mutex_unlock(&cmd->lck);

 readd:
  event_add(g_cmdev, NULL);
}



/* ---------------------------- Our daapcache API  --------------------------- */

void
daapcache_trigger(void)
{
  struct daapcache_command *cmd; 

  if (!g_initialized)
    return;

  cmd = (struct daapcache_command *)malloc(sizeof(struct daapcache_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not allocate daapcache_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct daapcache_command));

  cmd->nonblock = 1;

  cmd->func = daapcache_update_timer;

  nonblock_command(cmd);
}

struct evbuffer *
daapcache_get(const char *query)
{
  struct daapcache_command cmd;
  struct evbuffer *evbuf;
  int ret;

  if (!g_initialized)
    return NULL;

  command_init(&cmd);

  cmd.func = daapcache_query_get;
  cmd.arg.query = strdup(query);

  ret = sync_command(&cmd);

  evbuf = cmd.arg.evbuf;

  command_deinit(&cmd);

  return ((ret < 0) ? NULL : evbuf);
}

void
daapcache_add(const char *query, const char *ua, int msec)
{
  struct daapcache_command *cmd; 

  if (!g_initialized)
    return;

  cmd = (struct daapcache_command *)malloc(sizeof(struct daapcache_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not allocate daapcache_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct daapcache_command));

  cmd->nonblock = 1;

  cmd->func = daapcache_query_add;
  cmd->arg.query = strdup(query);
  cmd->arg.ua = strdup(ua);
  cmd->arg.msec = msec;

  nonblock_command(cmd);
}

int
daapcache_threshold(void)
{
  return g_cfg_threshold;
}

int
daapcache_init(void)
{
  int ret;

  g_db_path = cfg_getstr(cfg_getsec(cfg, "general"), "daapcache_path");
  if (!g_db_path || (strlen(g_db_path) == 0))
    {
      DPRINTF(E_LOG, L_DCACHE, "Cache path invalid, disabling cache\n");
      g_initialized = 0;
      return 0;
    }

  g_cfg_threshold = cfg_getint(cfg_getsec(cfg, "general"), "daapcache_threshold");
  if (g_cfg_threshold == 0)
    {
      DPRINTF(E_LOG, L_DCACHE, "Cache threshold set to 0, disabling cache\n");
      g_initialized = 0;
      return 0;
    }

# if defined(__linux__)
  ret = pipe2(g_exit_pipe, O_CLOEXEC);
# else
  ret = pipe(g_exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create pipe: %s\n", strerror(errno));
      goto exit_fail;
    }

# if defined(__linux__)
  ret = pipe2(g_cmd_pipe, O_CLOEXEC);
# else
  ret = pipe(g_cmd_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create command pipe: %s\n", strerror(errno));
      goto cmd_fail;
    }

  evbase_daapcache = event_base_new();
  if (!evbase_daapcache)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create an event base\n");
      goto evbase_fail;
    }

#ifdef HAVE_LIBEVENT2
  g_exitev = event_new(evbase_daapcache, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create exit event\n");
      goto evnew_fail;
    }

  g_cmdev = event_new(evbase_daapcache, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create cmd event\n");
      goto evnew_fail;
    }

  g_cacheev = evtimer_new(evbase_daapcache, daapcache_update_cb, NULL);
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create daapcache event\n");
      goto evnew_fail;
    }
#else
  g_exitev = (struct event *)malloc(sizeof(struct event));
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create exit event\n");
      goto evnew_fail;
    }
  event_set(g_exitev, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  event_base_set(evbase_daapcache, g_exitev);

  g_cmdev = (struct event *)malloc(sizeof(struct event));
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create cmd event\n");
      goto evnew_fail;
    }
  event_set(g_cmdev, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  event_base_set(evbase_daapcache, g_cmdev);

  g_cacheev = (struct event *)malloc(sizeof(struct event));
  if (!g_cacheev)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not create daapcache event\n");
      goto evnew_fail;
    }
  event_set(g_cacheev, -1, EV_TIMEOUT, daapcache_update_cb, NULL);
  event_base_set(evbase_daapcache, g_cacheev);
#endif

  event_add(g_exitev, NULL);
  event_add(g_cmdev, NULL);

  DPRINTF(E_INFO, L_DCACHE, "daapcache thread init\n");

  ret = pthread_create(&tid_daapcache, NULL, daapcache, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DCACHE, "Could not spawn daapcache thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;
  
 thread_fail:
 evnew_fail:
  event_base_free(evbase_daapcache);
  evbase_daapcache = NULL;

 evbase_fail:
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);

 cmd_fail:
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);

 exit_fail:
  return -1;
}

void
daapcache_deinit(void)
{
  int ret;

  if (!g_initialized)
    return;

  thread_exit();

  ret = pthread_join(tid_daapcache, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_DCACHE, "Could not join daapcache thread: %s\n", strerror(errno));
      return;
    }

  // Free event base (should free events too)
  event_base_free(evbase_daapcache);

  // Close pipes
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);
}
