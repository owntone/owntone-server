/*
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2010 Kai Elwert <elwertk@googlemail.com>
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
#include "db.h"


static int is_artwork_cache_enabled;
static char *db_path;
static __thread sqlite3 *hdl;




/* Image cache */
int
db_artwork_add(int itemid, int groupid, int max_w, int max_h, int dataid)
{
#define Q_TMPL "INSERT INTO images (id, item_id, group_id, max_w, max_h, data_id) VALUES (NULL, %d, %d, %d, %d, %d);"
  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, itemid, groupid, (max_w > 0 ? max_w : 0), (max_h > 0 ? max_h : 0), dataid);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(hdl, query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  return 0;

#undef Q_TMPL
}

int
db_artwork_file_add(int format, char *filename, int max_w, int max_h, char *data, int datalen)
{
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  query = "INSERT INTO imagedata (id, format, filepath, max_w, max_h, data) VALUES (NULL, ?, ?, ?, ?, ?);";
  ret = db_blocking_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_bind_int(stmt, 1, format);
  sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, max_w);
  sqlite3_bind_int(stmt, 4, max_h);
  sqlite3_bind_blob(stmt, 5, data, datalen, SQLITE_STATIC);

  ret = db_blocking_step(hdl, stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      sqlite3_finalize(stmt);
      ret = sqlite3_last_insert_rowid(hdl);

      return ret;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);
  sqlite3_finalize(stmt);

  return 0;
}

int
db_artwork_get(int itemid, int groupid, int max_w, int max_h, int *cached, int *dataid)
{
#define Q_TMPL "SELECT i.data_id FROM images i WHERE i.item_id = %d AND i.group_id = %d AND i.max_w = %d AND i.max_h = %d;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, itemid, groupid, (max_w > 0 ? max_w : 0), (max_h > 0 ? max_h : 0));
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      ret = -1;
      goto out;
    }

  ret = db_blocking_step(hdl, stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret != SQLITE_DONE)
	{
	  DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
	  ret = -1;
	}
      else
	ret = 0;

      sqlite3_finalize(stmt);

      goto out;
    }

  *cached  = 1;
  *dataid  = sqlite3_column_int(stmt, 0);

#ifdef DB_PROFILE
  while (db_blocking_step(hdl, stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);

  ret = 0;

 out:
  sqlite3_free(query);
  return ret;

#undef Q_TMPL
}

int
db_artwork_file_get(int id, int *format, char **data, int *datalen)
{
#define Q_TMPL "SELECT i.format, i.data FROM imagedata i WHERE i.id = %d;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      ret = -1;
      goto out;
    }

  ret = db_blocking_step(hdl, stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret != SQLITE_DONE)
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);

      ret = -1;
      goto out;
    }

  *format  = sqlite3_column_int(stmt, 0);
  *datalen = sqlite3_column_bytes(stmt, 1);
  *data = (char *)malloc(*datalen);
  memcpy(*data, sqlite3_column_blob(stmt, 1), *datalen);

#ifdef DB_PROFILE
  while (db_blocking_step(hdl, stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);

  ret = 0;

 out:
  sqlite3_free(query);
  return ret;

#undef Q_TMPL
}

int
db_artwork_file_get_by_path_and_size(char *path, int max_w, int max_h, int *id, int *format, char **data, int *datalen)
{
#define Q_TMPL "SELECT i.id, i.format, i.data FROM imagedata i WHERE i.filepath = '%q' AND max_w = %d AND max_h = %d;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path, max_w, max_h);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      ret = -1;
      goto out;
    }

  ret = db_blocking_step(hdl, stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret != SQLITE_DONE)
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);

      ret = -1;
      goto out;
    }

  *id      = sqlite3_column_int(stmt, 0);
  *format  = sqlite3_column_int(stmt, 1);
  *datalen = sqlite3_column_bytes(stmt, 2);
  *data = (char *)malloc(*datalen);
  memcpy(*data, sqlite3_column_blob(stmt, 2), *datalen);

#ifdef DB_PROFILE
  while (db_blocking_step(hdl, stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);

  ret = 0;

 out:
  sqlite3_free(query);
  return ret;

#undef Q_TMPL
}

int
db_artwork_perthread_init(void)
{
  //char *errmsg;
  int ret;
  int cache_size;
  char *journal_mode;
  int synchronous;

  ret = sqlite3_open(db_path, &hdl);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not open database: %s\n", sqlite3_errmsg(hdl));

      sqlite3_close(hdl);
      return -1;
    }

#ifdef DB_PROFILE
  sqlite3_profile(hdl, db_xprofile, NULL);
#endif

  cache_size = cfg_getint(cfg_getsec(cfg, "general"), "db_pragma_cache_size");
  if (cache_size > -1)
    {
      db_pragma_set_cache_size(hdl, cache_size);
      cache_size = db_pragma_get_cache_size(hdl);
      DPRINTF(E_DBG, L_DB, "Database cache size in pages: %d\n", cache_size);
    }

  journal_mode = cfg_getstr(cfg_getsec(cfg, "general"), "db_pragma_journal_mode");
  if (journal_mode)
    {
      journal_mode = db_pragma_set_journal_mode(hdl, journal_mode);
      DPRINTF(E_DBG, L_DB, "Database journal mode: %s\n", journal_mode);
    }

  synchronous = cfg_getint(cfg_getsec(cfg, "general"), "db_pragma_synchronous");
  if (synchronous > -1)
    {
      db_pragma_set_synchronous(hdl, synchronous);
      synchronous = db_pragma_get_synchronous(hdl);
      DPRINTF(E_DBG, L_DB, "Database synchronous: %d\n", synchronous);
    }

  return 0;
}

void
db_artwork_perthread_deinit(void)
{
  sqlite3_stmt *stmt;

  if (!hdl)
    return;

  /* Tear down anything that's in flight */
  while ((stmt = sqlite3_next_stmt(hdl, 0)))
    sqlite3_finalize(stmt);

  sqlite3_close(hdl);
}

#define T_ADMIN_ARTWORK				\
  "CREATE TABLE IF NOT EXISTS admin_artwork("	\
  "   key   VARCHAR(32) NOT NULL,"		\
  "   value VARCHAR(32) NOT NULL"		\
  ");"

#define T_IMAGES					\
  "CREATE TABLE IF NOT EXISTS images ("			\
  "   id             INTEGER PRIMARY KEY NOT NULL,"	\
  "   item_id        INTEGER NOT NULL,"			\
  "   group_id       INTEGER NOT NULL,"			\
  "   max_w          INTEGER NOT NULL,"			\
  "   max_h          INTEGER NOT NULL,"			\
  "   data_id        INTEGER NOT NULL"			\
  ");"

#define T_IMAGEDATA					\
  "CREATE TABLE IF NOT EXISTS imagedata ("		\
  "   id             INTEGER PRIMARY KEY NOT NULL,"	\
  "   format         INTEGER NOT NULL,"			\
  "   filepath       VARCHAR(4096) NOT NULL,"		\
  "   max_w          INTEGER NOT NULL,"			\
  "   max_h          INTEGER NOT NULL,"			\
  "   data           BLOB"				\
  ");"

#define I_IMAGE				\
  "CREATE INDEX IF NOT EXISTS idx_itemid_groupid ON images(item_id, group_id, max_w, max_h);"

#define CACHE_VERSION 1
#define Q_CACHE_VERSION					\
  "INSERT INTO admin_artwork (key, value) VALUES ('cache_version', '1');"

static const struct db_init_query db_init_queries[] =
  {
    { T_ADMIN_ARTWORK,   "create table admin" },
    { T_IMAGES,          "create table images" },
    { T_IMAGEDATA,       "create table imagedata" },

    { I_IMAGE,           "create image index" },

    { Q_CACHE_VERSION,   "set cache version" },
  };


#define D_DROP_IDX_IMAGE		\
  "DROP INDEX IF EXISTS idx_itemid_groupid;"

#define D_DROP_IMAGES			\
  "DROP TABLE IF EXISTS images;"

#define D_DROP_IMAGEDATA		\
  "DROP TABLE IF EXISTS imagedata;"

#define D_DROP_ADMIN_ARTWORK		\
  "DROP TABLE IF EXISTS admin_artwork;"

static const struct db_init_query db_drop_queries[] =
  {
    { D_DROP_IDX_IMAGE,       "drop image index" },

    { D_DROP_IMAGES,          "drop table images" },
    { D_DROP_IMAGEDATA,       "drop table imagedata" },

    { D_DROP_ADMIN_ARTWORK,   "drop table admin artwork" },
  };

static int
db_artwork_create_tables(void)
{
  char *errmsg;
  int i;
  int ret;

  for (i = 0; i < (sizeof(db_init_queries) / sizeof(db_init_queries[0])); i++)
    {
      DPRINTF(E_DBG, L_DB, "DB init query: %s\n", db_init_queries[i].desc);

      ret = sqlite3_exec(hdl, db_init_queries[i].query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_FATAL, L_DB, "DB init error: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  return 0;
}

static int
db_artwork_check_version(void)
{
#define Q_VER "SELECT value FROM admin_artwork WHERE key = 'cache_version';"
#define Q_VACUUM "VACUUM;"
  sqlite3_stmt *stmt;
  char *errmsg;
  int cur_ver;
  int i;
  int nqueries;
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", Q_VER);

  ret = sqlite3_prepare_v2(hdl, Q_VER, strlen(Q_VER) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return 1;
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      return -1;
    }

  cur_ver = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);

  if (cur_ver != CACHE_VERSION)
    {
      DPRINTF(E_LOG, L_DB, "Database schema outdated, deleting artwork cache v%d -> v%d\n", cur_ver, CACHE_VERSION);

      nqueries = sizeof(db_drop_queries);
      for (i = 0; i < nqueries; i++)
	{
	  DPRINTF(E_DBG, L_DB, "DB upgrade query: %s\n", db_drop_queries[i].desc);

	  ret = sqlite3_exec(hdl, db_drop_queries[i].query, NULL, NULL, &errmsg);
	  if (ret != SQLITE_OK)
	    {
	      DPRINTF(E_FATAL, L_DB, "DB upgrade error: %s\n", errmsg);

	      sqlite3_free(errmsg);
	      return -1;
	    }
	}

      /* What about some housekeeping work, eh? */
      DPRINTF(E_INFO, L_DB, "Now vacuuming database, this may take some time...\n");

      ret = sqlite3_exec(hdl, Q_VACUUM, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Could not VACUUM database: %s\n", errmsg);

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
db_artwork_init(void)
{
  int ret;

  db_path = cfg_getstr(cfg_getsec(cfg, "general"), "artwork_cache_path");

  if (db_path)
    {
      DPRINTF(E_LOG, L_DB, "Artwork cache enabled\n");
      is_artwork_cache_enabled = 1;
    }
  else
    {
      DPRINTF(E_LOG, L_DB, "Artwork cache disabled\n");
      is_artwork_cache_enabled = 0;
      return 0;
    }

  ret = db_artwork_perthread_init();
  if (ret < 0)
    return ret;

  ret = db_artwork_check_version();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_DB, "Database version check errored out, incompatible database\n");

      db_artwork_perthread_deinit();
      return -1;
    }
  else if (ret > 0)
    {
      DPRINTF(E_FATAL, L_DB, "Could not check database version, trying DB init\n");

      ret = db_artwork_create_tables();
      if (ret < 0)
      {
	DPRINTF(E_FATAL, L_DB, "Could not create tables\n");
	db_artwork_perthread_deinit();
	return -1;
      }
    }

  db_artwork_perthread_deinit();

  return 0;
}
