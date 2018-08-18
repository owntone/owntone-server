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
#include <unictype.h>
#include <uninorm.h>
#include <unistr.h>
#include <sys/mman.h>
#include <limits.h>

#include <sqlite3.h>

#include "conffile.h"
#include "logger.h"
#include "cache.h"
#include "listener.h"
#include "library.h"
#include "misc.h"
#include "db.h"
#include "db_init.h"
#include "db_upgrade.h"
#include "rng.h"


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

struct query_clause {
  char *where;
  char *group;
  char *having;
  char *order;
  char *index;
};

struct browse_clause {
  char *select;
  char *where;
  char *group;
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
    { mfi_offsetof(skip_count),         DB_TYPE_INT },
    { mfi_offsetof(time_skipped),       DB_TYPE_INT },
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
    { pli_offsetof(query_order),DB_TYPE_STRING },
    { pli_offsetof(query_limit),  DB_TYPE_INT },

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
    dbmfi_offsetof(skip_count),
    dbmfi_offsetof(time_skipped),
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
    dbpli_offsetof(query_order),
    dbpli_offsetof(query_limit),

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

/* Sort clauses, used for ORDER BY */
/* Keep in sync with enum sort_type and indices */
static const char *sort_clause[] =
  {
    "",
    "f.title_sort",
    "f.album_sort, f.disc, f.track",
    "f.album_artist_sort, f.album_sort, f.disc, f.track",
    "f.type, f.parent_id, f.special_id, f.title",
    "f.year",
    "f.genre",
    "f.composer_sort",
    "f.disc",
    "f.track",
    "f.virtual_path",
    "pos",
    "shuffle_pos",
  };

/* Browse clauses, used for SELECT, WHERE, GROUP BY and for default ORDER BY
 * Keep in sync with enum query_type and indices
 * Col 1: for SELECT, Col 2: for WHERE, Col 3: for GROUP BY/ORDER BY
 */
static const struct browse_clause browse_clause[] =
  {
    { "",                                      "",                 "" },
    { "f.album_artist, f.album_artist_sort",   "f.album_artist",   "f.album_artist_sort, f.album_artist" },
    { "f.album, f.album_sort",                 "f.album",          "f.album_sort, f.album" },
    { "f.genre, f.genre",                      "f.genre",          "f.genre" },
    { "f.composer, f.composer_sort",           "f.composer",       "f.composer_sort, f.composer" },
    { "f.year, f.year",                        "f.year",           "f.year" },
    { "f.disc, f.disc",                        "f.disc",           "f.disc" },
    { "f.track, f.track",                      "f.track",          "f.track" },
    { "f.virtual_path, f.virtual_path",        "f.virtual_path",   "f.virtual_path" },
    { "f.path, f.path",                        "f.path",           "f.path" },
  };


struct media_kind_label {
  enum media_kind type;
  const char *label;
};

/* Keep in sync with enum media_kind */
static const struct media_kind_label media_kind_labels[] =
  {
    { MEDIA_KIND_MUSIC,      "music" },
    { MEDIA_KIND_MOVIE,      "movie" },
    { MEDIA_KIND_PODCAST,    "podcast" },
    { MEDIA_KIND_AUDIOBOOK,  "audiobook" },
    { MEDIA_KIND_MUSICVIDEO, "musicvideo" },
    { MEDIA_KIND_TVSHOW,     "tvshow" },
  };

const char *
db_media_kind_label(enum media_kind media_kind)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(media_kind_labels); i++)
    {
      if (media_kind == media_kind_labels[i].type)
	return media_kind_labels[i].label;
    }

  return NULL;
}

enum media_kind
db_media_kind_enum(const char *label)
{
  int i;

  if (!label)
    return 0;

  for (i = 0; i < ARRAY_SIZE(media_kind_labels); i++)
    {
      if (strcmp(label, media_kind_labels[i].label) == 0)
	return media_kind_labels[i].type;
    }

  return 0;
}

/* Keep in sync with enum data_kind */
static char *data_kind_label[] = { "file", "url", "spotify", "pipe" };

const char *
db_data_kind_label(enum data_kind data_kind)
{
  if (data_kind < ARRAY_SIZE(data_kind_label))
    {
      return data_kind_label[data_kind];
    }

  return NULL;
}

/* Shuffle RNG state */
struct rng_ctx shuffle_rng;

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

static int
db_query_run(char *query, int free, short update_events);


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

// Basically a wrapper for sqlite3_mprintf()
char *
db_mprintf(const char *fmt, ...)
{
  char *query;
  char *ret;
  va_list va;

  va_start(va, fmt);
  ret = sqlite3_vmprintf(fmt, va);
  if (!ret)
    {
      DPRINTF(E_FATAL, L_MISC, "Out of memory for db_mprintf\n");
      abort();
    }
  va_end(va);

  query = strdup(ret);
  sqlite3_free(ret);

  return query;
}

int
db_snprintf(char *s, int n, const char *fmt, ...)
{
  char *ret;
  va_list va;

  if (n < 2)
    return -1;

  // For size check since sqlite3_vsnprintf does not seem to support it
  s[n - 2] = '\0';

  va_start(va, fmt);
  ret = sqlite3_vsnprintf(n, s, fmt, va);
  va_end(va);

  if (!ret || (s[n - 2] != '\0'))
    return -1;

  return 0;
}

void
free_pi(struct pairing_info *pi, int content_only)
{
  if (!pi)
    return;

  free(pi->remote_id);
  free(pi->name);
  free(pi->guid);

  if (!content_only)
    free(pi);
  else
    memset(pi, 0, sizeof(struct pairing_info));
}

void
free_mfi(struct media_file_info *mfi, int content_only)
{
  if (!mfi)
    return;

  free(mfi->path);
  free(mfi->fname);
  free(mfi->title);
  free(mfi->artist);
  free(mfi->album);
  free(mfi->genre);
  free(mfi->comment);
  free(mfi->type);
  free(mfi->composer);
  free(mfi->orchestra);
  free(mfi->conductor);
  free(mfi->grouping);
  free(mfi->description);
  free(mfi->codectype);
  free(mfi->album_artist);
  free(mfi->tv_series_name);
  free(mfi->tv_episode_num_str);
  free(mfi->tv_network_name);
  free(mfi->title_sort);
  free(mfi->artist_sort);
  free(mfi->album_sort);
  free(mfi->composer_sort);
  free(mfi->album_artist_sort);
  free(mfi->virtual_path);

  if (!content_only)
    free(mfi);
  else
    memset(mfi, 0, sizeof(struct media_file_info));
}

void
free_pli(struct playlist_info *pli, int content_only)
{
  if (!pli)
    return;

  free(pli->title);
  free(pli->query);
  free(pli->path);
  free(pli->virtual_path);
  free(pli->query_order);

  if (!content_only)
    free(pli);
  else
    memset(pli, 0, sizeof(struct playlist_info));
}

void
free_di(struct directory_info *di, int content_only)
{
  if (!di)
    return;

  free(di->virtual_path);

  if (!content_only)
    free(di);
  else
    memset(di, 0, sizeof(struct directory_info));
}

void
free_query_params(struct query_params *qp, int content_only)
{
  if (!qp)
    return;

  free(qp->filter);
  free(qp->having);
  free(qp->order);

  if (!content_only)
    free(qp);
  else
    memset(qp, 0, sizeof(struct query_params));
}

void
free_queue_item(struct db_queue_item *queue_item, int content_only)
{
  if (!queue_item)
    return;

  free(queue_item->path);
  free(queue_item->virtual_path);
  free(queue_item->title);
  free(queue_item->artist);
  free(queue_item->album_artist);
  free(queue_item->album);
  free(queue_item->genre);
  free(queue_item->artist_sort);
  free(queue_item->album_sort);
  free(queue_item->album_artist_sort);
  free(queue_item->artwork_url);

  if (!content_only)
    free(queue_item);
  else
    memset(queue_item, 0, sizeof(struct db_queue_item));
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

static void
sort_tag_create(char **sort_tag, char *src_tag)
{
  const uint8_t *i_ptr;
  const uint8_t *n_ptr;
  const uint8_t *number;
  uint8_t out[1024];
  uint8_t *o_ptr;
  int append_number;
  ucs4_t puc;
  int numlen;
  size_t len;
  int charlen;

  /* Note: include terminating NUL in string length for u8_normalize */

  if (*sort_tag)
    {
      DPRINTF(E_DBG, L_LIB, "Existing sort tag will be normalized: %s\n", *sort_tag);
      o_ptr = u8_normalize(UNINORM_NFD, (uint8_t *)*sort_tag, strlen(*sort_tag) + 1, NULL, &len);
      free(*sort_tag);
      *sort_tag = (char *)o_ptr;
      return;
    }

  if (!src_tag || ((len = strlen(src_tag)) == 0))
    {
      *sort_tag = NULL;
      return;
    }

  // Set input pointer past article if present
  if ((strncasecmp(src_tag, "a ", 2) == 0) && (len > 2))
    i_ptr = (uint8_t *)(src_tag + 2);
  else if ((strncasecmp(src_tag, "an ", 3) == 0) && (len > 3))
    i_ptr = (uint8_t *)(src_tag + 3);
  else if ((strncasecmp(src_tag, "the ", 4) == 0) && (len > 4))
    i_ptr = (uint8_t *)(src_tag + 4);
  else
    i_ptr = (uint8_t *)src_tag;

  // Poor man's natural sort. Makes sure we sort like this: a1, a2, a10, a11, a21, a111
  // We do this by padding zeroes to (short) numbers. As an alternative we could have
  // made a proper natural sort algorithm in sqlext.c, but we don't, since we don't
  // want any risk of hurting response times
  memset(&out, 0, sizeof(out));
  o_ptr = (uint8_t *)&out;
  number = NULL;
  append_number = 0;

  do
    {
      n_ptr = u8_next(&puc, i_ptr);

      if (uc_is_digit(puc))
	{
	  if (!number) // We have encountered the beginning of a number
	    number = i_ptr;
	  append_number = (n_ptr == NULL); // If last char in string append number now
	}
      else
	{
	  if (number)
	    append_number = 1; // A number has ended so time to append it
	  else
	    {
              charlen = u8_strmblen(i_ptr);
              if (charlen >= 0)
	    	o_ptr = u8_stpncpy(o_ptr, i_ptr, charlen); // No numbers in sight, just append char
	    }
	}

      // Break if less than 100 bytes remain (prevent buffer overflow)
      if (sizeof(out) - u8_strlen(out) < 100)
	break;

      // Break if number is very large (prevent buffer overflow)
      if (number && (i_ptr - number > 50))
	break;

      if (append_number)
	{
	  numlen = i_ptr - number;
	  if (numlen < 5) // Max pad width
	    {
	      u8_strcpy(o_ptr, (uint8_t *)"00000");
	      o_ptr += (5 - numlen);
	    }
	  o_ptr = u8_stpncpy(o_ptr, number, numlen + u8_strmblen(i_ptr));

	  number = NULL;
	  append_number = 0;
	}

      i_ptr = n_ptr;
    }
  while (n_ptr);

  *sort_tag = (char *)u8_normalize(UNINORM_NFD, (uint8_t *)&out, u8_strlen(out) + 1, NULL, &len);
}

void
fixup_tags_mfi(struct media_file_info *mfi)
{
  cfg_t *lib;
  size_t len;
  char *tag;
  char *sep = " - ";
  char *ca;

  if (mfi->genre && (strlen(mfi->genre) == 0))
    {
      free(mfi->genre);
      mfi->genre = NULL;
    }

  if (mfi->artist && (strlen(mfi->artist) == 0))
    {
      free(mfi->artist);
      mfi->artist = NULL;
    }

  if (mfi->title && (strlen(mfi->title) == 0))
    {
      free(mfi->title);
      mfi->title = NULL;
    }

  /*
   * Default to mpeg4 video/audio for unknown file types
   * in an attempt to allow streaming of DRM-afflicted files
   */
  if (mfi->codectype && strcmp(mfi->codectype, "unkn") == 0)
    {
      if (mfi->has_video)
	{
	  strcpy(mfi->codectype, "mp4v");
	  strcpy(mfi->type, "m4v");
	}
      else
	{
	  strcpy(mfi->codectype, "mp4a");
	  strcpy(mfi->type, "m4a");
	}
    }

  if (!mfi->artist)
    {
      if (mfi->orchestra && mfi->conductor)
	{
	  len = strlen(mfi->orchestra) + strlen(sep) + strlen(mfi->conductor);
	  tag = (char *)malloc(len + 1);
	  if (tag)
	    {
	      sprintf(tag,"%s%s%s", mfi->orchestra, sep, mfi->conductor);
	      mfi->artist = tag;
            }
        }
      else if (mfi->orchestra)
	{
	  mfi->artist = strdup(mfi->orchestra);
        }
      else if (mfi->conductor)
	{
	  mfi->artist = strdup(mfi->conductor);
        }
    }

  /* Handle TV shows, try to present prettier metadata */
  if (mfi->tv_series_name && strlen(mfi->tv_series_name) != 0)
    {
      mfi->media_kind = MEDIA_KIND_TVSHOW;  /* tv show */

      /* Default to artist = series_name */
      if (mfi->artist && strlen(mfi->artist) == 0)
	{
	  free(mfi->artist);
	  mfi->artist = NULL;
	}

      if (!mfi->artist)
	mfi->artist = strdup(mfi->tv_series_name);

      /* Default to album = "<series_name>, Season <season_num>" */
      if (mfi->album && strlen(mfi->album) == 0)
	{
	  free(mfi->album);
	  mfi->album = NULL;
	}

      if (!mfi->album)
	{
	  len = snprintf(NULL, 0, "%s, Season %u", mfi->tv_series_name, mfi->tv_season_num);

	  mfi->album = (char *)malloc(len + 1);
	  if (mfi->album)
	    sprintf(mfi->album, "%s, Season %u", mfi->tv_series_name, mfi->tv_season_num);
	}
    }

  /* Check the 4 top-tags are filled */
  if (!mfi->artist)
    mfi->artist = strdup("Unknown artist");
  if (!mfi->album)
    mfi->album = strdup("Unknown album");
  if (!mfi->genre)
    mfi->genre = strdup("Unknown genre");
  if (!mfi->title)
    {
      /* fname is left untouched by unicode_fixup_mfi() for
       * obvious reasons, so ensure it is proper UTF-8
       */
      mfi->title = unicode_fixup_string(mfi->fname, "ascii");
      if (mfi->title == mfi->fname)
	mfi->title = strdup(mfi->fname);
    }

  /* Ensure sort tags are filled, manipulated and normalized */
  sort_tag_create(&mfi->artist_sort, mfi->artist);
  sort_tag_create(&mfi->album_sort, mfi->album);
  sort_tag_create(&mfi->title_sort, mfi->title);

  /* We need to set album_artist according to media type and config */
  if (mfi->compilation)          /* Compilation */
    {
      lib = cfg_getsec(cfg, "library");
      ca = cfg_getstr(lib, "compilation_artist");
      if (ca && mfi->album_artist)
	{
	  free(mfi->album_artist);
	  mfi->album_artist = strdup(ca);
	}
      else if (ca && !mfi->album_artist)
	{
	  mfi->album_artist = strdup(ca);
	}
      else if (!ca && !mfi->album_artist)
	{
	  mfi->album_artist = strdup("");
	  mfi->album_artist_sort = strdup("");
	}
    }
  else if (mfi->media_kind == MEDIA_KIND_PODCAST) /* Podcast */
    {
      if (mfi->album_artist)
	free(mfi->album_artist);
      mfi->album_artist = strdup("");
      mfi->album_artist_sort = strdup("");
    }
  else if (!mfi->album_artist)   /* Regular media without album_artist */
    {
      mfi->album_artist = strdup(mfi->artist);
    }

  if (!mfi->album_artist_sort && (strcmp(mfi->album_artist, mfi->artist) == 0))
    mfi->album_artist_sort = strdup(mfi->artist_sort);
  else
    sort_tag_create(&mfi->album_artist_sort, mfi->album_artist);

  /* Composer is not one of our mandatory tags, so take extra care */
  if (mfi->composer_sort || mfi->composer)
    sort_tag_create(&mfi->composer_sort, mfi->composer);
}

static void
fixup_tags_queue_item(struct db_queue_item *queue_item)
{
  if (queue_item->genre && (strlen(queue_item->genre) == 0))
    {
      free(queue_item->genre);
      queue_item->genre = NULL;
    }

  if (queue_item->artist && (strlen(queue_item->artist) == 0))
    {
      free(queue_item->artist);
      queue_item->artist = NULL;
    }

  if (queue_item->title && (strlen(queue_item->title) == 0))
    {
      free(queue_item->title);
      queue_item->title = NULL;
    }

  /* Check the 4 top-tags are filled */
  if (!queue_item->artist)
    queue_item->artist = strdup("Unknown artist");
  if (!queue_item->album)
    queue_item->album = strdup("Unknown album");
  if (!queue_item->genre)
    queue_item->genre = strdup("Unknown genre");
  if (!queue_item->title)
    queue_item->title = strdup(queue_item->path);

  /* Ensure sort tags are filled, manipulated and normalized */
  sort_tag_create(&queue_item->artist_sort, queue_item->artist);
  sort_tag_create(&queue_item->album_sort, queue_item->album);

  /* We need to set album_artist according to media type and config */
  if (queue_item->media_kind == MEDIA_KIND_PODCAST) /* Podcast */
    {
      if (queue_item->album_artist)
	free(queue_item->album_artist);
      queue_item->album_artist = strdup("");
      queue_item->album_artist_sort = strdup("");
    }
  else if (!queue_item->album_artist)   /* Regular media without album_artist */
    {
      queue_item->album_artist = strdup(queue_item->artist);
    }

  if (!queue_item->album_artist_sort && (strcmp(queue_item->album_artist, queue_item->artist) == 0))
    queue_item->album_artist_sort = strdup(queue_item->artist_sort);
  else
    sort_tag_create(&queue_item->album_artist_sort, queue_item->album_artist);
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

      CHECK_ERR(L_DB, pthread_mutex_lock(&u->lck));

      u->proceed = 1;
      CHECK_ERR(L_DB, pthread_cond_signal(&u->cond));

      CHECK_ERR(L_DB, pthread_mutex_unlock(&u->lck));
    }
}

static int
db_wait_unlock(void)
{
  struct db_unlock u;
  int ret;

  u.proceed = 0;
  CHECK_ERR(L_DB, mutex_init(&u.lck));
  CHECK_ERR(L_DB, pthread_cond_init(&u.cond, NULL));

  ret = sqlite3_unlock_notify(hdl, unlock_notify_cb, &u);
  if (ret == SQLITE_OK)
    {
      CHECK_ERR(L_DB, pthread_mutex_lock(&u.lck));

      if (!u.proceed)
	{
	  DPRINTF(E_INFO, L_DB, "Waiting for database unlock\n");
	  CHECK_ERR(L_DB, pthread_cond_wait(&u.cond, &u.lck));
	}

      CHECK_ERR(L_DB, pthread_mutex_unlock(&u.lck));
    }

  CHECK_ERR(L_DB, pthread_cond_destroy(&u.cond));
  CHECK_ERR(L_DB, pthread_mutex_destroy(&u.lck));

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
db_pragma_optimize(void)
{
  const char *query = "ANALYZE;";
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

  db_pragma_optimize();

  DPRINTF(E_DBG, L_DB, "Done with post-scan DB maintenance\n");
}

void
db_purge_cruft(time_t ref)
{
#define Q_TMPL "DELETE FROM directories WHERE id >= %d AND db_timestamp < %" PRIi64 ";"
  int i;
  int ret;
  char *query;
  char *queries_tmpl[4] =
    {
      "DELETE FROM playlistitems WHERE playlistid IN (SELECT p.id FROM playlists p WHERE p.type <> %d AND p.db_timestamp < %" PRIi64 ");",
      "DELETE FROM playlistitems WHERE filepath IN (SELECT f.path FROM files f WHERE -1 <> %d AND f.db_timestamp < %" PRIi64 ");",
      "DELETE FROM playlists WHERE type <> %d AND db_timestamp < %" PRIi64 ";",
      "DELETE FROM files WHERE -1 <> %d AND db_timestamp < %" PRIi64 ";",
    };

  db_transaction_begin();

  for (i = 0; i < (sizeof(queries_tmpl) / sizeof(queries_tmpl[0])); i++)
    {
      query = sqlite3_mprintf(queries_tmpl[i], PL_SPECIAL, (int64_t)ref);
      if (!query)
	{
	  DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
	  db_transaction_end();
	  return;
	}

      DPRINTF(E_DBG, L_DB, "Running purge query '%s'\n", query);

      ret = db_query_run(query, 1, 0);
      if (ret == 0)
	DPRINTF(E_DBG, L_DB, "Purged %d rows\n", sqlite3_changes(hdl));
    }

  query = sqlite3_mprintf(Q_TMPL, DIR_MAX, (int64_t)ref);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      db_transaction_end();
      return;
    }

  DPRINTF(E_DBG, L_DB, "Running purge query '%s'\n", query);

  ret = db_query_run(query, 1, LISTENER_DATABASE);
  if (ret == 0)
    DPRINTF(E_DBG, L_DB, "Purged %d rows\n", sqlite3_changes(hdl));

  db_transaction_end();

#undef Q_TMPL
}

void
db_purge_all(void)
{
#define Q_TMPL_PL "DELETE FROM playlists WHERE type <> %d;"
#define Q_TMPL_DIR "DELETE FROM directories WHERE id >= %d;"
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

  // Purge playlists
  query = sqlite3_mprintf(Q_TMPL_PL, PL_SPECIAL);
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

  // Purge directories
  query = sqlite3_mprintf(Q_TMPL_DIR, DIR_MAX);
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

#undef Q_TMPL_PL
#undef Q_TMPL_DIR
}

static int
db_get_one_int(const char *query)
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
      if (ret == SQLITE_DONE)
	DPRINTF(E_INFO, L_DB, "No matching row found for query: %s\n", query);
      else
	DPRINTF(E_LOG, L_DB, "Could not step: %s (%s)\n", sqlite3_errmsg(hdl), query);

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

void
db_transaction_rollback(void)
{
  char *query = "ROLLBACK TRANSACTION;";
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

static void
db_free_query_clause(struct query_clause *qc)
{
  if (!qc)
    return;

  sqlite3_free(qc->where);
  sqlite3_free(qc->group);
  sqlite3_free(qc->having);
  sqlite3_free(qc->order);
  sqlite3_free(qc->index);
  free(qc);
}

static struct query_clause *
db_build_query_clause(struct query_params *qp)
{
  struct query_clause *qc;

  qc = calloc(1, sizeof(struct query_clause));
  if (!qc)
    goto error;

  if (qp->type & Q_F_BROWSE)
    qc->group = sqlite3_mprintf("GROUP BY %s", browse_clause[qp->type & ~Q_F_BROWSE].group);

  if (qp->filter)
    qc->where = sqlite3_mprintf("WHERE f.disabled = 0 AND %s", qp->filter);
  else
    qc->where = sqlite3_mprintf("WHERE f.disabled = 0");

  if (qp->having && (qp->type & (Q_GROUP_ALBUMS | Q_GROUP_ARTISTS)))
      qc->having = sqlite3_mprintf("HAVING %s", qp->having);
    else
      qc->having = sqlite3_mprintf("");

  if (qp->order)
    qc->order = sqlite3_mprintf("ORDER BY %s", qp->order);
  else if (qp->sort)
    qc->order = sqlite3_mprintf("ORDER BY %s", sort_clause[qp->sort]);
  else if (qp->type & Q_F_BROWSE)
    qc->order = sqlite3_mprintf("ORDER BY %s", browse_clause[qp->type & ~Q_F_BROWSE].group);
  else
    qc->order = sqlite3_mprintf("");

  switch (qp->idx_type)
    {
      case I_FIRST:
	qc->index = sqlite3_mprintf("LIMIT %d", qp->limit);
	break;

      case I_LAST:
	qc->index = sqlite3_mprintf("LIMIT -1 OFFSET %d", qp->results - qp->limit);
	break;

      case I_SUB:
	if (qp->limit)
	  qc->index = sqlite3_mprintf("LIMIT %d OFFSET %d", qp->limit, qp->offset);
	else
	  qc->index = sqlite3_mprintf("LIMIT -1 OFFSET %d", qp->offset);
	break;

      case I_NONE:
	qc->index = sqlite3_mprintf("");
	break;
    }

  if (!qc->where || !qc->index)
    goto error;

  return qc;

 error:
  DPRINTF(E_LOG, L_DB, "Error building query clause\n");
  db_free_query_clause(qc);
  return NULL;
}

static char *
db_build_query_check(struct query_params *qp, char *count, char *query)
{
  if (!count || !query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      goto failed;
    }

  qp->results = db_get_one_int(count);
  if (qp->results < 0)
    goto failed;

  sqlite3_free(count);

  return query;

 failed:
  sqlite3_free(count);
  sqlite3_free(query);
  return NULL;
}

static char *
db_build_query_items(struct query_params *qp)
{
  struct query_clause *qc;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  count = sqlite3_mprintf("SELECT COUNT(*) FROM files f %s;", qc->where);
  query = sqlite3_mprintf("SELECT f.* FROM files f %s %s %s;", qc->where, qc->order, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_pls(struct query_params *qp)
{
  struct query_clause *qc;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  count = sqlite3_mprintf("SELECT COUNT(*) FROM playlists f %s;", qc->where);
  query = sqlite3_mprintf("SELECT f.* FROM playlists f %s %s %s;", qc->where, qc->order, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_find_pls(struct query_params *qp)
{
  struct query_clause *qc;
  char *count;
  char *query;

  if (!qp->filter)
    {
      DPRINTF(E_LOG, L_DB, "Bug! Playlist find called without search criteria\n");
      return NULL;
    }

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  // Use qp->filter because qc->where has a f.disabled which is not a column in playlistitems
  count = sqlite3_mprintf("SELECT COUNT(*) FROM playlists f WHERE f.id IN (SELECT playlistid FROM playlistitems WHERE %s);", qp->filter);
  query = sqlite3_mprintf("SELECT f.* FROM playlists f WHERE f.id IN (SELECT playlistid FROM playlistitems WHERE %s) %s %s;", qp->filter, qc->order, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_plitems_plain(struct query_params *qp)
{
  struct query_clause *qc;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  count = sqlite3_mprintf("SELECT COUNT(*) FROM files f JOIN playlistitems pi ON f.path = pi.filepath %s AND pi.playlistid = %d;", qc->where, qp->id);
  query = sqlite3_mprintf("SELECT f.* FROM files f JOIN playlistitems pi ON f.path = pi.filepath %s AND pi.playlistid = %d ORDER BY pi.id ASC %s;", qc->where, qp->id, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_plitems_smart(struct query_params *qp, struct playlist_info *pli)
{
  struct query_clause *qc;
  char *count;
  char *query;
  bool free_orderby = false;

  if (pli->query_limit > 0)
    {
      if (qp->idx_type == I_SUB)
	{
	  if (pli->query_limit > qp->offset + qp->limit)
	    qp->limit = pli->query_limit;
	}
      else if (qp->idx_type == I_NONE)
	{
	  qp->idx_type = I_SUB;
	  qp->limit = pli->query_limit;
	  qp->offset = 0;
	}
      else
	{
	  DPRINTF(E_WARN, L_DB, "Cannot append limit from smart playlist '%s' to query\n", pli->path);
	}
    }

  if (pli->query_order)
    {
      if (!qp->order && qp->sort == S_NONE)
	{
	  qp->order = strdup(pli->query_order);
	  free_orderby = true;
	}
      else
	DPRINTF(E_WARN, L_DB, "Cannot append order by from smart playlist '%s' to query\n", pli->path);
    }

  qc = db_build_query_clause(qp);
  if (free_orderby)
    {
      free(qp->order);
      qp->order = NULL;
    }
  if (!qc)
    return NULL;

  count = sqlite3_mprintf("SELECT COUNT(*) FROM files f %s AND %s LIMIT %d;", qc->where, pli->query, pli->query_limit);
  query = sqlite3_mprintf("SELECT f.* FROM files f %s AND %s %s %s;", qc->where, pli->query, qc->order, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_plitems(struct query_params *qp)
{
  struct playlist_info *pli;
  char *query;

  if (qp->id <= 0)
  {
    DPRINTF(E_LOG, L_DB, "No playlist id specified in playlist items query\n");
    return NULL;
  }

  pli = db_pl_fetch_byid(qp->id);
  if (!pli)
    return NULL;

  switch (pli->type)
    {
      case PL_SPECIAL:
      case PL_SMART:
	query = db_build_query_plitems_smart(qp, pli);
	break;

      case PL_PLAIN:
      case PL_FOLDER:
	query = db_build_query_plitems_plain(qp);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unknown playlist type %d in playlist items query\n", pli->type);
	query = NULL;
	break;
    }

  free_pli(pli, 0);

  return query;
}

static char *
db_build_query_group_albums(struct query_params *qp)
{
  struct query_clause *qc;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  count = sqlite3_mprintf("SELECT COUNT(DISTINCT f.songalbumid) FROM files f %s;", qc->where);
  query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album, f.album_sort, COUNT(f.id) as track_count, 1 as album_count, f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songalbumid = g.persistentid %s GROUP BY f.songalbumid %s %s %s;", qc->where, qc->having, qc->order, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_group_artists(struct query_params *qp)
{
  struct query_clause *qc;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  count = sqlite3_mprintf("SELECT COUNT(DISTINCT f.songartistid) FROM files f %s;", qc->where);
  query = sqlite3_mprintf("SELECT g.id, g.persistentid, f.album_artist, f.album_artist_sort, COUNT(f.id) as track_count, COUNT(DISTINCT f.songalbumid) as album_count, f.album_artist, f.songartistid, SUM(f.song_length) FROM files f JOIN groups g ON f.songartistid = g.persistentid %s GROUP BY f.songartistid %s %s %s;", qc->where, qc->having, qc->order, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_group_items(struct query_params *qp)
{
  enum group_type gt;
  struct query_clause *qc;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  gt = db_group_type_bypersistentid(qp->persistentid);

  switch (gt)
    {
      case G_ALBUMS:
	count = sqlite3_mprintf("SELECT COUNT(*) FROM files f %s AND f.songalbumid = %" PRIi64 ";", qc->where, qp->persistentid);
	query = sqlite3_mprintf("SELECT f.* FROM files f %s AND f.songalbumid = %" PRIi64 " %s %s;", qc->where, qp->persistentid, qc->order, qc->index);
	break;

      case G_ARTISTS:
	count = sqlite3_mprintf("SELECT COUNT(*) FROM files f %s AND f.songartistid = %" PRIi64 ";", qc->where, qp->persistentid);
	query = sqlite3_mprintf("SELECT f.* FROM files f %s AND f.songartistid = %" PRIi64 " %s %s;", qc->where, qp->persistentid, qc->order, qc->index);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unsupported group type %d for group id %" PRIi64 "\n", gt, qp->persistentid);
        db_free_query_clause(qc);
	return NULL;
    }

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_group_dirs(struct query_params *qp)
{
  enum group_type gt;
  struct query_clause *qc;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  gt = db_group_type_bypersistentid(qp->persistentid);

  switch (gt)
    {
      case G_ALBUMS:
	count = sqlite3_mprintf("SELECT COUNT(DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1)))"
				" FROM files f %s AND f.songalbumid = %" PRIi64 ";", qc->where, qp->persistentid);
	query = sqlite3_mprintf("SELECT DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1))"
				" FROM files f %s AND f.songalbumid = %" PRIi64 " %s %s;", qc->where, qp->persistentid, qc->order, qc->index);
	break;

      case G_ARTISTS:
	count = sqlite3_mprintf("SELECT COUNT(DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1)))"
				" FROM files f %s AND f.songartistid = %" PRIi64 ";", qc->where, qp->persistentid);
	query = sqlite3_mprintf("SELECT DISTINCT(SUBSTR(f.path, 1, LENGTH(f.path) - LENGTH(f.fname) - 1))"
				" FROM files f %s AND f.songartistid = %" PRIi64 " %s %s;", qc->where, qp->persistentid, qc->order, qc->index);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "Unsupported group type %d for group id %" PRIi64 "\n", gt, qp->persistentid);
        db_free_query_clause(qc);
	return NULL;
    }

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_browse(struct query_params *qp)
{
  struct query_clause *qc;
  const char *where;
  const char *select;
  char *count;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  select = browse_clause[qp->type & ~Q_F_BROWSE].select;
  where  = browse_clause[qp->type & ~Q_F_BROWSE].where;

  count = sqlite3_mprintf("SELECT COUNT(*) FROM files f %s AND %s != '' %s;", qc->where, where, qc->group);
  query = sqlite3_mprintf("SELECT %s FROM files f %s AND %s != '' %s %s %s;", select, qc->where, where, qc->group, qc->order, qc->index);

  db_free_query_clause(qc);

  return db_build_query_check(qp, count, query);
}

static char *
db_build_query_count_items(struct query_params *qp)
{
  struct query_clause *qc;
  char *query;

  qc = db_build_query_clause(qp);
  if (!qc)
    return NULL;

  qp->results = 1;

  query = sqlite3_mprintf("SELECT COUNT(*), SUM(song_length), COUNT(DISTINCT songartistid), COUNT(DISTINCT songalbumid) FROM files f %s;", qc->where);
  if (!query)
    DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

  db_free_query_clause(qc);

  return query;
}

int
db_query_start(struct query_params *qp)
{
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  qp->stmt = NULL;
  qp->results = -1;

  switch (qp->type)
    {
      case Q_ITEMS:
	query = db_build_query_items(qp);
	break;

      case Q_PL:
	query = db_build_query_pls(qp);
	break;

      case Q_FIND_PL:
	query = db_build_query_find_pls(qp);
	break;

      case Q_PLITEMS:
	query = db_build_query_plitems(qp);
	break;

      case Q_GROUP_ALBUMS:
	query = db_build_query_group_albums(qp);
	break;

      case Q_GROUP_ARTISTS:
	query = db_build_query_group_artists(qp);
	break;

      case Q_GROUP_ITEMS:
	query = db_build_query_group_items(qp);
	break;

      case Q_GROUP_DIRS:
	query = db_build_query_group_dirs(qp);
	break;

      case Q_COUNT_ITEMS:
	query = db_build_query_count_items(qp);
	break;

      default:
	if (qp->type & Q_F_BROWSE)
	  query = db_build_query_browse(qp);
        else
	  query = NULL;
    }

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Could not create query, unknown type %d\n", qp->type);
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Starting query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  qp->stmt = stmt;

  return 0;
}

void
db_query_end(struct query_params *qp)
{
  if (!qp->stmt)
    return;

  sqlite3_finalize(qp->stmt);
  qp->stmt = NULL;
}

/*
 * Utility function for running write queries (INSERT, UPDATE, DELETE). If you
 * set free to non-zero, the function will free the query. If you set
 * library_update to non-zero it means that the update was not just of some
 * internal value (like a timestamp), but of something that requires clients
 * to update their cache of the library (and of course also of our own cache).
 */
static int
db_query_run(char *query, int free, short update_events)
{
  char *errmsg;
  int changes = 0;
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
  else
    changes = sqlite3_changes(hdl);

  sqlite3_free(errmsg);

  if (free)
    sqlite3_free(query);

  cache_daap_resume();

  if (update_events && changes > 0)
    library_update_trigger(update_events);

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

  if ((qp->type != Q_PL) && (qp->type != Q_FIND_PL))
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

  if (ARRAY_SIZE(dbpli_cols_map) > ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: dbpli column map out of sync with schema\n");
      return -1;
    }
  if (ARRAY_SIZE(dbpli_cols_map) < ncols)
    {
      DPRINTF(E_LOG, L_DB, "dbpli column map out of sync with schema, database schema does not match forked-daapd version!\n");
    }

  for (i = 0; i < ARRAY_SIZE(dbpli_cols_map); i++)
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
  fci->length = sqlite3_column_int64(qp->stmt, 1);
  fci->artist_count = sqlite3_column_int(qp->stmt, 2);
  fci->album_count = sqlite3_column_int(qp->stmt, 3);

  return 0;
}

int
db_filecount_get(struct filecount_info *fci, struct query_params *qp)
{
  int ret;

  ret = db_query_start(qp);
  if (ret < 0)
    {
      db_query_end(qp);

      return -1;
    }

  ret = db_query_fetch_count(qp, fci);
  if (ret < 0)
    {
      db_query_end(qp);

      return -1;
    }

  db_query_end(qp);
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
  return db_get_one_int("SELECT COUNT(*) FROM files f WHERE f.disabled = 0;");
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
db_file_inc_skipcount(int id)
{
#define Q_TMPL "UPDATE files SET skip_count = skip_count + 1, time_skipped = %" PRIi64 " WHERE id = %d;"
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

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_file_ping_bypath(const char *path, time_t mtime_max)
{
#define Q_TMPL "UPDATE files SET db_timestamp = %" PRIi64 ", disabled = 0 WHERE path = '%q' AND db_timestamp >= %" PRIi64 ";"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)time(NULL), path, (int64_t)mtime_max);

  ret = db_query_run(query, 1, 0);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}

void
db_file_ping_bymatch(const char *path, int isdir)
{
#define Q_TMPL_DIR "UPDATE files SET db_timestamp = %" PRIi64 " WHERE path LIKE '%q/%%';"
#define Q_TMPL_NODIR "UPDATE files SET db_timestamp = %" PRIi64 " WHERE path LIKE '%q%%';"
  char *query;

  if (isdir)
    query = sqlite3_mprintf(Q_TMPL_DIR, (int64_t)time(NULL), path);
  else
    query = sqlite3_mprintf(Q_TMPL_NODIR, (int64_t)time(NULL), path);

  db_query_run(query, 1, 0);
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
db_file_id_byquery(const char *query)
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
db_file_id_bypath(const char *path)
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
db_file_id_byfile(const char *filename)
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
db_file_id_byurl(const char *url)
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
db_file_id_by_virtualpath_match(const char *path)
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

  mfi = calloc(1, sizeof(struct media_file_info));
  if (!mfi)
    {
      DPRINTF(E_LOG, L_DB, "Could not allocate struct media_file_info, out of memory\n");
      return NULL;
    }

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
db_file_fetch_byvirtualpath(const char *virtual_path)
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
#define Q_TMPL "INSERT INTO files ("                                            \
                " id, path, fname, title, artist, album, genre, comment, type," \
                " composer, orchestra, conductor, grouping, url, bitrate,"      \
                " samplerate, song_length, file_size, year, track,"             \
                " total_tracks, disc, total_discs, bpm, compilation, artwork,"  \
                " rating, play_count, seek, data_kind, item_kind, description," \
                " time_added, time_modified, time_played, db_timestamp,"        \
                " disabled, sample_count, codectype, idx, has_video,"           \
                " contentrating, bits_per_sample, album_artist, media_kind,"    \
                " tv_series_name, tv_episode_num_str, tv_network_name,"         \
                " tv_episode_sort, tv_season_num, songartistid, songalbumid,"   \
                " title_sort, artist_sort, album_sort, composer_sort,"          \
                " album_artist_sort, virtual_path, directory_id, date_released,"\
                " skip_count, time_skipped"                                     \
               ") VALUES ("                                                     \
                " NULL, '%q', '%q', TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), %Q," \
                " TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q), %Q, %d,"              \
                " %d, %d, %" PRIi64 ", %d, %d,"                                 \
                " %d, %d, %d, %d, %d, %d,"                                      \
                " %d, %d, %d, %d, %d, %Q,"                                      \
                " %" PRIi64 ", %" PRIi64 ", %" PRIi64 ", %" PRIi64 ","          \
                " %d, %" PRIi64 ", %Q, %d, %d,"                                 \
                " %d, %d, TRIM(%Q), %d,"                                        \
                " TRIM(%Q), TRIM(%Q), TRIM(%Q),"                                \
                " %d, %d, daap_songalbumid(LOWER(TRIM(%Q)), ''), daap_songalbumid(LOWER(TRIM(%Q)), LOWER(TRIM(%Q)))," \
                " TRIM(%Q), TRIM(%Q), TRIM(%Q), TRIM(%Q),"                      \
                " TRIM(%Q), TRIM(%Q), %d, %d,"                                  \
                " %d, %" PRIi64 ""                                              \
               ");"

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
                  STR(mfi->path), STR(mfi->fname), mfi->title, mfi->artist, mfi->album, mfi->genre, mfi->comment, mfi->type,
                  mfi->composer, mfi->orchestra, mfi->conductor, mfi->grouping, mfi->url, mfi->bitrate,
                  mfi->samplerate, mfi->song_length, mfi->file_size, mfi->year, mfi->track,
                  mfi->total_tracks, mfi->disc, mfi->total_discs, mfi->bpm, mfi->compilation, mfi->artwork,
                  mfi->rating, mfi->play_count, mfi->seek, mfi->data_kind, mfi->item_kind, mfi->description,
                  (int64_t)mfi->time_added, (int64_t)mfi->time_modified, (int64_t)mfi->time_played, (int64_t)mfi->db_timestamp,
                  mfi->disabled, mfi->sample_count, mfi->codectype, mfi->index, mfi->has_video,
                  mfi->contentrating, mfi->bits_per_sample, mfi->album_artist, mfi->media_kind,
                  mfi->tv_series_name, mfi->tv_episode_num_str, mfi->tv_network_name,
                  mfi->tv_episode_sort, mfi->tv_season_num, mfi->album_artist, mfi->album_artist, mfi->album,
                  mfi->title_sort, mfi->artist_sort, mfi->album_sort, mfi->composer_sort,
                  mfi->album_artist_sort, mfi->virtual_path, mfi->directory_id, mfi->date_released,
                  mfi->skip_count, mfi->time_skipped);

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

  library_update_trigger(LISTENER_DATABASE);

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
                " virtual_path = TRIM(%Q), directory_id = %d, date_released = %d, skip_count = %d, time_skipped = %" PRIi64 "" \
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
			  mfi->virtual_path, mfi->directory_id, mfi->date_released, mfi->skip_count, mfi->time_skipped,
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

  library_update_trigger(LISTENER_DATABASE);

  return 0;

#undef Q_TMPL
}

void
db_file_seek_update(int id, uint32_t seek)
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

int
db_file_rating_update_byid(uint32_t id, uint32_t rating)
{
#define Q_TMPL "UPDATE files SET rating = %d WHERE id = %d;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, rating, id);

  ret = db_query_run(query, 1, 0);

  if (ret == 0)
    listener_notify(LISTENER_RATING);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}

int
db_file_rating_update_byvirtualpath(const char *virtual_path, uint32_t rating)
{
#define Q_TMPL "UPDATE files SET rating = %d WHERE virtual_path = %Q;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, rating, virtual_path);

  ret = db_query_run(query, 1, 0);

  if (ret == 0)
    listener_notify(LISTENER_RATING);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}

void
db_file_delete_bypath(const char *path)
{
#define Q_TMPL "DELETE FROM files WHERE path = '%q';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, path);

  db_query_run(query, 1, LISTENER_DATABASE);
#undef Q_TMPL
}

void
db_file_disable_bypath(const char *path, enum strip_type strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE files SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path = '%q';"
  char *query;
  int64_t disabled;
  int path_striplen;
  int vpath_striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;

  path_striplen = (strip == STRIP_PATH) ? strlen(path) : 0;
  vpath_striplen = (strip == STRIP_PATH) ? strlen("/file:") + path_striplen : 0;

  query = sqlite3_mprintf(Q_TMPL, path_striplen + 1, vpath_striplen + 1, disabled, path);

  db_query_run(query, 1, LISTENER_DATABASE);
#undef Q_TMPL
}

void
db_file_disable_bymatch(const char *path, enum strip_type strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE files SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path LIKE '%q/%%';"
  char *query;
  int64_t disabled;
  int path_striplen;
  int vpath_striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;

  path_striplen = (strip == STRIP_PATH) ? strlen(path) : 0;
  vpath_striplen = (strip == STRIP_PATH) ? strlen("/file:") + path_striplen : 0;

  query = sqlite3_mprintf(Q_TMPL, path_striplen + 1, vpath_striplen + 1, disabled, path);

  db_query_run(query, 1, LISTENER_DATABASE);
#undef Q_TMPL
}

// "path" will be the directory part for directory updates (dir moved) and the
// full path for file updates (file moved). The db will have the filename in 
// the path field for the former case (with a "/" prefix), and an empty path
// field for the latter.
int
db_file_enable_bycookie(uint32_t cookie, const char *path, const char *filename)
{
#define Q_TMPL_UPDATE_FNAME "UPDATE files SET path = ('%q' || path), virtual_path = ('/file:%q' || virtual_path), fname = '%q', disabled = 0 WHERE disabled = %" PRIi64 ";"
#define Q_TMPL "UPDATE files SET path = ('%q' || path), virtual_path = ('/file:%q' || virtual_path), disabled = 0 WHERE disabled = %" PRIi64 ";"
  char *query;
  int ret;

  if (filename)
    query = sqlite3_mprintf(Q_TMPL_UPDATE_FNAME, path, path, filename, (int64_t)cookie);
  else
    query = sqlite3_mprintf(Q_TMPL, path, path, (int64_t)cookie);

  ret = db_query_run(query, 1, LISTENER_DATABASE);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL_UPDATE_FNAME
#undef Q_TMPL
}

int
db_file_update_directoryid(const char *path, int dir_id)
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
  return db_get_one_int("SELECT COUNT(*) FROM playlists p WHERE p.disabled = 0;");
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

  ret = db_get_one_int(query);

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

  ret = db_get_one_int(query);

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
db_pl_ping_bymatch(const char *path, int isdir)
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

void
db_pl_ping_items_bymatch(const char *path, int id)
{
#define Q_TMPL "UPDATE files SET db_timestamp = %" PRIi64 ", disabled = 0 WHERE path IN (SELECT filepath FROM playlistitems WHERE filepath LIKE '%q%%' AND playlistid = %d);"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, (int64_t)time(NULL), path, id);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_pl_id_bypath(const char *path)
{
#define Q_TMPL "SELECT p.id FROM playlists p WHERE p.path = '%q';"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  ret = db_get_one_int(query);

  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

static struct playlist_info *
db_pl_fetch_byquery(const char *query)
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

  pli = calloc(1, sizeof(struct playlist_info));
  if (!pli)
    {
      DPRINTF(E_LOG, L_DB, "Could not allocate struct playlist_info, out of memory\n");
      return NULL;
    }

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

  if (ARRAY_SIZE(pli_cols_map) > ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: pli column map out of sync with schema\n");

      sqlite3_finalize(stmt);
      free(pli);
      return NULL;
    }
  if (ARRAY_SIZE(pli_cols_map) < ncols)
    {
      DPRINTF(E_LOG, L_DB, "BUG: pli column map out of sync with schema\n");
    }

  for (i = 0; i < ARRAY_SIZE(pli_cols_map); i++)
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
db_pl_fetch_bypath(const char *path)
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
db_pl_fetch_byvirtualpath(const char *virtual_path)
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
db_pl_fetch_bytitlepath(const char *title, const char *path)
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
#define QADD_TMPL "INSERT INTO playlists (title, type, query, db_timestamp, disabled, path, idx, special_id, " \
                  " parent_id, virtual_path, directory_id, query_order, query_limit)" \
                  " VALUES (TRIM(%Q), %d, '%q', %" PRIi64 ", %d, '%q', %d, %d, %d, '%q', %d, %Q, %d);"
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

  ret = db_get_one_int(query);

  sqlite3_free(query);

  if (ret > 0)
    {
      DPRINTF(E_WARN, L_DB, "Duplicate playlist with title '%s' path '%s'\n", pli->title, pli->path);
      return -1;
    }

  /* Add */
  query = sqlite3_mprintf(QADD_TMPL,
			  pli->title, pli->type, pli->query, (int64_t)time(NULL), pli->disabled, STR(pli->path),
			  pli->index, pli->special_id, pli->parent_id, pli->virtual_path, pli->directory_id,
			  pli->query_order, pli->query_limit);

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
db_pl_add_item_bypath(int plid, const char *path)
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
               " path = '%q', idx = %d, special_id = %d, parent_id = %d, virtual_path = '%q', directory_id = %d, " \
               " query_order = %Q, query_limit = %d " \
               " WHERE id = %d;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL,
			  pli->title, pli->type, pli->query, (int64_t)time(NULL), pli->disabled, STR(pli->path),
			  pli->index, pli->special_id, pli->parent_id, pli->virtual_path, pli->directory_id,
			  pli->query_order, pli->query_limit, pli->id);

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
db_pl_delete_bypath(const char *path)
{
  int i;
  int ret;
  char *query;
  char *queries_tmpl[] =
    {
      "DELETE FROM playlistitems WHERE playlistid IN (SELECT id FROM playlists WHERE path = '%q');",
      "DELETE FROM playlists WHERE path = '%q';",
    };

  for (i = 0; i < (sizeof(queries_tmpl) / sizeof(queries_tmpl[0])); i++)
    {
      query = sqlite3_mprintf(queries_tmpl[i], path);

      ret = db_query_run(query, 1, 0);
      if (ret == 0)
	DPRINTF(E_DBG, L_DB, "Purged %d rows\n", sqlite3_changes(hdl));
    }
}

void
db_pl_disable_bypath(const char *path, enum strip_type strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE playlists SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path = '%q';"
  char *query;
  int64_t disabled;
  int path_striplen;
  int vpath_striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;

  path_striplen = (strip == STRIP_PATH) ? strlen(path) : 0;
  vpath_striplen = (strip == STRIP_PATH) ? strlen("/file:") + path_striplen : 0;

  query = sqlite3_mprintf(Q_TMPL, path_striplen + 1, vpath_striplen + 1, disabled, path);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_pl_disable_bymatch(const char *path, enum strip_type strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE playlists SET path = substr(path, %d), virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE path LIKE '%q/%%';"
  char *query;
  int64_t disabled;
  int path_striplen;
  int vpath_striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;

  path_striplen = (strip == STRIP_PATH) ? strlen(path) : 0;
  vpath_striplen = (strip == STRIP_PATH) ? strlen("/file:") + path_striplen : 0;

  query = sqlite3_mprintf(Q_TMPL, path_striplen + 1, vpath_striplen + 1, disabled, path);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_pl_enable_bycookie(uint32_t cookie, const char *path)
{
#define Q_TMPL "UPDATE playlists SET path = ('%q' || path), virtual_path = ('/file:%q' || virtual_path), disabled = 0 WHERE disabled = %" PRIi64 ";"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path, path, (int64_t)cookie);

  ret = db_query_run(query, 1, 0);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}



/* Groups */

// Remove album and artist entries in the groups table that are not longer referenced from the files table
int
db_groups_cleanup()
{
#define Q_TMPL_ALBUM "DELETE FROM groups WHERE type = 1 AND NOT persistentid IN (SELECT songalbumid from files WHERE disabled = 0);"
#define Q_TMPL_ARTIST "DELETE FROM groups WHERE type = 2 AND NOT persistentid IN (SELECT songartistid from files WHERE disabled = 0);"
  int ret;

  db_transaction_begin();

  ret = db_query_run(Q_TMPL_ALBUM, 0, LISTENER_DATABASE);
  if (ret < 0)
    {
      db_transaction_rollback();
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Removed album group-entries: %d\n", sqlite3_changes(hdl));

  ret = db_query_run(Q_TMPL_ARTIST, 0, LISTENER_DATABASE);
  if (ret < 0)
    {
      db_transaction_rollback();
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Removed artist group-entries: %d\n", sqlite3_changes(hdl));
  db_transaction_end();

  return 0;

#undef Q_TMPL_ALBUM
#undef Q_TMPL_ARTIST
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
  sqlite3_stmt *stmt;
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

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  de->stmt = stmt;

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
  int vp_len = strlen(di->virtual_path);
  int ret;

  if (vp_len && di->virtual_path[vp_len-1] == ' ')
    {
      /* Since sqlite removes the trailing space, so these
       * directories will be found as new in perpetuity.
       */
      DPRINTF(E_LOG, L_DB, "Directory name ends with space: '%s'\n", di->virtual_path);
    }

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

  DPRINTF(E_DBG, L_DB, "Added directory '%s' with id %d\n", di->virtual_path, *id);

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

  DPRINTF(E_DBG, L_DB, "Updated directory '%s' with id %d\n", di->virtual_path, di->id);

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
db_directory_ping_bymatch(char *virtual_path)
{
#define Q_TMPL_DIR "UPDATE directories SET db_timestamp = %" PRIi64 " WHERE virtual_path = '%q' OR virtual_path LIKE '%q/%%';"
  char *query;

  query = sqlite3_mprintf(Q_TMPL_DIR, (int64_t)time(NULL), virtual_path, virtual_path);

  db_query_run(query, 1, 0);
#undef Q_TMPL_DIR
}

void
db_directory_disable_bymatch(char *path, enum strip_type strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE directories SET virtual_path = substr(virtual_path, %d), disabled = %" PRIi64 " WHERE virtual_path = '/file:%q' OR virtual_path LIKE '/file:%q/%%';"
  char *query;
  int64_t disabled;
  int vpath_striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;

  vpath_striplen = (strip == STRIP_PATH) ? strlen("/file:") + strlen(path) : 0;

  query = sqlite3_mprintf(Q_TMPL, vpath_striplen + 1, disabled, path, path, path);

  db_query_run(query, 1, LISTENER_DATABASE);
#undef Q_TMPL
}

int
db_directory_enable_bycookie(uint32_t cookie, char *path)
{
#define Q_TMPL "UPDATE directories SET virtual_path = ('/file:%q' || virtual_path), disabled = 0 WHERE disabled = %" PRIi64 ";"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path, (int64_t)cookie);

  ret = db_query_run(query, 1, LISTENER_DATABASE);

  return ((ret < 0) ? -1 : sqlite3_changes(hdl));
#undef Q_TMPL
}

int
db_directory_enable_bypath(char *path)
{
#define Q_TMPL "UPDATE directories SET disabled = 0 WHERE virtual_path = %Q AND disabled <> 0;"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, path);

  ret = db_query_run(query, 1, LISTENER_DATABASE);

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
#define Q_TMPL "UPDATE directories SET disabled = %" PRIi64 " WHERE virtual_path = '/spotify:' AND disabled <> %" PRIi64 ";"
  char *queries[4] =
    {
      "DELETE FROM files WHERE path LIKE 'spotify:%%';",
      "DELETE FROM playlistitems WHERE filepath LIKE 'spotify:%%';",
      "DELETE FROM playlists WHERE path LIKE 'spotify:%%';",
      "DELETE FROM directories WHERE virtual_path LIKE '/spotify:/%%';",
    };
  char *query;
  int i;
  int ret;

  for (i = 0; i < (sizeof(queries) / sizeof(queries[0])); i++)
    {
      ret = db_query_run(queries[i], 0, LISTENER_DATABASE);

      if (ret == 0)
	DPRINTF(E_DBG, L_DB, "Processed %d rows\n", sqlite3_changes(hdl));
    }

  // Disable the spotify directory by setting 'disabled' to INOTIFY_FAKE_COOKIE value
  query = sqlite3_mprintf(Q_TMPL, INOTIFY_FAKE_COOKIE, INOTIFY_FAKE_COOKIE);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");
      return;
    }
  ret = db_query_run(query, 1, LISTENER_DATABASE);

  if (ret == 0)
    DPRINTF(E_DBG, L_DB, "Disabled spotify directory\n");

#undef Q_TMPL
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

      ret = db_query_run(query, 1, LISTENER_DATABASE);

      if (ret == 0)
	DPRINTF(E_DBG, L_DB, "Deleted %d rows\n", sqlite3_changes(hdl));
    }
}

/* Spotify */
void
db_spotify_files_delete(void)
{
#define Q_TMPL "DELETE FROM files WHERE path LIKE 'spotify:%%' AND NOT path IN (SELECT filepath FROM playlistitems);"
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL);

  ret = db_query_run(query, 1, LISTENER_DATABASE);

  if (ret == 0)
    DPRINTF(E_DBG, L_DB, "Deleted %d rows\n", sqlite3_changes(hdl));
#undef Q_TMPL
}
#endif

/* Admin */
int
db_admin_set(const char *key, const char *value)
{
#define Q_TMPL "INSERT OR REPLACE INTO admin (key, value) VALUES ('%q', '%q');"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, key, value);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_admin_setint(const char *key, int value)
{
#define Q_TMPL "INSERT OR REPLACE INTO admin (key, value) VALUES ('%q', '%d');"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, key, value);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_admin_setint64(const char *key, int64_t value)
{
#define Q_TMPL "INSERT OR REPLACE INTO admin (key, value) VALUES ('%q', '%" PRIi64 "');"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, key, value);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

static int
admin_get(const char *key, short type, void *value)
{
#define Q_TMPL "SELECT value FROM admin a WHERE a.key = '%q';"
  char *query;
  sqlite3_stmt *stmt;
  char *cval;
  int32_t *ival;
  int64_t *i64val;
  char **strval;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, key);
  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Running query '%s'\n", query);

  ret = db_blocking_prepare_v2(query, strlen(query) + 1, &stmt, NULL);
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

  switch (type)
    {
      case DB_TYPE_CHAR:
	cval = (char *) value;
	*cval = sqlite3_column_int(stmt, 0);
	break;

      case DB_TYPE_INT:
	ival = (int32_t *) value;

	*ival = sqlite3_column_int(stmt, 0);
	break;

      case DB_TYPE_INT64:
	i64val = (int64_t *) value;

	*i64val = sqlite3_column_int64(stmt, 0);
	break;

      case DB_TYPE_STRING:
	strval = (char **) value;

	cval = (char *)sqlite3_column_text(stmt, 0);
	if (cval)
	  *strval = strdup(cval);
	break;

      default:
	DPRINTF(E_LOG, L_DB, "BUG: Unknown type %d in admin_set\n", type);

	ret = -2;
    }

#ifdef DB_PROFILE
  while (db_blocking_step(stmt) == SQLITE_ROW)
    ; /* EMPTY */
#endif

  sqlite3_finalize(stmt);
  sqlite3_free(query);

  return ret;

#undef Q_TMPL
}

char *
db_admin_get(const char *key)
{
  char *value = NULL;
  int ret;

  ret = admin_get(key, DB_TYPE_STRING, &value);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_DB, "Could not find key '%s' in admin table\n", key);
      return NULL;
    }

  return value;
}

int
db_admin_getint(const char *key)
{
  int value = 0;
  int ret;

  ret = admin_get(key, DB_TYPE_INT, &value);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_DB, "Could not find key '%s' in admin table\n", key);
      return 0;
    }

  return value;
}

int64_t
db_admin_getint64(const char *key)
{
  int64_t value = 0;
  int ret;

  ret = admin_get(key, DB_TYPE_INT64, &value);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_DB, "Could not find key '%s' in admin table\n", key);
      return 0;
    }

  return value;
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
db_speaker_save(struct output_device *device)
{
#define Q_TMPL "INSERT OR REPLACE INTO speakers (id, selected, volume, name, auth_key) VALUES (%" PRIi64 ", %d, %d, %Q, %Q);"
  char *query;

  query = sqlite3_mprintf(Q_TMPL, device->id, device->selected, device->volume, device->name, device->auth_key);

  return db_query_run(query, 1, 0);
#undef Q_TMPL
}

int
db_speaker_get(struct output_device *device, uint64_t id)
{
#define Q_TMPL "SELECT s.selected, s.volume, s.name, s.auth_key FROM speakers s WHERE s.id = %" PRIi64 ";"
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

  device->id = id;
  device->selected = sqlite3_column_int(stmt, 0);
  device->volume = sqlite3_column_int(stmt, 1);

  free(device->name);
  device->name = safe_strdup((char *)sqlite3_column_text(stmt, 2));

  free(device->auth_key);
  device->auth_key = safe_strdup((char *)sqlite3_column_text(stmt, 3));

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

/* Queue */

/*
 * Start a new transaction for modifying the queue. Returns the new queue version for the following changes.
 * After finishing all queue modifications 'queue_transaction_end' needs to be called.
 */
static int
queue_transaction_begin()
{
  int queue_version;

  db_transaction_begin();

  queue_version = db_admin_getint(DB_ADMIN_QUEUE_VERSION);
  queue_version++;

  return queue_version;
}

/*
 * If retval == 0, updates the version of the queue in the admin table, commits the transaction
 * and notifies listener of LISTENER_QUEUE about the changes.
 * If retval < 0, rollsback the transaction.
 *
 * This function must be called after modifying the queue.
 *
 * @param retval 'retval' == 0, if modifying the queue was successful or 'retval' < 0 if an error occurred
 * @param queue_version The new queue version, for the pending modifications
 */
static void
queue_transaction_end(int retval, int queue_version)
{
  int ret;

  if (retval != 0)
    goto error;

  ret = db_admin_setint(DB_ADMIN_QUEUE_VERSION, queue_version);
  if (ret < 0)
    goto error;

  db_transaction_end();
  listener_notify(LISTENER_QUEUE);
  return;

 error:
  db_transaction_rollback();
}

static int
queue_reshuffle(uint32_t item_id, int queue_version);

static int
queue_add_file(struct db_media_file_info *dbmfi, int pos, int shuffle_pos, int queue_version)
{
#define Q_TMPL "INSERT INTO queue "							\
		    "(id, file_id, song_length, data_kind, media_kind, "		\
		    "pos, shuffle_pos, path, virtual_path, title, "			\
		    "artist, album_artist, album, genre, songalbumid, "			\
		    "time_modified, artist_sort, album_sort, album_artist_sort, year, "	\
		    "track, disc, queue_version)" 					\
		"VALUES"                                           			\
		    "(NULL, %s, %s, %s, %s, "						\
		    "%d, %d, %Q, %Q, %Q, "						\
		    "%Q, %Q, %Q, %Q, %s, "						\
		    "%s, %Q, %Q, %Q, %s, "						\
		    "%s, %s, %d);"

  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL,
			  dbmfi->id, dbmfi->song_length, dbmfi->data_kind, dbmfi->media_kind,
			  pos, pos, dbmfi->path, dbmfi->virtual_path, dbmfi->title,
			  dbmfi->artist, dbmfi->album_artist, dbmfi->album, dbmfi->genre, dbmfi->songalbumid,
			  dbmfi->time_modified, dbmfi->artist_sort, dbmfi->album_sort, dbmfi->album_artist_sort, dbmfi->year,
			  dbmfi->track, dbmfi->disc, queue_version);
  ret = db_query_run(query, 1, 0);

  return ret;

#undef Q_TMPL
}

static int
queue_add_item(struct db_queue_item *item, int pos, int shuffle_pos, int queue_version)
{
#define Q_TMPL "INSERT INTO queue "							\
		    "(id, file_id, song_length, data_kind, media_kind, "		\
		    "pos, shuffle_pos, path, virtual_path, title, "			\
		    "artist, album_artist, album, genre, songalbumid, "			\
		    "time_modified, artist_sort, album_sort, album_artist_sort, year, "	\
		    "track, disc, queue_version)" 					\
		"VALUES"                                           			\
		    "(NULL, %d, %d, %d, %d, "						\
		    "%d, %d, %Q, %Q, %Q, "						\
		    "%Q, %Q, %Q, %Q, %" PRIi64 ", "					\
		    "%d, %Q, %Q, %Q, %d, "						\
		    "%d, %d, %d);"

  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL,
			  item->file_id, item->song_length, item->data_kind, item->media_kind,
			  pos, pos, item->path, item->virtual_path, item->title,
			  item->artist, item->album_artist, item->album, item->genre, item->songalbumid,
			  item->time_modified, item->artist_sort, item->album_sort, item->album_artist_sort, item->year,
			  item->track, item->disc, queue_version);
  ret = db_query_run(query, 1, 0);

  return ret;

#undef Q_TMPL
}

int
db_queue_update_item(struct db_queue_item *qi)
{
#define Q_TMPL "UPDATE queue SET "							\
		    "file_id = %d, song_length = %d, data_kind = %d, media_kind = %d, "	\
		    "pos = %d, shuffle_pos = %d, path = '%q', virtual_path = %Q, "	\
		    "title = %Q, artist = %Q, album_artist = %Q, album = %Q, "		\
		    "genre = %Q, songalbumid = %" PRIi64 ", time_modified = %d, "	\
		    "artist_sort = %Q, album_sort = %Q, album_artist_sort = %Q, "	\
		    "year = %d, track = %d, disc = %d, artwork_url = %Q, "		\
		    "queue_version = %d "						\
		"WHERE id = %d;"

  int queue_version;
  char *query;
  int ret;

  queue_version = queue_transaction_begin();

  query = sqlite3_mprintf(Q_TMPL,
			  qi->file_id, qi->song_length, qi->data_kind, qi->media_kind,
			  qi->pos, qi->shuffle_pos, qi->path, qi->virtual_path,
			  qi->title, qi->artist, qi->album_artist, qi->album,
			  qi->genre, qi->songalbumid, qi->time_modified,
			  qi->artist_sort, qi->album_sort, qi->album_artist_sort,
			  qi->year, qi->track, qi->disc, qi->artwork_url, queue_version,
			  qi->id);

  ret = db_query_run(query, 1, 0);

  /* MPD changes playlist version when metadata changes */
  queue_transaction_end(ret, queue_version);

  return ret;
#undef Q_TMPL
}

/*
 * Adds the files matching the given query to the queue after the item with the given item id
 *
 * The files table is queried with the given parameters and all found files are added after the
 * item with the given item id to the "normal" queue. They are appended to end of the shuffled queue
 * (assuming that the shuffled queue will get reshuffled after adding new items).
 *
 * The function returns -1 on failure (e. g. error reading from database) and if the given item id
 * does not exist. It wraps all database access in a transaction and performs a rollback if an error
 * occurs, leaving the queue in a consistent state.
 *
 * @param qp Query parameters for the files table
 * @param item_id Files are added after item with this id
 * @return 0 on success, -1 on failure
 */
int
db_queue_add_by_queryafteritemid(struct query_params *qp, uint32_t item_id)
{
  int queue_version;
  struct db_media_file_info dbmfi;
  char *query;
  int shuffle_pos;
  int pos;
  int ret;

  queue_version = queue_transaction_begin();

  // Position of the first new item
  pos = db_queue_get_pos(item_id, 0);
  if (pos < 0)
    {
      ret = -1;
      goto end_transaction;
    }
  pos++;

  // Shuffle position of the first new item
  shuffle_pos = db_queue_get_count();
  if (shuffle_pos < 0)
    {
      ret = -1;
      goto end_transaction;
    }

  // Start query for new items from files table
  ret = db_query_start(qp);
  if (ret < 0)
    goto end_transaction;

  DPRINTF(E_DBG, L_DB, "Player queue query returned %d items\n", qp->results);

  // Update pos for all items after the item with item_id
  query = sqlite3_mprintf("UPDATE queue SET pos = pos + %d, queue_version = %d WHERE pos > %d;", qp->results, queue_version, (pos - 1));
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  // Iterate over new items from files table and insert into queue
  while (((ret = db_query_fetch_file(qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = queue_add_file(&dbmfi, pos, shuffle_pos, queue_version);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Failed to add song with id %s (%s) to queue\n", dbmfi.id, dbmfi.title);
	  break;
	}

      DPRINTF(E_DBG, L_DB, "Added song with id %s (%s) to queue\n", dbmfi.id, dbmfi.title);
      shuffle_pos++;
      pos++;
    }

  db_query_end(qp);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;
}

int
db_queue_add_start(struct db_queue_add_info *queue_add_info)
{
  int ret = 0;

  memset(queue_add_info, 0, sizeof(struct db_queue_add_info));
  queue_add_info->queue_version = queue_transaction_begin();

  queue_add_info->pos = db_queue_get_count();
  if (queue_add_info->pos < 0)
    {
      ret = -1;
      queue_transaction_end(ret, queue_add_info->queue_version);
      return ret;
    }

   return 0;
}

void
db_queue_add_end(struct db_queue_add_info *queue_add_info, int ret)
{
  queue_transaction_end(ret, queue_add_info->queue_version);
}

int
db_queue_add_item(struct db_queue_add_info *queue_add_info, struct db_queue_item *item)
{
  int ret;

  fixup_tags_queue_item(item);
  ret = queue_add_item(item, queue_add_info->pos, queue_add_info->pos, queue_add_info->queue_version);
  if (ret == 0)
    queue_add_info->pos++;

  return ret;

#undef Q_TMPL
}

/*
 * Adds the files matching the given query to the queue
 *
 * The files table is queried with the given parameters and all found files are added to the end of the
 * "normal" queue and the shuffled queue.
 *
 * The function returns -1 on failure (e. g. error reading from database). It wraps all database access
 * in a transaction and performs a rollback if an error occurs, leaving the queue in a consistent state.
 *
 * @param qp Query parameters for the files table
 * @param reshuffle If 1 queue will be reshuffled after adding new items
 * @param item_id The base item id, all items after this will be reshuffled
 * @return Item id of the last inserted item on success, -1 on failure
 */
int
db_queue_add_by_query(struct query_params *qp, char reshuffle, uint32_t item_id)
{
  struct db_media_file_info dbmfi;
  int queue_version;
  int pos;
  int new_item_id = 0; // Quell compiler warning about uninitialized use of new_item_id
  int ret;

  queue_version = queue_transaction_begin();

  pos = db_queue_get_count();
  if (pos < 0)
    {
      ret = -1;
      goto end_transaction;
    }

  ret = db_query_start(qp);
  if (ret < 0)
    goto end_transaction;

  DPRINTF(E_DBG, L_DB, "Player queue query returned %d items\n", qp->results);

  if (qp->results == 0)
    {
      db_query_end(qp);
      db_transaction_end();
      return 0;
    }

  while (((ret = db_query_fetch_file(qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = queue_add_file(&dbmfi, pos, pos, queue_version);

      if (ret < 0)
	{
	  DPRINTF(E_DBG, L_DB, "Failed to add song id %s (%s)\n", dbmfi.id, dbmfi.title);
	  break;
	}

      DPRINTF(E_DBG, L_DB, "Added song id %s (%s) to queue\n", dbmfi.id, dbmfi.title);
      pos++;
    }

  db_query_end(qp);

  if (ret < 0)
    goto end_transaction;

  new_item_id = (int) sqlite3_last_insert_rowid(hdl);

  // Reshuffle after adding new items
  if (reshuffle)
    {
      ret = queue_reshuffle(item_id, queue_version);
    }

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return (ret == 0) ? new_item_id : ret;
}

/*
 * Adds the items of the stored playlist with the given id to the end of the queue
 *
 * @param plid Id of the stored playlist
 * @param reshuffle If 1 queue will be reshuffled after adding new items
 * @param item_id The base item id, all items after this will be reshuffled
 * @return Item id of the last inserted item on success, -1 on failure
 */
int
db_queue_add_by_playlistid(int plid, char reshuffle, uint32_t item_id)
{
  struct query_params qp;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  qp.id = plid;
  qp.type = Q_PLITEMS;

  ret = db_queue_add_by_query(&qp, reshuffle, item_id);

  return ret;
}

/*
 * Adds the file with the given id to the queue
 *
 * @param id Id of the file
 * @param reshuffle If 1 queue will be reshuffled after adding new items
 * @param item_id The base item id, all items after this will be reshuffled
 * @return Item id of the last inserted item on success, -1 on failure
 */
int
db_queue_add_by_fileid(int id, char reshuffle, uint32_t item_id)
{
  struct query_params qp;
  char buf[124];
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_ITEMS;
  snprintf(buf, sizeof(buf), "f.id = %" PRIu32, id);
  qp.filter = buf;

  ret = db_queue_add_by_query(&qp, reshuffle, item_id);

  return ret;
}

static int
queue_enum_start(struct query_params *qp)
{
#define Q_TMPL "SELECT * FROM queue f WHERE %s ORDER BY %s;"
  sqlite3_stmt *stmt;
  char *query;
  const char *orderby;
  int ret;

  qp->stmt = NULL;

  if (qp->sort)
    orderby = sort_clause[qp->sort];
  else
    orderby = sort_clause[S_POS];

  if (qp->filter)
    query = sqlite3_mprintf(Q_TMPL, qp->filter, orderby);
  else
    query = sqlite3_mprintf(Q_TMPL, "1=1", orderby);

  if (!query)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for query string\n");

      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Starting enum '%s'\n", query);

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  qp->stmt = stmt;

  return 0;

#undef Q_TMPL
}

static inline char *
strdup_if(char *str, int cond)
{
  if (str == NULL)
    return NULL;

  if (cond)
    return strdup(str);

  return str;
}

static int
queue_enum_fetch(struct query_params *qp, struct db_queue_item *queue_item, int keep_item)
{
  int ret;

  memset(queue_item, 0, sizeof(struct db_queue_item));

  if (!qp->stmt)
    {
      DPRINTF(E_LOG, L_DB, "Queue enum not started!\n");
      return -1;
    }

  ret = db_blocking_step(qp->stmt);
  if (ret == SQLITE_DONE)
    {
      DPRINTF(E_DBG, L_DB, "End of queue enum results\n");
      return 0;
    }
  else if (ret != SQLITE_ROW)
    {
      DPRINTF(E_LOG, L_DB, "Could not step: %s\n", sqlite3_errmsg(hdl));
      return -1;
    }

  queue_item->id = (uint32_t)sqlite3_column_int(qp->stmt, 0);
  queue_item->file_id = (uint32_t)sqlite3_column_int(qp->stmt, 1);
  queue_item->pos = (uint32_t)sqlite3_column_int(qp->stmt, 2);
  queue_item->shuffle_pos = (uint32_t)sqlite3_column_int(qp->stmt, 3);
  queue_item->data_kind = sqlite3_column_int(qp->stmt, 4);
  queue_item->media_kind = sqlite3_column_int(qp->stmt, 5);
  queue_item->song_length = (uint32_t)sqlite3_column_int(qp->stmt, 6);
  queue_item->path = strdup_if((char *)sqlite3_column_text(qp->stmt, 7), keep_item);
  queue_item->virtual_path = strdup_if((char *)sqlite3_column_text(qp->stmt, 8), keep_item);
  queue_item->title = strdup_if((char *)sqlite3_column_text(qp->stmt, 9), keep_item);
  queue_item->artist = strdup_if((char *)sqlite3_column_text(qp->stmt, 10), keep_item);
  queue_item->album_artist = strdup_if((char *)sqlite3_column_text(qp->stmt, 11), keep_item);
  queue_item->album = strdup_if((char *)sqlite3_column_text(qp->stmt, 12), keep_item);
  queue_item->genre = strdup_if((char *)sqlite3_column_text(qp->stmt, 13), keep_item);
  queue_item->songalbumid = sqlite3_column_int64(qp->stmt, 14);
  queue_item->time_modified = sqlite3_column_int(qp->stmt, 15);
  queue_item->artist_sort = strdup_if((char *)sqlite3_column_text(qp->stmt, 16), keep_item);
  queue_item->album_sort = strdup_if((char *)sqlite3_column_text(qp->stmt, 17), keep_item);
  queue_item->album_artist_sort = strdup_if((char *)sqlite3_column_text(qp->stmt, 18), keep_item);
  queue_item->year = sqlite3_column_int(qp->stmt, 19);
  queue_item->track = sqlite3_column_int(qp->stmt, 20);
  queue_item->disc = sqlite3_column_int(qp->stmt, 21);
  queue_item->artwork_url = strdup_if((char *)sqlite3_column_text(qp->stmt, 22), keep_item);

  return 0;
}

int
db_queue_enum_start(struct query_params *qp)
{
  int ret;

  db_transaction_begin();

  ret = queue_enum_start(qp);

  if (ret < 0)
    db_transaction_rollback();

  return ret;
}

void
db_queue_enum_end(struct query_params *qp)
{
  db_query_end(qp);
  db_transaction_end();
}

int
db_queue_enum_fetch(struct query_params *qp, struct db_queue_item *queue_item)
{
  return queue_enum_fetch(qp, queue_item, 0);
}

int
db_queue_get_pos(uint32_t item_id, char shuffle)
{
#define Q_TMPL "SELECT pos FROM queue WHERE id = %d;"
#define Q_TMPL_SHUFFLE "SELECT shuffle_pos FROM queue WHERE id = %d;"

  char *query;
  int pos;

  if (shuffle)
    query = sqlite3_mprintf(Q_TMPL_SHUFFLE, item_id);
  else
    query = sqlite3_mprintf(Q_TMPL, item_id);

  pos = db_get_one_int(query);

  sqlite3_free(query);

  return pos;

#undef Q_TMPL
#undef Q_TMPL_SHUFFLE
}

static int
queue_fetch_byitemid(uint32_t item_id, struct db_queue_item *queue_item, int with_metadata)
{
  struct query_params qp;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));
  qp.filter = sqlite3_mprintf("id = %d", item_id);

  ret = queue_enum_start(&qp);
  if (ret < 0)
    {
      sqlite3_free(qp.filter);
      return -1;
    }

  ret = queue_enum_fetch(&qp, queue_item, with_metadata);
  db_query_end(&qp);
  sqlite3_free(qp.filter);
  return ret;
}

struct db_queue_item *
db_queue_fetch_byitemid(uint32_t item_id)
{
  struct db_queue_item *queue_item;
  int ret;

  queue_item = calloc(1, sizeof(struct db_queue_item));
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for queue_item\n");
      return NULL;
    }

  db_transaction_begin();
  ret = queue_fetch_byitemid(item_id, queue_item, 1);
  db_transaction_end();

  if (ret < 0)
    {
      free_queue_item(queue_item, 0);
      DPRINTF(E_LOG, L_DB, "Error fetching queue item by item id\n");
      return NULL;
    }
  else if (queue_item->id == 0)
    {
      // No item found
      free_queue_item(queue_item, 0);
      return NULL;
    }

  return queue_item;
}

struct db_queue_item *
db_queue_fetch_byfileid(uint32_t file_id)
{
  struct db_queue_item *queue_item;
  struct query_params qp;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));
  queue_item = calloc(1, sizeof(struct db_queue_item));
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_DB, "Out of memory for queue_item\n");
      return NULL;
    }

  db_transaction_begin();

  qp.filter = sqlite3_mprintf("file_id = %d", file_id);

  ret = queue_enum_start(&qp);
  if (ret < 0)
    {
      sqlite3_free(qp.filter);
      db_transaction_end();
      free_queue_item(queue_item, 0);
      DPRINTF(E_LOG, L_DB, "Error fetching queue item by file id\n");
      return NULL;
    }

  ret = queue_enum_fetch(&qp, queue_item, 1);
  db_query_end(&qp);
  sqlite3_free(qp.filter);
  db_transaction_end();

  if (ret < 0)
    {
      free_queue_item(queue_item, 0);
      DPRINTF(E_LOG, L_DB, "Error fetching queue item by file id\n");
      return NULL;
    }
  else if (queue_item->id == 0)
    {
      // No item found
      free_queue_item(queue_item, 0);
      return NULL;
    }

  return queue_item;
}

static int
queue_fetch_bypos(uint32_t pos, char shuffle, struct db_queue_item *queue_item, int with_metadata)
{
  struct query_params qp;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));
  if (shuffle)
    qp.filter = sqlite3_mprintf("shuffle_pos = %d", pos);
  else
    qp.filter = sqlite3_mprintf("pos = %d", pos);

  ret = queue_enum_start(&qp);
  if (ret < 0)
    {
      sqlite3_free(qp.filter);
      return -1;
    }

  ret = queue_enum_fetch(&qp, queue_item, with_metadata);
  db_query_end(&qp);
  sqlite3_free(qp.filter);
  return ret;
}

struct db_queue_item *
db_queue_fetch_bypos(uint32_t pos, char shuffle)
{
  struct db_queue_item *queue_item;
  int ret;

  queue_item = calloc(1, sizeof(struct db_queue_item));
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_MAIN, "Out of memory for queue_item\n");
      return NULL;
    }

  db_transaction_begin();
  ret = queue_fetch_bypos(pos, shuffle, queue_item, 1);
  db_transaction_end();

  if (ret < 0)
    {
      free_queue_item(queue_item, 0);
      DPRINTF(E_LOG, L_DB, "Error fetching queue item by pos id\n");
      return NULL;
    }
  else if (queue_item->id == 0)
    {
      // No item found
      free_queue_item(queue_item, 0);
      return NULL;
    }

  return queue_item;
}

static int
queue_fetch_byposrelativetoitem(int pos, uint32_t item_id, char shuffle, struct db_queue_item *queue_item, int with_metadata)
{
  int pos_absolute;
  int ret;

  DPRINTF(E_DBG, L_DB, "Fetch by pos: pos (%d) relative to item with id (%d)\n", pos, item_id);

  pos_absolute = db_queue_get_pos(item_id, shuffle);
  if (pos_absolute < 0)
    {
      return -1;
    }

  DPRINTF(E_DBG, L_DB, "Fetch by pos: item (%d) has absolute pos %d\n", item_id, pos_absolute);

  pos_absolute += pos;

  ret = queue_fetch_bypos(pos_absolute, shuffle, queue_item, with_metadata);

  if (ret < 0)
    DPRINTF(E_LOG, L_DB, "Error fetching item by pos: pos (%d) relative to item with id (%d)\n", pos, item_id);
  else
    DPRINTF(E_DBG, L_DB, "Fetch by pos: fetched item (id=%d, pos=%d, file-id=%d)\n", queue_item->id, queue_item->pos, queue_item->file_id);

  return ret;
}

struct db_queue_item *
db_queue_fetch_byposrelativetoitem(int pos, uint32_t item_id, char shuffle)
{
  struct db_queue_item *queue_item;
  int ret;

  DPRINTF(E_DBG, L_DB, "Fetch by pos: pos (%d) relative to item with id (%d)\n", pos, item_id);

  queue_item = calloc(1, sizeof(struct db_queue_item));
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_MAIN, "Out of memory for queue_item\n");
      return NULL;
    }

  db_transaction_begin();

  ret = queue_fetch_byposrelativetoitem(pos, item_id, shuffle, queue_item, 1);

  db_transaction_end();

  if (ret < 0)
    {
      free_queue_item(queue_item, 0);
      DPRINTF(E_LOG, L_DB, "Error fetching queue item by pos relative to item id\n");
      return NULL;
    }
  else if (queue_item->id == 0)
    {
      // No item found
      free_queue_item(queue_item, 0);
      return NULL;
    }

  DPRINTF(E_DBG, L_DB, "Fetch by pos: fetched item (id=%d, pos=%d, file-id=%d)\n", queue_item->id, queue_item->pos, queue_item->file_id);

  return queue_item;
}

struct db_queue_item *
db_queue_fetch_next(uint32_t item_id, char shuffle)
{
  return db_queue_fetch_byposrelativetoitem(1, item_id, shuffle);
}

struct db_queue_item *
db_queue_fetch_prev(uint32_t item_id, char shuffle)
{
  return db_queue_fetch_byposrelativetoitem(-1, item_id, shuffle);
}

static int
queue_fix_pos(enum sort_type sort, int queue_version)
{
#define Q_TMPL "UPDATE queue SET %q = %d, queue_version = %d WHERE id = %d and %q <> %d;"

  struct query_params qp;
  struct db_queue_item queue_item;
  char *query;
  int pos;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));
  qp.sort = sort;

  ret = queue_enum_start(&qp);
  if (ret < 0)
    {
      return -1;
    }

  pos = 0;
  while ((ret = queue_enum_fetch(&qp, &queue_item, 0)) == 0 && (queue_item.id > 0))
    {
      if (queue_item.pos != pos)
        {
	  if (sort == S_SHUFFLE_POS)
	    query = sqlite3_mprintf(Q_TMPL, "shuffle_pos", pos, queue_version, queue_item.id, "shuffle_pos", pos);
	  else
	    query = sqlite3_mprintf(Q_TMPL, "pos", pos, queue_version, queue_item.id, "pos", pos);

	  ret = db_query_run(query, 1, 0);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_DB, "Failed to update item with item-id: %d\n", queue_item.id);
	      break;
	    }
	}

      pos++;
    }

  db_query_end(&qp);
  return ret;

#undef Q_TMPL
}

/*
 * Remove files that are disabled or non existent in the library and repair ordering of
 * the queue (shuffle and normal)
 */
int
db_queue_cleanup()
{
#define Q_TMPL "DELETE FROM queue WHERE NOT file_id IN (SELECT id from files WHERE disabled = 0);"

  int queue_version;
  int deleted;
  int ret;

  queue_version = queue_transaction_begin();

  ret = db_query_run(Q_TMPL, 0, 0);
  if (ret < 0)
    goto end_transaction;

  deleted = sqlite3_changes(hdl);
  if (deleted <= 0)
    {
      // Nothing to do
      db_transaction_end();
      return 0;
    }

  // Update position of normal queue
  ret = queue_fix_pos(S_POS, queue_version);
  if (ret < 0)
    goto end_transaction;

  // Update position of shuffle queue
  ret = queue_fix_pos(S_SHUFFLE_POS, queue_version);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;

#undef Q_TMPL
}

/*
 * Removes all items from the queue except the item give by 'keep_item_id' (if 'keep_item_id' > 0).
 *
 * @param keep_item_id item-id (e. g. the now playing item) to be left in the queue
 */
int
db_queue_clear(uint32_t keep_item_id)
{
  int queue_version;
  char *query;
  int ret;

  queue_version = queue_transaction_begin();

  query = sqlite3_mprintf("DELETE FROM queue where id <> %d;", keep_item_id);
  ret = db_query_run(query, 1, 0);

  if (ret == 0 && keep_item_id)
    {
      query = sqlite3_mprintf("UPDATE queue SET pos = 0, shuffle_pos = 0, queue_version = %d where id = %d;", queue_version, keep_item_id);
      ret = db_query_run(query, 1, 0);
    }

  queue_transaction_end(ret, queue_version);

  return ret;
}

static int
queue_delete_item(struct db_queue_item *queue_item, int queue_version)
{
  char *query;
  int ret;

  // Remove item with the given item_id
  query = sqlite3_mprintf("DELETE FROM queue where id = %d;", queue_item->id);
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    {
      return -1;
    }

  // Update pos for all items after the item with given item_id
  query = sqlite3_mprintf("UPDATE queue SET pos = pos - 1, queue_version = %d WHERE pos > %d;", queue_version, queue_item->pos);
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    {
      return -1;
    }

  // Update shuffle_pos for all items after the item with given item_id
  query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = shuffle_pos - 1, queue_version = %d WHERE shuffle_pos > %d;", queue_version, queue_item->shuffle_pos);
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    {
      return -1;
    }

  return 0;
}

int
db_queue_delete_byitemid(uint32_t item_id)
{
  int queue_version;
  struct db_queue_item queue_item;
  int ret;

  queue_version = queue_transaction_begin();

  ret = queue_fetch_byitemid(item_id, &queue_item, 0);

  if (ret < 0)
    goto end_transaction;

  if (queue_item.id == 0)
    {
      db_transaction_end();
      return 0;
    }

  ret = queue_delete_item(&queue_item, queue_version);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;
}

int
db_queue_delete_bypos(uint32_t pos, int count)
{
  int queue_version;
  char *query;
  int to_pos;
  int ret;

  queue_version = queue_transaction_begin();

  // Remove item with the given item_id
  to_pos = pos + count;
  query = sqlite3_mprintf("DELETE FROM queue where pos >= %d AND pos < %d;", pos, to_pos);
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  ret = queue_fix_pos(S_POS, queue_version);
  if (ret < 0)
    goto end_transaction;

  ret = queue_fix_pos(S_SHUFFLE_POS, queue_version);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;
}

int
db_queue_delete_byposrelativetoitem(uint32_t pos, uint32_t item_id, char shuffle)
{
  int queue_version;
  struct db_queue_item queue_item;
  int ret;

  queue_version = queue_transaction_begin();

  ret = queue_fetch_byposrelativetoitem(pos, item_id, shuffle, &queue_item, 0);
  if (ret < 0)
    goto end_transaction;

  if (queue_item.id == 0)
    {
      // No item found
      db_transaction_end();
      return 0;
    }

  ret = queue_delete_item(&queue_item, queue_version);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;
}

/*
 * Moves the queue item with the given id to the given position (zero-based).
 *
 * @param item_id Queue item id
 * @param pos_to target position in the queue (zero-based)
 * @þaram shuffle If 1 move item in the shuffle queue
 * @return 0 on success, -1 on failure
 */
int
db_queue_move_byitemid(uint32_t item_id, int pos_to, char shuffle)
{
  int queue_version;
  char *query;
  int pos_from;
  int ret;

  queue_version = queue_transaction_begin();

  // Find item with the given item_id
  pos_from = db_queue_get_pos(item_id, shuffle);
  if (pos_from < 0)
    {
      ret = -1;
      goto end_transaction;
    }

  // Update pos for all items after the item with given item_id
  if (shuffle)
    query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = shuffle_pos - 1, queue_version = %d WHERE shuffle_pos > %d;", queue_version, pos_from);
  else
    query = sqlite3_mprintf("UPDATE queue SET pos = pos - 1, queue_version = %d WHERE pos > %d;", queue_version, pos_from);

  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  // Update pos for all items from the given pos_to
  if (shuffle)
    query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = shuffle_pos + 1, queue_version = %d WHERE shuffle_pos >= %d;", queue_version, pos_to);
  else
    query = sqlite3_mprintf("UPDATE queue SET pos = pos + 1, queue_version = %d WHERE pos >= %d;", queue_version, pos_to);

  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  // Update item with the given item_id
  if (shuffle)
    query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = %d, queue_version = %d where id = %d;", pos_to, queue_version, item_id);
  else
    query = sqlite3_mprintf("UPDATE queue SET pos = %d, queue_version = %d where id = %d;", pos_to, queue_version, item_id);

  ret = db_query_run(query, 1, 0);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;
}

/*
 * Moves the queue item at the given position to the given position (zero-based).
 *
 * @param pos_from Position of the queue item to move
 * @param pos_to target position in the queue (zero-based)
 * @return 0 on success, -1 on failure
 */
int
db_queue_move_bypos(int pos_from, int pos_to)
{
  int queue_version;
  struct db_queue_item queue_item;
  char *query;
  int ret;

  queue_version = queue_transaction_begin();

  // Find item to move
  ret = queue_fetch_bypos(pos_from, 0, &queue_item, 0);
  if (ret < 0)
    goto end_transaction;

  if (queue_item.id == 0)
    {
      db_transaction_end();
      return 0;
    }

  // Update pos for all items after the item with given position
  query = sqlite3_mprintf("UPDATE queue SET pos = pos - 1, queue_version = %d WHERE pos > %d;", queue_version, queue_item.pos);
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  // Update pos for all items from the given pos_to
  query = sqlite3_mprintf("UPDATE queue SET pos = pos + 1, queue_version = %d WHERE pos >= %d;", queue_version, pos_to);
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  // Update item with the given item_id
  query = sqlite3_mprintf("UPDATE queue SET pos = %d, queue_version = %d where id = %d;", pos_to, queue_version, queue_item.id);
  ret = db_query_run(query, 1, 0);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;
}

/*
 * Moves the queue item at the given position to the given target position. The positions
 * are relavtive to the given base item (item id).
 *
 * @param from_pos Relative position of the queue item to the base item
 * @param to_offset Target position relative to the base item
 * @param item_id The base item id (normaly the now playing item)
 * @return 0 on success, -1 on failure
 */
int
db_queue_move_byposrelativetoitem(uint32_t from_pos, uint32_t to_offset, uint32_t item_id, char shuffle)
{
  int queue_version;
  struct db_queue_item queue_item;
  char *query;
  int pos_move_from;
  int pos_move_to;
  int ret;

  queue_version = queue_transaction_begin();

  DPRINTF(E_DBG, L_DB, "Move by pos: from %d offset %d relative to item (%d)\n", from_pos, to_offset, item_id);

  // Find item with the given item_id
  ret = queue_fetch_byitemid(item_id, &queue_item, 0);
  if (ret < 0)
    goto end_transaction;

  DPRINTF(E_DBG, L_DB, "Move by pos: base item (id=%d, pos=%d, file-id=%d)\n", queue_item.id, queue_item.pos, queue_item.file_id);

  if (queue_item.id == 0)
    {
      db_transaction_end();
      return 0;
    }

  // Calculate the position of the item to move
  if (shuffle)
    pos_move_from = queue_item.shuffle_pos + from_pos;
  else
    pos_move_from = queue_item.pos + from_pos;

  // Calculate the position where to move the item to
  if (shuffle)
    pos_move_to = queue_item.shuffle_pos + to_offset;
  else
    pos_move_to = queue_item.pos + to_offset;

  if (pos_move_to < pos_move_from)
    {
      /*
       * Moving an item to a previous position seems to send an offset incremented by one
       */
      pos_move_to++;
    }

  DPRINTF(E_DBG, L_DB, "Move by pos: absolute pos: move from %d to %d\n", pos_move_from, pos_move_to);

  // Find item to move
  ret = queue_fetch_bypos(pos_move_from, shuffle, &queue_item, 0);
  if (ret < 0)
    goto end_transaction;

  DPRINTF(E_DBG, L_DB, "Move by pos: move item (id=%d, pos=%d, file-id=%d)\n", queue_item.id, queue_item.pos, queue_item.file_id);

  if (queue_item.id == 0)
    {
      db_transaction_end();
      return 0;
    }

  // Update pos for all items after the item with given position
  if (shuffle)
    query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = shuffle_pos - 1, queue_version = %d WHERE shuffle_pos > %d;", queue_version, queue_item.shuffle_pos);
  else
    query = sqlite3_mprintf("UPDATE queue SET pos = pos - 1, queue_version = %d WHERE pos > %d;", queue_version, queue_item.pos);

  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  // Update pos for all items from the given pos_to
  if (shuffle)
    query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = shuffle_pos + 1, queue_version = %d WHERE shuffle_pos >= %d;", queue_version, pos_move_to);
  else
    query = sqlite3_mprintf("UPDATE queue SET pos = pos + 1, queue_version = %d WHERE pos >= %d;", queue_version, pos_move_to);

  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    goto end_transaction;

  // Update item with the given item_id
  if (shuffle)
    query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = %d, queue_version = %d where id = %d;", pos_move_to, queue_version, queue_item.id);
  else
    query = sqlite3_mprintf("UPDATE queue SET pos = %d, queue_version = %d where id = %d;", pos_move_to, queue_version, queue_item.id);

  ret = db_query_run(query, 1, 0);

 end_transaction:
  queue_transaction_end(ret, queue_version);

  return ret;
}

/*
 * Reshuffles the shuffle queue
 *
 * If the given item_id is 0, the whole shuffle queue is reshuffled, otherwise the
 * queue is reshuffled after the item with the given id (excluding this item).
 *
 * @param item_id The base item, after this item the queue is reshuffled
 * @return 0 on success, -1 on failure
 */
static int
queue_reshuffle(uint32_t item_id, int queue_version)
{
  char *query;
  int pos;
  int count;
  struct db_queue_item queue_item;
  int *shuffle_pos;
  int len;
  int i;
  struct query_params qp;
  int ret;

  DPRINTF(E_DBG, L_DB, "Reshuffle queue after item with item-id: %d\n", item_id);

  // Reset the shuffled order and mark all items as changed
  query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = pos, queue_version = %d;", queue_version);
  ret = db_query_run(query, 1, 0);
  if (ret < 0)
    {
      return -1;
    }

  pos = 0;
  if (item_id > 0)
    {
      pos = db_queue_get_pos(item_id, 0);
      if (pos < 0)
	{
	  return -1;
	}

      pos++; // Do not reshuffle the base item
    }

  count = db_queue_get_count();

  len = count - pos;

  DPRINTF(E_DBG, L_DB, "Reshuffle %d items off %d total items, starting from pos %d\n", len, count, pos);

  shuffle_pos = malloc(len * sizeof(int));
  for (i = 0; i < len; i++)
    {
      shuffle_pos[i] = i + pos;
    }

  shuffle_int(&shuffle_rng, shuffle_pos, len);

  memset(&qp, 0, sizeof(struct query_params));
  qp.filter = sqlite3_mprintf("pos >= %d", pos);

  ret = queue_enum_start(&qp);
  if (ret < 0)
    {
      sqlite3_free(qp.filter);
      return -1;
    }

  i = 0;
  while ((ret = queue_enum_fetch(&qp, &queue_item, 0)) == 0 && (queue_item.id > 0) && (i < len))
    {
      query = sqlite3_mprintf("UPDATE queue SET shuffle_pos = %d where id = %d;", shuffle_pos[i], queue_item.id);
      ret = db_query_run(query, 1, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DB, "Failed to delete item with item-id: %d\n", queue_item.id);
	  break;
	}

      i++;
    }

  db_query_end(&qp);
  sqlite3_free(qp.filter);

  if (ret < 0)
    {
      return -1;
    }

  return 0;
}

/*
 * Reshuffles the shuffle queue
 *
 * If the given item_id is 0, the whole shuffle queue is reshuffled, otherwise the
 * queue is reshuffled after the item with the given id (excluding this item).
 *
 * @param item_id The base item, after this item the queue is reshuffled
 * @return 0 on success, -1 on failure
 */
int
db_queue_reshuffle(uint32_t item_id)
{
  int queue_version;
  int ret;

  queue_version = queue_transaction_begin();

  ret = queue_reshuffle(item_id, queue_version);

  queue_transaction_end(ret, queue_version);

  return ret;
}

int
db_queue_get_count()
{
  return db_get_one_int("SELECT COUNT(*) FROM queue;");
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
db_watch_mark_bypath(char *path, enum strip_type strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE inotify SET path = substr(path, %d), cookie = %" PRIi64 " WHERE path = '%q';"
  char *query;
  int64_t disabled;
  int path_striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;

  path_striplen = (strip == STRIP_PATH) ? strlen(path) : 0;

  query = sqlite3_mprintf(Q_TMPL, path_striplen + 1, disabled, path);

  db_query_run(query, 1, 0);
#undef Q_TMPL
}

void
db_watch_mark_bymatch(char *path, enum strip_type strip, uint32_t cookie)
{
#define Q_TMPL "UPDATE inotify SET path = substr(path, %d), cookie = %" PRIi64 " WHERE path LIKE '%q/%%';"
  char *query;
  int64_t disabled;
  int path_striplen;

  disabled = (cookie != 0) ? cookie : INOTIFY_FAKE_COOKIE;

  path_striplen = (strip == STRIP_PATH) ? strlen(path) : 0;

  query = sqlite3_mprintf(Q_TMPL, path_striplen + 1, disabled, path);

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

  ret = db_get_one_int(query);

  sqlite3_free(query);

  return (ret > 0);

#undef Q_TMPL
}

int
db_watch_enum_start(struct watch_enum *we)
{
#define Q_MATCH_TMPL "SELECT wd FROM inotify WHERE path LIKE '%q/%%';"
#define Q_COOKIE_TMPL "SELECT wd FROM inotify WHERE cookie = %" PRIi64 ";"
  sqlite3_stmt *stmt;
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

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      sqlite3_free(query);
      return -1;
    }

  sqlite3_free(query);

  we->stmt = stmt;

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
static int
db_xprofile(unsigned int trace_type, void *notused, void *ptr, void *ptr_data)
{
  sqlite3_stmt *pstmt;
  int64_t ms = 0;
  sqlite3_stmt *stmt;
  const char *pquery;
  char *query;
  int log_level;
  int ret;

  if (trace_type != SQLITE_TRACE_PROFILE)
    return 0;

  pstmt = ptr;
  pquery = sqlite3_sql(pstmt);
  ms = *((int64_t *) ptr_data) / 1000000;

  if (ms > 1000)
    log_level = E_LOG;
  else if (ms > 500)
    log_level = E_WARN;
  else if (ms > 10)
    log_level = E_DBG;
  else
    log_level = E_SPAM;

  if (log_level > logger_severity())
    return 0;

  DPRINTF(log_level, L_DBPERF, "SQL PROFILE query: %s\n", pquery);
  DPRINTF(log_level, L_DBPERF, "SQL PROFILE time: %" PRIi64 " ms\n", ms);

  if ((strncmp(pquery, "SELECT", 6) != 0)
       && (strncmp(pquery, "UPDATE", 6) != 0)
       && (strncmp(pquery, "DELETE", 6) != 0))
      return 0;

  /* Disable profiling callback */
  sqlite3_trace_v2(hdl, 0, NULL, NULL);

  query = sqlite3_mprintf("EXPLAIN QUERY PLAN %s", pquery);
  if (!query)
    {
      DPRINTF(log_level, L_DBPERF, "Query plan: Out of memory\n");

      goto out;
    }

  ret = db_blocking_prepare_v2(query, -1, &stmt, NULL);
  sqlite3_free(query);
  if (ret != SQLITE_OK)
    {
      DPRINTF(log_level, L_DBPERF, "Query plan: Could not prepare statement: %s\n", sqlite3_errmsg(hdl));

      goto out;
    }

  DPRINTF(log_level, L_DBPERF, "Query plan:\n");

  while ((ret = db_blocking_step(stmt)) == SQLITE_ROW)
    {
      DPRINTF(log_level, L_DBPERF, "(%d,%d,%d) %s\n",
	      sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2),
	      sqlite3_column_text(stmt, 3));
    }

  if (ret != SQLITE_DONE)
    DPRINTF(log_level, L_DBPERF, "Query plan: Could not step: %s\n", sqlite3_errmsg(hdl));

  sqlite3_finalize(stmt);

 out:
  /* Reenable profiling callback */
  sqlite3_trace_v2(hdl, SQLITE_TRACE_PROFILE, db_xprofile, NULL);

  return 0;
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

static int
db_pragma_get_mmap_size()
{
  sqlite3_stmt *stmt;
  char *query = "PRAGMA mmap_size;";
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
db_pragma_set_mmap_size(int mmap_size)
{
#define Q_TMPL "PRAGMA mmap_size=%d;"
  sqlite3_stmt *stmt;
  char *query;
  int ret;

  query = sqlite3_mprintf(Q_TMPL, mmap_size);
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
  int mmap_size;

  ret = sqlite3_open(db_path, &hdl);
  if (ret != SQLITE_OK)
    {
      DPRINTF(E_LOG, L_DB, "Could not open '%s': %s\n", db_path, sqlite3_errmsg(hdl));

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
  sqlite3_trace_v2(hdl, SQLITE_TRACE_PROFILE, db_xprofile, NULL);
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

  mmap_size = cfg_getint(cfg_getsec(cfg, "sqlite"), "pragma_mmap_size_library");
  if (mmap_size > -1)
    {
      db_pragma_set_mmap_size(mmap_size);
      mmap_size = db_pragma_get_mmap_size();
      DPRINTF(E_DBG, L_DB, "Database mmap_size: %d\n", mmap_size);
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
  char *errmsg;
  int db_ver_major;
  int db_ver_minor;
  int db_ver;
  int vacuum;
  int ret;

  vacuum = cfg_getbool(cfg_getsec(cfg, "sqlite"), "vacuum");

  db_ver_major = db_admin_getint(DB_ADMIN_SCHEMA_VERSION_MAJOR);
  if (!db_ver_major)
    db_ver_major = db_admin_getint(DB_ADMIN_SCHEMA_VERSION); // Pre schema v15.1

  if (!db_ver_major)
    return 1; // Will create new database

  db_ver_minor = db_admin_getint(DB_ADMIN_SCHEMA_VERSION_MINOR);

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

  db_set_cfg_names();

  files = db_files_get_count();
  pls = db_pl_get_count();
  db_admin_setint64(DB_ADMIN_START_TIME, (int64_t) time(NULL));

  db_perthread_deinit();

  DPRINTF(E_LOG, L_DB, "Database OK with %d active files and %d active playlists\n", files, pls);

  rng_init(&shuffle_rng);

  return 0;
}

void
db_deinit(void)
{
  sqlite3_shutdown();
}
