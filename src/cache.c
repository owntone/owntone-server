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
#include <stdbool.h>
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
#include "httpd.h" // TODO get rid of this, only used for httpd_gzip_deflate
#include "httpd_daap.h"
#include "transcode.h"
#include "db.h"
#include "worker.h"
#include "cache.h"
#include "listener.h"
#include "commands.h"

struct cache_arg
{
  sqlite3 *hdl; // which cache database

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

struct cache_db_def
{
  const char *name;
  const char *create_query;
  const char *drop_query;
};

struct cache_artwork_stash
{
  char *path;
  int format;
  size_t size;
  uint8_t *data;
};

struct cache_xcode_job
{
  const char *format;
  char *file_path;
  int file_id;

  struct event *ev;
  bool is_encoding;

  struct evbuffer *header;
};


/* --------------------------------- GLOBALS -------------------------------- */

// cache thread
static pthread_t tid_cache;

// Event base, pipes and events
static struct event_base *evbase_cache;
static struct commands_base *cmdbase;

// State
static bool cache_is_initialized;
static bool cache_is_suspended;

#define DB_DEF_ADMIN \
  { \
    "admin", \
    "CREATE TABLE IF NOT EXISTS admin(" \
    " key VARCHAR(32) PRIMARY KEY NOT NULL,"  \
    " value VARCHAR(32) NOT NULL" \
    ");", \
    "DROP TABLE IF EXISTS admin;", \
  }

// DAAP cache
#define CACHE_DAAP_VERSION 5
static sqlite3 *cache_daap_hdl;
static struct event *cache_daap_updateev;
// The user may configure a threshold (in msec), and queries slower than
// that will have their reply cached
static int cache_daap_threshold;
static struct cache_db_def cache_daap_db_def[] = {
  DB_DEF_ADMIN,
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
};

// Artwork cache
#define CACHE_ARTWORK_VERSION 5
static sqlite3 *cache_artwork_hdl;
static struct cache_artwork_stash cache_stash;
static struct cache_db_def cache_artwork_db_def[] = {
  DB_DEF_ADMIN,
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
};

// Transcoding cache
#define CACHE_XCODE_VERSION 1
#define CACHE_XCODE_NTHREADS 4
#define CACHE_XCODE_FORMAT_MP4 "mp4"
static sqlite3 *cache_xcode_hdl;
static struct event *cache_xcode_updateev;
static struct event *cache_xcode_prepareev;
static struct cache_xcode_job cache_xcode_jobs[CACHE_XCODE_NTHREADS];
static bool cache_xcode_is_enabled;
static struct cache_db_def cache_xcode_db_def[] = {
  DB_DEF_ADMIN,
  {
    "files",
    "CREATE TABLE IF NOT EXISTS files ("
    "   id                 INTEGER PRIMARY KEY NOT NULL,"
    "   time_modified      INTEGER DEFAULT 0,"
    "   filepath           VARCHAR(4096) NOT NULL"
    ");",
    "DROP TABLE IF EXISTS files;",
  },
  {
    "data",
    "CREATE TABLE IF NOT EXISTS data ("
    "   id                 INTEGER PRIMARY KEY NOT NULL,"
    "   timestamp          INTEGER DEFAULT 0,"
    "   file_id            INTEGER DEFAULT 0,"
    "   format             VARCHAR(255) NOT NULL,"
    "   header             BLOB,"
    "   UNIQUE(file_id, format) ON CONFLICT REPLACE"
    ");",
    "DROP TABLE IF EXISTS data;",
  },
};


/* --------------------------------- HELPERS -------------------------------- */

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


/* ---------------------------------- MAIN ---------------------------------- */
/*                                Thread: cache                               */

static int
cache_tables_create(sqlite3 *hdl, int version, struct cache_db_def *db_def, int db_def_size)
{
#define Q_CACHE_VERSION "INSERT INTO admin (key, value) VALUES ('cache_version', '%d');"
  char *query;
  char *errmsg;
  int ret;
  int i;

  for (i = 0; i < db_def_size; i++)
    {
      ret = sqlite3_exec(hdl, db_def[i].create_query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_FATAL, L_CACHE, "Error creating cache db entity '%s': %s\n", db_def[i].name, errmsg);
	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  query = sqlite3_mprintf(Q_CACHE_VERSION, version);
  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_CACHE, "Error inserting cache version: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  return 0;
#undef Q_CACHE_VERSION
}

static int
cache_tables_drop(sqlite3 *hdl, struct cache_db_def *db_def, int db_def_size)
{
#define Q_VACUUM	"VACUUM;"
  char *errmsg;
  int ret;
  int i;

  for (i = 0; i < db_def_size; i++)
    {
      ret = sqlite3_exec(hdl, db_def[i].drop_query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_FATAL, L_CACHE, "Error dropping cache db entity '%s': %s\n", db_def[i].name, errmsg);
	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  ret = sqlite3_exec(hdl, Q_VACUUM, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error vacuuming cache database: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  return 0;
#undef Q_VACUUM
}

static int
cache_version_check(int *have_version, sqlite3 *hdl, int want_version)
{
#define Q_VER "SELECT value FROM admin WHERE key = 'cache_version';"
  sqlite3_stmt *stmt;
  int ret;

  *have_version = 0;

  ret = sqlite3_prepare_v2(hdl, Q_VER, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      return 0; // Virgin database, admin table doesn't exists
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not step: %s\n", sqlite3_errmsg(hdl));
      sqlite3_finalize(stmt);
      return -1;
    }

  *have_version = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  return 0;
#undef Q_VER
}

static int
cache_pragma_set(sqlite3 *hdl)
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

  // Set page cache size in number of pages
  cache_size = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_cache_size_cache");
  if (cache_size > -1)
    {
      query = sqlite3_mprintf(Q_PRAGMA_CACHE_SIZE, cache_size);
      ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
      sqlite3_free(query);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_cache_size_cache: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  // Set journal mode
  journal_mode = cfg_getstr(cfg_getsec(cfg, "sqlite"), "pragma_journal_mode");
  if (journal_mode)
    {
      query = sqlite3_mprintf(Q_PRAGMA_JOURNAL_MODE, journal_mode);
      ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
      sqlite3_free(query);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_journal_mode: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  // Set synchronous flag
  synchronous = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_synchronous");
  if (synchronous > -1)
    {
      query = sqlite3_mprintf(Q_PRAGMA_SYNCHRONOUS, synchronous);
      ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
      sqlite3_free(query);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_synchronous: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  // Set mmap size
  mmap_size = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_mmap_size_cache");
  if (synchronous > -1)
    {
      query = sqlite3_mprintf(Q_PRAGMA_MMAP_SIZE, mmap_size);
      ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error setting pragma_mmap_size: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  return 0;
#undef Q_PRAGMA_CACHE_SIZE
#undef Q_PRAGMA_JOURNAL_MODE
#undef Q_PRAGMA_SYNCHRONOUS
#undef Q_PRAGMA_MMAP_SIZE
}

static void
cache_close_one(sqlite3 **hdl)
{
  sqlite3_stmt *stmt;

  if (!*hdl)
    return;

  /* Tear down anything that's in flight */
  while ((stmt = sqlite3_next_stmt(*hdl, 0)))
    sqlite3_finalize(stmt);

  sqlite3_close(*hdl);
  *hdl = NULL;
}

static void
cache_close(void)
{
  cache_close_one(&cache_daap_hdl);
  cache_close_one(&cache_artwork_hdl);
  cache_close_one(&cache_xcode_hdl);

  DPRINTF(E_DBG, L_CACHE, "Cache closed\n");
}

static int
cache_open_one(sqlite3 **hdl, const char *path, const char *name, int want_version, struct cache_db_def *db_def, int db_def_size)
{
  sqlite3 *h;
  int have_version;
  int ret;

  ret = sqlite3_open(path, &h);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not open '%s': %s\n", path, sqlite3_errmsg(h));
      goto error;
    }

  ret = cache_version_check(&have_version, h, want_version);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not check cache '%s' database version\n", name);
      goto error;
    }

  if (have_version > 0 && have_version < want_version)
    {
      DPRINTF(E_LOG, L_CACHE, "Database schema outdated, deleting cache '%s' v%d -> v%d\n", name, have_version, want_version);

      ret = cache_tables_drop(h, db_def, db_def_size);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error deleting '%s' database tables\n", name);
	  goto error;
	}
    }

  if (have_version < want_version)
    {
      ret = cache_tables_create(h, want_version, db_def, db_def_size);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_CACHE, "Could not create cache '%s' database tables\n", name);
	  goto error;
	}

      DPRINTF(E_INFO, L_CACHE, "Cache '%s' database tables created\n", name);
    }

  *hdl = h;
  return 0;

 error:
  sqlite3_close(h);
  return -1;
}

static int
cache_open(void)
{
  const char *directory;
  const char *filename;
  char *daap_db_path;
  char *artwork_db_path;
  char *xcode_db_path;
  int ret;

  directory = cfg_getstr(cfg_getsec(cfg, "general"), "cache_dir");

  CHECK_NULL(L_DB, filename = cfg_getstr(cfg_getsec(cfg, "general"), "cache_daap_filename"));
  CHECK_NULL(L_DB, daap_db_path = safe_asprintf("%s%s", directory, filename));

  CHECK_NULL(L_DB, filename = cfg_getstr(cfg_getsec(cfg, "general"), "cache_artwork_filename"));
  CHECK_NULL(L_DB, artwork_db_path = safe_asprintf("%s%s", directory, filename));

  CHECK_NULL(L_DB, filename = cfg_getstr(cfg_getsec(cfg, "general"), "cache_xcode_filename"));
  CHECK_NULL(L_DB, xcode_db_path = safe_asprintf("%s%s", directory, filename));

  ret = cache_open_one(&cache_daap_hdl, daap_db_path, "daap", CACHE_DAAP_VERSION, cache_daap_db_def, ARRAY_SIZE(cache_daap_db_def));
  if (ret < 0)
    goto error;

  ret = cache_open_one(&cache_artwork_hdl, artwork_db_path, "artwork", CACHE_ARTWORK_VERSION, cache_artwork_db_def, ARRAY_SIZE(cache_artwork_db_def));
  if (ret < 0)
    goto error;

  ret = cache_open_one(&cache_xcode_hdl, xcode_db_path, "xcode", CACHE_XCODE_VERSION, cache_xcode_db_def, ARRAY_SIZE(cache_xcode_db_def));
  if (ret < 0)
    goto error;

  ret = cache_pragma_set(cache_artwork_hdl);
  if (ret < 0)
    goto error;

  DPRINTF(E_DBG, L_CACHE, "Cache opened\n");

  free(daap_db_path);
  free(artwork_db_path);
  free(xcode_db_path);
  return 0;

 error:
  cache_close();
  free(daap_db_path);
  free(artwork_db_path);
  free(xcode_db_path);
  return -1;
}


/* Adds the reply (stored in evbuf) to the cache */
static int
cache_daap_reply_add(sqlite3 *hdl, const char *query, struct evbuffer *evbuf)
{
#define Q_TMPL "INSERT INTO replies (query, reply) VALUES (?, ?);"
  sqlite3_stmt *stmt;
  unsigned char *data;
  size_t datalen;
  int ret;

  datalen = evbuffer_get_length(evbuf);
  data = evbuffer_pullup(evbuf, -1);

  ret = sqlite3_prepare_v2(hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, data, datalen, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_CACHE, "Error stepping query for cache update: %s\n", sqlite3_errmsg(hdl));
      sqlite3_finalize(stmt);
      return -1;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error finalizing query for cache update: %s\n", sqlite3_errmsg(hdl));
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
  struct cache_arg *cmdarg = arg;
  struct timeval delay = { 60, 0 };
  char *query;
  char *errmsg;
  int ret;

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

  ret = sqlite3_exec(cmdarg->hdl, query, NULL, NULL, &errmsg);
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
  ret = sqlite3_exec(cmdarg->hdl, Q_CLEANUP, NULL, NULL, &errmsg);
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
  struct cache_arg *cmdarg = arg;
  sqlite3_stmt *stmt;
  char *query;
  int datalen;
  int ret;

  query = cmdarg->query;
  remove_tag(query, "session-id");
  remove_tag(query, "revision-number");

  // Look in the DB
  ret = sqlite3_prepare_v2(cmdarg->hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing query for cache update: %s\n", sqlite3_errmsg(cmdarg->hdl));
      free(query);
      *retval = -1;
      return COMMAND_END;
    }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)  
    {
      if (ret != SQLITE_DONE)
	DPRINTF(E_LOG, L_CACHE, "Error stepping query for cache update: %s\n", sqlite3_errmsg(cmdarg->hdl));
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
    DPRINTF(E_LOG, L_CACHE, "Error finalizing query for getting cache: %s\n", sqlite3_errmsg(cmdarg->hdl));

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
cache_daap_query_delete(sqlite3 *hdl, const int id)
{
#define Q_TMPL "DELETE FROM queries WHERE id = %d;"
  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, id);

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
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
  sqlite3 *hdl = cache_daap_hdl;
  sqlite3_stmt *stmt;
  struct evbuffer *evbuf;
  struct evbuffer *gzbuf;
  char *errmsg;
  char *query;
  int ret;

  if (cache_is_suspended)
    {
      DPRINTF(E_DBG, L_CACHE, "Got a request to update DAAP cache while suspended\n");
      return;
    }

  DPRINTF(E_INFO, L_CACHE, "Beginning DAAP cache update\n");

  ret = sqlite3_exec(hdl, "DELETE FROM replies;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error clearing reply cache before update: %s\n", errmsg);
      sqlite3_free(errmsg);
      return;
    }

  ret = sqlite3_prepare_v2(hdl, "SELECT id, user_agent, is_remote, query FROM queries;", -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing for cache update: %s\n", sqlite3_errmsg(hdl));
      return;
    }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      query = strdup((char *)sqlite3_column_text(stmt, 3));

      evbuf = daap_reply_build(query, (char *)sqlite3_column_text(stmt, 1), sqlite3_column_int(stmt, 2));
      if (!evbuf)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error building DAAP reply for query: %s\n", query);
	  cache_daap_query_delete(hdl, sqlite3_column_int(stmt, 0));
	  free(query);

	  continue;
	}

      gzbuf = httpd_gzip_deflate(evbuf);
      if (!gzbuf)
	{
	  DPRINTF(E_LOG, L_CACHE, "Error gzipping DAAP reply for query: %s\n", query);
	  cache_daap_query_delete(hdl, sqlite3_column_int(stmt, 0));
	  free(query);
	  evbuffer_free(evbuf);

	  continue;
	}

      evbuffer_free(evbuf);

      cache_daap_reply_add(hdl, query, gzbuf);

      free(query);
      evbuffer_free(gzbuf);
    }

  if (ret != SQLITE_DONE)
    DPRINTF(E_LOG, L_CACHE, "Could not step: %s\n", sqlite3_errmsg(hdl));

  sqlite3_finalize(stmt);

  DPRINTF(E_INFO, L_CACHE, "DAAP cache updated\n");
}


/* ----------------------- Caching of transcoded data ----------------------- */

static void
xcode_job_clear(struct cache_xcode_job *job)
{
  free(job->file_path);
  if (job->header)
    evbuffer_free(job->header);

  // Can't just memset to zero, because *ev is persistent
  job->format = NULL;
  job->file_path = NULL;
  job->file_id = 0;
  job->header = NULL;
  job->is_encoding = false;
}

static enum command_state
xcode_header_get(void *arg, int *retval)
{
#define Q_TMPL "SELECT header FROM data WHERE length(header) > 0 AND file_id = ? AND format = ?;"
  struct cache_arg *cmdarg = arg;
  sqlite3_stmt *stmt = NULL;
  int ret;

  cmdarg->cached = 0;

  ret = sqlite3_prepare_v2(cmdarg->hdl, Q_TMPL, -1, &stmt, 0);
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
  DPRINTF(E_LOG, L_CACHE, "Database error getting prepared header from cache: %s\n", sqlite3_errmsg(cmdarg->hdl));
  if (stmt)
    sqlite3_finalize(stmt);
  *retval = -1;
  return COMMAND_END;
#undef Q_TMPL
}

static void
xcode_trigger(void)
{
  struct timeval delay_xcode = { 5, 0 };

  if (cache_xcode_is_enabled)
    event_add(cache_xcode_updateev, &delay_xcode);
}

static enum command_state
xcode_toggle(void *arg, int *retval)
{
  bool *enable = arg;

  if (*enable == cache_xcode_is_enabled)
    goto end;

  cache_xcode_is_enabled = *enable;
  xcode_trigger();

 end:
  *retval = 0;
  return COMMAND_END;
}

static int
xcode_add_entry(sqlite3 *hdl, uint32_t id, uint32_t ts, const char *path)
{
#define Q_TMPL "INSERT OR REPLACE INTO files (id, time_modified, filepath) VALUES (%d, %d, '%q');"
  char *query;
  char *errmsg;
  int ret;

  DPRINTF(E_SPAM, L_CACHE, "Adding xcode file id %d, path '%s'\n", id, path);

  query = sqlite3_mprintf(Q_TMPL, id, ts, path);

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
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
xcode_del_entry(sqlite3 *hdl, uint32_t id)
{
#define Q_TMPL_FILES "DELETE FROM files WHERE id = %d;"
#define Q_TMPL_DATA "DELETE FROM data WHERE file_id = %d;"
  char query[256];
  char *errmsg;
  int ret;

  DPRINTF(E_SPAM, L_CACHE, "Deleting xcode file id %d\n", id);

  sqlite3_snprintf(sizeof(query), query, Q_TMPL_FILES, (int)id);
  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error deleting row from xcode files: %s\n", errmsg);
      sqlite3_free(errmsg);
      return -1;
    }

  sqlite3_snprintf(sizeof(query), query, Q_TMPL_DATA, (int)id);
  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
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
xcode_sync_with_files(sqlite3 *hdl)
{
  sqlite3_stmt *stmt;
  struct cachelist *cachelist = NULL;
  size_t cachelist_size = 0;
  size_t cachelist_len = 0;
  struct query_params qp = { .type = Q_ITEMS, .filter = "f.data_kind = 0", .order = "f.id" };
  struct db_media_file_info dbmfi;
  struct db_media_file_info *rowA;
  struct cachelist *rowB;
  uint32_t id;
  uint32_t ts;
  int cmp;
  int i;
  int ret;

  // Both lists must be sorted by id, otherwise the compare below won't work
  ret = sqlite3_prepare_v2(hdl, "SELECT id, time_modified FROM files ORDER BY id;", -1, &stmt, 0);
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

  // Silence false maybe-uninitialized warning
  rowB = NULL;

  // Loop while either list ("A" files list, "B" cache list) has remaining items
  for(i = 0, cmp = 0;;)
    {
      if (cmp <= 0)
        rowA = (db_query_fetch_file(&dbmfi, &qp) == 0) ? &dbmfi : NULL;;
      if (cmp >= 0)
        rowB = (i < cachelist_len) ? &cachelist[i++] : NULL;
      if (!rowA && !rowB)
        break; // Done with both lists

#if 0
      if (rowA)
	DPRINTF(E_DBG, L_CACHE, "cmp %d, rowA->id %s\n", cmp, rowA->id);
      if (rowB)
	DPRINTF(E_DBG, L_CACHE, "cmp %d, rowB->id %u, i %d, cachelist_len %zu\n", cmp, rowB->id, i, cachelist_len);
#endif

      if (rowA)
	{
	  safe_atou32(rowA->id, &id);
	  safe_atou32(rowA->time_modified, &ts);
	}

      cmp = 0; // In both lists - unless:
      if (!rowB || (rowA && rowB->id > id)) // A had an item not in B
	{
	  xcode_add_entry(hdl, id, ts, rowA->path);
	  cmp = -1;
	}
      else if (!rowA || (rowB && rowB->id < id)) // B had an item not in A
	{
	  xcode_del_entry(hdl, rowB->id);
	  cmp = 1;
	}
      else if (rowB->id == id && rowB->ts < ts) // Item in B is too old
	{
	  xcode_del_entry(hdl, rowB->id);
	  xcode_add_entry(hdl, id, ts, rowA->path);
	}
    }

  db_query_end(&qp);

  free(cachelist);
  return 0;

 error:
  DPRINTF(E_LOG, L_CACHE, "Database error while processing xcode files table\n");
  free(cachelist);
  return -1;
}

static int
xcode_header_save(sqlite3 *hdl, int file_id, const char *format, uint8_t *data, size_t datalen)
{
#define Q_TMPL "INSERT INTO data (timestamp, file_id, format, header) VALUES (?, ?, ?, ?);"
  sqlite3_stmt *stmt;
  int ret;

  ret = sqlite3_prepare_v2(hdl, Q_TMPL, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error preparing xcode data for cache update: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  sqlite3_bind_int(stmt, 1, (uint64_t)time(NULL));
  sqlite3_bind_int(stmt, 2, file_id);
  sqlite3_bind_text(stmt, 3, format, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 4, data, datalen, SQLITE_STATIC);

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_CACHE, "Error stepping xcode data for cache update: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  sqlite3_finalize(stmt);
  return 0;
#undef Q_TMPL
}

static int
xcode_file_next(int *file_id, char **file_path, sqlite3 *hdl, const char *format)
{
#define Q_TMPL "SELECT f.id, f.filepath, d.id FROM files f LEFT JOIN data d ON f.id = d.file_id AND d.format = '%q' WHERE d.id IS NULL LIMIT 1;"
  sqlite3_stmt *stmt;
  char query[256];
  int ret;

  sqlite3_snprintf(sizeof(query), query, Q_TMPL, format);

  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error occured while finding next file to prepare header for\n");
      return -1;
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      sqlite3_finalize(stmt);
      return -1; // All done
    }

  *file_id = sqlite3_column_int(stmt, 0);
  *file_path = strdup((char *)sqlite3_column_text(stmt, 1));

  sqlite3_finalize(stmt);

  // Save an empty header so next call to this function will return a new file
  return xcode_header_save(hdl, *file_id, format, NULL, 0);
#undef Q_TMPL
}

// Thread: worker
static void
xcode_worker(void *arg)
{
  struct cache_xcode_job *job = *(struct cache_xcode_job **)arg;
  int ret;

  DPRINTF(E_DBG, L_CACHE, "Preparing %s header for '%s' (file id %d)\n", job->format, job->file_path, job->file_id);

  if (strcmp(job->format, CACHE_XCODE_FORMAT_MP4) == 0)
    {
      ret = transcode_prepare_header(&job->header, XCODE_MP4_ALAC, job->file_path);
      if (ret < 0)
	DPRINTF(E_LOG, L_CACHE, "Error preparing %s header for '%s' (file id %d)\n", job->format, job->file_path, job->file_id);
    }

  // Tell the cache thread that we are done. Only the cache thread can save the
  // result to the DB.
  event_active(job->ev, 0, 0);
}

static void
cache_xcode_job_complete_cb(int fd, short what, void *arg)
{
  struct cache_xcode_job *job = arg;
  uint8_t *data;
  size_t datalen;

  if (job->header)
    {
#if 1
      datalen = evbuffer_get_length(job->header);
      data = evbuffer_pullup(job->header, -1);
#else
      data = (unsigned char*)"dummy";
      datalen = 6;
#endif
      xcode_header_save(cache_xcode_hdl, job->file_id, job->format, data, datalen);
    }

  xcode_job_clear(job); // Makes the job available again
  event_active(cache_xcode_prepareev, 0, 0);
}

// Preparing headers can take very long, so we use worker threads. However, all
// DB access must be from the cache thread. So this function will find the next
// file from the db and then dispatch a thread for the encoding.
static void
cache_xcode_prepare_cb(int fd, short what, void *arg)
{
  struct cache_xcode_job *job = NULL;
  bool is_encoding = false;
  int ret;
  int i;

  if (!cache_is_initialized)
    return;

  for (i = 0; i < ARRAY_SIZE(cache_xcode_jobs); i++)
    {
      if (cache_xcode_jobs[i].is_encoding)
	is_encoding = true;
      else if (!job)
	job = &cache_xcode_jobs[i];
    }

  if (!job)
    return; // No available thread right now, wait for cache_xcode_job_complete_cb()

  ret = xcode_file_next(&job->file_id, &job->file_path, cache_xcode_hdl, CACHE_XCODE_FORMAT_MP4);
  if (ret < 0)
    {
      if (!is_encoding)
	DPRINTF(E_LOG, L_CACHE, "Header generation completed\n");

      return;
    }
  else if (!is_encoding)
    DPRINTF(E_LOG, L_CACHE, "Kicking off header generation\n");

  job->is_encoding = true;
  job->format = CACHE_XCODE_FORMAT_MP4;

  worker_execute(xcode_worker, &job, sizeof(struct cache_xcode_job *), 0);

  // Set off more threads
  event_active(cache_xcode_prepareev, 0, 0);
}

static void
cache_xcode_update_cb(int fd, short what, void *arg)
{
  if (xcode_sync_with_files(cache_xcode_hdl) < 0)
    return;

  event_active(cache_xcode_prepareev, 0, 0);
}

/* Sets off an update by activating the event. The delay is because we are low
 * priority compared to other listeners of database updates.
 */
static enum command_state
cache_database_update(void *arg, int *retval)
{
  struct timeval delay_daap = { 10, 0 };

  event_add(cache_daap_updateev, &delay_daap);

// TODO unlink or rename cache.db

  xcode_trigger();

  *retval = 0;
  return COMMAND_END;
}

/* Callback from filescanner thread */
static void
cache_daap_listener_cb(short event_mask, void *ctx)
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
  struct cache_arg *cmdarg = arg;
  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL_PING, (int64_t)time(NULL), cmdarg->pathcopy, (int64_t)cmdarg->mtime);

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

  ret = sqlite3_exec(cmdarg->hdl, query, NULL, NULL, &errmsg);
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

      ret = sqlite3_exec(cmdarg->hdl, query, NULL, NULL, &errmsg);
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
  struct cache_arg *cmdarg = arg;
  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL_DEL, cmdarg->path);

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

  ret = sqlite3_exec(cmdarg->hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      *retval = -1;
      return COMMAND_END;
    }

  DPRINTF(E_DBG, L_CACHE, "Deleted %d rows\n", sqlite3_changes(cmdarg->hdl));

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

  struct cache_arg *cmdarg = arg;
  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)cmdarg->mtime);

  DPRINTF(E_DBG, L_CACHE, "Running purge query '%s'\n", query);

  ret = sqlite3_exec(cmdarg->hdl, query, NULL, NULL, &errmsg);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      *retval = -1;
      return COMMAND_END;
    }

  DPRINTF(E_DBG, L_CACHE, "Purged %d rows\n", sqlite3_changes(cmdarg->hdl));

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
  struct cache_arg *cmdarg = arg;
  sqlite3_stmt *stmt;
  char *query;
  uint8_t *data;
  int datalen;
  int ret;

  query = "INSERT INTO artwork (id, persistentid, max_w, max_h, format, filepath, db_timestamp, data, type) VALUES (NULL, ?, ?, ?, ?, ?, ?, ?, ?);";

  ret = sqlite3_prepare_v2(cmdarg->hdl, query, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(cmdarg->hdl));
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
      DPRINTF(E_LOG, L_CACHE, "Error stepping query for artwork add: %s\n", sqlite3_errmsg(cmdarg->hdl));
      sqlite3_finalize(stmt);
      *retval = -1;
      return COMMAND_END;
    }

  ret = sqlite3_finalize(stmt);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Error finalizing query for artwork add: %s\n", sqlite3_errmsg(cmdarg->hdl));
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
  struct cache_arg *cmdarg = arg;
  sqlite3_stmt *stmt;
  char *query;
  int datalen;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, cmdarg->type, cmdarg->persistentid, cmdarg->max_w, cmdarg->max_h);
  if (!query)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for query string\n");
      *retval = -1;
      return COMMAND_END;
    }

  DPRINTF(E_DBG, L_CACHE, "Running query '%s'\n", query);

  ret = sqlite3_prepare_v2(cmdarg->hdl, query, -1, &stmt, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(cmdarg->hdl));
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
	  DPRINTF(E_LOG, L_CACHE, "Could not step: %s\n", sqlite3_errmsg(cmdarg->hdl));
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
    DPRINTF(E_LOG, L_CACHE, "Error finalizing query for getting cache: %s\n", sqlite3_errmsg(cmdarg->hdl));

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
  struct cache_arg *cmdarg = arg;

  // Clear current stash
  if (cache_stash.path)
    {
      free(cache_stash.path);
      free(cache_stash.data);
      memset(&cache_stash, 0, sizeof(struct cache_artwork_stash));
    }

  // If called with no evbuf then we are done, we just needed to clear the stash
  if (!cmdarg->evbuf)
    {
      *retval = 0;
      return COMMAND_END;
    }

  cache_stash.size = evbuffer_get_length(cmdarg->evbuf);
  cache_stash.data = malloc(cache_stash.size);
  if (!cache_stash.data)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork stash data\n");
      *retval = -1;
      return COMMAND_END;
    }

  cache_stash.path = strdup(cmdarg->path);
  if (!cache_stash.path)
    {
      DPRINTF(E_LOG, L_CACHE, "Out of memory for artwork stash path\n");
      free(cache_stash.data);
      *retval = -1;
      return COMMAND_END;
    }

  cache_stash.format = cmdarg->format;

  *retval = evbuffer_copyout(cmdarg->evbuf, cache_stash.data, cache_stash.size);
  return COMMAND_END;
}

static enum command_state
cache_artwork_read_impl(void *arg, int *retval)
{
  struct cache_arg *cmdarg = arg;

  cmdarg->format = 0;

  if (!cache_stash.path || !cache_stash.data || (strcmp(cache_stash.path, cmdarg->path) != 0))
    {
      *retval = -1;
      return COMMAND_END;
    }

  cmdarg->format = cache_stash.format;

  DPRINTF(E_DBG, L_CACHE, "Stash hit (format %d, size %zu): %s\n", cache_stash.format, cache_stash.size, cache_stash.path);

  *retval = evbuffer_add(cmdarg->evbuf, cache_stash.data, cache_stash.size);
  return COMMAND_END;
}

static void *
cache(void *arg)
{
  int ret;
  int i;

  ret = cache_open();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: Cache create failed. Cache will be disabled.\n");
      pthread_exit(NULL);
    }

  // The thread needs a connection with the main db, so it can generate DAAP
  // replies through httpd_daap.c and read changes from the files table
  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Error: DB init failed. Cache will be disabled.\n");
      cache_close();

      pthread_exit(NULL);
    }

  CHECK_NULL(L_CACHE, cache_daap_updateev = evtimer_new(evbase_cache, cache_daap_update_cb, NULL));
  CHECK_NULL(L_CACHE, cache_xcode_updateev = evtimer_new(evbase_cache, cache_xcode_update_cb, NULL));
  CHECK_NULL(L_CACHE, cache_xcode_prepareev = evtimer_new(evbase_cache, cache_xcode_prepare_cb, NULL));
  CHECK_ERR(L_CACHE, event_priority_set(cache_xcode_prepareev, 0));
  for (i = 0; i < ARRAY_SIZE(cache_xcode_jobs); i++)
    CHECK_NULL(L_CACHE, cache_xcode_jobs[i].ev = evtimer_new(evbase_cache, cache_xcode_job_complete_cb, &cache_xcode_jobs[i]));

  CHECK_ERR(L_CACHE, listener_add(cache_daap_listener_cb, LISTENER_DATABASE, NULL));

  cache_is_initialized = 1;

  event_base_dispatch(evbase_cache);

  if (cache_is_initialized)
    {
      DPRINTF(E_LOG, L_CACHE, "Cache event loop terminated ahead of time!\n");
      cache_is_initialized = 0;
    }

  listener_remove(cache_daap_listener_cb);

  for (i = 0; i < ARRAY_SIZE(cache_xcode_jobs); i++)
    event_free(cache_xcode_jobs[i].ev);
  event_free(cache_xcode_prepareev);
  event_free(cache_xcode_updateev);
  event_free(cache_daap_updateev);

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
  cache_is_suspended = 1;
}

void
cache_daap_resume(void)
{
  cache_is_suspended = 0;
}

int
cache_daap_get(struct evbuffer *evbuf, const char *query)
{
  struct cache_arg cmdarg;

  if (!cache_is_initialized)
    return -1;

  cmdarg.hdl = cache_daap_hdl;
  cmdarg.query = strdup(query);
  cmdarg.evbuf = evbuf;

  return commands_exec_sync(cmdbase, cache_daap_query_get, NULL, &cmdarg);
}

void
cache_daap_add(const char *query, const char *ua, int is_remote, int msec)
{
  struct cache_arg *cmdarg;

  if (!cache_is_initialized)
    return;

  cmdarg = calloc(1, sizeof(struct cache_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not allocate cache_arg\n");
      return;
    }

  cmdarg->hdl = cache_daap_hdl;
  cmdarg->query = strdup(query);
  cmdarg->ua = strdup(ua);
  cmdarg->is_remote = is_remote;
  cmdarg->msec = msec;

  commands_exec_async(cmdbase, cache_daap_query_add, cmdarg);
}

int
cache_daap_threshold_get(void)
{
  return cache_daap_threshold;
}


/* --------------------------- Transcode cache API  ------------------------- */

int
cache_xcode_header_get(struct evbuffer *evbuf, int *cached, uint32_t id, const char *format)
{
  struct cache_arg cmdarg;
  int ret;

  if (!cache_is_initialized)
    return -1;

  cmdarg.hdl = cache_xcode_hdl;
  cmdarg.evbuf = evbuf;
  cmdarg.id = id;
  cmdarg.header_format = format;

  ret = commands_exec_sync(cmdbase, xcode_header_get, NULL, &cmdarg);

  *cached = cmdarg.cached;

  return ret;
}

int
cache_xcode_toggle(bool enable)
{
  if (!cache_is_initialized)
    return -1;

  return commands_exec_sync(cmdbase, xcode_toggle, NULL, &enable);
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

  if (!cache_is_initialized)
    return;

  cmdarg = calloc(1, sizeof(struct cache_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_CACHE, "Could not allocate cache_arg\n");
      return;
    }

  cmdarg->hdl = cache_artwork_hdl;
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

  if (!cache_is_initialized)
    return -1;

  cmdarg.hdl = cache_artwork_hdl;
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

  if (!cache_is_initialized)
    return -1;

  cmdarg.hdl = cache_artwork_hdl;
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

  if (!cache_is_initialized)
    return -1;

  cmdarg.hdl = cache_artwork_hdl;
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

  if (!cache_is_initialized)
    {
      *cached = 0;
      *format = 0;
      return 0;
    }

  cmdarg.hdl = cache_artwork_hdl;
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

  if (!cache_is_initialized)
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

  if (!cache_is_initialized)
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
  cache_daap_threshold = cfg_getint(cfg_getsec(cfg, "general"), "cache_daap_threshold");
  if (cache_daap_threshold == 0)
    {
      DPRINTF(E_LOG, L_CACHE, "Cache threshold set to 0, disabling cache\n");
      return 0;
    }

  CHECK_NULL(L_CACHE, evbase_cache = event_base_new());
  CHECK_ERR(L_CACHE, event_base_priority_init(evbase_cache, 8));
  CHECK_NULL(L_CACHE, cmdbase = commands_base_new(evbase_cache, NULL));

  CHECK_ERR(L_CACHE, pthread_create(&tid_cache, NULL, cache, NULL));
  thread_setname(tid_cache, "cache");

  DPRINTF(E_INFO, L_CACHE, "Cache thread init\n");

  return 0;
}

void
cache_deinit(void)
{
  int ret;

  if (!cache_is_initialized)
    return;

  cache_is_initialized = 0;

  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_cache, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_CACHE, "Could not join cache thread: %s\n", strerror(errno));
      return;
    }

  event_base_free(evbase_cache);
}
