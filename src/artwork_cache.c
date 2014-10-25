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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <pthread.h>

#include <sqlite3.h>

#include "conffile.h"
#include "logger.h"
#include "misc.h"
#include "db_utils.h"


static int g_initialized;
static __thread sqlite3 *g_db_hdl;
static char *g_db_path;


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
artworkcache_ping(char *path, time_t mtime, int del)
{
#define Q_TMPL_PING "UPDATE artwork SET db_timestamp = %" PRIi64 " WHERE filepath = '%q' AND db_timestamp >= %" PRIi64 ";"
#define Q_TMPL_DEL "DELETE FROM artwork WHERE filepath = '%q' AND db_timestamp < %" PRIi64 ";"

  char *query;
  char *errmsg;
  int ret;

  if (!g_initialized)
    return -1;

  query = sqlite3_mprintf(Q_TMPL_PING, (int64_t)time(NULL), path, (int64_t)mtime);

  DPRINTF(E_DBG, L_ACACHE, "Running query '%s'\n", query);

  ret = dbutils_exec(g_db_hdl, query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  if (!del)
    return 0;

  query = sqlite3_mprintf(Q_TMPL_DEL, path, (int64_t)mtime);

  DPRINTF(E_DBG, L_ACACHE, "Running query '%s'\n", query);

  ret = dbutils_exec(g_db_hdl, query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  return 0;

#undef Q_TMPL_PING
#undef Q_TMPL_DEL
}

/*
 * Removes all cache entries for the given path
 *
 * @param path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork)
 * @return 0 if successful, -1 if an error occurred
 */
int
artworkcache_delete_by_path(char *path)
{
#define Q_TMPL_DEL "DELETE FROM artwork WHERE filepath = '%q';"

  char *query;
  char *errmsg;
  int ret;

  if (!g_initialized)
    return -1;

  query = sqlite3_mprintf(Q_TMPL_DEL, path);

  DPRINTF(E_DBG, L_ACACHE, "Running query '%s'\n", query);

  ret = dbutils_exec(g_db_hdl, query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Query error: %s\n", errmsg);

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
 * @param ref reference timestamp
 * @return 0 if successful, -1 if an error occurred
 */
int
artworkcache_purge_cruft(time_t ref)
{
#define Q_TMPL "DELETE FROM artwork WHERE db_timestamp < %" PRIi64 ";"

  char *query;
  char *errmsg;
  int ret;

  if (!g_initialized)
    return -1;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)ref);

  DPRINTF(E_DBG, L_ACACHE, "Running purge query '%s'\n", query);

  ret = dbutils_exec(g_db_hdl, query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  DPRINTF(E_DBG, L_ACACHE, "Purged %d rows\n", sqlite3_changes(g_db_hdl));

  sqlite3_free(query);

  return 0;

#undef Q_TMPL
}

/*
 * Adds the given (scaled) artwork image to the artwork cache
 *
 * @param persistentid persistent songalbumid or songartistid
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param format ART_FMT_PNG for png, ART_FMT_JPEG for jpeg or 0 if no artwork available
 * @param filename the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork) or empty if no artwork available
 * @param data the (scaled) image
 * @param datalen lengt of data
 * @return 0 if successful, -1 if an error occurred
 */
int
artworkcache_add(int64_t peristentid, int max_w, int max_h, int format, char *filename, char *data, int datalen)
{
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  if (!g_initialized)
    return -1;

  query = "INSERT INTO artwork (id, persistentid, max_w, max_h, format, filepath, db_timestamp, data) VALUES (NULL, ?, ?, ?, ?, ?, ?, ?);";

  ret = dbutils_blocking_prepare_v2(g_db_hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(g_db_hdl));
      return -1;
    }

  sqlite3_bind_int64(stmt, 1, peristentid);
  sqlite3_bind_int(stmt, 2, max_w);
  sqlite3_bind_int(stmt, 3, max_h);
  sqlite3_bind_int(stmt, 4, format);
  sqlite3_bind_text(stmt, 5, filename, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 6, (uint64_t)time(NULL));
  sqlite3_bind_blob(stmt, 7, data, datalen, SQLITE_STATIC);

  ret = dbutils_blocking_step(g_db_hdl, stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_ACACHE, "No results\n");
      else
	DPRINTF(E_LOG, L_ACACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));

      sqlite3_finalize(stmt);
      return -1;
    }

  sqlite3_finalize(stmt);

  return 0;
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
 * @param data set by this function to the scaled image
 * @param datalen set by this function to the length of data
 * @return 0 if successful, -1 if an error occurred
 */
int
artworkcache_get(int64_t persistentid, int max_w, int max_h, int *cached, int *format, char **data, int *datalen)
{
#define Q_TMPL "SELECT a.format, a.data FROM artwork a WHERE a.persistentid = '%" PRId64 "' AND a.max_w = %d AND a.max_h = %d;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  if (!g_initialized)
    return -1;

  query = sqlite3_mprintf(Q_TMPL, persistentid, max_w, max_h);
  if (!query)
    {
      DPRINTF(E_LOG, L_ACACHE, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_ACACHE, "Running query '%s'\n", query);
  ret = dbutils_blocking_prepare_v2(g_db_hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(g_db_hdl));
      ret = -1;
      goto out;
    }

  ret = dbutils_blocking_step(g_db_hdl, stmt);
  if (ret != SQLITE_ROW)
    {
      *cached = 0;

      if (ret == SQLITE_DONE)
	{
	  ret = 0;
	  DPRINTF(E_DBG, L_ACACHE, "No results\n");
	}
      else
	{
	  ret = -1;
	  DPRINTF(E_LOG, L_ACACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));
	}

      sqlite3_finalize(stmt);
      goto out;
    }

  *format = sqlite3_column_int(stmt, 0);
  *datalen = sqlite3_column_bytes(stmt, 1);
  *data = (char *) malloc(*datalen);
  memcpy(*data, sqlite3_column_blob(stmt, 1), *datalen);
  *cached = 1;

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
  ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  ret = 0;

 out: sqlite3_free(query);
  return ret;
#undef Q_TMPL
}

int
artworkcache_perthread_init(void)
{
  int ret;
  int cache_size;
  int page_size;
  char *journal_mode;
  int synchronous;

  ret = sqlite3_open(g_db_path, &g_db_hdl);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Could not open database: %s\n", sqlite3_errmsg(g_db_hdl));

      sqlite3_close(g_db_hdl);
      return -1;
    }

#ifdef DB_PROFILE
  sqlite3_profile(g_db_hdl, db_xprofile, NULL);
#endif

  page_size = 4096; //cfg_getint(cfg_getsec(cfg, "general"), "db_pragma_page_size");
  if (page_size > -1)
    {
      dbutils_pragma_set_page_size(g_db_hdl, page_size);
      page_size = dbutils_pragma_get_page_size(g_db_hdl);
      DPRINTF(E_DBG, L_ACACHE, "Artwork cache page size in pages: %d\n", page_size);
    }

  cache_size = 5000; //cfg_getint(cfg_getsec(cfg, "general"), "db_pragma_cache_size");
  if (cache_size > -1)
    {
      dbutils_pragma_set_cache_size(g_db_hdl, cache_size);
      page_size = dbutils_pragma_get_cache_size(g_db_hdl);
      DPRINTF(E_DBG, L_ACACHE, "Artwork cache cache size in pages: %d\n", cache_size);
    }

  journal_mode = "OFF"; //cfg_getstr(cfg_getsec(cfg, "general"), "db_pragma_journal_mode");
  if (journal_mode)
    {
      journal_mode = dbutils_pragma_set_journal_mode(g_db_hdl, journal_mode);
      DPRINTF(E_DBG, L_ACACHE, "Artwork cache journal mode: %s\n", journal_mode);
    }

  synchronous = 0; //cfg_getint(cfg_getsec(cfg, "general"), "db_pragma_synchronous");
  if (synchronous > -1)
    {
      dbutils_pragma_set_synchronous(g_db_hdl, synchronous);
      synchronous = dbutils_pragma_get_synchronous(g_db_hdl);
      DPRINTF(E_DBG, L_ACACHE, "Artwork cache synchronous: %d\n", synchronous);
    }

  return 0;
}

void
artworkcache_perthread_deinit(void)
{
  sqlite3_stmt *stmt;

  if (!g_db_hdl)
    return;

  /* Tear down anything that's in flight */
  while ((stmt = sqlite3_next_stmt(g_db_hdl, 0)))
    sqlite3_finalize(stmt);

  sqlite3_close(g_db_hdl);
}

#define T_ADMIN_ARTWORK				\
  "CREATE TABLE IF NOT EXISTS admin_artwork("	\
  "   key   VARCHAR(32) NOT NULL,"		\
  "   value VARCHAR(32) NOT NULL"		\
  ");"

#define T_ARTWORK					\
  "CREATE TABLE IF NOT EXISTS artwork ("		\
  "   id                  INTEGER PRIMARY KEY NOT NULL,"\
  "   persistentid        INTEGER NOT NULL,"		\
  "   max_w               INTEGER NOT NULL,"		\
  "   max_h               INTEGER NOT NULL,"		\
  "   format              INTEGER NOT NULL,"		\
  "   filepath            VARCHAR(4096) NOT NULL,"	\
  "   db_timestamp        INTEGER DEFAULT 0,"		\
  "   data                BLOB"				\
  ");"

#define I_ARTWORK_ID				\
  "CREATE INDEX IF NOT EXISTS idx_persistentidwh ON artwork(persistentid, max_w, max_h);"
#define I_ARTWORK_PATH				\
  "CREATE INDEX IF NOT EXISTS idx_pathtime ON artwork(filepath, db_timestamp);"


#define CACHE_VERSION 1
#define Q_CACHE_VERSION					\
  "INSERT INTO admin_artwork (key, value) VALUES ('cache_version', '1');"

struct db_init_query {
  char *query;
  char *desc;
};

/*
 * List of all queries necessary to initialize the cache database
 */
static const struct db_init_query db_init_queries[] =
  {
    { T_ADMIN_ARTWORK,   "create table admin" },
    { T_ARTWORK,         "create table artwork" },

    { I_ARTWORK_ID,      "create artwork persistentid index" },
    { I_ARTWORK_PATH,    "create artwork filepath index" },

    { Q_CACHE_VERSION,   "set cache version" },
  };


#define D_DROP_IDX_ARTWORK_ID		\
  "DROP INDEX IF EXISTS idx_persistentidwh;"

#define D_DROP_IDX_ARTWORK_PATH		\
  "DROP INDEX IF EXISTS idx_pathtime;"

#define D_DROP_ARTWORK			\
  "DROP TABLE IF EXISTS artwork;"

#define D_DROP_ADMIN_ARTWORK		\
  "DROP TABLE IF EXISTS admin_artwork;"

static const struct db_init_query db_drop_queries[] =
  {
    { D_DROP_IDX_ARTWORK_ID,   "drop artwork persistentid index" },
    { D_DROP_IDX_ARTWORK_PATH, "drop artwork path index" },
    { D_DROP_ARTWORK,          "drop table artwork" },
    { D_DROP_ADMIN_ARTWORK,    "drop table admin artwork" },
  };

static int
artworkcache_create_tables(void)
{
  char *errmsg;
  int i;
  int ret;

  for (i = 0; i < (sizeof(db_init_queries) / sizeof(db_init_queries[0])); i++)
    {
      DPRINTF(E_DBG, L_ACACHE, "DB init query: %s\n", db_init_queries[i].desc);

      ret = sqlite3_exec(g_db_hdl, db_init_queries[i].query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_FATAL, L_ACACHE, "DB init error: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  return 0;
}

static int
artworkcache_check_version(void)
{
#define Q_VER "SELECT value FROM admin_artwork WHERE key = 'cache_version';"
#define Q_VACUUM "VACUUM;"
  sqlite3_stmt *stmt;
  char *errmsg;
  int cur_ver;
  int i;
  int nqueries;
  int ret;

  DPRINTF(E_DBG, L_ACACHE, "Running query '%s'\n", Q_VER);

  ret = sqlite3_prepare_v2(g_db_hdl, Q_VER, strlen(Q_VER) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_ACACHE, "Could not prepare statement: %s\n", sqlite3_errmsg(g_db_hdl));
      return 1;
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_ACACHE, "Could not step: %s\n", sqlite3_errmsg(g_db_hdl));

      sqlite3_finalize(stmt);
      return -1;
    }

  cur_ver = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);

  if (cur_ver != CACHE_VERSION)
    {
      DPRINTF(E_LOG, L_ACACHE, "Database schema outdated, deleting artwork cache v%d -> v%d\n", cur_ver, CACHE_VERSION);

      nqueries = sizeof(db_drop_queries);
      for (i = 0; i < nqueries; i++)
	{
	  DPRINTF(E_DBG, L_ACACHE, "DB upgrade query: %s\n", db_drop_queries[i].desc);

	  ret = sqlite3_exec(g_db_hdl, db_drop_queries[i].query, NULL, NULL, &errmsg);
	  if (ret != SQLITE_OK)
	    {
	      DPRINTF(E_FATAL, L_ACACHE, "DB upgrade error: %s\n", errmsg);

	      sqlite3_free(errmsg);
	      return -1;
	    }
	}

      /* What about some housekeeping work, eh? */
      DPRINTF(E_INFO, L_ACACHE, "Now vacuuming database, this may take some time...\n");

      ret = sqlite3_exec(g_db_hdl, Q_VACUUM, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_ACACHE, "Could not VACUUM database: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}

      return 1;
    }

  return 0;

#undef Q_VER
#undef Q_VACUUM
}

int
artworkcache_init(void)
{
  int ret;

  g_initialized = 0;

  g_db_path = cfg_getstr(cfg_getsec(cfg, "general"), "artworkcache_path");
  if (!g_db_path || (strlen(g_db_path) == 0))
    {
      DPRINTF(E_LOG, L_ACACHE, "Artwork cache path invalid, disabling cache\n");
      return 0;
    }

  ret = artworkcache_perthread_init();
  if (ret < 0)
    return ret;

  ret = artworkcache_check_version();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_ACACHE, "Artwork cache version check errored out, incompatible database\n");

      artworkcache_perthread_deinit();
      return -1;
    }
  else if (ret > 0)
    {
      DPRINTF(E_FATAL, L_ACACHE, "Could not check artwork cache version, trying DB init\n");

      ret = artworkcache_create_tables();
      if (ret < 0)
      {
	DPRINTF(E_FATAL, L_ACACHE, "Could not create tables\n");
	artworkcache_perthread_deinit();
	return -1;
      }
    }

  g_initialized = 1;

  artworkcache_perthread_deinit();

  return 0;
}

