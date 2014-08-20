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


struct daapcache_command;

typedef int (*cmd_func)(struct daapcache_command *cmd);

struct daapcache_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;

  int nonblock;

  struct {
    const char *query;
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

/* --------------------------------- HELPERS ------------------------------- */


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
#define T_CACHE						\
  "CREATE TABLE IF NOT EXISTS cache ("			\
  "   id                 INTEGER PRIMARY KEY NOT NULL,"	\
  "   query              VARCHAR(4096) NOT NULL,"	\
  "   reply              BLOB"	\
  ");"
#define I_QUERY				\
  "CREATE INDEX IF NOT EXISTS idx_query ON cache(query);"
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

  // Create cache table
  ret = sqlite3_exec(g_db_hdl, T_CACHE, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DCACHE, "Error creating cache table: %s\n", errmsg);

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

/* Adds the reply in evbuf to the cache */
static int
daapcache_query_add(const char *query, struct evbuffer *evbuf)
{
#define Q_TMPL "INSERT INTO cache(query, reply) VALUES(?, ?);"
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

  return 0;
#undef Q_TMPL
}

/* Gets a reply from the cache */
static int
daapcache_query_get(struct daapcache_command *cmd)
{
#define Q_TMPL "SELECT reply FROM cache WHERE query = ?;"
  sqlite3_stmt *stmt;
  int datlen;
  int ret;

  cmd->arg.evbuf = NULL;

  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

  sqlite3_bind_text(stmt, 1, cmd->arg.query, -1, SQLITE_STATIC);

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

  return 0;

 error:
  sqlite3_finalize(stmt);
  return -1;  
#undef Q_TMPL
}

/* Here we actually update the cache by asking to httpd_daap for responses
 * to known queries
 */
static void
daapcache_update_cb(int fd, short what, void *arg)
{
  struct evbuffer *evbuf;
  char *errmsg;
  char *query = "test";
  int ret;

  DPRINTF(E_INFO, L_DCACHE, "Timeout reached, time to update DAAP cache\n");

  ret = sqlite3_exec(g_db_hdl, "DELETE FROM cache;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error clearing cache before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      return;
    }

  evbuf = daap_reply_build(query);
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DCACHE, "Error building DAAP reply for cache\n");
      return;
    }

  daapcache_query_add(query, evbuf);

  evbuffer_free(evbuf);
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
  cmd.arg.query = query;

  ret = sync_command(&cmd);

  evbuf = cmd.arg.evbuf;

  command_deinit(&cmd);

  return ((ret < 0) ? NULL : evbuf);
}

int
daapcache_init(void)
{
  int ret;

  g_db_path = cfg_getstr(cfg_getsec(cfg, "general"), "daapcache_path");
  if (!g_db_path)
    {
      DPRINTF(E_LOG, L_DCACHE, "Cache disabled\n");
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
