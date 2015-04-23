/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <sys/mman.h>

#include <stdint.h>
#include <inttypes.h>

#include <plist/plist.h>

#ifdef HAVE_LIBEVENT2
# include <event2/http.h>
#else
# include "evhttp/evhttp.h"
#endif

#include "logger.h"
#include "db.h"
#include "filescanner.h"
#include "conffile.h"
#include "misc.h"


/* Mapping between iTunes library IDs and our DB IDs using a "hash" table of
 * size ID_MAP_SIZE
 */
#define ID_MAP_SIZE 16384
struct itml_to_db_map {
  uint64_t itml_id;
  uint32_t db_id;
  struct itml_to_db_map *next;
};
struct itml_to_db_map **id_map;


/* Mapping between iTunes library metadata keys and the offset
 * of the equivalent metadata field in struct media_file_info */
struct metadata_map {
  char *key;
  plist_type type;
  size_t offset;
};

static struct metadata_map md_map[] =
  {
    { "Name",         PLIST_STRING,  mfi_offsetof(title) },
    { "Artist",       PLIST_STRING,  mfi_offsetof(artist) },
    { "Album Artist", PLIST_STRING,  mfi_offsetof(album_artist) },
    { "Composer",     PLIST_STRING,  mfi_offsetof(composer) },
    { "Album",        PLIST_STRING,  mfi_offsetof(album) },
    { "Genre",        PLIST_STRING,  mfi_offsetof(genre) },
    { "Comments",     PLIST_STRING,  mfi_offsetof(comment) },
    { "Track Count",  PLIST_UINT,    mfi_offsetof(total_tracks) },
    { "Track Number", PLIST_UINT,    mfi_offsetof(track) },
    { "Disc Count",   PLIST_UINT,    mfi_offsetof(total_discs) },
    { "Disc Number",  PLIST_UINT,    mfi_offsetof(disc) },
    { "Year",         PLIST_UINT,    mfi_offsetof(year) },
    { "Total Time",   PLIST_UINT,    mfi_offsetof(song_length) },
    { "Bit Rate",     PLIST_UINT,    mfi_offsetof(bitrate) },
    { "Sample Rate",  PLIST_UINT,    mfi_offsetof(samplerate) },
    { "BPM",          PLIST_UINT,    mfi_offsetof(bpm) },
    { "Rating",       PLIST_UINT,    mfi_offsetof(rating) },
    { "Compilation",  PLIST_BOOLEAN, mfi_offsetof(compilation) },
    { "Date Added",   PLIST_DATE,    mfi_offsetof(time_added) },
    { NULL,           0, 0 }
  };

static void
id_map_free(void)
{
  struct itml_to_db_map *map;
  int i;

  for (i = 0; i < ID_MAP_SIZE; i++)
    {
      if (!id_map[i])
	continue;

      for (map = id_map[i]; id_map[i]; map = id_map[i])
	{
	  id_map[i] = map->next;
	  free(map);
	}
    }

  free(id_map);
}

/* Inserts a linked list item into "hash" position in the id_table */
static int
id_map_add(uint64_t itml_id, uint32_t db_id)
{
  struct itml_to_db_map *new_map;
  struct itml_to_db_map *cur_map;
  int i;

  new_map = malloc(sizeof(struct itml_to_db_map));
  if (!new_map)
    return -1;

  new_map->itml_id = itml_id;
  new_map->db_id = db_id;

  i = itml_id % ID_MAP_SIZE;
  cur_map = id_map[i];
  new_map->next = cur_map;
  id_map[i] = new_map;

  return 0;
}

static uint32_t
id_map_get(uint64_t itml_id)
{
  struct itml_to_db_map *map;
  int i;

  i = itml_id % ID_MAP_SIZE;
  for (map = id_map[i]; map; map = map->next)
    {
      if (itml_id == map->itml_id)
	return map->db_id;
    }

  return 0;
}

/* plist helpers */
static int
get_dictval_int_from_key(plist_t dict, const char *key, uint64_t *val)
{
  plist_t node;

  node = plist_dict_get_item(dict, key);

  if (!node)
    return -1;

  if (plist_get_node_type(node) != PLIST_UINT)
    return -1;

  plist_get_uint_val(node, val);

  return 0;
}

static int
get_dictval_date_from_key(plist_t dict, const char *key, uint32_t *val)
{
  plist_t node;
  int32_t secs;
  int32_t dummy;

  node = plist_dict_get_item(dict, key);

  if (!node)
    return -1;

  if (plist_get_node_type(node) != PLIST_DATE)
    return -1;

  plist_get_date_val(node, &secs, &dummy);

  *val = (uint32_t) secs;

  return 0;
}

static int
get_dictval_bool_from_key(plist_t dict, const char *key, uint8_t *val)
{
  plist_t node;

  node = plist_dict_get_item(dict, key);

  /* Not present means false */
  if (!node)
    {
      *val = 0;

      return 0;
    }

  if (plist_get_node_type(node) != PLIST_BOOLEAN)
    return -1;

  plist_get_bool_val(node, val);

  return 0;
}

static int
get_dictval_string_from_key(plist_t dict, const char *key, char **val)
{
  plist_t node;

  node = plist_dict_get_item(dict, key);

  if (!node)
    return -1;

  if (plist_get_node_type(node) != PLIST_STRING)
    return -1;

  plist_get_string_val(node, val);

  return 0;
}

static int
get_dictval_dict_from_key(plist_t dict, const char *key, plist_t *val)
{
  plist_t node;

  node = plist_dict_get_item(dict, key);

  if (!node)
    return -1;

  if (plist_get_node_type(node) != PLIST_DICT)
    return -1;

  *val = node;

  return 0;
}

static int
get_dictval_array_from_key(plist_t dict, const char *key, plist_t *val)
{
  plist_t node;

  node = plist_dict_get_item(dict, key);

  if (!node)
    return -1;

  if (plist_get_node_type(node) != PLIST_ARRAY)
    return -1;

  *val = node;

  return 0;
}


/* We don't actually check anything (yet) despite the name */
static int
check_meta(plist_t dict)
{
  char *appver;
  char *folder;
  uint64_t major;
  uint64_t minor;
  int ret;

  ret = get_dictval_int_from_key(dict, "Major Version", &major);
  if (ret < 0)
    return -1;

  ret = get_dictval_int_from_key(dict, "Minor Version", &minor);
  if (ret < 0)
    return -1;

  ret = get_dictval_string_from_key(dict, "Application Version", &appver);
  if (ret < 0)
    return -1;

  ret = get_dictval_string_from_key(dict, "Music Folder", &folder);
  if (ret < 0)
    {
      free(appver);
      return -1;
    }

  DPRINTF(E_INFO, L_SCAN, "iTunes XML playlist Major:%" PRIu64 " Minor:%" PRIu64
	  " Application:%s Folder:%s\n", major, minor, appver, folder);

  free(appver);
  free(folder);

  return 0;
}

static int
find_track_file(char *location)
{
  int ret;
  int plen;
  int mfi_id;
  char *entry;
  char *ptr;

  location = evhttp_decode_uri(location);
  if (!location)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not decode iTunes XML playlist url.\n");

      return 0;
    }

  plen = strlen("file://");

  /* Not a local file ... */
  if (strncmp(location, "file://", plen) != 0)
    return 0;

  /* Now search for the library item where the path has closest match to playlist item */
  /* Succes is when we find an unambiguous match, or when we no longer can expand the  */
  /* the path to refine our search.                                                    */
  entry = NULL;
  do
    {
      ptr = strrchr(location, '/');
      if (entry)
	*(entry - 1) = '/';
      if (ptr)
	{
	  *ptr = '\0';
	  entry = ptr + 1;
	}
      else
	entry = location;

      DPRINTF(E_SPAM, L_SCAN, "iTunes XML playlist entry is now %s\n", entry);
      ret = db_files_get_count_bymatch(entry);

    } while (ptr && (ret > 1));

  if (ret > 0)
    {
      mfi_id = db_file_id_bymatch(entry);
      DPRINTF(E_DBG, L_SCAN, "Found iTunes XML playlist entry match, id is %d, entry is %s\n", mfi_id, entry);

      free(location);
      return mfi_id;
    }
  else
    {
      DPRINTF(E_DBG, L_SCAN, "No match for iTunes XML playlist entry %s\n", entry);

      free(location);
      return 0;
    }

}

static int
process_track_file(plist_t trk)
{
  char *location;
  struct media_file_info *mfi;
  char *string;
  uint64_t integer;
  char **strval;
  uint32_t *intval;
  char *chrval;
  uint8_t boolean;
  int mfi_id;
  int i;
  int ret;

  ret = get_dictval_string_from_key(trk, "Location", &location);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Track type File with no Location\n");

      return 0;
    }

  mfi_id = find_track_file(location);

  if (mfi_id <= 0)
    {
      DPRINTF(E_INFO, L_SCAN, "Could not match location '%s' to any known file\n", location);

      free(location);
      return 0;
    }

  free(location);

  if (!cfg_getbool(cfg_getsec(cfg, "library"), "itunes_overrides"))
    return mfi_id;

  /* Override our metadata with what's provided by iTunes */
  mfi = db_file_fetch_byid(mfi_id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not retrieve file info for file id %d\n", mfi_id);

      return mfi_id;
    }

  for (i = 0; md_map[i].key != NULL; i++)
    {
      switch (md_map[i].type)
	{
	  case PLIST_UINT:
	    ret = get_dictval_int_from_key(trk, md_map[i].key, &integer);
	    if (ret < 0)
	      break;

	    intval = (uint32_t *) ((char *) mfi + md_map[i].offset);

	    *intval = (uint32_t)integer;
	    break;

	  case PLIST_STRING:
	    ret = get_dictval_string_from_key(trk, md_map[i].key, &string);
	    if (ret < 0)
	      break;

	    strval = (char **) ((char *) mfi + md_map[i].offset);

	    if (*strval)
	      free(*strval);

	    *strval = string;
	    break;

	  case PLIST_BOOLEAN:
	    ret = get_dictval_bool_from_key(trk, md_map[i].key, &boolean);
	    if (ret < 0)
	      break;

	    chrval = (char *) mfi + md_map[i].offset;

	    *chrval = boolean;
	    break;

	  case PLIST_DATE:
	    intval = (uint32_t *) ((char *) mfi + md_map[i].offset);

	    get_dictval_date_from_key(trk, md_map[i].key, intval);
	    break;

	  default:
	    DPRINTF(E_WARN, L_SCAN, "Unhandled metadata type %d\n", md_map[i].type);
	    break;
	}
    }

  /* Set media_kind to 4 (Podcast) if Podcast is true */
  ret = get_dictval_bool_from_key(trk, "Podcast", &boolean); 
  if ((ret == 0) && boolean)
    {
      mfi->media_kind = MEDIA_KIND_PODCAST;
    }

  /* Don't let album_artist set to "Unknown artist" if we've
   * filled artist from the iTunes data in the meantime
   */
  if (strcmp(mfi->album_artist, "Unknown artist") == 0)
    {
      free(mfi->album_artist);
      mfi->album_artist = strdup(mfi->artist);
    }

  unicode_fixup_mfi(mfi);
  db_file_update(mfi);

  free_mfi(mfi, 0);

  return mfi_id;
}

static int
process_track_stream(plist_t trk)
{
  char *url;
  int ret;

  ret = get_dictval_string_from_key(trk, "Location", &url);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Track type URL with no Location entry!\n");

      return 0;
    }

  ret = db_file_id_byurl(url);

  free(url);

  return ret;
}

static int
process_tracks(plist_t tracks)
{
  plist_t trk;
  plist_dict_iter iter;
  char *str;
  uint64_t trk_id;
  uint8_t disabled;
  int ntracks;
  int mfi_id;
  int ret;

  if (plist_dict_get_size(tracks) == 0)
    {
      DPRINTF(E_WARN, L_SCAN, "No tracks in iTunes library\n");
      return 0;
    }

  ntracks = 0;

  iter = NULL;
  plist_dict_new_iter(tracks, &iter);

  plist_dict_next_item(tracks, iter, NULL, &trk);
  while (trk)
    {
      if (plist_get_node_type(trk) != PLIST_DICT)
	{
	  plist_dict_next_item(tracks, iter, NULL, &trk);
	  continue;
	}

      ret = get_dictval_int_from_key(trk, "Track ID", &trk_id);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_SCAN, "Track ID not found!\n");

	  plist_dict_next_item(tracks, iter, NULL, &trk);
	  continue;
	}

      ret = get_dictval_bool_from_key(trk, "Disabled", &disabled);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_SCAN, "Malformed track record (id %" PRIu64 ")\n", trk_id);

	  plist_dict_next_item(tracks, iter, NULL, &trk);
	  continue;
	}

      if (disabled)
	{
	  DPRINTF(E_INFO, L_SCAN, "Track %" PRIu64 " disabled; skipping\n", trk_id);

	  plist_dict_next_item(tracks, iter, NULL, &trk);
	  continue;
	}

      ret = get_dictval_string_from_key(trk, "Track Type", &str);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_SCAN, "Track %" PRIu64 " has no track type\n", trk_id);

	  plist_dict_next_item(tracks, iter, NULL, &trk);
	  continue;
	}

      if (strcmp(str, "URL") == 0)
	mfi_id = process_track_stream(trk);
      else if (strcmp(str, "File") == 0)
	mfi_id = process_track_file(trk);
      else
	{
	  DPRINTF(E_LOG, L_SCAN, "Unknown track type: %s\n", str);

	  free(str);
	  plist_dict_next_item(tracks, iter, NULL, &trk);
	  continue;
	}

      free(str);

      if (mfi_id <= 0)
	{
	  plist_dict_next_item(tracks, iter, NULL, &trk);
	  continue;
	}

      ntracks++;

      ret = id_map_add(trk_id, mfi_id);
      if (ret < 0)
	DPRINTF(E_LOG, L_SCAN, "Out of memory for itml -> db mapping\n");

      plist_dict_next_item(tracks, iter, NULL, &trk);
    }

  free(iter);

  return ntracks;
}


static void
process_pl_items(plist_t items, int pl_id)
{
  plist_t trk;
  uint64_t itml_id;
  uint32_t db_id;
  uint32_t alen;
  uint32_t i;
  int ret;

  alen = plist_array_get_size(items);
  for (i = 0; i < alen; i++)
    {
      trk = plist_array_get_item(items, i);

      if (plist_get_node_type(trk) != PLIST_DICT)
	continue;

      ret = get_dictval_int_from_key(trk, "Track ID", &itml_id);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_SCAN, "No Track ID found for playlist item %u\n", i);
	  continue;
	}

      db_id = id_map_get(itml_id);
      if (!db_id)
	{
	  DPRINTF(E_INFO, L_SCAN, "Track ID %" PRIu64 " dropped\n", itml_id);
	  continue;
	}

      ret = db_pl_add_item_byid(pl_id, db_id);
      if (ret < 0)
	DPRINTF(E_WARN, L_SCAN, "Could not add ID %d to playlist\n", db_id);
    }
}

static int 
ignore_pl(plist_t pl, char *name)
{
  uint64_t kind;
  int smart;
  uint8_t master;
  uint8_t party;

  kind = 0;
  smart = 0;
  master = 0;
  party = 0;

  /* Special (builtin) playlists */
  get_dictval_int_from_key(pl, "Distinguished Kind", &kind);

  /* If only we could recover the smart playlists ... */
  if (plist_dict_get_item(pl, "Smart Info")
      || plist_dict_get_item(pl, "Smart Criteria"))
    smart = 1;

  /* Not interested in the Master playlist */
  get_dictval_bool_from_key(pl, "Master", &master);
  /* Not interested in Party Shuffle playlists */
  get_dictval_bool_from_key(pl, "Party Shuffle", &party);

  if ((kind > 0) || smart || party || master)
    {
      DPRINTF(E_INFO, L_SCAN, "Ignoring playlist '%s' (k %" PRIu64 " s%d p%d m%d)\n", name, kind, smart, party, master);

      return 1;
    }

  return 0;
}

static void
process_pls(plist_t playlists, char *file)
{
  plist_t pl;
  plist_t items;
  struct playlist_info *pli;
  char *name;
  uint64_t id;
  int pl_id;
  uint32_t alen;
  uint32_t i;
  char virtual_path[PATH_MAX];
  int ret;

  alen = plist_array_get_size(playlists);
  for (i = 0; i < alen; i++)
    {
      pl = plist_array_get_item(playlists, i);

      if (plist_get_node_type(pl) != PLIST_DICT)
	continue;

      ret = get_dictval_int_from_key(pl, "Playlist ID", &id);
      if (ret < 0)
	{
	  DPRINTF(E_DBG, L_SCAN, "Playlist ID not found!\n");
	  continue;
	}

      ret = get_dictval_string_from_key(pl, "Name", &name);
      if (ret < 0)
	{
	  DPRINTF(E_DBG, L_SCAN, "Name not found!\n");
	  continue;
	}

      if (ignore_pl(pl, name))
	{
	  free(name);
	  continue;
	}

      pli = db_pl_fetch_bytitlepath(name, file);

      if (pli)
	{
	  pl_id = pli->id;

	  free_pli(pli, 0);

	  db_pl_ping(pl_id);
	  db_pl_clear_items(pl_id);
	}
      else
	pl_id = 0;

      ret = get_dictval_array_from_key(pl, "Playlist Items", &items);
      if (ret < 0)
	{
	  DPRINTF(E_INFO, L_SCAN, "Playlist '%s' has no items\n", name);

	  free(name);
	  continue;
	}

      if (pl_id == 0)
	{
	  pli = (struct playlist_info *)malloc(sizeof(struct playlist_info));
	  if (!pli)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Out of memory\n");

	      return;
	    }
	  memset(pli, 0, sizeof(struct playlist_info));

	  pli->type = PL_PLAIN;
	  pli->title = strdup(name);
	  pli->path = strdup(file);
	  snprintf(virtual_path, PATH_MAX, "/file:%s", file);
	  pli->virtual_path = strdup(virtual_path);

	  ret = db_pl_add(pli, &pl_id);
	  free_pli(pli, 0);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Error adding iTunes playlist '%s' (%s)\n", name, file);

	      free(name);
	      continue;
	    }

	  DPRINTF(E_INFO, L_SCAN, "Added playlist as id %d\n", pl_id);
	}

      free(name);

      process_pl_items(items, pl_id);
    }
}


void
scan_itunes_itml(char *file)
{
  struct stat sb;
  char *itml_xml;
  char *ptr;
  plist_t itml;
  plist_t node;
  int fd;
  int size;
  int ret;

  DPRINTF(E_LOG, L_SCAN, "Processing iTunes library: %s\n", file);

  fd = open(file, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not open iTunes library '%s': %s\n", file, strerror(errno));

      return;
    }

  ret = fstat(fd, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not stat iTunes library '%s': %s\n", file, strerror(errno));

      close(fd);
      return;
    }

  itml_xml = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (itml_xml == MAP_FAILED)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not map iTunes library: %s\n", strerror(errno));

      close(fd);
      return;
    }

  itml = NULL;
  plist_from_xml(itml_xml, sb.st_size, &itml);

  ret = munmap(itml_xml, sb.st_size);
  if (ret < 0)
    DPRINTF(E_LOG, L_SCAN, "Could not unmap iTunes library: %s\n", strerror(errno));

  close(fd);

  if (!itml)
    {
      DPRINTF(E_LOG, L_SCAN, "iTunes XML playlist '%s' failed to parse\n", file);

      return;
    }

  if (plist_get_node_type(itml) != PLIST_DICT)
    {
      DPRINTF(E_LOG, L_SCAN, "Malformed iTunes XML playlist '%s'\n", file);

      plist_free(itml);
      return;
    }

  /* Meta data */
  ret = check_meta(itml);
  if (ret < 0)
    {
      plist_free(itml);
      return;
    }

  /* Tracks */
  ret = get_dictval_dict_from_key(itml, "Tracks", &node);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not find Tracks dict\n");

      plist_free(itml);
      return;
    }

  size = ID_MAP_SIZE * sizeof(struct itml_to_db_map *);
  id_map = malloc(size);
  if (!id_map)
    {
      DPRINTF(E_FATAL, L_SCAN, "iTunes library parser could not allocate ID map\n");

      plist_free(itml);
      return;
    }
  memset(id_map, 0, size);

  ptr = strrchr(file, '/');
  if (!ptr)
    {
      DPRINTF(E_FATAL, L_SCAN, "Invalid filename\n");

      id_map_free();
      plist_free(itml);
      return;
    }

  *ptr = '\0';

  ret = process_tracks(node);
  if (ret <= 0)
    {
      DPRINTF(E_LOG, L_SCAN, "No tracks loaded\n");

      id_map_free();
      plist_free(itml);
      return;
    }

  *ptr = '/';

  DPRINTF(E_INFO, L_SCAN, "Loaded %d tracks from iTunes library\n", ret);

  /* Playlists */
  ret = get_dictval_array_from_key(itml, "Playlists", &node);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not find Playlists dict\n");

      id_map_free();
      plist_free(itml);
      return;
    }

  process_pls(node, file);

  id_map_free();
  plist_free(itml);
}
