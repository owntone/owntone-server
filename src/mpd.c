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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>

#if defined(HAVE_SYS_EVENTFD_H) && defined(HAVE_EVENTFD)
# define USE_EVENTFD
# include <sys/eventfd.h>
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <netinet/in.h>
#endif

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "mpd.h"

#include "player.h"
#include "filescanner.h"


static pthread_t tid_mpd;

struct event_base *evbase_mpd;
static int g_exit_pipe[2];
static struct event *g_exitev;

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

static void
thread_exit(void)
{
  int dummy = 42;

  DPRINTF(E_DBG, L_MPD, "Killing mpd thread\n");

  if (write(g_exit_pipe[1], &dummy, sizeof(dummy)) != sizeof(dummy))
    DPRINTF(E_LOG, L_MPD, "Could not write to exit fd: %s\n", strerror(errno));
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
exit_cb(int fd, short what, void *arg)
{
  int dummy;
  int ret;

  ret = read(g_exit_pipe[0], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_MPD, "Error reading from exit pipe\n");

  event_base_loopbreak(evbase_mpd);

  event_add(g_exitev, NULL);
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
 * @param mfi media information
 * @param pos_pl position in the playqueue, if -1 the position is ignored
 * @return the number of bytes added if successful, or -1 if an error occurred.
 */
static int
mpd_add_mediainfo(struct evbuffer *evbuf, struct media_file_info *mfi, int pos_pl)
{
  char modified[32];
  int ret;

  mpd_time(modified, sizeof(modified), mfi->time_modified);

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
    "Disc: %d\n",
    (mfi->virtual_path + 1),
    modified,
    (mfi->song_length / 1000),
    mfi->artist,
    mfi->album_artist,
    mfi->artist_sort,
    mfi->album_artist_sort,
    mfi->album,
    mfi->title,
    mfi->track,
    mfi->year,
    mfi->genre,
    mfi->disc);

  if (pos_pl >= 0)
    {
      ret = evbuffer_add_printf(evbuf,
	"Pos: %d\n",
	pos_pl);
    }

  ret = evbuffer_add_printf(evbuf,
    "Id: %d\n",
    mfi->id);

  return ret;
}

static int
mpd_add_mediainfo_byid(struct evbuffer *evbuf, int id, int pos_pl)
{
  struct media_file_info *mfi;
  int ret;

  mfi = db_file_fetch_byid(id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_MPD, "Error fetching file by id: %d\n", id);
      return -1;
    }

  ret = mpd_add_mediainfo(evbuf, mfi, pos_pl);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Error adding media info for file with id: %d\n", id);

      free_mfi(mfi, 0);

      return -1;
    }

  free_mfi(mfi, 0);
  return 0;
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
 *   Id: 1
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
      "Disc: %s\n"
      "Id: %s\n",
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
      dbmfi->disc,
      dbmfi->id);

  return ret;
}

/*
 * Command handler function for 'currentsong'
 */
static int
mpd_command_currentsong(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{

  struct player_status status;
  int ret;

  player_get_status(&status);

  if (status.status == PLAY_STOPPED)
    {
      // Return empty evbuffer if there is no current playing song
      return 0;
    }

  ret = mpd_add_mediainfo_byid(evbuf, status.id, status.pos_pl);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Error adding media info for file with id: %d", status.id);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");

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
  DPRINTF(E_WARN, L_MPD, "Idle command is not supported by forked-daapd, there will be no notifications about changes\n");
  return 0;
}

static int
mpd_command_noidle(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
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
  char *state;

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
      0 /* consume: not supported by forked-daapd, always return 'off' */,
      status.plid,
      status.playlistlength,
      state);

  if (status.status != PLAY_STOPPED)
    {
      evbuffer_add_printf(evbuf,
	  "song: %d\n"
	  "songid: %d\n"
	  "time: %d:%d\n"
	  "elapsed: %#.3f\n"
	  "bitrate: 128\n"
	  "audio: 44100:16:2\n"
	  "nextsong: %d\n"
	  "nextsongid: %d\n",
	  status.pos_pl,
	  status.id,
	  (status.pos_ms / 1000), (status.len_ms / 1000),
	  (status.pos_ms / 1000.0),
	  status.next_pos_pl,
	  status.next_id);
    }

  if (filescanner_scanning())
    {
      evbuffer_add(evbuf, "updating_db: 1\n", 15);
    }

  if (status.status != PLAY_STOPPED)
    {
      evbuffer_add_printf(evbuf,
	  "nextsong: %d\n"
	  "nextsongid: %d\n",
	  status.next_pos_pl,
	  status.next_id);
    }

  return 0;
}

/*
 * Command handler function for 'stats'
 */
static int
mpd_command_stats(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  //TODO implement command stats
  evbuffer_add_printf(evbuf,
      "artists: %d\n"
      "albums: %d\n"
      "songs: %d\n"
      "uptime: %d\n" //in seceonds
      "db_playtime: %d\n"
      "db_update: %d\n"
      "playtime: %d\n",
        1,
        2,
        3,
        4,
        5,
        6,
        7);

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
      ret = asprintf(errmsg, "Missing argument for command 'random'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &enable);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'repeat'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &enable);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'setvol'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &volume);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'single'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &enable);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'volume'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atoi32(argv[1], &volume);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Failed to skip to next song");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start(NULL);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Player returned an error for start after nextitem");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
	  ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
    ret = player_playback_start(NULL);

  if (ret != 0)
    {
      ret = asprintf(errmsg, "Failed to pause playback");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
  int ret;

  player_get_status(&status);

  //TODO verfiy handling of play with parameter if already playing
  if (status.status == PLAY_PLAYING)
    {
      ret = player_playback_pause();
      if (ret < 0)
      {
	DPRINTF(E_LOG, L_MPD, "Error pausing playback\n");
      }
    }

  songpos = 0;
  if (argc > 1)
    {
      ret = safe_atoi32(argv[1], &songpos);
      if (ret < 0)
	{
	  ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
	  return ACK_ERROR_ARG;
	}
    }

  if (songpos > 0)
    ret = player_playback_startpos(songpos, NULL);
  else
    ret = player_playback_start(NULL);

  if (ret != 0)
    {
      ret = asprintf(errmsg, "Failed to start playback");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
  int ret;

  player_get_status(&status);

  //TODO verfiy handling of play with parameter if already playing
  if (status.status == PLAY_PLAYING)
    {
      ret = player_playback_pause();
      if (ret < 0)
      {
	DPRINTF(E_LOG, L_MPD, "Error pausing playback\n");
      }
    }

  id = 0;
  if (argc > 1)
    {
      ret = safe_atou32(argv[1], &id);
      if (ret < 0)
	{
	  ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
	  return ACK_ERROR_ARG;
	}
    }

  if (id > 0)
    ret = player_playback_startid(id, NULL);
  else
    ret = player_playback_start(NULL);

  if (ret != 0)
    {
      ret = asprintf(errmsg, "Failed to start playback");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Failed to skip to previous song");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start(NULL);
    if (ret < 0)
      {
        ret = asprintf(errmsg, "Player returned an error for start after previtem");
	if (ret < 0)
	  DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'seekcur'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &songpos);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  //TODO Allow seeking in songs not currently playing
  if (songpos != 0)
    {
      ret = asprintf(errmsg, "Given song is not the current playing one, seeking is not supported");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  seek_target_sec = strtof(argv[2], NULL);
  seek_target_msec = seek_target_sec * 1000;

  ret = player_playback_seek(seek_target_msec);

  if (ret < 0)
    {
      ret = asprintf(errmsg, "Failed to seek current song to time %d msec", seek_target_msec);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start(NULL);
    if (ret < 0)
      {
        ret = asprintf(errmsg, "Player returned an error for start after seekcur");
	if (ret < 0)
	  DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'seekcur'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &id);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  //TODO Allow seeking in songs not currently playing
  player_get_status(&status);
  if (status.id != id)
    {
      ret = asprintf(errmsg, "Given song is not the current playing one, seeking is not supported");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  seek_target_sec = strtof(argv[2], NULL);
  seek_target_msec = seek_target_sec * 1000;

  ret = player_playback_seek(seek_target_msec);

  if (ret < 0)
    {
      ret = asprintf(errmsg, "Failed to seek current song to time %d msec", seek_target_msec);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start(NULL);
    if (ret < 0)
      {
        ret = asprintf(errmsg, "Player returned an error for start after seekcur");
	if (ret < 0)
	  DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'seekcur'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  seek_target_sec = strtof(argv[1], NULL);
  seek_target_msec = seek_target_sec * 1000;

  // TODO If prefixed by '+' or '-', then the time is relative to the current playing position.
  ret = player_playback_seek(seek_target_msec);

  if (ret < 0)
    {
      ret = asprintf(errmsg, "Failed to seek current song to time %d msec", seek_target_msec);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  ret = player_playback_start(NULL);
    if (ret < 0)
      {
        ret = asprintf(errmsg, "Player returned an error for start after seekcur");
	if (ret < 0)
	  DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Failed to stop playback");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  return 0;
}

/*
 * Command handler function for 'add'
 * Adds the all songs under the given path to the end of the playqueue (directories add recursively).
 * Expects argument argv[1] to be a path to a single file or directory.
 */
static int
mpd_command_add(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct player_source *ps;
  int ret;

  if (argc < 2)
    {
      ret = asprintf(errmsg, "Missing argument for command 'add'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ps = player_queue_make_mpd(argv[1], 1);

  if (!ps)
    {
      ret = asprintf(errmsg, "Failed to add song '%s' to playlist", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  player_queue_add(ps);

  ret = player_playback_start(NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not start playback\n");
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
  struct player_source *ps;
  int ret;

  if (argc < 2)
    {
      ret = asprintf(errmsg, "Missing argument for command 'addid'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  //TODO if argc > 2 add song at position argv[2]
  if (argc > 2)
    {
      DPRINTF(E_LOG, L_MPD, "Adding at a specified position not supported for 'addid', adding songs at end of queue.\n");
    }

  ps = player_queue_make_mpd(argv[1], 0);

  if (!ps)
    {
      ret = asprintf(errmsg, "Failed to add song '%s' to playlist", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }


  player_queue_add(ps);

  evbuffer_add_printf(evbuf,
      "addid: %s\n"
      "Id: %d\n",
      argv[1],
      ps->id);

  ret = player_playback_start(NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not start playback\n");
    }

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

  player_queue_clear();

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
  struct player_status status;
  uint32_t start_pos;
  int pos;
  int ret;

  // If argv[1] is ommited clear the whole queue except the current playing one
  if (argc < 2)
    {
      player_queue_empty(0);
      return 0;
    }

  // If argument argv[1] is present remove only the specified songs
  //TODO support ranges for argv[1]
  ret = safe_atou32(argv[1], &start_pos);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  player_get_status(&status);

  pos = start_pos - status.pos_pl;

  if (pos < 1)
    {
      ret = asprintf(errmsg, "Removing playing or previously played song not supported (song position %d)", pos);
      if (ret < 0)
      	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = player_queue_remove(pos);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Failed to remove song at position '%d'", pos);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'deleteid'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &songid);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = player_queue_removeid(songid);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Failed to remove song with id '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
  struct player_queue *queue;
  uint32_t songid;
  int pos_pl;
  int i;
  int ret;

  songid = 0;

  if (argc > 1)
    {
      ret = safe_atou32(argv[1], &songid);
      if (ret < 0)
	{
	  ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
	  return ACK_ERROR_ARG;
	}
    }

  // Get the whole queue (start_pos = 0, end_pos = -1)
  queue = player_queue_get(0, -1, 0);

  if (!queue)
    {
      // Queue is emtpy
      return 0;
    }

  pos_pl = queue->start_pos;
  for (i = 0; i < queue->count; i++)
    {
      if (songid == 0 || songid == queue->queue[i])
	{
	  ret = mpd_add_mediainfo_byid(evbuf, queue->queue[i], pos_pl);
	  if (ret < 0)
	    {
	      ret = asprintf(errmsg, "Error adding media info for file with id: %d", queue->queue[i]);

	      queue_free(queue);

	      if (ret < 0)
		DPRINTF(E_LOG, L_MPD, "Out of memory\n");
	      return ACK_ERROR_UNKNOWN;
	    }
	}

      pos_pl++;
    }

  queue_free(queue);

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
  struct player_queue *queue;
  int start_pos;
  int end_pos;
  int pos_pl;
  int i;
  int ret;

  start_pos = 0;
  end_pos = -1;

  if (argc > 1)
    {
      ret = mpd_pars_range_arg(argv[1], &start_pos, &end_pos);
      if (ret < 0)
	{
	  ret = asprintf(errmsg, "Argument doesn't convert to integer or range: '%s'", argv[1]);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
	  return ACK_ERROR_ARG;
	}
    }

  queue = player_queue_get(start_pos, end_pos, 0);

  if (!queue)
    {
      // Queue is emtpy
      return 0;
    }

  pos_pl = queue->start_pos;
  for (i = 0; i < queue->count; i++)
    {
      ret = mpd_add_mediainfo_byid(evbuf, queue->queue[i], pos_pl);
      if (ret < 0)
	{
	  ret = asprintf(errmsg, "Error adding media info for file with id: %d", queue->queue[i]);

	  queue_free(queue);

	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
	  return ACK_ERROR_UNKNOWN;
	}

      pos_pl++;
    }

  queue_free(queue);

  return 0;
}

static int
mpd_command_plchanges(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
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
      ret = asprintf(errmsg, "Missing argument for command 'load'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  if (strncmp(argv[1], "/", 1) == 0)
    {
      ret = snprintf(path, sizeof(path), "%s", argv[1]);
    }
  else
    {
      ret = snprintf(path, sizeof(path), "/%s", argv[1]);
    }

  pli = db_pl_fetch_byvirtualpath(path);
  if (!pli)
    {
      ret = asprintf(errmsg, "Playlist not found for path '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

      ret = asprintf(errmsg, "Could not start query");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'load'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  if (strncmp(argv[1], "/", 1) == 0)
    {
      ret = snprintf(path, sizeof(path), "%s", argv[1]);
    }
  else
    {
      ret = snprintf(path, sizeof(path), "/%s", argv[1]);
    }

  pli = db_pl_fetch_byvirtualpath(path);
  if (!pli)
    {
      ret = asprintf(errmsg, "Playlist not found for path '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

      ret = asprintf(errmsg, "Could not start query");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

      ret = asprintf(errmsg, "Could not start query");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  while (((ret = db_query_fetch_pl(&qp, &dbpli)) == 0) && (dbpli.id))
    {
      if (safe_atou32(dbpli.db_timestamp, &time_modified) != 0)
        {
          DPRINTF(E_LOG, L_MPD, "Error converting time modified to uint32_t: %s\n", dbpli.db_timestamp);
          return -1;
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
  struct player_source *ps;
  uint32_t pos;
  int ret;

  if (argc < 2)
    {
      ret = asprintf(errmsg, "Missing argument for command 'load'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  if (strncmp(argv[1], "/", 1) == 0)
    {
      ret = snprintf(path, sizeof(path), "%s", argv[1]);
    }
  else
    {
      ret = snprintf(path, sizeof(path), "/%s", argv[1]);
    }

  pli = db_pl_fetch_byvirtualpath(path);
  if (!pli)
    {
      ret = asprintf(errmsg, "Playlist not found for path '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  //TODO If a second parameter is given only add the specified range of songs to the playqueue

  ps = player_queue_make_pl(pli->id, &pos);

  if (!ps)
    {
      free_pli(pli, 0);

      ret = asprintf(errmsg, "Failed to add song '%s' to playlist", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  player_queue_add(ps);

  ret = player_playback_start(NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not start playback\n");
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
  struct count_info ci;
  int ret;

  if (argc < 3 || ((argc - 1) % 2) != 0)
    {
      ret = asprintf(errmsg, "Missing argument(s) for command 'find'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_COUNT_ITEMS;

  mpd_get_query_params_find(argc - 1, argv + 1, &qp);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);

      ret = asprintf(errmsg, "Could not start query");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  ret = db_query_fetch_count(&qp, &ci);
  if (ret < 0)
    {
      db_query_end(&qp);

      ret = asprintf(errmsg, "Could not fetch query count");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  evbuffer_add_printf(evbuf,
      "songs: %d\n"
      "playtime: %d\n",
        ci.count,
        (ci.length / 1000));

  db_query_end(&qp);

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
      ret = asprintf(errmsg, "Missing argument(s) for command 'find'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

      ret = asprintf(errmsg, "Could not start query");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

  return 0;
}

static int
mpd_command_findadd(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
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
	  ret = asprintf(errmsg, "Missing argument(s) for command 'list'");
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

      ret = asprintf(errmsg, "Could not start query");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_UNKNOWN;
    }

  if (qp.type == Q_BROWSE_YEARS)
    {
      while (((ret = db_query_fetch_string_sort(&qp, &browse_item, &sort_item)) == 0) && (browse_item))
	{
	  evbuffer_add_printf(evbuf,
		"%s%s\n",
		type,
		browse_item);
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

  return 0;
}

/*
 * Command handler function for 'lsinfo'
 * Lists the contents of the directory given in argv[1].
 */
static int
mpd_command_lsinfo(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  struct query_params qp;
  char parent[PATH_MAX];
  struct filelist_info *fi;
  struct media_file_info *mfi;
  char modified[32];
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
      ret = snprintf(parent, sizeof(parent), "/%s/", argv[1]);
    }

  if ((ret < 0) || (ret >= sizeof(parent)))
    {
      DPRINTF(E_INFO, L_MPD, "Parent path exceeds PATH_MAX\n");
      return -1;
    }

  fi = (struct filelist_info*)malloc(sizeof(struct filelist_info));
  if (!fi)
    {
      DPRINTF(E_LOG, L_MPD, "Out of memory for fi\n");
      return ACK_ERROR_UNKNOWN;
    }

  memset(&qp, 0, sizeof(struct query_params));

  ret = db_mpd_start_query_filelist(&qp, parent);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Could not start query for path '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");

      free_fi(fi, 0);
      return ACK_ERROR_UNKNOWN;
    }

  while (((ret = db_mpd_query_fetch_filelist(&qp, fi)) == 0) && (fi->virtual_path))
    {
      if (fi->type == F_DIR)
	{
	  mpd_time(modified, sizeof(modified), fi->time_modified);

	  evbuffer_add_printf(evbuf,
	    "directory: %s\n"
	    "Last-Modified: %s\n",
	    (fi->virtual_path + 1),
	    modified);
	}
      else if (fi->type == F_PLAYLIST)
	{
	  mpd_time(modified, sizeof(modified), fi->time_modified);

	  evbuffer_add_printf(evbuf,
	    "playlist: %s\n"
	    "Last-Modified: %s\n",
	    (fi->virtual_path + 1),
	    modified);
	}
      else if (fi->type == F_FILE)
	{
	  mfi = db_file_fetch_byvirtualpath(fi->virtual_path);
	  if (mfi)
	    {
	      ret = mpd_add_mediainfo(evbuf, mfi, -1);
	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_MPD, "Could not add mediainfo for path '%s'\n", fi->virtual_path);
		}

	      free_mfi(mfi, 0);
	    }
	}
    }

  db_query_end(&qp);

  if (fi)
    free_fi(fi, 0);

  return 0;
}

static int
mpd_get_query_params_search(int argc, char **argv, struct query_params *qp)
{
  char *c1;
  char *c2;
  int start_pos;
  int end_pos;
  int i;
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
      ret = asprintf(errmsg, "Missing argument(s) for command 'search'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

      ret = asprintf(errmsg, "Could not start query");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

  return 0;
}

/*
 * Command handler function for 'update'
 * Initiates an init-rescan (scans for new files)
 */
static int
mpd_command_update(struct evbuffer *evbuf, int argc, char **argv, char **errmsg)
{
  int ret;

  if (argc > 1 && strlen(argv[1]) > 0)
    {
      ret = asprintf(errmsg, "Update for specific uri not supported for command 'update'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  filescanner_trigger_initscan();

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
      ret = asprintf(errmsg, "Rescan for specific uri not supported for command 'rescan'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
outputs_enum_cb(uint64_t id, const char *name, int relvol, struct spk_flags flags, void *arg)
{
  struct outputs *outputs;
  struct output *output;

  outputs = (struct outputs *)arg;

  DPRINTF(E_DBG, L_MPD, "outputid: %" PRIu64 ", outputname: %s, outputenabled: %d\n",
      id, name, flags.selected);

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
      ret = asprintf(errmsg, "Missing argument for command 'disableoutput'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &num);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Speakers deactivation failed: %d", num);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'disableoutput'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &num);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Speakers activation failed: %d", num);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Missing argument for command 'disableoutput'");
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
      return ACK_ERROR_ARG;
    }

  ret = safe_atou32(argv[1], &num);
  if (ret < 0)
    {
      ret = asprintf(errmsg, "Argument doesn't convert to integer: '%s'", argv[1]);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
      ret = asprintf(errmsg, "Speakers de/activation failed: %d", num);
      if (ret < 0)
	DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
 */
static void
speaker_enum_cb(uint64_t id, const char *name, int relvol, struct spk_flags flags, void *arg)
{
  struct evbuffer *evbuf;

  evbuf = (struct evbuffer *)arg;

  evbuffer_add_printf(evbuf,
    "outputid: %d\n"
    "outputname: %s\n"
    "outputenabled: %d\n",
    (unsigned short) id,
    name,
    flags.selected);
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


struct command
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

static struct command mpd_handlers[] =
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
      .handler = mpd_command_ignore
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
    /*
    {
      .mpdcommand = "move",
      .handler = mpd_command_move
    },
    {
      .mpdcommand = "moveid",
      .handler = mpd_command_moveid
    },
    */
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
    /*
    {
      .mpdcommand = "plchangesposid",
      .handler = mpd_command_plchangesposid
    },
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
    /*
    {
      .mpdcommand = "playlistadd",
      .handler = mpd_command_playlistadd
    },
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
    {
      .mpdcommand = "rm",
      .handler = mpd_command_rm
    },
    {
      .mpdcommand = "save",
      .handler = mpd_command_save
    },
     */

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
    /*
    {
      .mpdcommand = "listall",
      .handler = mpd_command_listall
    },
    {
      .mpdcommand = "listallinfo",
      .handler = mpd_command_listallinfo
    },
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
    /*
    {
      .mpdcommand = "searchadd",
      .handler = mpd_command_searchadd
    },
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
    /*
    {
      .mpdcommand = "sticker",
      .handler = mpd_command_sticker
    },
     */

    /*
     * Connection settings
     */
    /*
    {
      .mpdcommand = "close",
      .handler = mpd_command_close
    },
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
    /*
    {
      .mpdcommand = "notcommands",
      .handler = mpd_command_notcommands
    },
    {
      .mpdcommand = "tagtypes",
      .handler = mpd_command_tagtypes
    },
    {
      .mpdcommand = "urlhandlers",
      .handler = mpd_command_urlhandlers
    },
    {
      .mpdcommand = "decoders",
      .handler = mpd_command_decoders
    },
     */

    /*
     * Client to client
     */
    /*
    {
      .mpdcommand = "subscribe",
      .handler = mpd_command_subscribe
    },
    {
      .mpdcommand = "unsubscribe",
      .handler = mpd_command_unsubscribe
    },
    {
      .mpdcommand = "channels",
      .handler = mpd_command_channels
    },
    {
      .mpdcommand = "readmessages",
      .handler = mpd_command_readmessages
    },
    {
      .mpdcommand = "sendmessage",
      .handler = mpd_command_sendmessage
    },
     */

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
static struct command*
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
  struct command *command;
  enum command_list_type listtype;
  int idle_cmd;
  char *argv[COMMAND_ARGV_MAX];
  int argc;

  /* Get the input evbuffer, contains the command sequence received from the client */
  input = bufferevent_get_input(bev);
  /* Get the output evbuffer, used to send the server response to the client */
  output = bufferevent_get_output(bev);

  DPRINTF(E_SPAM, L_MPD, "Received MPD command sequence\n");

  idle_cmd = 0;

  listtype = COMMAND_LIST_NONE;
  ncmd = 0;

  while ((line = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY)))
    {
      DPRINTF(E_DBG, L_MPD, "MPD message: %s\n", line);

      // Split the read line into command name and arguments
      ret = mpd_parse_args(line, &argc, argv);
      if (ret != 0)
	{
	  // Error handling for argument parsing error
	  DPRINTF(E_LOG, L_MPD, "Error parsing arguments for MPD message: %s\n", line);
	  ret = asprintf(&errmsg, "Error parsing arguments");
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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

      /*
       * Find the command handler and execute the command function
       */
      command = mpd_find_command(argv[0]);

      if (command == NULL)
	{
	  ret = asprintf(&errmsg, "Unsupported command '%s'", argv[0]);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MPD, "Out of memory\n");
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
  if (ret == 0 && idle_cmd == 0)
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
  if (events & BEV_EVENT_ERROR)
    DPRINTF(E_LOG, L_MPD, "Error from buffer event\n");

  if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
      bufferevent_free(bev);
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


/* Thread: main */
int mpd_init(void)
{
  struct evconnlistener *listener;
  struct sockaddr *saddr;
  size_t saddr_length;
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
  unsigned short port;
  int v6enabled;
  int ret;


  port = cfg_getint(cfg_getsec(cfg, "mpd"), "port");
  if (port <= 0)
    {
      DPRINTF(E_INFO, L_MPD, "MPD not enabled\n");
      return 0;
    }

  v6enabled = cfg_getbool(cfg_getsec(cfg, "general"), "ipv6");

# if defined(__linux__)
  ret = pipe2(g_exit_pipe, O_CLOEXEC);
# else
  ret = pipe(g_exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not create pipe: %s\n", strerror(errno));
      goto exit_fail;
    }

  evbase_mpd = event_base_new();
  if (!evbase_mpd)
    {
      DPRINTF(E_LOG, L_MPD, "Could not create an event base\n");
      goto evbase_fail;
    }

  g_exitev = event_new(evbase_mpd, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_MPD, "Could not create exit event\n");
      goto evnew_fail;
    }

  event_add(g_exitev, NULL);

  if (v6enabled)
    {
      saddr_length = sizeof(sin6);
      memset(&sin6, 0, saddr_length);
      sin6.sin6_family = AF_INET6;
      sin6.sin6_port = htons(port);
      saddr = (struct sockaddr *)&sin6;
    }
  else
    {
      saddr_length = sizeof(struct sockaddr_in);
      memset(&sin, 0, saddr_length);
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = htonl(0);
      sin.sin_port = htons(port);
      saddr = (struct sockaddr *)&sin;
    }

  listener = evconnlistener_new_bind(
      evbase_mpd,
      mpd_accept_conn_cb,
      NULL,
      LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
      -1,
      saddr,
      saddr_length);

  if (!listener)
    {
      DPRINTF(E_LOG, L_MPD, "Could not create connection listener for mpd clients on port %d\n", port);

      goto connew_fail;
    }
  evconnlistener_set_error_cb(listener, mpd_accept_error_cb);

  DPRINTF(E_INFO, L_MPD, "cache thread init\n");

  ret = pthread_create(&tid_mpd, NULL, mpd, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MPD, "Could not spawn cache thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;


 thread_fail:
 connew_fail:
 evnew_fail:
  event_base_free(evbase_mpd);
  evbase_mpd = NULL;

 evbase_fail:
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);

 exit_fail:
  return -1;
}

/* Thread: main */
void mpd_deinit(void)
{
  unsigned short port;
  int ret;

  port = cfg_getint(cfg_getsec(cfg, "mpd"), "port");
  if (port <= 0)
    {
      DPRINTF(E_INFO, L_MPD, "MPD not enabled\n");
      return;
    }

  thread_exit();

  ret = pthread_join(tid_mpd, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MPD, "Could not join cache thread: %s\n", strerror(errno));
      return;
    }

  // Free event base (should free events too)
  event_base_free(evbase_mpd);

  // Close pipes
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);
}
