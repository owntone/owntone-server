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
#include "cache.h"


#define CACHE_VERSION 2

struct cache_command;

typedef int (*cmd_func)(struct cache_command *cmd);

struct cache_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;

  int nonblock;

  struct {
    char *query; // daap query
    char *ua;    // user agent
    int msec;

    char *path;  // artwork path
    int type;    // individual or group artwork
    int64_t persistentid;
    int max_w;
    int max_h;
    int format;
    time_t mtime;
    int cached;
    int del;

    struct evbuffer *evbuf;
  } arg;

  int ret;
};

/* --- Globals --- */
// cache thread
static pthread_t tid_cache;

// Event base, pipes and events
struct event_base *evbase_cache;
static int g_exit_pipe[2];
static int g_cmd_pipe[2];
static struct event *g_exitev;
static struct event *g_cmdev;
static struct event *g_cacheev;

static int g_initialized;

// Global cache database handle
static sqlite3 *g_db_hdl;
static char *g_db_path;

// Global artwork stash
struct stash
{
  char *path;
  int format;
  size_t size;
  uint8_t *data;
} g_stash;

// After being triggered wait 60 seconds before rebuilding cache
static struct timeval g_wait = { 60, 0 };

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
command_init(struct cache_command *cmd)
{
  memset(cmd, 0, sizeof(struct cache_command));

  pthread_mutex_init(&cmd->lck, NULL);
  pthread_cond_init(&cmd->cond, NULL);
}

static void
command_deinit(struct cache_command *cmd)
{
  pthread_cond_destroy(&cmd->cond);
  pthread_mutex_destroy(&cmd->lck);
}

static int
send_command(struct cache_command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      DPRINTF(E_LOG, L_CACHE, "BUG: cmd->func is NULL!\n");
      return -1;
    }

  ret = write(g_cmd_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_CACHE, "Could not send command: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

static int
sync_command(struct cache_command *cmd)
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
nonblock_command(struct cache_command *cmd)
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

  DPRINTF(E_DBG, L_CACHE, "Killing cache thread\n");

  if (write(g_exit_pipe[1], &dummy, sizeof(dummy)) != sizeof(dummy))
    DPRINTF(E_LOG, L_CACHE, "Could not write to exit fd: %s\n", strerror(errno));
}


/* --------------------------------- MAIN --------------------------------- */
/*                              Thread: cache                              */

static int
cache_create_tables(void)
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
#define T_ARTWORK					\
  "CREATE TABLE IF NOT EXISTS artwork ("		\
  "   id                  INTEGER PRIMARY KEY NOT NULL,"\
  "   type                INTEGER NOT NULL DEFAULT 0,"  \
  "   persistentid        INTEGER NOT NULL,"		\
  "   max_w               INTEGER NOT NULL,"		\
  "   max_h               INTEGER NOT NULL,"		\
  "   format              INTEGER NOT NULL,"		\
  "   filepath            VARCHAR(4096) NOT NULL,"	\
  "   db_timestamp        INTEGER DEFAULT 0,"		\
  "   data                BLOB"				\
  ");"
#define I_ARTWORK_ID				\
  "CREATE INDEX IF NOT EXISTS idx_persistentidwh ON artwork(type, persistentid, max_w, max_h);"
#define I_ARTWORK_PATH				\
  "CREATE INDEX IF NOT EXISTS idx_pathtime ON artwork(filepath, db_timestamp);"
#define T_ADMIN_CACHE	\
  "CREATE TABLE IF NOT EXISTS admin_cache("	\
  " key VARCHAR(32) PRIMARY KEY NOT NULL,"	\
  " value VARCHAR(32) NOT NULL"	\
  ");"
#define Q_CACHE_VERSION	\
  "INSERT INTO admin_cache (key, value) VALUES ('cache_version', '%d');"

  char *query;
  char *errmsg;
  int ret;


  // Create reply cache table
  ret = sqlite3_exec(g_db_hdl, T_REPLIES, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error creating cache table 'replies': %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create query table (the queries for which we will generate and cache replies)
  ret = sqlite3_exec(g_db_hdl, T_QUERIES, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error creating cache table 'queries': %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create index
  ret = sqlite3_exec(g_db_hdl, I_QUERY, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error creating index on replies(query): %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create artwork table
  ret = sqlite3_exec(g_db_hdl, T_ARTWORK, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error creating cache table 'artwork': %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create index
  ret = sqlite3_exec(g_db_hdl, I_ARTWORK_ID, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error creating index on artwork(type, persistentid, max_w, max_h): %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }
  ret = sqlite3_exec(g_db_hdl, I_ARTWORK_PATH, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error creating index on artwork(filepath, db_timestamp): %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Create admin cache table
  ret = sqlite3_exec(g_db_hdl, T_ADMIN_CACHE, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error creating cache table 'admin_cache': %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }
  query = sqlite3_mprintf(Q_CACHE_VERSION, CACHE_VERSION);
  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error inserting cache version: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  sqlite3_free(query);

  DPRINTF(E_DBG, L_CACHE, "Cache tables created\n");

  return 0;
#undef T_REPLIES
#undef T_QUERIES
#undef I_QUERY
#undef T_ARTWORK
#undef I_ARTWORK_ID
#undef I_ARTWORK_PATH
#undef T_ADMIN_CACHE
#undef Q_CACHE_VERSION
}

static int
cache_drop_tables(void)
{
#define D_REPLIES	"DROP TABLE IF EXISTS replies;"
#define D_QUERIES	"DROP TABLE IF EXISTS queries;"
#define D_QUERY		"DROP INDEX IF EXISTS idx_query;"
#define D_ARTWORK	"DROP TABLE IF EXISTS artwork;"
#define D_ARTWORK_ID	"DROP INDEX IF EXISTS idx_persistentidwh;"
#define D_ARTWORK_PATH	"DROP INDEX IF EXISTS idx_pathtime;"
#define D_ADMIN_CACHE	"DROP TABLE IF EXISTS admin_cache;"
#define Q_VACUUM	"VACUUM;"

  char *errmsg;
  int ret;


  // Drop reply cache table
  ret = sqlite3_exec(g_db_hdl, D_REPLIES, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error dropping reply cache table: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Drop query table
  ret = sqlite3_exec(g_db_hdl, D_QUERIES, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error dropping query table: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Drop index
  ret = sqlite3_exec(g_db_hdl, D_QUERY, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error dropping query index: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Drop artwork table
  ret = sqlite3_exec(g_db_hdl, D_ARTWORK, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error dropping artwork table: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Drop index
  ret = sqlite3_exec(g_db_hdl, D_ARTWORK_ID, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error dropping artwork id index: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }
  ret = sqlite3_exec(g_db_hdl, D_ARTWORK_PATH, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error dropping artwork path index: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Drop admin cache table
  ret = sqlite3_exec(g_db_hdl, D_ADMIN_CACHE, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error dropping admin cache table: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Vacuum
  ret = sqlite3_exec(g_db_hdl, Q_VACUUM, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error vacuuming cache database: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  DPRINTF(E_DBG, L_CACHE, "Cache tables dropped\n");

  return 0;
#undef D_REPLIES
#undef D_QUERIES
#undef D_QUERY
#undef D_ARTWORK
#undef D_ARTWORK_ID
#undef D_ARTWORK_PATH
#undef D_ADMIN_CACHE
#undef Q_VACUUM
}

/*
 * Compares the CACHE_VERSION against the version stored in the cache admin table.
 * Drops the tables and indexes if the versions are different.
 *
 * @return 0 if versions are equal, 1 if versions are different or the admin table does not exist, -1 if an error occurred
 */
static int
cache_check_version(void)
{
#define Q_VER "SELECT value FROM admin_cache WHERE key = 'cache_version';"
  sqlite3_stmt *stmt;
  int cur_ver;
  int ret;

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", Q_VER);

  ret = sqlite3_prepare_v2(g_db_hdl, Q_VER, strlen(Q_VER) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_WARN, L_CACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(g_db_hdl));
      return 1;
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));
      sqlite3_finalize(stmt);
      return -1;
    }

  cur_ver = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (cur_ver != CACHE_VERSION)
    {
      DPRINTF(E_LOG, L_CACHE, "Database schema outdated, deleting cache v%d -> v%d\n", cur_ver, CACHE_VERSION);
      ret = cache_drop_tables();
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error deleting database tables\n");
	  return -1;
	}
      return 1;
    }
  return 0;
#undef Q_VER
}

static int
cache_create(void)
{
#define Q_PRAGMA_CACHE_SIZE "PRAGMA cache_size=%d;"
#define Q_PRAGMA_JOURNAL_MODE "PRAGMA journal_mode=%s;"
#define Q_PRAGMA_SYNCHRONOUS "PRAGMA synchronous=%d;"
  char *errmsg;
  int ret;
  int cache_size;
  char *journal_mode;
  int synchronous;
  char *query;

  // Open db
  ret = sqlite3_open(g_db_path, &g_db_hdl);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not open cache database: %s\n", sqlite3_errmsg(g_db_hdl));

      sqlite3_close(g_db_hdl);
      return -1;
    }

  // Check cache version
  ret = cache_check_version();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not check cache database version\n");

      sqlite3_close(g_db_hdl);
      return -1;
    }
  else if (ret > 0)
    {
      ret = cache_create_tables();
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_CACHE, "Could not create cache database tables\n");

	  sqlite3_close(g_db_hdl);
	  return -1;
	}
    }

  // Set page cache size in number of pages
  cache_size = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_cache_size_cache");
  if (cache_size > -1)
    {
      query = sqlite3_mprintf(Q_PRAGMA_CACHE_SIZE, cache_size);
      ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_cache_size_cache: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_close(g_db_hdl);
	  return -1;
	}
    }

  // Set journal mode
  journal_mode = cfg_getstr(cfg_getsec(cfg, "sqlite"), "pragma_journal_mode");
  if (journal_mode)
    {
      query = sqlite3_mprintf(Q_PRAGMA_JOURNAL_MODE, journal_mode);
      ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_journal_mode: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_close(g_db_hdl);
	  return -1;
	}
    }

  // Set synchronous flag
  synchronous = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_synchronous");
  if (synchronous > -1)
    {
      query = sqlite3_mprintf(Q_PRAGMA_SYNCHRONOUS, synchronous);
      ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_synchronous: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_close(g_db_hdl);
	  return -1;
	}
    }

  DPRINTF(E_DBG, L_CACHE, "Cache created\n");

  return 0;
#undef Q_PRAGMA_CACHE_SIZE
#undef Q_PRAGMA_JOURNAL_MODE
#undef Q_PRAGMA_SYNCHRONOUS
}

static void
cache_close(void)
{
  sqlite3_stmt *stmt;

  if (!g_db_hdl)
    return;

  /* Tear down anything that's in flight */
  while ((stmt = sqlite3_next_stmt(g_db_hdl, 0)))
    sqlite3_finalize(stmt);

  sqlite3_close(g_db_hdl);

  DPRINTF(E_DBG, L_CACHE, "Cache closed\n");
}

/* Adds the reply (stored in evbuf) to the cache */
static int
cache_daap_reply_add(const char *query, struct evbuffer *evbuf)
{
#define Q_TMPL "INSERT INTO replies (query, reply) VALUES (?, ?);"
  sqlite3_stmt *stmt;
  unsigned char *data;
  size_t datalen;
  int ret;

#ifdef HAVE_LIBEVENT2
  datalen = evbuffer_get_length(evbuf);
  data = evbuffer_pullup(evbuf, -1);
#else
  datalen = EVBUFFER_LENGTH(evbuf);
  data = EVBUFFER_DATA(evbuf);
#endif

  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, data, datalen, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_CACHE, "Error stepping query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      sqlite3_finalize(stmt);
      return -1;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error finalizing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

  //DPRINTF(E_DBG, L_CACHE, "Wrote cache reply, size %d\n", datalen);

  return 0;
#undef Q_TMPL
}

/* Adds the query to the list of queries for which we will build and cache a reply */
static int
cache_daap_query_add(struct cache_command *cmd)
{
#define Q_TMPL "INSERT OR REPLACE INTO queries (user_agent, query, msec, timestamp) VALUES ('%q', '%q', %d, %" PRIi64 ");"
#define Q_CLEANUP "DELETE FROM queries WHERE id NOT IN (SELECT id FROM queries ORDER BY timestamp DESC LIMIT 20);"
  char *query;
  char *errmsg;
  int ret;

  if (!cmd->arg.ua)
    {
      DPRINTF(E_LOG, L_CACHE, "Couldn't add slow query to cache, unknown user-agent\n");

      goto error_add;
    }

  // Currently we are only able to pre-build and cache these reply types
  if ( (strncmp(cmd->arg.query, "/databases/1/containers/", strlen("/databases/1/containers/")) != 0) &&
       (strncmp(cmd->arg.query, "/databases/1/groups?", strlen("/databases/1/groups?")) != 0) &&
       (strncmp(cmd->arg.query, "/databases/1/items?", strlen("/databases/1/items?")) != 0) &&
       (strncmp(cmd->arg.query, "/databases/1/browse/", strlen("/databases/1/browse/")) != 0) )
    goto error_add;

  remove_tag(cmd->arg.query, "session-id");
  remove_tag(cmd->arg.query, "revision-number");

  query = sqlite3_mprintf(Q_TMPL, cmd->arg.ua, cmd->arg.query, cmd->arg.msec, (int64_t)time(NULL));
  if (!query)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory making query string.\n");

      goto error_add;
    }

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error adding query to query list: %s\n", errmsg);

      sqlite3_free(query);
      sqlite3_free(errmsg);
      goto error_add;
    }

  sqlite3_free(query);

  DPRINTF(E_INFO, L_CACHE, "Slow query (%d ms) added to cache: '%s' (user-agent: '%s')\n", cmd->arg.msec, cmd->arg.query, cmd->arg.ua);

  free(cmd->arg.ua);
  free(cmd->arg.query);

  // Limits the size of the cache to only contain replies for 20 most recent queries
  ret = sqlite3_exec(g_db_hdl, Q_CLEANUP, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error cleaning up query list before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  cache_daap_trigger();

  return 0;

 error_add:
  if (cmd->arg.ua)
    free(cmd->arg.ua);

  if (cmd->arg.query)
    free(cmd->arg.query);

  return -1;
#undef Q_CLEANUP
#undef Q_TMPL
}

/* Gets a reply from the cache */
static int
cache_daap_query_get(struct cache_command *cmd)
{
#define Q_TMPL "SELECT reply FROM replies WHERE query = ?;"
  sqlite3_stmt *stmt;
  char *query;
  int datalen;
  int ret;

  query = cmd->arg.query;
  remove_tag(query, "session-id");
  remove_tag(query, "revision-number");

  // Look in the DB
  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      free(query);
      return -1;
    }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)  
    {
      if (ret != SQLITE_DONE)
	DPRINTF(E_LOG, L_CACHE, "Error stepping query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      goto error_get;
    }

  datalen = sqlite3_column_bytes(stmt, 0);

  if (!cmd->arg.evbuf)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: DAAP reply evbuffer is NULL\n");
      goto error_get;
    }

  ret = evbuffer_add(cmd->arg.evbuf, sqlite3_column_blob(stmt, 0), datalen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for DAAP reply evbuffer\n");
      goto error_get;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    DPRINTF(E_LOG, L_CACHE, "Error finalizing query for getting cache: %s\n", sqlite3_errmsg(g_db_hdl));

  DPRINTF(E_INFO, L_CACHE, "Cache hit: %s\n", query);

  free(query);

  return 0;

 error_get:
  sqlite3_finalize(stmt);
  free(query);
  return -1;  
#undef Q_TMPL
}

/* Removes the query from the cache */
static int
cache_daap_query_delete(const int id)
{
#define Q_TMPL "DELETE FROM queries WHERE id = %d;"
  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, id);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error deleting query from cache: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);
  return 0;
#undef Q_TMPL
}

/* Here we actually update the cache by asking httpd_daap for responses
 * to the queries set for caching
 */
static void
cache_daap_update_cb(int fd, short what, void *arg)
{
  sqlite3_stmt *stmt;
  struct evbuffer *evbuf;
  char *errmsg;
  char *query;
  int ret;

  DPRINTF(E_INFO, L_CACHE, "Timeout reached, time to update DAAP cache\n");

  ret = sqlite3_exec(g_db_hdl, "DELETE FROM replies;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error clearing reply cache before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      return;
    }

  ret = sqlite3_prepare_v2(g_db_hdl, "SELECT id, user_agent, query FROM queries;", -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return;
    }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      query = strdup((char *)sqlite3_column_text(stmt, 2));

      evbuf = daap_reply_build(query, (char *)sqlite3_column_text(stmt, 1));
      if (!evbuf)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error building DAAP reply for query: %s\n", query);
	  cache_daap_query_delete(sqlite3_column_int(stmt, 0));
	  free(query);

	  continue;
	}

      cache_daap_reply_add(query, evbuf);

      free(query);
      evbuffer_free(evbuf);
    }

  if (ret != SQLITE_DONE)
    DPRINTF(E_LOG, L_CACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));

  sqlite3_finalize(stmt);

  DPRINTF(E_INFO, L_CACHE, "DAAP cache updated\n");
}

/* This function will just set a timer, which when it times out will trigger
 * the actual cache update. The purpose is to avoid avoid cache updates when
 * the database is busy, eg during a library scan.
 */
static int
cache_daap_update_timer(struct cache_command *cmd)
{
  if (!g_cacheev)
    return -1;

  evtimer_add(g_cacheev, &g_wait);

  return 0;
}


/*
 * Updates cached timestamps to current time for all cache entries for the given path, if the file was not modfied
 * after the cached timestamp. All cache entries for the given path are deleted, if the file was
 * modified after the cached timestamp.
 *
 * @param cmd->arg.path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @param cmd->arg.mtime modified timestamp of the artwork file
 * @return 0 if successful, -1 if an error occurred
 */
static int
cache_artwork_ping_impl(struct cache_command *cmd)
{
#define Q_TMPL_PING "UPDATE artwork SET db_timestamp = %" PRIi64 " WHERE filepath = '%q' AND db_timestamp >= %" PRIi64 ";"
#define Q_TMPL_DEL "DELETE FROM artwork WHERE filepath = '%q' AND db_timestamp < %" PRIi64 ";"

  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL_PING, (int64_t)time(NULL), cmd->arg.path, (int64_t)cmd->arg.mtime);

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  if (cmd->arg.del > 0)
    {
      query = sqlite3_mprintf(Q_TMPL_DEL, cmd->arg.path, (int64_t)cmd->arg.mtime);

      DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

      ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_free(query);
	  return -1;
	}

      sqlite3_free(query);
    }

  return 0;

#undef Q_TMPL_PING
#undef Q_TMPL_DEL
}

/*
 * Removes all cache entries for the given path
 *
 * @param cmd->arg.path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @return 0 if successful, -1 if an error occurred
 */
static int
cache_artwork_delete_by_path_impl(struct cache_command *cmd)
{
#define Q_TMPL_DEL "DELETE FROM artwork WHERE filepath = '%q';"

  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL_DEL, cmd->arg.path);

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  return 0;

#undef Q_TMPL_DEL
}

/*
 * Removes all cache entries with cached timestamp older than the given reference timestamp
 *
 * @param cmd->arg.mtime reference timestamp
 * @return 0 if successful, -1 if an error occurred
 */
static int
cache_artwork_purge_cruft_impl(struct cache_command *cmd)
{
#define Q_TMPL "DELETE FROM artwork WHERE db_timestamp < %" PRIi64 ";"

  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)cmd->arg.mtime);

  DPRINTF(E_DBG, L_CACHE, "Running purge query '%s'\n", query);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  DPRINTF(E_DBG, L_CACHE, "Purged %d rows\n", sqlite3_changes(g_db_hdl));

  sqlite3_free(query);

  return 0;

#undef Q_TMPL
}

/*
 * Adds the given (scaled) artwork image to the artwork cache
 *
 * @param cmd->arg.persistentid persistent songalbumid or songartistid
 * @param cmd->arg.max_w maximum image width
 * @param cmd->arg.max_h maximum image height
 * @param cmd->arg.format ART_FMT_PNG for png, ART_FMT_JPEG for jpeg or 0 if no artwork available
 * @param cmd->arg.filename the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork) or empty if no artwork available
 * @param cmd->arg.evbuf event buffer containing the (scaled) image
 * @return 0 if successful, -1 if an error occurred
 */
static int
cache_artwork_add_impl(struct cache_command *cmd)
{
  sqlite3_stmt *stmt;
  char *query;
  uint8_t *data;
  int datalen;
  int ret;

  query = "INSERT INTO artwork (id, persistentid, max_w, max_h, format, filepath, db_timestamp, data, type) VALUES (NULL, ?, ?, ?, ?, ?, ?, ?, ?);";

  ret = sqlite3_prepare_v2(g_db_hdl, query, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

#ifdef HAVE_LIBEVENT2
  datalen = evbuffer_get_length(cmd->arg.evbuf);
  data = evbuffer_pullup(cmd->arg.evbuf, -1);
#else
  datalen = EVBUFFER_LENGTH(cmd->arg.evbuf);
  data = EVBUFFER_DATA(cmd->arg.evbuf);
#endif

  sqlite3_bind_int64(stmt, 1, cmd->arg.persistentid);
  sqlite3_bind_int(stmt, 2, cmd->arg.max_w);
  sqlite3_bind_int(stmt, 3, cmd->arg.max_h);
  sqlite3_bind_int(stmt, 4, cmd->arg.format);
  sqlite3_bind_text(stmt, 5, cmd->arg.path, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 6, (uint64_t)time(NULL));
  sqlite3_bind_blob(stmt, 7, data, datalen, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 8, cmd->arg.type);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_CACHE, "Error stepping query for artwork add: %s\n", sqlite3_errmsg(g_db_hdl));
      sqlite3_finalize(stmt);
      return -1;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error finalizing query for artwork add: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

  return 0;
}

/*
 * Get the cached artwork image for the given persistentid and maximum width/height
 *
 * If there is a cached entry for the given id and width/height, the parameter cached is set to 1.
 * In this case format and data contain the cached values.
 *
 * @param cmd->arg.type individual or group artwork
 * @param cmd->arg.persistentid persistent itemid, songalbumid or songartistid
 * @param cmd->arg.max_w maximum image width
 * @param cmd->arg.max_h maximum image height
 * @param cmd->arg.cached set by this function to 0 if no cache entry exists, otherwise 1
 * @param cmd->arg.format set by this function to the format of the cache entry
 * @param cmd->arg.evbuf event buffer filled by this function with the scaled image
 * @return 0 if successful, -1 if an error occurred
 */
static int
cache_artwork_get_impl(struct cache_command *cmd)
{
#define Q_TMPL "SELECT a.format, a.data FROM artwork a WHERE a.type = %d AND a.persistentid = %" PRIi64 " AND a.max_w = %d AND a.max_h = %d;"
  sqlite3_stmt *stmt;
  char *query;
  int datalen;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, cmd->arg.type, cmd->arg.persistentid, cmd->arg.max_w, cmd->arg.max_h);
  if (!query)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);
  ret = sqlite3_prepare_v2(g_db_hdl, query, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(g_db_hdl));
      ret = -1;
      goto error_get;
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      cmd->arg.cached = 0;

      if (ret == SQLITE_DONE)
	{
	  ret = 0;
	  DPRINTF(E_DBG, L_CACHE, "No results\n");
	}
      else
	{
	  ret = -1;
	  DPRINTF(E_LOG, L_CACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));
	}

      goto error_get;
    }

  cmd->arg.format = sqlite3_column_int(stmt, 0);
  datalen = sqlite3_column_bytes(stmt, 1);
  if (!cmd->arg.evbuf)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: Artwork evbuffer is NULL\n");
      goto error_get;
    }

  ret = evbuffer_add(cmd->arg.evbuf, sqlite3_column_blob(stmt, 1), datalen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork evbuffer\n");
      goto error_get;
    }

  cmd->arg.cached = 1;

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    DPRINTF(E_LOG, L_CACHE, "Error finalizing query for getting cache: %s\n", sqlite3_errmsg(g_db_hdl));

  DPRINTF(E_DBG, L_CACHE, "Cache hit: %s\n", query);

  return 0;

 error_get:
  sqlite3_finalize(stmt);
  return -1;
#undef Q_TMPL
}

static int
cache_artwork_stash_impl(struct cache_command *cmd)
{
  /* Clear current stash */
  if (g_stash.path)
    {
      free(g_stash.path);
      free(g_stash.data);
      memset(&g_stash, 0, sizeof(struct stash));
    }

  g_stash.size = evbuffer_get_length(cmd->arg.evbuf);
  g_stash.data = malloc(g_stash.size);
  if (!g_stash.data)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork stash data\n");
      return -1;
    }

  g_stash.path = strdup(cmd->arg.path);
  if (!g_stash.path)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork stash path\n");
      free(g_stash.data);
      return -1;
    }

  g_stash.format = cmd->arg.format;

  return evbuffer_copyout(cmd->arg.evbuf, g_stash.data, g_stash.size);
}

static int
cache_artwork_read_impl(struct cache_command *cmd)
{
  cmd->arg.format = 0;

  if (!g_stash.path || !g_stash.data || (strcmp(g_stash.path, cmd->arg.path) != 0))
    return -1;

  cmd->arg.format = g_stash.format;

  DPRINTF(E_DBG, L_CACHE, "Stash hit (format %d, size %zu): %s\n", g_stash.format, g_stash.size, g_stash.path);

  return evbuffer_add(cmd->arg.evbuf, g_stash.data, g_stash.size);
}

static void *
cache(void *arg)
{
  int ret;

  ret = cache_create();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: Cache create failed. Cache will be disabled.\n");
      pthread_exit(NULL);
    }

  /* The thread needs a connection with the main db, so it can generate DAAP
   * replies through httpd_daap.c
   */
  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: DB init failed. Cache will be disabled.\n");
      cache_close();

      pthread_exit(NULL);
    }

  g_initialized = 1;

  event_base_dispatch(evbase_cache);

  if (g_initialized)
    {
      DPRINTF(E_LOG, L_CACHE, "Cache event loop terminated ahead of time!\n");
      g_initialized = 0;
    }

  db_perthread_deinit();

  cache_close();

  pthread_exit(NULL);
}

static void
exit_cb(int fd, short what, void *arg)
{
  int dummy;
  int ret;

  ret = read(g_exit_pipe[0], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_CACHE, "Error reading from exit pipe\n");

  event_base_loopbreak(evbase_cache);

  g_initialized = 0;

  event_add(g_exitev, NULL);
}

static void
command_cb(int fd, short what, void *arg)
{
  struct cache_command *cmd;
  int ret;

  ret = read(g_cmd_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_CACHE, "Could not read command! (read %d): %s\n", ret, (ret < 0) ? strerror(errno) : "-no error-");
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



/* ---------------------------- DAAP cache API  --------------------------- */

/* The DAAP cache will cache raw daap replies for queries added with
 * cache_daap_add(). Only some query types are supported.
 * You can't add queries where the canonical reply is not HTTP_OK, because
 * daap_request will use that as default for cache replies.
 *
 */

void
cache_daap_trigger(void)
{
  struct cache_command *cmd;

  if (!g_initialized)
    return;

  cmd = (struct cache_command *)malloc(sizeof(struct cache_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not allocate cache_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct cache_command));

  cmd->nonblock = 1;

  cmd->func = cache_daap_update_timer;

  nonblock_command(cmd);
}

int
cache_daap_get(const char *query, struct evbuffer *evbuf)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_daap_query_get;
  cmd.arg.query = strdup(query);
  cmd.arg.evbuf = evbuf;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

void
cache_daap_add(const char *query, const char *ua, int msec)
{
  struct cache_command *cmd;

  if (!g_initialized)
    return;

  cmd = (struct cache_command *)malloc(sizeof(struct cache_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not allocate cache_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct cache_command));

  cmd->nonblock = 1;

  cmd->func = cache_daap_query_add;
  cmd->arg.query = strdup(query);
  cmd->arg.ua = strdup(ua);
  cmd->arg.msec = msec;

  nonblock_command(cmd);
}

int
cache_daap_threshold(void)
{
  return g_cfg_threshold;
}


/* --------------------------- Artwork cache API -------------------------- */

/*
 * Updates cached timestamps to current time for all cache entries for the given path, if the file was not modfied
 * after the cached timestamp.
 *
 * If the parameter "del" is greater than 0, all cache entries for the given path are deleted, if the file was
 * modified after the cached timestamp.
 *
 * @param path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @param mtime modified timestamp of the artwork file
 * @param del if > 0 cached entries for the given path are deleted if the cached timestamp (db_timestamp) is older than mtime
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_ping(char *path, time_t mtime, int del)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_artwork_ping_impl;
  cmd.arg.path = strdup(path);
  cmd.arg.mtime = mtime;
  cmd.arg.del = del;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Removes all cache entries for the given path
 *
 * @param path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_delete_by_path(char *path)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_artwork_delete_by_path_impl;
  cmd.arg.path = strdup(path);

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Removes all cache entries with cached timestamp older than the given reference timestamp
 *
 * @param ref reference timestamp
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_purge_cruft(time_t ref)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_artwork_purge_cruft_impl;
  cmd.arg.mtime = ref;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Adds the given (scaled) artwork image to the artwork cache
 *
 * @param type individual or group artwork
 * @param persistentid persistent itemid, songalbumid or songartistid
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param format ART_FMT_PNG for png, ART_FMT_JPEG for jpeg or 0 if no artwork available
 * @param filename the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork) or empty if no artwork available
 * @param evbuf event buffer containing the (scaled) image
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_add(int type, int64_t persistentid, int max_w, int max_h, int format, char *filename, struct evbuffer *evbuf)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_artwork_add_impl;
  cmd.arg.type = type;
  cmd.arg.persistentid = persistentid;
  cmd.arg.max_w = max_w;
  cmd.arg.max_h = max_h;
  cmd.arg.format = format;
  cmd.arg.path = strdup(filename);
  cmd.arg.evbuf = evbuf;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Get the cached artwork image for the given persistentid and maximum width/height
 *
 * If there is a cached entry for the given id and width/height, the parameter cached is set to 1.
 * In this case format and data contain the cached values.
 *
 * @param persistentid persistent songalbumid or songartistid
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param cached set by this function to 0 if no cache entry exists, otherwise 1
 * @param format set by this function to the format of the cache entry
 * @param evbuf event buffer filled by this function with the scaled image
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_get(int type, int64_t persistentid, int max_w, int max_h, int *cached, int *format, struct evbuffer *evbuf)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_artwork_get_impl;
  cmd.arg.type = type;
  cmd.arg.persistentid = persistentid;
  cmd.arg.max_w = max_w;
  cmd.arg.max_h = max_h;
  cmd.arg.evbuf = evbuf;

  ret = sync_command(&cmd);

  *format = cmd.arg.format;
  *cached = cmd.arg.cached;

  command_deinit(&cmd);

  return ret;
}

/*
 * Put an artwork image in the in-memory stash (the previous will be deleted)
 *
 * @param evbuf event buffer with the cached image to cache
 * @param path the source (url) of the image to stash
 * @param format the format of the image
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_stash(struct evbuffer *evbuf, char *path, int format)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_artwork_stash_impl;
  cmd.arg.evbuf = evbuf;
  cmd.arg.path = path;
  cmd.arg.format = format;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Read the cached artwork image in the in-memory stash into evbuffer
 *
 * @param evbuf event buffer filled by this function with the cached image
 * @param path this function will check that the path matches the cached image's path
 * @param format set by this function to the format of the image
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_read(struct evbuffer *evbuf, char *path, int *format)
{
  struct cache_command cmd;
  int ret;

  if (!g_initialized)
    return -1;

  command_init(&cmd);

  cmd.func = cache_artwork_read_impl;
  cmd.arg.evbuf = evbuf;
  cmd.arg.path = path;

  ret = sync_command(&cmd);

  *format = cmd.arg.format;

  command_deinit(&cmd);

  return ret;
}


/* -------------------------- Cache general API --------------------------- */

int
cache_init(void)
{
  int ret;

  g_initialized = 0;

  g_db_path = cfg_getstr(cfg_getsec(cfg, "general"), "cache_path");
  if (!g_db_path || (strlen(g_db_path) == 0))
    {
      DPRINTF(E_LOG, L_CACHE, "Cache path invalid, disabling cache\n");
      return 0;
    }

  g_cfg_threshold = cfg_getint(cfg_getsec(cfg, "general"), "cache_daap_threshold");
  if (g_cfg_threshold == 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Cache threshold set to 0, disabling cache\n");
      return 0;
    }

# if defined(__linux__)
  ret = pipe2(g_exit_pipe, O_CLOEXEC);
# else
  ret = pipe(g_exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create pipe: %s\n", strerror(errno));
      goto exit_fail;
    }

# if defined(__linux__)
  ret = pipe2(g_cmd_pipe, O_CLOEXEC);
# else
  ret = pipe(g_cmd_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create command pipe: %s\n", strerror(errno));
      goto cmd_fail;
    }

  evbase_cache = event_base_new();
  if (!evbase_cache)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create an event base\n");
      goto evbase_fail;
    }

#ifdef HAVE_LIBEVENT2
  g_exitev = event_new(evbase_cache, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create exit event\n");
      goto evnew_fail;
    }

  g_cmdev = event_new(evbase_cache, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create cmd event\n");
      goto evnew_fail;
    }

  g_cacheev = evtimer_new(evbase_cache, cache_daap_update_cb, NULL);
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create cache event\n");
      goto evnew_fail;
    }
#else
  g_exitev = (struct event *)malloc(sizeof(struct event));
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create exit event\n");
      goto evnew_fail;
    }
  event_set(g_exitev, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  event_base_set(evbase_cache, g_exitev);

  g_cmdev = (struct event *)malloc(sizeof(struct event));
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create cmd event\n");
      goto evnew_fail;
    }
  event_set(g_cmdev, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  event_base_set(evbase_cache, g_cmdev);

  g_cacheev = (struct event *)malloc(sizeof(struct event));
  if (!g_cacheev)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not create cache event\n");
      goto evnew_fail;
    }
  event_set(g_cacheev, -1, EV_TIMEOUT, cache_daap_update_cb, NULL);
  event_base_set(evbase_cache, g_cacheev);
#endif

  event_add(g_exitev, NULL);
  event_add(g_cmdev, NULL);

  DPRINTF(E_INFO, L_CACHE, "cache thread init\n");

  ret = pthread_create(&tid_cache, NULL, cache, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not spawn cache thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;
  
 thread_fail:
 evnew_fail:
  event_base_free(evbase_cache);
  evbase_cache = NULL;

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
cache_deinit(void)
{
  int ret;

  if (!g_initialized)
    return;

  thread_exit();

  ret = pthread_join(tid_cache, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_CACHE, "Could not join cache thread: %s\n", strerror(errno));
      return;
    }

  // Free event base (should free events too)
  event_base_free(evbase_cache);

  // Close pipes
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);
}
