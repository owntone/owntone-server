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


#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/mman.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "logger.h"
#include "misc.h"


struct db_upgrade_query {
  char *query;
  char *desc;
};

static int
db_drop_indices(sqlite3 *hdl)
{
#define Q_INDEX "SELECT name FROM sqlite_master WHERE type == 'index' AND name LIKE 'idx_%';"
#define Q_TMPL "DROP INDEX %q;"
  sqlite3_stmt *stmt;
  char *errmsg;
  char *query;
  char *index[256];
  int ret;
  int i;
  int n;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", Q_INDEX);

  ret = sqlite3_prepare_v2(hdl, Q_INDEX, strlen(Q_INDEX) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  n = 0;
  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      index[n] = strdup((char *)sqlite3_column_text(stmt, 0));
      n++;
    }

  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);

      ret = -1;
      goto out;
    }

  sqlite3_finalize(stmt);

  for (i = 0; i < n; i++)
    {
      query = sqlite3_mprintf(Q_TMPL, index[i]);

      DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

      ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "DB error while running '%s': %s\n", query, errmsg);

	  sqlite3_free(errmsg);
	  sqlite3_free(query);

	  ret = -1;
	  goto out;
	}

      sqlite3_free(query);
    }

 out:
  for (i = 0; i < n; i++)
    free(index[i]);
  return ret;
#undef Q_TMPL
#undef Q_INDEX
}

static int
db_drop_triggers(sqlite3 *hdl)
{
#define Q_TRIGGER "SELECT name FROM sqlite_master WHERE type == 'trigger' AND name LIKE 'trg_%';"
#define Q_TMPL "DROP TRIGGER %q;"
  sqlite3_stmt *stmt;
  char *errmsg;
  char *query;
  char *trigger[256];
  int ret;
  int i;
  int n;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", Q_TRIGGER);

  ret = sqlite3_prepare_v2(hdl, Q_TRIGGER, strlen(Q_TRIGGER) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  n = 0;
  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      trigger[n] = strdup((char *)sqlite3_column_text(stmt, 0));
      n++;
    }

  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      return -1;
    }

  sqlite3_finalize(stmt);

  for (i = 0; i < n; i++)
    {
      query = sqlite3_mprintf(Q_TMPL, trigger[i]);
      free(trigger[i]);

      DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

      ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "DB error while running '%s': %s\n", query, errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}

      sqlite3_free(query);
    }

  return 0;
#undef Q_TMPL
#undef Q_TRIGGER
}


static int
db_generic_upgrade(sqlite3 *hdl, const struct db_upgrade_query *queries, unsigned int nqueries)
{
  char *errmsg;
  int i;
  int ret;

  for (i = 0; i < nqueries; i++, queries++)
    {
      DPRINTF(E_DBG, L_DB, "DB upgrade query: %s\n", queries->desc);

      ret = sqlite3_exec(hdl, queries->query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_FATAL, L_DB, "DB upgrade error: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  return 0;
}

/* The below implements relevant parts of SQLITE's recommended 12 steps to
 * altering a table. It is not required to use this function if you just want to
 * add a column). The steps:
 * 1.  If foreign key constraints are enabled, disable them using PRAGMA
 *     foreign_keys=OFF.
 * 2.  Start a transaction.
 * 3.  Remember the format of all indexes and triggers associated with table X.
 *     This information will be needed in step 8 below. One way to do this is to
 *     run a query like the following: SELECT type, sql FROM sqlite_master WHERE
 *     tbl_name='X'.
 * 4.  Use CREATE TABLE to construct a new table "new_X" that is in the desired
 *     revised format of table X. Make sure that the name "new_X" does not
 *     collide with any existing table name, of course.
 * 5.  Transfer content from X into new_X using a statement like: INSERT INTO
 *     new_X SELECT ... FROM X.
 * 6.  Drop the old table X: DROP TABLE X.
 * 7.  Change the name of new_X to X using: ALTER TABLE new_X RENAME TO X.
 * 8.  Use CREATE INDEX and CREATE TRIGGER to reconstruct indexes and triggers
 *     associated with table X. Perhaps use the old format of the triggers and
 *     indexes saved from step 3 above as a guide, making changes as appropriate
 *     for the alteration.
 * 9.  If any views refer to table X in a way that is affected by the schema
 *     change, then drop those views using DROP VIEW and recreate them with
 *     whatever changes are necessary to accommodate the schema change using
 *     CREATE VIEW.
 * 10. If foreign key constraints were originally enabled then run PRAGMA
 *     foreign_key_check to verify that the schema change did not break any
 *     foreign key constraints.
 * 11. Commit the transaction started in step 2.
 * 12. If foreign keys constraints were originally enabled, reenable them now.
 * Source: https://www.sqlite.org/lang_altertable.html
 */
static int
db_table_upgrade(sqlite3 *hdl, const char *name, const char *newtablequery)
{
  sqlite3_stmt *stmt;
  char *query;
  char *errmsg;
  int ret;

  DPRINTF(E_LOG, L_DB, "Upgrading %s table...\n", name);

  // Step 1: Skipped, no foreign key constraints
  // Step 2: Skipped, we are already in a transaction
  // Step 3: Nothing to do, we already know our indexes and triggers
  // Step 4: Create the new table using table definition from db_init, but with
  // new_ prefixed to the name
  CHECK_NULL(L_DB, query = sqlite3_mprintf(newtablequery));

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    goto error;

  sqlite3_free(query);

  // Step 5: Transfer content - note: no support for changed column names or dropped columns!
  // This will select the column names from our new table (which where given to us in newtablequery)
  CHECK_NULL(L_DB, query = sqlite3_mprintf("SELECT group_concat(name) FROM pragma_table_info('new_%s');", name));

  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      errmsg = sqlite3_mprintf("%s", sqlite3_errmsg(hdl));
      goto error;
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
        errmsg = sqlite3_mprintf("Getting col names from pragma_table_info returned nothing");
      else
        errmsg = sqlite3_mprintf("%s", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      goto error;
    }

  sqlite3_free(query);

  CHECK_NULL(L_DB, query = sqlite3_mprintf("INSERT INTO new_%s SELECT %s FROM %s;", name, sqlite3_column_text(stmt, 0), name));

  sqlite3_finalize(stmt);

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    goto error;

  sqlite3_free(query);

  // Step 6: Drop old table
  CHECK_NULL(L_DB, query = sqlite3_mprintf("DROP TABLE %s;", name));

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    goto error;

  sqlite3_free(query);

  // Step 7: Give the new table the final name
  CHECK_NULL(L_DB, query = sqlite3_mprintf("ALTER TABLE new_%s RENAME TO %s;", name, name));

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    goto error;

  sqlite3_free(query);

  // Step 8: Skipped, will be done by db_check_version in db.c
  // Step 9: Skipped, no views
  // Step 10: Skipped, no foreign key constraints
  // Step 11: Skipped, our caller takes care of COMMIT
  // Step 12: Skipped, no foreign key constraints

  DPRINTF(E_LOG, L_DB, "Upgrade of %s table complete!\n", name);

  return 0;

 error:
  DPRINTF(E_LOG, L_DB, "DB error %d running query '%s': %s\n", ret, query, errmsg);
  sqlite3_free(query);
  sqlite3_free(errmsg);

  return -1;
}

/* Upgrade from schema v17.00 to v18.00 */
/* Change playlist type enumeration and recreate filelist view (include smart
 * playlists in view)
 */

#define U_V18_PL_TYPE_CHANGE_PLAIN				\
  "UPDATE playlists SET type = 3 WHERE type = 0;"
#define U_V18_PL_TYPE_CHANGE_SPECIAL				\
  "UPDATE playlists SET type = 0 WHERE type = 2;"
#define U_V18_DROP_VIEW_FILELIST				\
  "DROP VIEW IF EXISTS filelist;"
#define U_V18_CREATE_VIEW_FILELIST				\
  "CREATE VIEW IF NOT EXISTS filelist as"			\
  "     SELECT "						\
  "       virtual_path, time_modified, 3 as type "		\
  "     FROM files WHERE disabled = 0"				\
  "   UNION "							\
  "     SELECT "						\
  "       virtual_path, db_timestamp, 1 as type "		\
  "     FROM playlists where disabled = 0 AND type IN (2, 3)"	\
  ";"

#define U_V18_SCVER_MAJOR			\
  "UPDATE admin SET value = '18' WHERE key = 'schema_version_major';"
#define U_V18_SCVER_MINOR			\
  "UPDATE admin SET value = '00' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v18_queries[] =
  {
    { U_V18_PL_TYPE_CHANGE_PLAIN, "changing numbering of plain playlists 0 -> 3" },
    { U_V18_PL_TYPE_CHANGE_SPECIAL, "changing numbering of default playlists 2 -> 0" },
    { U_V18_DROP_VIEW_FILELIST, "dropping view filelist" },
    { U_V18_CREATE_VIEW_FILELIST, "creating view filelist" },

    { U_V18_SCVER_MAJOR,    "set schema_version_major to 18" },
    { U_V18_SCVER_MINOR,    "set schema_version_minor to 00" },
  };

/* Upgrade from schema v18.00 to v18.01 */
/* Change virtual_path for playlists: remove file extension
 */

#define U_V1801_UPDATE_PLAYLISTS_M3U						\
  "UPDATE playlists SET virtual_path = replace(virtual_path, '.m3u', '');"
#define U_V1801_UPDATE_PLAYLISTS_PLS						\
  "UPDATE playlists SET virtual_path = replace(virtual_path, '.pls', '');"
#define U_V1801_UPDATE_PLAYLISTS_SMARTPL					\
  "UPDATE playlists SET virtual_path = replace(virtual_path, '.smartpl', '');"

#define U_V1801_SCVER_MAJOR			\
  "UPDATE admin SET value = '18' WHERE key = 'schema_version_major';"
#define U_V1801_SCVER_MINOR			\
  "UPDATE admin SET value = '01' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1801_queries[] =
  {
    { U_V1801_UPDATE_PLAYLISTS_M3U, "update table playlists" },
    { U_V1801_UPDATE_PLAYLISTS_PLS, "update table playlists" },
    { U_V1801_UPDATE_PLAYLISTS_SMARTPL, "update table playlists" },

    { U_V1801_SCVER_MAJOR,    "set schema_version_major to 18" },
    { U_V1801_SCVER_MINOR,    "set schema_version_minor to 01" },
  };

/* Upgrade from schema v18.01 to v19.00 */
/* Replace 'filelist' view with new table 'directories'
 */

#define U_V1900_CREATE_TABLE_DIRECTORIES						\
  "CREATE TABLE IF NOT EXISTS directories ("			\
  "   id                  INTEGER PRIMARY KEY NOT NULL,"	\
  "   virtual_path        VARCHAR(4096) NOT NULL,"		\
  "   db_timestamp        INTEGER DEFAULT 0,"			\
  "   disabled            INTEGER DEFAULT 0,"			\
  "   parent_id           INTEGER DEFAULT 0"			\
  ");"

#define U_V1900_DROP_VIEW_FILELIST \
  "DROP VIEW IF EXISTS filelist;"
#define U_V1900_ALTER_PL_ADD_DIRECTORYID			\
  "ALTER TABLE playlists ADD COLUMN directory_id INTEGER DEFAULT 0;"
#define U_V1900_ALTER_FILES_ADD_DIRECTORYID			\
  "ALTER TABLE files ADD COLUMN directory_id INTEGER DEFAULT 0;"
#define U_V1900_ALTER_FILES_ADD_DATERELEASED			\
  "ALTER TABLE files ADD COLUMN date_released INTEGER DEFAULT 0;"
#define U_V1900_ALTER_SPEAKERS_ADD_NAME			\
  "ALTER TABLE speakers ADD COLUMN name VARCHAR(255) DEFAULT NULL;"

#define U_V1900_INSERT_DIR1 \
  "INSERT INTO directories (id, virtual_path, db_timestamp, disabled, parent_id)" \
  " VALUES (1, '/', 0, 0, 0);"
#define U_V1900_INSERT_DIR2 \
  "INSERT INTO directories (id, virtual_path, db_timestamp, disabled, parent_id)" \
  " VALUES (2, '/file:', 0, 0, 1);"
#define U_V1900_INSERT_DIR3 \
  "INSERT INTO directories (id, virtual_path, db_timestamp, disabled, parent_id)" \
  " VALUES (3, '/http:', 0, 0, 1);"
#define U_V1900_INSERT_DIR4 \
  "INSERT INTO directories (id, virtual_path, db_timestamp, disabled, parent_id)" \
  " VALUES (4, '/spotify:', 0, 4294967296, 1);"

#define U_V1900_SCVER_MAJOR			\
  "UPDATE admin SET value = '19' WHERE key = 'schema_version_major';"
#define U_V1900_SCVER_MINOR			\
  "UPDATE admin SET value = '00' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1900_queries[] =
  {
    { U_V1900_CREATE_TABLE_DIRECTORIES,    "create table directories" },
    { U_V1900_ALTER_PL_ADD_DIRECTORYID,    "alter table pl add column directory_id" },
    { U_V1900_ALTER_FILES_ADD_DIRECTORYID, "alter table files add column directory_id" },
    { U_V1900_ALTER_FILES_ADD_DATERELEASED,"alter table files add column date_released" },
    { U_V1900_ALTER_SPEAKERS_ADD_NAME,     "alter table speakers add column name" },
    { U_V1900_INSERT_DIR1,                 "insert root directory" },
    { U_V1900_INSERT_DIR2,                 "insert /file: directory" },
    { U_V1900_INSERT_DIR3,                 "insert /htttp: directory" },
    { U_V1900_INSERT_DIR4,                 "insert /spotify: directory" },
    { U_V1900_DROP_VIEW_FILELIST,          "drop view directories" },

    { U_V1900_SCVER_MAJOR,    "set schema_version_major to 19" },
    { U_V1900_SCVER_MINOR,    "set schema_version_minor to 00" },
  };

int
db_upgrade_v19_directory_id(sqlite3 *hdl, char *virtual_path)
{
  sqlite3_stmt *stmt;
  char *query;
  int id;
  int ret;

  query = sqlite3_mprintf("SELECT d.id FROM directories d WHERE d.disabled = 0 AND d.virtual_path = '%q';", virtual_path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DB, "Error preparing query '%s'\n", query);
      sqlite3_free(query);

      return -1;
    }

  ret = sqlite3_step(stmt);

  if (ret == SQLITE_ROW)
    id = sqlite3_column_int(stmt, 0);
  else if (ret == SQLITE_DONE)
    id = 0; // Not found
  else
    {
      DPRINTF(E_LOG, L_DB, "Error stepping query '%s'\n", query);
      sqlite3_free(query);
      sqlite3_finalize(stmt);

      return -1;
    }

  sqlite3_free(query);
  sqlite3_finalize(stmt);

  return id;
}

int
db_upgrade_v19_insert_directory(sqlite3 *hdl, char *virtual_path, int parent_id)
{
  char *query;
  char *errmsg;
  int id;
  int ret;

  query = sqlite3_mprintf(
      "INSERT INTO directories (virtual_path, db_timestamp, disabled, parent_id) VALUES (TRIM(%Q), %d, %d, %d);",
      virtual_path,  (uint64_t)time(NULL), 0, parent_id);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  id = (int)sqlite3_last_insert_rowid(hdl);

  DPRINTF(E_DBG, L_DB, "Added directory %s with id %d\n", virtual_path, id);

  return id;
}

static int
db_upgrade_v19_insert_parent_directories(sqlite3 *hdl, char *virtual_path)
{
  char *ptr;
  int dir_id;
  int parent_id;
  char buf[PATH_MAX];

  // The root directoy ID
  parent_id = 1;

  ptr = virtual_path + 1; // Skip first '/'
  while (ptr && (ptr = strchr(ptr, '/')))
    {
      strncpy(buf, virtual_path, (ptr - virtual_path));
      buf[(ptr - virtual_path)] = '\0';

      dir_id = db_upgrade_v19_directory_id(hdl, buf);

      if (dir_id < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Select of directory failed '%s'\n", buf);

	  return -1;
	}
      else if (dir_id == 0)
	{
	  dir_id = db_upgrade_v19_insert_directory(hdl, buf, parent_id);
	  if (dir_id < 0)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Insert of directory failed '%s'\n", buf);

	      return -1;
	    }
	}

      parent_id = dir_id;
      ptr++;
    }

  return parent_id;
}

static int
db_upgrade_v19(sqlite3 *hdl)
{
  sqlite3_stmt *stmt;
  char *query;
  char *uquery;
  char *errmsg;
  int id;
  char *virtual_path;
  int dir_id;
  int ret;

  query = "SELECT id, virtual_path FROM files;";

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      id = sqlite3_column_int(stmt, 0);
      virtual_path = (char *)sqlite3_column_text(stmt, 1);

      dir_id = db_upgrade_v19_insert_parent_directories(hdl, virtual_path);
      if (dir_id < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Error processing parent directories for file: %s\n", virtual_path);
	}
      else
	{
	  uquery = sqlite3_mprintf("UPDATE files SET directory_id = %d WHERE id = %d;", dir_id, id);
	  ret = sqlite3_exec(hdl, uquery, NULL, NULL, &errmsg);
	  if (ret != SQLITE_OK)
	    {
	      DPRINTF(E_LOG, L_DB, "Error updating files: %s\n", errmsg);
	    }

	  sqlite3_free(uquery);
	  sqlite3_free(errmsg);
	}
    }

  sqlite3_finalize(stmt);


  query = "SELECT id, virtual_path FROM playlists WHERE type = 2 OR type = 3;"; //Only update normal and smart playlists

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      id = sqlite3_column_int(stmt, 0);
      virtual_path = (char *)sqlite3_column_text(stmt, 1);

      dir_id = db_upgrade_v19_insert_parent_directories(hdl, virtual_path);
      if (dir_id < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Error processing parent directories for file: %s\n", virtual_path);
	}
      else
	{
	  uquery = sqlite3_mprintf("UPDATE files SET directory_id = %d WHERE id = %d;", dir_id, id);
	  ret = sqlite3_exec(hdl, uquery, NULL, NULL, &errmsg);
	  if (ret != SQLITE_OK)
	    {
	      DPRINTF(E_LOG, L_DB, "Error updating files: %s\n", errmsg);
	    }

	  sqlite3_free(uquery);
	  sqlite3_free(errmsg);
	}
    }

  sqlite3_finalize(stmt);

  return 0;
}

/* Upgrade from schema v19.00 to v19.01 */
/* Create new table queue for persistent playqueue
 */

#define U_V1901_CREATE_TABLE_QUEUE					\
  "CREATE TABLE IF NOT EXISTS queue ("					\
  "   id                  INTEGER PRIMARY KEY NOT NULL,"		\
  "   file_id             INTEGER NOT NULL,"				\
  "   pos                 INTEGER NOT NULL,"				\
  "   shuffle_pos         INTEGER NOT NULL,"				\
  "   data_kind           INTEGER NOT NULL,"				\
  "   media_kind          INTEGER NOT NULL,"				\
  "   song_length         INTEGER NOT NULL,"				\
  "   path                VARCHAR(4096) NOT NULL,"			\
  "   virtual_path        VARCHAR(4096) NOT NULL,"			\
  "   title               VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist              VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist        VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   album               VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   genre               VARCHAR(255) DEFAULT NULL COLLATE DAAP,"	\
  "   songalbumid         INTEGER NOT NULL,"				\
  "   time_modified       INTEGER DEFAULT 0,"				\
  "   artist_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_sort          VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist_sort   VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   year                INTEGER DEFAULT 0,"				\
  "   track               INTEGER DEFAULT 0,"				\
  "   disc                INTEGER DEFAULT 0"				\
  ");"

#define U_V1901_QUEUE_VERSION			\
  "INSERT INTO admin (key, value) VALUES ('queue_version', '0');"

#define U_V1901_SCVER_MAJOR			\
  "UPDATE admin SET value = '19' WHERE key = 'schema_version_major';"
#define U_V1901_SCVER_MINOR			\
  "UPDATE admin SET value = '01' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1901_queries[] =
  {
    { U_V1901_CREATE_TABLE_QUEUE,    "create table directories" },
    { U_V1901_QUEUE_VERSION,         "insert queue version" },

    { U_V1901_SCVER_MAJOR,    "set schema_version_major to 19" },
    { U_V1901_SCVER_MINOR,    "set schema_version_minor to 01" },
  };

/* Upgrade from schema v19.01 to v19.02 */
/* Set key column as primary key in the admin table
 */

#define U_V1902_CREATE_TABLE_ADMINTMP 			\
  "CREATE TEMPORARY TABLE IF NOT EXISTS admin_tmp("	\
  "   key   VARCHAR(32) NOT NULL,"		\
  "   value VARCHAR(32) NOT NULL"			\
  ");"
#define U_V1902_INSERT_ADMINTMP \
  "INSERT INTO admin_tmp SELECT * FROM admin;"
#define U_V1902_DROP_TABLE_ADMIN \
  "DROP TABLE admin;"
#define U_V1902_CREATE_TABLE_ADMIN 			\
  "CREATE TABLE IF NOT EXISTS admin("			\
  "   key   VARCHAR(32) PRIMARY KEY NOT NULL,"		\
  "   value VARCHAR(32) NOT NULL"			\
  ");"
#define U_V1902_INSERT_ADMIN \
  "INSERT OR IGNORE INTO admin SELECT * FROM admin_tmp;"
#define U_V1902_DROP_TABLE_ADMINTMP \
  "DROP TABLE admin_tmp;"

#define U_V1902_SCVER_MAJOR			\
  "UPDATE admin SET value = '19' WHERE key = 'schema_version_major';"
#define U_V1902_SCVER_MINOR			\
  "UPDATE admin SET value = '02' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1902_queries[] =
  {
    { U_V1902_CREATE_TABLE_ADMINTMP,    "create temporary table admin_tmp" },
    { U_V1902_INSERT_ADMINTMP,          "insert admin_tmp" },
    { U_V1902_DROP_TABLE_ADMIN,         "drop table admin" },
    { U_V1902_CREATE_TABLE_ADMIN,       "create table admin" },
    { U_V1902_INSERT_ADMIN,             "insert admin" },
    { U_V1902_DROP_TABLE_ADMINTMP,      "drop table admin_tmp" },

    { U_V1902_SCVER_MAJOR,    "set schema_version_major to 19" },
    { U_V1902_SCVER_MINOR,    "set schema_version_minor to 02" },
  };


#define U_V1903_ALTER_QUEUE_ADD_ARTWORKURL \
  "ALTER TABLE queue ADD COLUMN artwork_url VARCHAR(4096) DEFAULT NULL;"

#define U_V1903_SCVER_MAJOR \
  "UPDATE admin SET value = '19' WHERE key = 'schema_version_major';"
#define U_V1903_SCVER_MINOR \
  "UPDATE admin SET value = '03' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1903_queries[] =
  {
    { U_V1903_ALTER_QUEUE_ADD_ARTWORKURL,    "alter table queue add column artwork_url" },

    { U_V1903_SCVER_MAJOR,    "set schema_version_major to 19" },
    { U_V1903_SCVER_MINOR,    "set schema_version_minor to 03" },
  };


#define U_V1904_ALTER_SPEAKERS_ADD_AUTHKEY \
  "ALTER TABLE speakers ADD COLUMN auth_key VARCHAR(2048) DEFAULT NULL;"

#define U_V1904_SCVER_MAJOR \
  "UPDATE admin SET value = '19' WHERE key = 'schema_version_major';"
#define U_V1904_SCVER_MINOR \
  "UPDATE admin SET value = '04' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1904_queries[] =
  {
    { U_V1904_ALTER_SPEAKERS_ADD_AUTHKEY,    "alter table speakers add column auth_key" },

    { U_V1904_SCVER_MAJOR,    "set schema_version_major to 19" },
    { U_V1904_SCVER_MINOR,    "set schema_version_minor to 04" },
  };


#define U_V1905_SCVER_MINOR \
  "UPDATE admin SET value = '05' WHERE key = 'schema_version_minor';"

// Purpose of this upgrade is to reset the indeces, so that I_FNAME gets added
static const struct db_upgrade_query db_upgrade_v1905_queries[] =
  {
    { U_V1905_SCVER_MINOR,    "set schema_version_minor to 05" },
  };


#define U_V1906_DROP_TABLE_QUEUE					\
  "DROP TABLE queue;"

#define U_V1906_CREATE_TABLE_QUEUE					\
  "CREATE TABLE IF NOT EXISTS queue ("					\
  "   id                  INTEGER PRIMARY KEY AUTOINCREMENT,"		\
  "   file_id             INTEGER NOT NULL,"				\
  "   pos                 INTEGER NOT NULL,"				\
  "   shuffle_pos         INTEGER NOT NULL,"				\
  "   data_kind           INTEGER NOT NULL,"				\
  "   media_kind          INTEGER NOT NULL,"				\
  "   song_length         INTEGER NOT NULL,"				\
  "   path                VARCHAR(4096) NOT NULL,"			\
  "   virtual_path        VARCHAR(4096) NOT NULL,"			\
  "   title               VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist              VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist        VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   album               VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   genre               VARCHAR(255) DEFAULT NULL COLLATE DAAP,"	\
  "   songalbumid         INTEGER NOT NULL,"				\
  "   time_modified       INTEGER DEFAULT 0,"				\
  "   artist_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_sort          VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist_sort   VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   year                INTEGER DEFAULT 0,"				\
  "   track               INTEGER DEFAULT 0,"				\
  "   disc                INTEGER DEFAULT 0,"				\
  "   artwork_url         VARCHAR(4096) DEFAULT NULL,"			\
  "   queue_version       INTEGER DEFAULT 0"				\
  ");"

#define U_V1906_UPDATE_HTTP_VIRTUAL_PATH \
  "UPDATE files SET virtual_path = '/' || path WHERE data_kind = 1;"

#define U_V1906_SCVER_MAJOR			\
  "UPDATE admin SET value = '19' WHERE key = 'schema_version_major';"
#define U_V1906_SCVER_MINOR			\
  "UPDATE admin SET value = '06' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_V1906_queries[] =
  {
    { U_V1906_DROP_TABLE_QUEUE,         "drop queue table" },
    { U_V1906_CREATE_TABLE_QUEUE,       "create queue table" },
    { U_V1906_UPDATE_HTTP_VIRTUAL_PATH, "update virtual path for http streams" },

    { U_V1906_SCVER_MAJOR,    "set schema_version_major to 19" },
    { U_V1906_SCVER_MINOR,    "set schema_version_minor to 06" },
  };


#define U_V1907_SCVER_MINOR			\
  "UPDATE admin SET value = '07' WHERE key = 'schema_version_minor';"

// Purpose of this upgrade is to reset the indeces
static const struct db_upgrade_query db_upgrade_V1907_queries[] =
  {
    { U_V1907_SCVER_MINOR,    "set schema_version_minor to 07" },
  };


#define U_V1908_ALTER_PL_ADD_ORDER \
  "ALTER TABLE playlists ADD COLUMN query_order VARCHAR(1024);"
#define U_V1908_ALTER_PL_ADD_LIMIT \
  "ALTER TABLE playlists ADD COLUMN query_limit INTEGER DEFAULT -1;"

#define U_V1908_SCVER_MINOR \
  "UPDATE admin SET value = '08' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1908_queries[] =
  {
    { U_V1908_ALTER_PL_ADD_ORDER,      "alter table playlists add column query_order" },
    { U_V1908_ALTER_PL_ADD_LIMIT,      "alter table playlists add column query_limit" },

    { U_V1908_SCVER_MINOR,    "set schema_version_minor to 08" },
  };


#define U_V1909_ALTER_FILES_ADD_SKIP_COUNT \
  "ALTER TABLE files ADD COLUMN skip_count INTEGER DEFAULT 0;"
#define U_V1909_ALTER_FILES_ADD_TIME_SKIPPED \
  "ALTER TABLE files ADD COLUMN time_skipped INTEGER DEFAULT 0;"

#define U_V1909_SCVER_MINOR \
  "UPDATE admin SET value = '09' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1909_queries[] =
  {
    { U_V1909_ALTER_FILES_ADD_SKIP_COUNT,   "alter table files add column skip_count" },
    { U_V1909_ALTER_FILES_ADD_TIME_SKIPPED, "alter table files add column time_skipped" },

    { U_V1909_SCVER_MINOR,    "set schema_version_minor to 09" },
  };


// Clean up after bug in commit fde0a281 (schema 19.09)
#define U_V1910_CLEANUP_TIME_SKIPPED \
  "UPDATE files SET time_skipped = 0 WHERE time_skipped > 2000000000;"

#define U_V1910_SCVER_MINOR \
  "UPDATE admin SET value = '10' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1910_queries[] =
  {
    { U_V1910_CLEANUP_TIME_SKIPPED,   "clean up time_skipped" },

    { U_V1910_SCVER_MINOR,    "set schema_version_minor to 10" },
  };


#define U_v1911_ALTER_QUEUE_ADD_COMPOSER \
  "ALTER TABLE queue ADD COLUMN composer VARCHAR(1024) DEFAULT NULL;"

#define U_v1911_SCVER_MAJOR			\
  "UPDATE admin SET value = '19' WHERE key = 'schema_version_major';"
#define U_v1911_SCVER_MINOR			\
  "UPDATE admin SET value = '11' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1911_queries[] =
  {
    { U_v1911_ALTER_QUEUE_ADD_COMPOSER,   "alter table queue add column composer" },

    { U_v1911_SCVER_MAJOR,    "set schema_version_major to 19" },
    { U_v1911_SCVER_MINOR,    "set schema_version_minor to 11" },
  };


#define U_V1912_ALTER_DIRECTORIES_ADD_PATH \
  "ALTER TABLE directories ADD COLUMN path VARCHAR(4096) DEFAULT NULL;"

#define U_V1912_UPDATE_FILE_DIRECTORIES_PATH \
  "UPDATE directories SET path = SUBSTR(path, 7) WHERE virtual_path like '/file:/%';"
#define U_V1912_UPDATE_FILE_ROOT_PATH \
  "UPDATE directories SET path = '/' WHERE virtual_path = '/file:';"

#define U_V1912_SCVER_MINOR \
  "UPDATE admin SET value = '12' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v1912_queries[] =
  {
    { U_V1912_ALTER_DIRECTORIES_ADD_PATH,   "alter table directories add column path" },
    { U_V1912_UPDATE_FILE_DIRECTORIES_PATH, "set paths for '/file:' directories" },
    { U_V1912_UPDATE_FILE_ROOT_PATH,        "set path for '/file:' directory" },

    { U_V1912_SCVER_MINOR,    "set schema_version_minor to 12" },
  };


#define U_V20_NEW_FILES_TABLE				\
  "CREATE TABLE new_files ("				\
  "   id                 INTEGER PRIMARY KEY NOT NULL,"	\
  "   path               VARCHAR(4096) NOT NULL,"	\
  "   virtual_path       VARCHAR(4096) DEFAULT NULL,"	\
  "   fname              VARCHAR(255) NOT NULL,"	\
  "   directory_id       INTEGER DEFAULT 0,"		\
  "   title              VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist             VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album              VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   album_artist       VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   genre              VARCHAR(255) DEFAULT NULL COLLATE DAAP,"	\
  "   comment            VARCHAR(4096) DEFAULT NULL COLLATE DAAP,"	\
  "   type               VARCHAR(255) DEFAULT NULL COLLATE DAAP,"	\
  "   composer           VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   orchestra          VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   conductor          VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   grouping           VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   url                VARCHAR(1024) DEFAULT NULL,"	\
  "   bitrate            INTEGER DEFAULT 0,"		\
  "   samplerate         INTEGER DEFAULT 0,"		\
  "   song_length        INTEGER DEFAULT 0,"		\
  "   file_size          INTEGER DEFAULT 0,"		\
  "   year               INTEGER DEFAULT 0,"		\
  "   date_released      INTEGER DEFAULT 0,"		\
  "   track              INTEGER DEFAULT 0,"		\
  "   total_tracks       INTEGER DEFAULT 0,"		\
  "   disc               INTEGER DEFAULT 0,"		\
  "   total_discs        INTEGER DEFAULT 0,"		\
  "   bpm                INTEGER DEFAULT 0,"		\
  "   compilation        INTEGER DEFAULT 0,"		\
  "   artwork            INTEGER DEFAULT 0,"		\
  "   rating             INTEGER DEFAULT 0,"		\
  "   play_count         INTEGER DEFAULT 0,"		\
  "   skip_count         INTEGER DEFAULT 0,"            \
  "   seek               INTEGER DEFAULT 0,"		\
  "   data_kind          INTEGER DEFAULT 0,"		\
  "   media_kind         INTEGER DEFAULT 0,"		\
  "   item_kind          INTEGER DEFAULT 0,"		\
  "   description        INTEGER DEFAULT 0,"		\
  "   db_timestamp       INTEGER DEFAULT 0,"		\
  "   time_added         INTEGER DEFAULT 0,"		\
  "   time_modified      INTEGER DEFAULT 0,"		\
  "   time_played        INTEGER DEFAULT 0,"		\
  "   time_skipped       INTEGER DEFAULT 0,"            \
  "   disabled           INTEGER DEFAULT 0,"		\
  "   sample_count       INTEGER DEFAULT 0,"		\
  "   codectype          VARCHAR(5) DEFAULT NULL,"	\
  "   idx                INTEGER NOT NULL,"		\
  "   has_video          INTEGER DEFAULT 0,"		\
  "   contentrating      INTEGER DEFAULT 0,"		\
  "   bits_per_sample    INTEGER DEFAULT 0,"		\
  "   tv_series_name     VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_num_str VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_network_name    VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_sort    INTEGER NOT NULL,"		\
  "   tv_season_num      INTEGER NOT NULL,"		\
  "   songartistid       INTEGER DEFAULT 0,"		\
  "   songalbumid        INTEGER DEFAULT 0,"		\
  "   title_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist_sort        VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist_sort  VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   composer_sort      VARCHAR(1024) DEFAULT NULL COLLATE DAAP"	\
  ");"

static int
db_upgrade_v20(sqlite3 *hdl)
{
  return db_table_upgrade(hdl, "files", U_V20_NEW_FILES_TABLE);
}

#define U_V2000_DROP_TRG1				\
  "DROP TRIGGER IF EXISTS update_groups_new_file;"
#define U_V2000_DROP_TRG2				\
  "DROP TRIGGER IF EXISTS update_groups_update_file;"

#define U_V2000_SCVER_MAJOR \
  "UPDATE admin SET value = '20' WHERE key = 'schema_version_major';"
#define U_V2000_SCVER_MINOR \
  "UPDATE admin SET value = '00' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v2000_queries[] =
  {
    { U_V2000_DROP_TRG1,      "drop trigger update_groups_new_file" },
    { U_V2000_DROP_TRG2,      "drop trigger update_groups_update_file" },

    { U_V2000_SCVER_MAJOR,    "set schema_version_major to 20" },
    { U_V2000_SCVER_MINOR,    "set schema_version_minor to 00" },
  };


int
db_upgrade(sqlite3 *hdl, int db_ver)
{
  int ret;

  ret = db_drop_indices(hdl);
  if (ret < 0)
    return -1;

  ret = db_drop_triggers(hdl);
  if (ret < 0)
    return -1;

  switch (db_ver)
    {
    case 1700:
      ret = db_generic_upgrade(hdl, db_upgrade_v18_queries, ARRAY_SIZE(db_upgrade_v18_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1800:
      ret = db_generic_upgrade(hdl, db_upgrade_v1801_queries, ARRAY_SIZE(db_upgrade_v1801_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1801:
      ret = db_generic_upgrade(hdl, db_upgrade_v1900_queries, ARRAY_SIZE(db_upgrade_v1900_queries));
      if (ret < 0)
	return -1;

      ret = db_upgrade_v19(hdl);
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1900:
      ret = db_generic_upgrade(hdl, db_upgrade_v1901_queries, ARRAY_SIZE(db_upgrade_v1901_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1901:
      ret = db_generic_upgrade(hdl, db_upgrade_v1902_queries, ARRAY_SIZE(db_upgrade_v1902_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1902:
      ret = db_generic_upgrade(hdl, db_upgrade_v1903_queries, ARRAY_SIZE(db_upgrade_v1903_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1903:
      ret = db_generic_upgrade(hdl, db_upgrade_v1904_queries, ARRAY_SIZE(db_upgrade_v1904_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1904:
      ret = db_generic_upgrade(hdl, db_upgrade_v1905_queries, ARRAY_SIZE(db_upgrade_v1905_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1905:
      ret = db_generic_upgrade(hdl, db_upgrade_V1906_queries, ARRAY_SIZE(db_upgrade_V1906_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1906:
      ret = db_generic_upgrade(hdl, db_upgrade_V1907_queries, ARRAY_SIZE(db_upgrade_V1907_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1907:
      ret = db_generic_upgrade(hdl, db_upgrade_v1908_queries, ARRAY_SIZE(db_upgrade_v1908_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1908:
      ret = db_generic_upgrade(hdl, db_upgrade_v1909_queries, ARRAY_SIZE(db_upgrade_v1909_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1909:
      ret = db_generic_upgrade(hdl, db_upgrade_v1910_queries, ARRAY_SIZE(db_upgrade_v1910_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1910:
      ret = db_generic_upgrade(hdl, db_upgrade_v1911_queries, ARRAY_SIZE(db_upgrade_v1911_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1911:
      ret = db_generic_upgrade(hdl, db_upgrade_v1912_queries, ARRAY_SIZE(db_upgrade_v1912_queries));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1912:
      ret = db_upgrade_v20(hdl);
      if (ret < 0)
	return -1;

      ret = db_generic_upgrade(hdl, db_upgrade_v2000_queries, ARRAY_SIZE(db_upgrade_v2000_queries));
      if (ret < 0)
	return -1;

      break;

    default:
      DPRINTF(E_FATAL, L_DB, "No upgrade path from the current DB schema\n");
      return -1;
    }

  return 0;
}
