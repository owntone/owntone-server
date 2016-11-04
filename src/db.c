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
#include <limits.h>

#include <pthread.h>

#include <sqlite3.h>

#include "conffile.h"
#include "logger.h"
#include "cache.h"
#include "misc.h"
#include "db.h"
#include "db_init.h"
#include "db_upgrade.h"


#define STR(x) ((x) ? (x) : "")

/* Inotify cookies are uint32_t */
#define INOTIFY_FAKE_COOKIE ((int64_t)1 << 32)

enum group_type {
  G_ALBUMS = 1,
  G_ARTISTS = 2,
};

struct db_unlock {
  int proceed;
  pthread_cond_t cond;
  pthread_mutex_t lck;
};

#define DB_TYPE_CHAR    1
#define DB_TYPE_INT     2
#define DB_TYPE_INT64   3
#define DB_TYPE_STRING  4

struct col_type_map {
  ssize_t offset;
  short type;
};

/* This list must be kept in sync with
 * - the order of the columns in the files table
 * - the type and name of the fields in struct media_file_info
 */
static const struct col_type_map mfi_cols_map[] =
  {
    { mfi_offsetof(id),                 DB_TYPE_INT },
    { mfi_offsetof(path),               DB_TYPE_STRING },
    { mfi_offsetof(fname),              DB_TYPE_STRING },
    { mfi_offsetof(title),              DB_TYPE_STRING },
    { mfi_offsetof(artist),             DB_TYPE_STRING },
    { mfi_offsetof(album),              DB_TYPE_STRING },
    { mfi_offsetof(genre),              DB_TYPE_STRING },
    { mfi_offsetof(comment),            DB_TYPE_STRING },
    { mfi_offsetof(type),               DB_TYPE_STRING },
    { mfi_offsetof(composer),           DB_TYPE_STRING },
    { mfi_offsetof(orchestra),          DB_TYPE_STRING },
    { mfi_offsetof(conductor),          DB_TYPE_STRING },
    { mfi_offsetof(grouping),           DB_TYPE_STRING },
    { mfi_offsetof(url),                DB_TYPE_STRING },
    { mfi_offsetof(bitrate),            DB_TYPE_INT },
    { mfi_offsetof(samplerate),         DB_TYPE_INT },
    { mfi_offsetof(song_length),        DB_TYPE_INT },
    { mfi_offsetof(file_size),          DB_TYPE_INT64 },
    { mfi_offsetof(year),               DB_TYPE_INT },
    { mfi_offsetof(track),              DB_TYPE_INT },
    { mfi_offsetof(total_tracks),       DB_TYPE_INT },
    { mfi_offsetof(disc),               DB_TYPE_INT },
    { mfi_offsetof(total_discs),        DB_TYPE_INT },
    { mfi_offsetof(bpm),                DB_TYPE_INT },
    { mfi_offsetof(compilation),        DB_TYPE_CHAR },
    { mfi_offsetof(artwork),            DB_TYPE_CHAR },
    { mfi_offsetof(rating),             DB_TYPE_INT },
    { mfi_offsetof(play_count),         DB_TYPE_INT },
    { mfi_offsetof(seek),               DB_TYPE_INT },
    { mfi_offsetof(data_kind),          DB_TYPE_INT },
    { mfi_offsetof(item_kind),          DB_TYPE_INT },
    { mfi_offsetof(description),        DB_TYPE_STRING },
    { mfi_offsetof(time_added),         DB_TYPE_INT },
    { mfi_offsetof(time_modified),      DB_TYPE_INT },
    { mfi_offsetof(time_played),        DB_TYPE_INT },
    { mfi_offsetof(db_timestamp),       DB_TYPE_INT },
    { mfi_offsetof(disabled),           DB_TYPE_INT },
    { mfi_offsetof(sample_count),       DB_TYPE_INT64 },
    { mfi_offsetof(codectype),          DB_TYPE_STRING },
    { mfi_offsetof(index),              DB_TYPE_INT },
    { mfi_offsetof(has_video),          DB_TYPE_INT },
    { mfi_offsetof(contentrating),      DB_TYPE_INT },
    { mfi_offsetof(bits_per_sample),    DB_TYPE_INT },
    { mfi_offsetof(album_artist),       DB_TYPE_STRING },
    { mfi_offsetof(media_kind),         DB_TYPE_INT },
    { mfi_offsetof(tv_series_name),     DB_TYPE_STRING },
    { mfi_offsetof(tv_episode_num_str), DB_TYPE_STRING },
    { mfi_offsetof(tv_network_name),    DB_TYPE_STRING },
    { mfi_offsetof(tv_episode_sort),    DB_TYPE_INT },
    { mfi_offsetof(tv_season_num),      DB_TYPE_INT },
    { mfi_offsetof(songartistid),       DB_TYPE_INT64 },
    { mfi_offsetof(songalbumid),        DB_TYPE_INT64 },
    { mfi_offsetof(title_sort),         DB_TYPE_STRING },
    { mfi_offsetof(artist_sort),        DB_TYPE_STRING },
    { mfi_offsetof(album_sort),         DB_TYPE_STRING },
    { mfi_offsetof(composer_sort),      DB_TYPE_STRING },
    { mfi_offsetof(album_artist_sort),  DB_TYPE_STRING },
    { mfi_offsetof(virtual_path),       DB_TYPE_STRING },
    { mfi_offsetof(directory_id),       DB_TYPE_INT },
    { mfi_offsetof(date_released),      DB_TYPE_INT },
  };

/* This list must be kept in sync with
 * - the order of the columns in the playlists table
 * - the type and name of the fields in struct playlist_info
 */
static const struct col_type_map pli_cols_map[] =
  {
    { pli_offsetof(id),           DB_TYPE_INT },
    { pli_offsetof(title),        DB_TYPE_STRING },
    { pli_offsetof(type),         DB_TYPE_INT },
    { pli_offsetof(query),        DB_TYPE_STRING },
    { pli_offsetof(db_timestamp), DB_TYPE_INT },
    { pli_offsetof(disabled),     DB_TYPE_INT },
    { pli_offsetof(path),         DB_TYPE_STRING },
    { pli_offsetof(index),        DB_TYPE_INT },
    { pli_offsetof(special_id),   DB_TYPE_INT },
    { pli_offsetof(virtual_path), DB_TYPE_STRING },
    { pli_offsetof(parent_id),    DB_TYPE_INT },
    { pli_offsetof(directory_id), DB_TYPE_INT },

    /* items is computed on the fly */
  };

/* This list must be kept in sync with
 * - the order of the columns in the files table
 * - the name of the fields in struct db_media_file_info
 */
static const ssize_t dbmfi_cols_map[] =
  {
    dbmfi_offsetof(id),
    dbmfi_offsetof(path),
    dbmfi_offsetof(fname),
    dbmfi_offsetof(title),
    dbmfi_offsetof(artist),
    dbmfi_offsetof(album),
    dbmfi_offsetof(genre),
    dbmfi_offsetof(comment),
    dbmfi_offsetof(type),
    dbmfi_offsetof(composer),
    dbmfi_offsetof(orchestra),
    dbmfi_offsetof(conductor),
    dbmfi_offsetof(grouping),
    dbmfi_offsetof(url),
    dbmfi_offsetof(bitrate),
    dbmfi_offsetof(samplerate),
    dbmfi_offsetof(song_length),
    dbmfi_offsetof(file_size),
    dbmfi_offsetof(year),
    dbmfi_offsetof(track),
    dbmfi_offsetof(total_tracks),
    dbmfi_offsetof(disc),
    dbmfi_offsetof(total_discs),
    dbmfi_offsetof(bpm),
    dbmfi_offsetof(compilation),
    dbmfi_offsetof(artwork),
    dbmfi_offsetof(rating),
    dbmfi_offsetof(play_count),
    dbmfi_offsetof(seek),
    dbmfi_offsetof(data_kind),
    dbmfi_offsetof(item_kind),
    dbmfi_offsetof(description),
    dbmfi_offsetof(time_added),
    dbmfi_offsetof(time_modified),
    dbmfi_offsetof(time_played),
    dbmfi_offsetof(db_timestamp),
    dbmfi_offsetof(disabled),
    dbmfi_offsetof(sample_count),
    dbmfi_offsetof(codectype),
    dbmfi_offsetof(idx),
    dbmfi_offsetof(has_video),
    dbmfi_offsetof(contentrating),
    dbmfi_offsetof(bits_per_sample),
    dbmfi_offsetof(album_artist),
    dbmfi_offsetof(media_kind),
    dbmfi_offsetof(tv_series_name),
    dbmfi_offsetof(tv_episode_num_str),
    dbmfi_offsetof(tv_network_name),
    dbmfi_offsetof(tv_episode_sort),
    dbmfi_offsetof(tv_season_num),
    dbmfi_offsetof(songartistid),
    dbmfi_offsetof(songalbumid),
    dbmfi_offsetof(title_sort),
    dbmfi_offsetof(artist_sort),
    dbmfi_offsetof(album_sort),
    dbmfi_offsetof(composer_sort),
    dbmfi_offsetof(album_artist_sort),
    dbmfi_offsetof(virtual_path),
    dbmfi_offsetof(directory_id),
    dbmfi_offsetof(date_released),
  };

/* This list must be kept in sync with
 * - the order of the columns in the playlists table
 * - the name of the fields in struct playlist_info
 */
static const ssize_t dbpli_cols_map[] =
  {
    dbpli_offsetof(id),
    dbpli_offsetof(title),
    dbpli_offsetof(type),
    dbpli_offsetof(query),
    dbpli_offsetof(db_timestamp),
    dbpli_offsetof(disabled),
    dbpli_offsetof(path),
    dbpli_offsetof(index),
    dbpli_offsetof(special_id),
    dbpli_offsetof(virtual_path),
    dbpli_offsetof(parent_id),
    dbpli_offsetof(directory_id),

    /* items is computed on the fly */
  };

/* This list must be kept in sync with
 * - the order of fields in the Q_GROUP_ALBUMS and Q_GROUP_ARTISTS query
 * - the name of the fields in struct group_info
 */
static const ssize_t dbgri_cols_map[] =
  {
    dbgri_offsetof(id),
    dbgri_offsetof(persistentid),
    dbgri_offsetof(itemname),
    dbgri_offsetof(itemname_sort),
    dbgri_offsetof(itemcount),
    dbgri_offsetof(groupalbumcount),
    dbgri_offsetof(songalbumartist),
    dbgri_offsetof(songartistid),
    dbgri_offsetof(song_length),
  };

/* This list must be kept in sync with
 * - the order of the columns in the inotify table
 * - the name and type of the fields in struct watch_info
 */
static const struct col_type_map wi_cols_map[] =
  {
    { wi_offsetof(wd),     DB_TYPE_INT },
    { wi_offsetof(cookie), DB_TYPE_INT },
    { wi_offsetof(path),   DB_TYPE_STRING },
  };

/* Sort clauses */
/* Keep in sync with enum sort_type */
static const char *sort_clause[] =
  {
    "",
    "ORDER BY f.title_sort ASC",
    "ORDER BY f.album_sort ASC, f.disc ASC, f.track ASC",
    "ORDER BY f.album_artist_sort ASC, f.album_sort ASC, f.disc ASC, f.track ASC",
    "ORDER BY f.type ASC, f.parent_id ASC, f.special_id ASC, f.title ASC",
    "ORDER BY f.year ASC",
    "ORDER BY f.genre ASC",
    "ORDER BY f.composer_sort ASC",
    "ORDER BY f.disc ASC",
    "ORDER BY f.track ASC",
    "ORDER BY f.virtual_path ASC",
  };

static char *db_path;
static __thread sqlite3 *hdl;


/* Forward */
static int
db_pl_count_items(int id, int streams_only);

static int
db_smartpl_count_items(const char *smartpl_query);

struct playlist_info *
db_pl_fetch_byid(int id);

static enum group_type
db_group_type_bypersistentid(int64_t persistentid);


char *
db_escape_string(const char *str)
{
  char *escaped;
  char *ret;

  escaped = sqlite3_mprintf("%q", str);
  if (!escaped)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for escaped string\n");

      return NULL;
    }

  ret = strdup(escaped);

  sqlite3_free(escaped);

  return ret;
}

void
free_pi(struct pairing_info *pi, int content_only)
{
  if (pi->remote_id)
    free(pi->remote_id);

  if (pi->name)
    free(pi->name);

  if (pi->guid)
    free(pi->guid);

  if (!content_only)
    free(pi);
  else
    memset(pi, 0, sizeof(struct pairing_info));
}

void
free_mfi(struct media_file_info *mfi, int content_only)
{
  if (mfi->path)
    free(mfi->path);

  if (mfi->fname)
    free(mfi->fname);

  if (mfi->title)
    free(mfi->title);

  if (mfi->artist)
    free(mfi->artist);

  if (mfi->album)
    free(mfi->album);

  if (mfi->genre)
    free(mfi->genre);

  if (mfi->comment)
    free(mfi->comment);

  if (mfi->type)
    free(mfi->type);

  if (mfi->composer)
    free(mfi->composer);

  if (mfi->orchestra)
    free(mfi->orchestra);

  if (mfi->conductor)
    free(mfi->conductor);

  if (mfi->grouping)
    free(mfi->grouping);

  if (mfi->description)
    free(mfi->description);

  if (mfi->codectype)
    free(mfi->codectype);

  if (mfi->album_artist)
    free(mfi->album_artist);

  if (mfi->tv_series_name)
    free(mfi->tv_series_name);

  if (mfi->tv_episode_num_str)
    free(mfi->tv_episode_num_str);

  if (mfi->tv_network_name)
    free(mfi->tv_network_name);

  if (mfi->title_sort)
    free(mfi->title_sort);

  if (mfi->artist_sort)
    free(mfi->artist_sort);

  if (mfi->album_sort)
    free(mfi->album_sort);

  if (mfi->composer_sort)
    free(mfi->composer_sort);

  if (mfi->album_artist_sort)
    free(mfi->album_artist_sort);

  if (mfi->virtual_path)
    free(mfi->virtual_path);

  if (!content_only)
    free(mfi);
  else
    memset(mfi, 0, sizeof(struct media_file_info));
}

void
unicode_fixup_mfi(struct media_file_info *mfi)
{
  char *ret;
  char **field;
  int i;

  for (i = 0; i < (sizeof(mfi_cols_map) / sizeof(mfi_cols_map[0])); i++)
    {
      if (mfi_cols_map[i].type != DB_TYPE_STRING)
	continue;

      switch (mfi_cols_map[i].offset)
	{
	  case mfi_offsetof(path):
	  case mfi_offsetof(fname):
	  case mfi_offsetof(codectype):
	    continue;
	}

      field = (char **) ((char *)mfi + mfi_cols_map[i].offset);

      if (!*field)
	continue;

      ret = unicode_fixup_string(*field, "ascii");
      if (ret != *field)
	{
	  free(*field);
	  *field = ret;
	}
    }
}

void
free_pli(struct playlist_info *pli, int content_only)
{
  if (pli->title)
    free(pli->title);

  if (pli->query)
    free(pli->query);

  if (pli->path)
    free(pli->path);

  if (pli->virtual_path)
    free(pli->virtual_path);

  if (!content_only)
    free(pli);
  else
    memset(pli, 0, sizeof(struct playlist_info));
}

void
free_di(struct directory_info *di, int content_only)
{
  if (di->virtual_path)
    free(di->virtual_path);

  if (!content_only)
    free(di);
  else
    memset(di, 0, sizeof(struct directory_info));
}


/* Unlock notification support */
static void
unlock_notify_cb(void **args, int nargs)
{
  struct db_unlock *u;
  int i;

  for (i = 0; i < nargs; i++)
    {
      u = (struct db_unlock *)args[i];

      pthread_mutex_lock(&u->lck);

      u->proceed = 1;
      pthread_cond_signal(&u->cond);

      pthread_mutex_unlock(&u->lck);
    }
}

static int
db_wait_unlock(void)
{
  struct db_unlock u;
  int ret;

  u.proceed = 0;
  pthread_mutex_init(&u.lck, NULL);
  pthread_cond_init(&u.cond, NULL);

  ret = sqlite3_unlock_notify(hdl, unlock_notify_cb, &u);
  if (ret == SQLITE_OK)
    {
      pthread_mutex_lock(&u.lck);

      if (!u.proceed)
	{
	  DPRINTF(E_INFO, L_DB, "Waiting for database unlock\n");
	  pthread_cond_wait(&u.cond, &u.lck);
	}

      pthread_mutex_unlock(&u.lck);
    }

  pthread_cond_destroy(&u.cond);
  pthread_mutex_destroy(&u.lck);

  return ret;
}

static int
db_blocking_step(sqlite3_stmt *stmt)
{
  int ret;

  while ((ret = sqlite3_step(stmt)) == SQLITE_LOCKED)
    {
      ret = db_wait_unlock();
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Database deadlocked!\n");
	  break;
	}

      sqlite3_reset(stmt);
    }

  return ret;
}

static int
db_blocking_prepare_v2(const char *query, int len, sqlite3_stmt **stmt, const char **end)
{
  int ret;

  while ((ret = sqlite3_prepare_v2(hdl, query, len, stmt, end)) == SQLITE_LOCKED)
    {
      ret = db_wait_unlock();
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Database deadlocked!\n");
	  break;
	}
    }

  return ret;
}


/* Modelled after sqlite3_exec() */
static int
db_exec(const char *query, char **errmsg)
{
  sqlite3_stmt *stmt;
  int try;
  int ret;

  *errmsg = NULL;

  for (try = 0; try < 5; try++)
    {
      ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
      if (ret != SQLITE_OK)
	{
	  *errmsg = sqlite3_mprintf("prepare failed: %s", sqlite3_errmsg(hdl));
	  return ret;
	}

      while ((ret = db_blocking_step(stmt)) == SQLITE_ROW)
	; /* EMPTY */

      sqlite3_finalize(stmt);

      if (ret != SQLITE_SCHEMA)
	break;
    }

  if (ret != SQLITE_DONE)
    {
      *errmsg = sqlite3_mprintf("step failed: %s", sqlite3_errmsg(hdl));
      return ret;
    }

  return SQLITE_OK;
}


/* Maintenance and DB hygiene */
static void
db_analyze(void)
{
  char *query = "ANALYZE;";
  char *errmsg;
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "ANALYZE failed: %s\n", errmsg);

      sqlite3_free(errmsg);
    }
}

/* Set names of default playlists according to config */
static void
db_set_cfg_names(void)
{
#define Q_TMPL "UPDATE playlists SET title = '%q' WHERE type = %d AND special_id = %d;"
  char *cfg_item[6] = { "name_library", "name_music", "name_movies", "name_tvshows", "name_podcasts", "name_audiobooks" };
  char special_id[6] = { 0, 6, 4, 5, 1, 7 };
  cfg_t *lib;
  char *query;
  char *title;
  char *errmsg;
  int ret;
  int i;

  lib = cfg_getsec(cfg, "library");

  for (i = 0; i < (sizeof(cfg_item) / sizeof(cfg_item[0])); i++)
    {
      title = cfg_getstr(lib, cfg_item[i]);
      if (!title)
	{
	  DPRINTF(E_LOG, L_DB, "Internal error, unknown config item '%s'\n", cfg_item[i]);

	  continue;
	}

      query = sqlite3_mprintf(Q_TMPL, title, PL_SPECIAL, special_id[i]);
      if (!query)
	{
	  DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

	  return;
	}

      ret = db_exec(query, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Error setting playlist title, query %s, error: %s\n", query, errmsg);

	  sqlite3_free(errmsg);
	}
      else
	DPRINTF(E_DBG, L_DB, "Playlist title for config item '%s' set with query '%s'\n", cfg_item[i], query);

      sqlite3_free(query);
    }
#undef Q_TMPL
}

void
db_hook_post_scan(void)
{
  DPRINTF(E_DBG, L_DB, "Running post-scan DB maintenance tasks...\n");

  db_analyze();

  DPRINTF(E_DBG, L_DB, "Done with post-scan DB maintenance\n");
}

void
db_purge_cruft(time_t ref)
{
  char *errmsg;
  int i;
  int ret;
  char *queries[4] = { NULL, NULL, NULL, NULL };
  char *queries_tmpl[4] =
    {
      "DELETE FROM playlistitems WHERE playlistid IN (SELECT id FROM playlists p WHERE p.type <> %d AND p.db_timestamp < %" PRIi64 ");",
      "DELETE FROM playlists WHERE type <> %d AND db_timestamp < %" PRIi64 ";",
      "DELETE FROM files WHERE -1 <> %d AND db_timestamp < %" PRIi64 ";",
      "DELETE FROM directories WHERE id > 4 AND -1 <> %d AND db_timestamp < %" PRIi64 ";"
    };

  if (sizeof(queries) != sizeof(queries_tmpl))
    {
      DPRINTF(E_LOG, L_DB, "db_purge_cruft(): queries out of sync with queries_tmpl\n");
      return;
    }

  for (i = 0; i < (sizeof(queries_tmpl) / sizeof(queries_tmpl[0])); i++)
    {
      queries[i] = sqlite3_mprintf(queries_tmpl[i], PL_SPECIAL, (int64_t)ref);
      if (!queries[i])
	{
	  DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
	  goto purge_fail;
	}
    }

  for (i = 0; i < (sizeof(queries) / sizeof(queries[0])); i++)
    {
      DPRINTF(E_DBG, L_DB, "Running purge query '%s'\n", queries[i]);

      ret = db_exec(queries[i], &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Purge query %d error: %s\n", i, errmsg);

	  sqlite3_free(errmsg);
	}
      else
	DPRINTF(E_DBG, L_DB, "Purged %d rows\n", sqlite3_changes(hdl));
    }

 purge_fail:
  for (i = 0; i < (sizeof(queries) / sizeof(queries[0])); i++)
    {
      sqlite3_free(queries[i]);
    }

}

void
db_purge_all(void)
{
#define Q_TMPL "DELETE FROM playlists WHERE type <> %d;"
  char *queries[4] =
    {
      "DELETE FROM inotify;",
      "DELETE FROM playlistitems;",
      "DELETE FROM files;",
      "DELETE FROM groups;",
    };
  char *errmsg;
  char *query;
  int i;
  int ret;

  for (i = 0; i < (sizeof(queries) / sizeof(queries[0])); i++)
    {
      DPRINTF(E_DBG, L_DB, "Running purge query '%s'\n", queries[i]);

      ret = db_exec(queries[i], &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Purge query %d error: %s\n", i, errmsg);

	  sqlite3_free(errmsg);
	}
      else
	DPRINTF(E_DBG, L_DB, "Purged %d rows\n", sqlite3_changes(hdl));
    }

  query = sqlite3_mprintf(Q_TMPL, PL_SPECIAL);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return;
    }

  DPRINTF(E_DBG, L_DB, "Running purge query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Purge query '%s' error: %s\n", query, errmsg);

      sqlite3_free(errmsg);
    }
  else
    DPRINTF(E_DBG, L_DB, "Purged %d rows\n", sqlite3_changes(hdl));

  sqlite3_free(query);
#undef Q_TMPL
}

static int
db_get_count(char *query)
{
  sqlite3_stmt *stmt;
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));	

      sqlite3_finalize(stmt);
      return -1;
    }

  ret = sqlite3_column_int(stmt, 0);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);

  return ret;
}


/* Transactions */
void
db_transaction_begin(void)
{
  char *query = "BEGIN TRANSACTION;";
  char *errmsg;
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "SQL error running '%s': %s\n", query, errmsg);

      sqlite3_free(errmsg);
    }
}

void
db_transaction_end(void)
{
  char *query = "END TRANSACTION;";
  char *errmsg;
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "SQL error running '%s': %s\n", query, errmsg);

      sqlite3_free(errmsg);
    }
}


/* Queries */
static int
db_build_query_index_clause(char **i, struct query_params *qp)
{
  char *idx;

  *i = NULL;

  switch (qp->idx_type)
    {
      case I_FIRST:
	idx = sqlite3_mprintf("LIMIT %d", qp->limit);
	break;

      case I_LAST:
	idx = sqlite3_mprintf("LIMIT -1 OFFSET %d", qp->results - qp->limit);
	break;

      case I_SUB:
	idx = sqlite3_mprintf("LIMIT %d OFFSET %d", qp->limit, qp->offset);
	break;

      case I_NONE:
	return 0;

      default:
	DPRINTF(E_LOG, L_DB, "Unknown index type\n");
	return -1;
    }

  if (!idx)
    {
      DPRINTF(E_LOG, L_DB, "Could not build index string; out of memory");
      return -1;
    }

  *i = idx;

  return 0;
}

static int
db_build_query_items(struct query_params *qp, char **q)
{
  char *query;
  char *count;
  char *idx;
  const char *sort;
  int ret;

  if (qp->filter)
    count = sqlite3_mprintf("SELECT COUNT(*) FROM files f WHERE f.disabled = 0 AND %s;", qp->filter);
  else
    count = sqlite3_mprintf("SELECT COUNT(*) FROM files f WHERE f.disabled = 0;");

  if (!count)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for count query string\n");

      return -1;
    }

  qp->results = db_get_count(count);
  sqlite3_free(count);

  if (qp->results < 0)
    return -1;

  /* Get index clause */
  ret = db_build_query_index_clause(&idx, qp);
  if (ret < 0)
    return -1;

  sort = sort_clause[qp->sort];

  if (idx && qp->filter)
    query = sqlite3_mprintf("SELECT f.* FROM files f WHERE f.disabled = 0 AND %s %s %s;", qp->filter, sort, idx);
  else if (idx)
    query = sqlite3_mprintf("SELECT f.* FROM files f WHERE f.disabled = 0 %s %s;", sort, idx);
  else if (qp->filter)
    query = sqlite3_mprintf("SELECT f.* FROM files f WHERE f.disabled = 0 AND %s %s;", qp->filter, sort);
  else
    query = sqlite3_mprintf("SELECT f.* FROM files f WHERE f.disabled = 0 %s;", sort);

  if (idx)
    sqlite3_free(idx);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_pls(struct query_params *qp, char **q)
{
  char *query;
  char *idx;
  const char *sort;
  int ret;

  qp->results = db_get_count("SELECT COUNT(*) FROM playlists p WHERE p.disabled = 0;");
  if (qp->results < 0)
    return -1;

  /* Get index clause */
  ret = db_build_query_index_clause(&idx, qp);
  if (ret < 0)
    return -1;

  sort = sort_clause[qp->sort];

  if (idx && qp->filter)
    query = sqlite3_mprintf("SELECT f.* FROM playlists f WHERE f.disabled = 0 AND %s %s %s;", qp->filter, sort, idx);
  else if (idx)
    query = sqlite3_mprintf("SELECT f.* FROM playlists f WHERE f.disabled = 0 %s %s;", sort, idx);
  else if (qp->filter)
    query = sqlite3_mprintf("SELECT f.* FROM playlists f WHERE f.disabled = 0 AND %s %s;", qp->filter, sort);
  else
    query = sqlite3_mprintf("SELECT f.* FROM playlists f WHERE f.disabled = 0 %s;", sort);

  if (idx)
    sqlite3_free(idx);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_plitems_plain(struct query_params *qp, char **q)
{
  char *query;
  char *count;
  char *idx;
  int ret;

  if (qp->filter)
    count = sqlite3_mprintf("SELECT COUNT(*) FROM files f JOIN playlistitems pi ON f.path = pi.filepath"
			    " WHERE pi.playlistid = %d AND f.disabled = 0 AND %s;", qp->id, qp->filter);
  else
    count = sqlite3_mprintf("SELECT COUNT(*) FROM files f JOIN playlistitems pi ON f.path = pi.filepath"
			    " WHERE pi.playlistid = %d AND f.disabled = 0;", qp->id);

  if (!count)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for count query string\n");

      return -1;
    }

  qp->results = db_get_count(count);
  sqlite3_free(count);

  if (qp->results < 0)
    return -1;

  /* Get index clause */
  ret = db_build_query_index_clause(&idx, qp);
  if (ret < 0)
    return -1;

  if (idx && qp->filter)
    query = sqlite3_mprintf("SELECT f.* FROM files f JOIN playlistitems pi ON f.path = pi.filepath"
			    " WHERE pi.playlistid = %d AND f.disabled = 0 AND %s ORDER BY pi.id ASC %s;",
			    qp->id, qp->filter, idx);
  else if (idx)
    query = sqlite3_mprintf("SELECT f.* FROM files f JOIN playlistitems pi ON f.path = pi.filepath"
			    " WHERE pi.playlistid = %d AND f.disabled = 0 ORDER BY pi.id ASC %s;",
			    qp->id, idx);
  else if (qp->filter)
    query = sqlite3_mprintf("SELECT f.* FROM files f JOIN playlistitems pi ON f.path = pi.filepath"
			    " WHERE pi.playlistid = %d AND f.disabled = 0 AND %s ORDER BY pi.id ASC;",
			    qp->id, qp->filter);
  else
    query = sqlite3_mprintf("SELECT f.* FROM files f JOIN playlistitems pi ON f.path = pi.filepath"
			    " WHERE pi.playlistid = %d AND f.disabled = 0 ORDER BY pi.id ASC;",
			    qp->id);

  if (idx)
    sqlite3_free(idx);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_plitems_smart(struct query_params *qp, char *smartpl_query, char **q)
{
  char *query;
  char *count;
  char *filter;
  char *idx;
  const char *sort;
  int ret;

  if (qp->filter)
    filter = qp->filter;
  else
    filter = "1 = 1";

  count = sqlite3_mprintf("SELECT COUNT(*) FROM files f WHERE f.disabled = 0 AND %s AND %s;", filter, smartpl_query);
  if (!count)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for count query string\n");
      return -1;
    }

  qp->results = db_get_count(count);

  sqlite3_free(count);

  if (qp->results < 0)
    return -1;

  /* Get index clause */
  ret = db_build_query_index_clause(&idx, qp);
  if (ret < 0)
    return -1;

  sort = sort_clause[qp->sort];

  if (idx)
    query = sqlite3_mprintf("SELECT f.* FROM files f WHERE f.disabled = 0 AND %s AND %s %s %s;", smartpl_query, filter, sort, idx);
  else
    query = sqlite3_mprintf("SELECT f.* FROM files f WHERE f.disabled = 0 AND %s AND %s %s;", smartpl_query, filter, sort);

  if (idx)
    sqlite3_free(idx);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_plitems(struct query_params *qp, char **q)
{
  struct playlist_info *pli;
  int ret;

  if (qp->id <= 0)
    {
      DPRINTF(E_LOG, L_DB, "No playlist id specified in playlist items query\n");
      return -1;
    }

  pli = db_pl_fetch_byid(qp->id);
  if (!pli)
    return -1;

  switch (pli->type)
    {
      case PL_SPECIAL:
      case PL_SMART:
	ret = db_build_query_plitems_smart(qp, pli->query, q);
	break;

      case PL_PLAIN:
      case PL_FOLDER:
	ret = db_build_query_plitems_plain(qp, q);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unknown playlist type %d in playlist items query\n", pli->type);
	ret = -1;
	break;
    }

  free_pli(pli, 0);

  return ret;
}

static int
db_build_query_group_albums(struct query_params *qp, char **q)
{
  char *query;
  char *idx;
  const char *sort;
  int ret;

  qp->results = db_get_count("SELECT COUNT(DISTINCT f.songalbumid) FROM files f WHERE f.disabled = 0;");
  if (qp->results < 0)
    return -1;

  /* Get index clause */
  ret = db_build_query_index_clause(&idx, qp);
  if (ret < 0)
    return -1;

  sort = sort_clause[qp->sort];

  if (idx && qp->filter)
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album, f.album_sort, COUNT(f.id), 1, f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songalbumid = g.persistentid WHERE f.disabled = 0 AND %s GROUP BY f.songalbumid %s %s;", qp->filter, sort, idx);
  else if (idx)
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album, f.album_sort, COUNT(f.id), 1, f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songalbumid = g.persistentid WHERE f.disabled = 0 GROUP BY f.songalbumid %s %s;", sort, idx);
  else if (qp->filter)
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album, f.album_sort, COUNT(f.id), 1, f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songalbumid = g.persistentid WHERE f.disabled = 0 AND %s GROUP BY f.songalbumid %s;", qp->filter, sort);
  else
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album, f.album_sort, COUNT(f.id), 1, f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songalbumid = g.persistentid WHERE f.disabled = 0 GROUP BY f.songalbumid %s;", sort);

  if (idx)
    sqlite3_free(idx);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_group_artists(struct query_params *qp, char **q)
{
  char *query;
  char *idx;
  const char *sort;
  int ret;

  qp->results = db_get_count("SELECT COUNT(DISTINCT f.songartistid) FROM files f WHERE f.disabled = 0;");
  if (qp->results < 0)
    return -1;

  /* Get index clause */
  ret = db_build_query_index_clause(&idx, qp);
  if (ret < 0)
    return -1;

  sort = sort_clause[qp->sort];

  if (idx && qp->filter)
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album_artist, f.album_artist_sort, COUNT(f.id), COUNT(DISTINCT f.songalbumid), f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songartistid = g.persistentid WHERE f.disabled = 0 AND %s GROUP BY f.songartistid %s %s;", qp->filter, sort, idx);
  else if (idx)
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album_artist, f.album_artist_sort, COUNT(f.id), COUNT(DISTINCT f.songalbumid), f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songartistid = g.persistentid WHERE f.disabled = 0 GROUP BY f.songartistid %s %s;", sort, idx);
  else if (qp->filter)
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album_artist, f.album_artist_sort, COUNT(f.id), COUNT(DISTINCT f.songalbumid), f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songartistid = g.persistentid WHERE f.disabled = 0 AND %s GROUP BY f.songartistid %s;", qp->filter, sort);
  else
    query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album_artist, f.album_artist_sort, COUNT(f.id), COUNT(DISTINCT f.songalbumid), f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songartistid = g.persistentid WHERE f.disabled = 0 GROUP BY f.songartistid %s;", sort);

  if (idx)
    sqlite3_free(idx);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_group_items(struct query_params *qp, char **q)
{
  char *query;
  char *count;
  enum group_type gt;

  gt = db_group_type_bypersistentid(qp->persistentid);

  switch (gt)
    {
      case G_ALBUMS:
	count = sqlite3_mprintf("SELECT COUNT(*) FROM files f"
				" WHERE f.songalbumid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      case G_ARTISTS:
	count = sqlite3_mprintf("SELECT COUNT(*) FROM files f"
				" WHERE f.songartistid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unsupported group type %d for group id %" PRIi64 "\n", gt, qp->persistentid);
	return -1;
    }

  if (!count)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for count query string\n");

      return -1;
    }

  qp->results = db_get_count(count);
  sqlite3_free(count);

  if (qp->results < 0)
    return -1;

  switch (gt)
    {
      case G_ALBUMS:
	query = sqlite3_mprintf("SELECT f.* FROM files f"
				" WHERE f.songalbumid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      case G_ARTISTS:
	query = sqlite3_mprintf("SELECT f.* FROM files f"
				" WHERE f.songartistid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      default:
	return -1;
    }

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_group_dirs(struct query_params *qp, char **q)
{
  char *query;
  char *count;
  enum group_type gt;

  gt = db_group_type_bypersistentid(qp->persistentid);

  switch (gt)
    {
      case G_ALBUMS:
	count = sqlite3_mprintf("SELECT COUNT(DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1)))"
				" FROM files f"
				" WHERE f.songalbumid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      case G_ARTISTS:
	count = sqlite3_mprintf("SELECT COUNT(DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1)))"
				" FROM files f"
				" WHERE f.songartistid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unsupported group type %d for group id %" PRIi64 "\n", gt, qp->persistentid);
	return -1;
    }

  if (!count)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for count query string\n");

      return -1;
    }

  qp->results = db_get_count(count);
  sqlite3_free(count);

  if (qp->results < 0)
    return -1;

  switch (gt)
    {
      case G_ALBUMS:
	query = sqlite3_mprintf("SELECT DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1))"
				" FROM files f"
				" WHERE f.songalbumid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      case G_ARTISTS:
	query = sqlite3_mprintf("SELECT DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1))"
				" FROM files f"
				" WHERE f.songartistid = %" PRIi64 " AND f.disabled = 0;", qp->persistentid);
	break;

      default:
	return -1;
    }

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_browse(struct query_params *qp, const char *field, const char *group_field, char **q)
{
  char *query;
  char *count;
  char *idx;
  const char *sort;
  int ret;

  if (qp->filter)
    count = sqlite3_mprintf("SELECT COUNT(DISTINCT f.%s) FROM files f WHERE f.disabled = 0 AND f.%s != '' AND %s;",
			    field, field, qp->filter);
  else
    count = sqlite3_mprintf("SELECT COUNT(DISTINCT f.%s) FROM files f WHERE f.disabled = 0 AND f.%s != '';",
			    field, field);

  if (!count)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for count query string\n");

      return -1;
    }

  qp->results = db_get_count(count);
  sqlite3_free(count);

  if (qp->results < 0)
    return -1;

  /* Get index clause */
  ret = db_build_query_index_clause(&idx, qp);
  if (ret < 0)
    return -1;

  sort = sort_clause[qp->sort];

  if (idx && qp->filter)
    query = sqlite3_mprintf("SELECT f.%s, f.%s FROM files f WHERE f.disabled = 0 AND f.%s != ''"
                            " AND %s GROUP BY f.%s %s %s;", field, group_field, field, qp->filter, group_field, sort, idx);
  else if (idx)
    query = sqlite3_mprintf("SELECT f.%s, f.%s FROM files f WHERE f.disabled = 0 AND f.%s != ''"
                            " GROUP BY f.%s %s %s;", field, group_field, field, group_field, sort, idx);
  else if (qp->filter)
    query = sqlite3_mprintf("SELECT f.%s, f.%s FROM files f WHERE f.disabled = 0 AND f.%s != ''"
                            " AND %s GROUP BY f.%s %s;", field, group_field, field, qp->filter, group_field, sort);
  else
    query = sqlite3_mprintf("SELECT f.%s, f.%s FROM files f WHERE f.disabled = 0 AND f.%s != ''"
                            " GROUP BY f.%s %s", field, group_field, field, group_field, sort);

  if (idx)
    sqlite3_free(idx);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

static int
db_build_query_count_items(struct query_params *qp, char **q)
{
  char *query;

  qp->results = 1;

  if (qp->filter)
    query = sqlite3_mprintf("SELECT COUNT(*), SUM(song_length) FROM files f WHERE f.disabled = 0 AND %s;", qp->filter);
  else
    query = sqlite3_mprintf("SELECT COUNT(*), SUM(song_length) FROM files f WHERE f.disabled = 0;");

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  *q = query;

  return 0;
}

int
db_query_start(struct query_params *qp)
{
  char *query;
  int ret;

  qp->stmt = NULL;

  switch (qp->type)
    {
      case Q_ITEMS:
	ret = db_build_query_items(qp, &query);
	break;

      case Q_PL:
	ret = db_build_query_pls(qp, &query);
	break;

      case Q_PLITEMS:
	ret = db_build_query_plitems(qp, &query);
	break;

      case Q_GROUP_ALBUMS:
	ret = db_build_query_group_albums(qp, &query);
	break;

      case Q_GROUP_ARTISTS:
	ret = db_build_query_group_artists(qp, &query);
	break;

      case Q_GROUP_ITEMS:
	ret = db_build_query_group_items(qp, &query);
	break;

      case Q_GROUP_DIRS:
	ret = db_build_query_group_dirs(qp, &query);
	break;

      case Q_BROWSE_ALBUMS:
	ret = db_build_query_browse(qp, "album", "album_sort", &query);
	break;

      case Q_BROWSE_ARTISTS:
	ret = db_build_query_browse(qp, "album_artist", "album_artist_sort", &query);
	break;

      case Q_BROWSE_GENRES:
	ret = db_build_query_browse(qp, "genre", "genre", &query);
	break;

      case Q_BROWSE_COMPOSERS:
	ret = db_build_query_browse(qp, "composer", "composer_sort", &query);
	break;

      case Q_BROWSE_YEARS:
	ret = db_build_query_browse(qp, "year", "year", &query);
	break;

      case Q_BROWSE_DISCS:
	ret = db_build_query_browse(qp, "disc", "disc", &query);
	break;

      case Q_BROWSE_TRACKS:
	ret = db_build_query_browse(qp, "track", "track", &query);
	break;

      case Q_BROWSE_VPATH:
	ret = db_build_query_browse(qp, "virtual_path", "virtual_path", &query);
	break;

      case Q_BROWSE_PATH:
	ret = db_build_query_browse(qp, "path", "path", &query);
	break;

      case Q_COUNT_ITEMS:
	ret = db_build_query_count_items(qp, &query);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unknown query type\n");
	return -1;
    }

  if (ret < 0)
    return -1;

  DPRINTF(E_DBG, L_DB, "Starting query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &qp->stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  return 0;
}

void
db_query_end(struct query_params *qp)
{
  if (!qp->stmt)
    return;

  qp->results = -1;

  sqlite3_finalize(qp->stmt);
  qp->stmt = NULL;
}

static int
db_query_run(char *query, int free, int cache_update)
{
  char *errmsg;
  int ret;

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  /* If the query will be long running we don't want the cache to start regenerating */
  cache_daap_suspend();

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    DPRINTF(E_LOG, L_DB, "Error '%s' while runnning '%s'\n", errmsg, query);

  sqlite3_free(errmsg);

  if (free)
    sqlite3_free(query);

  if (cache_update)
    cache_daap_trigger();
  else
    cache_daap_resume();

  return ((ret != SQLITE_OK) ? -1 : 0);
}

int
db_query_fetch_file(struct query_params *qp, struct db_media_file_info *dbmfi)
{
  int ncols;
  char **strcol;
  int i;
  int ret;

  memset(dbmfi, 0, sizeof(struct db_media_file_info));

  if (!qp->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Query not started!\n");
      return -1;
    }

  if ((qp->type != Q_ITEMS) && (qp->type != Q_PLITEMS) && (qp->type != Q_GROUP_ITEMS))
    {
      DPRINTF(E_LOG, L_DB, "Not an items, playlist or group items query!\n");
      return -1;
    }

  ret = db_blocking_step(qp->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      dbmfi->id = NULL;
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));	
      return -1;
    }

  ncols = sqlite3_column_count(qp->stmt);

  if (sizeof(dbmfi_cols_map) / sizeof(dbmfi_cols_map[0]) != ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: dbmfi column map out of sync with schema\n");
      return -1;
    }

  for (i = 0; i < ncols; i++)
    {
      strcol = (char **) ((char *)dbmfi + dbmfi_cols_map[i]);

      *strcol = (char *)sqlite3_column_text(qp->stmt, i);
    }

  return 0;
}

int
db_query_fetch_pl(struct query_params *qp, struct db_playlist_info *dbpli, int with_itemcount)
{
  int ncols;
  char **strcol;
  int id;
  int type;
  int nitems;
  int nstreams;
  int i;
  int ret;

  memset(dbpli, 0, sizeof(struct db_playlist_info));

  if (!qp->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Query not started!\n");
      return -1;
    }

  if (qp->type != Q_PL)
    {
      DPRINTF(E_LOG, L_DB, "Not a playlist query!\n");
      return -1;
    }

  ret = db_blocking_step(qp->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      dbpli->id = NULL;
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));	
      return -1;
    }

  ncols = sqlite3_column_count(qp->stmt);

  if (sizeof(dbpli_cols_map) / sizeof(dbpli_cols_map[0]) != ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: dbpli column map out of sync with schema\n");
      return -1;
    }

  for (i = 0; i < ncols; i++)
    {
      strcol = (char **) ((char *)dbpli + dbpli_cols_map[i]);

      *strcol = (char *)sqlite3_column_text(qp->stmt, i);
    }

  if (with_itemcount)
    {
      type = sqlite3_column_int(qp->stmt, 2);

      switch (type)
	{
	  case PL_PLAIN:
	  case PL_FOLDER:
	    id = sqlite3_column_int(qp->stmt, 0);
	    nitems = db_pl_count_items(id, 0);
	    nstreams = db_pl_count_items(id, 1);
	    break;

	  case PL_SPECIAL:
	  case PL_SMART:
	    nitems = db_smartpl_count_items(dbpli->query);
	    nstreams = 0;
	    break;

	  default:
	    DPRINTF(E_LOG, L_DB, "Unknown playlist type %d while fetching playlist\n", type);
	    return -1;
	}

      dbpli->items = qp->buf1;
      ret = snprintf(qp->buf1, sizeof(qp->buf1), "%d", nitems);
      if ((ret < 0) || (ret >= sizeof(qp->buf1)))
	{
	  DPRINTF(E_LOG, L_DB, "Could not convert item count, buffer too small\n");

	  strcpy(qp->buf1, "0");
	}
      dbpli->streams = qp->buf2;
      ret = snprintf(qp->buf2, sizeof(qp->buf2), "%d", nstreams);
      if ((ret < 0) || (ret >= sizeof(qp->buf2)))
	{
	  DPRINTF(E_LOG, L_DB, "Could not convert stream count, buffer too small\n");

	  strcpy(qp->buf2, "0");
	}
    }

  return 0;
}

int
db_query_fetch_group(struct query_params *qp, struct db_group_info *dbgri)
{
  int ncols;
  char **strcol;
  int i;
  int ret;

  memset(dbgri, 0, sizeof(struct db_group_info));

  if (!qp->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Query not started!\n");
      return -1;
    }

  if ((qp->type != Q_GROUP_ALBUMS) && (qp->type != Q_GROUP_ARTISTS))
    {
      DPRINTF(E_LOG, L_DB, "Not a groups query!\n");
      return -1;
    }

  ret = db_blocking_step(qp->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      return 1;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  ncols = sqlite3_column_count(qp->stmt);

  if (sizeof(dbgri_cols_map) / sizeof(dbgri_cols_map[0]) != ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: dbgri column map out of sync with schema\n");
      return -1;
    }

  for (i = 0; i < ncols; i++)
    {
      strcol = (char **) ((char *)dbgri + dbgri_cols_map[i]);

      *strcol = (char *)sqlite3_column_text(qp->stmt, i);
    }

  return 0;
}

int
db_query_fetch_count(struct query_params *qp, struct filecount_info *fci)
{
  int ret;

  memset(fci, 0, sizeof(struct filecount_info));

  if (!qp->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Query not started!\n");
      return -1;
    }

  if (qp->type != Q_COUNT_ITEMS)
    {
      DPRINTF(E_LOG, L_DB, "Not a count query!\n");
      return -1;
    }

  ret = db_blocking_step(qp->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results for count query\n");
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  fci->count = sqlite3_column_int(qp->stmt, 0);
  fci->length = sqlite3_column_int(qp->stmt, 1);

  return 0;
}

int
db_query_fetch_string(struct query_params *qp, char **string)
{
  int ret;

  *string = NULL;

  if (!qp->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Query not started!\n");
      return -1;
    }

  if (!(qp->type & Q_F_BROWSE))
    {
      DPRINTF(E_LOG, L_DB, "Not a browse query!\n");
      return -1;
    }

  ret = db_blocking_step(qp->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      *string = NULL;
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));	
      return -1;
    }

  *string = (char *)sqlite3_column_text(qp->stmt, 0);

  return 0;
}

int
db_query_fetch_string_sort(struct query_params *qp, char **string, char **sortstring)
{
  int ret;

  *string = NULL;

  if (!qp->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Query not started!\n");
      return -1;
    }

  if (!(qp->type & Q_F_BROWSE))
    {
      DPRINTF(E_LOG, L_DB, "Not a browse query!\n");
      return -1;
    }

  ret = db_blocking_step(qp->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      *string = NULL;
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  *string = (char *)sqlite3_column_text(qp->stmt, 0);
  *sortstring = (char *)sqlite3_column_text(qp->stmt, 1);

  return 0;
}


/* Files */
int
db_files_get_count(void)
{
  return db_get_count("SELECT COUNT(*) FROM files f WHERE f.disabled = 0;");
}

int
db_files_get_artist_count(void)
{
  return db_get_count("SELECT COUNT(DISTINCT songartistid) FROM files f WHERE f.disabled = 0;");
}

int
db_files_get_album_count(void)
{
  return db_get_count("SELECT COUNT(DISTINCT songalbumid) FROM files f WHERE f.disabled = 0;");
}

int
db_files_get_count_bymatch(char *path)
{
#define Q_TMPL "SELECT COUNT(*) FROM files f WHERE f.path LIKE '%%%q';"
  char *query;
  int count;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory making count query string.\n");

      return -1;
    }

  count = db_get_count(query);
  sqlite3_free(query);

  return count;
#undef Q_TMPL
}

void
db_files_update_songartistid(void)
{
  db_query_run("UPDATE files SET songartistid = daap_songalbumid(LOWER(album_artist), '');", 0, 1);
}

void
db_files_update_songalbumid(void)
{
  db_query_run("UPDATE files SET songalbumid = daap_songalbumid(LOWER(album_artist), LOWER(album));", 0, 1);
}

void
db_file_inc_playcount(int id)
{
#define Q_TMPL "UPDATE files SET play_count = play_count + 1, time_played = %" PRIi64 ", seek = 0 WHERE id = %d;"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)time(NULL), id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return;
    }

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_file_ping(int id)
{
#define Q_TMPL "UPDATE files SET db_timestamp = %" PRIi64 ", disabled = 0 WHERE id = %d;"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)time(NULL), id);

  db_query_run(query, 1, 1);
#undef Q_TMPL
}

void
db_file_ping_bymatch(char *path, int isdir)
{
#define Q_TMPL_DIR "UPDATE files SET db_timestamp = %" PRIi64 " WHERE path LIKE '%q/%%';"
#define Q_TMPL_NODIR "UPDATE files SET db_timestamp = %" PRIi64 " WHERE path LIKE '%q%%';"
  char *query;

  if (isdir)
    query = sqlite3_mprintf(Q_TMPL_DIR, (int64_t)time(NULL), path);
  else
    query = sqlite3_mprintf(Q_TMPL_NODIR, (int64_t)time(NULL), path);

  db_query_run(query, 1, 1);
#undef Q_TMPL_DIR
#undef Q_TMPL_NODIR
}

char *
db_file_path_byid(int id)
{
#define Q_TMPL "SELECT f.path FROM files f WHERE f.id = %d;"
  char *query;
  sqlite3_stmt *stmt;
  char *res;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, strlen(query) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return NULL;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return NULL;
    }

  res = (char *)sqlite3_column_text(stmt, 0);
  if (res)
    res = strdup(res);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return res;

#undef Q_TMPL
}

static int
db_file_id_byquery(char *query)
{
  sqlite3_stmt *stmt;
  int ret;

  if (!query)
    return 0;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, strlen(query) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      return 0;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      return 0;
    }

  ret = sqlite3_column_int(stmt, 0);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);

  return ret;
}

int
db_file_id_bypath(char *path)
{
#define Q_TMPL "SELECT f.id FROM files f WHERE f.path = '%q';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_file_id_byquery(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

int
db_file_id_bymatch(char *path)
{
#define Q_TMPL "SELECT f.id FROM files f WHERE f.path LIKE '%%%q';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_file_id_byquery(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

//TODO [cleanup] unused function(?)
int
db_file_id_byfilebase(char *filename, char *base)
{
#define Q_TMPL "SELECT f.id FROM files f WHERE f.path LIKE '%q/%%/%q';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, base, filename);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_file_id_byquery(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

int
db_file_id_byfile(char *filename)
{
#define Q_TMPL "SELECT f.id FROM files f WHERE f.fname = '%q';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, filename);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_file_id_byquery(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

int
db_file_id_byurl(char *url)
{
#define Q_TMPL "SELECT f.id FROM files f WHERE f.url = '%q';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, url);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_file_id_byquery(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

int
db_file_id_by_virtualpath_match(char *path)
{
#define Q_TMPL "SELECT f.id FROM files f WHERE f.virtual_path LIKE '%%%q%%';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_file_id_byquery(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

void
db_file_stamp_bypath(char *path, time_t *stamp, int *id)
{
#define Q_TMPL "SELECT f.id, f.db_timestamp FROM files f WHERE f.path = '%q';"
  char *query;
  sqlite3_stmt *stmt;
  int ret;

  *stamp = 0;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, strlen(query) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return;
    }

  *id = sqlite3_column_int(stmt, 0);
  *stamp = (time_t)sqlite3_column_int64(stmt, 1);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

#undef Q_TMPL
}

static struct media_file_info *
db_file_fetch_byquery(char *query)
{
  struct media_file_info *mfi;
  sqlite3_stmt *stmt;
  int ncols;
  char *cval;
  uint32_t *ival;
  uint64_t *i64val;
  char **strval;
  uint64_t disabled;
  int i;
  int ret;

  if (!query)
    return NULL;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  mfi = (struct media_file_info *)malloc(sizeof(struct media_file_info));
  if (!mfi)
    {
      DPRINTF(E_LOG, L_DB, "Could not allocate struct media_file_info, out of memory\n");
      return NULL;
    }
  memset(mfi, 0, sizeof(struct media_file_info));

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      free(mfi);
      return NULL;
    }

  ret = db_blocking_step(stmt);

  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      free(mfi);
      return NULL;
    }

  ncols = sqlite3_column_count(stmt);

  if (sizeof(mfi_cols_map) / sizeof(mfi_cols_map[0]) != ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: mfi column map out of sync with schema\n");

      sqlite3_finalize(stmt);
      free(mfi);
      return NULL;
    }

  for (i = 0; i < ncols; i++)
    {
      switch (mfi_cols_map[i].type)
	{
	  case DB_TYPE_CHAR:
	    cval = (char *)mfi + mfi_cols_map[i].offset;

	    *cval = sqlite3_column_int(stmt, i);
	    break;

	  case DB_TYPE_INT:
	    ival = (uint32_t *) ((char *)mfi + mfi_cols_map[i].offset);

	    if (mfi_cols_map[i].offset == mfi_offsetof(disabled))
	      {
		disabled = sqlite3_column_int64(stmt, i);
		*ival = (disabled != 0);
	      }
	    else
	      *ival = sqlite3_column_int(stmt, i);
	    break;

	  case DB_TYPE_INT64:
	    i64val = (uint64_t *) ((char *)mfi + mfi_cols_map[i].offset);

	    *i64val = sqlite3_column_int64(stmt, i);
	    break;

	  case DB_TYPE_STRING:
	    strval = (char **) ((char *)mfi + mfi_cols_map[i].offset);

	    cval = (char *)sqlite3_column_text(stmt, i);
	    if (cval)
	      *strval = strdup(cval);
	    break;

	  default:
	    DPRINTF(E_LOG, L_DB, "BUG: Unknown type %d in mfi column map\n", mfi_cols_map[i].type);

	    free_mfi(mfi, 0);
	    sqlite3_finalize(stmt);
	    return NULL;
	}
    }

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);

  return mfi;
}

struct media_file_info *
db_file_fetch_byid(int id)
{
#define Q_TMPL "SELECT f.* FROM files f WHERE f.id = %d;"
  struct media_file_info *mfi;
  char *query;

  query = sqlite3_mprintf(Q_TMPL, id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  mfi = db_file_fetch_byquery(query);

  sqlite3_free(query);

  return mfi;

#undef Q_TMPL
}

struct media_file_info *
db_file_fetch_byvirtualpath(char *virtual_path)
{
#define Q_TMPL "SELECT f.* FROM files f WHERE f.virtual_path = %Q;"
  struct media_file_info *mfi;
  char *query;

  query = sqlite3_mprintf(Q_TMPL, virtual_path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  mfi = db_file_fetch_byquery(query);

  sqlite3_free(query);

  return mfi;

#undef Q_TMPL
}

int
db_file_add(struct media_file_info *mfi)
{
#define Q_TMPL "INSERT INTO files (id, path, fname, title, artist, album, genre, comment, type, composer," \
               " orchestra, conductor, grouping, url, bitrate, samplerate, song_length, file_size, year, track," \
               " total_tracks, disc, total_discs, bpm, compilation, artwork, rating, play_count, seek, data_kind, item_kind," \
               " description, time_added, time_modified, time_played, db_timestamp, disabled, sample_count," \
               " codectype, idx, has_video, contentrating, bits_per_sample, album_artist," \
               " media_kind, tv_series_name, tv_episode_num_str, tv_network_name, tv_episode_sort, tv_season_num, " \
               " songartistid, songalbumid, " \
               " title_sort, artist_sort, album_sort, composer_sort, album_artist_sort, virtual_path," \
               " directory_id, date_released) " \
               " VALUES (NULL, '%q', '%q', TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), %Q, TRIM(%Q)," \
               " TRIM(%Q), TRIM(%Q), TRIM(%Q), %Q, %d, %d, %d, %" PRIi64 ", %d, %d," \
               " %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d," \
               " %Q, %" PRIi64 ", %" PRIi64 ", %" PRIi64 ", %" PRIi64 ", %d, %" PRIi64 "," \
               " %Q, %d, %d, %d, %d, TRIM(%Q)," \
               " %d, TRIM(%Q), TRIM(%Q), TRIM(%Q), %d, %d," \
               " daap_songalbumid(LOWER(TRIM(%Q)), ''), daap_songalbumid(LOWER(TRIM(%Q)), LOWER(TRIM(%Q))), " \
               " TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), %d, %d);"

  char *query;
  char *errmsg;
  int ret;


  if (mfi->id != 0)
    {
      DPRINTF(E_WARN, L_DB, "Trying to add file with non-zero id; use db_file_update()?\n");
      return -1;
    }

  mfi->db_timestamp = (uint64_t)time(NULL);

  if (mfi->time_added == 0)
    mfi->time_added = mfi->db_timestamp;

  if (mfi->time_modified == 0)
    mfi->time_modified = mfi->db_timestamp;

  query = sqlite3_mprintf(Q_TMPL,
			  STR(mfi->path), STR(mfi->fname), mfi->title, mfi->artist, mfi->album,
			  mfi->genre, mfi->comment, mfi->type, mfi->composer,
			  mfi->orchestra, mfi->conductor, mfi->grouping, mfi->url, mfi->bitrate,
			  mfi->samplerate, mfi->song_length, mfi->file_size, mfi->year, mfi->track,
			  mfi->total_tracks, mfi->disc, mfi->total_discs, mfi->bpm, mfi->compilation, mfi->artwork,
			  mfi->rating, mfi->play_count, mfi->seek, mfi->data_kind, mfi->item_kind,
			  mfi->description, (int64_t)mfi->time_added, (int64_t)mfi->time_modified,
			  (int64_t)mfi->time_played, (int64_t)mfi->db_timestamp, mfi->disabled, mfi->sample_count,
			  mfi->codectype, mfi->index, mfi->has_video,
			  mfi->contentrating, mfi->bits_per_sample, mfi->album_artist,
			  mfi->media_kind, mfi->tv_series_name, mfi->tv_episode_num_str,
			  mfi->tv_network_name, mfi->tv_episode_sort, mfi->tv_season_num,
			  mfi->album_artist, mfi->album_artist, mfi->album, mfi->title_sort, mfi->artist_sort, mfi->album_sort,
			  mfi->composer_sort, mfi->album_artist_sort, mfi->virtual_path, mfi->directory_id, mfi->date_released);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  cache_daap_trigger();

  return 0;

#undef Q_TMPL
}

int
db_file_update(struct media_file_info *mfi)
{
#define Q_TMPL "UPDATE files SET path = '%q', fname = '%q', title = TRIM(%Q), artist = TRIM(%Q), album = TRIM(%Q), genre = TRIM(%Q)," \
	       " comment = TRIM(%Q), type = %Q, composer = TRIM(%Q), orchestra = TRIM(%Q), conductor = TRIM(%Q), grouping = TRIM(%Q)," \
	       " url = %Q, bitrate = %d, samplerate = %d, song_length = %d, file_size = %" PRIi64 "," \
	       " year = %d, track = %d, total_tracks = %d, disc = %d, total_discs = %d, bpm = %d," \
	       " compilation = %d, artwork = %d, rating = %d, seek = %d, data_kind = %d, item_kind = %d," \
	       " description = %Q, time_modified = %" PRIi64 "," \
	       " db_timestamp = %" PRIi64 ", disabled = %" PRIi64 ", sample_count = %" PRIi64 "," \
	       " codectype = %Q, idx = %d, has_video = %d," \
	       " bits_per_sample = %d, album_artist = TRIM(%Q)," \
	       " media_kind = %d, tv_series_name = TRIM(%Q), tv_episode_num_str = TRIM(%Q)," \
	       " tv_network_name = TRIM(%Q), tv_episode_sort = %d, tv_season_num = %d," \
	       " songartistid = daap_songalbumid(LOWER(TRIM(%Q)), ''), songalbumid = daap_songalbumid(LOWER(TRIM(%Q)), LOWER(TRIM(%Q)))," \
	       " title_sort = TRIM(%Q), artist_sort = TRIM(%Q), album_sort = TRIM(%Q), composer_sort = TRIM(%Q), album_artist_sort = TRIM(%Q)," \
	       " virtual_path = TRIM(%Q), directory_id = %d, date_released = %d" \
	       " WHERE id = %d;"

  char *query;
  char *errmsg;
  int ret;

  if (mfi->id == 0)
    {
      DPRINTF(E_WARN, L_DB, "Trying to update file with id 0; use db_file_add()?\n");
      return -1;
    }

  mfi->db_timestamp = (uint64_t)time(NULL);

  if (mfi->time_modified == 0)
    mfi->time_modified = mfi->db_timestamp;

  query = sqlite3_mprintf(Q_TMPL,
			  STR(mfi->path), STR(mfi->fname), mfi->title, mfi->artist, mfi->album, mfi->genre,
			  mfi->comment, mfi->type, mfi->composer, mfi->orchestra, mfi->conductor, mfi->grouping,
			  mfi->url, mfi->bitrate, mfi->samplerate, mfi->song_length, mfi->file_size,
			  mfi->year, mfi->track, mfi->total_tracks, mfi->disc, mfi->total_discs, mfi->bpm,
			  mfi->compilation, mfi->artwork, mfi->rating, mfi->seek, mfi->data_kind, mfi->item_kind,
			  mfi->description, (int64_t)mfi->time_modified,
			  (int64_t)mfi->db_timestamp, (int64_t)mfi->disabled, mfi->sample_count,
			  mfi->codectype, mfi->index, mfi->has_video,
			  mfi->bits_per_sample, mfi->album_artist,
			  mfi->media_kind, mfi->tv_series_name, mfi->tv_episode_num_str,
			  mfi->tv_network_name, mfi->tv_episode_sort, mfi->tv_season_num,
			  mfi->album_artist, mfi->album_artist, mfi->album,
			  mfi->title_sort, mfi->artist_sort, mfi->album_sort,
			  mfi->composer_sort, mfi->album_artist_sort,
			  mfi->virtual_path, mfi->directory_id, mfi->date_released,
			  mfi->id);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  cache_daap_trigger();

  return 0;

#undef Q_TMPL
}

void
db_file_update_icy(int id, char *artist, char *album)
{
#define Q_TMPL "UPDATE files SET artist = TRIM(%Q), album = TRIM(%Q) WHERE id = %d;"
  char *query;

  if (id == 0)
    return;

  query = sqlite3_mprintf(Q_TMPL, artist, album, id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return;
    }

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_file_save_seek(int id, uint32_t seek)
{
#define Q_TMPL "UPDATE files SET seek = %d WHERE id = %d;"
  char *query;

  if (id == 0)
    return;

  query = sqlite3_mprintf(Q_TMPL, seek, id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return;
    }

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_file_delete_bypath(char *path)
{
#define Q_TMPL "DELETE FROM files WHERE path = '%q';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, path);

  db_query_run(query, 1, 1);
#undef Q_TMPL
}

void
db_file_disable_bypath(char *path, char *strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE files SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path = '%q';"
  char *query;
  int64_t disabled;
  int striplen;
  int striplenvpath;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;
  striplen = strlen(strip) + 1;
  if (strlen(strip) > 0)
    striplenvpath = strlen(strip) + strlen("/file:/");
  else
    striplenvpath = 0;

  query = sqlite3_mprintf(Q_TMPL, striplen, striplenvpath, disabled, path);

  db_query_run(query, 1, 1);
#undef Q_TMPL
}

void
db_file_disable_bymatch(char *path, char *strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE files SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path LIKE '%q/%%';"
  char *query;
  int64_t disabled;
  int striplen;
  int striplenvpath;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;
  striplen = strlen(strip) + 1;
  if (strlen(strip) > 0)
    striplenvpath = strlen(strip) + strlen("/file:/");
  else
    striplenvpath = 0;

  query = sqlite3_mprintf(Q_TMPL, striplen, striplenvpath, disabled, path);

  db_query_run(query, 1, 1);
#undef Q_TMPL
}

int
db_file_enable_bycookie(uint32_t cookie, char *path)
{
#define Q_TMPL "UPDATE files SET path = '%q' || path, virtual_path = '/file:%q' || virtual_path, disabled = 0 WHERE disabled = %" PRIi64 ";"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path, path, (int64_t)cookie);

  ret = db_query_run(query, 1, 1);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}

int
db_file_update_directoryid(char *path, int dir_id)
{
#define Q_TMPL "UPDATE files SET directory_id = %d WHERE path = %Q;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, dir_id, path);

  ret = db_query_run(query, 1, 0);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}


/* Playlists */
int
db_pl_get_count(void)
{
  return db_get_count("SELECT COUNT(*) FROM playlists p WHERE p.disabled = 0;");
}

static int
db_pl_count_items(int id, int streams_only)
{
#define Q_TMPL "SELECT COUNT(*) FROM playlistitems pi JOIN files f" \
               " ON pi.filepath = f.path WHERE f.disabled = 0 AND pi.playlistid = %d;"
#define Q_TMPL_STREAMS "SELECT COUNT(*) FROM playlistitems pi JOIN files f" \
               " ON pi.filepath = f.path WHERE f.disabled = 0 AND f.data_kind = 1 AND pi.playlistid = %d;"
  char *query;
  int ret;

  if (!streams_only)
    query = sqlite3_mprintf(Q_TMPL, id);
  else
    query = sqlite3_mprintf(Q_TMPL_STREAMS, id);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return 0;
    }

  ret = db_get_count(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL_STREAMS
#undef Q_TMPL
}

static int
db_smartpl_count_items(const char *smartpl_query)
{
#define Q_TMPL "SELECT COUNT(*) FROM files f WHERE f.disabled = 0 AND %s;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, smartpl_query);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return 0;
    }

  ret = db_get_count(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

void
db_pl_ping(int id)
{
#define Q_TMPL "UPDATE playlists SET db_timestamp = %" PRIi64 ", disabled = 0 WHERE id = %d;"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)time(NULL), id);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_pl_ping_bymatch(char *path, int isdir)
{
#define Q_TMPL_DIR "UPDATE playlists SET db_timestamp = %" PRIi64 " WHERE path LIKE '%q/%%';"
#define Q_TMPL_NODIR "UPDATE playlists SET db_timestamp = %" PRIi64 " WHERE path LIKE '%q%%';"
  char *query;

  if (isdir)
    query = sqlite3_mprintf(Q_TMPL_DIR, (int64_t)time(NULL), path);
  else
    query = sqlite3_mprintf(Q_TMPL_NODIR, (int64_t)time(NULL), path);

  db_query_run(query, 1, 0);
#undef Q_TMPL_DIR
#undef Q_TMPL_NODIR
}

static int
db_pl_id_bypath(char *path, int *id)
{
#define Q_TMPL "SELECT p.id FROM playlists p WHERE p.path = '%q';"
  char *query;
  sqlite3_stmt *stmt;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));	

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return -1;
    }

  *id = sqlite3_column_int(stmt, 0);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return 0;

#undef Q_TMPL
}

static struct playlist_info *
db_pl_fetch_byquery(char *query)
{
  struct playlist_info *pli;
  sqlite3_stmt *stmt;
  int ncols;
  char *cval;
  uint32_t *ival;
  char **strval;
  uint64_t disabled;
  int i;
  int ret;

  if (!query)
    return NULL;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  pli = (struct playlist_info *)malloc(sizeof(struct playlist_info));
  if (!pli)
    {
      DPRINTF(E_LOG, L_DB, "Could not allocate struct playlist_info, out of memory\n");
      return NULL;
    }
  memset(pli, 0, sizeof(struct playlist_info));

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      free(pli);
      return NULL;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      free(pli);
      return NULL;
    }

  ncols = sqlite3_column_count(stmt);

  if (sizeof(pli_cols_map) / sizeof(pli_cols_map[0]) != ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: pli column map out of sync with schema\n");

      sqlite3_finalize(stmt);
      free(pli);
      return NULL;
    }

  for (i = 0; i < ncols; i++)
    {
      switch (pli_cols_map[i].type)
	{
	  case DB_TYPE_INT:
	    ival = (uint32_t *) ((char *)pli + pli_cols_map[i].offset);

	    if (pli_cols_map[i].offset == pli_offsetof(disabled))
	      {
		disabled = sqlite3_column_int64(stmt, i);
		*ival = (disabled != 0);
	      }
	    else
	      *ival = sqlite3_column_int(stmt, i);
	    break;

	  case DB_TYPE_STRING:
	    strval = (char **) ((char *)pli + pli_cols_map[i].offset);

	    cval = (char *)sqlite3_column_text(stmt, i);
	    if (cval)
	      *strval = strdup(cval);
	    break;

	  default:
	    DPRINTF(E_LOG, L_DB, "BUG: Unknown type %d in pli column map\n", pli_cols_map[i].type);

	    sqlite3_finalize(stmt);
	    free_pli(pli, 0);
	    return NULL;
	}
    }

  ret = db_blocking_step(stmt);
  sqlite3_finalize(stmt);

  if (ret != SQLITE_DONE)
    {
      DPRINTF(E_WARN, L_DB, "Query had more than a single result!\n");

      free_pli(pli, 0);
      return NULL;
    }

  switch (pli->type)
    {
      case PL_PLAIN:
      case PL_FOLDER:
	pli->items = db_pl_count_items(pli->id, 0);
	pli->streams = db_pl_count_items(pli->id, 1);
	break;

      case PL_SPECIAL:
      case PL_SMART:
	pli->items = db_smartpl_count_items(pli->query);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unknown playlist type %d while fetching playlist\n", pli->type);

	free_pli(pli, 0);
	return NULL;
    }

  return pli;
}

struct playlist_info *
db_pl_fetch_bypath(char *path)
{
#define Q_TMPL "SELECT p.* FROM playlists p WHERE p.path = '%q';"
  struct playlist_info *pli;
  char *query;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  pli = db_pl_fetch_byquery(query);

  sqlite3_free(query);

  return pli;

#undef Q_TMPL
}

struct playlist_info *
db_pl_fetch_byvirtualpath(char *virtual_path)
{
#define Q_TMPL "SELECT p.* FROM playlists p WHERE p.virtual_path = '%q';"
  struct playlist_info *pli;
  char *query;

  query = sqlite3_mprintf(Q_TMPL, virtual_path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  pli = db_pl_fetch_byquery(query);

  sqlite3_free(query);

  return pli;

#undef Q_TMPL
}

struct playlist_info *
db_pl_fetch_byid(int id)
{
#define Q_TMPL "SELECT p.* FROM playlists p WHERE p.id = %d;"
  struct playlist_info *pli;
  char *query;

  query = sqlite3_mprintf(Q_TMPL, id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  pli = db_pl_fetch_byquery(query);

  sqlite3_free(query);

  return pli;

#undef Q_TMPL
}

struct playlist_info *
db_pl_fetch_bytitlepath(char *title, char *path)
{
#define Q_TMPL "SELECT p.* FROM playlists p WHERE p.title = '%q' AND p.path = '%q';"
  struct playlist_info *pli;
  char *query;

  query = sqlite3_mprintf(Q_TMPL, title, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  pli = db_pl_fetch_byquery(query);

  sqlite3_free(query);

  return pli;

#undef Q_TMPL
}

int
db_pl_add(struct playlist_info *pli, int *id)
{
#define QDUP_TMPL "SELECT COUNT(*) FROM playlists p WHERE p.title = TRIM(%Q) AND p.path = '%q';"
#define QADD_TMPL "INSERT INTO playlists (title, type, query, db_timestamp, disabled, path, idx, special_id, parent_id, virtual_path, directory_id)" \
                  " VALUES (TRIM(%Q), %d, '%q', %" PRIi64 ", %d, '%q', %d, %d, %d, '%q', %d);"
  char *query;
  char *errmsg;
  int ret;

  /* Check duplicates */
  query = sqlite3_mprintf(QDUP_TMPL, pli->title, STR(pli->path));
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  ret = db_get_count(query);

  sqlite3_free(query);

  if (ret > 0)
    {
      DPRINTF(E_WARN, L_DB, "Duplicate playlist with title '%s' path '%s'\n", pli->title, pli->path);
      return -1;
    }

  /* Add */
  query = sqlite3_mprintf(QADD_TMPL,
			  pli->title, pli->type, pli->query, (int64_t)time(NULL), pli->disabled, STR(pli->path),
			  pli->index, pli->special_id, pli->parent_id, pli->virtual_path, pli->directory_id);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  *id = (int)sqlite3_last_insert_rowid(hdl);
  if (*id == 0)
    {
      DPRINTF(E_LOG, L_DB, "Successful insert but no last_insert_rowid!\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Added playlist %s (path %s) with id %d\n", pli->title, pli->path, *id);

  return 0;

#undef QDUP_TMPL
#undef QADD_TMPL
}

int
db_pl_add_item_bypath(int plid, char *path)
{
#define Q_TMPL "INSERT INTO playlistitems (playlistid, filepath) VALUES (%d, '%q');"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, plid, path);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_pl_add_item_byid(int plid, int fileid)
{
#define Q_TMPL "INSERT INTO playlistitems (playlistid, filepath) VALUES (%d, (SELECT f.path FROM files f WHERE f.id = %d));"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, plid, fileid);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_pl_update(struct playlist_info *pli)
{
#define Q_TMPL "UPDATE playlists SET title = TRIM(%Q), type = %d, query = '%q', db_timestamp = %" PRIi64 ", disabled = %d, " \
               " path = '%q', idx = %d, special_id = %d, parent_id = %d, virtual_path = '%q', directory_id = %d " \
               " WHERE id = %d;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL,
			  pli->title, pli->type, pli->query, (int64_t)time(NULL), pli->disabled, STR(pli->path),
			  pli->index, pli->special_id, pli->parent_id, pli->virtual_path, pli->directory_id, pli->id);

  ret = db_query_run(query, 1, 0);

  return ret;
#undef Q_TMPL
}

void
db_pl_clear_items(int id)
{
#define Q_TMPL "DELETE FROM playlistitems WHERE playlistid = %d;"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, id);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_pl_delete(int id)
{
#define Q_TMPL "DELETE FROM playlists WHERE id = %d;"
  char *query;
  int ret;

  if (id == 1)
    return;

  query = sqlite3_mprintf(Q_TMPL, id);

  ret = db_query_run(query, 1, 0);

  if (ret == 0)
    db_pl_clear_items(id);
#undef Q_TMPL
}

void
db_pl_delete_bypath(char *path)
{
  int id;
  int ret;

  ret = db_pl_id_bypath(path, &id);
  if (ret < 0)
    return;

  db_pl_delete(id);
}

void
db_pl_disable_bypath(char *path, char *strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE playlists SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path = '%q';"
  char *query;
  int64_t disabled;
  int striplen;
  int striplenvpath;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;
  striplen = strlen(strip) + 1;
  if (strlen(strip) > 0)
    striplenvpath = strlen(strip) + strlen("/file:/");
  else
    striplenvpath = 0;

  query = sqlite3_mprintf(Q_TMPL, striplen, striplenvpath, disabled, path);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_pl_disable_bymatch(char *path, char *strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE playlists SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path LIKE '%q/%%';"
  char *query;
  int64_t disabled;
  int striplen;
  int striplenvpath;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;
  striplen = strlen(strip) + 1;
  if (strlen(strip) > 0)
    striplenvpath = strlen(strip) + strlen("/file:/");
  else
    striplenvpath = 0;

  query = sqlite3_mprintf(Q_TMPL, striplen, striplenvpath, disabled, path);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_pl_enable_bycookie(uint32_t cookie, char *path)
{
#define Q_TMPL "UPDATE playlists SET path = '%q' || path, virtual_path = '/file:%q' || virtual_path, disabled = 0 WHERE disabled = %" PRIi64 ";"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path, path, (int64_t)cookie);

  ret = db_query_run(query, 1, 0);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}



/* Groups */
int
db_groups_clear(void)
{
  return db_query_run("DELETE FROM groups;", 0, 1);
}

static enum group_type
db_group_type_bypersistentid(int64_t persistentid)
{
#define Q_TMPL "SELECT g.type FROM groups g WHERE g.persistentid = %" PRIi64 ";"
  char *query;
  sqlite3_stmt *stmt;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, persistentid);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, strlen(query) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return 0;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return 0;
    }

  ret = sqlite3_column_int(stmt, 0);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

int
db_group_persistentid_byid(int id, int64_t *persistentid)
{
#define Q_TMPL "SELECT g.persistentid FROM groups g WHERE g.id = %d;"
  char *query;
  sqlite3_stmt *stmt;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, id);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return -1;
    }

  *persistentid = sqlite3_column_int64(stmt, 0);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
  ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return 0;

#undef Q_TMPL
}


/* Directories */
int
db_directory_id_byvirtualpath(char *virtual_path)
{
#define Q_TMPL "SELECT d.id FROM directories d WHERE d.virtual_path = '%q';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, virtual_path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_file_id_byquery(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

int
db_directory_enum_start(struct directory_enum *de)
{
#define Q_TMPL "SELECT * FROM directories WHERE disabled = 0 AND parent_id = %d ORDER BY virtual_path;"
  char *query;
  int ret;

  de->stmt = NULL;

  query = sqlite3_mprintf(Q_TMPL, de->parent_id);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Starting enum '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &de->stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  return 0;

#undef Q_TMPL
}

int
db_directory_enum_fetch(struct directory_enum *de, struct directory_info *di)
{
  uint64_t disabled;
  int ret;

  memset(di, 0, sizeof(struct directory_info));

  if (!de->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Directory enum not started!\n");
      return -1;
    }

  ret = db_blocking_step(de->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of directory enum results\n");
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  di->id = sqlite3_column_int(de->stmt, 0);
  di->virtual_path = (char *)sqlite3_column_text(de->stmt, 1);
  di->db_timestamp = sqlite3_column_int(de->stmt, 2);
  disabled = sqlite3_column_int64(de->stmt, 3);
  di->disabled = (disabled != 0);
  di->parent_id = sqlite3_column_int(de->stmt, 4);

  return 0;
}

void
db_directory_enum_end(struct directory_enum *de)
{
  if (!de->stmt)
    return;

  sqlite3_finalize(de->stmt);
  de->stmt = NULL;
}

static int
db_directory_add(struct directory_info *di, int *id)
{
#define QADD_TMPL "INSERT INTO directories (virtual_path, db_timestamp, disabled, parent_id)" \
                  " VALUES (TRIM(%Q), %d, %d, %d);"

  char *query;
  char *errmsg;
  int ret;

  query = sqlite3_mprintf(QADD_TMPL, di->virtual_path, di->db_timestamp, di->disabled, di->parent_id);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  *id = (int)sqlite3_last_insert_rowid(hdl);
  if (*id == 0)
    {
      DPRINTF(E_LOG, L_DB, "Successful insert but no last_insert_rowid!\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Added directory %s with id %d\n", di->virtual_path, *id);

  return 0;

#undef QADD_TMPL
}

static int
db_directory_update(struct directory_info *di)
{
#define QADD_TMPL "UPDATE directories SET virtual_path = TRIM(%Q), db_timestamp = %d, disabled = %d, parent_id = %d" \
                  " WHERE id = %d;"
  char *query;
  char *errmsg;
  int ret;

  /* Add */
  query = sqlite3_mprintf(QADD_TMPL, di->virtual_path, di->db_timestamp, di->disabled, di->parent_id, di->id);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_exec(query, &errmsg);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Query error: %s\n", errmsg);

      sqlite3_free(errmsg);
      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  DPRINTF(E_DBG, L_DB, "Updated directory %s with id %d\n", di->virtual_path, di->id);

  return 0;

#undef QADD_TMPL
}

int
db_directory_addorupdate(char *virtual_path, int disabled, int parent_id)
{
  struct directory_info di;
  int id;
  int ret;

  id = db_directory_id_byvirtualpath(virtual_path);

  di.id = id;
  di.parent_id = parent_id;
  di.virtual_path = virtual_path;
  di.disabled = disabled;
  di.db_timestamp = (uint64_t)time(NULL);

  if (di.id == 0)
    ret = db_directory_add(&di, &id);
  else
    ret = db_directory_update(&di);

  if (ret < 0 || id <= 0)
  {
    DPRINTF(E_LOG, L_DB, "Insert or update of directory failed '%s'\n", virtual_path);
    return -1;
  }

  return id;
}

void
db_directory_ping_bymatch(char *path)
{
#define Q_TMPL_DIR "UPDATE directories SET db_timestamp = %" PRIi64 " WHERE virtual_path = '/file:%q' OR virtual_path LIKE '/file:%q/%%';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL_DIR, (int64_t)time(NULL), path, path);

  db_query_run(query, 1, 1);
#undef Q_TMPL_DIR
}

void
db_directory_disable_bymatch(char *path, char *strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE directories SET virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE virtual_path = '/file:%q' OR virtual_path LIKE '/file:%q/%%';"
  char *query;
  int64_t disabled;
  int striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;
  if (strlen(strip) > 0)
    striplen = strlen(strip) + strlen("/file:/");
  else
    striplen = 0;

  query = sqlite3_mprintf(Q_TMPL, striplen, disabled, path, path, path);

  db_query_run(query, 1, 1);
#undef Q_TMPL
}

int
db_directory_enable_bycookie(uint32_t cookie, char *path)
{
#define Q_TMPL "UPDATE directories SET virtual_path = '/file:%q' || virtual_path, disabled = 0 WHERE disabled = %" PRIi64 ";"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path, (int64_t)cookie);

  ret = db_query_run(query, 1, 0);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}

int
db_directory_enable_bypath(char *path)
{
#define Q_TMPL "UPDATE directories SET disabled = 0 WHERE virtual_path = %Q;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path);

  ret = db_query_run(query, 1, 0);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}


/* Remotes */
static int
db_pairing_delete_byremote(char *remote_id)
{
#define Q_TMPL "DELETE FROM pairings WHERE remote = '%q';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, remote_id);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_pairing_add(struct pairing_info *pi)
{
#define Q_TMPL "INSERT INTO pairings (remote, name, guid) VALUES ('%q', '%q', '%q');"
  char *query;
  int ret;

  ret = db_pairing_delete_byremote(pi->remote_id);
  if (ret < 0)
    return ret;

  query = sqlite3_mprintf(Q_TMPL, pi->remote_id, pi->name, pi->guid);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_pairing_fetch_byguid(struct pairing_info *pi)
{
#define Q_TMPL "SELECT p.* FROM pairings p WHERE p.guid = '%q';"
  char *query;
  sqlite3_stmt *stmt;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, pi->guid);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_INFO, L_DB, "Pairing GUID %s not found\n", pi->guid);
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return -1;
    }

  pi->remote_id = strdup((char *)sqlite3_column_text(stmt, 0));
  pi->name = strdup((char *)sqlite3_column_text(stmt, 1));

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return 0;

#undef Q_TMPL
}

#ifdef HAVE_SPOTIFY_H
/* Spotify */
void
db_spotify_purge(void)
{
  char *queries[5] =
    {
      "DELETE FROM files WHERE path LIKE 'spotify:%%';",
      "DELETE FROM playlistitems WHERE filepath LIKE 'spotify:%%';",
      "DELETE FROM playlists WHERE path LIKE 'spotify:%%';",
      "DELETE FROM directories WHERE virtual_path LIKE '/spotify:/%%';",
      "UPDATE directories SET disabled = 4294967296 WHERE virtual_path = '/spotify:';",
    };
  int i;
  int ret;

  for (i = 0; i < (sizeof(queries) / sizeof(queries[0])); i++)
    {
      ret = db_query_run(queries[i], 0, 1);

      if (ret == 0)
	DPRINTF(E_DBG, L_DB, "Purged %d rows\n", sqlite3_changes(hdl));
    }
}

/* Spotify */
void
db_spotify_pl_delete(int id)
{
  char *queries_tmpl[2] =
    {
      "DELETE FROM playlists WHERE id = %d;",
      "DELETE FROM playlistitems WHERE playlistid = %d;",
    };
  char *query;
  int i;
  int ret;

  for (i = 0; i < (sizeof(queries_tmpl) / sizeof(queries_tmpl[0])); i++)
    {
      query = sqlite3_mprintf(queries_tmpl[i], id);

      ret = db_query_run(query, 1, 1);

      if (ret == 0)
	DPRINTF(E_DBG, L_DB, "Deleted %d rows\n", sqlite3_changes(hdl));
    }
}

/* Spotify */
void
db_spotify_files_delete()
{
#define Q_TMPL "DELETE FROM files WHERE path LIKE 'spotify:%%' AND NOT path IN (SELECT filepath FROM playlistitems);"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL);

  ret = db_query_run(query, 1, 1);

  if (ret == 0)
    DPRINTF(E_DBG, L_DB, "Deleted %d rows\n", sqlite3_changes(hdl));
#undef Q_TMPL
}
#endif

/* Admin */
int
db_admin_add(const char *key, const char *value)
{
#define Q_TMPL "INSERT OR REPLACE INTO admin (key, value) VALUES ('%q', '%q');"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, key, value);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

char *
db_admin_get(const char *key)
{
#define Q_TMPL "SELECT value FROM admin a WHERE a.key = '%q';"
  char *query;
  sqlite3_stmt *stmt;
  char *res;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, key);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return NULL;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, strlen(query) + 1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_WARN, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return NULL;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret == SQLITE_DONE)
	DPRINTF(E_DBG, L_DB, "No results\n");
      else
	DPRINTF(E_WARN, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));	

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return NULL;
    }

  res = (char *)sqlite3_column_text(stmt, 0);
  if (res)
    res = strdup(res);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return res;

#undef Q_TMPL
}

int
db_admin_update(const char *key, const char *value)
{
#define Q_TMPL "UPDATE admin SET value='%q' WHERE key='%q';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, value, key);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_admin_delete(const char *key)
{
#define Q_TMPL "DELETE FROM admin where key='%q';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, key);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

/* Speakers */
int
db_speaker_save(uint64_t id, int selected, int volume, const char *name)
{
#define Q_TMPL "INSERT OR REPLACE INTO speakers (id, selected, volume, name) VALUES (%" PRIi64 ", %d, %d, '%q');"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, id, selected, volume, name);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_speaker_get(uint64_t id, int *selected, int *volume)
{
#define Q_TMPL "SELECT s.selected, s.volume FROM speakers s WHERE s.id = %" PRIi64 ";"
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

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      ret = -1;
      goto out;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      if (ret != SQLITE_DONE)
	DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));

      sqlite3_finalize(stmt);

      ret = -1;
      goto out;
    }

  *selected = sqlite3_column_int(stmt, 0);
  *volume = sqlite3_column_int(stmt, 1);

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);

  ret = 0;

 out:
  sqlite3_free(query);
  return ret;

#undef Q_TMPL
}

void
db_speaker_clear_all(void)
{
  db_query_run("UPDATE speakers SET selected = 0;", 0, 0);
}


/* Inotify */
int
db_watch_clear(void)
{
  return db_query_run("DELETE FROM inotify;", 0, 0);
}

int
db_watch_add(struct watch_info *wi)
{
#define Q_TMPL "INSERT INTO inotify (wd, cookie, path) VALUES (%d, 0, '%q');"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, wi->wd, wi->path);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_watch_delete_bywd(uint32_t wd)
{
#define Q_TMPL "DELETE FROM inotify WHERE wd = %d;"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, wd);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_watch_delete_bypath(char *path)
{
#define Q_TMPL "DELETE FROM inotify WHERE path = '%q';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, path);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_watch_delete_bymatch(char *path)
{
#define Q_TMPL "DELETE FROM inotify WHERE path LIKE '%q/%%';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, path);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_watch_delete_bycookie(uint32_t cookie)
{
#define Q_TMPL "DELETE FROM inotify WHERE cookie = %" PRIi64 ";"
  char *query;

  if (cookie == 0)
    return -1;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)cookie);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

static int
db_watch_get_byquery(struct watch_info *wi, char *query)
{
  sqlite3_stmt *stmt;
  char **strval;
  char *cval;
  uint32_t *ival;
  int64_t cookie;
  int ncols;
  int i;
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  ret = db_blocking_step(stmt);
  if (ret != SQLITE_ROW)
    {
      DPRINTF(E_WARN, L_DB, "Watch not found: '%s'\n", query);

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return -1;
    }

  ncols = sqlite3_column_count(stmt);

  if (sizeof(wi_cols_map) / sizeof(wi_cols_map[0]) != ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: wi column map out of sync with schema\n");

      sqlite3_finalize(stmt);
      sqlite3_free(query);
      return -1;
    }

  for (i = 0; i < ncols; i++)
    {
      switch (wi_cols_map[i].type)
	{
	  case DB_TYPE_INT:
	    ival = (uint32_t *) ((char *)wi + wi_cols_map[i].offset);

	    if (wi_cols_map[i].offset == wi_offsetof(cookie))
	      {
		cookie = sqlite3_column_int64(stmt, i);
		*ival = (cookie == INOTIFY_FAKE_COOKIE) ? 0 : cookie;
	      }
	    else
	      *ival = sqlite3_column_int(stmt, i);
	    break;

	  case DB_TYPE_STRING:
	    strval = (char **) ((char *)wi + wi_cols_map[i].offset);

	    cval = (char *)sqlite3_column_text(stmt, i);
	    if (cval)
	      *strval = strdup(cval);
	    break;

	  default:
	    DPRINTF(E_LOG, L_DB, "BUG: Unknown type %d in wi column map\n", wi_cols_map[i].type);
	    sqlite3_finalize(stmt);
	    sqlite3_free(query);
	    return -1;
	}
    }

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return 0;
}

int
db_watch_get_bywd(struct watch_info *wi)
{
#define Q_TMPL "SELECT * FROM inotify WHERE wd = %d;"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, wi->wd);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  return db_watch_get_byquery(wi, query);
#undef Q_TMPL
}

int
db_watch_get_bypath(struct watch_info *wi)
{
#define Q_TMPL "SELECT * FROM inotify WHERE path = '%q';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, wi->path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return -1;
    }

  return db_watch_get_byquery(wi, query);
#undef Q_TMPL
}

void
db_watch_mark_bypath(char *path, char *strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE inotify SET path = substr(path, %d), cookie = %" PRIi64 " WHERE path = '%q';"
  char *query;
  int64_t disabled;
  int striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;
  striplen = strlen(strip) + 1;

  query = sqlite3_mprintf(Q_TMPL, striplen, disabled, path);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_watch_mark_bymatch(char *path, char *strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE inotify SET path = substr(path, %d), cookie = %" PRIi64 " WHERE path LIKE '%q/%%';"
  char *query;
  int64_t disabled;
  int striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;
  striplen = strlen(strip) + 1;

  query = sqlite3_mprintf(Q_TMPL, striplen, disabled, path);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_watch_move_bycookie(uint32_t cookie, char *path)
{
#define Q_TMPL "UPDATE inotify SET path = '%q' || path, cookie = 0 WHERE cookie = %" PRIi64 ";"
  char *query;

  if (cookie == 0)
    return;

  query = sqlite3_mprintf(Q_TMPL, path, (int64_t)cookie);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_watch_cookie_known(uint32_t cookie)
{
#define Q_TMPL "SELECT COUNT(*) FROM inotify WHERE cookie = %" PRIi64 ";"
  char *query;
  int ret;

  if (cookie == 0)
    return 0;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)cookie);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return 0;
    }

  ret = db_get_count(query);

  sqlite3_free(query);

  return (ret > 0);

#undef Q_TMPL
}

int
db_watch_enum_start(struct watch_enum *we)
{
#define Q_MATCH_TMPL "SELECT wd FROM inotify WHERE path LIKE '%q/%%';"
#define Q_COOKIE_TMPL "SELECT wd FROM inotify WHERE cookie = %" PRIi64 ";"
  char *query;
  int ret;

  we->stmt = NULL;

  if (we->match)
    query = sqlite3_mprintf(Q_MATCH_TMPL, we->match);
  else if (we->cookie != 0)
    query = sqlite3_mprintf(Q_COOKIE_TMPL, we->cookie);
  else
    {
      DPRINTF(E_LOG, L_DB, "Could not start enum, no parameter given\n");
      return -1;
    }

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Starting enum '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &we->stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  return 0;

#undef Q_MATCH_TMPL
#undef Q_COOKIE_TMPL
}

void
db_watch_enum_end(struct watch_enum *we)
{
  if (!we->stmt)
    return;

  sqlite3_finalize(we->stmt);
  we->stmt = NULL;
}

int
db_watch_enum_fetchwd(struct watch_enum *we, uint32_t *wd)
{
  int ret;

  *wd = 0;

  if (!we->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Watch enum not started!\n");
      return -1;
    }

  ret = db_blocking_step(we->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_INFO, L_DB, "End of watch enum results\n");
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));	
      return -1;
    }

  *wd = (uint32_t)sqlite3_column_int(we->stmt, 0);

  return 0;
}


#ifdef DB_PROFILE
static void
db_xprofile(void *notused, const char *pquery, sqlite3_uint64 ptime)
{
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  DPRINTF(E_DBG, L_DBPERF, "SQL PROFILE query: %s\n", pquery);
  DPRINTF(E_DBG, L_DBPERF, "SQL PROFILE time: %" PRIu64 " ms\n", ((uint64_t)ptime / 1000000));

  if ((strncmp(pquery, "SELECT", 6) != 0)
       && (strncmp(pquery, "UPDATE", 6) != 0)
       && (strncmp(pquery, "DELETE", 6) != 0))
      return;

  /* Disable profiling callback */
  sqlite3_profile(hdl, NULL, NULL);

  query = sqlite3_mprintf("EXPLAIN QUERY PLAN %s", pquery);
  if (!query)
    {
      DPRINTF(E_DBG, L_DBPERF, "Query plan: Out of memory\n");

      goto out;
    }

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_DBG, L_DBPERF, "Query plan: Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      goto out;
    }

  DPRINTF(E_DBG, L_DBPERF, "Query plan:\n");

  while ((ret = db_blocking_step(stmt)) == SQLITE_ROW)
    {
      DPRINTF(E_DBG, L_DBPERF, "(%d,%d,%d) %s\n",
	      sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2),
	      sqlite3_column_text(stmt, 3));
    }

  if (ret != SQLITE_DONE)
    DPRINTF(E_DBG, L_DBPERF, "Query plan: Could not step: %s\n", sqlite3_errmsg(hdl));

  DPRINTF(E_DBG, L_DBPERF, "---\n");

  sqlite3_finalize(stmt);

 out:
  /* Reenable profiling callback */
  sqlite3_profile(hdl, db_xprofile, NULL);
}
#endif

static int
db_pragma_get_cache_size()
{
  sqlite3_stmt *stmt;
  char *query = "PRAGMA cache_size;";
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return 0;
    }

  ret = db_blocking_step(stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      sqlite3_free(query);
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      sqlite3_free(query);
      return -1;
    }

  ret = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);
  return ret;
}

static int
db_pragma_set_cache_size(int pages)
{
#define Q_TMPL "PRAGMA cache_size=%d;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, pages);
  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return 0;
    }

  sqlite3_finalize(stmt);
  sqlite3_free(query);
  return 0;
#undef Q_TMPL
}

static char *
db_pragma_set_journal_mode(char *mode)
{
#define Q_TMPL "PRAGMA journal_mode=%s;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;
  char *new_mode;

  query = sqlite3_mprintf(Q_TMPL, mode);
  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return NULL;
    }

  ret = db_blocking_step(stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      sqlite3_free(query);
      return NULL;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      sqlite3_free(query);
      return NULL;
    }

  new_mode = (char *) sqlite3_column_text(stmt, 0);
  sqlite3_finalize(stmt);
  sqlite3_free(query);
  return new_mode;
#undef Q_TMPL
}

static int
db_pragma_get_synchronous()
{
  sqlite3_stmt *stmt;
  char *query = "PRAGMA synchronous;";
  int ret;

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return 0;
    }

  ret = db_blocking_step(stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of query results\n");
      sqlite3_free(query);
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      sqlite3_free(query);
      return -1;
    }

  ret = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);
  return ret;
}

static int
db_pragma_set_synchronous(int synchronous)
{
#define Q_TMPL "PRAGMA synchronous=%d;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, synchronous);
  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return 0;
    }

  sqlite3_finalize(stmt);
  sqlite3_free(query);
  return 0;
#undef Q_TMPL
}



int
db_perthread_init(void)
{
  char *errmsg;
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

  ret = sqlite3_enable_load_extension(hdl, 1);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not enable extension loading\n");

      sqlite3_close(hdl);
      return -1;
    }

  errmsg = NULL;
  ret = sqlite3_load_extension(hdl, PKGLIBDIR "/forked-daapd-sqlext.so", NULL, &errmsg);
  if (ret != SQLITE_OK)
    {
      if (errmsg)
	{
	  DPRINTF(E_LOG, L_DB, "Could not load SQLite extension: %s\n", errmsg);
	  sqlite3_free(errmsg);
	}
      else
	DPRINTF(E_LOG, L_DB, "Could not load SQLite extension: %s\n", sqlite3_errmsg(hdl));

      sqlite3_close(hdl);
      return -1;
    }

  ret = sqlite3_enable_load_extension(hdl, 0);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not disable extension loading\n");

      sqlite3_close(hdl);
      return -1;
    }

#ifdef DB_PROFILE
  sqlite3_profile(hdl, db_xprofile, NULL);
#endif

  cache_size = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_cache_size_library");
  if (cache_size > -1)
    {
      db_pragma_set_cache_size(cache_size);
      cache_size = db_pragma_get_cache_size();
      DPRINTF(E_DBG, L_DB, "Database cache size in pages: %d\n", cache_size);
    }

  journal_mode = cfg_getstr(cfg_getsec(cfg, "sqlite"), "pragma_journal_mode");
  if (journal_mode)
    {
      journal_mode = db_pragma_set_journal_mode(journal_mode);
      DPRINTF(E_DBG, L_DB, "Database journal mode: %s\n", journal_mode);
    }

  synchronous = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_synchronous");
  if (synchronous > -1)
    {
      db_pragma_set_synchronous(synchronous);
      synchronous = db_pragma_get_synchronous();
      DPRINTF(E_DBG, L_DB, "Database synchronous: %d\n", synchronous);
    }

  return 0;
}

void
db_perthread_deinit(void)
{
  sqlite3_stmt *stmt;

  if (!hdl)
    return;

  /* Tear down anything that's in flight */
  while ((stmt = sqlite3_next_stmt(hdl, 0)))
    sqlite3_finalize(stmt);

  sqlite3_close(hdl);
}




static int
db_check_version(void)
{
#define Q_VACUUM "VACUUM;"
  char *buf;
  char *errmsg;
  int db_ver_major;
  int db_ver_minor;
  int db_ver;
  int vacuum;
  int ret;

  vacuum = cfg_getbool(cfg_getsec(cfg, "sqlite"), "vacuum");

  buf = db_admin_get("schema_version_major");
  if (!buf)
    buf = db_admin_get("schema_version"); // Pre schema v15.1

  if (!buf)
    return 1; // Will create new database

  safe_atoi32(buf, &db_ver_major);
  free(buf);

  buf = db_admin_get("schema_version_minor");
  if (buf)
    {
      safe_atoi32(buf, &db_ver_minor);
      free(buf);
    }
  else
    db_ver_minor = 0;

  db_ver = db_ver_major * 100 + db_ver_minor;

  if (db_ver_major < 10)
    {
      DPRINTF(E_FATAL, L_DB, "Database schema v%d too old, cannot upgrade\n", db_ver_major);

      return -1;
    }
  else if (db_ver_major > SCHEMA_VERSION_MAJOR)
    {
      DPRINTF(E_FATAL, L_DB, "Database schema v%d is newer than the supported version\n", db_ver_major);

      return -1;
    }
  else if (db_ver < (SCHEMA_VERSION_MAJOR * 100 + SCHEMA_VERSION_MINOR))
    {
      DPRINTF(E_LOG, L_DB, "Database schema outdated, upgrading schema v%d.%d -> v%d.%d...\n",
                           db_ver_major, db_ver_minor, SCHEMA_VERSION_MAJOR, SCHEMA_VERSION_MINOR);

      ret = sqlite3_exec(hdl, "BEGIN TRANSACTION;", NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "DB error while running 'BEGIN TRANSACTION': %s\n",  errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}

      ret = db_upgrade(hdl, db_ver);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Database upgrade errored out, rolling back changes ...\n");
	  ret = sqlite3_exec(hdl, "ROLLBACK TRANSACTION;", NULL, NULL, &errmsg);
	  if (ret != SQLITE_OK)
	    {
	      DPRINTF(E_LOG, L_DB, "DB error while running 'ROLLBACK TRANSACTION': %s\n",  errmsg);

	      sqlite3_free(errmsg);
	    }

	  return -1;
	}

      ret = db_init_indices(hdl);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Database upgrade errored out, rolling back changes ...\n");
	  ret = sqlite3_exec(hdl, "ROLLBACK TRANSACTION;", NULL, NULL, &errmsg);
	  if (ret != SQLITE_OK)
	    {
	      DPRINTF(E_LOG, L_DB, "DB error while running 'ROLLBACK TRANSACTION': %s\n",  errmsg);

	      sqlite3_free(errmsg);
	    }

	  return -1;
	}

      ret = sqlite3_exec(hdl, "COMMIT TRANSACTION;", NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "DB error while running 'COMMIT TRANSACTION': %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}

      DPRINTF(E_LOG, L_DB, "Upgrading schema to v%d.%d completed\n", SCHEMA_VERSION_MAJOR, SCHEMA_VERSION_MINOR);

      vacuum = 1;
    }

  if (vacuum)
    {
      DPRINTF(E_LOG, L_DB, "Now vacuuming database, this may take some time...\n");

      ret = sqlite3_exec(hdl, Q_VACUUM, NULL, NULL, &errmsg);
      if (ret != SQLITE_OK)
	{
	  DPRINTF(E_LOG, L_DB, "Could not VACUUM database: %s\n", errmsg);

	  sqlite3_free(errmsg);
	  return -1;
	}
    }

  return 0;

#undef Q_VACUUM
}

int
db_init(void)
{
  int files;
  int pls;
  int ret;

  db_path = cfg_getstr(cfg_getsec(cfg, "general"), "db_path");

  ret = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DB, "Could not switch SQLite3 to multithread mode\n");
      DPRINTF(E_FATAL, L_DB, "Check that SQLite3 has been configured for thread-safe operations\n");
      return -1;
    }

  ret = sqlite3_enable_shared_cache(1);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DB, "Could not enable SQLite3 shared-cache mode\n");
      return -1;
    }

  ret = sqlite3_initialize();
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_FATAL, L_DB, "SQLite3 failed to initialize\n");
      return -1;
    }

  ret = db_perthread_init();
  if (ret < 0)
    return ret;

  ret = db_check_version();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_DB, "Database version check errored out, incompatible database\n");

      db_perthread_deinit();
      return -1;
    }
  else if (ret > 0)
    {
      DPRINTF(E_LOG, L_DB, "Could not check database version, trying DB init\n");

      ret = db_init_tables(hdl);
      if (ret < 0)
	{
	  DPRINTF(E_FATAL, L_DB, "Could not create tables\n");
	  db_perthread_deinit();
	  return -1;
	}
    }

  db_analyze();

  db_set_cfg_names();

  files = db_files_get_count();
  pls = db_pl_get_count();

  db_perthread_deinit();

  DPRINTF(E_LOG, L_DB, "Database OK with %d active files and %d active playlists\n", files, pls);

  return 0;
}

void
db_deinit(void)
{
  sqlite3_shutdown();
}
