/*
 * Copyright (C) 2016 Christian Meffert <christian.meffert@googlemail.com>
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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/listener.h>

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <netinet/in.h>
#endif

#include "artwork.h"
#include "commands.h"
#include "conffile.h"
#include "db.h"
#include "httpd.h"
#include "library.h"
#include "listener.h"
#include "logger.h"
#include "misc.h"
#include "player.h"
#include "remote_pairing.h"


static pthread_t tid_mpd;

static struct event_base *evbase_mpd;

static struct commands_base *cmdbase;

static struct evhttp *evhttpd;

struct evconnlistener *mpd_listener6;
struct evconnlistener *mpd_listener;

// Virtual path to the default playlist directory
static char *default_pl_dir;
static bool allow_modifying_stored_playlists;

#define COMMAND_ARGV_MAX 37

/* MPD error codes (taken from ack.h) */
enum ack
{
  ACK_ERROR_NOT_LIST = 1,
  ACK_ERROR_ARG = 2,
  ACK_ERROR_PASSWORD = 3,
  ACK_ERROR_PERMISSION = 4,
  ACK_ERROR_UNKNOWN = 5,

  ACK_ERROR_NO_EXIST = 50,
  ACK_ERROR_PLAYLIST_MAX = 51,
  ACK_ERROR_SYSTEM = 52,
  ACK_ERROR_PLAYLIST_LOAD = 53,
  ACK_ERROR_UPDATE_ALREADY = 54,
  ACK_ERROR_PLAYER_SYNC = 55,
  ACK_ERROR_EXIST = 56,
};

enum command_list_type
{
  COMMAND_LIST = 1,
  COMMAND_LIST_OK = 2,
  COMMAND_LIST_NONE = 3
};

/**
 * This lists for ffmpeg suffixes and mime types are taken from the ffmpeg decoder plugin from mpd
 * (FfmpegDecoderPlugin.cxx, git revision 9fb351a139a56fc7b1ece549894f8fc31fa887cd).
 *
 * forked-daapd does not support different decoders and always uses ffmpeg or libav for decoding.
 * Some clients rely on a response for the decoder commands (e.g. ncmpccp) therefor return something
 * valid for this command.
 */
static const char * const ffmpeg_suffixes[] = { "16sv", "3g2", "3gp", "4xm", "8svx", "aa3", "aac", "ac3", "afc", "aif",
    "aifc", "aiff", "al", "alaw", "amr", "anim", "apc", "ape", "asf", "atrac", "au", "aud", "avi", "avm2", "avs", "bap",
    "bfi", "c93", "cak", "cin", "cmv", "cpk", "daud", "dct", "divx", "dts", "dv", "dvd", "dxa", "eac3", "film", "flac",
    "flc", "fli", "fll", "flx", "flv", "g726", "gsm", "gxf", "iss", "m1v", "m2v", "m2t", "m2ts", "m4a", "m4b", "m4v",
    "mad", "mj2", "mjpeg", "mjpg", "mka", "mkv", "mlp", "mm", "mmf", "mov", "mp+", "mp1", "mp2", "mp3", "mp4", "mpc",
    "mpeg", "mpg", "mpga", "mpp", "mpu", "mve", "mvi", "mxf", "nc", "nsv", "nut", "nuv", "oga", "ogm", "ogv", "ogx",
    "oma", "ogg", "omg", "psp", "pva", "qcp", "qt", "r3d", "ra", "ram", "rl2", "rm", "rmvb", "roq", "rpl", "rvc", "shn",
    "smk", "snd", "sol", "son", "spx", "str", "swf", "tgi", "tgq", "tgv", "thp", "ts", "tsp", "tta", "xa", "xvid", "uv",
    "uv2", "vb", "vid", "vob", "voc", "vp6", "vmd", "wav", "webm", "wma", "wmv", "wsaud", "wsvga", "wv", "wve",
    NULL
};
static const char * const ffmpeg_mime_types[] = { "application/flv", "application/m4a", "application/mp4",
    "application/octet-stream", "application/ogg", "application/x-ms-wmz", "application/x-ms-wmd", "application/x-ogg",
    "application/x-shockwave-flash", "application/x-shorten", "audio/8svx", "audio/16sv", "audio/aac", "audio/ac3",
    "audio/aiff", "audio/amr", "audio/basic", "audio/flac", "audio/m4a", "audio/mp4", "audio/mpeg", "audio/musepack",
    "audio/ogg", "audio/qcelp", "audio/vorbis", "audio/vorbis+ogg", "audio/x-8svx", "audio/x-16sv", "audio/x-aac",
    "audio/x-ac3", "audio/x-aiff", "audio/x-alaw", "audio/x-au", "audio/x-dca", "audio/x-eac3", "audio/x-flac",
    "audio/x-gsm", "audio/x-mace", "audio/x-matroska", "audio/x-monkeys-audio", "audio/x-mpeg", "audio/x-ms-wma",
    "audio/x-ms-wax", "audio/x-musepack", "audio/x-ogg", "audio/x-vorbis", "audio/x-vorbis+ogg", "audio/x-pn-realaudio",
    "audio/x-pn-multirate-realaudio", "audio/x-speex", "audio/x-tta", "audio/x-voc", "audio/x-wav", "audio/x-wma",
    "audio/x-wv", "video/anim", "video/quicktime", "video/msvideo", "video/ogg", "video/theora", "video/webm",
    "video/x-dv", "video/x-flv", "video/x-matroska", "video/x-mjpeg", "video/x-mpeg", "video/x-ms-asf",
    "video/x-msvideo", "video/x-ms-wmv", "video/x-ms-wvx", "video/x-ms-wm", "video/x-ms-wmx", "video/x-nut",
    "video/x-pva", "video/x-theora", "video/x-vid", "video/x-wmv", "video/x-xvid",

    /* special value for the "ffmpeg" input plugin: all streams by
     the "ffmpeg" input plugin shall be decoded by this
     plugin */
    "audio/x-mpd-ffmpeg",

    NULL
};

struct output
{
  unsigned short shortid;
  uint64_t id;
  char *name;

  unsigned selected;

  struct output *next;
};

struct outputs
{
  unsigned int count;
  unsigned int active;
  struct output *outputs;
};

static void
free_outputs(struct output *outputs)
{
  struct output *temp;
  struct output *next;

  temp = outputs;
  next = outputs ? outputs->next : NULL;
  while (temp)
    {
      free(temp->name);
      free(temp);
      temp = next;
      next = next ? next->next : NULL;
    }
}

struct idle_client
{
  struct evbuffer *evbuffer;
  short events;

  struct idle_client *next;
};

struct idle_client *idle_clients;


/*
 * Creates a new string for the given path that starts with a '/'.
 * If 'path' already starts with a '/' the returned string is a duplicate
 * of 'path'.
 *
 * The returned string needs to be freed by the caller.
 */
static char *
prepend_slash(const char *path)
{
  char *result;

  if (path[0] == '/')
    result = strdup(path);
  else
    result = safe_asprintf("/%s", path);

  return result;
}


/* Thread: mpd */
static void *
mpd(void *arg)
{
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  event_base_dispatch(evbase_mpd);

  db_perthread_deinit();

  pthread_exit(NULL);
}

static void
mpd_time(char *buffer, size_t bufferlen, time_t t)
{
  struct tm tm;
  const struct tm *tm2 = gmtime_r(&t, &tm);
  if (tm2 == NULL)
    return;

  strftime(buffer, bufferlen, "%FT%TZ", tm2);
}

/*
 * Parses a rage argument of the form START:END (the END item is not included in the range)
 * into its start and end position.
 *
 * @param range the range argument
 * @param start_pos set by this method to the start position
 * @param end_pos set by this method to the end postion
 * @return 0 on success, -1 on failure
 */
static int
mpd_pars_range_arg(char *range, int *start_pos, int *end_pos)
{
  int ret;

  if (strchr(range, ':'))
    {
      ret = sscanf(range, "%d:%d", start_pos, end_pos);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error parsing range argument '%s' (return code = %d)\n", range, ret);
	  return -1;
	}
    }
  else
    {
      ret = safe_atoi32(range, start_pos);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error parsing integer argument '%s' (return code = %d)\n", range, ret);
	  return -1;
	}

      *end_pos = (*start_pos) + 1;
    }

  return 0;
}

/*
 * Returns the next unquoted string argument from the input string
 */
static char*
mpd_pars_unquoted(char **input)
{
  char *arg;

  arg = *input;

  while (**input != 0)
    {
      if (**input == ' ')
	{
	  **input = '\0';
	  (*input)++;
	  return arg;
	}

      (*input)++;
    }

  return arg;
}

/*
 * Returns the next quoted string argument from the input string
 * with the quotes removed
 */
static char*
mpd_pars_quoted(char **input)
{
  char *arg;

  // skip double quote character
  (*input)++;

  arg = *input;

  while (**input != '"')
    {
      // A backslash character escapes the following character
      if (**input == '\\')
	{
	  (*input)++;
	}

      if (**input == 0)
	{
	  // Error handling for missing double quote at end of parameter
	  DPRINTF(E_LOG, L_MPD, "Error missing closing double quote in argument\n");
	  return NULL;
	}

      (*input)++;
    }

  **input = '\0';
  (*input)++;

  return arg;
}

/*
 * Parses the argument string into an array of strings.
 * Arguments are seperated by a whitespace character and may be wrapped in double quotes.
 *
 * @param args the arguments
 * @param argc the number of arguments in the argument string
 * @param argv the array containing the found arguments
 */
static int
mpd_parse_args(char *args, int *argc, char **argv)
{
  char *input;

  input = args;
  *argc = 0;

  while (*input != 0)
    {
      // Ignore whitespace characters
      if (*input == ' ')
	{
	  input++;
	  continue;
	}

      // Check if the parameter is wrapped in double quotes
      if (*input == '"')
	{
	  argv[*argc] = mpd_pars_quoted(&input);
	  if (argv[*argc] == NULL)
	    {
	      return -1;
	    }
	  *argc = *argc + 1;
	}
      else
	{
	  argv[*argc] = mpd_pars_unquoted(&input);
	  *argc = *argc + 1;
	}
    }

  return 0;
}

/*
 * Adds the informations (path, id, tags, etc.) for the given song to the given buffer
 * with additional information for the position of this song in the playqueue.
 *
 * Example output:
 *   file: foo/bar/song.mp3
 *   Last-Modified: 2013-07-14T06:57:59Z
 *   Time: 172
 *   Artist: foo
 *   AlbumArtist: foo
 *   ArtistSort: foo
 *   AlbumArtistSort: foo
 *   Title: song
 *   Album: bar
 *   Track: 1/11
 *   Date: 2012-09-11
 *   Genre: Alternative
 *   Disc: 1/1
 *   MUSICBRAINZ_ALBUMARTISTID: c5c2ea1c-4bde-4f4d-bd0b-47b200bf99d6
 *   MUSICBRAINZ_ARTISTID: c5c2ea1c-4bde-4f4d-bd0b-47b200bf99d6
 *   MUSICBRAINZ_ALBUMID: 812f4b87-8ad9-41bd-be79-38151f17a2b4
 *   MUSICBRAINZ_TRACKID: fde95c39-ee51-48f6-a7f9-b5631c2ed156
 *   Pos: 0
 *   Id: 1
 *
 * @param evbuf the response event buffer
 * @param queue_item queue item information
 * @return the number of bytes added if successful, or -1 if an error occurred.
 */
static int
mpd_add_db_queue_item(struct evbuffer *evbuf, struct db_queue_item *queue_item)
{
  char modified[32];
  int ret;

  mpd_time(modified, sizeof(modified), queue_item->time_modified);

  ret = evbuffer_add_printf(evbuf,
    "file: %s\n"
    "Last-Modified: %s\n"
    "Time: %d\n"
    "Artist: %s\n"
    "AlbumArtist: %s\n"
    "ArtistSort: %s\n"
    "AlbumArtistSort: %s\n"
    "Album: %s\n"
    "Title: %s\n"
    "Track: %d\n"
    "Date: %d\n"
    "Genre: %s\n"
      "Disc: %d\n"
      "Pos: %d\n"
      "Id: %d\n",
      (queue_item->virtual_path + 1),
    modified,
      (queue_item->song_length / 1000),
      queue_item->artist,
      queue_item->album_artist,
      queue_item->artist_sort,
      queue_item->album_artist_sort,
      queue_item->album,
      queue_item->title,
      queue_item->track,
      queue_item->year,
      queue_item->genre,
      queue_item->disc,
      queue_item->pos,
      queue_item->id);

  return ret;
}

/*
 * Adds the informations (path, id, tags, etc.) for the given song to the given buffer.
 *
 * Example output:
 *   file: foo/bar/song.mp3
 *   Last-Modified: 2013-07-14T06:57:59Z
 *   Time: 172
 *   Artist: foo
 *   AlbumArtist: foo
 *   ArtistSort: foo
 *   AlbumArtistSort: foo
 *   Title: song
 *   Album: bar
 *   Track: 1/11
 *   Date: 2012-09-11
 *   Genre: Alternative
 *   Disc: 1/1
 *   MUSICBRAINZ_ALBUMARTISTID: c5c2ea1c-4bde-4f4d-bd0b-47b200bf99d6
 *   MUSICBRAINZ_ARTISTID: c5c2ea1c-4bde-4f4d-bd0b-47b200bf99d6
 *   MUSICBRAINZ_ALBUMID: 812f4b87-8ad9-41bd-be79-38151f17a2b4
 *   MUSICBRAINZ_TRACKID: fde95c39-ee51-48f6-a7f9-b5631c2ed156
 *
 * @param evbuf the response event buffer
 * @param mfi media information
 * @return the number of bytes added if successful, or -1 if an error occurred.
 */
static int
mpd_add_db_media_file_info(struct evbuffer *evbuf, struct db_media_file_info *dbmfi)
{
  char modified[32];
  uint32_t time_modified;
  uint32_t songlength;
  int ret;

  if (safe_atou32(dbmfi->time_modified, &time_modified) != 0)
    {
      DPRINTF(E_LOG, L_MPD, "Error converting time modified to uint32_t: %s\n", dbmfi->time_modified);
      return -1;
    }

  mpd_time(modified, sizeof(modified), time_modified);

  if (safe_atou32(dbmfi->song_length, &songlength) != 0)
    {
      DPRINTF(E_LOG, L_MPD, "Error converting song length to uint32_t: %s\n", dbmfi->song_length);
      return -1;
    }

  ret = evbuffer_add_printf(evbuf,
      "file: %s\n"
      "Last-Modified: %s\n"
      "Time: %d\n"
      "Artist: %s\n"
      "AlbumArtist: %s\n"
      "ArtistSort: %s\n"
      "AlbumArtistSort: %s\n"
      "Album: %s\n"
      "Title: %s\n"
      "Track: %s\n"
      "Date: %s\n"
      "Genre: %s\n"
      "Disc: %s\n",
      (dbmfi->virtual_path + 1),
      modified,
      (songlength / 1000),
      dbmfi->artist,
      dbmfi->album_artist,
      dbmfi->artist_sort,
      dbmfi->album_artist_sort,
      dbmfi->album,
      dbmfi->title,
      dbmfi->track,
      dbmfi->year,
      dbmfi->genre,
      dbmfi->disc);

  return ret;
}

/*
 * Command handler function for 'currentsong'
 */
static int
mpd_command_currentsong(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{

  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  player_get_status(&status);

  if (status.status == PLAY_STOPPED)
    {
      // Return empty evbuffer if there is no current playing song
      return 0;
    }

  queue_item = db_queue_fetch_byitemid(status.item_id);
  if (!queue_item)
    {
      *errmsg = safe_asprintf("Error adding queue item info for file with id: %d", status.item_id);
      return ACK_ERROR_UNKNOWN;
    }

  ret = mpd_add_db_queue_item(evbuf, queue_item);

  free_queue_item(queue_item, 0);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Error adding media info for file with id: %d", status.id);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 *
 * Example input:
 * idle "database" "mixer" "options" "output" "player" "playlist" "sticker" "update"
 */
static int
mpd_command_idle(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct idle_client *client;
  int i;

  client = (struct idle_client*)malloc(sizeof(struct idle_client));
  if (!client)
    {
      DPRINTF(E_LOG, L_MPD, "Out of memory for idle_client\n");
      return ACK_ERROR_UNKNOWN;
    }

  client->evbuffer = evbuf;
  client->events = 0;
  client->next = idle_clients;

  if (argc > 1)
    {
      for (i = 1; i < argc; i++)
	{
	  if (0 == strcmp(argv[i], "database"))
	    {
	      client->events |= LISTENER_DATABASE;
	    }
	  else if (0 == strcmp(argv[i], "player"))
	    {
	      client->events |= LISTENER_PLAYER;
	    }
	  else if (0 == strcmp(argv[i], "playlist"))
	    {
	      client->events |= LISTENER_QUEUE;
	    }
	  else if (0 == strcmp(argv[i], "mixer"))
	    {
	      client->events |= LISTENER_VOLUME;
	    }
	  else if (0 == strcmp(argv[i], "output"))
	    {
	      client->events |= LISTENER_SPEAKER;
	    }
	  else if (0 == strcmp(argv[i], "options"))
	    {
	      client->events |= LISTENER_OPTIONS;
	    }
	  else if (0 == strcmp(argv[i], "stored_playlist"))
	    {
	      client->events |= LISTENER_STORED_PLAYLIST;
	    }
	  else
	    {
	      DPRINTF(E_DBG, L_MPD, "Idle command for '%s' not supported\n", argv[i]);
	    }
	}
    }
  else
    client->events = LISTENER_PLAYER | LISTENER_QUEUE | LISTENER_VOLUME | LISTENER_SPEAKER | LISTENER_OPTIONS;

  idle_clients = client;

  return 0;
}

static void
mpd_remove_idle_client(struct evbuffer *evbuf)
{
  struct idle_client *client;
  struct idle_client *prev;

  client = idle_clients;
  prev = NULL;

  while (client)
    {
      if (client->evbuffer == evbuf)
	{
	  DPRINTF(E_DBG, L_MPD, "Removing idle client for evbuffer\n");

	  if (prev)
	    prev->next = client->next;
	  else
	    idle_clients = client->next;

	  free(client);
	  break;
	}

      prev = client;
      client = client->next;
    }
}

static int
mpd_command_noidle(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  mpd_remove_idle_client(evbuf);
  return 0;
}

/*
 * Command handler function for 'status'
 *
 * Example output:
 *  volume: -1
 *  repeat: 0
 *  random: 0
 *  single: 0
 *  consume: 0
 *  playlist: 2
 *  playlistlength: 34
 *  mixrampdb: 0.000000
 *  state: stop
 *  song: 0
 *  songid: 1
 *  time: 28:306
 *  elapsed: 28.178
 *  bitrate: 278
 *  audio: 44100:f:2
 *  nextsong: 1
 *  nextsongid: 2
 */
static int
mpd_command_status(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct player_status status;
  int queue_length;
  int queue_version;
  char *state;
  int pos_pl;
  struct db_queue_item *next_item;

  player_get_status(&status);

  switch (status.status)
    {
      case PLAY_PAUSED:
	state = "pause";
	break;

      case PLAY_PLAYING:
	state = "play";
	break;

      default:
	state = "stop";
	break;
    }

  queue_version = db_queue_get_version();
  queue_length = db_queue_get_count();

  evbuffer_add_printf(evbuf,
      "volume: %d\n"
      "repeat: %d\n"
      "random: %d\n"
      "single: %d\n"
      "consume: %d\n"
      "playlist: %d\n"
      "playlistlength: %d\n"
      "mixrampdb: 0.000000\n"
      "state: %s\n",
      status.volume,
      (status.repeat == REPEAT_OFF ? 0 : 1),
      status.shuffle,
      (status.repeat == REPEAT_SONG ? 1 : 0),
      status.consume,
      queue_version,
      queue_length,
      state);

  if (status.status != PLAY_STOPPED)
    {
      pos_pl = db_queue_get_pos(status.item_id, 0);

      evbuffer_add_printf(evbuf,
	  "song: %d\n"
	  "songid: %d\n"
	  "time: %d:%d\n"
	  "elapsed: %#.3f\n"
	  "bitrate: 128\n"
	  "audio: 44100:16:2\n",
	  pos_pl,
	  status.item_id,
	  (status.pos_ms / 1000), (status.len_ms / 1000),
	  (status.pos_ms / 1000.0));
    }

  if (library_is_scanning())
    {
      evbuffer_add(evbuf, "updating_db: 1\n", 15);
    }

  if (status.status != PLAY_STOPPED)
    {
      next_item = db_queue_fetch_next(status.item_id, status.shuffle);
      if (next_item)
	{
      evbuffer_add_printf(evbuf,
	  "nextsong: %d\n"
	  "nextsongid: %d\n",
	      next_item->id,
	      next_item->pos);

	  free_queue_item(next_item, 0);
	}
    }

  return 0;
}

/*
 * Command handler function for 'stats'
 */
static int
mpd_command_stats(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct filecount_info fci;
  int artists;
  int albums;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_COUNT_ITEMS;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  ret = db_query_fetch_count(&qp, &fci);
  if (ret < 0)
    {
      db_query_end(&qp);

      *errmsg = safe_asprintf("Could not fetch query count");
      return ACK_ERROR_UNKNOWN;
    }

  db_query_end(&qp);

  artists = db_files_get_artist_count();
  albums = db_files_get_album_count();

  //TODO [mpd] Implement missing stats attributes (uptime, db_update, playtime)
  evbuffer_add_printf(evbuf,
      "artists: %d\n"
      "albums: %d\n"
      "songs: %d\n"
      "uptime: %d\n" //in seceonds
      "db_playtime: %" PRIu64 "\n"
      "db_update: %d\n"
      "playtime: %d\n",
        artists,
        albums,
        fci.count,
        4,
        (fci.length / 1000),
        6,
        7);

  return 0;
}

/*
 * Command handler function for 'consume'
 * Sets the consume mode, expects argument argv[1] to be an integer with
 *   0 = disable consume
 *   1 = enable consume
 */
static int
mpd_command_consume(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int enable;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'consume'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &enable);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  player_consume_set(enable);
  return 0;
}

/*
 * Command handler function for 'random'
 * Sets the shuffle mode, expects argument argv[1] to be an integer with
 *   0 = disable shuffle
 *   1 = enable shuffle
 */
static int
mpd_command_random(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int enable;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'random'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &enable);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  player_shuffle_set(enable);
  return 0;
}

/*
 * Command handler function for 'repeat'
 * Sets the repeat mode, expects argument argv[1] to be an integer with
 *   0 = repeat off
 *   1 = repeat all
 */
static int
mpd_command_repeat(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int enable;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'repeat'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &enable);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  if (enable == 0)
    player_repeat_set(REPEAT_OFF);
  else
    player_repeat_set(REPEAT_ALL);

  return 0;
}

/*
 * Command handler function for 'setvol'
 * Sets the volume, expects argument argv[1] to be an integer 0-100
 */
static int
mpd_command_setvol(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int volume;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'setvol'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &volume);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  player_volume_set(volume);

  return 0;
}

/*
 * Command handler function for 'single'
 * Sets the repeat mode, expects argument argv[1] to be an integer.
 * forked-daapd only allows single-mode in combination with repeat, therefor the command
 * single translates (depending on the current repeat mode) into:
 * a) if repeat off:
 *   0 = repeat off
 *   1 = repeat song
 * b) if repeat all:
 *   0 = repeat all
 *   1 = repeat song
 * c) if repeat song:
 *   0 = repeat all
 *   1 = repeat song
 */
static int
mpd_command_single(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int enable;
  struct player_status status;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'single'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &enable);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  player_get_status(&status);

  if (enable == 0 && status.repeat != REPEAT_OFF)
    player_repeat_set(REPEAT_ALL);
  else if (enable == 0)
    player_repeat_set(REPEAT_OFF);
  else
    player_repeat_set(REPEAT_SONG);

  return 0;
}

/*
 * Command handler function for 'replay_gain_status'
 * forked-daapd does not support replay gain, therefor this function returns always
 * "replay_gain_mode: off".
 */
static int
mpd_command_replay_gain_status(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  evbuffer_add(evbuf, "replay_gain_mode: off\n", 22);
  return 0;
}

/*
 * Command handler function for 'volume'
 * Changes the volume by the given amount, expects argument argv[1] to be an integer
 *
 * According to the mpd protocoll specification this function is deprecated.
 */
static int
mpd_command_volume(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct player_status status;
  int volume;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'volume'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &volume);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  player_get_status(&status);

  volume += status.volume;

  player_volume_set(volume);

  return 0;
}

/*
 * Command handler function for 'next'
 * Skips to the next song in the playqueue
 */
static int
mpd_command_next(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int ret;

  ret = player_playback_next();

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to skip to next song");
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Player returned an error for start after nextitem");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'pause'
 * Toggles pause/play, if the optional argument argv[1] is present, it must be an integer with
 *   0 = play
 *   1 = pause
 */
static int
mpd_command_pause(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int pause;
  struct player_status status;
  int ret;

  pause = 1;
  if (argc > 1)
    {
      ret = safe_atoi32(argv[1], &pause);
      if (ret < 0)
	{
	  *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
	  return ACK_ERROR_ARG;
	}
    }
  else
    {
      player_get_status(&status);

      if (status.status != PLAY_PLAYING)
	pause = 0;
    }

  if (pause == 1)
    ret = player_playback_pause();
  else
    ret = player_playback_start();

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to pause playback");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'play'
 * Starts playback, the optional argument argv[1] represents the position in the playqueue
 * where to start playback.
 */
static int
mpd_command_play(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int songpos;
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  songpos = 0;
  if (argc > 1)
    {
      ret = safe_atoi32(argv[1], &songpos);
      if (ret < 0)
	{
	  *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
	  return ACK_ERROR_ARG;
	}
    }

  player_get_status(&status);

  if (status.status == PLAY_PLAYING && songpos < 0)
    {
      DPRINTF(E_DBG, L_MPD, "Ignoring play command with parameter '%s', player is already playing.\n", argv[1]);
      return 0;
    }

  if (status.status == PLAY_PLAYING)
    {
      // Stop playback, if player is already playing and a valid song position is given (it will be restarted for the given song position)
      player_playback_stop();
    }

  if (songpos > 0)
    {
      queue_item = db_queue_fetch_bypos(songpos, 0);
      if (!queue_item)
	{
	  *errmsg = safe_asprintf("Failed to start playback");
	  return ACK_ERROR_UNKNOWN;
	}

      ret = player_playback_start_byitem(queue_item);
      free_queue_item(queue_item, 0);
    }
  else
    ret = player_playback_start();

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to start playback");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'playid'
 * Starts playback, the optional argument argv[1] represents the songid of the song
 * where to start playback.
 */
static int
mpd_command_playid(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  uint32_t id;
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  player_get_status(&status);

  id = 0;
  if (argc > 1)
    {
      //TODO [mpd] mpd allows passing "-1" as argument and simply ignores it, forked-daapd fails to convert "-1" to an unsigned int
      ret = safe_atou32(argv[1], &id);
      if (ret < 0)
	{
	  *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
	  return ACK_ERROR_ARG;
	}
    }

  if (status.status == PLAY_PLAYING)
    {
      // Stop playback, if player is already playing and a valid item id is given (it will be restarted for the given song)
      player_playback_stop();
    }

  if (id > 0)
    {
      queue_item = db_queue_fetch_byitemid(id);
      if (!queue_item)
	{
	  *errmsg = safe_asprintf("Failed to start playback");
	  return ACK_ERROR_UNKNOWN;
	}

      ret = player_playback_start_byitem(queue_item);
      free_queue_item(queue_item, 0);
    }
  else
    ret = player_playback_start();

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to start playback");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'previous'
 * Skips to the previous song in the playqueue
 */
static int
mpd_command_previous(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int ret;

  ret = player_playback_prev();

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to skip to previous song");
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Player returned an error for start after previtem");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'seekid'
 * Seeks to song at the given position in argv[1] to the position in seconds given in argument argv[2]
 * (fractions allowed).
 */
static int
mpd_command_seek(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  uint32_t songpos;
  float seek_target_sec;
  int seek_target_msec;
  int ret;

  if (argc < 3)
    {
      *errmsg = safe_asprintf("Missing argument for command 'seek'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &songpos);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  //TODO Allow seeking in songs not currently playing

  seek_target_sec = strtof(argv[2], NULL);
  seek_target_msec = seek_target_sec * 1000;

  ret = player_playback_seek(seek_target_msec);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to seek current song to time %d msec", seek_target_msec);
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start();
    if (ret < 0)
      {
	*errmsg = safe_asprintf("Player returned an error for start after seekcur");
	return ACK_ERROR_UNKNOWN;
      }

  return 0;
}

/*
 * Command handler function for 'seekid'
 * Seeks to song with id given in argv[1] to the position in seconds given in argument argv[2]
 * (fractions allowed).
 */
static int
mpd_command_seekid(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct player_status status;
  uint32_t id;
  float seek_target_sec;
  int seek_target_msec;
  int ret;

  if (argc < 3)
    {
      *errmsg = safe_asprintf("Missing argument for command 'seekcur'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &id);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  //TODO Allow seeking in songs not currently playing
  player_get_status(&status);
  if (status.item_id != id)
    {
      *errmsg = safe_asprintf("Given song is not the current playing one, seeking is not supported");
      return ACK_ERROR_UNKNOWN;
    }

  seek_target_sec = strtof(argv[2], NULL);
  seek_target_msec = seek_target_sec * 1000;

  ret = player_playback_seek(seek_target_msec);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to seek current song to time %d msec", seek_target_msec);
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Player returned an error for start after seekcur");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'seekcur'
 * Seeks the current song to the position in seconds given in argument argv[1] (fractions allowed).
 */
static int
mpd_command_seekcur(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  float seek_target_sec;
  int seek_target_msec;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'seekcur'");
      return ACK_ERROR_ARG;
    }

  seek_target_sec = strtof(argv[1], NULL);
  seek_target_msec = seek_target_sec * 1000;

  // TODO If prefixed by '+' or '-', then the time is relative to the current playing position.
  ret = player_playback_seek(seek_target_msec);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to seek current song to time %d msec", seek_target_msec);
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Player returned an error for start after seekcur");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'stop'
 * Stop playback.
 */
static int
mpd_command_stop(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int ret;

  ret = player_playback_stop();

  if (ret != 0)
    {
      *errmsg = safe_asprintf("Failed to stop playback");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Add media file item with given virtual path to the queue
 *
 * @param path The virtual path
 * @param recursive If 0 add only item with exact match, otherwise add all items virtual path start with the given path
 * @return The queue item id of the last inserted item or -1 on failure
 */
static int
mpd_queue_add(char *path, int recursive)
{
  struct query_params qp;
  struct player_status status;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_ITEMS;
  qp.idx_type = I_NONE;
  qp.sort = S_ARTIST;

  if (recursive)
    {
      qp.filter = sqlite3_mprintf("f.disabled = 0 AND f.virtual_path LIKE '/%q%%'", path);
      if (!qp.filter)
	DPRINTF(E_DBG, L_PLAYER, "Out of memory\n");
    }
  else
    {
      qp.filter = sqlite3_mprintf("f.disabled = 0 AND f.virtual_path LIKE '/%q'", path);
      if (!qp.filter)
	DPRINTF(E_DBG, L_PLAYER, "Out of memory\n");
    }

  player_get_status(&status);

  ret = db_queue_add_by_query(&qp, status.shuffle, status.item_id);

  sqlite3_free(qp.filter);
  return ret;
}

/*
 * Command handler function for 'add'
 * Adds the all songs under the given path to the end of the playqueue (directories add recursively).
 * Expects argument argv[1] to be a path to a single file or directory.
 */
static int
mpd_command_add(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct media_file_info mfi;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'add'");
      return ACK_ERROR_ARG;
    }

  ret = mpd_queue_add(argv[1], 1);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to add song '%s' to playlist", argv[1]);
      return ACK_ERROR_UNKNOWN;
    }

  if (ret == 0)
    {
      // Given path is not in the library, check if it is possible to add as a non-library queue item
      ret = library_scan_media(argv[1], &mfi);
      if (ret != LIBRARY_OK)
	{
	  *errmsg = safe_asprintf("Failed to add song '%s' to playlist (unkown path)", argv[1]);
	  return ACK_ERROR_UNKNOWN;
	}

      library_add_queue_item(&mfi);
      free_mfi(&mfi, 1);
    }

  return 0;
}

/*
 * Command handler function for 'addid'
 * Adds the song under the given path to the end or to the given position of the playqueue.
 * Expects argument argv[1] to be a path to a single file. argv[2] is optional, if present
 * it must be an integer representing the position in the playqueue.
 */
static int
mpd_command_addid(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct media_file_info mfi;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'addid'");
      return ACK_ERROR_ARG;
    }

  //TODO if argc > 2 add song at position argv[2]
  if (argc > 2)
    {
      DPRINTF(E_LOG, L_MPD, "Adding at a specified position not supported for 'addid', adding songs at end of queue.\n");
    }

  ret = mpd_queue_add(argv[1], 0);

  if (ret == 0)
    {
      // Given path is not in the library, directly add it as a new queue item
      ret = library_scan_media(argv[1], &mfi);
      if (ret != LIBRARY_OK)
	{
	  *errmsg = safe_asprintf("Failed to add song '%s' to playlist (unkown path)", argv[1]);
	  return ACK_ERROR_UNKNOWN;
	}

      ret = library_add_queue_item(&mfi);
      free_mfi(&mfi, 1);
    }

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to add song '%s' to playlist", argv[1]);
      return ACK_ERROR_UNKNOWN;
    }

  evbuffer_add_printf(evbuf,
      "Id: %d\n",
      ret); // mpd_queue_add returns the item_id of the last inserted queue item

  return 0;
}

/*
 * Command handler function for 'clear'
 * Stops playback and removes all songs from the playqueue
 */
static int
mpd_command_clear(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int ret;

  ret = player_playback_stop();
  if (ret != 0)
    {
      DPRINTF(E_DBG, L_MPD, "Failed to stop playback\n");
    }

  db_queue_clear(0);

  return 0;
}

/*
 * Command handler function for 'delete'
 * Removes songs from the playqueue. Expects argument argv[1] (optional) to be an integer or
 * an integer range {START:END} representing the position of the songs in the playlist, that
 * should be removed.
 */
static int
mpd_command_delete(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int start_pos;
  int end_pos;
  int count;
  int ret;

  // If argv[1] is ommited clear the whole queue
  if (argc < 2)
    {
      db_queue_clear(0);
      return 0;
    }

  // If argument argv[1] is present remove only the specified songs
  ret = mpd_pars_range_arg(argv[1], &start_pos, &end_pos);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer or range: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  count = end_pos - start_pos;

  ret = db_queue_delete_bypos(start_pos, count);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to remove %d songs starting at position %d", count, start_pos);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'deleteid'
 * Removes the song with given id from the playqueue. Expects argument argv[1] to be an integer (song id).
 */
static int
mpd_command_deleteid(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  uint32_t songid;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'deleteid'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &songid);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  ret = db_queue_delete_byitemid(songid);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to remove song with id '%s'", argv[1]);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

//Moves the song at FROM or range of songs at START:END to TO in the playlist.
static int
mpd_command_move(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int start_pos;
  int end_pos;
  int count;
  uint32_t to_pos;
  int ret;

  if (argc < 3)
    {
      *errmsg = safe_asprintf("Missing argument for command 'move'");
      return ACK_ERROR_ARG;
    }

  ret = mpd_pars_range_arg(argv[1], &start_pos, &end_pos);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer or range: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  count = end_pos - start_pos;
  if (count > 1)
    DPRINTF(E_WARN, L_MPD, "Moving ranges is not supported, only the first item will be moved\n");

  ret = safe_atou32(argv[2], &to_pos);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[2]);
      return ACK_ERROR_ARG;
    }

  ret = db_queue_move_bypos(start_pos, to_pos);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to move song at position %d to %d", start_pos, to_pos);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

static int
mpd_command_moveid(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  uint32_t songid;
  uint32_t to_pos;
  int ret;

  if (argc < 3)
    {
      *errmsg = safe_asprintf("Missing argument for command 'moveid'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &songid);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[2], &to_pos);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[2]);
      return ACK_ERROR_ARG;
    }

  ret = db_queue_move_byitemid(songid, to_pos, 0);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to move song with id '%s' to index '%s'", argv[1], argv[2]);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'playlistid'
 * Displays a list of all songs in the queue, or if the optional argument is given, displays information
 * only for the song with ID.
 *
 * The order of the songs is always the not shuffled order.
 */
static int
mpd_command_playlistid(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params query_params;
  struct db_queue_item queue_item;
  uint32_t songid;
  int ret;

  songid = 0;

  if (argc > 1)
    {
      ret = safe_atou32(argv[1], &songid);
      if (ret < 0)
	{
	  *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
	  return ACK_ERROR_ARG;
	}
    }

  memset(&query_params, 0, sizeof(struct query_params));

  if (songid > 0)
    query_params.filter = sqlite3_mprintf("id = %d", songid);

  ret = db_queue_enum_start(&query_params);
  if (ret < 0)
    {
      sqlite3_free(query_params.filter);
      *errmsg = safe_asprintf("Failed to start queue enum for command playlistid: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  while ((ret = db_queue_enum_fetch(&query_params, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(evbuf, &queue_item);
	  if (ret < 0)
	    {
	      *errmsg = safe_asprintf("Error adding media info for file with id: %d", queue_item.file_id);

	      db_queue_enum_end(&query_params);
	      sqlite3_free(query_params.filter);
	      return ACK_ERROR_UNKNOWN;
	    }
	}

  db_queue_enum_end(&query_params);
  sqlite3_free(query_params.filter);

  return 0;
}


/*
 * Command handler function for 'playlistinfo'
 * Displays a list of all songs in the queue, or if the optional argument is given, displays information
 * only for the song SONGPOS or the range of songs START:END given in argv[1].
 *
 * The order of the songs is always the not shuffled order.
 */
static int
mpd_command_playlistinfo(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params query_params;
  struct db_queue_item queue_item;
  int start_pos;
  int end_pos;
  int ret;

  start_pos = 0;
  end_pos = 0;
  memset(&query_params, 0, sizeof(struct query_params));

  if (argc > 1)
    {
      ret = mpd_pars_range_arg(argv[1], &start_pos, &end_pos);
      if (ret < 0)
	{
	  *errmsg = safe_asprintf("Argument doesn't convert to integer or range: '%s'", argv[1]);
	  return ACK_ERROR_ARG;
	}

  if (start_pos < 0)
      DPRINTF(E_DBG, L_MPD, "Command 'playlistinfo' called with pos < 0 (arg = '%s'), ignore arguments and return whole queue\n", argv[1]);
      else
	query_params.filter = sqlite3_mprintf("pos >= %d AND pos < %d", start_pos, end_pos);
    }

  ret = db_queue_enum_start(&query_params);
  if (ret < 0)
    {
      sqlite3_free(query_params.filter);
      *errmsg = safe_asprintf("Failed to start queue enum for command playlistinfo: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  while ((ret = db_queue_enum_fetch(&query_params, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(evbuf, &queue_item);
      if (ret < 0)
	{
	  *errmsg = safe_asprintf("Error adding media info for file with id: %d", queue_item.file_id);

	  db_queue_enum_end(&query_params);
	  sqlite3_free(query_params.filter);
	  return ACK_ERROR_UNKNOWN;
	}
    }

  db_queue_enum_end(&query_params);
  sqlite3_free(query_params.filter);

  return 0;
}

/*
 * Command handler function for 'plchanges'
 * Lists all changed songs in the queue since the given playlist version in argv[1].
 */
static int
mpd_command_plchanges(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params query_params;
  struct db_queue_item queue_item;
  int ret;

  /*
   * forked-daapd does not keep track of changes in the queue based on the playlist version,
   * therefor plchanges returns all songs in the queue as changed ignoring the given version.
   */
  memset(&query_params, 0, sizeof(struct query_params));

  ret = db_queue_enum_start(&query_params);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to start queue enum for command plchanges");
      return ACK_ERROR_ARG;
    }

  while ((ret = db_queue_enum_fetch(&query_params, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(evbuf, &queue_item);
      if (ret < 0)
	{
	  *errmsg = safe_asprintf("Error adding media info for file with id: %d", queue_item.file_id);

	  db_queue_enum_end(&query_params);
	  return ACK_ERROR_UNKNOWN;
	}
    }

  db_queue_enum_end(&query_params);

  return 0;
}

/*
 * Command handler function for 'plchangesposid'
 * Lists all changed songs in the queue since the given playlist version in argv[1] without metadata.
 */
static int
mpd_command_plchangesposid(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params query_params;
  struct db_queue_item queue_item;
  int ret;

  /*
   * forked-daapd does not keep track of changes in the queue based on the playlist version,
   * therefor plchangesposid returns all songs in the queue as changed ignoring the given version.
   */
  memset(&query_params, 0, sizeof(struct query_params));

  ret = db_queue_enum_start(&query_params);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to start queue enum for command plchangesposid");
      return ACK_ERROR_ARG;
    }

  while ((ret = db_queue_enum_fetch(&query_params, &queue_item)) == 0 && queue_item.id > 0)
    {
      evbuffer_add_printf(evbuf,
      	  "cpos: %d\n"
      	  "Id: %d\n",
      	  queue_item.pos,
	  queue_item.id);
    }

  db_queue_enum_end(&query_params);

  return 0;
}

/*
 * Command handler function for 'listplaylist'
 * Lists all songs in the playlist given by virtual-path in argv[1].
 */
static int
mpd_command_listplaylist(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  char path[PATH_MAX];
  struct playlist_info *pli;
  struct query_params qp;
  struct db_media_file_info dbmfi;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'listplaylist'");
      return ACK_ERROR_ARG;
    }

  if (strncmp(argv[1], "/", 1) == 0)
    ret = snprintf(path, sizeof(path), "%s", argv[1]);
  else
    ret = snprintf(path, sizeof(path), "/%s", argv[1]);

  if (ret >= sizeof(path))
    {
      *errmsg = safe_asprintf("Length of path exceeds the PATH_MAX value '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  pli = db_pl_fetch_byvirtualpath(path);
  if (!pli)
    {
      *errmsg = safe_asprintf("Playlist not found for path '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_PLITEMS;
  qp.idx_type = I_NONE;
  qp.id = pli->id;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);

      free_pli(pli, 0);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      evbuffer_add_printf(evbuf,
	  "file: %s\n",
	  (dbmfi.virtual_path + 1));
    }

  db_query_end(&qp);

  free_pli(pli, 0);

  return 0;
}

/*
 * Command handler function for 'listplaylistinfo'
 * Lists all songs in the playlist given by virtual-path in argv[1] with metadata.
 */
static int
mpd_command_listplaylistinfo(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  char path[PATH_MAX];
  struct playlist_info *pli;
  struct query_params qp;
  struct db_media_file_info dbmfi;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'listplaylistinfo'");
      return ACK_ERROR_ARG;
    }

  if (strncmp(argv[1], "/", 1) == 0)
    ret = snprintf(path, sizeof(path), "%s", argv[1]);
  else
    ret = snprintf(path, sizeof(path), "/%s", argv[1]);

  if (ret >= sizeof(path))
    {
      *errmsg = safe_asprintf("Length of path exceeds the PATH_MAX value '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  pli = db_pl_fetch_byvirtualpath(path);
  if (!pli)
    {
      *errmsg = safe_asprintf("Playlist not found for path '%s'", argv[1]);
      return ACK_ERROR_NO_EXIST;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_PLITEMS;
  qp.idx_type = I_NONE;
  qp.id = pli->id;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);

      free_pli(pli, 0);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = mpd_add_db_media_file_info(evbuf, &dbmfi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error adding song to the evbuffer, song id: %s\n", dbmfi.id);
	}
    }

  db_query_end(&qp);

  free_pli(pli, 0);

  return 0;
}

/*
 * Command handler function for 'listplaylists'
 * Lists all playlists with their last modified date.
 */
static int
mpd_command_listplaylists(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct db_playlist_info dbpli;
  char modified[32];
  uint32_t time_modified;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_PL;
  qp.sort = S_PLAYLIST;
  qp.idx_type = I_NONE;
  qp.filter = sqlite3_mprintf("(f.type = %d OR f.type = %d)", PL_PLAIN, PL_SMART);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  while (((ret = db_query_fetch_pl(&qp, &dbpli, 0)) == 0) && (dbpli.id))
    {
      if (safe_atou32(dbpli.db_timestamp, &time_modified) != 0)
        {
          *errmsg = safe_asprintf("Error converting time modified to uint32_t: %s\n", dbpli.db_timestamp);
          return ACK_ERROR_UNKNOWN;
        }

      mpd_time(modified, sizeof(modified), time_modified);

      evbuffer_add_printf(evbuf,
	  "playlist: %s\n"
	  "Last-Modified: %s\n",
	  (dbpli.virtual_path + 1),
	  modified);
    }

  db_query_end(&qp);

  sqlite3_free(qp.filter);

  return 0;
}

/*
 * Command handler function for 'load'
 * Adds the playlist given by virtual-path in argv[1] to the queue.
 */
static int
mpd_command_load(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  char path[PATH_MAX];
  struct playlist_info *pli;
  struct player_status status;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'load'");
      return ACK_ERROR_ARG;
    }

  if (strncmp(argv[1], "/", 1) == 0)
    ret = snprintf(path, sizeof(path), "%s", argv[1]);
  else
    ret = snprintf(path, sizeof(path), "/%s", argv[1]);

  if (ret >= sizeof(path))
    {
      *errmsg = safe_asprintf("Length of path exceeds the PATH_MAX value '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  pli = db_pl_fetch_byvirtualpath(path);
  if (!pli)
    {
      *errmsg = safe_asprintf("Playlist not found for path '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  //TODO If a second parameter is given only add the specified range of songs to the playqueue

  player_get_status(&status);

  ret = db_queue_add_by_playlistid(pli->id, status.shuffle, status.item_id);
  free_pli(pli, 0);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to add song '%s' to playlist", argv[1]);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

static int
mpd_command_playlistadd(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  char *vp_playlist;
  char *vp_item;
  int ret;

  if (!allow_modifying_stored_playlists)
    {
      *errmsg = safe_asprintf("Modifying stored playlists is not enabled");
      return ACK_ERROR_PERMISSION;
    }

  if (argc < 3)
    {
      *errmsg = safe_asprintf("Missing argument for command 'playlistadd'");
      return ACK_ERROR_ARG;
    }

  if (!default_pl_dir || strstr(argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      vp_playlist = prepend_slash(argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      vp_playlist = safe_asprintf("%s/%s", default_pl_dir, argv[1]);
    }

  vp_item = prepend_slash(argv[2]);

  ret = library_playlist_add(vp_playlist, vp_item);
  free(vp_playlist);
  free(vp_item);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Error saving queue to file '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  return 0;
}

static int
mpd_command_rm(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  char *virtual_path;
  int ret;

  if (!allow_modifying_stored_playlists)
    {
      *errmsg = safe_asprintf("Modifying stored playlists is not enabled");
      return ACK_ERROR_PERMISSION;
    }

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'rm'");
      return ACK_ERROR_ARG;
    }

  if (!default_pl_dir || strstr(argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      virtual_path = prepend_slash(argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      virtual_path = safe_asprintf("%s/%s", default_pl_dir, argv[1]);
    }

  ret = library_playlist_remove(virtual_path);
  free(virtual_path);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Error removing playlist '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  return 0;
}

static int
mpd_command_save(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  char *virtual_path;
  int ret;

  if (!allow_modifying_stored_playlists)
    {
      *errmsg = safe_asprintf("Modifying stored playlists is not enabled");
      return ACK_ERROR_PERMISSION;
    }

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'save'");
      return ACK_ERROR_ARG;
    }

  if (!default_pl_dir || strstr(argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      virtual_path = prepend_slash(argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      virtual_path = safe_asprintf("%s/%s", default_pl_dir, argv[1]);
    }

  ret = library_queue_save(virtual_path);
  free(virtual_path);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Error saving queue to file '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  return 0;
}

static int
mpd_get_query_params_find(int argc, char **argv, struct query_params *qp)
{
  char *c1;
  char *c2;
  int start_pos;
  int end_pos;
  int i;
  uint32_t num;
  int ret;

  c1 = NULL;
  c2 = NULL;

  for (i = 0; i < argc; i += 2)
    {
      if (0 == strcasecmp(argv[i], "any"))
	{
	  c1 = sqlite3_mprintf("(f.artist LIKE '%%%q%%' OR f.album LIKE '%%%q%%' OR f.title LIKE '%%%q%%')", argv[i + 1], argv[i + 1], argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "file"))
	{
	  c1 = sqlite3_mprintf("(f.virtual_path = '/%q')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "base"))
	{
	  c1 = sqlite3_mprintf("(f.virtual_path LIKE '/%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "modified-since"))
	{
	  DPRINTF(E_WARN, L_MPD, "Special parameter 'modified-since' is not supported by forked-daapd and will be ignored\n");
	}
      else if (0 == strcasecmp(argv[i], "window"))
	{
	  ret = mpd_pars_range_arg(argv[i + 1], &start_pos, &end_pos);
	  if (ret == 0)
	    {
	      qp->idx_type = I_SUB;
	      qp->limit = end_pos - start_pos;
	      qp->offset = start_pos;
	    }
	  else
	    {
	      DPRINTF(E_LOG, L_MPD, "Window argument doesn't convert to integer or range: '%s'\n", argv[i + 1]);
	    }
	}
      else if (0 == strcasecmp(argv[i], "artist"))
	{
	  c1 = sqlite3_mprintf("(f.artist = '%q')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "albumartist"))
	{
	  c1 = sqlite3_mprintf("(f.album_artist = '%q')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "album"))
	{
	  c1 = sqlite3_mprintf("(f.album = '%q')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "title"))
	{
	  c1 = sqlite3_mprintf("(f.title = '%q')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "genre"))
	{
	  c1 = sqlite3_mprintf("(f.genre = '%q')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "disc"))
	{
	  ret = safe_atou32(argv[i + 1], &num);
	  if (ret < 0)
	    DPRINTF(E_WARN, L_MPD, "Disc parameter '%s' is not an integer and will be ignored\n", argv[i + 1]);
	  else
	    c1 = sqlite3_mprintf("(f.disc = %d)", num);
	}
      else if (0 == strcasecmp(argv[i], "track"))
	{
	  ret = safe_atou32(argv[i + 1], &num);
	  if (ret < 0)
	    DPRINTF(E_WARN, L_MPD, "Track parameter '%s' is not an integer and will be ignored\n", argv[i + 1]);
	  else
	    c1 = sqlite3_mprintf("(f.track = %d)", num);
	}
      else if (0 == strcasecmp(argv[i], "date"))
	{
	  ret = safe_atou32(argv[i + 1], &num);
	  if (ret < 0)
	    c1 = sqlite3_mprintf("(f.year = 0 OR f.year IS NULL)");
	  else
	    c1 = sqlite3_mprintf("(f.year = %d)", num);
	}
      else if (i == 0 && argc == 1)
	{
	  // Special case: a single token is allowed if listing albums for an artist
	  c1 = sqlite3_mprintf("(f.album_artist = '%q')", argv[i]);
	}
      else
	{
	  DPRINTF(E_WARN, L_MPD, "Parameter '%s' is not supported by forked-daapd and will be ignored\n", argv[i]);
	}

      if (c1)
	{
	  if (qp->filter)
	    c2 = sqlite3_mprintf("%s AND %s", qp->filter, c1);
	  else
	    c2 = sqlite3_mprintf("%s", c1);

	  if (qp->filter)
	    sqlite3_free(qp->filter);

	  qp->filter = c2;
	  c2 = NULL;
	  sqlite3_free(c1);
	  c1 = NULL;
	}
    }

  return 0;
}

static int
mpd_command_count(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct filecount_info fci;
  int ret;

  if (argc < 3 || ((argc - 1) % 2) != 0)
    {
      *errmsg = safe_asprintf("Missing argument(s) for command 'find'");
      return ACK_ERROR_ARG;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_COUNT_ITEMS;

  mpd_get_query_params_find(argc - 1, argv + 1, &qp);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      sqlite3_free(qp.filter);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  ret = db_query_fetch_count(&qp, &fci);
  if (ret < 0)
    {
      db_query_end(&qp);
      sqlite3_free(qp.filter);

      *errmsg = safe_asprintf("Could not fetch query count");
      return ACK_ERROR_UNKNOWN;
    }

  evbuffer_add_printf(evbuf,
      "songs: %d\n"
      "playtime: %" PRIu64 "\n",
        fci.count,
        (fci.length / 1000));

  db_query_end(&qp);
  sqlite3_free(qp.filter);

  return 0;
}

static int
mpd_command_find(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  int ret;

  if (argc < 3 || ((argc - 1) % 2) != 0)
    {
      *errmsg = safe_asprintf("Missing argument(s) for command 'find'");
      return ACK_ERROR_ARG;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_ITEMS;
  qp.sort = S_NAME;
  qp.idx_type = I_NONE;

  mpd_get_query_params_find(argc - 1, argv + 1, &qp);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      sqlite3_free(qp.filter);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = mpd_add_db_media_file_info(evbuf, &dbmfi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error adding song to the evbuffer, song id: %s\n", dbmfi.id);
	}
    }

  db_query_end(&qp);
  sqlite3_free(qp.filter);

  return 0;
}

static int
mpd_command_findadd(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct player_status status;
  int ret;

  if (argc < 3 || ((argc - 1) % 2) != 0)
    {
      *errmsg = safe_asprintf("Missing argument(s) for command 'findadd'");
      return ACK_ERROR_ARG;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_ITEMS;
  qp.sort = S_ARTIST;
  qp.idx_type = I_NONE;

  mpd_get_query_params_find(argc - 1, argv + 1, &qp);

  player_get_status(&status);

  ret = db_queue_add_by_query(&qp, status.shuffle, status.item_id);
  sqlite3_free(qp.filter);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to add songs to playlist");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

static int
mpd_command_list(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct db_group_info dbgri;
  char *type;
  char *browse_item;
  char *sort_item;
  int ret;

  if (argc < 2 || ((argc % 2) != 0))
    {
      if (argc != 3 || (0 != strcasecmp(argv[1], "album")))
	{
	  *errmsg = safe_asprintf("Missing argument(s) for command 'list'");
	  return ACK_ERROR_ARG;
	}
    }

  memset(&qp, 0, sizeof(struct query_params));

  if (0 == strcasecmp(argv[1], "artist"))
    {
      qp.type = Q_GROUP_ARTISTS;
      qp.sort = S_ARTIST;
      type = "Artist: ";
    }
  else if (0 == strcasecmp(argv[1], "albumartist"))
      {
        qp.type = Q_GROUP_ARTISTS;
        qp.sort = S_ARTIST;
        type = "AlbumArtist: ";
      }
  else if (0 == strcasecmp(argv[1], "album"))
    {
      qp.type = Q_GROUP_ALBUMS;
      qp.sort = S_ALBUM;
      type = "Album: ";
    }
  else if (0 == strcasecmp(argv[1], "date"))
    {
      qp.type = Q_BROWSE_YEARS;
      qp.sort = S_YEAR;
      type = "Date: ";
    }
  else if (0 == strcasecmp(argv[1], "genre"))
    {
      qp.type = Q_BROWSE_GENRES;
      qp.sort = S_GENRE;
      type = "Genre: ";
    }
  else if (0 == strcasecmp(argv[1], "disc"))
    {
      qp.type = Q_BROWSE_DISCS;
      qp.sort = S_DISC;
      type = "Disc: ";
    }
  else if (0 == strcasecmp(argv[1], "track"))
    {
      qp.type = Q_BROWSE_TRACKS;
      qp.sort = S_TRACK;
      type = "Track: ";
    }
  else if (0 == strcasecmp(argv[1], "file"))
    {
      qp.type = Q_BROWSE_VPATH;
      qp.sort = S_VPATH;
      type = "file: ";
    }
  else
    {
      DPRINTF(E_WARN, L_MPD, "Unsupported type argument for command 'list': %s\n", argv[1]);
      return 0;
    }

  qp.idx_type = I_NONE;

  if (argc > 2)
    {
      mpd_get_query_params_find(argc - 2, argv + 2, &qp);
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      sqlite3_free(qp.filter);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  if (qp.type & Q_F_BROWSE)
    {
      if (qp.type == Q_BROWSE_VPATH)
	{
	  while (((ret = db_query_fetch_string_sort(&qp, &browse_item, &sort_item)) == 0) && (browse_item))
	    {
		// Remove the first "/" from the virtual_path
		evbuffer_add_printf(evbuf,
		      "%s%s\n",
		      type,
		      (browse_item + 1));
	    }
	}
      else
	{
	  while (((ret = db_query_fetch_string_sort(&qp, &browse_item, &sort_item)) == 0) && (browse_item))
	    {
		evbuffer_add_printf(evbuf,
		      "%s%s\n",
		      type,
		      browse_item);
	    }
	}
    }
  else
    {
      while ((ret = db_query_fetch_group(&qp, &dbgri)) == 0)
	{
	  evbuffer_add_printf(evbuf,
		"%s%s\n",
		type,
		dbgri.itemname);
	}
    }

  db_query_end(&qp);
  sqlite3_free(qp.filter);

  return 0;
}

static int
mpd_add_directory(struct evbuffer *evbuf, int directory_id, int listall, int listinfo, char **errmsg)
{
  struct directory_info subdir;
  struct query_params qp;
  struct directory_enum dir_enum;
  struct db_playlist_info dbpli;
  char modified[32];
  uint32_t time_modified;
  struct db_media_file_info dbmfi;
  int ret;

  // Load playlists for dir-id
  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_PL;
  qp.sort = S_PLAYLIST;
  qp.idx_type = I_NONE;
  qp.filter = sqlite3_mprintf("(f.directory_id = %d AND (f.type = %d OR f.type = %d))", directory_id, PL_PLAIN, PL_SMART);
  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }
  while (((ret = db_query_fetch_pl(&qp, &dbpli, 0)) == 0) && (dbpli.id))
    {
      if (safe_atou32(dbpli.db_timestamp, &time_modified) != 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error converting time modified to uint32_t: %s\n", dbpli.db_timestamp);
	  return -1;
	}

      if (listinfo)
	{
	  mpd_time(modified, sizeof(modified), time_modified);
	  evbuffer_add_printf(evbuf,
	    "playlist: %s\n"
	    "Last-Modified: %s\n",
	    (dbpli.virtual_path + 1),
	    modified);
	}
      else
	{
	  evbuffer_add_printf(evbuf,
	    "playlist: %s\n",
	    (dbpli.virtual_path + 1));
	}
    }
  db_query_end(&qp);
  sqlite3_free(qp.filter);

  // Load sub directories for dir-id
  memset(&dir_enum, 0, sizeof(struct directory_enum));
  dir_enum.parent_id = directory_id;
  ret = db_directory_enum_start(&dir_enum);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Failed to start directory enum for parent_id %d\n", directory_id);
      return -1;
    }
  while ((ret = db_directory_enum_fetch(&dir_enum, &subdir)) == 0 && subdir.id > 0)
    {
      if (listinfo)
	{
	  evbuffer_add_printf(evbuf,
	    "directory: %s\n"
	    "Last-Modified: %s\n",
	    (subdir.virtual_path + 1),
	    "2015-12-01 00:00");
	}
      else
	{
	  evbuffer_add_printf(evbuf,
	    "directory: %s\n",
	    (subdir.virtual_path + 1));
	}

      if (listall)
	{
	  mpd_add_directory(evbuf, subdir.id, listall, listinfo, errmsg);
	}
    }
  db_directory_enum_end(&dir_enum);

  // Load files for dir-id
  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_ITEMS;
  qp.sort = S_ARTIST;
  qp.idx_type = I_NONE;
  qp.filter = sqlite3_mprintf("(f.directory_id = %d)", directory_id);
  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }
  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      if (listinfo)
	{
	  ret = mpd_add_db_media_file_info(evbuf, &dbmfi);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_MPD, "Error adding song to the evbuffer, song id: %s\n", dbmfi.id);
	    }
	}
      else
	{
	  evbuffer_add_printf(evbuf,
	    "file: %s\n",
	    (dbmfi.virtual_path + 1));
	}
    }
  db_query_end(&qp);

  return 0;
}

static int
mpd_command_listall(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int dir_id;
  char parent[PATH_MAX];
  int ret;

  if (argc < 2 || strlen(argv[1]) == 0
      || (strncmp(argv[1], "/", 1) == 0 && strlen(argv[1]) == 1))
    {
      ret = snprintf(parent, sizeof(parent), "/");
    }
  else if (strncmp(argv[1], "/", 1) == 0)
    {
      ret = snprintf(parent, sizeof(parent), "%s/", argv[1]);
    }
  else
    {
      ret = snprintf(parent, sizeof(parent), "/%s", argv[1]);
    }

  if ((ret < 0) || (ret >= sizeof(parent)))
    {
      *errmsg = safe_asprintf("Parent path exceeds PATH_MAX");
      return ACK_ERROR_UNKNOWN;
    }

  // Load dir-id from db for parent-path
  dir_id = db_directory_id_byvirtualpath(parent);
  if (dir_id == 0)
    {
      *errmsg = safe_asprintf("Directory info not found for virtual-path '%s'", parent);
      return ACK_ERROR_NO_EXIST;
    }

  ret = mpd_add_directory(evbuf, dir_id, 1, 0, errmsg);

  return ret;
}

static int
mpd_command_listallinfo(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int dir_id;
  char parent[PATH_MAX];
  int ret;

  if (argc < 2 || strlen(argv[1]) == 0
      || (strncmp(argv[1], "/", 1) == 0 && strlen(argv[1]) == 1))
    {
      ret = snprintf(parent, sizeof(parent), "/");
    }
  else if (strncmp(argv[1], "/", 1) == 0)
    {
      ret = snprintf(parent, sizeof(parent), "%s/", argv[1]);
    }
  else
    {
      ret = snprintf(parent, sizeof(parent), "/%s", argv[1]);
    }

  if ((ret < 0) || (ret >= sizeof(parent)))
    {
      *errmsg = safe_asprintf("Parent path exceeds PATH_MAX");
      return ACK_ERROR_UNKNOWN;
    }

  // Load dir-id from db for parent-path
  dir_id = db_directory_id_byvirtualpath(parent);
  if (dir_id == 0)
    {
      *errmsg = safe_asprintf("Directory info not found for virtual-path '%s'", parent);
      return ACK_ERROR_NO_EXIST;
    }

  ret = mpd_add_directory(evbuf, dir_id, 1, 1, errmsg);

  return ret;
}

/*
 * Command handler function for 'lsinfo'
 * Lists the contents of the directory given in argv[1].
 */
static int
mpd_command_lsinfo(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int dir_id;
  char parent[PATH_MAX];
  int print_playlists;
  int ret;

  if (argc < 2 || strlen(argv[1]) == 0
      || (strncmp(argv[1], "/", 1) == 0 && strlen(argv[1]) == 1))
    {
      ret = snprintf(parent, sizeof(parent), "/");
    }
  else if (strncmp(argv[1], "/", 1) == 0)
    {
      ret = snprintf(parent, sizeof(parent), "%s/", argv[1]);
    }
  else
    {
      ret = snprintf(parent, sizeof(parent), "/%s", argv[1]);
    }

  if ((ret < 0) || (ret >= sizeof(parent)))
    {
      *errmsg = safe_asprintf("Parent path exceeds PATH_MAX");
      return ACK_ERROR_UNKNOWN;
    }

  print_playlists = 0;
  if ((strncmp(parent, "/", 1) == 0 && strlen(parent) == 1))
    {
      /*
       * Special handling necessary if the root directory '/' is given.
       * In this case additional to the directory contents the stored playlists will be returned.
       * This behavior is deprecated in the mpd protocol but clients like ncmpccp or ympd uses it.
       */
      print_playlists = 1;
    }


  // Load dir-id from db for parent-path
  dir_id = db_directory_id_byvirtualpath(parent);
  if (dir_id == 0)
    {
      *errmsg = safe_asprintf("Directory info not found for virtual-path '%s'", parent);
      return ACK_ERROR_NO_EXIST;
    }

  ret = mpd_add_directory(evbuf, dir_id, 0, 1, errmsg);

  // If the root directory was passed as argument add the stored playlists to the response
  if (ret == 0 && print_playlists)
    {
      return mpd_command_listplaylists(evbuf, argc, argv, errmsg);
    }

  return ret;
}

static int
mpd_get_query_params_search(int argc, char **argv, struct query_params *qp)
{
  char *c1;
  char *c2;
  int start_pos;
  int end_pos;
  int i;
  uint32_t num;
  int ret;

  c1 = NULL;
  c2 = NULL;

  for (i = 0; i < argc; i += 2)
    {
      if (0 == strcasecmp(argv[i], "any"))
	{
	  c1 = sqlite3_mprintf("(f.artist LIKE '%%%q%%' OR f.album LIKE '%%%q%%' OR f.title LIKE '%%%q%%')", argv[i + 1], argv[i + 1], argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "file"))
	{
	  c1 = sqlite3_mprintf("(f.virtual_path LIKE '%%%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "base"))
	{
	  c1 = sqlite3_mprintf("(f.virtual_path LIKE '/%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "modified-since"))
	{
	  DPRINTF(E_WARN, L_MPD, "Special parameter 'modified-since' is not supported by forked-daapd and will be ignored\n");
	}
      else if (0 == strcasecmp(argv[i], "window"))
	{
	  ret = mpd_pars_range_arg(argv[i + 1], &start_pos, &end_pos);
	  if (ret == 0)
	    {
	      qp->idx_type = I_SUB;
	      qp->limit = end_pos - start_pos;
	      qp->offset = start_pos;
	    }
	  else
	    {
	      DPRINTF(E_LOG, L_MPD, "Window argument doesn't convert to integer or range: '%s'\n", argv[i + 1]);
	    }
	}
      else if (0 == strcasecmp(argv[i], "artist"))
	{
	  c1 = sqlite3_mprintf("(f.artist LIKE '%%%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "albumartist"))
	{
	  c1 = sqlite3_mprintf("(f.album_artist LIKE '%%%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "album"))
	{
	  c1 = sqlite3_mprintf("(f.album LIKE '%%%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "title"))
	{
	  c1 = sqlite3_mprintf("(f.title LIKE '%%%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "genre"))
	{
	  c1 = sqlite3_mprintf("(f.genre LIKE '%%%q%%')", argv[i + 1]);
	}
      else if (0 == strcasecmp(argv[i], "disc"))
	{
	  ret = safe_atou32(argv[i + 1], &num);
	  if (ret < 0)
	    DPRINTF(E_WARN, L_MPD, "Disc parameter '%s' is not an integer and will be ignored\n", argv[i + 1]);
	  else
	    c1 = sqlite3_mprintf("(f.disc = %d)", num);
	}
      else if (0 == strcasecmp(argv[i], "track"))
	{
	  ret = safe_atou32(argv[i + 1], &num);
	  if (ret < 0)
	    DPRINTF(E_WARN, L_MPD, "Track parameter '%s' is not an integer and will be ignored\n", argv[i + 1]);
	  else
	    c1 = sqlite3_mprintf("(f.track = %d)", num);
	}
      else if (0 == strcasecmp(argv[i], "date"))
	{
	  ret = safe_atou32(argv[i + 1], &num);
	  if (ret < 0)
	    c1 = sqlite3_mprintf("(f.year = 0 OR f.year IS NULL)");
	  else
	    c1 = sqlite3_mprintf("(f.year = %d)", num);
	}
      else
	{
	  DPRINTF(E_WARN, L_MPD, "Parameter '%s' is not supported by forked-daapd and will be ignored\n", argv[i]);
	}

      if (c1)
	{
	  if (qp->filter)
	    c2 = sqlite3_mprintf("%s AND %s", qp->filter, c1);
	  else
	    c2 = sqlite3_mprintf("%s", c1);

	  if (qp->filter)
	    sqlite3_free(qp->filter);

	  qp->filter = c2;
	  c2 = NULL;
	  sqlite3_free(c1);
	  c1 = NULL;
	}
    }

  return 0;
}

/*
 * Command handler function for 'search'
 * Lists any song that matches the given list of arguments. Arguments are pairs of TYPE and WHAT, where
 * TYPE is the tag that contains WHAT (case insensitiv).
 *
 * TYPE can also be one of the special parameter:
 * - any: checks all tags
 * - file: checks the virtual_path
 * - base: restricts result to the given directory
 * - modified-since (not supported)
 * - window: limits result to the given range of "START:END"
 *
 * Example request: "search artist foo album bar"
 */
static int
mpd_command_search(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  int ret;

  if (argc < 3 || ((argc - 1) % 2) != 0)
    {
      *errmsg = safe_asprintf("Missing argument(s) for command 'search'");
      return ACK_ERROR_ARG;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_ITEMS;
  qp.sort = S_NAME;
  qp.idx_type = I_NONE;

  mpd_get_query_params_search(argc - 1, argv + 1, &qp);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      sqlite3_free(qp.filter);

      *errmsg = safe_asprintf("Could not start query");
      return ACK_ERROR_UNKNOWN;
    }

  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = mpd_add_db_media_file_info(evbuf, &dbmfi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error adding song to the evbuffer, song id: %s\n", dbmfi.id);
	}
    }

  db_query_end(&qp);
  sqlite3_free(qp.filter);

  return 0;
}

static int
mpd_command_searchadd(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  struct player_status status;
  int ret;

  if (argc < 3 || ((argc - 1) % 2) != 0)
    {
      *errmsg = safe_asprintf("Missing argument(s) for command 'search'");
      return ACK_ERROR_ARG;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_ITEMS;
  qp.sort = S_ARTIST;
  qp.idx_type = I_NONE;

  mpd_get_query_params_search(argc - 1, argv + 1, &qp);

  player_get_status(&status);

  ret = db_queue_add_by_query(&qp, status.shuffle, status.item_id);
  sqlite3_free(qp.filter);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Failed to add songs to playlist");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'update'
 * Initiates an init-rescan (scans for new files)
 */
static int
mpd_command_update(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  if (argc > 1 && strlen(argv[1]) > 0)
    {
      *errmsg = safe_asprintf("Update for specific uri not supported for command 'update'");
      return ACK_ERROR_ARG;
    }

  library_rescan();

  evbuffer_add(evbuf, "updating_db: 1\n", 15);

  return 0;
}

/*
static int
mpd_command_rescan(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int ret;

  if (argc > 1)
    {
      DPRINTF(E_LOG, L_MPD, "Rescan for specific uri not supported for command 'rescan'\n");
      *errmsg = safe_asprintf("Rescan for specific uri not supported for command 'rescan'");
      return ACK_ERROR_ARG;
    }

  filescanner_trigger_fullrescan();

  evbuffer_add(evbuf, "updating_db: 1\n", 15);

  return 0;
}
*/

/*
 * Callback function for the 'player_speaker_enumerate' function.
 * Adds a new struct output to the given struct outputs in *arg for the given speaker (id, name, etc.).
 */
static void
outputs_enum_cb(uint64_t id, const char *name, int relvol, int absvol, struct spk_flags flags, void *arg)
{
  struct outputs *outputs;
  struct output *output;

  outputs = (struct outputs *)arg;

  output = (struct output*)malloc(sizeof(struct output));

  output->id = id;
  output->shortid = (unsigned short) id;
  output->name = strdup(name);
  output->selected = flags.selected;

  output->next = outputs->outputs;
  outputs->outputs = output;
  outputs->count++;
  if (flags.selected)
    outputs->active++;

  DPRINTF(E_DBG, L_MPD, "Output enum: shortid %d, id %" PRIu64 ", name '%s', selected %d\n",
      output->shortid, output->id, output->name, output->selected);
}

/*
 * Command handler function for 'disableoutput'
 * Expects argument argv[1] to be the id of the speaker to disable.
 */
static int
mpd_command_disableoutput(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct outputs outputs;
  struct output *output;
  uint32_t num;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'disableoutput'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &num);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  outputs.count = 0;
  outputs.active = 0;
  outputs.outputs = NULL;

  player_speaker_enumerate(outputs_enum_cb, &outputs);

  nspk = outputs.active;
  output = outputs.outputs;
  while (output)
    {
      if (output->shortid == num && output->selected)
	{
	  nspk--;
	  break;
	}
      output = output->next;
    }

  if (nspk == outputs.active)
    {
      DPRINTF(E_LOG, L_MPD, "No speaker to deactivate\n");
      free_outputs(outputs.outputs);
      return 0;
    }

  ids = (uint64_t *)malloc((nspk + 1) * sizeof(uint64_t));

  ids[0] = nspk;

  i = 1;
  output = outputs.outputs;
  while (output)
    {
      if (output->shortid != num && output->selected)
      {
	ids[i] = output->id;
	i++;
      }

      output = output->next;
    }

  ret = player_speaker_set(ids);

  free(ids);
  free_outputs(outputs.outputs);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Speakers deactivation failed: %d", num);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'enableoutput'
 * Expects argument argv[1] to be the id of the speaker to enable.
 */
static int
mpd_command_enableoutput(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct outputs outputs;
  struct output *output;
  uint32_t num;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'enableoutput'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &num);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  outputs.count = 0;
  outputs.active = 0;
  outputs.outputs = NULL;

  player_speaker_enumerate(outputs_enum_cb, &outputs);

  nspk = outputs.active;
  output = outputs.outputs;
  while (output)
    {
      if (output->shortid == num && !output->selected)
	{
	  nspk++;
	  break;
	}
      output = output->next;
    }

  if (nspk == outputs.active)
    {
      DPRINTF(E_LOG, L_MPD, "No speaker to activate\n");
      free_outputs(outputs.outputs);
      return 0;
    }

  ids = (uint64_t *)malloc((nspk + 1) * sizeof(uint64_t));

  ids[0] = nspk;

  i = 1;
  output = outputs.outputs;
  while (output)
    {
      if (output->shortid == num || output->selected)
      {
	ids[i] = output->id;
	i++;
      }

      output = output->next;
    }

  ret = player_speaker_set(ids);

  if (ids)
    free(ids);
  free_outputs(outputs.outputs);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Speakers activation failed: %d", num);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'toggleoutput'
 * Expects argument argv[1] to be the id of the speaker to enable/disable.
 */
static int
mpd_command_toggleoutput(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct outputs outputs;
  struct output *output;
  uint32_t num;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  if (argc < 2)
    {
      *errmsg = safe_asprintf("Missing argument for command 'toggleoutput'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &num);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  outputs.count = 0;
  outputs.active = 0;
  outputs.outputs = NULL;

  player_speaker_enumerate(outputs_enum_cb, &outputs);

  nspk = outputs.active;
  output = outputs.outputs;
  while (output)
    {
      if (output->shortid == num && !output->selected)
	{
	  nspk++;
	  break;
	}
      else if (output->shortid == num && output->selected)
	{
	  nspk--;
	  break;
	}
      output = output->next;
    }

  if (nspk == outputs.active)
    {
      DPRINTF(E_LOG, L_MPD, "No speaker to de/activate\n");
      free_outputs(outputs.outputs);
      return 0;
    }

  ids = (uint64_t *)malloc((nspk + 1) * sizeof(uint64_t));

  ids[0] = nspk;

  i = 1;
  output = outputs.outputs;
  while (output)
    {
      if ((output->shortid == num && !output->selected)
	  || (output->shortid != num && output->selected))
      {
	ids[i] = output->id;
	i++;
      }

      output = output->next;
    }

  ret = player_speaker_set(ids);

  if (ids)
    free(ids);
  free_outputs(outputs.outputs);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Speakers de/activation failed: %d", num);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Callback function for the 'outputs' command.
 * Gets called for each available speaker and prints the speaker information to the evbuffer given in *arg.
 *
 * Example output:
 *   outputid: 0
 *   outputname: Computer
 *   outputenabled: 1
 *   outputvolume: 50
 */
static void
speaker_enum_cb(uint64_t id, const char *name, int relvol, int absvol, struct spk_flags flags, void *arg)
{
  struct evbuffer *evbuf;

  evbuf = (struct evbuffer *)arg;

  evbuffer_add_printf(evbuf,
		      "outputid: %d\n"
		      "outputname: %s\n"
		      "outputenabled: %d\n"
		      "outputvolume: %d\n",
		      (unsigned short) id,
		      name,
		      flags.selected,
		      absvol);
}

/*
 * Command handler function for 'outputs'
 * Returns a lists with the avaiable speakers.
 */
static int
mpd_command_outputs(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  player_speaker_enumerate(speaker_enum_cb, evbuf);

  return 0;
}

static int
outputvolume_set(uint32_t shortid, int volume, char **errmsg)
{
  struct outputs outputs;
  struct output *output;
  int ret;

  outputs.count = 0;
  outputs.active = 0;
  outputs.outputs = NULL;

  player_speaker_enumerate(outputs_enum_cb, &outputs);

  output = outputs.outputs;
  while (output)
    {
      if (output->shortid == shortid)
	{
	  break;
	}
      output = output->next;
    }

  if (!output)
    {
      free_outputs(outputs.outputs);
      *errmsg = safe_asprintf("No speaker found for short id: %d", shortid);
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_volume_setabs_speaker(output->id, volume);

  free_outputs(outputs.outputs);

  if (ret < 0)
    {
      *errmsg = safe_asprintf("Setting volume to %d for speaker with short-id %d failed", volume, shortid);
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

static int
mpd_command_outputvolume(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  uint32_t shortid;
  int volume;
  int ret;

  if (argc < 3)
    {
      *errmsg = safe_asprintf("Missing argument for command 'outputvolume'");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &shortid);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[1]);
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[2], &volume);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Argument doesn't convert to integer: '%s'", argv[2]);
      return ACK_ERROR_ARG;
    }

  ret = outputvolume_set(shortid, volume, errmsg);

  return ret;
}

static void
channel_outputvolume(const char *message)
{
  uint32_t shortid;
  int volume;
  char *tmp;
  char *ptr;
  char *errmsg = NULL;
  int ret;

  tmp = strdup(message);
  ptr = strrchr(tmp, ':');
  if (!ptr)
    {
      free(tmp);
      DPRINTF(E_LOG, L_MPD, "Failed to parse output id and volume from message '%s' (expected format: \"output-id:volume\"\n", message);
      return;
    }

  *ptr = '\0';

  ret = safe_atou32(tmp, &shortid);
  if (ret < 0)
    {
      free(tmp);
      DPRINTF(E_LOG, L_MPD, "Failed to parse output id from message: '%s'\n", message);
      return;
    }

  ret = safe_atoi32((ptr + 1), &volume);
  if (ret < 0)
    {
      free(tmp);
      DPRINTF(E_LOG, L_MPD, "Failed to parse volume from message: '%s'\n", message);
      return;
    }

  outputvolume_set(shortid, volume, &errmsg);
  if (errmsg)
    DPRINTF(E_LOG, L_MPD, "Failed to set output volume from message: '%s' (error='%s')\n", message, errmsg);

  free(tmp);
}

static void
channel_pairing(const char *message)
{
  remote_pairing_kickoff((char **)&message);
}

static void
channel_verification(const char *message)
{
  player_raop_verification_kickoff((char **)&message);
}

struct mpd_channel
{
  /* The channel name */
  const char *channel;

  /*
   * The function to execute the sendmessage command for a specific channel
   *
   * @param message message received on this channel
   */
  void (*handler)(const char *message);
};

static struct mpd_channel mpd_channels[] =
  {
    {
      .channel = "outputvolume",
      .handler = channel_outputvolume
    },
    {
      .channel = "pairing",
      .handler = channel_pairing
    },
    {
      .channel = "verification",
      .handler = channel_verification
    },
    {
      .channel = NULL,
      .handler = NULL
    },
  };

/*
 * Finds the channel handler for the given channel name
 *
 * @param name channel name from sendmessage command
 * @return the channel or NULL if it is an unknown/unsupported channel
 */
static struct mpd_channel *
mpd_find_channel(const char *name)
{
  int i;

  for (i = 0; mpd_channels[i].handler; i++)
    {
      if (0 == strcmp(name, mpd_channels[i].channel))
	{
	  return &mpd_channels[i];
	}
    }

  return NULL;
}

static int
mpd_command_channels(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int i;

  for (i = 0; mpd_channels[i].handler; i++)
    {
      evbuffer_add_printf(evbuf,
      		      "channel: %s\n",
      		      mpd_channels[i].channel);
    }

  return 0;
}

static int
mpd_command_sendmessage(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  const char *channelname;
  const char *message;
  struct mpd_channel *channel;

  if (argc < 3)
    {
      *errmsg = safe_asprintf("Missing argument for command 'sendmessage'");
      return ACK_ERROR_ARG;
    }

  channelname = argv[1];
  message = argv[2];

  channel = mpd_find_channel(channelname);
  if (!channel)
    {
      // Just ignore the message, only log an error message
      DPRINTF(E_LOG, L_MPD, "Unsupported channel '%s'\n", channelname);
      return 0;
    }

  channel->handler(message);
  return 0;
}

/*
 * Dummy function to handle commands that are not supported by forked-daapd and should
 * not raise an error.
 */
static int
mpd_command_ignore(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  //do nothing
  DPRINTF(E_DBG, L_MPD, "Ignore command %s\n", argv[0]);
  return 0;
}

static int
mpd_command_commands(struct evbuffer *evbuf, int argc, char **argv, char **errmsg);

/*
 * Command handler function for 'tagtypes'
 * Returns a lists with supported tags in the form:
 *   tagtype: Artist
 */
static int
mpd_command_tagtypes(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  evbuffer_add_printf(evbuf,
      "tagtype: Artist\n"
      "tagtype: AlbumArtist\n"
      "tagtype: ArtistSort\n"
      "tagtype: AlbumArtistSort\n"
      "tagtype: Album\n"
      "tagtype: Title\n"
      "tagtype: Track\n"
      "tagtype: Genre\n"
      "tagtype: Disc\n");

  return 0;
}

/*
 * Command handler function for 'decoders'
 * MPD returns the decoder plugins with their supported suffix and mime types.
 *
 * forked-daapd only uses libav/ffmepg for decoding and does not support decoder plugins,
 * therefor the function reports only ffmpeg as available.
 */
static int
mpd_command_decoders(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int i;

  evbuffer_add_printf(evbuf, "plugin: ffmpeg\n");

  for (i = 0; ffmpeg_suffixes[i]; i++)
    {
      evbuffer_add_printf(evbuf, "suffix: %s\n", ffmpeg_suffixes[i]);
    }

  for (i = 0; ffmpeg_mime_types[i]; i++)
    {
      evbuffer_add_printf(evbuf, "mime_type: %s\n", ffmpeg_mime_types[i]);
    }

  return 0;
}

struct mpd_command
{
  /* The command name */
  const char *mpdcommand;

  /*
   * The function to execute the command
   *
   * @param evbuf the response event buffer
   * @param argc number of arguments in argv
   * @param argv argument array, first entry is the commandname
   * @param errmsg error message set by this function if an error occured
   * @return 0 if successful, one of ack values if an error occured
   */
  int (*handler)(struct evbuffer *evbuf, int argc, char **argv, char **errmsg);
};

static struct mpd_command mpd_handlers[] =
  {
    /*
     * Commands for querying status
     */
    {
      .mpdcommand = "clearerror",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "currentsong",
      .handler = mpd_command_currentsong
    },
    {
      .mpdcommand = "idle",
      .handler = mpd_command_idle
    },
    {
      .mpdcommand = "noidle",
      .handler = mpd_command_noidle
    },
    {
      .mpdcommand = "status",
      .handler = mpd_command_status
    },
    {
      .mpdcommand = "stats",
      .handler = mpd_command_stats
    },

    /*
     * Playback options
     */
    {
      .mpdcommand = "consume",
      .handler = mpd_command_consume
    },
    {
      .mpdcommand = "crossfade",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "mixrampdb",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "mixrampdelay",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "random",
      .handler = mpd_command_random
    },
    {
      .mpdcommand = "repeat",
      .handler = mpd_command_repeat
    },
    {
      .mpdcommand = "setvol",
      .handler = mpd_command_setvol
    },
    {
      .mpdcommand = "single",
      .handler = mpd_command_single
    },
    {
      .mpdcommand = "replay_gain_mode",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "replay_gain_status",
      .handler = mpd_command_replay_gain_status
    },
    {
      .mpdcommand = "volume",
      .handler = mpd_command_volume
    },

    /*
     * Controlling playback
     */
    {
      .mpdcommand = "next",
      .handler = mpd_command_next
    },
    {
      .mpdcommand = "pause",
      .handler = mpd_command_pause
    },
    {
      .mpdcommand = "play",
      .handler = mpd_command_play
    },
    {
      .mpdcommand = "playid",
      .handler = mpd_command_playid
    },
    {
      .mpdcommand = "previous",
      .handler = mpd_command_previous
    },
    {
      .mpdcommand = "seek",
      .handler = mpd_command_seek
    },
    {
      .mpdcommand = "seekid",
      .handler = mpd_command_seekid
    },
    {
      .mpdcommand = "seekcur",
      .handler = mpd_command_seekcur
    },
    {
      .mpdcommand = "stop",
      .handler = mpd_command_stop
    },

    /*
     * The current playlist
     */
    {
      .mpdcommand = "add",
      .handler = mpd_command_add
    },
    {
      .mpdcommand = "addid",
      .handler = mpd_command_addid
    },
    {
      .mpdcommand = "clear",
      .handler = mpd_command_clear
    },
    {
      .mpdcommand = "delete",
      .handler = mpd_command_delete
    },
    {
      .mpdcommand = "deleteid",
      .handler = mpd_command_deleteid
    },
    {
      .mpdcommand = "move",
      .handler = mpd_command_move
    },
    {
      .mpdcommand = "moveid",
      .handler = mpd_command_moveid
    },
    // According to the mpd protocol the use of "playlist" is deprecated
    {
      .mpdcommand = "playlist",
      .handler = mpd_command_playlistinfo
    },
    /*
    {
      .mpdcommand = "playlistfind",
      .handler = mpd_command_playlistfind
    },
    */
    {
      .mpdcommand = "playlistid",
      .handler = mpd_command_playlistid
    },
    {
      .mpdcommand = "playlistinfo",
      .handler = mpd_command_playlistinfo
    },
    /*
    {
      .mpdcommand = "playlistsearch",
      .handler = mpd_command_playlistsearch
    },
    */
    {
      .mpdcommand = "plchanges",
      .handler = mpd_command_plchanges
    },
    {
      .mpdcommand = "plchangesposid",
      .handler = mpd_command_plchangesposid
    },
    /*
    {
      .mpdcommand = "prio",
      .handler = mpd_command_prio
    },
    {
      .mpdcommand = "prioid",
      .handler = mpd_command_prioid
    },
    {
      .mpdcommand = "rangeid",
      .handler = mpd_command_rangeid
    },
    {
      .mpdcommand = "shuffle",
      .handler = mpd_command_shuffle
    },
    {
      .mpdcommand = "swap",
      .handler = mpd_command_swap
    },
    {
      .mpdcommand = "swapid",
      .handler = mpd_command_swapid
    },
    {
      .mpdcommand = "addtagid",
      .handler = mpd_command_addtagid
    },
    {
      .mpdcommand = "cleartagid",
      .handler = mpd_command_cleartagid
    },
     */

    /*
     * Stored playlists
     */
    {
      .mpdcommand = "listplaylist",
      .handler = mpd_command_listplaylist
    },
    {
      .mpdcommand = "listplaylistinfo",
      .handler = mpd_command_listplaylistinfo
    },
    {
      .mpdcommand = "listplaylists",
      .handler = mpd_command_listplaylists
    },
    {
      .mpdcommand = "load",
      .handler = mpd_command_load
    },
    {
      .mpdcommand = "playlistadd",
      .handler = mpd_command_playlistadd
    },
    /*
    {
      .mpdcommand = "playlistclear",
      .handler = mpd_command_playlistclear
    },
    {
      .mpdcommand = "playlistdelete",
      .handler = mpd_command_playlistdelete
    },
    {
      .mpdcommand = "playlistmove",
      .handler = mpd_command_playlistmove
    },
    {
      .mpdcommand = "rename",
      .handler = mpd_command_rename
    },
     */
    {
      .mpdcommand = "rm",
      .handler = mpd_command_rm
    },
    {
      .mpdcommand = "save",
      .handler = mpd_command_save
    },

    /*
     * The music database
     */
    {
      .mpdcommand = "count",
      .handler = mpd_command_count
    },
    {
      .mpdcommand = "find",
      .handler = mpd_command_find
    },
    {
      .mpdcommand = "findadd",
      .handler = mpd_command_findadd
    },
    {
      .mpdcommand = "list",
      .handler = mpd_command_list
    },
    {
      .mpdcommand = "listall",
      .handler = mpd_command_listall
    },
    {
      .mpdcommand = "listallinfo",
      .handler = mpd_command_listallinfo
    },
    /*
    {
      .mpdcommand = "listfiles",
      .handler = mpd_command_listfiles
    },
    */
    {
      .mpdcommand = "lsinfo",
      .handler = mpd_command_lsinfo
    },
    /*
    {
      .mpdcommand = "readcomments",
      .handler = mpd_command_readcomments
    },
    */
    {
      .mpdcommand = "search",
      .handler = mpd_command_search
    },
    {
      .mpdcommand = "searchadd",
      .handler = mpd_command_searchadd
    },
    /*
    {
      .mpdcommand = "searchaddpl",
      .handler = mpd_command_searchaddpl
    },
    */
    {
      .mpdcommand = "update",
      .handler = mpd_command_update
    },
    /*
    {
      .mpdcommand = "rescan",
      .handler = mpd_command_rescan
    },
    */

    /*
     * Mounts and neighbors
     */
    /*
    {
      .mpdcommand = "mount",
      .handler = mpd_command_mount
    },
    {
      .mpdcommand = "unmount",
      .handler = mpd_command_unmount
    },
    {
      .mpdcommand = "listmounts",
      .handler = mpd_command_listmounts
    },
    {
      .mpdcommand = "listneighbors",
      .handler = mpd_command_listneighbors
    },
     */

    /*
     * Stickers
     */
    {
      .mpdcommand = "sticker",
      .handler = mpd_command_ignore
    },

    /*
     * Connection settings
     */
    {
      .mpdcommand = "close",
      .handler = mpd_command_ignore
    },
    /*
    {
      .mpdcommand = "kill",
      .handler = mpd_command_kill
    },
    {
      .mpdcommand = "password",
      .handler = mpd_command_password
    },
     */
    {
      .mpdcommand = "ping",
      .handler = mpd_command_ignore
    },

    /*
     * Audio output devices
     */
    {
      .mpdcommand = "disableoutput",
      .handler = mpd_command_disableoutput
    },
    {
      .mpdcommand = "enableoutput",
      .handler = mpd_command_enableoutput
    },
    {
      .mpdcommand = "toggleoutput",
      .handler = mpd_command_toggleoutput
    },
    {
      .mpdcommand = "outputs",
      .handler = mpd_command_outputs
    },

    /*
     * Reflection
     */
    /*
    {
      .mpdcommand = "config",
      .handler = mpd_command_config
    },
    */
    {
      .mpdcommand = "commands",
      .handler = mpd_command_commands
    },
    {
      .mpdcommand = "notcommands",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "tagtypes",
      .handler = mpd_command_tagtypes
    },
    {
      .mpdcommand = "urlhandlers",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "decoders",
      .handler = mpd_command_decoders
    },

    /*
     * Client to client
     */
    {
      .mpdcommand = "subscribe",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "unsubscribe",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "channels",
      .handler = mpd_command_channels
    },
    {
      .mpdcommand = "readmessages",
      .handler = mpd_command_ignore
    },
    {
      .mpdcommand = "sendmessage",
      .handler = mpd_command_sendmessage
    },

    /*
     * Forked-daapd commands (not supported by mpd)
     */
    {
      .mpdcommand = "outputvolume",
      .handler = mpd_command_outputvolume
    },

    /*
     * NULL command to terminate loop
     */
    {
      .mpdcommand = NULL,
      .handler = NULL
    }
  };

/*
 * Finds the command handler for the given command name
 *
 * @param name the name of the command
 * @return the command or NULL if it is an unknown/unsupported command
 */
static struct mpd_command*
mpd_find_command(const char *name)
{
  int i;

  for (i = 0; mpd_handlers[i].handler; i++)
    {
      if (0 == strcmp(name, mpd_handlers[i].mpdcommand))
	{
	  return &mpd_handlers[i];
	}
    }

  return NULL;
}

static int
mpd_command_commands(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int i;

  for (i = 0; mpd_handlers[i].handler; i++)
    {
      evbuffer_add_printf(evbuf,
          "command: %s\n",
	  mpd_handlers[i].mpdcommand);
    }

  return 0;
}


/*
 * The read callback function is invoked if a complete command sequence was received from the client
 * (see mpd_input_filter function).
 *
 * @param bev the buffer event
 * @param ctx (not used)
 */
static void
mpd_read_cb(struct bufferevent *bev, void *ctx)
{
  struct evbuffer *input;
  struct evbuffer *output;
  int ret;
  int ncmd;
  char *line;
  char *errmsg;
  struct mpd_command *command;
  enum command_list_type listtype;
  int idle_cmd;
  int close_cmd;
  char *argv[COMMAND_ARGV_MAX];
  int argc;

  /* Get the input evbuffer, contains the command sequence received from the client */
  input = bufferevent_get_input(bev);
  /* Get the output evbuffer, used to send the server response to the client */
  output = bufferevent_get_output(bev);

  DPRINTF(E_SPAM, L_MPD, "Received MPD command sequence\n");

  idle_cmd = 0;
  close_cmd = 0;

  listtype = COMMAND_LIST_NONE;
  ncmd = 0;
  ret = -1;

  while ((line = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY)))
    {
      DPRINTF(E_DBG, L_MPD, "MPD message: %s\n", line);

      // Split the read line into command name and arguments
      ret = mpd_parse_args(line, &argc, argv);
      if (ret != 0 || argc <= 0)
	{
	  // Error handling for argument parsing error
	  DPRINTF(E_LOG, L_MPD, "Error parsing arguments for MPD message: %s\n", line);
	  errmsg = safe_asprintf("Error parsing arguments");
	  ret = ACK_ERROR_ARG;
	  evbuffer_add_printf(output, "ACK [%d@%d] {%s} %s\n", ret, ncmd, "unkown", errmsg);
	  free(errmsg);
	  free(line);
	  break;
	}

      /*
       * Check if it is a list command
       */
      if (0 == strcmp(argv[0], "command_list_ok_begin"))
	{
	  listtype = COMMAND_LIST_OK;
	  free(line);
	  continue;
	}
      else if (0 == strcmp(argv[0], "command_list_begin"))
	{
	  listtype = COMMAND_LIST;
	  free(line);
	  continue;
	}
      else if (0 == strcmp(argv[0], "command_list_end"))
	{
	  free(line);
	  break;
	}
      else if (0 == strcmp(argv[0], "idle"))
	idle_cmd = 1;
      else if (0 == strcmp(argv[0], "noidle"))
	idle_cmd = 0;
      else if (0 == strcmp(argv[0], "close"))
	close_cmd = 1;

      /*
       * Find the command handler and execute the command function
       */
      command = mpd_find_command(argv[0]);

      if (command == NULL)
	{
	  errmsg = safe_asprintf("Unsupported command '%s'", argv[0]);
	  ret = ACK_ERROR_UNKNOWN;
	}
      else
	ret = command->handler(output, argc, argv, &errmsg);

      /*
       * If an error occurred, add the ACK line to the response buffer and exit the loop
       */
      if (ret != 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error executing command '%s': %s\n", argv[0], errmsg);
	  evbuffer_add_printf(output, "ACK [%d@%d] {%s} %s\n", ret, ncmd, argv[0], errmsg);
	  free(errmsg);
	  free(line);
	  break;
	}

      /*
       * If the command sequence started with command_list_ok_begin, add a list_ok line to the
       * response buffer after each command output.
       */
      if (listtype == COMMAND_LIST_OK)
	{
	  evbuffer_add(output, "list_OK\n", 8);
	}

      free(line);
      ncmd++;
    }

  DPRINTF(E_SPAM, L_MPD, "Finished MPD command sequence: %d\n", ret);

  /*
   * If everything was successful add OK line to signal clients end of message.
   * If an error occured the necessary ACK line should already be added to the response buffer.
   */
  if (ret == 0 && idle_cmd == 0 && close_cmd == 0)
    {
      evbuffer_add(output, "OK\n", 3);
    }
}

/*
 * Callback when an event occurs on the bufferevent
 */
static void
mpd_event_cb(struct bufferevent *bev, short events, void *ctx)
{
  struct evbuffer *evbuf;

  if (events & BEV_EVENT_ERROR)
    {
      DPRINTF(E_LOG, L_MPD, "Error from bufferevent: %s\n",
	  evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }

  if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
      evbuf = bufferevent_get_output(bev);
      mpd_remove_idle_client(evbuf);
      bufferevent_free(bev);
    }
}

/*
 * The input filter buffer callback checks if the data received from the client is a complete command sequence.
 * A command sequence has end with '\n' and if it starts with "command_list_begin\n" or "command_list_ok_begin\n"
 * the last line has to be "command_list_end\n".
 *
 * @param src evbuffer to read data from (contains the data received from the client)
 * @param dst evbuffer to write data to (this is the evbuffer for the read callback)
 * @param lim the upper bound of bytes to add to destination
 * @param state write mode
 * @param ctx (not used)
 * @return BEV_OK if a complete command sequence was received otherwise BEV_NEED_MORE
 */
static enum bufferevent_filter_result
mpd_input_filter(struct evbuffer *src, struct evbuffer *dst, ev_ssize_t lim, enum bufferevent_flush_mode state, void *ctx)
{
  struct evbuffer_ptr p;
  char *line;
  int ret;

  while ((line = evbuffer_readln(src, NULL, EVBUFFER_EOL_ANY)))
    {
      ret = evbuffer_add_printf(dst, "%s\n", line);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error adding line to buffer: '%s'\n", line);
	  free(line);
	  return BEV_ERROR;
	}
      free(line);
    }

  if (evbuffer_get_length(src) > 0)
    {
      DPRINTF(E_DBG, L_MPD, "Message incomplete, waiting for more data\n");
      return BEV_NEED_MORE;
    }

  p = evbuffer_search(dst, "command_list_begin", 18, NULL);
  if (p.pos < 0)
    {
      p = evbuffer_search(dst, "command_list_ok_begin", 21, NULL);
    }

  if (p.pos >= 0)
    {
      p = evbuffer_search(dst, "command_list_end", 16, NULL);
      if (p.pos < 0)
	{
	  DPRINTF(E_DBG, L_MPD, "Message incomplete (missing command_list_end), waiting for more data\n");
	  return BEV_NEED_MORE;
	}
    }

  return BEV_OK;
}

/*
 * The connection listener callback function is invoked when a new connection was received.
 *
 * @param listener the connection listener that received the connection
 * @param sock the new socket
 * @param address the address from which the connection was received
 * @param socklen the length of that address
 * @param ctx (not used)
 */
static void
mpd_accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t sock, struct sockaddr *address, int socklen,
    void *ctx)
{
  /*
   * For each new connection setup a new buffer event and wrap it around a filter event.
   * The filter event ensures, that the read callback on the buffer event is only invoked if a complete
   * command sequence from the client was received.
   */
  struct event_base *base = evconnlistener_get_base(listener);
  struct bufferevent *bev = bufferevent_socket_new(base, sock, BEV_OPT_CLOSE_ON_FREE);

  bev = bufferevent_filter_new(bev, mpd_input_filter, NULL, BEV_OPT_CLOSE_ON_FREE, NULL, NULL);
  bufferevent_setcb(bev, mpd_read_cb, NULL, mpd_event_cb, bev);
  bufferevent_enable(bev, EV_READ | EV_WRITE);

  /*
   * According to the mpd protocol send "OK MPD <version>\n" to the client, where version is the version
   * of the supported mpd protocol and not the server version.
   */
  evbuffer_add(bufferevent_get_output(bev), "OK MPD 0.18.0\n", 14);
}

/*
 * Error callback that gets called whenever an accept() call fails on the listener
 * @param listener the connection listener that received the connection
 * @param ctx (not used)
 */
static void
mpd_accept_error_cb(struct evconnlistener *listener, void *ctx)
{
  int err;

  err = EVUTIL_SOCKET_ERROR();
  DPRINTF(E_LOG, L_MPD, "Error occured %d (%s) on the listener.\n", err, evutil_socket_error_to_string(err));
}

static int
mpd_notify_idle_client(struct idle_client *client, enum listener_event_type type)
{
  if (!(client->events & type))
    {
      DPRINTF(E_DBG, L_MPD, "Client not listening for event: %d\n", type);
      return 1;
    }

  switch (type)
    {
      case LISTENER_PLAYER:
	evbuffer_add(client->evbuffer, "changed: player\n", 16);
	break;

      case LISTENER_QUEUE:
	evbuffer_add(client->evbuffer, "changed: playlist\n", 18);
	break;

      case LISTENER_VOLUME:
	evbuffer_add(client->evbuffer, "changed: mixer\n", 15);
	break;

      case LISTENER_SPEAKER:
	evbuffer_add(client->evbuffer, "changed: output\n", 16);
	break;

      case LISTENER_OPTIONS:
	evbuffer_add(client->evbuffer, "changed: options\n", 17);
	break;

      case LISTENER_STORED_PLAYLIST:
	evbuffer_add(client->evbuffer, "changed: stored_playlist\n", 25);
	break;

      default:
	DPRINTF(E_WARN, L_MPD, "Unsupported event type (%d) in notify idle clients.\n", type);
	return -1;
    }

  evbuffer_add(client->evbuffer, "OK\n", 3);

  return 0;
}

static enum command_state
mpd_notify_idle(void *arg, int *retval)
{
  enum listener_event_type type;
  struct idle_client *client;
  struct idle_client *prev;
  struct idle_client *next;
  int i;
  int ret;

  type = *(enum listener_event_type *)arg;
  DPRINTF(E_DBG, L_MPD, "Notify clients waiting for idle results: %d\n", type);

  prev = NULL;
  next = NULL;
  i = 0;
  client = idle_clients;
  while (client)
    {
      DPRINTF(E_DBG, L_MPD, "Notify client #%d\n", i);

      next = client->next;

      ret = mpd_notify_idle_client(client, type);

      if (ret == 0)
	{
	  if (prev)
	    prev->next = next;
	  else
	    idle_clients = next;

	  free(client);
	}
      else
	{
	  prev = client;
	}

      client = next;
      i++;
    }

  *retval = 0;
  return COMMAND_END;
}

static void
mpd_listener_cb(enum listener_event_type type)
{
  enum listener_event_type *ptr;

  ptr = (enum listener_event_type *)malloc(sizeof(enum listener_event_type));
  *ptr = type;

  DPRINTF(E_DBG, L_MPD, "Listener callback called with event type %d.\n", type);
  commands_exec_async(cmdbase, mpd_notify_idle, ptr);
}

/*
 * Callback function that handles http requests for artwork files
 *
 * Some MPD clients allow retrieval of local artwork by making http request for artwork
 * files.
 *
 * A request for the artwork of an item with virtual path "file:/path/to/example.mp3" looks
 * like:
 * GET http://<host>:<port>/path/to/cover.jpg
 *
 * Artwork is found by taking the uri and removing everything after the last '/'. The first
 * item in the library with a virtual path that matches *path/to* is used to read the artwork
 * file through the default forked-daapd artwork logic.
 */
static void
artwork_cb(struct evhttp_request *req, void *arg)
{
  struct evbuffer *evbuffer;
  struct evhttp_uri *decoded;
  const char *uri;
  const char *path;
  char *decoded_path;
  char *last_slash;
  int itemid;
  int format;

  if (evhttp_request_get_command(req) != EVHTTP_REQ_GET)
    {
      DPRINTF(E_LOG, L_MPD, "Unsupported request type for artwork\n");
      httpd_send_error(req, HTTP_BADMETHOD, "Method not allowed");
      return;
    }

  uri = evhttp_request_get_uri(req);
  DPRINTF(E_DBG, L_MPD, "Got artwork request with uri '%s'\n", uri);

  decoded = evhttp_uri_parse(uri);
  if (!decoded)
    {
      DPRINTF(E_LOG, L_MPD, "Bad artwork request with uri '%s'\n", uri);
      httpd_send_error(req, HTTP_BADREQUEST, 0);
      return;
    }

  path = evhttp_uri_get_path(decoded);
  if (!path)
    {
      DPRINTF(E_LOG, L_MPD, "Invalid path from artwork request with uri '%s'\n", uri);
      httpd_send_error(req, HTTP_BADREQUEST, 0);
      evhttp_uri_free(decoded);
      return;
    }

  decoded_path = evhttp_uridecode(path, 0, NULL);
  if (!decoded_path)
    {
      DPRINTF(E_LOG, L_MPD, "Error decoding path from artwork request with uri '%s'\n", uri);
      httpd_send_error(req, HTTP_BADREQUEST, 0);
      evhttp_uri_free(decoded);
      return;
    }

  last_slash = strrchr(decoded_path, '/');
  if (last_slash)
    *last_slash = '\0';

  DPRINTF(E_DBG, L_MPD, "Artwork request for path: %s\n", decoded_path);

  itemid = db_file_id_by_virtualpath_match(decoded_path);
  if (!itemid)
    {
      DPRINTF(E_WARN, L_MPD, "No item found for path '%s' from request uri '%s'\n", decoded_path, uri);
      httpd_send_error(req, HTTP_NOTFOUND, "Document was not found");
      evhttp_uri_free(decoded);
      free(decoded_path);
      return;
    }

  evbuffer = evbuffer_new();
  if (!evbuffer)
    {
      DPRINTF(E_LOG, L_MPD, "Could not allocate an evbuffer for artwork request\n");
      httpd_send_error(req, HTTP_INTERNAL, "Document was not found");
      evhttp_uri_free(decoded);
      free(decoded_path);
      return;
    }

  format = artwork_get_item(evbuffer, itemid, 600, 600);
  if (format < 0)
    {
      httpd_send_error(req, HTTP_NOTFOUND, "Document was not found");
    }
  else
    {
      switch (format)
	{
	  case ART_FMT_PNG:
	    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "image/png");
	    break;

	  default:
	    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "image/jpeg");
	    break;
	}

      httpd_send_reply(req, HTTP_OK, "OK", evbuffer, HTTPD_SEND_NO_GZIP);
    }

  evbuffer_free(evbuffer);
  evhttp_uri_free(decoded);
  free(decoded_path);
}

/* Thread: main */
int mpd_init(void)
{
  struct sockaddr *saddr;
  size_t saddr_length;
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
  unsigned short port;
  unsigned short http_port;
  int v6enabled;
  const char *pl_dir;
  int ret;


  port = cfg_getint(cfg_getsec(cfg, "mpd"), "port");
  if (port <= 0)
    {
      DPRINTF(E_INFO, L_MPD, "MPD not enabled\n");
      return 0;
    }

  v6enabled = cfg_getbool(cfg_getsec(cfg, "general"), "ipv6");

  evbase_mpd = event_base_new();
  if (!evbase_mpd)
    {
      DPRINTF(E_LOG, L_MPD, "Could not create an event base\n");
      goto evbase_fail;
    }

  cmdbase = commands_base_new(evbase_mpd, NULL);

  if (v6enabled)
    {
      saddr_length = sizeof(sin6);
      memset(&sin6, 0, saddr_length);
      sin6.sin6_family = AF_INET6;
      sin6.sin6_port = htons(port);
      saddr = (struct sockaddr *)&sin6;

      mpd_listener6 = evconnlistener_new_bind(
	  evbase_mpd,
	  mpd_accept_conn_cb,
	  NULL,
	  LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
	  -1,
	  saddr,
	  saddr_length);

      if (!mpd_listener6)
	{
	  DPRINTF(E_LOG, L_MPD, "Could not bind to port %d, falling back to IPv4\n", port);
	  v6enabled = 0;
	}
      else
	evconnlistener_set_error_cb(mpd_listener6, mpd_accept_error_cb);
    }

  saddr_length = sizeof(struct sockaddr_in);
  memset(&sin, 0, saddr_length);
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(0);
  sin.sin_port = htons(port);
  saddr = (struct sockaddr *)&sin;

  mpd_listener = evconnlistener_new_bind(
      evbase_mpd,
      mpd_accept_conn_cb,
      NULL,
      LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
      -1,
      saddr,
      saddr_length);

  if (!mpd_listener)
    {
      if (!v6enabled)
	{
	  DPRINTF(E_LOG, L_MPD, "Could not create connection listener for mpd clients on port %d\n", port);
	  goto connew_fail;
	}

#ifndef __linux__
      // Linux will listen on both ipv6 and ipv4, but FreeBSD won't
      DPRINTF(E_LOG, L_MPD, "Could not bind to port %d with IPv4, listening on IPv6 only\n", port);
#endif
    }
  else
    evconnlistener_set_error_cb(mpd_listener, mpd_accept_error_cb);

  http_port = cfg_getint(cfg_getsec(cfg, "mpd"), "http_port");
  if (http_port > 0)
    {
      evhttpd = evhttp_new(evbase_mpd);
      if (!evhttpd)
	{
	  DPRINTF(E_LOG, L_MPD, "Could not create HTTP artwork server\n");

	  goto evhttp_fail;
	}

      evhttp_set_gencb(evhttpd, artwork_cb, NULL);

      if (v6enabled)
	{
	  ret = evhttp_bind_socket(evhttpd, "::", http_port);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_MPD, "Could not bind HTTP artwork server to port %d with IPv6, falling back to IPv4\n", http_port);
	      v6enabled = 0;
	    }
	}

      ret = evhttp_bind_socket(evhttpd, "0.0.0.0", http_port);
      if (ret < 0)
	{
	  if (!v6enabled)
	    {
	      DPRINTF(E_LOG, L_MPD, "Could not bind HTTP artwork server to port %d with IPv4\n", http_port);
	      goto bind_fail;
	    }

#ifndef __linux__
	  // Linux will listen on both ipv6 and ipv4, but FreeBSD won't
	  DPRINTF(E_LOG, L_MPD, "Could not bind HTTP artwork server to port %d with IPv4, listening on IPv6 only\n", http_port);
#endif
	}
    }

  allow_modifying_stored_playlists = cfg_getbool(cfg_getsec(cfg, "mpd"), "allow_modifying_stored_playlists");
  pl_dir = cfg_getstr(cfg_getsec(cfg, "mpd"), "default_playlist_directory");
  if (pl_dir)
    default_pl_dir = safe_asprintf("/file:%s", pl_dir);

  DPRINTF(E_INFO, L_MPD, "mpd thread init\n");

  ret = pthread_create(&tid_mpd, NULL, mpd, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not spawn cache thread: %s\n", strerror(errno));

      goto thread_fail;
    }

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_mpd, "mpd");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_mpd, "mpd");
#endif

  idle_clients = NULL;
  listener_add(mpd_listener_cb, LISTENER_PLAYER | LISTENER_QUEUE | LISTENER_VOLUME | LISTENER_SPEAKER | LISTENER_OPTIONS);

  return 0;


 thread_fail:
 bind_fail:
  if (http_port > 0)
    evhttp_free(evhttpd);
 evhttp_fail:
  evconnlistener_free(mpd_listener);
  evconnlistener_free(mpd_listener6);
 connew_fail:
  commands_base_free(cmdbase);
  event_base_free(evbase_mpd);
  evbase_mpd = NULL;

 evbase_fail:
  return -1;
}

/* Thread: main */
void mpd_deinit(void)
{
  struct idle_client *temp;
  unsigned short port;
  unsigned short http_port;
  int ret;

  port = cfg_getint(cfg_getsec(cfg, "mpd"), "port");
  if (port <= 0)
    {
      DPRINTF(E_INFO, L_MPD, "MPD not enabled\n");
      return;
    }

  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_mpd, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MPD, "Could not join cache thread: %s\n", strerror(errno));
      return;
    }

  listener_remove(mpd_listener_cb);

  while (idle_clients)
    {
      temp = idle_clients;
      idle_clients = idle_clients->next;
      free(temp);
    }

  http_port = cfg_getint(cfg_getsec(cfg, "mpd"), "http_port");
  if (http_port > 0)
    evhttp_free(evhttpd);

  evconnlistener_free(mpd_listener);
  evconnlistener_free(mpd_listener6);

  // Free event base (should free events too)
  event_base_free(evbase_mpd);

  free(default_pl_dir);
}
