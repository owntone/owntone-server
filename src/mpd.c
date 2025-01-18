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

/*
 * To test with raw command input use:
 *  echo "command" | nc -q0 localhost 6600
 * Or interactive with:
 *  socat - TCP:localhost:6600
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <ctype.h> // isdigit

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/listener.h>

#include "artwork.h"
#include "commands.h"
#include "conffile.h"
#include "db.h"
#include "library.h"
#include "listener.h"
#include "logger.h"
#include "misc.h"
#include "player.h"
#include "remote_pairing.h"
#include "parsers/mpd_parser.h"

// TODO
// optimize queries (map albumartist/album groupings to songalbumid/artistid in db.c)
// support for group in count, e.g. count group artist
// empty group value is a negation

// Command handlers should use this for returning errors to make sure both
// ack_error and errmsg are correctly set
#define RETURN_ERROR(r, ...) \
  do { out->ack_error = (r); free(out->errmsg); out->errmsg = safe_asprintf(__VA_ARGS__); return -1; } while(0)

// According to the mpd protocol send "OK MPD <version>\n" to the client, where
// version is the version of the supported mpd protocol and not the server version
//   0.22.4 binarylimit
//   0.23   getvol, addid relative position "+3", searchadd position param
// TODO:
//   0.23.1 load plname 0: +3
//   0.23.3 add position param, playlistadd position param, playlistdelete plname songpos
//   0.23.4 searchaddpl position param
//   0.23.5 searchadd relative position
//   0.24   case sensitive operands, added tag in response, oneshot status,
//          consume oneshot, listplaylist/listplaylistinfo range, playlistmove
//          range, save mode, sticker find sort uri/value/value_int
#define MPD_PROTOCOL_VERSION_OK "OK MPD 0.23.0\n"

/**
 * from MPD source:
 * *
 * * The most we ever use is for search/find, and that limits it to the
 * * number of tags we can have.  Add one for the command, and one extra
 * * to catch errors clients may send us
 * *
 *  static constexpr std::size_t COMMAND_ARGV_MAX = 2 + TAG_NUM_OF_ITEM_TYPES * 2;
 *
 * https://github.com/MusicPlayerDaemon/MPD/blob/master/src/command/AllCommands.cxx
 */
#define MPD_COMMAND_ARGV_MAX 70

/**
 * config:
 * max_command_list_size KBYTES
 *
 * https://github.com/MusicPlayerDaemon/MPD/blob/master/src/client/Config.cxx
 */
#define MPD_MAX_COMMAND_LIST_SIZE (2048*1024)

#define MPD_ALL_IDLE_LISTENER_EVENTS (LISTENER_PLAYER | LISTENER_QUEUE | LISTENER_VOLUME | LISTENER_SPEAKER | LISTENER_OPTIONS | LISTENER_DATABASE | LISTENER_UPDATE | LISTENER_STORED_PLAYLIST | LISTENER_RATING)
#define MPD_RATING_FACTOR 10.0
#define MPD_BINARY_SIZE 8192  /* MPD MAX_BINARY_SIZE */
#define MPD_BINARY_SIZE_MIN 64  /* min size from MPD ClientCommands.cxx */

// MPD error codes (taken from ack.h)
enum mpd_ack_error
{
  ACK_ERROR_NONE            = 0,
  ACK_ERROR_NOT_LIST        = 1,
  ACK_ERROR_ARG             = 2,
  ACK_ERROR_PASSWORD        = 3,
  ACK_ERROR_PERMISSION      = 4,
  ACK_ERROR_UNKNOWN         = 5,

  ACK_ERROR_NO_EXIST        = 50,
  ACK_ERROR_PLAYLIST_MAX    = 51,
  ACK_ERROR_SYSTEM          = 52,
  ACK_ERROR_PLAYLIST_LOAD   = 53,
  ACK_ERROR_UPDATE_ALREADY  = 54,
  ACK_ERROR_PLAYER_SYNC     = 55,
  ACK_ERROR_EXIST           = 56,
};

enum command_list_type
{
  COMMAND_LIST_NONE = 0,
  COMMAND_LIST_BEGIN,
  COMMAND_LIST_OK_BEGIN,
  COMMAND_LIST_END,
  COMMAND_LIST_OK_END,
};

struct mpd_client_ctx
{
  // True if the connection is already authenticated or does not need authentication
  bool authenticated;

  // The events the client needs to be notified of
  short events;

  // True if the client is waiting for idle events
  bool is_idle;

  // The events the client is waiting for (set by the idle command)
  short idle_events;

  // The current binary limit size
  unsigned int binarylimit;

  // The output buffer for the client (used to send data to the client)
  struct evbuffer *evbuffer;

  // Equals COMMAND_LIST_NONE unless command_list_begin or command_list_ok_begin
  // received.
  enum command_list_type cmd_list_type;

  // Current command list
  // When cmd_list_type is either COMMAND_LIST_BEGIN or COMMAND_LIST_OK_BEGIN
  // received commands are added to this buffer. When command_list_end is
  // received, the commands saved in this buffer are processed.
  struct evbuffer *cmd_list_buffer;

  // Set to true by handlers and command processing if we must cut the client
  // connection.
  bool must_disconnect;

  struct mpd_client_ctx *next;
};

#define MPD_WANTS_NUM_ARGV_MIN 1
#define MPD_WANTS_NUM_ARGV_MAX 3
enum command_wants_num
{
  MPD_WANTS_NUM_NONE = 0,
  MPD_WANTS_NUM_ARG1_IVAL = (1 << (0 + MPD_WANTS_NUM_ARGV_MIN)),
  MPD_WANTS_NUM_ARG2_IVAL = (1 << (1 + MPD_WANTS_NUM_ARGV_MIN)),
  MPD_WANTS_NUM_ARG3_IVAL = (1 << (2 + MPD_WANTS_NUM_ARGV_MIN)),
  MPD_WANTS_NUM_ARG1_UVAL = (1 << (0 + MPD_WANTS_NUM_ARGV_MIN + MPD_WANTS_NUM_ARGV_MAX)),
  MPD_WANTS_NUM_ARG2_UVAL = (1 << (1 + MPD_WANTS_NUM_ARGV_MIN + MPD_WANTS_NUM_ARGV_MAX)),
  MPD_WANTS_NUM_ARG3_UVAL = (1 << (2 + MPD_WANTS_NUM_ARGV_MIN + MPD_WANTS_NUM_ARGV_MAX)),
};

struct mpd_command_input
{
  // Raw argument line
  const char *args_raw;

  // Argument line unescaped and split
  char *args_split;
  char *argv[MPD_COMMAND_ARGV_MAX];
  int argc;

  int has_num;
  int32_t argv_i32val[MPD_COMMAND_ARGV_MAX];
  uint32_t argv_u32val[MPD_COMMAND_ARGV_MAX];
};

struct mpd_command_output
{
  struct evbuffer *evbuf;
  char *errmsg;
  enum mpd_ack_error ack_error;
};

struct mpd_command
{
  const char *name;
  int (*handler)(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx);
  int min_argc;
  int wants_num;
};

struct param_output
{
  struct evbuffer *evbuf;
  uint32_t last_shortid;
};

/* ---------------------------------- Globals ------------------------------- */

static pthread_t tid_mpd;
static struct event_base *evbase_mpd;
static struct commands_base *cmdbase;
static struct evhttp *evhttpd;
static struct evconnlistener *mpd_listener;
static int mpd_sockfd;
static bool mpd_plugin_httpd;
static int mpd_plugin_httpd_shortid = -1;

// Virtual path to the default playlist directory
static char *default_pl_dir;
static bool allow_modifying_stored_playlists;

// Forward
static struct mpd_command mpd_handlers[];

// List of all connected mpd clients
struct mpd_client_ctx *mpd_clients;

/**
 * This lists for ffmpeg suffixes and mime types are taken from the ffmpeg decoder plugin from mpd
 * (FfmpegDecoderPlugin.cxx, git revision 9fb351a139a56fc7b1ece549894f8fc31fa887cd).
 *
 * The server does not support different decoders and always uses ffmpeg or libav for decoding.
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


/* -------------------------------- Helpers --------------------------------- */

/*
 * Some MPD clients crash if the tag value includes the newline character.
 * While they should normally not be included in most ID3 tags, they sometimes
 * are, so we just change them to space. See #1613 for more details.
 */
static char *
sanitize(char *strval)
{
  char *ptr = strval;

  if (!strval)
    return "";

  while (*ptr != '\0')
    {
      if (*ptr == '\n')
	*ptr = ' ';

      ptr++;
    }

  return strval;
}

static void
client_ctx_free(struct mpd_client_ctx *client_ctx)
{
  if (!client_ctx)
    return;

  evbuffer_free(client_ctx->cmd_list_buffer);
  free(client_ctx);
}

static struct mpd_client_ctx *
client_ctx_new(void)
{
  struct mpd_client_ctx *client_ctx;

  CHECK_NULL(L_MPD, client_ctx = calloc(1, sizeof(struct mpd_client_ctx)));
  CHECK_NULL(L_MPD, client_ctx->cmd_list_buffer = evbuffer_new());

  return client_ctx;
}

static void
client_ctx_remove(void *arg)
{
  struct mpd_client_ctx *client_ctx = arg;
  struct mpd_client_ctx *client;
  struct mpd_client_ctx *prev;

  client = mpd_clients;
  prev = NULL;

  while (client)
    {
      if (client == client_ctx)
	{
	  DPRINTF(E_DBG, L_MPD, "Removing mpd client\n");

	  if (prev)
	    prev->next = client->next;
	  else
	    mpd_clients = client->next;

	  break;
	}

      prev = client;
      client = client->next;
    }

  client_ctx_free(client_ctx);
}

static struct mpd_client_ctx *
client_ctx_add(void)
{
  struct mpd_client_ctx *client_ctx = client_ctx_new();

  client_ctx->next = mpd_clients;
  mpd_clients = client_ctx;

  return client_ctx;
}

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
 * Splits a range argument of the form START:END (the END item is not included in the range)
 * into its start and end position.
 *
 * @param start_pos set by this method to the start position
 * @param end_pos set by this method to the end postion
 * @param range the range argument
 * @return 0 on success, -1 on failure
 */
static int
range_pos_from_arg(int *start_pos, int *end_pos, const char *range)
{
  int ret;

  if (strchr(range, ':'))
    {
      ret = sscanf(range, "%d:%d", start_pos, end_pos);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error splitting range argument '%s' (return code = %d)\n", range, ret);
	  return -1;
	}
    }
  else
    {
      ret = safe_atoi32(range, start_pos);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error spitting integer argument '%s' (return code = %d)\n", range, ret);
	  return -1;
	}

      *end_pos = (*start_pos) + 1;
    }

  return 0;
}

/*
 * Converts a TO arg, which is either an absolute position "X", or a position
 * relative to currently playing song "+X"/"-X", into absolute queue pos.
 *
 * @param to_pos set by this method to absolute queue pos
 * @param to_arg string with an integer (abs) or prefixed with +/- (rel)
 * @return 0 on success, -1 on failure
 */
static int
to_pos_from_arg(int *to_pos, const char *to_arg)
{
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  *to_pos = -1;

  if (!to_arg)
    return 0; // If no to_arg, assume end of queue (to_pos = -1)

  ret = safe_atoi32(to_arg, to_pos); // +12 will become 12, -12 becomes -12
  if (ret < 0)
    return -1;

  if (to_arg[0] != '+' && to_arg[0] != '-')
    return 0;

  ret = player_get_status(&status);
  if (ret < 0)
    return -1;

  queue_item = (status.status == PLAY_STOPPED) ? db_queue_fetch_bypos(0, status.shuffle) : db_queue_fetch_byitemid(status.item_id);
  if (!queue_item)
    return -1;

  *to_pos += queue_item->pos;
  free_queue_item(queue_item, 0);
  return 0;
}

/*
 * Returns the next unquoted string argument from the input string
 */
static char*
mpd_next_unquoted(char **input)
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
mpd_next_quoted(char **input)
{
  char *arg;
  char *src;
  char *dst;
  char ch;

  // skip double quote character
  (*input)++;

  src = dst = arg = *input;
  while ((ch = *src) != '"')
    {
      // A backslash character escapes the following character and should be removed
      if (ch == '\\')
	{
	  ch = *(++src);
	}
      *dst++ = ch;

      if (ch == 0)
	{
	  // Error handling for missing double quote at end of parameter
	  DPRINTF(E_LOG, L_MPD, "Error missing closing double quote in argument\n");
	  *input = src;
	  return NULL;
	}

      ++src;
    }

  *dst = '\0';
  *input = ++src;

  return arg;
}

// Splits the argument string into an array of strings. Arguments are separated
// by a whitespace character and may be wrapped in double quotes.
static int
mpd_split_args(char **argv, int argv_size, int *argc, char **split, const char *line)
{
  char *ptr;
  int arg_count = 0;

  *split = safe_strdup(line);
  ptr = *split;

  while (*ptr != 0 && arg_count < argv_size)
    {
      // Ignore whitespace characters
      if (*ptr == ' ')
	{
	  ptr++;
	  continue;
	}

      // Check if the parameter is wrapped in double quotes
      if (*ptr == '"')
	argv[arg_count] = mpd_next_quoted(&ptr);
      else
	argv[arg_count] = mpd_next_unquoted(&ptr);

      if (!argv[arg_count])
	goto error;

      arg_count++;
    }

  *argc = arg_count;

  // No args or too many args
  if (arg_count == 0 || (*ptr != 0 && arg_count == argv_size))
    goto error;

  return 0;

 error:
  free(*split);
  *split = NULL;
  return -1;
}

/*
 * Adds the information (path, id, tags, etc.) for the given song to the given buffer
 * with additional information for the position of this song in the player queue.
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
      sanitize(queue_item->artist),
      sanitize(queue_item->album_artist),
      sanitize(queue_item->artist_sort),
      sanitize(queue_item->album_artist_sort),
      sanitize(queue_item->album),
      sanitize(queue_item->title),
      queue_item->track,
      queue_item->year,
      sanitize(queue_item->genre),
      queue_item->disc,
      queue_item->pos,
      queue_item->id);

  return ret;
}

/*
 * Adds the information (path, id, tags, etc.) for the given song to the given buffer.
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
      "duration: %.3f\n"
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
      ((float) songlength / 1000),
      sanitize(dbmfi->artist),
      sanitize(dbmfi->album_artist),
      sanitize(dbmfi->artist_sort),
      sanitize(dbmfi->album_artist_sort),
      sanitize(dbmfi->album),
      sanitize(dbmfi->title),
      dbmfi->track,
      dbmfi->year,
      sanitize(dbmfi->genre),
      dbmfi->disc);

  return ret;
}

static bool
is_filter_end(const char *arg)
{
  return (strcasecmp(arg, "sort") == 0 || strcasecmp(arg, "group") == 0 || strcasecmp(arg, "window") == 0);
}

// The bison/flex parser only works with the filter format used after version
// 0.20 e.g. "(TAG == 'value') instead of TAG VALUE.
static int
args_reassemble(char *args, size_t args_size, int argc, char **argv)
{
  const char *op;
  int filter_start;
  int filter_end;
  int i;

  // "list" has the filter as second arg, the rest have it as first
  filter_start = (strcmp(argv[0], "list") == 0) ? 2 : 1;
  op = strstr(argv[0], "search") ? " contains " : " == ";

  for (i = filter_start; i < argc && !is_filter_end(argv[i]); i++)
    ;

  filter_end = i;

  snprintf(args, args_size, "%s", argv[0]);

  for (i = 1; i < filter_start; i++)
    safe_snprintf_cat(args, args_size, " %s", argv[i]);

  for (i = filter_start; i < filter_end; i++)
    {
      if (*argv[i] == '(')
	{
	  safe_snprintf_cat(args, args_size, " %s", argv[i]);
	}
      else if (i + 1 < filter_end) // Legacy filter format (0.20 and before), we will convert
	{
	  safe_snprintf_cat(args, args_size, " %s%s%s\"%s\"%s", i == filter_start ? "((" : "AND (", argv[i], op, argv[i + 1], i + 2 == filter_end ? "))" : ")");
	  i++;
	}
      else if (filter_end == filter_start + 1) // Special case: a legacy single token is allowed if listing albums for an artist
	{
	  safe_snprintf_cat(args, args_size, " (AlbumArtist%s\"%s\")", op, argv[i]);
        }
    }

  for (i = filter_end; i < argc; i++)
    safe_snprintf_cat(args, args_size, " %s", argv[i]);

  // Return an error if the buffer was filled and thus probably truncated 
  return (strlen(args) + 1 < args_size) ? 0 : -1;
}

/*
 * Invokes a lexer/parser to read a supported command
 *
 * @param qp Must be initialized by caller (zeroed or set to default values)
 * @param pos Allocated string with TO position parameter, e.g. "+5" (or NULL)
 * @param type Allocated string with TYPE parameter, e.g. "albums" (or NULL)
 * @param in The command input
 */
static int
parse_command(struct query_params *qp, char **pos, char **tagtype, struct mpd_command_input *in)
{
  struct mpd_result result;
  char args_reassembled[8192];
  int ret;

  if (pos)
    *pos = NULL;
  if (tagtype)
    *tagtype = NULL;

  if (in->argc < 2)
    return 0; // Nothing to parse

  ret = args_reassemble(args_reassembled, sizeof(args_reassembled), in->argc, in->argv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not prepare '%s' for parsing\n", in->args_raw);
      return -1;
    }

  DPRINTF(E_DBG, L_MPD, "Parse mpd query input '%s'\n", args_reassembled);

  ret = mpd_lex_parse(&result, args_reassembled);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not parse '%s': %s\n", args_reassembled, result.errmsg);
      return -1;
    }

  qp->filter = safe_strdup(result.where);
  qp->order = safe_strdup(result.order);
  qp->group = safe_strdup(result.group);

  qp->limit = result.limit;
  qp->offset = result.offset;
  qp->idx_type = (qp->limit || qp->offset) ? I_SUB : I_NONE;

  if (pos)
    *pos = safe_strdup(result.position);

  if (tagtype)
    *tagtype = safe_strdup(result.tagtype);

  return 0;
}

static int
notify_idle_client(struct mpd_client_ctx *client_ctx, short events, bool add_ok)
{
  if (!client_ctx->is_idle)
    {
      client_ctx->events |= events;
      return 1;
    }

  if (!(client_ctx->idle_events & events))
    {
      DPRINTF(E_DBG, L_MPD, "Client not listening for events: %d\n", events);
      return 1;
    }

  if (events & LISTENER_DATABASE)
    evbuffer_add(client_ctx->evbuffer, "changed: database\n", 18);
  if (events & LISTENER_UPDATE)
    evbuffer_add(client_ctx->evbuffer, "changed: update\n", 16);
  if (events & LISTENER_QUEUE)
    evbuffer_add(client_ctx->evbuffer, "changed: playlist\n", 18);
  if (events & LISTENER_PLAYER)
    evbuffer_add(client_ctx->evbuffer, "changed: player\n", 16);
  if (events & LISTENER_VOLUME)
    evbuffer_add(client_ctx->evbuffer, "changed: mixer\n", 15);
  if (events & LISTENER_SPEAKER)
    evbuffer_add(client_ctx->evbuffer, "changed: output\n", 16);
  if (events & LISTENER_OPTIONS)
    evbuffer_add(client_ctx->evbuffer, "changed: options\n", 17);
  if (events & LISTENER_STORED_PLAYLIST)
    evbuffer_add(client_ctx->evbuffer, "changed: stored_playlist\n", 25);
  if (events & LISTENER_RATING)
    evbuffer_add(client_ctx->evbuffer, "changed: sticker\n", 17);

  if (add_ok)
    evbuffer_add(client_ctx->evbuffer, "OK\n", 3);

  client_ctx->is_idle = false;
  client_ctx->idle_events = 0;
  client_ctx->events = 0;

  return 0;
}


/* ----------------------------- Command handlers --------------------------- */

static int
mpd_command_currentsong(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  player_get_status(&status);

  if (status.status == PLAY_STOPPED)
    queue_item = db_queue_fetch_bypos(0, status.shuffle);
  else
    queue_item = db_queue_fetch_byitemid(status.item_id);

  if (!queue_item)
    return 0;

  ret = mpd_add_db_queue_item(out->evbuf, queue_item);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Error setting media info for file with id: %d", status.id); 

  return 0;
}

/*
 * Example input:
 * idle "database" "mixer" "options" "output" "player" "playlist" "sticker" "update"
 */
static int
mpd_command_idle(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *key;
  int i;

  ctx->idle_events = 0;
  ctx->is_idle = true;

  if (in->argc > 1)
    {
      for (i = 1; i < in->argc; i++)
	{
	  key = in->argv[i];

	  if (0 == strcmp(key, "database"))
	    ctx->idle_events |= LISTENER_DATABASE;
	  else if (0 == strcmp(key, "update"))
	    ctx->idle_events |= LISTENER_UPDATE;
	  else if (0 == strcmp(key, "player"))
	    ctx->idle_events |= LISTENER_PLAYER;
	  else if (0 == strcmp(key, "playlist"))
	    ctx->idle_events |= LISTENER_QUEUE;
	  else if (0 == strcmp(key, "mixer"))
	    ctx->idle_events |= LISTENER_VOLUME;
	  else if (0 == strcmp(key, "output"))
	    ctx->idle_events |= LISTENER_SPEAKER;
	  else if (0 == strcmp(key, "options"))
	    ctx->idle_events |= LISTENER_OPTIONS;
	  else if (0 == strcmp(key, "stored_playlist"))
	    ctx->idle_events |= LISTENER_STORED_PLAYLIST;
	  else if (0 == strcmp(key, "sticker"))
            ctx->idle_events |= LISTENER_RATING;
	  else
	    DPRINTF(E_DBG, L_MPD, "Idle command for '%s' not supported\n", key);
	}
    }
  else
    ctx->idle_events = MPD_ALL_IDLE_LISTENER_EVENTS;

  // If events the client listens to occurred since the last idle call (or since the client connected,
  // if it is the first idle call), notify immediately.
  if (ctx->events & ctx->idle_events)
    notify_idle_client(ctx, ctx->events, false);

  return 0;
}

static int
mpd_command_noidle(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  /*
   * The protocol specifies: "The idle command can be canceled by
   * sending the command noidle (no other commands are allowed). MPD
   * will then leave idle mode and print results immediately; might be
   * empty at this time."
   */
  if (ctx->events)
    notify_idle_client(ctx, ctx->events, false);

  ctx->is_idle = false;
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
mpd_command_status(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct player_status status;
  uint32_t queue_length = 0;
  int queue_version = 0;
  char *state;
  uint32_t itemid = 0;
  struct db_queue_item *queue_item;

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

  db_admin_getint(&queue_version, DB_ADMIN_QUEUE_VERSION);
  db_queue_get_count(&queue_length);

  evbuffer_add_printf(out->evbuf,
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
    queue_item = db_queue_fetch_byitemid(status.item_id);
  else
    queue_item = db_queue_fetch_bypos(0, status.shuffle);

  if (queue_item)
   {
      evbuffer_add_printf(out->evbuf,
	  "song: %d\n"
	  "songid: %d\n",
	  queue_item->pos,
	  queue_item->id);

      itemid = queue_item->id;
      free_queue_item(queue_item, 0);
   }

  if (status.status != PLAY_STOPPED)
   {
      evbuffer_add_printf(out->evbuf,
	  "time: %d:%d\n"
	  "elapsed: %#.3f\n"
	  "bitrate: 128\n"
	  "audio: 44100:16:2\n",
	  (status.pos_ms / 1000), (status.len_ms / 1000),
	  (status.pos_ms / 1000.0));
   }

  if (library_is_scanning())
    {
      evbuffer_add(out->evbuf, "updating_db: 1\n", 15);
    }

  if (itemid > 0)
    {
      queue_item = db_queue_fetch_next(itemid, status.shuffle);
      if (queue_item)
	{
	  evbuffer_add_printf(out->evbuf,
	      "nextsong: %d\n"
	      "nextsongid: %d\n",
	      queue_item->pos,
	      queue_item->id);

	  free_queue_item(queue_item, 0);
	}
    }

  return 0;
}

/*
 * Command handler function for 'stats'
 */
static int
mpd_command_stats(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { .type = Q_COUNT_ITEMS };
  struct filecount_info fci;
  double uptime;
  int64_t db_start = 0;
  int64_t db_update = 0;
  int ret;

  ret = db_filecount_get(&fci, &qp);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query"); 

  db_admin_getint64(&db_start, DB_ADMIN_START_TIME);
  uptime = difftime(time(NULL), (time_t) db_start);
  db_admin_getint64(&db_update, DB_ADMIN_DB_UPDATE);

  //TODO [mpd] Implement missing stats attributes (playtime)
  evbuffer_add_printf(out->evbuf,
      "artists: %d\n"
      "albums: %d\n"
      "songs: %d\n"
      "uptime: %.f\n" //in seceonds
      "db_playtime: %" PRIi64 "\n"
      "db_update: %" PRIi64 "\n"
      "playtime: %d\n",
      fci.artist_count,
      fci.album_count,
      fci.count,
      uptime,
      (fci.length / 1000),
      db_update,
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
mpd_command_consume(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  player_consume_set(in->argv_u32val[1]);
  return 0;
}

/*
 * Command handler function for 'random'
 * Sets the shuffle mode, expects argument argv[1] to be an integer with
 *   0 = disable shuffle
 *   1 = enable shuffle
 */
static int
mpd_command_random(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  player_shuffle_set(in->argv_u32val[1]);
  return 0;
}

/*
 * Command handler function for 'repeat'
 * Sets the repeat mode, expects argument argv[1] to be an integer with
 *   0 = repeat off
 *   1 = repeat all
 */
static int
mpd_command_repeat(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  if (in->argv_u32val[1] == 0)
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
mpd_command_setvol(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  player_volume_set(in->argv_u32val[1]);
  return 0;
}

/*
 * Read the (master) volume
 */
static int
mpd_command_getvol(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct player_status status;

  player_get_status(&status);
  evbuffer_add_printf(out->evbuf, "volume: %d\n", status.volume);
  return 0;
}

/*
 * Command handler function for 'single'
 * Sets the repeat mode, expects argument argv[1] to be an integer or
 * "oneshot" for 0.21 protocol.
 * The server only allows single-mode in combination with repeat, therefore
 * the command single translates (depending on the current repeat mode) into:
 * a) if repeat off:
 *   0 = repeat off
 *   1 = repeat song
 * b) if repeat all:
 *   0 = repeat all
 *   1 = repeat song
 * c) if repeat song:
 *   0 = repeat all
 *   1 = repeat song
 * Thus "oneshot" is accepted, but ignored under all circumstances.
 */
static int
mpd_command_single(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  bool has_enable = (in->has_num & MPD_WANTS_NUM_ARG1_UVAL); 
  uint32_t enable = in->argv_u32val[1];
  struct player_status status;

  // 0.21 protocol: accept "oneshot" mode
  if (strcmp(in->argv[1], "oneshot") == 0)
    return 0;

  if (!has_enable)
    RETURN_ERROR(ACK_ERROR_ARG, "Command 'single' expects integer or 'oneshot' argument");

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
 * The server does not support replay gain, therefor this function returns always
 * "replay_gain_mode: off".
 */
static int
mpd_command_replay_gain_status(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  evbuffer_add(out->evbuf, "replay_gain_mode: off\n", 22);
  return 0;
}

/*
 * Command handler function for 'volume'
 * Changes the volume by the given amount, expects argument argv[1] to be an integer
 */
static int
mpd_command_volume(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct player_status status;
  int32_t volume = in->argv_i32val[1];

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
mpd_command_next(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int ret;

  ret = player_playback_next();
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to skip to next song");

  ret = player_playback_start();
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Player returned an error for start after nextitem");

  return 0;
}

/*
 * Command handler function for 'pause'
 * Toggles pause/play, if the optional argument argv[1] is present, it must be an integer with
 *   0 = play
 *   1 = pause
 */
static int
mpd_command_pause(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  uint32_t pause;
  struct player_status status;
  int ret;

  player_get_status(&status);

  if (in->has_num & MPD_WANTS_NUM_ARG1_UVAL)
    pause = in->argv_u32val[1];
  else
    pause = (status.status == PLAY_PLAYING) ? 1 : 0;

  // MPD ignores pause in stopped state
  if (pause == 1 && status.status == PLAY_PLAYING)
    ret = player_playback_pause();
  else if (pause == 0 && status.status == PLAY_PAUSED)
    ret = player_playback_start();
  else
    ret = 0;

  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to %s playback", (pause == 1) ? "pause" : "start");

  return 0;
}

/*
 * Command handler function for 'play'
 * Starts playback, the optional argument argv[1] represents the position in the playqueue
 * where to start playback.
 */
static int
mpd_command_play(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  bool has_songpos = (in->has_num & MPD_WANTS_NUM_ARG1_UVAL);
  uint32_t songpos = in->argv_u32val[1];
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  player_get_status(&status);

  if (status.status == PLAY_PLAYING && !has_songpos)
    return 0;

  // Stop playback, if player is already playing and a valid song position is
  // given (it will be restarted for the given song position)
  if (status.status == PLAY_PLAYING)
    player_playback_stop();

  if (has_songpos)
    {
      queue_item = db_queue_fetch_bypos(songpos, 0);
      if (!queue_item)
	RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start playback, unknown song position");

      ret = player_playback_start_byitem(queue_item);
      free_queue_item(queue_item, 0);
    }
  else
    ret = player_playback_start();

  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start playback (queue empty?)");

  return 0;
}

/*
 * Command handler function for 'playid'
 * Starts playback, the optional argument argv[1] represents the songid of the song
 * where to start playback.
 */
static int
mpd_command_playid(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  bool has_id = (in->has_num & MPD_WANTS_NUM_ARG1_UVAL);
  uint32_t id = in->argv_u32val[1];
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  player_get_status(&status);

  // Stop playback, if player is already playing and a valid item id is given
  // (it will be restarted for the given song)
  if (status.status == PLAY_PLAYING)
    player_playback_stop();

  if (has_id)
    {
      queue_item = db_queue_fetch_byitemid(id);
      if (!queue_item)
	RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start playback, unknown song id");

      ret = player_playback_start_byitem(queue_item);
      free_queue_item(queue_item, 0);
    }
  else
    ret = player_playback_start();

  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start playback (queue empty?)");

  return 0;
}

/*
 * Command handler function for 'previous'
 * Skips to the previous song in the playqueue
 */
static int
mpd_command_previous(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int ret;

  ret = player_playback_prev();
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to skip to previous song");

  ret = player_playback_start();
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Player returned an error for start after previtem");

  return 0;
}

/*
 * Command handler function for 'seek'
 * Seeks to song at the given position in argv[1] to the position in seconds given in argument argv[2]
 * (fractions allowed).
 */
static int
mpd_command_seek(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  float seek_target_sec;
  int seek_target_msec;
  int ret;

  //TODO Allow seeking in songs not currently playing

  seek_target_sec = strtof(in->argv[2], NULL);
  seek_target_msec = seek_target_sec * 1000;

  ret = player_playback_seek(seek_target_msec, PLAYER_SEEK_POSITION);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to seek current song to time %d msec", seek_target_msec);

  ret = player_playback_start();
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start playback");

  return 0;
}

/*
 * Command handler function for 'seekid'
 * Seeks to song with id given in argv[1] to the position in seconds given in argument argv[2]
 * (fractions allowed).
 */
static int
mpd_command_seekid(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct player_status status;
  float seek_target_sec;
  int seek_target_msec;
  int ret;

  //TODO Allow seeking in songs not currently playing
  player_get_status(&status);
  if (status.item_id != in->argv_u32val[1])
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Given song is not the current playing one, seeking is not supported");

  seek_target_sec = strtof(in->argv[2], NULL);
  seek_target_msec = seek_target_sec * 1000;

  ret = player_playback_seek(seek_target_msec, PLAYER_SEEK_POSITION);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to seek current song to time %d msec", seek_target_msec);

  ret = player_playback_start();
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start playback");

  return 0;
}

/*
 * Command handler function for 'seekcur'
 * Seeks the current song to the position in seconds given in argument argv[1] (fractions allowed).
 */
static int
mpd_command_seekcur(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  float seek_target_sec;
  int seek_target_msec;
  int ret;

  seek_target_sec = strtof(in->argv[1], NULL);
  seek_target_msec = seek_target_sec * 1000;

  // TODO If prefixed by '+' or '-', then the time is relative to the current playing position.
  ret = player_playback_seek(seek_target_msec, PLAYER_SEEK_POSITION);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to seek current song to time %d msec", seek_target_msec);

  ret = player_playback_start();
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start playback");

  return 0;
}

/*
 * Command handler function for 'stop'
 * Stop playback.
 */
static int
mpd_command_stop(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int ret;

  ret = player_playback_stop();
  if (ret != 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to stop playback");

  return 0;
}

/*
 * Add media file item with given virtual path to the queue
 *
 * @param path The virtual path
 * @param exact_match If TRUE add only item with exact match, otherwise add all items virtual path start with the given path
 * @return The queue item id of the last inserted item or -1 on failure
 */
static int
mpd_queue_add(char *path, bool exact_match, int position)
{
  struct query_params qp = { .type = Q_ITEMS, .idx_type = I_NONE, .sort = S_ARTIST };
  struct player_status status;
  int new_item_id;
  int ret;

  new_item_id = 0;

  if (exact_match)
    CHECK_NULL(L_MPD, qp.filter = db_mprintf("f.disabled = 0 AND f.virtual_path LIKE '/%q'", path));
  else
    CHECK_NULL(L_MPD, qp.filter = db_mprintf("f.disabled = 0 AND f.virtual_path LIKE '/%q%%'", path));

  player_get_status(&status);

  ret = db_queue_add_by_query(&qp, status.shuffle, status.item_id, position, NULL, &new_item_id);

  free_query_params(&qp, 1);

  if (ret == 0)
    return new_item_id;

  return ret;
}

/*
 * add "file:/srv/music/Blue Foundation/Life Of A Ghost 2/07 Ghost.mp3" +1
 *
 * Adds the all songs under the given path to the end of the playqueue (directories add recursively).
 * Expects argument argv[1] to be a path to a single file or directory.
 */
static int
mpd_command_add(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct player_status status;
  int ret;

  ret = mpd_queue_add(in->argv[1], false, -1);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to add song '%s' to playlist", in->argv[1]);

  if (ret == 0)
    {
      player_get_status(&status);

      // Given path is not in the library, check if it is possible to add as a non-library queue item
      ret = library_queue_item_add(in->argv[1], -1, status.shuffle, status.item_id, NULL, NULL);
      if (ret != LIBRARY_OK)
	RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to add song '%s' to playlist (unknown path)", in->argv[1]);
    }

  return 0;
}

/*
 * addid "file:/srv/music/Blue Foundation/Life Of A Ghost 2/07 Ghost.mp3" +1
 *
 * Adds the song under the given path to the end or to the given position of the playqueue.
 * Expects argument argv[1] to be a path to a single file. argv[2] is optional, if present
 * as int is the absolute new position, if present as "+x" og "-x" it is relative.
 */
static int
mpd_command_addid(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int to_pos = -1;
  struct player_status status;
  char *path = in->argv[1];
  int ret;

  if (in->argc > 2)
    {
      ret = to_pos_from_arg(&to_pos, in->argv[2]);
      if (ret < 0)
	RETURN_ERROR(ACK_ERROR_ARG, "Invalid TO argument: '%s'", in->argv[2]);
    }

  ret = mpd_queue_add(path, true, to_pos);
  if (ret == 0)
    {
      player_get_status(&status);

      // Given path is not in the library, directly add it as a new queue item
      ret = library_queue_item_add(path, to_pos, status.shuffle, status.item_id, NULL, NULL);
      if (ret != LIBRARY_OK)
	RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to add song '%s' to playlist (unknown path)", path);
    }

  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to add song '%s' to playlist", path);

  evbuffer_add_printf(out->evbuf,
      "Id: %d\n",
      ret); // mpd_queue_add returns the item_id of the last inserted queue item

  return 0;
}

/*
 * Command handler function for 'clear'
 * Stops playback and removes all songs from the playqueue
 */
static int
mpd_command_clear(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
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
mpd_command_delete(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int start_pos;
  int end_pos;
  int count;
  int ret;

  // If argv[1] is omitted clear the whole queue
  if (in->argc < 2)
    {
      db_queue_clear(0);
      return 0;
    }

  // If argument argv[1] is present remove only the specified songs
  ret = range_pos_from_arg(&start_pos, &end_pos, in->argv[1]);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Argument doesn't convert to integer or range: '%s'", in->argv[1]);

  count = end_pos - start_pos;

  ret = db_queue_delete_bypos(start_pos, count);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to remove %d songs starting at position %d", count, start_pos);

  return 0;
}

/* Command handler function for 'deleteid'
 * Removes the song with given id from the playqueue. Expects argument argv[1] to be an integer (song id).
 */
static int
mpd_command_deleteid(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  uint32_t songid = in->argv_u32val[1];
  int ret;

  ret = db_queue_delete_byitemid(songid);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to remove song with id '%s'", in->argv[1]);

  return 0;
}

// Moves the song at FROM or range of songs at START:END to TO in the playlist.
static int
mpd_command_move(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int start_pos;
  int end_pos;
  int count;
  int to_pos;
  uint32_t queue_length;
  int ret;

  ret = range_pos_from_arg(&start_pos, &end_pos, in->argv[1]);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Argument doesn't convert to integer or range: '%s'", in->argv[1]);

  count = end_pos - start_pos;

  ret = to_pos_from_arg(&to_pos, in->argv[2]);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Invalid argument: '%s'", in->argv[2]);

  db_queue_get_count(&queue_length);

  // valid move pos and range is:
  //     0  <=  start  <  queue_len
  // start  <   end    <= queue_len
  //     0  <=  to     <= queue_len - count
  if (!(start_pos >= 0 && start_pos < queue_length
      && end_pos > start_pos && end_pos <= queue_length
      && to_pos >= 0 && to_pos <= queue_length - count))
    RETURN_ERROR(ACK_ERROR_ARG, "Range too large for target position %d or bad song index (count %d, length %u)", to_pos, count, queue_length);

  ret = db_queue_move_bypos_range(start_pos, end_pos, to_pos);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to move song at position %d to %d", start_pos, to_pos);

  return 0;
}

static int
mpd_command_moveid(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  uint32_t songid = in->argv_u32val[1];
  int to_pos;
  int ret;

  ret = to_pos_from_arg(&to_pos, in->argv[2]);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Invalid TO argument: '%s'", in->argv[2]);

  ret = db_queue_move_byitemid(songid, to_pos, 0);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to move song with id '%u' to index '%u'", songid, to_pos);

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
mpd_command_playlistid(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  bool has_songid = (in->has_num & MPD_WANTS_NUM_ARG1_UVAL);
  uint32_t songid = in->argv_u32val[1];
  struct query_params qp = { 0 };
  struct db_queue_item queue_item;
  int ret;

  if (has_songid)
    qp.filter = db_mprintf("id = %d", songid);

  ret = db_queue_enum_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_ARG, "Failed to start queue enum for command playlistid");
    }

  while ((ret = db_queue_enum_fetch(&qp, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(out->evbuf, &queue_item);
      if (ret < 0)
	{
	  db_queue_enum_end(&qp);
	  free_query_params(&qp, 1);
	  RETURN_ERROR(ACK_ERROR_UNKNOWN, "Error adding media info for file"); 
	}
    }

  db_queue_enum_end(&qp);
  free_query_params(&qp, 1);

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
mpd_command_playlistinfo(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { 0 };
  struct db_queue_item queue_item;
  int start_pos;
  int end_pos;
  int ret;

  start_pos = 0;
  end_pos = 0;

  if (in->argc > 1)
    {
      ret = range_pos_from_arg(&start_pos, &end_pos, in->argv[1]);
      if (ret < 0)
	RETURN_ERROR(ACK_ERROR_ARG, "Argument doesn't convert to integer or range: '%s'", in->argv[1]);

      if (start_pos < 0)
	DPRINTF(E_DBG, L_MPD, "Command 'playlistinfo' called with pos < 0 (arg = '%s'), ignore arguments and return whole queue\n", in->argv[1]);
      else
	qp.filter = db_mprintf("pos >= %d AND pos < %d", start_pos, end_pos);
    }

  ret = db_queue_enum_start(&qp);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Failed to start queue enum for command playlistinfo: '%s'", in->argv[1]);

  while ((ret = db_queue_enum_fetch(&qp, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(out->evbuf, &queue_item);
      if (ret < 0)
	{
	  db_queue_enum_end(&qp);
	  free_query_params(&qp, 1);
	  RETURN_ERROR(ACK_ERROR_UNKNOWN, "Error adding media info");
	}
    }

  db_queue_enum_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

/*
 * playlistfind {FILTER} [sort {TYPE}] [window {START:END}]
 * Searches for songs that match in the queue
 * TODO add support for window (currently not supported by db_queue_enum_x)
 */
static int
mpd_command_playlistfind(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { 0 };
  struct db_queue_item queue_item;
  int ret;

  ret = parse_command(&qp, NULL, NULL, in);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown argument(s) for command 'playlistfind'");

  ret = db_queue_enum_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_ARG, "Failed to start queue enum for command playlistinfo: '%s'", in->argv[1]);
    }

  while ((ret = db_queue_enum_fetch(&qp, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(out->evbuf, &queue_item);
      if (ret < 0)
	{
	  db_queue_enum_end(&qp);
	  free_query_params(&qp, 1);
	  RETURN_ERROR(ACK_ERROR_UNKNOWN, "Error adding media info");
	}
    }

  db_queue_enum_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

/*
 * playlistsearch {FILTER} [sort {TYPE}] [window {START:END}]
 */
static int
mpd_command_playlistsearch(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { 0 };
  struct db_queue_item queue_item;
  int ret;

  ret = parse_command(&qp, NULL, NULL, in);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown argument(s) for command 'playlistsearch'");

  ret = db_queue_enum_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_ARG, "Failed to start queue enum for command playlistinfo: '%s'", in->argv[1]);
    }

  while ((ret = db_queue_enum_fetch(&qp, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(out->evbuf, &queue_item);
      if (ret < 0)
	{
	  db_queue_enum_end(&qp);
	  free_query_params(&qp, 1);
	  RETURN_ERROR(ACK_ERROR_UNKNOWN, "Error adding media info");
	}
    }

  db_queue_enum_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

static int
plchanges_build_queryparams(struct query_params *qp, uint32_t version, const char *range)
{
  int start_pos;
  int end_pos;
  int ret;

  memset(qp, 0, sizeof(struct query_params));

  start_pos = 0;
  end_pos = 0;
  if (range)
    {
      ret = range_pos_from_arg(&start_pos, &end_pos, range);
      if (ret < 0)
	return -1;

      if (start_pos < 0)
	DPRINTF(E_DBG, L_MPD, "Invalid range '%s', will return entire queue\n", range);
    }

  if (start_pos < 0 || end_pos <= 0)
    qp->filter = db_mprintf("(queue_version > %d)", version);
  else
    qp->filter = db_mprintf("(queue_version > %d AND pos >= %d AND pos < %d)", version, start_pos, end_pos);

  return 0;
}

/*
 * plchanges {VERSION} [START:END]
 * Lists all changed songs in the queue since the given playlist version in argv[1].
 */
static int
mpd_command_plchanges(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  uint32_t version = in->argv_u32val[1];
  const char *range = (in->argc > 2) ? in->argv[2] : NULL;
  struct query_params qp;
  struct db_queue_item queue_item;
  int ret;

  ret = plchanges_build_queryparams(&qp, version, range);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Invalid range '%s' provided to plchanges", range);

  ret = db_queue_enum_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start queue enum for command plchangesposid");
    }

  while ((ret = db_queue_enum_fetch(&qp, &queue_item)) == 0 && queue_item.id > 0)
    {
      ret = mpd_add_db_queue_item(out->evbuf, &queue_item);
      if (ret < 0)
	{
	  db_queue_enum_end(&qp);
	  free_query_params(&qp, 1);
	  RETURN_ERROR(ACK_ERROR_UNKNOWN, "Error adding media info");
	}
    }

  db_queue_enum_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

/*
 * plchangesposid {VERSION} [START:END]
 * Lists all changed songs in the queue since the given playlist version in
 * argv[1] without metadata.
 */
static int
mpd_command_plchangesposid(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  uint32_t version = in->argv_u32val[1];
  const char *range = (in->argc > 2) ? in->argv[2] : NULL;
  struct query_params qp;
  struct db_queue_item queue_item;
  int ret;

  ret = plchanges_build_queryparams(&qp, version, range);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Invalid range '%s' provided to plchangesposid", range);

  ret = db_queue_enum_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start queue enum for command plchangesposid");
    }

  while ((ret = db_queue_enum_fetch(&qp, &queue_item)) == 0 && queue_item.id > 0)
    {
      evbuffer_add_printf(out->evbuf,
      	  "cpos: %d\n"
      	  "Id: %d\n",
      	  queue_item.pos,
	  queue_item.id);
    }

  db_queue_enum_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

/*
 * Command handler function for 'listplaylist'
 * Lists all songs in the playlist given by virtual-path in argv[1].
 */
static int
mpd_command_listplaylist(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *path;
  struct playlist_info *pli;
  struct query_params qp = { .type = Q_PLITEMS, .idx_type = I_NONE };
  struct db_media_file_info dbmfi;
  int ret;

  if (!default_pl_dir || strstr(in->argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      path = prepend_slash(in->argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      path = safe_asprintf("%s/%s", default_pl_dir, in->argv[1]);
    }

  pli = db_pl_fetch_byvirtualpath(path);
  free(path);
  if (!pli)
    RETURN_ERROR(ACK_ERROR_ARG, "Playlist not found for path '%s'", in->argv[1]);

  qp.id = pli->id;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_pli(pli, 0);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }

  while ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      evbuffer_add_printf(out->evbuf,
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
mpd_command_listplaylistinfo(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *path;
  struct playlist_info *pli;
  struct query_params qp = { .type = Q_PLITEMS, .idx_type = I_NONE };
  struct db_media_file_info dbmfi;
  int ret;

  if (!default_pl_dir || strstr(in->argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      path = prepend_slash(in->argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      path = safe_asprintf("%s/%s", default_pl_dir, in->argv[1]);
    }

  pli = db_pl_fetch_byvirtualpath(path);
  free(path);
  if (!pli)
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "Playlist not found for path '%s'", in->argv[1]);

  qp.id = pli->id;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_pli(pli, 0);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }

  while ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      ret = mpd_add_db_media_file_info(out->evbuf, &dbmfi);
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
mpd_command_listplaylists(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { .type = Q_PL, .idx_type = I_NONE, .sort = S_PLAYLIST };
  struct db_playlist_info dbpli;
  char modified[32];
  uint32_t time_modified;
  int ret;

  qp.filter = db_mprintf("(f.type = %d OR f.type = %d)", PL_PLAIN, PL_SMART);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }

  while (((ret = db_query_fetch_pl(&dbpli, &qp)) == 0) && (dbpli.id))
    {
      if (safe_atou32(dbpli.db_timestamp, &time_modified) != 0)
	{
	  db_query_end(&qp);
	  free_query_params(&qp, 1);
	  RETURN_ERROR(ACK_ERROR_UNKNOWN, "Error converting time modified to uint32_t");
	}

      mpd_time(modified, sizeof(modified), time_modified);

      evbuffer_add_printf(out->evbuf,
	  "playlist: %s\n"
	  "Last-Modified: %s\n",
	  (dbpli.virtual_path + 1),
	  modified);
    }

  db_query_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

/*
 * Command handler function for 'load'
 * Adds the playlist given by virtual-path in argv[1] to the queue.
 */
static int
mpd_command_load(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *path;
  struct playlist_info *pli;
  struct player_status status;
  struct query_params qp = { .type = Q_PLITEMS };
  int ret;

  if (!default_pl_dir || strstr(in->argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      path = prepend_slash(in->argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      path = safe_asprintf("%s/%s", default_pl_dir, in->argv[1]);
    }

  pli = db_pl_fetch_byvirtualpath(path);
  free(path);
  if (!pli)
    RETURN_ERROR(ACK_ERROR_ARG, "Playlist not found for path '%s'", in->argv[1]);

  //TODO If a second parameter is given only add the specified range of songs to the playqueue

  qp.id = pli->id;
  free_pli(pli, 0);

  player_get_status(&status);

  ret = db_queue_add_by_query(&qp, status.shuffle, status.item_id, -1, NULL, NULL);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to add song '%s' to playlist", in->argv[1]);

  return 0;
}

static int
mpd_command_playlistadd(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *vp_playlist;
  char *vp_item;
  int ret;

  if (!allow_modifying_stored_playlists)
    RETURN_ERROR(ACK_ERROR_PERMISSION, "Modifying stored playlists is not enabled");

  if (!default_pl_dir || strstr(in->argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      vp_playlist = prepend_slash(in->argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      vp_playlist = safe_asprintf("%s/%s", default_pl_dir, in->argv[1]);
    }

  vp_item = prepend_slash(in->argv[2]);

  ret = library_playlist_item_add(vp_playlist, vp_item);
  free(vp_playlist);
  free(vp_item);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Error adding item to file '%s'", in->argv[1]);

  return 0;
}

static int
mpd_command_rm(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *virtual_path;
  int ret;

  if (!allow_modifying_stored_playlists)
    RETURN_ERROR(ACK_ERROR_PERMISSION, "Modifying stored playlists is not enabled");

  if (!default_pl_dir || strstr(in->argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      virtual_path = prepend_slash(in->argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      virtual_path = safe_asprintf("%s/%s", default_pl_dir, in->argv[1]);
    }

  ret = library_playlist_remove(virtual_path);
  free(virtual_path);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Error removing playlist '%s'", in->argv[1]);

  return 0;
}

static int
mpd_command_save(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *virtual_path;
  int ret;

  if (!allow_modifying_stored_playlists)
    RETURN_ERROR(ACK_ERROR_PERMISSION, "Modifying stored playlists is not enabled");

  if (!default_pl_dir || strstr(in->argv[1], ":/"))
    {
      // Argument is a virtual path, make sure it starts with a '/'
      virtual_path = prepend_slash(in->argv[1]);
    }
  else
    {
      // Argument is a playlist name, prepend default playlist directory
      virtual_path = safe_asprintf("%s/%s", default_pl_dir, in->argv[1]);
    }

  ret = library_queue_save(virtual_path);
  free(virtual_path);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Error saving queue to file '%s'", in->argv[1]);

  return 0;
}

/*
 * albumart {uri} {offset}
 *  or
 * readpicture {uri} {offset}
 *
 * From the docs the offset appears to be mandatory even if 0, but we treat it
 * as optional. We don't differentiate between getting album or picture (track)
 * artwork, since the artwork module will do its own thing. If no artwork can
 * be found we return a 0 byte response, as per the docs.
 */
static int
mpd_command_albumart(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  char *virtual_path;
  uint32_t offset = in->argv_u32val[2];
  struct evbuffer *artwork;
  size_t total_size;
  size_t len;
  int format;
  int id;

  virtual_path = prepend_slash(in->argv[1]);
  id = db_file_id_byvirtualpath(virtual_path);
  free(virtual_path);
  if (id <= 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Invalid path");

  CHECK_NULL(L_MPD, artwork = evbuffer_new());

  // Ref. docs: "If the song file was recognized, but there is no picture, the
  // response is successful, but is otherwise empty"
  format = artwork_get_item(artwork, id, ART_DEFAULT_WIDTH, ART_DEFAULT_HEIGHT, 0);
  if (format == ART_FMT_PNG)
    evbuffer_add_printf(out->evbuf, "type: image/png\n");
  else if (format == ART_FMT_JPEG)
    evbuffer_add_printf(out->evbuf, "type: image/jpeg\n");
  else
    goto out;

  total_size = evbuffer_get_length(artwork);
  evbuffer_add_printf(out->evbuf, "size: %zu\n", total_size);

  evbuffer_drain(artwork, offset);

  len = MIN(ctx->binarylimit, evbuffer_get_length(artwork));
  evbuffer_add_printf(out->evbuf, "binary: %zu\n", len);

  evbuffer_remove_buffer(artwork, out->evbuf, len);
  evbuffer_add(out->evbuf, "\n", 1);

 out:
  evbuffer_free(artwork);
  return 0;
}

/*
 * count [FILTER] [group {GROUPTYPE}]
 *
 * TODO Support for groups (the db interface doesn't have method for this). Note
 * that mpd only supports one group. Mpd has filter as optional. Without filter,
 * but with group, mpd returns:
 *
 * Album: Welcome
 * songs: 1
 * playtime: 249
 */
static int
mpd_command_count(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { .type = Q_COUNT_ITEMS };
  struct filecount_info fci;
  int ret;

  ret = parse_command(&qp, NULL, NULL, in);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown argument(s) for command 'count'");

  ret = db_filecount_get(&fci, &qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }

  evbuffer_add_printf(out->evbuf,
      "songs: %d\n"
      "playtime: %" PRIu64 "\n",
      fci.count,
      (fci.length / 1000));

  db_query_end(&qp);
  free_query_params(&qp, 1);
  return 0;
}

/*
 * find "albumartist" "Blue Foundation" "album" "Life Of A Ghost" "date" "2007" "window" "0:1"
 * search "(modified-since '2024-06-04T22:49:41Z')"
 * find "((Album == 'No Sign Of Bad') AND (AlbumArtist == 'Led Zeppelin'))" window 0:1
 * find "(Artist == \"foo\\'bar\\\"\")"
 *
 * TODO not sure if we support this correctly: "An empty value string means:
 * match only if the given tag type does not exist at all; this implies that
 * negation with an empty value checks for the existence of the given tag type."
 * MaximumMPD (search function):
 *   count "((Artist == \"Blue Foundation\") AND (album == \"\"))"
 */
static int
mpd_command_find(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { .type = Q_ITEMS, .idx_type = I_NONE, .sort = S_ARTIST };
  struct db_media_file_info dbmfi;
  int ret;

  ret = parse_command(&qp, NULL, NULL, in);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown argument(s) in '%s'", in->args_raw);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }

  while ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      ret = mpd_add_db_media_file_info(out->evbuf, &dbmfi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error adding song to the evbuffer, song id: %s\n", dbmfi.id);
	}
    }

  db_query_end(&qp);
  free_query_params(&qp, 1);
  return 0;
}

/*
 * findadd "albumartist" "Blue Foundation" "album" "Life Of A Ghost" "date" "2007" "window" "3:4" position 0
 */
static int
mpd_command_findadd(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { .type = Q_ITEMS, .idx_type = I_NONE, .sort = S_ARTIST };
  char *pos = NULL;
  int to_pos;
  int ret;

  ret = parse_command(&qp, &pos, NULL, in);
  if (ret < 0)
    goto error;

  ret = to_pos_from_arg(&to_pos, pos);
  if (ret < 0)
    goto error;

  ret = db_queue_add_by_query(&qp, 0, 0, to_pos, NULL, NULL);
  if (ret < 0)
    goto error;

  free_query_params(&qp, 1);
  free(pos);
  return 0;

 error:
  free_query_params(&qp, 1);
  free(pos);
  RETURN_ERROR(ACK_ERROR_ARG, "Invalid arguments");
}

static void
groups_from_dbcols(struct mpd_tag_map *groups[], size_t sz, const char *dbcols)
{
  char *copy = strdup(dbcols);
  char *saveptr;
  char *col;
  int i;

  for (col = strtok_r(copy, ",", &saveptr), i = 0; col && i < sz - 1; col = strtok_r(NULL, ",", &saveptr), i++)
    {
      trim(col);
      groups[i] = mpd_parser_tag_from_dbcol(col);
    }

  groups[i] = NULL;
  free(copy);
}

/*
 * list {TYPE} {FILTER} [group {GROUPTYPE} [group {GROUPTYPE} [...]]]
 *
 * Examples
 *  Rygelian: list Album group Date group AlbumArtistSort group AlbumArtist
 *  Plattenalbum: list "albumsort" "albumartist" "Bob Dylan" "group" "date" "group" "album"
 *  list Album "(Artist starts_with \"K\")" group AlbumArtist
 *
 * TODO Note that the below repeats group tags like so:
 *  AlbumArtist: Kasabian
 *  Album: Empire
 *  AlbumArtist: Kasabian
 *  Album: West Ryder Pauper Lunatic Asylum
 * mpd doesn't repeat them:
 *  AlbumArtist: Kasabian
 *  Album: Empire
 *  Album: West Ryder Pauper Lunatic Asylum
 */
static int
mpd_command_list(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct query_params qp = { .type = Q_ITEMS, .sort = S_ARTIST };
  struct db_media_file_info dbmfi;
  struct mpd_tag_map *groups[16];
  char **strval;
  int ret;
  int i;

  ret = parse_command(&qp, NULL, NULL, in);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown argument(s) in: '%s'", in->args_raw);

  // qp.group should at least include the tag type field
  if (!qp.group)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Bug! Unknown or unsupported tag type/groups: '%s'", in->args_raw);
    }

  groups_from_dbcols(groups, ARRAY_SIZE(groups), qp.group);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }

  while ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      for (i = 0; i < ARRAY_SIZE(groups) && groups[i]; i++)
	{
	  strval = (char **) ((char *)&dbmfi + groups[i]->dbmfi_offset);

	  if (!(*strval) || (**strval == '\0'))
	    continue;

	  evbuffer_add_printf(out->evbuf, "%s: %s\n", groups[i]->name, sanitize(*strval));
	}
    }

  db_query_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

static int
mpd_add_directory(struct mpd_command_output *out, int directory_id, int listall, int listinfo)
{
  struct directory_info subdir;
  struct query_params qp = { .type = Q_PL, .idx_type = I_NONE, .sort = S_PLAYLIST };
  struct directory_enum dir_enum;
  struct db_playlist_info dbpli;
  char modified[32];
  uint32_t time_modified;
  struct db_media_file_info dbmfi;
  int ret;

  // Load playlists for dir-id
  qp.filter = db_mprintf("(f.directory_id = %d AND (f.type = %d OR f.type = %d))", directory_id, PL_PLAIN, PL_SMART);
  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }
  while (((ret = db_query_fetch_pl(&dbpli, &qp)) == 0) && (dbpli.id))
    {
      if (safe_atou32(dbpli.db_timestamp, &time_modified) != 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error converting time modified to uint32_t: %s\n", dbpli.db_timestamp);
	}

      if (listinfo)
	{
	  mpd_time(modified, sizeof(modified), time_modified);
	  evbuffer_add_printf(out->evbuf,
	    "playlist: %s\n"
	    "Last-Modified: %s\n",
	    (dbpli.virtual_path + 1),
	    modified);
	}
      else
	{
	  evbuffer_add_printf(out->evbuf,
	    "playlist: %s\n",
	    (dbpli.virtual_path + 1));
	}
    }
  db_query_end(&qp);
  free_query_params(&qp, 1);

  // Load sub directories for dir-id
  memset(&dir_enum, 0, sizeof(struct directory_enum));
  dir_enum.parent_id = directory_id;
  ret = db_directory_enum_start(&dir_enum);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Failed to start directory enum");

  while ((ret = db_directory_enum_fetch(&dir_enum, &subdir)) == 0 && subdir.id > 0)
    {
      if (listinfo)
	{
	  evbuffer_add_printf(out->evbuf,
	    "directory: %s\n"
	    "Last-Modified: %s\n",
	    (subdir.virtual_path + 1),
	    "2015-12-01 00:00");
	}
      else
	{
	  evbuffer_add_printf(out->evbuf,
	    "directory: %s\n",
	    (subdir.virtual_path + 1));
	}

      if (listall)
	{
	  mpd_add_directory(out, subdir.id, listall, listinfo);
	}
    }
  db_directory_enum_end(&dir_enum);

  // Load files for dir-id
  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_ITEMS;
  qp.sort = S_ARTIST;
  qp.idx_type = I_NONE;
  qp.filter = db_mprintf("(f.directory_id = %d)", directory_id);
  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }
  while ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      if (listinfo)
	{
	  ret = mpd_add_db_media_file_info(out->evbuf, &dbmfi);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_MPD, "Error adding song to the evbuffer, song id: %s\n", dbmfi.id);
	    }
	}
      else
	{
	  evbuffer_add_printf(out->evbuf,
	    "file: %s\n",
	    (dbmfi.virtual_path + 1));
	}
    }
  db_query_end(&qp);
  free_query_params(&qp, 1);

  return 0;
}

static int
mpd_command_listall(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int dir_id;
  char parent[PATH_MAX];
  int ret;

  if (in->argc < 2 || strlen(in->argv[1]) == 0
      || (strncmp(in->argv[1], "/", 1) == 0 && strlen(in->argv[1]) == 1))
    {
      ret = snprintf(parent, sizeof(parent), "/");
    }
  else if (strncmp(in->argv[1], "/", 1) == 0)
    {
      ret = snprintf(parent, sizeof(parent), "%s/", in->argv[1]);
    }
  else
    {
      ret = snprintf(parent, sizeof(parent), "/%s", in->argv[1]);
    }

  if ((ret < 0) || (ret >= sizeof(parent)))
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Parent path exceeds PATH_MAX");

  // Load dir-id from db for parent-path
  dir_id = db_directory_id_byvirtualpath(parent);
  if (dir_id == 0)
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "Directory info not found for virtual-path '%s'", parent);

  return mpd_add_directory(out, dir_id, 1, 0);
}

static int
mpd_command_listallinfo(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int dir_id;
  char parent[PATH_MAX];
  int ret;

  if (in->argc < 2 || strlen(in->argv[1]) == 0
      || (strncmp(in->argv[1], "/", 1) == 0 && strlen(in->argv[1]) == 1))
    {
      ret = snprintf(parent, sizeof(parent), "/");
    }
  else if (strncmp(in->argv[1], "/", 1) == 0)
    {
      ret = snprintf(parent, sizeof(parent), "%s/", in->argv[1]);
    }
  else
    {
      ret = snprintf(parent, sizeof(parent), "/%s", in->argv[1]);
    }

  if ((ret < 0) || (ret >= sizeof(parent)))
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Parent path exceeds PATH_MAX");

  // Load dir-id from db for parent-path
  dir_id = db_directory_id_byvirtualpath(parent);
  if (dir_id == 0)
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "Directory info not found for virtual-path '%s'", parent);

  return mpd_add_directory(out, dir_id, 1, 1);
}

/*
 * Command handler function for 'lsinfo'
 * Lists the contents of the directory given in argv[1].
 */
static int
mpd_command_lsinfo(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int dir_id;
  char parent[PATH_MAX];
  int print_playlists;
  int ret;

  if (in->argc < 2 || strlen(in->argv[1]) == 0
      || (strncmp(in->argv[1], "/", 1) == 0 && strlen(in->argv[1]) == 1))
    {
      ret = snprintf(parent, sizeof(parent), "/");
    }
  else if (strncmp(in->argv[1], "/", 1) == 0)
    {
      ret = snprintf(parent, sizeof(parent), "%s/", in->argv[1]);
    }
  else
    {
      ret = snprintf(parent, sizeof(parent), "/%s", in->argv[1]);
    }

  if ((ret < 0) || (ret >= sizeof(parent)))
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Parent path exceeds PATH_MAX");

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
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "Directory info not found for virtual-path '%s'", parent);

  ret = mpd_add_directory(out, dir_id, 0, 1);

  // If the root directory was passed as argument add the stored playlists to the response
  if (ret == 0 && print_playlists)
    {
      return mpd_command_listplaylists(out, in, ctx);
    }

  return ret;
}

/*
 * Command handler function for 'listfiles'
 *
 * This command should list all files including files that are not part of the library. We do not support this
 * and only report files in the library.
 */
static int
mpd_command_listfiles(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  return mpd_command_lsinfo(out, in, ctx);
}

/*
 * Command handler function for 'update'
 * Initiates an init-rescan (scans for new files)
 */
static int
mpd_command_update(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  if (in->argc > 1 && strlen(in->argv[1]) > 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Update for specific uri not supported for command 'update'");

  library_rescan(0);

  evbuffer_add(out->evbuf, "updating_db: 1\n", 15);

  return 0;
}

/*
 * sticker get song "/file:/path/to/song.wav" rating
 */
static int
mpd_sticker_get(struct mpd_command_output *out, struct mpd_command_input *in, const char *virtual_path)
{
  struct media_file_info *mfi;
  uint32_t rating;

  if (strcmp(in->argv[4], "rating") != 0)
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "No such sticker");

  mfi = db_file_fetch_byvirtualpath(virtual_path);
  if (!mfi)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown sticker domain");

  if (mfi->rating > 0)
    {
      rating = mfi->rating / MPD_RATING_FACTOR;
      evbuffer_add_printf(out->evbuf, "sticker: rating=%d\n", rating);
    }

  free_mfi(mfi, 0);

  return 0;
}

/*
 * sticker set song "/file:/path/to/song.wav" rating 10
 */
static int
mpd_sticker_set(struct mpd_command_output *out, struct mpd_command_input *in, const char *virtual_path)
{
  uint32_t rating;
  int id;
  int ret;

  if (strcmp(in->argv[4], "rating") != 0)
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "No such sticker");

  ret = safe_atou32(in->argv[5], &rating);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Rating '%s' doesn't convert to integer", in->argv[5]);

  rating *= MPD_RATING_FACTOR;
  if (rating > DB_FILES_RATING_MAX)
    RETURN_ERROR(ACK_ERROR_ARG, "Rating '%s' is greater than maximum value allowed", in->argv[5]);

  id = db_file_id_byvirtualpath(virtual_path);
  if (id <= 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Invalid path '%s'", virtual_path);

  library_item_attrib_save(id, LIBRARY_ATTRIB_RATING, rating);

  return 0;
}

static int
mpd_sticker_delete(struct mpd_command_output *out, struct mpd_command_input *in, const char *virtual_path)
{
  int id;

  if (strcmp(in->argv[4], "rating") != 0)
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "No such sticker");

  id = db_file_id_byvirtualpath(virtual_path);
  if (id <= 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Invalid path '%s'", virtual_path);

  library_item_attrib_save(id, LIBRARY_ATTRIB_RATING, 0);

  return 0;
}

/*
 * sticker list song "/file:/path/to/song.wav"
 */
static int
mpd_sticker_list(struct mpd_command_output *out, struct mpd_command_input *in, const char *virtual_path)
{
  struct media_file_info *mfi;
  uint32_t rating;

  mfi = db_file_fetch_byvirtualpath(virtual_path);
  if (!mfi)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown sticker domain");

  if (mfi->rating > 0)
    {
      rating = mfi->rating / MPD_RATING_FACTOR;
      evbuffer_add_printf(out->evbuf, "sticker: rating=%d\n", rating);
    }

  free_mfi(mfi, 0);

  /* |:todo:| real sticker implementation */
  return 0;
}

/*
 * sticker find {TYPE} {URI} {NAME} [sort {SORTTYPE}] [window {START:END}]
 * sticker find {TYPE} {URI} {NAME} = {VALUE} [sort {SORTTYPE}] [window {START:END}]
 *
 * Example:
 *   sticker find song "/file:/path" rating = 10 
 */
static int
mpd_sticker_find(struct mpd_command_output *out, struct mpd_command_input *in, const char *virtual_path)
{
  struct query_params qp = { .type = Q_ITEMS, .idx_type = I_NONE, .sort = S_VPATH };
  struct db_media_file_info dbmfi;
  uint32_t rating = 0;
  uint32_t rating_arg = 0;
  const char *operator;
  int ret = 0;

  if (strcmp(in->argv[4], "rating") != 0)
    RETURN_ERROR(ACK_ERROR_NO_EXIST, "No such sticker");

  if (in->argc > 6)
    {
      if (strcmp(in->argv[5], "=") != 0 && strcmp(in->argv[5], ">") != 0 && strcmp(in->argv[5], "<") != 0)
	RETURN_ERROR(ACK_ERROR_ARG, "Invalid operator '%s' given to 'sticker find'", in->argv[5]);

      operator = in->argv[5];

      ret = safe_atou32(in->argv[6], &rating_arg);
      if (ret < 0)
	RETURN_ERROR(ACK_ERROR_ARG, "Rating '%s' doesn't convert to integer", in->argv[6]);

      rating_arg *= MPD_RATING_FACTOR;
    }
  else
    {
      operator = ">";
      rating_arg = 0;
    }

  qp.filter = db_mprintf("(f.virtual_path LIKE '%s%%' AND f.rating > 0 AND f.rating %s %d)", virtual_path, operator, rating_arg);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      free_query_params(&qp, 1);
      RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not start query");
    }

  while ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      ret = safe_atou32(dbmfi.rating, &rating);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MPD, "Error rating=%s doesn't convert to integer, song id: %s\n",
		  dbmfi.rating, dbmfi.id);
	  continue;
	}

      rating /= MPD_RATING_FACTOR;
      ret = evbuffer_add_printf(out->evbuf,
				"file: %s\n"
				"sticker: rating=%d\n",
				(dbmfi.virtual_path + 1),
				rating);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Error adding song to the evbuffer, song id: %s\n", dbmfi.id);
    }

  db_query_end(&qp);
  free_query_params(&qp, 1);
  return 0;
}

struct mpd_sticker_command {
  const char *cmd;
  int (*handler)(struct mpd_command_output *out, struct mpd_command_input *in, const char *virtual_path);
  int need_args;
};

static struct mpd_sticker_command mpd_sticker_handlers[] =
  {
    /* sticker command    | handler function        | minimum argument count */
    { "get",                mpd_sticker_get,          5 },
    { "set",                mpd_sticker_set,          6 },
    { "delete",             mpd_sticker_delete,       5 },
    { "list",               mpd_sticker_list,         4 },
    { "find",               mpd_sticker_find,         6 },
    { NULL, NULL, 0 },
  };

/*
 * Command handler function for 'sticker'
 *
 *   sticker get "noth here" rating
 *   ACK [2@0] {sticker} unknown sticker domain
 *
 *   sticker get song "Al Cohn & Shorty Rogers/East Coast - West Coast Scene/04 Shorty Rogers - Cool Sunshine.flac" rating
 *   ACK [50@0] {sticker} no such sticker
 *
 *   sticker get song "Al Cohn & Shorty Rogers/East Coast - West Coast Scene/03 Al Cohn - Serenade For Kathy.flac" rating
 *   sticker: rating=8
 *   OK
 *
 * From cantata:
 *   sticker set song "file:/srv/music/VA/The Electro Swing Revolution Vol 3 1 - Hop, Hop, Hop/13 Mr. Hotcut - You Are.mp3" rating "6"
 *   OK
 */
static int
mpd_command_sticker(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct mpd_sticker_command *cmd_param = NULL;  // Quell compiler warning about uninitialized use of cmd_param
  char *virtual_path;
  int ret;
  int i;

  if (strcmp(in->argv[2], "song") != 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown sticker domain");

  for (i = 0; i < (sizeof(mpd_sticker_handlers) / sizeof(struct mpd_sticker_command)); ++i)
    {
      cmd_param = &mpd_sticker_handlers[i];
      if (cmd_param->cmd && strcmp(in->argv[1], cmd_param->cmd) == 0)
	break;
    }

  if (!cmd_param->cmd)
    RETURN_ERROR(ACK_ERROR_ARG, "Bad request");

  if (in->argc < cmd_param->need_args)
    RETURN_ERROR(ACK_ERROR_ARG, "Not enough arguments");

  virtual_path = prepend_slash(in->argv[3]);

  ret = cmd_param->handler(out, in, virtual_path);

  free(virtual_path);
  return ret;
}

static int
mpd_command_close(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  ctx->must_disconnect = true;
  return 0;
}

static int
mpd_command_password(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  const char *required_password;
  const char *supplied_password = (in->argc > 1) ? in->argv[1] : "";
  bool password_is_required;

  required_password = cfg_getstr(cfg_getsec(cfg, "library"), "password");
  password_is_required = required_password && required_password[0] != '\0';
  if (password_is_required && strcmp(supplied_password, required_password) != 0)
    RETURN_ERROR(ACK_ERROR_PASSWORD, "Wrong password. Authentication failed.");

  ctx->authenticated = true;
  return 0;
}

static int
mpd_command_binarylimit(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  uint32_t size = in->argv_u32val[1];

  if (size < MPD_BINARY_SIZE_MIN)
    RETURN_ERROR(ACK_ERROR_ARG, "Value too small");

  ctx->binarylimit = size;

  return 0;
}

/*
 * Command handler function for 'disableoutput', 'enableoutput', 'toggleoutput'
 * Expects argument argv[1] to be the id of the output.
 */
static int
mpd_command_xoutput(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  const char *action = in->argv[0];
  uint32_t shortid = in->argv_u32val[1];
  struct player_speaker_info spk;
  int ret;

  if (shortid == mpd_plugin_httpd_shortid)
    RETURN_ERROR(ACK_ERROR_ARG, "Output cannot be toggled");

  ret = player_speaker_get_byindex(&spk, shortid);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_ARG, "Unknown output");

  if ((spk.selected && strcasecmp(action, "enable") == 0) || (!spk.selected && strcasecmp(action, "disable") == 0))
    return 0; // Nothing to do

  ret = spk.selected ? player_speaker_disable(spk.id) : player_speaker_enable(spk.id);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Output error, see log");

  return 0;
}

/*
 * Callback function for the 'outputs' command.
 * Gets called for each available speaker and prints the speaker information to the evbuffer given in *arg.
 *
 * Example output:
 *   outputid: 0
 *   outputname: Computer
 *   plugin: alsa
 *   outputenabled: 1
 * https://mpd.readthedocs.io/en/latest/protocol.html#command-outputs
 */
static void
speaker_enum_cb(struct player_speaker_info *spk, void *arg)
{
  struct param_output *param = arg;
  char plugin[sizeof(spk->output_type)];
  char *p;
  char *q;

  /* MPD outputs lowercase plugin (audio_output:type) so convert to
   * lowercase, convert spaces to underscores to make it a single word */
  for (p = spk->output_type, q = plugin; *p != '\0'; p++, q++)
    {
      *q = tolower(*p);
      if (*q == ' ')
      	*q = '_';
    }
  *q = '\0';

  evbuffer_add_printf(param->evbuf,
		      "outputid: %u\n"
		      "outputname: %s\n"
		      "plugin: %s\n"
		      "outputenabled: %d\n",
		      spk->index,
		      spk->name,
		      plugin,
		      spk->selected);

  param->last_shortid = spk->index;
}

/*
 * Command handler function for 'output'
 * Returns a lists with the avaiable speakers.
 */
static int
mpd_command_outputs(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  struct param_output param = { .evbuf = out->evbuf, .last_shortid = -1 };

  /* Reference:
   * https://mpd.readthedocs.io/en/latest/protocol.html#audio-output-devices
   * the ID returned by mpd may change between excutions (!!), so what we do
   * is simply enumerate the speakers with the speaker index */
  player_speaker_enumerate(speaker_enum_cb, &param);

  /* streaming output is not in the speaker list, so add it as pseudo
   * element when configured to do so */
  if (mpd_plugin_httpd)
    {
      mpd_plugin_httpd_shortid = param.last_shortid + 1;
      evbuffer_add_printf(param.evbuf,
                          "outputid: %u\n"
                          "outputname: MP3 stream\n"
                          "plugin: httpd\n"
                          "outputenabled: 1\n",
                          mpd_plugin_httpd_shortid);
    }

  return 0;
}

static int
outputvolume_set(uint32_t shortid, int volume)
{
  struct player_speaker_info spk;
  int ret;

  ret = player_speaker_get_byindex(&spk, shortid);
  if (ret < 0)
    return -1;

  return player_volume_setabs_speaker(spk.id, volume);
}

static int
mpd_command_outputvolume(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  uint32_t shortid = in->argv_u32val[1];
  int32_t volume = in->argv_i32val[2];
  int ret;

  ret = outputvolume_set(shortid, volume);
  if (ret < 0)
    RETURN_ERROR(ACK_ERROR_UNKNOWN, "Could not set volume for speaker with id: %d", shortid);

  return 0;
}

static void
channel_outputvolume(const char *message)
{
  uint32_t shortid;
  int volume;
  char *tmp;
  char *ptr;
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

  ret = outputvolume_set(shortid, volume);
  if (ret < 0)
    DPRINTF(E_LOG, L_MPD, "Failed to set output volume from message: '%s'\n", message);

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
    /* channel               | handler function */
    { "outputvolume",          channel_outputvolume },
    { "pairing",               channel_pairing },
    { "verification",          channel_verification },
    { NULL, NULL },
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
mpd_command_channels(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int i;

  for (i = 0; mpd_channels[i].handler; i++)
    {
      evbuffer_add_printf(out->evbuf,
	  "channel: %s\n",
	  mpd_channels[i].channel);
    }

  return 0;
}

static int
mpd_command_sendmessage(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  const char *channelname;
  const char *message;
  struct mpd_channel *channel;

  channelname = in->argv[1];
  message = in->argv[2];

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
 * Dummy function to handle commands that are not supported and should
 * not raise an error.
 */
static int
mpd_command_ignore(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  //do nothing
  DPRINTF(E_DBG, L_MPD, "Ignore command %s\n", in->argv[0]);
  return 0;
}

static int
mpd_command_commands(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int i;

  for (i = 0; mpd_handlers[i].handler; i++)
    {
      evbuffer_add_printf(out->evbuf,
	  "command: %s\n",
	  mpd_handlers[i].name);
    }

  return 0;
}

static void
tagtypes_enum(struct mpd_tag_map *tag, void *arg)
{
  struct evbuffer *evbuf = arg;

  if (tag->type != MPD_TYPE_SPECIAL)
    evbuffer_add_printf(evbuf, "tagtype: %s\n", tag->name);
}

/*
 * Command handler function for 'tagtypes'
 * Returns a lists with supported tags in the form:
 *   tagtype: Artist
 */
static int
mpd_command_tagtypes(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  mpd_parser_enum_tagtypes(tagtypes_enum, out->evbuf);
  return 0;
}

/*
 * Command handler function for 'urlhandlers'
 * Returns a lists with supported tags in the form:
 *   handler: protocol://
 */
static int
mpd_command_urlhandlers(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  evbuffer_add_printf(out->evbuf,
      "handler: http://\n"
      // handlers supported by MPD 0.19.12
      // "handler: https://\n"
      // "handler: mms://\n"
      // "handler: mmsh://\n"
      // "handler: mmst://\n"
      // "handler: mmsu://\n"
      // "handler: gopher://\n"
      // "handler: rtp://\n"
      // "handler: rtsp://\n"
      // "handler: rtmp://\n"
      // "handler: rtmpt://\n"
      // "handler: rtmps://\n"
      // "handler: smb://\n"
      // "handler: nfs://\n"
      // "handler: cdda://\n"
      // "handler: alsa://\n"
      );

  return 0;
}

/*
 * Command handler function for 'decoders'
 * MPD returns the decoder plugins with their supported suffix and mime types.
 *
 * The server only uses libav/ffmepg for decoding and does not support decoder plugins,
 * therefor the function reports only ffmpeg as available.
 */
static int
mpd_command_decoders(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  int i;

  evbuffer_add_printf(out->evbuf, "plugin: ffmpeg\n");

  for (i = 0; ffmpeg_suffixes[i]; i++)
    {
      evbuffer_add_printf(out->evbuf, "suffix: %s\n", ffmpeg_suffixes[i]);
    }

  for (i = 0; ffmpeg_mime_types[i]; i++)
    {
      evbuffer_add_printf(out->evbuf, "mime_type: %s\n", ffmpeg_mime_types[i]);
    }

  return 0;
}

static int
mpd_command_command_list_begin(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  ctx->cmd_list_type = COMMAND_LIST_BEGIN;
  return 0;
}

static int
mpd_command_command_list_ok_begin(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  ctx->cmd_list_type = COMMAND_LIST_OK_BEGIN;
  return 0;
}

static int
mpd_command_command_list_end(struct mpd_command_output *out, struct mpd_command_input *in, struct mpd_client_ctx *ctx)
{
  if (ctx->cmd_list_type == COMMAND_LIST_BEGIN)
    ctx->cmd_list_type = COMMAND_LIST_END;
  else if (ctx->cmd_list_type == COMMAND_LIST_OK_BEGIN)
    ctx->cmd_list_type = COMMAND_LIST_OK_END;
  else
    RETURN_ERROR(ACK_ERROR_ARG, "Got with command list end without preceeding list start");

  return 0;
}

static struct mpd_command mpd_handlers[] =
  {
    /* commandname                | handler function                      | min arg count  | handler requires int args */

    // Commands for querying status
    { "clearerror",                 mpd_command_ignore,                     -1 },
    { "currentsong",                mpd_command_currentsong,                -1 },
    { "idle",                       mpd_command_idle,                       -1 },
    { "noidle",                     mpd_command_noidle,                     -1 },
    { "status",                     mpd_command_status,                     -1 },
    { "stats",                      mpd_command_stats,                      -1 },

    // Playback options
    { "consume",                    mpd_command_consume,                     2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "crossfade",                  mpd_command_ignore,                     -1 },
    { "mixrampdb",                  mpd_command_ignore,                     -1 },
    { "mixrampdelay",               mpd_command_ignore,                     -1 },
    { "random",                     mpd_command_random,                      2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "repeat",                     mpd_command_repeat,                      2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "setvol",                     mpd_command_setvol,                      2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "getvol",                     mpd_command_getvol,                     -1 },
    { "single",                     mpd_command_single,                      2 },
    { "replay_gain_mode",           mpd_command_ignore,                     -1 },
    { "replay_gain_status",         mpd_command_replay_gain_status,         -1 },
    { "volume",                     mpd_command_volume,                      2,              MPD_WANTS_NUM_ARG1_IVAL },

    // Controlling playback
    { "next",                       mpd_command_next,                       -1 },
    { "pause",                      mpd_command_pause,                       1,              MPD_WANTS_NUM_ARG1_UVAL },
    { "play",                       mpd_command_play,                        1,              MPD_WANTS_NUM_ARG1_UVAL },
    { "playid",                     mpd_command_playid,                      1,              MPD_WANTS_NUM_ARG1_UVAL },
    { "previous",                   mpd_command_previous,                   -1 },
    { "seek",                       mpd_command_seek,                        3,              MPD_WANTS_NUM_ARG1_UVAL },
    { "seekid",                     mpd_command_seekid,                      3,              MPD_WANTS_NUM_ARG1_UVAL },
    { "seekcur",                    mpd_command_seekcur,                     2 },
    { "stop",                       mpd_command_stop,                       -1 },

    // The current playlist
    { "add",                        mpd_command_add,                         2 },
    { "addid",                      mpd_command_addid,                       2 },
    { "clear",                      mpd_command_clear,                      -1 },
    { "delete",                     mpd_command_delete,                     -1 },
    { "deleteid",                   mpd_command_deleteid,                    2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "move",                       mpd_command_move,                        3 },
    { "moveid",                     mpd_command_moveid,                      3,              MPD_WANTS_NUM_ARG1_UVAL },
    { "playlist",                   mpd_command_playlistinfo,               -1 }, // According to the mpd protocol the use of "playlist" is deprecated
    { "playlistfind",               mpd_command_playlistfind,                2 },
    { "playlistid",                 mpd_command_playlistid,                  1,              MPD_WANTS_NUM_ARG1_UVAL },
    { "playlistinfo",               mpd_command_playlistinfo,               -1 },
    { "playlistsearch",             mpd_command_playlistsearch,              2 },
    { "plchanges",                  mpd_command_plchanges,                   2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "plchangesposid",             mpd_command_plchangesposid,              2,              MPD_WANTS_NUM_ARG1_UVAL },
//    { "prio",                       mpd_command_prio,                       -1 },
//    { "prioid",                     mpd_command_prioid,                     -1 },
//    { "rangeid",                    mpd_command_rangeid,                    -1 },
//    { "shuffle",                    mpd_command_shuffle,                    -1 },
//    { "swap",                       mpd_command_swap,                       -1 },
//    { "swapid",                     mpd_command_swapid,                     -1 },
//    { "addtagid",                   mpd_command_addtagid,                   -1 },
//    { "cleartagid",                 mpd_command_cleartagid,                 -1 },

    // Stored playlists
    { "listplaylist",               mpd_command_listplaylist,                2 },
    { "listplaylistinfo",           mpd_command_listplaylistinfo,            2 },
    { "listplaylists",              mpd_command_listplaylists,              -1 },
    { "load",                       mpd_command_load,                        2 },
    { "playlistadd",                mpd_command_playlistadd,                 3 },
//    { "playlistclear",              mpd_command_playlistclear,              -1 },
//    { "playlistdelete",             mpd_command_playlistdelete,             -1 },
//    { "playlistmove",               mpd_command_playlistmove,               -1 },
//    { "rename",                     mpd_command_rename,                     -1 },
    { "rm",                         mpd_command_rm,                          2 },
    { "save",                       mpd_command_save,                        2 },

    // The music database
    { "albumart",                   mpd_command_albumart,                    2,              MPD_WANTS_NUM_ARG2_UVAL },
    { "count",                      mpd_command_count,                      -1 },
    { "find",                       mpd_command_find,                        2 },
    { "findadd",                    mpd_command_findadd,                     2 },
    { "search",                     mpd_command_find,                        2 },
    { "searchadd",                  mpd_command_findadd,                     2 },
    { "list",                       mpd_command_list,                        2 },
    { "listall",                    mpd_command_listall,                    -1 },
    { "listallinfo",                mpd_command_listallinfo,                -1 },
    { "listfiles",                  mpd_command_listfiles,                  -1 },
    { "lsinfo",                     mpd_command_lsinfo,                     -1 },
//    { "readcomments",               mpd_command_readcomments,               -1 },
    { "readpicture",                mpd_command_albumart,                    2,              MPD_WANTS_NUM_ARG2_UVAL },
//    { "searchaddpl",                mpd_command_searchaddpl,                -1 },
    { "update",                     mpd_command_update,                     -1 },
//    { "rescan",                     mpd_command_rescan,                     -1 },

    // Mounts and neighbors
//    { "mount",                      mpd_command_mount,                      -1 },
//    { "unmount",                    mpd_command_unmount,                    -1 },
//    { "listmounts",                 mpd_command_listmounts,                 -1 },
//    { "listneighbors",              mpd_command_listneighbors,              -1 },

    // Stickers
    { "sticker",                    mpd_command_sticker,                     4 },

    // Connection settings
    { "close",                      mpd_command_close,                      -1 },
//    { "kill",                       mpd_command_kill,                       -1 },
    { "password",                   mpd_command_password,                   -1 },
    { "ping",                       mpd_command_ignore,                     -1 },
    { "binarylimit",                mpd_command_binarylimit,                 2,              MPD_WANTS_NUM_ARG1_UVAL },

    // Audio output devices
    { "disableoutput",              mpd_command_xoutput,                     2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "enableoutput",               mpd_command_xoutput,                     2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "toggleoutput",               mpd_command_xoutput,                     2,              MPD_WANTS_NUM_ARG1_UVAL },
    { "outputs",                    mpd_command_outputs,                    -1 },

    // Custom command outputvolume (not supported by mpd)
    { "outputvolume",               mpd_command_outputvolume,                3,              MPD_WANTS_NUM_ARG1_UVAL | MPD_WANTS_NUM_ARG2_IVAL },

    // Client to client
    { "subscribe",                  mpd_command_ignore,                     -1 },
    { "unsubscribe",                mpd_command_ignore,                     -1 },
    { "channels",                   mpd_command_channels,                   -1 },
    { "readmessages",               mpd_command_ignore,                     -1 },
    { "sendmessage",                mpd_command_sendmessage,                3 },

    // Reflection
//    { "config",                     mpd_command_config,                     -1 },
    { "commands",                   mpd_command_commands,                   -1 },
    { "notcommands",                mpd_command_ignore,                     -1 },
    { "tagtypes",                   mpd_command_tagtypes,                   -1 },
    { "urlhandlers",                mpd_command_urlhandlers,                -1 },
    { "decoders",                   mpd_command_decoders,                   -1 },

    // Command lists
    { "command_list_begin",         mpd_command_command_list_begin,         -1 },
    { "command_list_ok_begin",      mpd_command_command_list_ok_begin,      -1 },
    { "command_list_end",           mpd_command_command_list_end,           -1 },

    // NULL command to terminate loop
    { NULL, NULL, -1 }
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
      if (0 == strcmp(name, mpd_handlers[i].name))
	{
	  return &mpd_handlers[i];
	}
    }

  return NULL;
}

static void
mpd_command_input_free(struct mpd_command_input *input)
{
  if (!input)
    return;

  free(input->args_split);
  free(input);
}

static int
mpd_command_input_create(struct mpd_command_input **out, const char *line)
{
  struct mpd_command_input *in;
  int ret;
  int i;

  CHECK_NULL(L_MPD, in = calloc(1, sizeof(struct mpd_command_input)));

  in->args_raw = line;

  ret = mpd_split_args(in->argv, sizeof(in->argv), &in->argc, &in->args_split, line);
  if (ret < 0)
    goto error;

  // Many of the handlers need numeric input. If you change this, then also
  // review command_has_num().
  for (i = MPD_WANTS_NUM_ARGV_MIN; i < in->argc && i <= MPD_WANTS_NUM_ARGV_MAX; i++)
    {
      if (!isdigit(in->argv[i][0]) && in->argv[i][0] != '-')
	continue; // Save some cycles if clearly not a number
      if (safe_atoi32(in->argv[i], &in->argv_i32val[i]) == 0)
	in->has_num |= 1 << i;
      if (safe_atou32(in->argv[i], &in->argv_u32val[i]) == 0)
	in->has_num |= 1 << (i + MPD_WANTS_NUM_ARGV_MAX);
    }
 
  *out = in;
  return 0;

 error:
  mpd_command_input_free(in);
  return -1;
}

// Check if input has the numeric arguments required for the command, taking
// into account that some commands have optional numeric args (purpose of mask)
static bool
command_has_num(int wants_num, int has_num, int argc)
{
  int ival_mask = (1 << argc) - 1; // If argc == 2 becomes ...00000011
  int uval_mask = (ival_mask << MPD_WANTS_NUM_ARGV_MAX); // If ..MAX == 3 becomes 00011000
  int mask = (ival_mask | uval_mask); // becomes 00011011

  return (wants_num & mask) == (has_num & wants_num & mask);
}

static bool
mpd_must_process_command_now(const char *line, struct mpd_client_ctx *client_ctx)
{
  size_t line_len = strlen(line);

  // We're in command list mode, just add command to buffer and return
  if ((client_ctx->cmd_list_type == COMMAND_LIST_BEGIN || client_ctx->cmd_list_type == COMMAND_LIST_OK_BEGIN) && strcmp(line, "command_list_end") != 0)
    {
      if (evbuffer_get_length(client_ctx->cmd_list_buffer) + line_len + 1 > MPD_MAX_COMMAND_LIST_SIZE)
	{
	  DPRINTF(E_LOG, L_MPD, "Max command list size (%uKB) exceeded\n", (MPD_MAX_COMMAND_LIST_SIZE / 1024));
	  client_ctx->must_disconnect = true;
	  return false;
	}

      evbuffer_add(client_ctx->cmd_list_buffer, line, line_len + 1);
      return false;
    }

  if (strcmp(line, "noidle") == 0 && !client_ctx->is_idle)
    {
      return false; // Just ignore, don't proceed to send an OK
    }

  return true;
}

static enum mpd_ack_error
mpd_process_command_line(struct evbuffer *evbuf, const char *line, int cmd_num, struct mpd_client_ctx *client_ctx)
{
  struct mpd_command_input *in = NULL;
  struct mpd_command_output out = { .evbuf = evbuf, .ack_error = ACK_ERROR_NONE };
  struct mpd_command *command;
  const char *cmd_name = NULL;
  int ret;

  if (!mpd_must_process_command_now(line, client_ctx))
    {
      return ACK_ERROR_NONE;
    }

  ret = mpd_command_input_create(&in, line);
  if (ret < 0)
    {
      client_ctx->must_disconnect = true; // This is what MPD does
      out.errmsg = safe_asprintf("Could not read command: '%s'", line);
      out.ack_error = ACK_ERROR_ARG;
      goto error;
    }

  cmd_name = in->argv[0];

  if (strcmp(cmd_name, "noidle") != 0 && client_ctx->is_idle)
    {
      client_ctx->must_disconnect = true;
      out.errmsg = safe_asprintf("Only 'noidle' is allowed during idle");
      out.ack_error = ACK_ERROR_ARG;
      goto error;
    }

  if (strcmp(cmd_name, "password") != 0 && !client_ctx->authenticated)
    {
      out.errmsg = safe_asprintf("Not authenticated");
      out.ack_error = ACK_ERROR_PERMISSION;
      goto error;
    }

  command = mpd_find_command(cmd_name);
  if (!command)
    {
      out.errmsg = safe_asprintf("Unknown command");
      out.ack_error = ACK_ERROR_UNKNOWN;
      goto error;
    }
  else if (command->min_argc > in->argc)
    {
      out.errmsg = safe_asprintf("Missing argument(s), expected %d, given %d", command->min_argc - 1, in->argc - 1);
      out.ack_error = ACK_ERROR_ARG;
      goto error;
    }
  else if (!command_has_num(command->wants_num, in->has_num, in->argc))
    {
      out.errmsg = safe_asprintf("Missing or invalid numeric values in command: '%s'", line);
      out.ack_error = ACK_ERROR_ARG;
      goto error;
    }

  ret = command->handler(&out, in, client_ctx);
  if (ret < 0)
    {
      goto error;
    }

  if (client_ctx->cmd_list_type == COMMAND_LIST_NONE && !client_ctx->is_idle)
    evbuffer_add_printf(out.evbuf, "OK\n");

  mpd_command_input_free(in);

  return out.ack_error;

 error:
  DPRINTF(E_LOG, L_MPD, "Error processing command '%s': %s\n", line, out.errmsg);

  if (cmd_name)
    evbuffer_add_printf(out.evbuf, "ACK [%d@%d] {%s} %s\n", out.ack_error, cmd_num, cmd_name, out.errmsg);

  mpd_command_input_free(in);
  free(out.errmsg);

  return out.ack_error;
}

// Process the commands that were added to client_ctx->cmd_list_buffer
// From MPD documentation (https://mpd.readthedocs.io/en/latest/protocol.html#command-lists):
// It does not execute any commands until the list has ended. The response is
// a concatenation of all individual responses.
// If a command fails, no more commands are executed and the appropriate ACK
// error is returned. If command_list_ok_begin is used, list_OK is returned
// for each successful command executed in the command list.
// On success for all commands, OK is returned.
static void
mpd_process_command_list(struct evbuffer *evbuf, struct mpd_client_ctx *client_ctx)
{
  char *line;
  enum mpd_ack_error ack_error = ACK_ERROR_NONE;
  int cmd_num = 0;

  while ((line = evbuffer_readln(client_ctx->cmd_list_buffer, NULL, EVBUFFER_EOL_NUL)))
    {
      ack_error = mpd_process_command_line(evbuf, line, cmd_num, client_ctx);

      cmd_num++;

      free(line);

      if (ack_error != ACK_ERROR_NONE)
	break;

      if (client_ctx->cmd_list_type == COMMAND_LIST_OK_END)
	evbuffer_add_printf(evbuf, "list_OK\n");
    }

  if (ack_error == ACK_ERROR_NONE)
    evbuffer_add_printf(evbuf, "OK\n");

  // Back to single-command mode
  evbuffer_drain(client_ctx->cmd_list_buffer, -1);
  client_ctx->cmd_list_type = COMMAND_LIST_NONE;
}

/* --------------------------- Server implementation ------------------------ */

/*
 * The read callback function is invoked if a complete command sequence was
 * received from the client (see mpd_input_filter function).
 *
 * @param bev the buffer event
 * @param ctx used for authentication
 */
static void
mpd_read_cb(struct bufferevent *bev, void *arg)
{
  struct mpd_client_ctx *client_ctx = arg;
  struct evbuffer *input;
  struct evbuffer *output;
  char *line;

  // Contains the command sequence received from the client
  input = bufferevent_get_input(bev);
  // Used to send the server response to the client
  output = bufferevent_get_output(bev);

  while ((line = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY)))
    {
      DPRINTF(E_DBG, L_MPD, "MPD message: '%s'\n", line);

      mpd_process_command_line(output, line, 0, client_ctx);

      free(line);

      if (client_ctx->cmd_list_type == COMMAND_LIST_END || client_ctx->cmd_list_type == COMMAND_LIST_OK_END)
        mpd_process_command_list(output, client_ctx);

      if (client_ctx->must_disconnect)
        goto disconnect;
    }

  return;

 disconnect:
  DPRINTF(E_DBG, L_MPD, "Disconnecting client\n");

  // Freeing the bufferevent closes the connection, since it was opened with
  // BEV_OPT_CLOSE_ON_FREE. Bufferevents are internally reference-counted, so if
  // the bufferevent has pending deferred callbacks when you free it, it won’t
  // be deleted until the callbacks are done.
  bufferevent_setcb(bev, NULL, NULL, NULL, NULL);
  bufferevent_free(bev);
}

/*
 * Callback when an event occurs on the bufferevent
 */
static void
mpd_event_cb(struct bufferevent *bev, short events, void *ctx)
{
  if (events & BEV_EVENT_ERROR)
    {
      DPRINTF(E_LOG, L_MPD, "Error from bufferevent: %s\n",
	  evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }

  if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
      bufferevent_free(bev);
    }
}

/*
 * The input filter buffer callback.
 *
 * Pass complete lines.
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
  char *line;
  int ret;
  // Filter functions must return BEV_OK
  // if any data was successfully written to the destination buffer
  int output_count = 0;

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
      output_count += ret;
    }

  if (output_count == 0)
    {
      DPRINTF(E_DBG, L_MPD, "Message incomplete, waiting for more data\n");
      return BEV_NEED_MORE;
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
  struct mpd_client_ctx *client_ctx = client_ctx_add();

  bev = bufferevent_filter_new(bev, mpd_input_filter, NULL, BEV_OPT_CLOSE_ON_FREE, client_ctx_remove, client_ctx);
  bufferevent_setcb(bev, mpd_read_cb, NULL, mpd_event_cb, client_ctx);
  bufferevent_enable(bev, EV_READ | EV_WRITE);

  client_ctx->evbuffer = bufferevent_get_output(bev);

  client_ctx->authenticated = !cfg_getstr(cfg_getsec(cfg, "library"), "password");
  if (!client_ctx->authenticated)
    {
      client_ctx->authenticated = net_peer_address_is_trusted((union net_sockaddr *)address);
    }

  client_ctx->binarylimit = MPD_BINARY_SIZE;

  // No zero terminator (newline in the string is terminator)
  evbuffer_add(client_ctx->evbuffer, MPD_PROTOCOL_VERSION_OK, strlen(MPD_PROTOCOL_VERSION_OK));

  DPRINTF(E_INFO, L_MPD, "New mpd client connection accepted\n");
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

static enum command_state
mpd_notify_idle(void *arg, int *retval)
{
  short event_mask;
  struct mpd_client_ctx *client;
  int i;

  event_mask = *(short *)arg;
  DPRINTF(E_DBG, L_MPD, "Notify clients waiting for idle results: %d\n", event_mask);

  i = 0;
  client = mpd_clients;
  while (client)
    {
      DPRINTF(E_DBG, L_MPD, "Notify client #%d\n", i);

      notify_idle_client(client, event_mask, true);

      client = client->next;
      i++;
    }

  *retval = 0;
  return COMMAND_END;
}

static void
mpd_listener_cb(short event_mask, void *ctx)
{
  short *ptr;

  ptr = (short *)malloc(sizeof(short));
  *ptr = event_mask;
  DPRINTF(E_DBG, L_MPD, "Asynchronous listener callback called with event type %d.\n", event_mask);
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
 * file through the default artwork logic.
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
      evhttp_send_error(req, HTTP_BADMETHOD, "Method not allowed");
      return;
    }

  uri = evhttp_request_get_uri(req);
  DPRINTF(E_DBG, L_MPD, "Got artwork request with uri '%s'\n", uri);

  decoded = evhttp_uri_parse(uri);
  if (!decoded)
    {
      DPRINTF(E_LOG, L_MPD, "Bad artwork request with uri '%s'\n", uri);
      evhttp_send_error(req, HTTP_BADREQUEST, 0);
      return;
    }

  path = evhttp_uri_get_path(decoded);
  if (!path)
    {
      DPRINTF(E_LOG, L_MPD, "Invalid path from artwork request with uri '%s'\n", uri);
      evhttp_send_error(req, HTTP_BADREQUEST, 0);
      evhttp_uri_free(decoded);
      return;
    }

  decoded_path = evhttp_uridecode(path, 0, NULL);
  if (!decoded_path)
    {
      DPRINTF(E_LOG, L_MPD, "Error decoding path from artwork request with uri '%s'\n", uri);
      evhttp_send_error(req, HTTP_BADREQUEST, 0);
      evhttp_uri_free(decoded);
      return;
    }

  last_slash = strrchr(decoded_path, '/');
  if (last_slash)
    *last_slash = '\0';

  DPRINTF(E_DBG, L_MPD, "Artwork request for path: %s\n", decoded_path);

  itemid = db_file_id_byvirtualpath_match(decoded_path);
  if (!itemid)
    {
      DPRINTF(E_WARN, L_MPD, "No item found for path '%s' from request uri '%s'\n", decoded_path, uri);
      evhttp_send_error(req, HTTP_NOTFOUND, "Document was not found");
      evhttp_uri_free(decoded);
      free(decoded_path);
      return;
    }

  evbuffer = evbuffer_new();
  if (!evbuffer)
    {
      DPRINTF(E_LOG, L_MPD, "Could not allocate an evbuffer for artwork request\n");
      evhttp_send_error(req, HTTP_INTERNAL, "Document was not found");
      evhttp_uri_free(decoded);
      free(decoded_path);
      return;
    }

  format = artwork_get_item(evbuffer, itemid, ART_DEFAULT_WIDTH, ART_DEFAULT_HEIGHT, 0);
  if (format < 0)
    {
      evhttp_send_error(req, HTTP_NOTFOUND, "Document was not found");
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

      evhttp_send_reply(req, HTTP_OK, "OK", evbuffer);
    }

  evbuffer_free(evbuffer);
  evhttp_uri_free(decoded);
  free(decoded_path);
}


/* -------------------------------- Event loop ------------------------------ */

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


/* -------------------------------- Init/deinit ----------------------------- */

/* Thread: main */
static int
mpd_httpd_init(void)
{
  unsigned short http_port;
  int ret;

  http_port = cfg_getint(cfg_getsec(cfg, "mpd"), "http_port");
  if (http_port == 0)
    return 0;

  evhttpd = evhttp_new(evbase_mpd);
  if (!evhttpd)
    return -1;

  ret = net_evhttp_bind(evhttpd, http_port, "mpd artwork");
  if (ret < 0)
    {
      evhttp_free(evhttpd);
      evhttpd = NULL;
      return -1;
    }

  evhttp_set_gencb(evhttpd, artwork_cb, NULL);

  return 0;
}

/* Thread: main */
static void
mpd_httpd_deinit(void)
{
  if (evhttpd)
    evhttp_free(evhttpd);

  evhttpd = NULL;
}

/* Thread: main */
int
mpd_init(void)
{
  unsigned short port;
  const char *pl_dir;
  int ret;

  port = cfg_getint(cfg_getsec(cfg, "mpd"), "port");
  if (port <= 0)
    {
      DPRINTF(E_INFO, L_MPD, "MPD not enabled\n");
      return 0;
    }

  CHECK_NULL(L_MPD, evbase_mpd = event_base_new());
  CHECK_NULL(L_MPD, cmdbase = commands_base_new(evbase_mpd, NULL));

  mpd_sockfd = net_bind(&port, SOCK_STREAM, "mpd");
  if (mpd_sockfd < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not bind mpd server to port %hu\n", port);
      goto bind_fail;
    }

  mpd_listener = evconnlistener_new(evbase_mpd, mpd_accept_conn_cb, NULL, 0, -1, mpd_sockfd);
  if (!mpd_listener)
    {
      DPRINTF(E_LOG, L_MPD, "Could not create connection listener for mpd clients on port %d\n", port);
      goto connew_fail;
    }
  evconnlistener_set_error_cb(mpd_listener, mpd_accept_error_cb);

  ret = mpd_httpd_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not initialize HTTP artwork server\n");
      goto httpd_fail;
    }

  allow_modifying_stored_playlists = cfg_getbool(cfg_getsec(cfg, "library"), "allow_modifying_stored_playlists");
  pl_dir = cfg_getstr(cfg_getsec(cfg, "library"), "default_playlist_directory");
  if (pl_dir)
    default_pl_dir = safe_asprintf("/file:%s", pl_dir);

  mpd_plugin_httpd = cfg_getbool(cfg_getsec(cfg, "mpd"), "enable_httpd_plugin");

  /* Handle deprecated config options */
  if (0 < cfg_opt_size(cfg_getopt(cfg_getsec(cfg, "mpd"), "allow_modifying_stored_playlists")))
    {
      DPRINTF(E_LOG, L_MPD, "Found deprecated option 'allow_modifying_stored_playlists' in section 'mpd', please update configuration file (move option to section 'library').\n");
      allow_modifying_stored_playlists = cfg_getbool(cfg_getsec(cfg, "mpd"), "allow_modifying_stored_playlists");
    }
  if (0 < cfg_opt_size(cfg_getopt(cfg_getsec(cfg, "mpd"), "default_playlist_directory")))
    {
      DPRINTF(E_LOG, L_MPD, "Found deprecated option 'default_playlist_directory' in section 'mpd', please update configuration file (move option to section 'library').\n");
      free(default_pl_dir);
      pl_dir = cfg_getstr(cfg_getsec(cfg, "mpd"), "default_playlist_directory");
      if (pl_dir)
        default_pl_dir = safe_asprintf("/file:%s", pl_dir);
    }

  DPRINTF(E_INFO, L_MPD, "mpd thread init\n");

  ret = pthread_create(&tid_mpd, NULL, mpd, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not spawn MPD thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  thread_setname(tid_mpd, "mpd");

  mpd_clients = NULL;
  listener_add(mpd_listener_cb, MPD_ALL_IDLE_LISTENER_EVENTS, NULL);

  return 0;

 thread_fail:
  mpd_httpd_deinit();
 httpd_fail:
  evconnlistener_free(mpd_listener);
 connew_fail:
  close(mpd_sockfd);
 bind_fail:
  commands_base_free(cmdbase);
  event_base_free(evbase_mpd);
  evbase_mpd = NULL;

  return -1;
}

/* Thread: main */
void
mpd_deinit(void)
{
  unsigned short port;
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
      DPRINTF(E_FATAL, L_MPD, "Could not join MPD thread: %s\n", strerror(errno));
      return;
    }

  listener_remove(mpd_listener_cb);

  while (mpd_clients)
    {
      client_ctx_remove(mpd_clients);
    }

  mpd_httpd_deinit();

  evconnlistener_free(mpd_listener);

  close(mpd_sockfd);

  // Free event base (should free events too)
  event_base_free(evbase_mpd);

  free(default_pl_dir);
}
