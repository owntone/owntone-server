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
      return -1;
    }

  sqlite3_finalize(stmt);

  for (i = 0; i < n; i++)
    {
      query = sqlite3_mprintf(Q_TMPL, index[i]);
      free(index[i]);

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
#undef Q_INDEX
}


static int
db_generic_upgrade(sqlite3 *hdl, const struct db_upgrade_query *queries, int nqueries)
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

/* Upgrade the files table to the new schema by dumping and reloading the
 * table. A bit tedious.
 */
static int
db_upgrade_files_table(sqlite3 *hdl, const char *dumpquery, const char *newtablequery)
{
  struct stat sb;
  FILE *fp;
  sqlite3_stmt *stmt;
  const unsigned char *dumprow;
  char *dump;
  char *errmsg;
  int fd;
  int ret;

  DPRINTF(E_LOG, L_DB, "Upgrading files table...\n");

  fp = tmpfile();
  if (!fp)
    {
      DPRINTF(E_LOG, L_DB, "Could not create temporary file for files table dump: %s\n", strerror(errno));
      return -1;
    }

  DPRINTF(E_LOG, L_DB, "Dumping old files table...\n");

  /* dump */
  ret = sqlite3_prepare_v2(hdl, dumpquery, strlen(dumpquery) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      ret = -1;
      goto out_fclose;
    }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      dumprow = sqlite3_column_text(stmt, 0);

      ret = fprintf(fp, "%s\n", dumprow);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Could not write dump: %s\n", strerror(errno));

	  sqlite3_finalize(stmt);

	  ret = -1;
	  goto out_fclose;
	}
    }

  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);

      ret = -1;
      goto out_fclose;
    }

  sqlite3_finalize(stmt);

  /* Seek back to start of dump file */
  ret = fseek(fp, 0, SEEK_SET);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DB, "Could not seek back to start of dump: %s\n", strerror(errno));

      ret = -1;
      goto out_fclose;
    }

  /* Map dump file */
  fd = fileno(fp);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_DB, "Could not obtain file descriptor: %s\n", strerror(errno));

      ret = -1;
      goto out_fclose;
    }

  ret = fstat(fd, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DB, "Could not stat dump file: %s\n", strerror(errno));

      ret = -1;
      goto out_fclose;
    }

  if (sb.st_size == 0)
    dump = NULL;
  else
    {
      dump = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
      if (dump == MAP_FAILED)
	{
	  DPRINTF(E_LOG, L_DB, "Could not map dump file: %s\n", strerror(errno));

	  ret = -1;
	  goto out_fclose;
	}
    }

  /* Drop remnants from last upgrade if still present */
  DPRINTF(E_LOG, L_DB, "Clearing old backups...\n");

  ret = sqlite3_exec(hdl, "DROP TABLE IF EXISTS files_backup;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Error clearing old backup - will continue anyway: %s\n", errmsg);

      sqlite3_free(errmsg);
    }

  /* Move old table out of the way */
  DPRINTF(E_LOG, L_DB, "Moving old files table out of the way...\n");

  ret = sqlite3_exec(hdl, "ALTER TABLE files RENAME TO files_backup;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Error making backup of old files table: %s\n", errmsg);

      sqlite3_free(errmsg);

      ret = -1;
      goto out_munmap;
    }

  /* Create new table */
  DPRINTF(E_LOG, L_DB, "Creating new files table...\n");

  ret = sqlite3_exec(hdl, newtablequery, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Error creating new files table: %s\n", errmsg);

      sqlite3_free(errmsg);

      ret = -1;
      goto out_munmap;
    }

  /* Reload dump */
  DPRINTF(E_LOG, L_DB, "Reloading new files table...\n");

  if (dump)
    {
      ret = sqlite3_exec(hdl, dump, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Error reloading files table data: %s\n", errmsg);

	  sqlite3_free(errmsg);

	  ret = -1;
	  goto out_munmap;
	}
    }

  /* Delete old files table */
  DPRINTF(E_LOG, L_DB, "Deleting backup files table...\n");

  ret = sqlite3_exec(hdl, "DROP TABLE files_backup;", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Error dropping backup files table: %s\n", errmsg);

      sqlite3_free(errmsg);
      /* Not an issue, but takes up space in the database */
    }

 DPRINTF(E_LOG, L_DB, "Upgrade of files table complete!\n");

 out_munmap:
  if (dump)
    {
      if (munmap(dump, sb.st_size) < 0)
	DPRINTF(E_LOG, L_DB, "Could not unmap dump file: %s\n", strerror(errno));
    }

 out_fclose:
  fclose(fp);

  return ret;
}

/* Upgrade from schema v10 to v11 */

#define U_V11_SPEAKERS					\
  "CREATE TABLE speakers("				\
  "   id             INTEGER PRIMARY KEY NOT NULL,"	\
  "   selected       INTEGER NOT NULL,"			\
  "   volume         INTEGER NOT NULL"			\
  ");"

#define U_V11_SCVER					\
  "UPDATE admin SET value = '11' WHERE key = 'schema_version';"

static const struct db_upgrade_query db_upgrade_v11_queries[] =
  {
    { U_V11_SPEAKERS,  "create new table speakers" },
    { U_V11_SCVER,     "set schema_version to 11" },
  };

static int
db_upgrade_v11(sqlite3 *hdl)
{
#define Q_NEWSPK "INSERT INTO speakers (id, selected, volume) VALUES (%" PRIi64 ", 1, 75);"
#define Q_SPKVOL "UPDATE speakers SET volume = %d;"
  sqlite3_stmt *stmt;
  char *query;
  char *errmsg;
  const char *strid;
  uint64_t *spkids;
  int volume;
  int count;
  int i;
  int qret;
  int ret;

  /* Get saved speakers */
  query = "SELECT COUNT(*) FROM admin WHERE key = 'player:active-spk';";
  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      goto clear_vars;
    }
  qret = sqlite3_step(stmt);
  if (qret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);

      goto clear_vars;
    }

  count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (count == 0)
    goto clear_vars;
  else if (count < 0)
    return -1;

  spkids = calloc(count, sizeof(uint64_t));
  if (!spkids)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for speaker IDs\n");

      return -1;
    }

  query = "SELECT value FROM admin WHERE key = 'player:active-spk';";

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      goto out_free_ids;
    }

  i = 0;
  ret = 0;
  while ((qret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      strid = (const char *)sqlite3_column_text(stmt, 0);

      ret = safe_hextou64(strid, spkids + i);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Could not convert speaker ID: %s\n", strid);
	  break;
	}

      i++;
    }

  sqlite3_finalize(stmt);

  if ((ret == 0) && (qret != SQLITE_DONE))
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      goto out_free_ids;
    }
  else if (ret < 0)
    goto out_free_ids;

  /* Get saved volume */
  query = "SELECT value FROM admin WHERE key = 'player:volume';";

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = sqlite3_prepare_v2(hdl, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      goto out_free_ids;
    }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      goto out_free_ids;
    }

  volume = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);

  /* Add speakers to the table */
  for (i = 0; i < count; i++)
    {
      query = sqlite3_mprintf(Q_NEWSPK, spkids[i]);
      if (!query)
	{
	  DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

	  goto out_free_ids;
	}

      DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

      ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	DPRINTF(E_LOG, L_DB, "Error adding speaker: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
    }

  free(spkids);

  /* Update with volume */
  query = sqlite3_mprintf(Q_SPKVOL, volume);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    DPRINTF(E_LOG, L_DB, "Error adding speaker: %s\n", errmsg);

  sqlite3_free(errmsg);
  sqlite3_free(query);

  /* Clear old config keys */
 clear_vars:
  query = "DELETE FROM admin WHERE key = 'player:volume' OR key = 'player:active-spk';";

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = sqlite3_exec(hdl, query, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK)
    DPRINTF(E_LOG, L_DB, "Error adding speaker: %s\n", errmsg);

  sqlite3_free(errmsg);

  return 0;

 out_free_ids:
  free(spkids);

  return -1;

#undef Q_NEWSPK
#undef Q_SPKVOL
}


/* Upgrade from schema v11 to v12 */

#define U_V12_NEW_FILES_TABLE				\
  "CREATE TABLE IF NOT EXISTS files ("			\
  "   id                 INTEGER PRIMARY KEY NOT NULL,"	\
  "   path               VARCHAR(4096) NOT NULL,"	\
  "   fname              VARCHAR(255) NOT NULL,"	\
  "   title              VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist             VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album              VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
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
  "   track              INTEGER DEFAULT 0,"		\
  "   total_tracks       INTEGER DEFAULT 0,"		\
  "   disc               INTEGER DEFAULT 0,"		\
  "   total_discs        INTEGER DEFAULT 0,"		\
  "   bpm                INTEGER DEFAULT 0,"		\
  "   compilation        INTEGER DEFAULT 0,"		\
  "   rating             INTEGER DEFAULT 0,"		\
  "   play_count         INTEGER DEFAULT 0,"		\
  "   data_kind          INTEGER DEFAULT 0,"		\
  "   item_kind          INTEGER DEFAULT 0,"		\
  "   description        INTEGER DEFAULT 0,"		\
  "   time_added         INTEGER DEFAULT 0,"		\
  "   time_modified      INTEGER DEFAULT 0,"		\
  "   time_played        INTEGER DEFAULT 0,"		\
  "   db_timestamp       INTEGER DEFAULT 0,"		\
  "   disabled           INTEGER DEFAULT 0,"		\
  "   sample_count       INTEGER DEFAULT 0,"		\
  "   codectype          VARCHAR(5) DEFAULT NULL,"	\
  "   idx                INTEGER NOT NULL,"		\
  "   has_video          INTEGER DEFAULT 0,"		\
  "   contentrating      INTEGER DEFAULT 0,"		\
  "   bits_per_sample    INTEGER DEFAULT 0,"		\
  "   album_artist       VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   media_kind         INTEGER NOT NULL,"		\
  "   tv_series_name     VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_num_str VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_network_name    VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_sort    INTEGER NOT NULL,"		\
  "   tv_season_num      INTEGER NOT NULL,"		\
  "   songalbumid        INTEGER NOT NULL,"		\
  "   title_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist_sort        VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   composer_sort      VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist_sort  VARCHAR(1024) DEFAULT NULL COLLATE DAAP"	\
  ");"

#define U_V12_TRG1							\
  "CREATE TRIGGER update_groups_new_file AFTER INSERT ON files FOR EACH ROW" \
  " BEGIN"								\
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (1, NEW.album, NEW.songalbumid);" \
  " END;"

#define U_V12_TRG2							\
  "CREATE TRIGGER update_groups_update_file AFTER UPDATE OF songalbumid ON files FOR EACH ROW" \
  " BEGIN"								\
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (1, NEW.album, NEW.songalbumid);" \
  " END;"

#define U_V12_SCVER				\
  "UPDATE admin SET value = '12' WHERE key = 'schema_version';"

static const struct db_upgrade_query db_upgrade_v12_queries[] =
  {
    { U_V12_TRG1,     "create trigger update_groups_new_file" },
    { U_V12_TRG2,     "create trigger update_groups_update_file" },

    { U_V12_SCVER,    "set schema_version to 12" },
  };

static int
db_upgrade_v12(sqlite3 *hdl)
{
#define Q_DUMP "SELECT 'INSERT INTO files " \
    "(id, path, fname, title, artist, album, genre, comment, type, composer," \
    " orchestra, conductor, grouping, url, bitrate, samplerate, song_length, file_size, year, track," \
    " total_tracks, disc, total_discs, bpm, compilation, rating, play_count, data_kind, item_kind," \
    " description, time_added, time_modified, time_played, db_timestamp, disabled, sample_count," \
    " codectype, idx, has_video, contentrating, bits_per_sample, album_artist," \
    " media_kind, tv_series_name, tv_episode_num_str, tv_network_name, tv_episode_sort, tv_season_num, " \
    " songalbumid, title_sort, artist_sort, album_sort, composer_sort, album_artist_sort)" \
    " VALUES (' || id || ', ' || QUOTE(path) || ', ' || QUOTE(fname) || ', ' || QUOTE(title) || ', '" \
    " || QUOTE(artist) || ', ' || QUOTE(album) || ', ' || QUOTE(genre) || ', ' || QUOTE(comment) || ', '" \
    " || QUOTE(type) || ', ' || QUOTE(composer) || ', ' || QUOTE(orchestra) || ', ' || QUOTE(conductor) || ', '" \
    " || QUOTE(grouping) || ', ' || QUOTE(url) || ', ' || bitrate || ', ' || samplerate || ', '" \
    " || song_length || ', ' || file_size || ', ' || year || ', ' || track || ', ' || total_tracks || ', '" \
    " || disc || ', ' || total_discs || ', ' || bpm || ', ' || compilation || ', ' || rating || ', '" \
    " || play_count || ', ' || data_kind || ', ' || item_kind || ', ' ||  QUOTE(description) || ', '" \
    " || time_added || ', ' || time_modified || ', ' || time_played || ', 1, '" \
    " || disabled || ', ' || sample_count || ', ' || QUOTE(codectype) || ', ' || idx || ', '" \
    " || has_video || ', ' || contentrating || ', ' || bits_per_sample || ', ' || QUOTE(album_artist) || ', '" \
    " || media_kind || ', ' || QUOTE(tv_series_name) || ', ' || QUOTE(tv_episode_num_str) || ', '" \
    " || QUOTE(tv_network_name) || ', ' || tv_episode_sort || ', ' || tv_season_num || ', '" \
    " || songalbumid || ', ' || QUOTE(title) || ', ' || QUOTE(artist) || ', ' || QUOTE(album) || ', '" \
    " || QUOTE(composer) || ', ' || QUOTE(album_artist) || ');' FROM files;"

  return db_upgrade_files_table(hdl, Q_DUMP, U_V12_NEW_FILES_TABLE);

#undef Q_DUMP
}


/* Upgrade from schema v12 to v13 */

#define U_V13_PL2							\
  "UPDATE playlists SET query = 'f.media_kind = 1' where id = 2;"

#define U_V13_PL3							\
  "UPDATE playlists SET query = 'f.media_kind = 2' where id = 3;"

#define U_V13_PL4							\
  "UPDATE playlists SET query = 'f.media_kind = 64' where id = 4;"

#define U_V13_SCVER				\
  "UPDATE admin SET value = '13' WHERE key = 'schema_version';"

static const struct db_upgrade_query db_upgrade_v13_queries[] =
  {
    { U_V13_PL2,           "update default smart playlist 'Music'" },
    { U_V13_PL3,           "update default smart playlist 'Movies'" },
    { U_V13_PL4,           "update default smart playlist 'TV Shows'" },

    { U_V13_SCVER,    "set schema_version to 13" },
  };

/* Upgrade from schema v13 to v14 */
/* Adds seek, songartistid, and two new smart playlists */

#define U_V14_NEW_FILES_TABLE				\
  "CREATE TABLE IF NOT EXISTS files ("			\
  "   id                 INTEGER PRIMARY KEY NOT NULL,"	\
  "   path               VARCHAR(4096) NOT NULL,"	\
  "   fname              VARCHAR(255) NOT NULL,"	\
  "   title              VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist             VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album              VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
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
  "   track              INTEGER DEFAULT 0,"		\
  "   total_tracks       INTEGER DEFAULT 0,"		\
  "   disc               INTEGER DEFAULT 0,"		\
  "   total_discs        INTEGER DEFAULT 0,"		\
  "   bpm                INTEGER DEFAULT 0,"		\
  "   compilation        INTEGER DEFAULT 0,"		\
  "   rating             INTEGER DEFAULT 0,"		\
  "   play_count         INTEGER DEFAULT 0,"		\
  "   seek               INTEGER DEFAULT 0,"		\
  "   data_kind          INTEGER DEFAULT 0,"		\
  "   item_kind          INTEGER DEFAULT 0,"		\
  "   description        INTEGER DEFAULT 0,"		\
  "   time_added         INTEGER DEFAULT 0,"		\
  "   time_modified      INTEGER DEFAULT 0,"		\
  "   time_played        INTEGER DEFAULT 0,"		\
  "   db_timestamp       INTEGER DEFAULT 0,"		\
  "   disabled           INTEGER DEFAULT 0,"		\
  "   sample_count       INTEGER DEFAULT 0,"		\
  "   codectype          VARCHAR(5) DEFAULT NULL,"	\
  "   idx                INTEGER NOT NULL,"		\
  "   has_video          INTEGER DEFAULT 0,"		\
  "   contentrating      INTEGER DEFAULT 0,"		\
  "   bits_per_sample    INTEGER DEFAULT 0,"		\
  "   album_artist       VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   media_kind         INTEGER NOT NULL,"		\
  "   tv_series_name     VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_num_str VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_network_name    VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_sort    INTEGER NOT NULL,"		\
  "   tv_season_num      INTEGER NOT NULL,"		\
  "   songartistid       INTEGER NOT NULL,"		\
  "   songalbumid        INTEGER NOT NULL,"		\
  "   title_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist_sort        VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   composer_sort      VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist_sort  VARCHAR(1024) DEFAULT NULL COLLATE DAAP"	\
  ");"

#define U_V14_DELETE_PL5_1			\
  "DELETE FROM playlists WHERE id=5;"

#define U_V14_DELETE_PL5_2			\
  "DELETE FROM playlistitems WHERE playlistid=5;"

#define U_V14_DELETE_PL6_1			\
  "DELETE FROM playlists WHERE id=6;"

#define U_V14_DELETE_PL6_2			\
  "DELETE FROM playlistitems WHERE playlistid=6;"

#define U_V14_TRG1							\
  "CREATE TRIGGER update_groups_new_file AFTER INSERT ON files FOR EACH ROW" \
  " BEGIN"								\
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (1, NEW.album, NEW.songalbumid);" \
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (2, NEW.album_artist, NEW.songartistid);" \
  " END;"

#define U_V14_TRG2							\
  "CREATE TRIGGER update_groups_update_file AFTER UPDATE OF songalbumid ON files FOR EACH ROW" \
  " BEGIN"								\
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (1, NEW.album, NEW.songalbumid);" \
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (2, NEW.album_artist, NEW.songartistid);" \
  " END;"

#define U_V14_PL5							\
  "INSERT OR IGNORE INTO playlists (id, title, type, query, db_timestamp, path, idx, special_id)" \
  " VALUES(5, 'Podcasts', 1, 'f.media_kind = 4', 0, '', 0, 1);"

#define U_V14_PL6							\
  "INSERT OR IGNORE INTO playlists (id, title, type, query, db_timestamp, path, idx, special_id)" \
  " VALUES(6, 'Audiobooks', 1, 'f.media_kind = 8', 0, '', 0, 7);"

#define U_V14_SCVER				\
  "UPDATE admin SET value = '14' WHERE key = 'schema_version';"

static const struct db_upgrade_query db_upgrade_v14_queries[] =
  {
    { U_V14_DELETE_PL5_1, "delete playlist id 5 table playlists" },
    { U_V14_DELETE_PL5_2, "delete playlist id 5 table playlistitems" },
    { U_V14_DELETE_PL6_1, "delete playlist id 6 table playlists" },
    { U_V14_DELETE_PL6_2, "delete playlist id 6 table playlistitems" },

    { U_V14_TRG1,     "create trigger update_groups_new_file" },
    { U_V14_TRG2,     "create trigger update_groups_update_file" },

    { U_V14_PL5,      "create default smart playlist 'Podcasts' table playlists" },
    { U_V14_PL6,      "create default smart playlist 'Audiobooks' table playlists" },

    { U_V14_SCVER,    "set schema_version to 14" },
  };

static int
db_upgrade_v14(sqlite3 *hdl)
{
#define Q_DUMP "SELECT 'INSERT INTO files " \
    "(id, path, fname, title, artist, album, genre, comment, type, composer," \
    " orchestra, conductor, grouping, url, bitrate, samplerate, song_length, file_size, year, track," \
    " total_tracks, disc, total_discs, bpm, compilation, rating, play_count, seek, data_kind, item_kind," \
    " description, time_added, time_modified, time_played, db_timestamp, disabled, sample_count," \
    " codectype, idx, has_video, contentrating, bits_per_sample, album_artist," \
    " media_kind, tv_series_name, tv_episode_num_str, tv_network_name, tv_episode_sort, tv_season_num, " \
    " songartistid, songalbumid, " \
    " title_sort, artist_sort, album_sort, composer_sort, album_artist_sort)" \
    " VALUES (' || id || ', ' || QUOTE(path) || ', ' || QUOTE(fname) || ', ' || QUOTE(title) || ', '" \
    " || QUOTE(artist) || ', ' || QUOTE(album) || ', ' || QUOTE(genre) || ', ' || QUOTE(comment) || ', '" \
    " || QUOTE(type) || ', ' || QUOTE(composer) || ', ' || QUOTE(orchestra) || ', ' || QUOTE(conductor) || ', '" \
    " || QUOTE(grouping) || ', ' || QUOTE(url) || ', ' || bitrate || ', ' || samplerate || ', '" \
    " || song_length || ', ' || file_size || ', ' || year || ', ' || track || ', ' || total_tracks || ', '" \
    " || disc || ', ' || total_discs || ', ' || bpm || ', ' || compilation || ', ' || rating || ', '" \
    " || play_count || ', 0, ' || data_kind || ', ' || item_kind || ', ' ||  QUOTE(description) || ', '" \
    " || time_added || ', ' || time_modified || ', ' || time_played || ', ' || db_timestamp || ', '" \
    " || disabled || ', ' || sample_count || ', ' || QUOTE(codectype) || ', ' || idx || ', '" \
    " || has_video || ', ' || contentrating || ', ' || bits_per_sample || ', ' || QUOTE(album_artist) || ', '" \
    " || media_kind || ', ' || QUOTE(tv_series_name) || ', ' || QUOTE(tv_episode_num_str) || ', '" \
    " || QUOTE(tv_network_name) || ', ' || tv_episode_sort || ', ' || tv_season_num || ', " \
    " daap_songalbumid(' || QUOTE(album_artist) || ', ''''), ' || songalbumid || ', '" \
    " || QUOTE(title_sort) || ', ' || QUOTE(artist_sort) || ', ' || QUOTE(album_sort) || ', '" \
    " || QUOTE(composer_sort) || ', ' || QUOTE(album_artist_sort) || ');' FROM files;"

  return db_upgrade_files_table(hdl, Q_DUMP, U_V14_NEW_FILES_TABLE);

#undef Q_DUMP
}

/* Upgrade from schema v14 to v15 */
/* Adds artwork field - nothing else */

#define U_V15_NEW_FILES_TABLE				\
  "CREATE TABLE IF NOT EXISTS files ("			\
  "   id                 INTEGER PRIMARY KEY NOT NULL,"	\
  "   path               VARCHAR(4096) NOT NULL,"	\
  "   fname              VARCHAR(255) NOT NULL,"	\
  "   title              VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist             VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album              VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
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
  "   track              INTEGER DEFAULT 0,"		\
  "   total_tracks       INTEGER DEFAULT 0,"		\
  "   disc               INTEGER DEFAULT 0,"		\
  "   total_discs        INTEGER DEFAULT 0,"		\
  "   bpm                INTEGER DEFAULT 0,"		\
  "   compilation        INTEGER DEFAULT 0,"		\
  "   artwork            INTEGER DEFAULT 0,"		\
  "   rating             INTEGER DEFAULT 0,"		\
  "   play_count         INTEGER DEFAULT 0,"		\
  "   seek               INTEGER DEFAULT 0,"		\
  "   data_kind          INTEGER DEFAULT 0,"		\
  "   item_kind          INTEGER DEFAULT 0,"		\
  "   description        INTEGER DEFAULT 0,"		\
  "   time_added         INTEGER DEFAULT 0,"		\
  "   time_modified      INTEGER DEFAULT 0,"		\
  "   time_played        INTEGER DEFAULT 0,"		\
  "   db_timestamp       INTEGER DEFAULT 0,"		\
  "   disabled           INTEGER DEFAULT 0,"		\
  "   sample_count       INTEGER DEFAULT 0,"		\
  "   codectype          VARCHAR(5) DEFAULT NULL,"	\
  "   idx                INTEGER NOT NULL,"		\
  "   has_video          INTEGER DEFAULT 0,"		\
  "   contentrating      INTEGER DEFAULT 0,"		\
  "   bits_per_sample    INTEGER DEFAULT 0,"		\
  "   album_artist       VARCHAR(1024) NOT NULL COLLATE DAAP,"		\
  "   media_kind         INTEGER NOT NULL,"		\
  "   tv_series_name     VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_num_str VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_network_name    VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   tv_episode_sort    INTEGER NOT NULL,"		\
  "   tv_season_num      INTEGER NOT NULL,"		\
  "   songartistid       INTEGER NOT NULL,"		\
  "   songalbumid        INTEGER NOT NULL,"		\
  "   title_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   artist_sort        VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_sort         VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   composer_sort      VARCHAR(1024) DEFAULT NULL COLLATE DAAP,"	\
  "   album_artist_sort  VARCHAR(1024) DEFAULT NULL COLLATE DAAP"	\
  ");"

#define U_V15_TRG1							\
  "CREATE TRIGGER update_groups_new_file AFTER INSERT ON files FOR EACH ROW" \
  " BEGIN"								\
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (1, NEW.album, NEW.songalbumid);" \
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (2, NEW.album_artist, NEW.songartistid);" \
  " END;"

#define U_V15_TRG2							\
  "CREATE TRIGGER update_groups_update_file AFTER UPDATE OF songalbumid ON files FOR EACH ROW" \
  " BEGIN"								\
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (1, NEW.album, NEW.songalbumid);" \
  "   INSERT OR IGNORE INTO groups (type, name, persistentid) VALUES (2, NEW.album_artist, NEW.songartistid);" \
  " END;"

#define U_V15_SCVER				\
  "UPDATE admin SET value = '15' WHERE key = 'schema_version';"

static const struct db_upgrade_query db_upgrade_v15_queries[] =
  {
    { U_V15_TRG1,     "create trigger update_groups_new_file" },
    { U_V15_TRG2,     "create trigger update_groups_update_file" },

    { U_V15_SCVER,    "set schema_version to 15" },
  };

static int
db_upgrade_v15(sqlite3 *hdl)
{
#define Q_DUMP "SELECT 'INSERT INTO files " \
    "(id, path, fname, title, artist, album, genre, comment, type, composer," \
    " orchestra, conductor, grouping, url, bitrate, samplerate, song_length, file_size, year, track," \
    " total_tracks, disc, total_discs, bpm, compilation, artwork, rating, play_count, seek, data_kind, item_kind," \
    " description, time_added, time_modified, time_played, db_timestamp, disabled, sample_count," \
    " codectype, idx, has_video, contentrating, bits_per_sample, album_artist," \
    " media_kind, tv_series_name, tv_episode_num_str, tv_network_name, tv_episode_sort, tv_season_num, " \
    " songartistid, songalbumid, " \
    " title_sort, artist_sort, album_sort, composer_sort, album_artist_sort)" \
    " VALUES (' || id || ', ' || QUOTE(path) || ', ' || QUOTE(fname) || ', ' || QUOTE(title) || ', '" \
    " || QUOTE(artist) || ', ' || QUOTE(album) || ', ' || QUOTE(genre) || ', ' || QUOTE(comment) || ', '" \
    " || QUOTE(type) || ', ' || QUOTE(composer) || ', ' || QUOTE(orchestra) || ', ' || QUOTE(conductor) || ', '" \
    " || QUOTE(grouping) || ', ' || QUOTE(url) || ', ' || bitrate || ', ' || samplerate || ', '" \
    " || song_length || ', ' || file_size || ', ' || year || ', ' || track || ', ' || total_tracks || ', '" \
    " || disc || ', ' || total_discs || ', ' || bpm || ', ' || compilation || ', 0, ' || rating || ', '" \
    " || play_count || ',  ' || seek || ', ' || data_kind || ', ' || item_kind || ', ' ||  QUOTE(description) || ', '" \
    " || time_added || ', ' || time_modified || ', ' || time_played || ', ' || db_timestamp || ', '" \
    " || disabled || ', ' || sample_count || ', ' || QUOTE(codectype) || ', ' || idx || ', '" \
    " || has_video || ', ' || contentrating || ', ' || bits_per_sample || ', ' || QUOTE(album_artist) || ', '" \
    " || media_kind || ', ' || QUOTE(tv_series_name) || ', ' || QUOTE(tv_episode_num_str) || ', '" \
    " || QUOTE(tv_network_name) || ', ' || tv_episode_sort || ', ' || tv_season_num || ', '" \
    " || songartistid ||', ' || songalbumid || ', '" \
    " || QUOTE(title_sort) || ', ' || QUOTE(artist_sort) || ', ' || QUOTE(album_sort) || ', '" \
    " || QUOTE(composer_sort) || ', ' || QUOTE(album_artist_sort) || ');' FROM files;"

  return db_upgrade_files_table(hdl, Q_DUMP, U_V15_NEW_FILES_TABLE);

#undef Q_DUMP
}

/* Upgrade from schema v15 to v15.01 */
/* Improved indices (will be generated by generic schema update) */

#define U_V1501_SCVER_MAJOR			\
  "INSERT INTO admin (key, value) VALUES ('schema_version_major', '15');"
#define U_V1501_SCVER_MINOR			\
  "INSERT INTO admin (key, value) VALUES ('schema_version_minor', '01');"

static const struct db_upgrade_query db_upgrade_v1501_queries[] =
  {
    { U_V1501_SCVER_MAJOR,    "set schema_version_major to 15" },
    { U_V1501_SCVER_MINOR,    "set schema_version_minor to 01" },
  };

/* Upgrade from schema v15.01 to v16 */

#define U_V16_CREATE_VIEW_FILELIST			\
  "CREATE VIEW IF NOT EXISTS filelist as"		\
  "     SELECT "					\
  "       virtual_path, time_modified, 3 as type "	\
  "     FROM files WHERE disabled = 0"			\
  "   UNION "						\
  "     SELECT "					\
  "       virtual_path, db_timestamp, 1 as type "	\
  "     FROM playlists WHERE disabled = 0 AND type = 0"	\
  ";"

#define U_V16_ALTER_TBL_FILES_ADD_COL					\
  "ALTER TABLE files ADD COLUMN virtual_path VARCHAR(4096) DEFAULT NULL;"

#define U_V16_ALTER_TBL_PL_ADD_COL					\
  "ALTER TABLE playlists ADD COLUMN virtual_path VARCHAR(4096) DEFAULT NULL;"

#define D_V1600_SCVER				\
  "DELETE FROM admin WHERE key = 'schema_version';"
#define U_V1600_SCVER_MAJOR			\
  "UPDATE admin SET value = '16' WHERE key = 'schema_version_major';"
#define U_V1600_SCVER_MINOR			\
  "UPDATE admin SET value = '00' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v16_queries[] =
  {
    { U_V16_ALTER_TBL_FILES_ADD_COL, "alter table files add column virtual_path" },
    { U_V16_ALTER_TBL_PL_ADD_COL,    "alter table playlists add column virtual_path" },
    { U_V16_CREATE_VIEW_FILELIST,    "create new view filelist" },

    { D_V1600_SCVER,                "delete schema_version" },
    { U_V1600_SCVER_MAJOR,          "set schema_version_major to 16" },
    { U_V1600_SCVER_MINOR,          "set schema_version_minor to 00" },
  };

static int
db_upgrade_v16(sqlite3 *hdl)
{
  sqlite3_stmt *stmt;
  char *query;
  char *uquery;
  char *errmsg;
  char *artist;
  char *album;
  char *title;
  int id;
  char *path;
  int type;
  char virtual_path[PATH_MAX];
  int ret;

  query = "SELECT id, album_artist, album, title, path FROM files;";

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
      artist = (char *)sqlite3_column_text(stmt, 1);
      album = (char *)sqlite3_column_text(stmt, 2);
      title = (char *)sqlite3_column_text(stmt, 3);
      path = (char *)sqlite3_column_text(stmt, 4);

      if (strncmp(path, "http:", strlen("http:")) == 0)
	{
	  snprintf(virtual_path, PATH_MAX, "/http:/%s", title);
	}
      else if (strncmp(path, "spotify:", strlen("spotify:")) == 0)
	{
	  snprintf(virtual_path, PATH_MAX, "/spotify:/%s/%s/%s", artist, album, title);
	}
      else
	{
	  snprintf(virtual_path, PATH_MAX, "/file:%s", path);
	}

      uquery = sqlite3_mprintf("UPDATE files SET virtual_path = '%q' WHERE id = %d;", virtual_path, id);
      ret = sqlite3_exec(hdl, uquery, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Error updating files: %s\n", errmsg);
	}

      sqlite3_free(uquery);
      sqlite3_free(errmsg);
    }

  sqlite3_finalize(stmt);


  query = "SELECT id, title, path, type FROM playlists;";

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
      title = (char *)sqlite3_column_text(stmt, 1);
      path = (char *)sqlite3_column_text(stmt, 2);
      type = sqlite3_column_int(stmt, 3);

      if (type == 0) /* Excludes default/Smart playlists and playlist folders */
	{
	  if (strncmp(path, "spotify:", strlen("spotify:")) == 0)
	    snprintf(virtual_path, PATH_MAX, "/spotify:/%s", title);
	  else
	    snprintf(virtual_path, PATH_MAX, "/file:%s", path);

	  uquery = sqlite3_mprintf("UPDATE playlists SET virtual_path = '%q' WHERE id = %d;", virtual_path, id);

	  ret = sqlite3_exec(hdl, uquery, NULL, NULL, &errmsg);
	  if (ret != SQLITE_OK)
	    DPRINTF(E_LOG, L_DB, "Error updating playlists: %s\n", errmsg);

	  sqlite3_free(uquery);
	  sqlite3_free(errmsg);
	}
    }

  sqlite3_finalize(stmt);

  return 0;
}

/* Upgrade from schema v16.00 to v17.00 */
/* Expand data model to allow for nested playlists and change default playlist
 * enumeration
 */

#define U_V17_PL_PARENTID_ADD			\
  "ALTER TABLE playlists ADD COLUMN parent_id INTEGER DEFAULT 0;"
#define U_V17_PL_TYPE_CHANGE			\
  "UPDATE playlists SET type = 2 WHERE type = 1;"

#define U_V17_SCVER_MAJOR			\
  "UPDATE admin SET value = '17' WHERE key = 'schema_version_major';"
#define U_V17_SCVER_MINOR			\
  "UPDATE admin SET value = '00' WHERE key = 'schema_version_minor';"

static const struct db_upgrade_query db_upgrade_v17_queries[] =
  {
    { U_V17_PL_PARENTID_ADD,"expanding table playlists with parent_id column" },
    { U_V17_PL_TYPE_CHANGE, "changing numbering of default playlists 1 -> 2" },

    { U_V17_SCVER_MAJOR,    "set schema_version_major to 17" },
    { U_V17_SCVER_MINOR,    "set schema_version_minor to 00" },
  };

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


int
db_upgrade(sqlite3 *hdl, int db_ver)
{
  int ret;

  ret = db_drop_indices(hdl);
  if (ret < 0)
    return -1;

  switch (db_ver)
    {
    case 1000:
      ret = db_generic_upgrade(hdl, db_upgrade_v11_queries, sizeof(db_upgrade_v11_queries) / sizeof(db_upgrade_v11_queries[0]));
      if (ret < 0)
	return -1;

      ret = db_upgrade_v11(hdl);
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1100:
      ret = db_upgrade_v12(hdl);
      if (ret < 0)
	return -1;

      ret = db_generic_upgrade(hdl, db_upgrade_v12_queries, sizeof(db_upgrade_v12_queries) / sizeof(db_upgrade_v12_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1200:
      ret = db_generic_upgrade(hdl, db_upgrade_v13_queries, sizeof(db_upgrade_v13_queries) / sizeof(db_upgrade_v13_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1300:
      ret = db_upgrade_v14(hdl);
      if (ret < 0)
	return -1;

      ret = db_generic_upgrade(hdl, db_upgrade_v14_queries, sizeof(db_upgrade_v14_queries) / sizeof(db_upgrade_v14_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1400:
      ret = db_upgrade_v15(hdl);
      if (ret < 0)
	return -1;

      ret = db_generic_upgrade(hdl, db_upgrade_v15_queries, sizeof(db_upgrade_v15_queries) / sizeof(db_upgrade_v15_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1500:
      ret = db_generic_upgrade(hdl, db_upgrade_v1501_queries, sizeof(db_upgrade_v1501_queries) / sizeof(db_upgrade_v1501_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1501:
      ret = db_generic_upgrade(hdl, db_upgrade_v16_queries, sizeof(db_upgrade_v16_queries) / sizeof(db_upgrade_v16_queries[0]));
      if (ret < 0)
	return -1;

      ret = db_upgrade_v16(hdl);
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1600:
      ret = db_generic_upgrade(hdl, db_upgrade_v17_queries, sizeof(db_upgrade_v17_queries) / sizeof(db_upgrade_v17_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1700:
      ret = db_generic_upgrade(hdl, db_upgrade_v18_queries, sizeof(db_upgrade_v18_queries) / sizeof(db_upgrade_v18_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1800:
      ret = db_generic_upgrade(hdl, db_upgrade_v1801_queries, sizeof(db_upgrade_v1801_queries) / sizeof(db_upgrade_v1801_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1801:
      ret = db_generic_upgrade(hdl, db_upgrade_v1900_queries, sizeof(db_upgrade_v1900_queries) / sizeof(db_upgrade_v1900_queries[0]));
      if (ret < 0)
	return -1;

      ret = db_upgrade_v19(hdl);
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1900:
      ret = db_generic_upgrade(hdl, db_upgrade_v1901_queries, sizeof(db_upgrade_v1901_queries) / sizeof(db_upgrade_v1901_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1901:
      ret = db_generic_upgrade(hdl, db_upgrade_v1902_queries, sizeof(db_upgrade_v1902_queries) / sizeof(db_upgrade_v1902_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1902:
      ret = db_generic_upgrade(hdl, db_upgrade_v1903_queries, sizeof(db_upgrade_v1903_queries) / sizeof(db_upgrade_v1903_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1903:
      ret = db_generic_upgrade(hdl, db_upgrade_v1904_queries, sizeof(db_upgrade_v1904_queries) / sizeof(db_upgrade_v1904_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1904:
      ret = db_generic_upgrade(hdl, db_upgrade_v1905_queries, sizeof(db_upgrade_v1905_queries) / sizeof(db_upgrade_v1905_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1905:
      ret = db_generic_upgrade(hdl, db_upgrade_V1906_queries, sizeof(db_upgrade_V1906_queries) / sizeof(db_upgrade_V1906_queries[0]));
      if (ret < 0)
	return -1;

      /* FALLTHROUGH */

    case 1906:
      ret = db_generic_upgrade(hdl, db_upgrade_V1907_queries, sizeof(db_upgrade_V1907_queries) / sizeof(db_upgrade_V1907_queries[0]));
      if (ret < 0)
	return -1;

      break;

    default:
      DPRINTF(E_FATAL, L_DB, "No upgrade path from the current DB schema\n");
      return -1;
    }

  return 0;
}
