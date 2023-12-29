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

#include <event2/event.h>
#include <sqlite3.h>

#include "conffile.h"
#include "logger.h"
#include "httpd.h"
#include "httpd_daap.h"
#include "db.h"
#include "cache.h"
#include "listener.h"
#include "commands.h"


#define CACHE_VERSION 4


struct cache_arg
{
  char *query; // daap query
  char *ua;    // user agent
  int is_remote;
  int msec;

  uint32_t id; // file id
  const char *header_format;

  const char *path;  // artwork path
  char *pathcopy;  // copy of artwork path (for async operations)
  int type;    // individual or group artwork
  int64_t persistentid;
  int max_w;
  int max_h;
  int format;
  time_t mtime;
  int cached;
  int del;

  struct evbuffer *evbuf;
};

struct cachelist
{
  uint32_t id;
  uint32_t ts;
};


/* --- Globals --- */
// cache thread
static pthread_t tid_cache;

// Event base, pipes and events
struct event_base *evbase_cache;
static struct commands_base *cmdbase;
static struct event *cache_daap_updateev;
static struct event *cache_xcode_updateev;

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

static int g_suspended;

// The user may configure a threshold (in msec), and queries slower than
// that will have their reply cached
static int g_cfg_threshold;

struct cache_db_def
{
  const char *name;
  const char *create_query;
  const char *drop_query;
};

struct cache_db_def cache_db_def[] = {
  {
    "xcode_files",
    "CREATE TABLE IF NOT EXISTS xcode_files ("
    "   id                 INTEGER PRIMARY KEY NOT NULL,"
    "   time_modified      INTEGER DEFAULT 0,"
    "   filepath           VARCHAR(4096) NOT NULL"
    ");",
    "DROP TABLE IF EXISTS xcode_files;",
  },
  {
    "xcode_data",
    "CREATE TABLE IF NOT EXISTS xcode_data ("
    "   id                 INTEGER PRIMARY KEY NOT NULL,"
    "   timestamp          INTEGER DEFAULT 0,"
    "   file_id            INTEGER DEFAULT 0,"
    "   format             VARCHAR(255) NOT NULL,"
    "   header             BLOB"
    ");",
    "DROP TABLE IF EXISTS xcode_data;",
  },
  {
    "replies",
    "CREATE TABLE IF NOT EXISTS replies ("
    "   id                 INTEGER PRIMARY KEY NOT NULL,"
    "   query              VARCHAR(4096) NOT NULL,"
    "   reply              BLOB"
    ");",
    "DROP TABLE IF EXISTS replies;",
  },
  {
    "queries",
    "CREATE TABLE IF NOT EXISTS queries ("
    "   id                 INTEGER PRIMARY KEY NOT NULL,"
    "   query              VARCHAR(4096) UNIQUE NOT NULL,"
    "   user_agent         VARCHAR(1024),"
    "   is_remote          INTEGER DEFAULT 0,"
    "   msec               INTEGER DEFAULT 0,"
    "   timestamp          INTEGER DEFAULT 0"
    ");",
    "DROP TABLE IF EXISTS queries;",
  },
  {
    "idx_query",
    "CREATE INDEX IF NOT EXISTS idx_query ON replies (query);",
    "DROP INDEX IF EXISTS idx_query;",
  },
  {
    "artwork",
    "CREATE TABLE IF NOT EXISTS artwork ("
    "   id                  INTEGER PRIMARY KEY NOT NULL,"
    "   type                INTEGER NOT NULL DEFAULT 0,"
    "   persistentid        INTEGER NOT NULL,"
    "   max_w               INTEGER NOT NULL,"
    "   max_h               INTEGER NOT NULL,"
    "   format              INTEGER NOT NULL,"
    "   filepath            VARCHAR(4096) NOT NULL,"
    "   db_timestamp        INTEGER DEFAULT 0,"
    "   data                BLOB"
    ");",
    "DROP TABLE IF EXISTS artwork;",
  },
  {
    "idx_persistentidwh",
    "CREATE INDEX IF NOT EXISTS idx_persistentidwh ON artwork(type, persistentid, max_w, max_h);",
    "DROP INDEX IF EXISTS idx_persistentidwh;",
  },
  {
    "idx_pathtime",
    "CREATE INDEX IF NOT EXISTS idx_pathtime ON artwork(filepath, db_timestamp);",
    "DROP INDEX IF EXISTS idx_pathtime;",
  },
  {
    "admin_cache",
    "CREATE TABLE IF NOT EXISTS admin_cache("
    " key VARCHAR(32) PRIMARY KEY NOT NULL,"
    " value VARCHAR(32) NOT NULL"
    ");",
    "DROP TABLE IF EXISTS admin_cache;",
  },
};


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


/* --------------------------------- MAIN --------------------------------- */
/*                              Thread: cache                              */


static int
cache_create_tables(void)
{
#define Q_CACHE_VERSION "INSERT INTO admin_cache (key, value) VALUES ('cache_version', '%d');"
  char *query;
  char *errmsg;
  int ret;
  int i;

  for (i = 0; i < ARRAY_SIZE(cache_db_def); i++)
    {
      ret = sqlite3_exec(g_db_hdl, cache_db_def[i].create_query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_FATAL, L_CACHE, "Error creating cache db entity '%s': %s\n", cache_db_def[i].name, errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_close(g_db_hdl);
	  return -1;
	}
    }

  query = sqlite3_mprintf(Q_CACHE_VERSION, CACHE_VERSION);
  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error inserting cache version: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_close(g_db_hdl);
      return -1;
    }

  DPRINTF(E_DBG, L_CACHE, "Cache tables created\n");

  return 0;
#undef Q_CACHE_VERSION
}

static int
cache_drop_tables(void)
{
#define Q_VACUUM	"VACUUM;"
  char *errmsg;
  int ret;
  int i;

  for (i = 0; i < ARRAY_SIZE(cache_db_def); i++)
    {
      ret = sqlite3_exec(g_db_hdl, cache_db_def[i].drop_query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_FATAL, L_CACHE, "Error dropping cache db entity '%s': %s\n", cache_db_def[i].name, errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_close(g_db_hdl);
	  return -1;
	}
    }

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
#define Q_PRAGMA_MMAP_SIZE "PRAGMA mmap_size=%d;"
  char *errmsg;
  int ret;
  int cache_size;
  char *journal_mode;
  int synchronous;
  int mmap_size;
  char *query;

  // Open db
  ret = sqlite3_open(g_db_path, &g_db_hdl);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not open '%s': %s\n", g_db_path, sqlite3_errmsg(g_db_hdl));

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
      sqlite3_free(query);
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
      sqlite3_free(query);
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
      sqlite3_free(query);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_synchronous: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_close(g_db_hdl);
	  return -1;
	}
    }

  // Set mmap size
  mmap_size = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_mmap_size_cache");
  if (synchronous > -1)
    {
      query = sqlite3_mprintf(Q_PRAGMA_MMAP_SIZE, mmap_size);
      ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_mmap_size: %s\n", errmsg);

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
#undef Q_PRAGMA_MMAP_SIZE
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

  datalen = evbuffer_get_length(evbuf);
  data = evbuffer_pullup(evbuf, -1);

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
static enum command_state
cache_daap_query_add(void *arg, int *retval)
{
#define Q_TMPL "INSERT OR REPLACE INTO queries (user_agent, is_remote, query, msec, timestamp) VALUES ('%q', %d, '%q', %d, %" PRIi64 ");"
#define Q_CLEANUP "DELETE FROM queries WHERE id NOT IN (SELECT id FROM queries ORDER BY timestamp DESC LIMIT 20);"
  struct cache_arg *cmdarg;
  struct timeval delay = { 60, 0 };
  char *query;
  char *errmsg;
  int ret;

  cmdarg = arg;
  if (!cmdarg->ua)
    {
      DPRINTF(E_LOG, L_CACHE, "Couldn't add slow query to cache, unknown user-agent\n");

      goto error_add;
    }

  // Currently we are only able to pre-build and cache these reply types
  if ( (strncmp(cmdarg->query, "/databases/1/containers/", strlen("/databases/1/containers/")) != 0) &&
       (strncmp(cmdarg->query, "/databases/1/groups?", strlen("/databases/1/groups?")) != 0) &&
       (strncmp(cmdarg->query, "/databases/1/items?", strlen("/databases/1/items?")) != 0) &&
       (strncmp(cmdarg->query, "/databases/1/browse/", strlen("/databases/1/browse/")) != 0) )
    goto error_add;

  remove_tag(cmdarg->query, "session-id");
  remove_tag(cmdarg->query, "revision-number");

  query = sqlite3_mprintf(Q_TMPL, cmdarg->ua, cmdarg->is_remote, cmdarg->query, cmdarg->msec, (int64_t)time(NULL));
  if (!query)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory making query string.\n");

      goto error_add;
    }

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error adding query to query list: %s\n", errmsg);

      sqlite3_free(errmsg);
      goto error_add;
    }

  DPRINTF(E_INFO, L_CACHE, "Slow query (%d ms) added to cache: '%s' (user-agent: '%s')\n", cmdarg->msec, cmdarg->query, cmdarg->ua);

  free(cmdarg->ua);
  free(cmdarg->query);

  // Limits the size of the cache to only contain replies for 20 most recent queries
  ret = sqlite3_exec(g_db_hdl, Q_CLEANUP, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error cleaning up query list before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      *retval = -1;
      return COMMAND_END;
    }

  // Will set of cache regeneration after waiting a bit (so there is less risk
  // of disturbing the user)
  evtimer_add(cache_daap_updateev, &delay);

  *retval = 0;
  return COMMAND_END;

 error_add:
  if (cmdarg->ua)
    free(cmdarg->ua);

  if (cmdarg->query)
    free(cmdarg->query);

  *retval = -1;
  return COMMAND_END;
#undef Q_CLEANUP
#undef Q_TMPL
}

// Gets a reply from the cache.
// cmdarg->evbuf will be filled with the reply (gzipped)
static enum command_state
cache_daap_query_get(void *arg, int *retval)
{
#define Q_TMPL "SELECT reply FROM replies WHERE query = ?;"
  struct cache_arg *cmdarg;
  sqlite3_stmt *stmt;
  char *query;
  int datalen;
  int ret;

  cmdarg = arg;
  query = cmdarg->query;
  remove_tag(query, "session-id");
  remove_tag(query, "revision-number");

  // Look in the DB
  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      free(query);
      *retval = -1;
      return COMMAND_END;
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

  if (!cmdarg->evbuf)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: DAAP reply evbuffer is NULL\n");
      goto error_get;
    }

  ret = evbuffer_add(cmdarg->evbuf, sqlite3_column_blob(stmt, 0), datalen);
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

  *retval = 0;
  return COMMAND_END;

 error_get:
  sqlite3_finalize(stmt);
  free(query);
  *retval = -1;
  return COMMAND_END;
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
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error deleting query from cache: %s\n", errmsg);

      sqlite3_free(errmsg);
      return -1;
    }

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
  struct evbuffer *gzbuf;
  char *errmsg;
  char *query;
  int ret;

  if (g_suspended)
    {
      DPRINTF(E_DBG, L_CACHE, "Got a request to update DAAP cache while suspended\n");
      return;
    }

  DPRINTF(E_LOG, L_CACHE, "Beginning DAAP cache update\n");

  ret = sqlite3_exec(g_db_hdl, "DELETE FROM replies;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error clearing reply cache before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      return;
    }

  ret = sqlite3_prepare_v2(g_db_hdl, "SELECT id, user_agent, is_remote, query FROM queries;", -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      return;
    }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      query = strdup((char *)sqlite3_column_text(stmt, 3));

      evbuf = daap_reply_build(query, (char *)sqlite3_column_text(stmt, 1), sqlite3_column_int(stmt, 2));
      if (!evbuf)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error building DAAP reply for query: %s\n", query);
	  cache_daap_query_delete(sqlite3_column_int(stmt, 0));
	  free(query);

	  continue;
	}

      gzbuf = httpd_gzip_deflate(evbuf);
      if (!gzbuf)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error gzipping DAAP reply for query: %s\n", query);
	  cache_daap_query_delete(sqlite3_column_int(stmt, 0));
	  free(query);
	  evbuffer_free(evbuf);

	  continue;
	}

      evbuffer_free(evbuf);

      cache_daap_reply_add(query, gzbuf);

      free(query);
      evbuffer_free(gzbuf);
    }

  if (ret != SQLITE_DONE)
    DPRINTF(E_LOG, L_CACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));

  sqlite3_finalize(stmt);

  DPRINTF(E_LOG, L_CACHE, "DAAP cache updated\n");
}

static enum command_state
xcode_header_get(void *arg, int *retval)
{
#define Q_TMPL "SELECT header FROM xcode_data WHERE length(header) > 0 AND id = ? AND format = ?;"
  struct cache_arg *cmdarg = arg;
  sqlite3_stmt *stmt = NULL;
  int ret;

  cmdarg->cached = 0;

  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    goto error;

  sqlite3_bind_int(stmt, 1, cmdarg->id);
  sqlite3_bind_text(stmt, 2, cmdarg->header_format, -1, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret == SQLITE_DONE)
    goto end;
  else if (ret != SQLITE_ROW)
    goto error;

  ret = evbuffer_add(cmdarg->evbuf, sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
  if (ret < 0)
    goto error;

  cmdarg->cached = 1;

  DPRINTF(E_DBG, L_CACHE, "Cache header hit (%zu bytes)\n", evbuffer_get_length(cmdarg->evbuf));

 end:
  sqlite3_finalize(stmt);
  *retval = 0;
  return COMMAND_END;

 error:
  DPRINTF(E_LOG, L_CACHE, "Database error getting prepared header from cache: %s\n", sqlite3_errmsg(g_db_hdl));
  if (stmt)
    sqlite3_finalize(stmt);
  *retval = -1;
  return COMMAND_END;
#undef Q_TMPL
}

static int
xcode_add_entry(uint32_t id, uint32_t ts, const char *path)
{
#define Q_TMPL "INSERT OR REPLACE INTO xcode_files (id, time_modified, filepath) VALUES (%d, %d, '%q');"
  char *query;
  char *errmsg;
  int ret;

  DPRINTF(E_LOG, L_CACHE, "Adding xcode file id %d, path '%s'\n", id, path);

  query = sqlite3_mprintf(Q_TMPL, id, ts, path);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error adding row to cache: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  return 0;
#undef Q_TMPL
}

static int
xcode_del_entry(uint32_t id)
{
#define Q_TMPL_FILES "DELETE FROM xcode_files WHERE id = %d;"
#define Q_TMPL_DATA "DELETE FROM xcode_data WHERE file_id = %d;"
  char query[256];
  char *errmsg;
  int ret;

  DPRINTF(E_LOG, L_CACHE, "Deleting xcode file id %d\n", id);

  sqlite3_snprintf(sizeof(query), query, Q_TMPL_FILES, (int)id);
  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error deleting row from xcode_files: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  sqlite3_snprintf(sizeof(query), query, Q_TMPL_DATA, (int)id);
  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error deleting rows from xcode_data: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  return 0;
#undef Q_TMPL_DATA
#undef Q_TMPL_FILES
}

/* In the xcode table we keep a prepared header for files that could be subject
 * to transcoding. Whenever the library changes, this callback runs, and the
 * list of files in the xcode table is synced with the main files table.
 *
 * In practice we compare two tables, both sorted by id:
 *
 * From files:                         From the cache
 *  | id      |  time_modified  |       | id       | time_modified | data    |
 *
 * We do it one item at the time from files, and then going through cache table
 * rows until: table end OR id is larger OR id is equal and time equal or newer
 */
static int
xcode_sync_with_files(void)
{
  sqlite3_stmt *stmt;
  struct cachelist *cachelist = NULL;
  size_t cachelist_size = 0;
  size_t cachelist_len = 0;
  struct query_params qp = { .type = Q_ITEMS, .filter = "f.data_kind = 0", .order = "f.id" };
  struct db_media_file_info dbmfi;
  uint32_t id;
  uint32_t ts;
  int i;
  int ret;

  DPRINTF(E_LOG, L_CACHE, "SYNC START\n");

  // Both lists must be sorted by id, otherwise the compare below won't work
  ret = sqlite3_prepare_v2(g_db_hdl, "SELECT id, time_modified FROM xcode_files ORDER BY id;", -1, &stmt, 0);
  if (ret != SQLITE_OK)
    goto error;

  while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      if (cachelist_len + 1 > cachelist_size)
	{
	  cachelist_size += 1024;
	  CHECK_NULL(L_CACHE, cachelist = realloc(cachelist, cachelist_size * sizeof(struct cachelist)));
	}
      cachelist[cachelist_len].id = sqlite3_column_int(stmt, 0);
      cachelist[cachelist_len].ts = sqlite3_column_int(stmt, 1);
      cachelist_len++;
    }
  sqlite3_finalize(stmt);

  ret = db_query_start(&qp);
  if (ret < 0)
    goto error;

  // Loop while either list has remaining items
  i = 0;
  while (1)
    {
      ret = db_query_fetch_file(&dbmfi, &qp);
      if (ret != 0) // At end of files table (or error occured)
	{
	  for (; i < cachelist_len; i++)
	    xcode_del_entry(cachelist[i].id);

	  break;
	}

      safe_atou32(dbmfi.id, &id);
      safe_atou32(dbmfi.time_modified, &ts);

      if (i == cachelist_len || cachelist[i].id > id) // At end of cache table or new file
	{
	  xcode_add_entry(id, ts, dbmfi.path);
	}
      else if (cachelist[i].id < id) // Removed file
	{
	  xcode_del_entry(cachelist[i].id);
	  i++;
	}
      else if (cachelist[i].id == id && cachelist[i].ts < ts) // Modified file
	{
	  xcode_del_entry(cachelist[i].id);
	  xcode_add_entry(id, ts, dbmfi.path);
	  i++;
	}
      else // Found in both tables and timestamp in cache table is adequate
	{
	  i++;
	}
    }
  db_query_end(&qp);

  free(cachelist);
  return 0;

 error:
  DPRINTF(E_LOG, L_CACHE, "Database error while processing xcode_files table\n");
  free(cachelist);
  return -1;
}

static int
xcode_prepare_header(const char *format, int id, const char *path)
{
#define Q_TMPL "INSERT INTO xcode_data (timestamp, file_id, format, header) VALUES (?, ?, ?, ?);"
  struct evbuffer *header = NULL;
  sqlite3_stmt *stmt = NULL;
  unsigned char *data = NULL;
  size_t datalen = 0;
  int ret;

  DPRINTF(E_DBG, L_CACHE, "Preparing %s header for '%s' (file id %d)\n", format, path, id);

#if 1
  ret = httpd_prepare_header(&header, format, path); // Proceed even if error, we also cache that
  if (ret == 0)
    {
      datalen = evbuffer_get_length(header);
      data = evbuffer_pullup(header, -1);
    }
#elif
  data = (unsigned char*)"dummy";
  datalen = 6;
#endif

  ret = sqlite3_prepare_v2(g_db_hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing xcode_data for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      goto error;
    }

  sqlite3_bind_int(stmt, 1, (uint64_t)time(NULL));
  sqlite3_bind_int(stmt, 2, id);
  sqlite3_bind_text(stmt, 3, format, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 4, data, datalen, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_CACHE, "Error stepping xcode_data for cache update: %s\n", sqlite3_errmsg(g_db_hdl));
      goto error;
    }

  sqlite3_finalize(stmt);
  if (header)
    evbuffer_free(header);
  return 0;

 error:
  if (stmt)
    sqlite3_finalize(stmt);
  if (header)
    evbuffer_free(header);
  return -1;
#undef Q_TMPL
}

static int
xcode_prepare_headers(const char *format)
{
#define Q_TMPL "SELECT xf.id, xf.filepath, xd.id FROM xcode_files xf LEFT JOIN xcode_data xd ON xf.id = xd.file_id AND xd.format = '%q';"
  sqlite3_stmt *stmt;
  char *query;
  const char *file_path;
  int file_id;
  int data_id;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, format);

  ret = sqlite3_prepare_v2(g_db_hdl, query, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    goto error;

  while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      data_id = sqlite3_column_int(stmt, 2);
      if (data_id > 0)
	continue; // Already have a prepared header

      file_id = sqlite3_column_int(stmt, 0);
      file_path = (const char *)sqlite3_column_text(stmt, 1);

      xcode_prepare_header(format, file_id, file_path);

    }
  sqlite3_finalize(stmt);
  sqlite3_free(query);
  return 0;

 error:
  DPRINTF(E_LOG, L_CACHE, "Error occured while preparing headers\n");
  sqlite3_free(query);
  return -1;
#undef Q_TMPL
}

static void
cache_xcode_update_cb(int fd, short what, void *arg)
{
  if (xcode_sync_with_files() < 0)
    return;

  xcode_prepare_headers("mp4");
}

/* Sets off an update by activating the event. The delay is because we are low
 * priority compared to other listeners of database updates.
 */
static enum command_state
cache_database_update(void *arg, int *retval)
{
  struct timeval delay_daap = { 10, 0 };
  struct timeval delay_xcode = { 5, 0 };
//  const char *prefer_format = cfg_getstr(cfg_getsec(cfg, "library"), "prefer_format");

  event_add(cache_daap_updateev, &delay_daap);

//  if (prefer_format && strcmp(prefer_format, "alac")) // TODO Ugly
    event_add(cache_xcode_updateev, &delay_xcode);

  *retval = 0;
  return COMMAND_END;
}

/* Callback from filescanner thread */
static void
cache_daap_listener_cb(short event_mask)
{
  commands_exec_async(cmdbase, cache_database_update, NULL);
}


/*
 * Updates cached timestamps to current time for all cache entries for the given path, if the file was not modfied
 * after the cached timestamp. All cache entries for the given path are deleted, if the file was
 * modified after the cached timestamp.
 *
 * @param cmdarg->pathcopy the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @param cmdarg->mtime modified timestamp of the artwork file
 * @return 0 if successful, -1 if an error occurred
 */
static enum command_state
cache_artwork_ping_impl(void *arg, int *retval)
{
#define Q_TMPL_PING "UPDATE artwork SET db_timestamp = %" PRIi64 " WHERE filepath = '%q' AND db_timestamp >= %" PRIi64 ";"
#define Q_TMPL_DEL "DELETE FROM artwork WHERE filepath = '%q' AND db_timestamp < %" PRIi64 ";"

  struct cache_arg *cmdarg;
  char *query;
  char *errmsg;
  int ret;

  cmdarg = arg;
  query = sqlite3_mprintf(Q_TMPL_PING, (int64_t)time(NULL), cmdarg->pathcopy, (int64_t)cmdarg->mtime);

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      goto error_ping;
    }

  if (cmdarg->del > 0)
    {
      query = sqlite3_mprintf(Q_TMPL_DEL, cmdarg->pathcopy, (int64_t)cmdarg->mtime);

      DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

      ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
      sqlite3_free(query);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

	  goto error_ping;
	}
    }

  free(cmdarg->pathcopy);

  *retval = 0;
  return COMMAND_END;

 error_ping:
  sqlite3_free(errmsg);
  free(cmdarg->pathcopy);

  *retval = -1;
  return COMMAND_END;
  
#undef Q_TMPL_PING
#undef Q_TMPL_DEL
}

/*
 * Removes all cache entries for the given path
 *
 * @param cmdarg->path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @return 0 if successful, -1 if an error occurred
 */
static enum command_state
cache_artwork_delete_by_path_impl(void *arg, int *retval)
{
#define Q_TMPL_DEL "DELETE FROM artwork WHERE filepath = '%q';"

  struct cache_arg *cmdarg;
  char *query;
  char *errmsg;
  int ret;

  cmdarg = arg;
  query = sqlite3_mprintf(Q_TMPL_DEL, cmdarg->path);

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      *retval = -1;
      return COMMAND_END;
    }

  DPRINTF(E_DBG, L_CACHE, "Deleted %d rows\n", sqlite3_changes(g_db_hdl));

  *retval = 0;
  return COMMAND_END;

#undef Q_TMPL_DEL
}

/*
 * Removes all cache entries with cached timestamp older than the given reference timestamp
 *
 * @param cmdarg->mtime reference timestamp
 * @return 0 if successful, -1 if an error occurred
 */
static enum command_state
cache_artwork_purge_cruft_impl(void *arg, int *retval)
{
#define Q_TMPL "DELETE FROM artwork WHERE db_timestamp < %" PRIi64 ";"

  struct cache_arg *cmdarg;
  char *query;
  char *errmsg;
  int ret;

  cmdarg = arg;
  query = sqlite3_mprintf(Q_TMPL, (int64_t)cmdarg->mtime);

  DPRINTF(E_DBG, L_CACHE, "Running purge query '%s'\n", query);

  ret = sqlite3_exec(g_db_hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      *retval = -1;
      return COMMAND_END;
    }

  DPRINTF(E_DBG, L_CACHE, "Purged %d rows\n", sqlite3_changes(g_db_hdl));

  *retval = 0;
  return COMMAND_END;

#undef Q_TMPL
}

/*
 * Adds the given (scaled) artwork image to the artwork cache
 *
 * @param cmdarg->persistentid persistent songalbumid or songartistid
 * @param cmdarg->max_w maximum image width
 * @param cmdarg->max_h maximum image height
 * @param cmdarg->format ART_FMT_PNG for png, ART_FMT_JPEG for jpeg or 0 if no artwork available
 * @param cmdarg->filename the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork) or empty if no artwork available
 * @param cmdarg->evbuf event buffer containing the (scaled) image
 * @return 0 if successful, -1 if an error occurred
 */
static enum command_state
cache_artwork_add_impl(void *arg, int *retval)
{
  struct cache_arg *cmdarg;
  sqlite3_stmt *stmt;
  char *query;
  uint8_t *data;
  int datalen;
  int ret;

  cmdarg = arg;
  query = "INSERT INTO artwork (id, persistentid, max_w, max_h, format, filepath, db_timestamp, data, type) VALUES (NULL, ?, ?, ?, ?, ?, ?, ?, ?);";

  ret = sqlite3_prepare_v2(g_db_hdl, query, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(g_db_hdl));
      *retval = -1;
      return COMMAND_END;
    }

  datalen = evbuffer_get_length(cmdarg->evbuf);
  data = evbuffer_pullup(cmdarg->evbuf, -1);

  sqlite3_bind_int64(stmt, 1, cmdarg->persistentid);
  sqlite3_bind_int(stmt, 2, cmdarg->max_w);
  sqlite3_bind_int(stmt, 3, cmdarg->max_h);
  sqlite3_bind_int(stmt, 4, cmdarg->format);
  sqlite3_bind_text(stmt, 5, cmdarg->path, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 6, (uint64_t)time(NULL));
  sqlite3_bind_blob(stmt, 7, data, datalen, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 8, cmdarg->type);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_CACHE, "Error stepping query for artwork add: %s\n", sqlite3_errmsg(g_db_hdl));
      sqlite3_finalize(stmt);
      *retval = -1;
      return COMMAND_END;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error finalizing query for artwork add: %s\n", sqlite3_errmsg(g_db_hdl));
      *retval = -1;
      return COMMAND_END;
    }

  *retval = 0;
  return COMMAND_END;
}

/*
 * Get the cached artwork image for the given persistentid and maximum width/height
 *
 * If there is a cached entry for the given id and width/height, the parameter cached is set to 1.
 * In this case format and data contain the cached values.
 *
 * @param cmdarg->type individual or group artwork
 * @param cmdarg->persistentid persistent itemid, songalbumid or songartistid
 * @param cmdarg->max_w maximum image width
 * @param cmdarg->max_h maximum image height
 * @param cmdarg->cached set by this function to 0 if no cache entry exists, otherwise 1
 * @param cmdarg->format set by this function to the format of the cache entry
 * @param cmdarg->evbuf event buffer filled by this function with the scaled image
 * @return 0 if successful, -1 if an error occurred
 */
static enum command_state
cache_artwork_get_impl(void *arg, int *retval)
{
#define Q_TMPL "SELECT a.format, a.data FROM artwork a WHERE a.type = %d AND a.persistentid = %" PRIi64 " AND a.max_w = %d AND a.max_h = %d;"
  struct cache_arg *cmdarg;
  sqlite3_stmt *stmt;
  char *query;
  int datalen;
  int ret;

  cmdarg = arg;
  query = sqlite3_mprintf(Q_TMPL, cmdarg->type, cmdarg->persistentid, cmdarg->max_w, cmdarg->max_h);
  if (!query)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for query string\n");
      *retval = -1;
      return COMMAND_END;
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
      cmdarg->cached = 0;

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

  cmdarg->format = sqlite3_column_int(stmt, 0);
  datalen = sqlite3_column_bytes(stmt, 1);
  if (!cmdarg->evbuf)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: Artwork evbuffer is NULL\n");
      ret = -1;
      goto error_get;
    }

  ret = evbuffer_add(cmdarg->evbuf, sqlite3_column_blob(stmt, 1), datalen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork evbuffer\n");
      ret = -1;
      goto error_get;
    }

  cmdarg->cached = 1;

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    DPRINTF(E_LOG, L_CACHE, "Error finalizing query for getting cache: %s\n", sqlite3_errmsg(g_db_hdl));

  DPRINTF(E_DBG, L_CACHE, "Cache hit: %s\n", query);

  sqlite3_free(query);

  *retval = 0;
  return COMMAND_END;

 error_get:
  sqlite3_finalize(stmt);
  sqlite3_free(query);

  *retval = ret;
  return COMMAND_END;
#undef Q_TMPL
}

static enum command_state
cache_artwork_stash_impl(void *arg, int *retval)
{
  struct cache_arg *cmdarg;

  cmdarg = arg;

  // Clear current stash
  if (g_stash.path)
    {
      free(g_stash.path);
      free(g_stash.data);
      memset(&g_stash, 0, sizeof(struct stash));
    }

  // If called with no evbuf then we are done, we just needed to clear the stash
  if (!cmdarg->evbuf)
    {
      *retval = 0;
      return COMMAND_END;
    }

  g_stash.size = evbuffer_get_length(cmdarg->evbuf);
  g_stash.data = malloc(g_stash.size);
  if (!g_stash.data)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork stash data\n");
      *retval = -1;
      return COMMAND_END;
    }

  g_stash.path = strdup(cmdarg->path);
  if (!g_stash.path)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork stash path\n");
      free(g_stash.data);
      *retval = -1;
      return COMMAND_END;
    }

  g_stash.format = cmdarg->format;

  *retval = evbuffer_copyout(cmdarg->evbuf, g_stash.data, g_stash.size);
  return COMMAND_END;
}

static enum command_state
cache_artwork_read_impl(void *arg, int *retval)
{
  struct cache_arg *cmdarg;

  cmdarg = arg;
  cmdarg->format = 0;

  if (!g_stash.path || !g_stash.data || (strcmp(g_stash.path, cmdarg->path) != 0))
    {
      *retval = -1;
      return COMMAND_END;
    }

  cmdarg->format = g_stash.format;

  DPRINTF(E_DBG, L_CACHE, "Stash hit (format %d, size %zu): %s\n", g_stash.format, g_stash.size, g_stash.path);

  *retval = evbuffer_add(cmdarg->evbuf, g_stash.data, g_stash.size);
  return COMMAND_END;
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
   * replies through httpd_daap.c and read changes from the files table
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


/* ----------------------------- DAAP cache API  ---------------------------- */

/* The DAAP cache will cache raw daap replies for queries added with
 * cache_daap_add(). Only some query types are supported.
 * You can't add queries where the canonical reply is not HTTP_OK, because
 * daap_request will use that as default for cache replies.
 *
 */

void
cache_daap_suspend(void)
{
  g_suspended = 1;
}

void
cache_daap_resume(void)
{
  g_suspended = 0;
}

int
cache_daap_get(struct evbuffer *evbuf, const char *query)
{
  struct cache_arg cmdarg;

  if (!g_initialized)
    return -1;

  cmdarg.query = strdup(query);
  cmdarg.evbuf = evbuf;

  return commands_exec_sync(cmdbase, cache_daap_query_get, NULL, &cmdarg);
}

void
cache_daap_add(const char *query, const char *ua, int is_remote, int msec)
{
  struct cache_arg *cmdarg;

  if (!g_initialized)
    return;

  cmdarg = calloc(1, sizeof(struct cache_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not allocate cache_arg\n");
      return;
    }

  cmdarg->query = strdup(query);
  cmdarg->ua = strdup(ua);
  cmdarg->is_remote = is_remote;
  cmdarg->msec = msec;

  commands_exec_async(cmdbase, cache_daap_query_add, cmdarg);
}

int
cache_daap_threshold(void)
{
  return g_cfg_threshold;
}


/* --------------------------- Transcode cache API  ------------------------- */

int
cache_xcode_header_get(struct evbuffer *evbuf, int *cached, uint32_t id, const char *format)
{
  struct cache_arg cmdarg;
  int ret;

  if (!g_initialized)
    return -1;

  cmdarg.evbuf = evbuf;
  cmdarg.id = id;
  cmdarg.header_format = format;

  ret = commands_exec_sync(cmdbase, xcode_header_get, NULL, &cmdarg);

  *cached = cmdarg.cached;

  return ret;
}


/* ---------------------------- Artwork cache API  -------------------------- */

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
void
cache_artwork_ping(const char *path, time_t mtime, int del)
{
  struct cache_arg *cmdarg;

  if (!g_initialized)
    return;

  cmdarg = calloc(1, sizeof(struct cache_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not allocate cache_arg\n");
      return;
    }

  cmdarg->pathcopy = strdup(path);
  cmdarg->mtime = mtime;
  cmdarg->del = del;

  commands_exec_async(cmdbase, cache_artwork_ping_impl, cmdarg);
}

/*
 * Removes all cache entries for the given path
 *
 * @param path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @return 0 if successful, -1 if an error occurred
 */
int
cache_artwork_delete_by_path(const char *path)
{
  struct cache_arg cmdarg;

  if (!g_initialized)
    return -1;

  cmdarg.path = path;

  return commands_exec_sync(cmdbase, cache_artwork_delete_by_path_impl, NULL, &cmdarg);
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
  struct cache_arg cmdarg;

  if (!g_initialized)
    return -1;

  cmdarg.mtime = ref;

  return commands_exec_sync(cmdbase, cache_artwork_purge_cruft_impl, NULL, &cmdarg);
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
  struct cache_arg cmdarg;

  if (!g_initialized)
    return -1;

  cmdarg.type = type;
  cmdarg.persistentid = persistentid;
  cmdarg.max_w = max_w;
  cmdarg.max_h = max_h;
  cmdarg.format = format;
  cmdarg.path = filename;
  cmdarg.evbuf = evbuf;

  return commands_exec_sync(cmdbase, cache_artwork_add_impl, NULL, &cmdarg);
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
  struct cache_arg cmdarg;
  int ret;

  if (!g_initialized)
    {
      *cached = 0;
      *format = 0;
      return 0;
    }

  cmdarg.type = type;
  cmdarg.persistentid = persistentid;
  cmdarg.max_w = max_w;
  cmdarg.max_h = max_h;
  cmdarg.evbuf = evbuf;

  ret = commands_exec_sync(cmdbase, cache_artwork_get_impl, NULL, &cmdarg);

  *format = cmdarg.format;
  *cached = cmdarg.cached;

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
cache_artwork_stash(struct evbuffer *evbuf, const char *path, int format)
{
  struct cache_arg cmdarg;

  if (!g_initialized)
    return -1;

  cmdarg.evbuf = evbuf;
  cmdarg.path = path;
  cmdarg.format = format;

  return commands_exec_sync(cmdbase, cache_artwork_stash_impl, NULL, &cmdarg);
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
cache_artwork_read(struct evbuffer *evbuf, const char *path, int *format)
{
  struct cache_arg cmdarg;
  int ret;

  if (!g_initialized)
    return -1;

  cmdarg.evbuf = evbuf;
  cmdarg.path = path;

  ret = commands_exec_sync(cmdbase, cache_artwork_read_impl, NULL, &cmdarg);

  *format = cmdarg.format;

  return ret;
}


/* --------------------------- Cache general API ---------------------------- */

int
cache_init(void)
{
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

  CHECK_NULL(L_CACHE, evbase_cache = event_base_new());
  CHECK_NULL(L_CACHE, cache_daap_updateev = evtimer_new(evbase_cache, cache_daap_update_cb, NULL));
  CHECK_NULL(L_CACHE, cache_xcode_updateev = evtimer_new(evbase_cache, cache_xcode_update_cb, NULL));
  CHECK_NULL(L_CACHE, cmdbase = commands_base_new(evbase_cache, NULL));
  CHECK_ERR(L_CACHE, listener_add(cache_daap_listener_cb, LISTENER_DATABASE));
  CHECK_ERR(L_CACHE, pthread_create(&tid_cache, NULL, cache, NULL));
  thread_setname(tid_cache, "cache");

  DPRINTF(E_INFO, L_CACHE, "cache thread init\n");

  return 0;
}

void
cache_deinit(void)
{
  int ret;

  if (!g_initialized)
    return;

  g_initialized = 0;

  listener_remove(cache_daap_listener_cb);

  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_cache, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_CACHE, "Could not join cache thread: %s\n", strerror(errno));
      return;
    }

  // Free event base
  event_free(cache_daap_updateev);
  event_base_free(evbase_cache);
}
