/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
 *
 * Stiched together from libspotify examples
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
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <pthread.h>

#include <dlfcn.h>
#include <libspotify/api.h>

#include "spotify.h"
#include "logger.h"
#include "conffile.h"
#include "filescanner.h"


/* How long to wait for audio (in sec) before giving up */
#define SPOTIFY_TIMEOUT 20

/* --- Types --- */
typedef struct audio_fifo_data
{
  TAILQ_ENTRY(audio_fifo_data) link;
  int nsamples;
  int16_t samples[0];
} audio_fifo_data_t;

typedef struct audio_fifo
{
  TAILQ_HEAD(, audio_fifo_data) q;
  int qlen;
  int fullcount;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} audio_fifo_t;

enum spotify_state
{
  SPOTIFY_STATE_INACTIVE,
  SPOTIFY_STATE_WAIT,
  SPOTIFY_STATE_PLAYING,
  SPOTIFY_STATE_PAUSED,
  SPOTIFY_STATE_STOPPING,
  SPOTIFY_STATE_STOPPED,
};

struct audio_get_param
{
  struct evbuffer *evbuf;
  int wanted;
};

struct artwork_get_param
{
  struct evbuffer *evbuf;
  char *path;
  int max_w;
  int max_h;
};

struct spotify_command;

typedef int (*cmd_func)(struct spotify_command *cmd);

struct spotify_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;
  cmd_func func_bh;

  int nonblock;

  union {
    void *noarg;
    sp_link *link;
    int seek_ms;
    struct audio_get_param audio;
    struct artwork_get_param artwork;
  } arg;

  int ret;
};

/* --- Globals --- */
// Spotify thread
static pthread_t tid_spotify;

// Used to make sure no login is attempted before the logout cb from Spotify
static pthread_mutex_t login_lck;
static pthread_cond_t login_cond;

// Event base, pipes and events
struct event_base *evbase_spotify;
static int g_exit_pipe[2];
static int g_cmd_pipe[2];
static int g_notify_pipe[2];
static struct event *g_exitev;
static struct event *g_cmdev;
static struct event *g_notifyev;

// The global session handle
static sp_session *g_sess;
// The global library handle
static void *g_libhandle;
// The global state telling us what the thread is currently doing
static enum spotify_state g_state;
/* (not used) Tells which commmand is currently being processed */
static struct spotify_command *g_cmd;
// The global base playlist id (parent of all Spotify playlists in the db)
static int g_base_plid;

// Audio fifo
static audio_fifo_t *g_audio_fifo;

/**
 * The application key is specific to forked-daapd, and allows Spotify
 * to produce statistics on how their service is used.
 */
const uint8_t g_appkey[] = {
	0x01, 0xC6, 0x9D, 0x18, 0xA4, 0xF7, 0x79, 0x12, 0x43, 0x55, 0x0F, 0xAD, 0xBF, 0x23, 0x23, 0x10,
	0x2E, 0x51, 0x46, 0x8F, 0x06, 0x3D, 0xEE, 0xC3, 0xF0, 0x2A, 0x5D, 0x8E, 0x72, 0x35, 0xD1, 0x21,
	0x44, 0xE3, 0x19, 0x80, 0xED, 0xD5, 0xAD, 0xE6, 0xE1, 0xDD, 0xBE, 0xCB, 0xA9, 0x84, 0xBD, 0xC2,
	0xAF, 0xB1, 0xF2, 0xD5, 0x87, 0xFC, 0x35, 0xD6, 0x1C, 0x5F, 0x5B, 0x76, 0x38, 0x1D, 0x6E, 0x49,
	0x6D, 0x85, 0x15, 0xCD, 0x38, 0x14, 0xD6, 0xB8, 0xFE, 0x05, 0x0A, 0xAC, 0x9B, 0x31, 0xD1, 0xC0,
	0xAF, 0x16, 0x78, 0x48, 0x49, 0x27, 0x41, 0xCA, 0xAF, 0x07, 0xEC, 0x10, 0x5D, 0x19, 0x43, 0x2E,
	0x84, 0xEB, 0x43, 0x5D, 0x4B, 0xBF, 0xD0, 0x5C, 0xDF, 0x3D, 0x12, 0x6D, 0x1C, 0x76, 0x4E, 0x9F,
	0xBF, 0x14, 0xC9, 0x46, 0x95, 0x99, 0x32, 0x6A, 0xC2, 0xF1, 0x89, 0xA4, 0xB3, 0xF3, 0xA0, 0xEB,
	0xDA, 0x84, 0x67, 0x27, 0x07, 0x1F, 0xF6, 0x19, 0xAC, 0xF1, 0xB8, 0xB6, 0xCF, 0xAB, 0xF8, 0x0A,
	0xEE, 0x4D, 0xAC, 0xC2, 0x39, 0x63, 0x50, 0x13, 0x7B, 0x51, 0x3A, 0x50, 0xE0, 0x03, 0x6E, 0xB7,
	0x17, 0xEE, 0x58, 0xCE, 0xF8, 0x15, 0x3C, 0x70, 0xDE, 0xE6, 0xEB, 0xE6, 0xD4, 0x2C, 0x27, 0xB9,
	0xCA, 0x15, 0xCE, 0x2E, 0x31, 0x54, 0xF5, 0x0A, 0x98, 0x8D, 0x78, 0xE5, 0xB6, 0xF8, 0xE4, 0x62,
	0x43, 0xAA, 0x37, 0x93, 0xFF, 0xE3, 0xAB, 0x17, 0xC5, 0x81, 0x4F, 0xFD, 0xF1, 0x84, 0xE1, 0x8A,
	0x99, 0xB0, 0x1D, 0x85, 0x80, 0xA2, 0x49, 0x35, 0x8D, 0xDD, 0xBC, 0x74, 0x0B, 0xBA, 0x33, 0x5B,
	0xD5, 0x7A, 0xB9, 0x2F, 0x9B, 0x24, 0xA5, 0xAB, 0xF6, 0x1E, 0xE3, 0xA3, 0xA8, 0x0D, 0x1E, 0x48,
	0xF7, 0xDB, 0xE2, 0x54, 0x65, 0x43, 0xA6, 0xD3, 0x3F, 0x2C, 0x9B, 0x13, 0x9A, 0xBE, 0x0F, 0x4D,
	0x51, 0xC3, 0x73, 0xA5, 0xFE, 0xFC, 0x93, 0x12, 0xEF, 0x9C, 0x4D, 0x68, 0xE3, 0xDA, 0x52, 0x67,
	0x28, 0x41, 0x17, 0x22, 0x3E, 0x33, 0xB0, 0x3A, 0xFB, 0x44, 0xB0, 0x2E, 0xA6, 0xD2, 0x95, 0xC0,
	0x9A, 0xBA, 0x32, 0xA3, 0xC5, 0xFE, 0x86, 0x5D, 0xC8, 0xBB, 0xB5, 0xDE, 0x92, 0x8C, 0x7D, 0xE4,
	0x03, 0xD4, 0xF9, 0xAE, 0x41, 0xE3, 0xBD, 0x35, 0x4B, 0x94, 0x27, 0xE0, 0x12, 0x21, 0x46, 0xE9,
	0x09,
};

// This section defines and assigns function pointers to the libspotify functions
// The arguments and return values must be in sync with the spotify api
// Please scroll through the ugliness which follows

typedef const char*  (*fptr_sp_error_message_t)(sp_error error);

typedef sp_error     (*fptr_sp_session_create_t)(const sp_session_config *config, sp_session **sess);
typedef sp_error     (*fptr_sp_session_release_t)(sp_session *sess);
typedef sp_error     (*fptr_sp_session_login_t)(sp_session *session, const char *username, const char *password, bool remember_me, const char *blob);
typedef sp_error     (*fptr_sp_session_relogin_t)(sp_session *session);
typedef sp_error     (*fptr_sp_session_logout_t)(sp_session *session);
typedef sp_error     (*fptr_sp_session_process_events_t)(sp_session *session, int *next_timeout);
typedef sp_playlist* (*fptr_sp_session_starred_create_t)(sp_session *session);
typedef sp_playlistcontainer* (*fptr_sp_session_playlistcontainer_t)(sp_session *session);
typedef sp_error     (*fptr_sp_session_player_load_t)(sp_session *session, sp_track *track);
typedef sp_error     (*fptr_sp_session_player_unload_t)(sp_session *session);
typedef sp_error     (*fptr_sp_session_player_play_t)(sp_session *session, bool play);
typedef sp_error     (*fptr_sp_session_player_seek_t)(sp_session *session, int offset);
typedef sp_connectionstate (*fptr_sp_session_connectionstate_t)(sp_session *session);
typedef sp_error     (*fptr_sp_session_preferred_bitrate_t)(sp_session *session, sp_bitrate bitrate);

typedef sp_error     (*fptr_sp_playlistcontainer_add_callbacks_t)(sp_playlistcontainer *pc, sp_playlistcontainer_callbacks *callbacks, void *userdata);
typedef int          (*fptr_sp_playlistcontainer_num_playlists_t)(sp_playlistcontainer *pc);
typedef sp_playlist* (*fptr_sp_playlistcontainer_playlist_t)(sp_playlistcontainer *pc, int index);

typedef sp_error     (*fptr_sp_playlist_add_callbacks_t)(sp_playlist *playlist, sp_playlist_callbacks *callbacks, void *userdata);
typedef const char*  (*fptr_sp_playlist_name_t)(sp_playlist *playlist);
typedef sp_error     (*fptr_sp_playlist_remove_callbacks_t)(sp_playlist *playlist, sp_playlist_callbacks *callbacks, void *userdata);
typedef int          (*fptr_sp_playlist_num_tracks_t)(sp_playlist *playlist);
typedef sp_track*    (*fptr_sp_playlist_track_t)(sp_playlist *playlist, int index);
typedef bool         (*fptr_sp_playlist_is_loaded_t)(sp_playlist *playlist);

typedef sp_error     (*fptr_sp_track_error_t)(sp_track *track);
typedef bool         (*fptr_sp_track_is_loaded_t)(sp_track *track);
typedef const char*  (*fptr_sp_track_name_t)(sp_track *track);
typedef int          (*fptr_sp_track_duration_t)(sp_track *track);
typedef int          (*fptr_sp_track_index_t)(sp_track *track);
typedef int          (*fptr_sp_track_disc_t)(sp_track *track);
typedef sp_album*    (*fptr_sp_track_album_t)(sp_track *track);
typedef sp_track_availability (*fptr_sp_track_get_availability_t)(sp_session *session, sp_track *track);
typedef bool         (*fptr_sp_track_is_starred_t)(sp_session *session, sp_track *track);

typedef sp_link*     (*fptr_sp_link_create_from_playlist_t)(sp_playlist *playlist);
typedef sp_link*     (*fptr_sp_link_create_from_track_t)(sp_track *track, int offset);
typedef sp_link*     (*fptr_sp_link_create_from_string_t)(const char *link);
typedef int          (*fptr_sp_link_as_string_t)(sp_link *link, char *buffer, int buffer_size);
typedef sp_track*    (*fptr_sp_link_as_track_t)(sp_link *link);
typedef sp_error     (*fptr_sp_link_release_t)(sp_link *link);

typedef const char*  (*fptr_sp_album_name_t)(sp_album *album);
typedef sp_artist*   (*fptr_sp_album_artist_t)(sp_album *album);
typedef int          (*fptr_sp_album_year_t)(sp_album *album);
typedef sp_albumtype (*fptr_sp_album_type_t)(sp_album *album);
typedef const byte*  (*fptr_sp_album_cover_t)(sp_album *album, sp_image_size size);

typedef const char*  (*fptr_sp_artist_name_t)(sp_artist *artist);

typedef sp_image*    (*fptr_sp_image_create_t)(sp_session *session, const byte image_id[20]);
typedef bool         (*fptr_sp_image_is_loaded_t)(sp_image *image);
typedef sp_error     (*fptr_sp_image_error_t)(sp_image *image);
typedef sp_imageformat (*fptr_sp_image_format_t)(sp_image *image);
typedef const void*  (*fptr_sp_image_data_t)(sp_image *image, size_t *data_size);
typedef sp_error     (*fptr_sp_image_release_t)(sp_image *image);

/* Define actual function pointers */
fptr_sp_error_message_t fptr_sp_error_message;

fptr_sp_session_create_t fptr_sp_session_create;
fptr_sp_session_release_t fptr_sp_session_release;
fptr_sp_session_login_t fptr_sp_session_login;
fptr_sp_session_relogin_t fptr_sp_session_relogin;
fptr_sp_session_logout_t fptr_sp_session_logout;
fptr_sp_session_starred_create_t fptr_sp_session_starred_create;
fptr_sp_session_playlistcontainer_t fptr_sp_session_playlistcontainer;
fptr_sp_session_process_events_t fptr_sp_session_process_events;
fptr_sp_session_player_load_t fptr_sp_session_player_load;
fptr_sp_session_player_unload_t fptr_sp_session_player_unload;
fptr_sp_session_player_play_t fptr_sp_session_player_play;
fptr_sp_session_player_seek_t fptr_sp_session_player_seek;
fptr_sp_session_connectionstate_t fptr_sp_session_connectionstate;
fptr_sp_session_preferred_bitrate_t fptr_sp_session_preferred_bitrate;

fptr_sp_playlistcontainer_add_callbacks_t fptr_sp_playlistcontainer_add_callbacks;
fptr_sp_playlistcontainer_num_playlists_t fptr_sp_playlistcontainer_num_playlists;
fptr_sp_playlistcontainer_playlist_t fptr_sp_playlistcontainer_playlist;

fptr_sp_playlist_add_callbacks_t fptr_sp_playlist_add_callbacks;
fptr_sp_playlist_name_t fptr_sp_playlist_name;
fptr_sp_playlist_remove_callbacks_t fptr_sp_playlist_remove_callbacks;
fptr_sp_playlist_num_tracks_t fptr_sp_playlist_num_tracks;
fptr_sp_playlist_track_t fptr_sp_playlist_track;
fptr_sp_playlist_is_loaded_t fptr_sp_playlist_is_loaded;

fptr_sp_track_error_t fptr_sp_track_error;
fptr_sp_track_is_loaded_t fptr_sp_track_is_loaded;
fptr_sp_track_name_t fptr_sp_track_name;
fptr_sp_track_duration_t fptr_sp_track_duration;
fptr_sp_track_index_t fptr_sp_track_index;
fptr_sp_track_disc_t fptr_sp_track_disc;
fptr_sp_track_album_t fptr_sp_track_album;
fptr_sp_track_get_availability_t fptr_sp_track_get_availability;
fptr_sp_track_is_starred_t fptr_sp_track_is_starred;

fptr_sp_link_create_from_playlist_t fptr_sp_link_create_from_playlist;
fptr_sp_link_create_from_track_t fptr_sp_link_create_from_track;
fptr_sp_link_create_from_string_t fptr_sp_link_create_from_string;
fptr_sp_link_as_string_t fptr_sp_link_as_string;
fptr_sp_link_as_track_t fptr_sp_link_as_track;
fptr_sp_link_release_t fptr_sp_link_release;

fptr_sp_album_name_t fptr_sp_album_name;
fptr_sp_album_artist_t fptr_sp_album_artist;
fptr_sp_album_year_t fptr_sp_album_year;
fptr_sp_album_type_t fptr_sp_album_type;
fptr_sp_album_cover_t fptr_sp_album_cover;

fptr_sp_artist_name_t fptr_sp_artist_name;

fptr_sp_image_create_t fptr_sp_image_create;
fptr_sp_image_is_loaded_t fptr_sp_image_is_loaded;
fptr_sp_image_error_t fptr_sp_image_error;
fptr_sp_image_format_t fptr_sp_image_format;
fptr_sp_image_data_t fptr_sp_image_data;
fptr_sp_image_release_t fptr_sp_image_release;

/* Assign function pointers to libspotify symbol */
static int
fptr_assign_all()
{
  void *h;
  char *err;
  int ret;

  h = g_libhandle;

  // The following is non-ISO compliant
  ret = (fptr_sp_error_message = dlsym(h, "sp_error_message"))
   && (fptr_sp_session_create = dlsym(h, "sp_session_create"))
   && (fptr_sp_session_release = dlsym(h, "sp_session_release"))
   && (fptr_sp_session_login = dlsym(h, "sp_session_login"))
   && (fptr_sp_session_relogin = dlsym(h, "sp_session_relogin"))
   && (fptr_sp_session_logout = dlsym(h, "sp_session_logout"))
   && (fptr_sp_session_playlistcontainer = dlsym(h, "sp_session_playlistcontainer"))
   && (fptr_sp_session_process_events = dlsym(h, "sp_session_process_events"))
   && (fptr_sp_session_player_load = dlsym(h, "sp_session_player_load"))
   && (fptr_sp_session_player_unload = dlsym(h, "sp_session_player_unload"))
   && (fptr_sp_session_player_play = dlsym(h, "sp_session_player_play"))
   && (fptr_sp_session_player_seek = dlsym(h, "sp_session_player_seek"))
   && (fptr_sp_session_connectionstate = dlsym(h, "sp_session_connectionstate"))
   && (fptr_sp_session_preferred_bitrate = dlsym(h, "sp_session_preferred_bitrate"))
   && (fptr_sp_playlistcontainer_add_callbacks = dlsym(h, "sp_playlistcontainer_add_callbacks"))
   && (fptr_sp_playlistcontainer_num_playlists = dlsym(h, "sp_playlistcontainer_num_playlists"))
   && (fptr_sp_session_starred_create = dlsym(h, "sp_session_starred_create"))
   && (fptr_sp_playlistcontainer_playlist = dlsym(h, "sp_playlistcontainer_playlist"))
   && (fptr_sp_playlist_add_callbacks = dlsym(h, "sp_playlist_add_callbacks"))
   && (fptr_sp_playlist_name = dlsym(h, "sp_playlist_name"))
   && (fptr_sp_playlist_remove_callbacks = dlsym(h, "sp_playlist_remove_callbacks"))
   && (fptr_sp_playlist_num_tracks = dlsym(h, "sp_playlist_num_tracks"))
   && (fptr_sp_playlist_track = dlsym(h, "sp_playlist_track"))
   && (fptr_sp_playlist_is_loaded = dlsym(h, "sp_playlist_is_loaded"))
   && (fptr_sp_track_error = dlsym(h, "sp_track_error"))
   && (fptr_sp_track_is_loaded = dlsym(h, "sp_track_is_loaded"))
   && (fptr_sp_track_name = dlsym(h, "sp_track_name"))
   && (fptr_sp_track_duration = dlsym(h, "sp_track_duration"))
   && (fptr_sp_track_index = dlsym(h, "sp_track_index"))
   && (fptr_sp_track_disc = dlsym(h, "sp_track_disc"))
   && (fptr_sp_track_album = dlsym(h, "sp_track_album"))
   && (fptr_sp_track_get_availability = dlsym(h, "sp_track_get_availability"))
   && (fptr_sp_track_is_starred = dlsym(h, "sp_track_is_starred"))
   && (fptr_sp_link_create_from_playlist = dlsym(h, "sp_link_create_from_playlist"))
   && (fptr_sp_link_create_from_track = dlsym(h, "sp_link_create_from_track"))
   && (fptr_sp_link_create_from_string = dlsym(h, "sp_link_create_from_string"))
   && (fptr_sp_link_as_string = dlsym(h, "sp_link_as_string"))
   && (fptr_sp_link_as_track = dlsym(h, "sp_link_as_track"))
   && (fptr_sp_link_release = dlsym(h, "sp_link_release"))
   && (fptr_sp_album_name = dlsym(h, "sp_album_name"))
   && (fptr_sp_album_artist = dlsym(h, "sp_album_artist"))
   && (fptr_sp_album_year = dlsym(h, "sp_album_year"))
   && (fptr_sp_album_type = dlsym(h, "sp_album_type"))
   && (fptr_sp_album_cover = dlsym(h, "sp_album_cover"))
   && (fptr_sp_artist_name = dlsym(h, "sp_artist_name"))
   && (fptr_sp_image_create = dlsym(h, "sp_image_create"))
   && (fptr_sp_image_is_loaded = dlsym(h, "sp_image_is_loaded"))
   && (fptr_sp_image_error = dlsym(h, "sp_image_error"))
   && (fptr_sp_image_format = dlsym(h, "sp_image_format"))
   && (fptr_sp_image_data = dlsym(h, "sp_image_data"))
   && (fptr_sp_image_release = dlsym(h, "sp_image_release"))
   ;

  err = dlerror();

  if (ret && !err)
    return ret;
  else if (err)
    DPRINTF(E_LOG, L_SPOTIFY, "Assignment error (%d): %s\n", ret, err);
  else
    DPRINTF(E_LOG, L_SPOTIFY, "Unknown assignment error (%d)\n", ret);

  return -1;
}
// End of ugly part


/* ---------------------------- COMMAND EXECUTION -------------------------- */

static void
command_init(struct spotify_command *cmd)
{
  memset(cmd, 0, sizeof(struct spotify_command));

  pthread_mutex_init(&cmd->lck, NULL);
  pthread_cond_init(&cmd->cond, NULL);
}

static void
command_deinit(struct spotify_command *cmd)
{
  pthread_cond_destroy(&cmd->cond);
  pthread_mutex_destroy(&cmd->lck);
}

static int
send_command(struct spotify_command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "BUG: cmd->func is NULL!\n");
      return -1;
    }

  ret = write(g_cmd_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not send command: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

static int
sync_command(struct spotify_command *cmd)
{
  int ret;

  pthread_mutex_lock(&cmd->lck);

  ret = send_command(cmd);
  if (ret < 0)
    {
      pthread_mutex_unlock(&cmd->lck);
      return -1;
    }

  pthread_cond_wait(&cmd->cond, &cmd->lck);
  pthread_mutex_unlock(&cmd->lck);

  ret = cmd->ret;

  return ret;
}

static int
nonblock_command(struct spotify_command *cmd)
{
  int ret;

  ret = send_command(cmd);
  if (ret < 0)
    return -1;

  return 0;
}

/* Thread: main and filescanner */
static void
thread_exit(void)
{
  int dummy = 42;

  DPRINTF(E_DBG, L_SPOTIFY, "Killing Spotify thread\n");

  if (write(g_exit_pipe[1], &dummy, sizeof(dummy)) != sizeof(dummy))
    DPRINTF(E_LOG, L_SPOTIFY, "Could not write to exit fd: %s\n", strerror(errno));
}


/* --------------------------  PLAYLIST HELPERS    ------------------------- */
/*            Should only be called from within the spotify thread           */

static int
spotify_metadata_get(sp_track *track, struct media_file_info *mfi, const char *pltitle)
{
  cfg_t *spotify_cfg;
  bool artist_override;
  bool starred_artist_override;
  bool album_override;
  bool starred_album_override;
  sp_album *album;
  sp_artist *artist;
  sp_albumtype albumtype;
  bool starred;
  int compilation;
  char *albumname;

  spotify_cfg = cfg_getsec(cfg, "spotify");
  artist_override = cfg_getbool(spotify_cfg, "artist_override");
  starred_artist_override = cfg_getbool(spotify_cfg, "starred_artist_override");
  album_override = cfg_getbool(spotify_cfg, "album_override");
  starred_album_override = cfg_getbool(spotify_cfg, "starred_album_override");

  album = fptr_sp_track_album(track);
  if (!album)
    return -1;

  artist = fptr_sp_album_artist(album);
  if (!artist)
    return -1;

  albumtype = fptr_sp_album_type(album);
  starred = fptr_sp_track_is_starred(g_sess, track);

  /*
   * Treat album as compilation if one of the following conditions is true:
   * - spotfy album type is compilation
   * - artist_override in config is set to true and track is not part of the starred playlist
   * - starred_artist_override in config is set to true and track is part of the starred playlist
   */
  compilation = ((albumtype == SP_ALBUMTYPE_COMPILATION)
		  || (starred && starred_artist_override)
		  || (!starred && artist_override));

  if ((starred && starred_album_override)
      || (!starred && album_override))
    albumname = strdup(pltitle);
  else
    albumname = strdup(fptr_sp_album_name(album));

  mfi->title       = strdup(fptr_sp_track_name(track));
  mfi->album       = albumname;
  mfi->artist      = strdup(fptr_sp_artist_name(artist));
  mfi->year        = fptr_sp_album_year(album);
  mfi->song_length = fptr_sp_track_duration(track);
  mfi->track       = fptr_sp_track_index(track);
  mfi->disc        = fptr_sp_track_disc(track);
  mfi->compilation = compilation;
  mfi->artwork     = ARTWORK_SPOTIFY;
  mfi->type        = strdup("spotify");
  mfi->codectype   = strdup("wav");
  mfi->description = strdup("Spotify audio");

  DPRINTF(E_SPAM, L_SPOTIFY, "Metadata for track:\n"
      "Title:       %s\n"
      "Album:       %s\n"
      "Artist:      %s\n"
      "Year:        %u\n"
      "Track:       %u\n"
      "Disc:        %u\n"
      "Compilation: %d\n"
      "Starred:     %d\n",
      mfi->title,
      mfi->album,
      mfi->artist,
      mfi->year,
      mfi->track,
      mfi->disc,
      mfi->compilation,
      starred);

  return 0;
}

static int
spotify_track_save(int plid, sp_track *track, const char *pltitle)
{
  struct media_file_info mfi;
  sp_link *link;
  char url[1024];
  int ret;

  if (!fptr_sp_track_is_loaded(track))
    {
      DPRINTF(E_INFO, L_SPOTIFY, "Metadata for track not ready yet\n");
      return 0;
    }

  if (fptr_sp_track_get_availability(g_sess, track) != SP_TRACK_AVAILABILITY_AVAILABLE)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Track not available for playback: '%s'\n", fptr_sp_track_name(track));
      return 0;
    }

  link = fptr_sp_link_create_from_track(track, 0);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create link for track: '%s'\n", fptr_sp_track_name(track));
      return -1;
    }

  ret = fptr_sp_link_as_string(link, url, sizeof(url));
  if (ret == sizeof(url))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Spotify link truncated: '%s'\n", url);
    }
  fptr_sp_link_release(link);

  /* Add to playlistitems table */
  ret = db_pl_add_item_bypath(plid, url);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not save playlist item: '%s'\n", url);
      return -1;
    }

  memset(&mfi, 0, sizeof(struct media_file_info));

  ret = spotify_metadata_get(track, &mfi, pltitle);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Metadata missing (but track should be loaded?): '%s'\n", fptr_sp_track_name(track));
      free_mfi(&mfi, 1);
      return -1;
    }

  filescanner_process_media(url, time(NULL), 0, F_SCAN_TYPE_SPOTIFY, &mfi);

  free_mfi(&mfi, 1);

  return 0;
}

static int
spotify_playlist_save(sp_playlist *pl)
{
  struct playlist_info *pli;
  sp_track *track;
  sp_link *link;
  char url[1024];
  const char *name;
  int plid;
  int num_tracks;
  char virtual_path[PATH_MAX];
  int ret;
  int i;
  
  if (!fptr_sp_playlist_is_loaded(pl))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist still not loaded - wait for rename callback\n");
      return 0;
    }

  name = fptr_sp_playlist_name(pl);

  // The starred playlist has an empty name, set it manually to "Starred"
  if (*name == '\0')
    name = "Starred";

  DPRINTF(E_INFO, L_SPOTIFY, "Saving playlist: '%s'\n", name);

  /* Save playlist (playlists table) */
  link = fptr_sp_link_create_from_playlist(pl);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create link for playlist (wait): '%s'\n", name);
      return -1;
    }

  ret = fptr_sp_link_as_string(link, url, sizeof(url));
  if (ret == sizeof(url))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Spotify link truncated: %s\n", url);
    }
  fptr_sp_link_release(link);

  pli = db_pl_fetch_bypath(url);

  snprintf(virtual_path, PATH_MAX, "/spotify:/%s", name);

  if (pli)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist found ('%s', link %s), updating\n", name, url);

      plid = pli->id;

      free(pli->title);
      pli->title = strdup(name);
      free(pli->virtual_path);
      pli->virtual_path = strdup(virtual_path);

      ret = db_pl_update(pli);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error updating playlist ('%s', link %s)\n", name, url);

	  free_pli(pli, 0);
	  return -1;
	}

      db_pl_clear_items(plid);
    }
  else
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Adding playlist ('%s', link %s)\n", name, url);

      pli = (struct playlist_info *)malloc(sizeof(struct playlist_info));
      if (!pli)
	{
	  DPRINTF(E_LOG, L_SCAN, "Out of memory\n");

	  return -1;
	}

      memset(pli, 0, sizeof(struct playlist_info));

      pli->type = PL_PLAIN;
      pli->title = strdup(name);
      pli->path = strdup(url);
      pli->virtual_path = strdup(virtual_path);
      pli->parent_id = g_base_plid;

      ret = db_pl_add(pli, &plid);
      if ((ret < 0) || (plid < 1))
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error adding playlist ('%s', link %s, ret %d, plid %d)\n", name, url, ret, plid);

	  free_pli(pli, 0);
	  return -1;
	}
    }

  free_pli(pli, 0);

  /* Save tracks and playlistitems (files and playlistitems table) */
  num_tracks = fptr_sp_playlist_num_tracks(pl);
  for (i = 0; i < num_tracks; i++)
    {
      track = fptr_sp_playlist_track(pl, i);
      if (!track)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Track %d in playlist '%s' (id %d) is invalid\n", i, name, plid);
	  continue;
	}

      ret = spotify_track_save(plid, track, name);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error saving track %d to playlist '%s' (id %d)\n", i, name, plid);
	  continue;
	}
    }

  return plid;
}


/* --------------------------  PLAYLIST CALLBACKS  ------------------------- */
/**
 * Called when a playlist is updating or is done updating
 *
 * This is called before and after a series of changes are applied to the
 * playlist. It allows e.g. the user interface to defer updating until the
 * entire operation is complete.
 *
 * @param[in]  pl         Playlist object
 * @param[in]  done       True iff the update is completed
 * @param[in]  userdata   Userdata passed to sp_playlist_add_callbacks()
 */
static void playlist_update_in_progress(sp_playlist *pl, bool done, void *userdata)
{
  if (done)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist update (status %d): %s\n", done, fptr_sp_playlist_name(pl));

      spotify_playlist_save(pl);
    }
}

static void playlist_metadata_updated(sp_playlist *pl, void *userdata)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Playlist metadata updated: %s\n", fptr_sp_playlist_name(pl));

  spotify_playlist_save(pl);
}

/**
 * The callbacks we are interested in for individual playlists.
 */
static sp_playlist_callbacks pl_callbacks = {
  .playlist_update_in_progress = &playlist_update_in_progress,
  .playlist_metadata_updated = &playlist_metadata_updated,
};


/* --------------------  PLAYLIST CONTAINER CALLBACKS  --------------------- */
/**
 * Callback from libspotify, telling us a playlist was added to the playlist container.
 *
 * We add our playlist callbacks to the newly added playlist.
 *
 * @param  pc            The playlist container handle
 * @param  pl            The playlist handle
 * @param  position      Index of the added playlist
 * @param  userdata      The opaque pointer
 */
static void playlist_added(sp_playlistcontainer *pc, sp_playlist *pl,
                           int position, void *userdata)
{
  DPRINTF(E_INFO, L_SPOTIFY, "Playlist added: %s (%d tracks)\n", fptr_sp_playlist_name(pl), fptr_sp_playlist_num_tracks(pl));

  fptr_sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);

  spotify_playlist_save(pl);
}

/**
 * Callback from libspotify, telling us a playlist was removed from the playlist container.
 *
 * This is the place to remove our playlist callbacks.
 *
 * @param  pc            The playlist container handle
 * @param  pl            The playlist handle
 * @param  position      Index of the removed playlist
 * @param  userdata      The opaque pointer
 */
static void
playlist_removed(sp_playlistcontainer *pc, sp_playlist *pl, int position, void *userdata)
{
  struct playlist_info *pli;
  sp_link *link;
  char url[1024];
  int plid;
  int ret;

  DPRINTF(E_INFO, L_SPOTIFY, "Playlist removed: %s\n", fptr_sp_playlist_name(pl));

  fptr_sp_playlist_remove_callbacks(pl, &pl_callbacks, NULL);

  link = fptr_sp_link_create_from_playlist(pl);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not find link for deleted playlist\n");
      return;
    }

  ret = fptr_sp_link_as_string(link, url, sizeof(url));
  if (ret == sizeof(url))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Spotify link truncated: %s\n", url);
    }
  fptr_sp_link_release(link);

  pli = db_pl_fetch_bypath(url);

  if (!pli)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist %s not found, can't delete\n", url);
      return;
    }

  plid = pli->id;

  free_pli(pli, 0);

  db_spotify_pl_delete(plid);
}

/**
 * Callback from libspotify, telling us the rootlist is fully synchronized
 *
 * @param  pc            The playlist container handle
 * @param  userdata      The opaque pointer
 */
static void
container_loaded(sp_playlistcontainer *pc, void *userdata)
{
  int num;

  num = fptr_sp_playlistcontainer_num_playlists(pc);

  DPRINTF(E_INFO, L_SPOTIFY, "Rootlist synchronized (%d playlists)\n", num);
}


/**
 * The playlist container callbacks
 */
static sp_playlistcontainer_callbacks pc_callbacks = {
  .playlist_added = &playlist_added,
  .playlist_removed = &playlist_removed,
  .container_loaded = &container_loaded,
};


/* --------------------- INTERNAL PLAYBACK AND AUDIO ----------------------- */
/*            Should only be called from within the spotify thread           */

static void
audio_fifo_flush(void)
{
    audio_fifo_data_t *afd;

    DPRINTF(E_DBG, L_SPOTIFY, "Flushing audio fifo\n");

    pthread_mutex_lock(&g_audio_fifo->mutex);

    while((afd = TAILQ_FIRST(&g_audio_fifo->q))) {
	TAILQ_REMOVE(&g_audio_fifo->q, afd, link);
	free(afd);
    }

    g_audio_fifo->qlen = 0;
    g_audio_fifo->fullcount = 0;
    pthread_mutex_unlock(&g_audio_fifo->mutex);
}

static int
playback_play(struct spotify_command *cmd)
{
  sp_track *track;
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Starting playback\n");

  if (SP_CONNECTION_STATE_LOGGED_IN != fptr_sp_session_connectionstate(g_sess))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Can't play music, not connected and logged in to Spotify\n");
      return -1;
    }

  if (!cmd->arg.link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, no Spotify link\n");
      return -1;
    }

  track = fptr_sp_link_as_track(cmd->arg.link);
  if (!track)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, invalid Spotify track\n");
      return -1;
    }
  
  err = fptr_sp_session_player_load(g_sess, track);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  audio_fifo_flush();

  err = fptr_sp_session_player_play(g_sess, 1);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  g_state = SPOTIFY_STATE_PLAYING;

  return 0;
}

static int
playback_pause(struct spotify_command *cmd)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Pausing playback\n");

  err = fptr_sp_session_player_play(g_sess, 0);
  DPRINTF(E_DBG, L_SPOTIFY, "Playback paused\n");

  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback pause failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  g_state = SPOTIFY_STATE_PAUSED;

  return 0;
}

static int
playback_resume(struct spotify_command *cmd)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Resuming playback\n");

  err = fptr_sp_session_player_play(g_sess, 1);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback resume failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  g_state = SPOTIFY_STATE_PLAYING;

  return 0;
}

static int
playback_stop(struct spotify_command *cmd)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Stopping playback\n");

  err = fptr_sp_session_player_unload(g_sess);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback stop failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  g_state = SPOTIFY_STATE_STOPPED;

  return 0;
}

static int
playback_seek(struct spotify_command *cmd)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback seek\n");

  err = fptr_sp_session_player_seek(g_sess, cmd->arg.seek_ms);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not seek: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  audio_fifo_flush();

  return 0;
}

static int
playback_eot(struct spotify_command *cmd)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback end of track\n");

  err = fptr_sp_session_player_unload(g_sess);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback end of track failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  g_state = SPOTIFY_STATE_STOPPING;

  return 0;
}

static int
audio_get(struct spotify_command *cmd)
{
  struct timespec ts;
  audio_fifo_data_t *afd;
  int processed;
  int timeout;
  int ret;
  int s;

  afd = NULL;
  processed = 0;

  // If spotify was paused begin by resuming playback
  if (g_state == SPOTIFY_STATE_PAUSED)
    playback_resume(NULL);

  pthread_mutex_lock(&g_audio_fifo->mutex);

  while ((processed < cmd->arg.audio.wanted) && (g_state != SPOTIFY_STATE_STOPPED))
    {
      // If track has ended and buffer is empty
      if ((g_state == SPOTIFY_STATE_STOPPING) && (g_audio_fifo->qlen <= 0))
	{
	  DPRINTF(E_DBG, L_SPOTIFY, "Track finished\n");
	  g_state = SPOTIFY_STATE_STOPPED;
	  break;
	}

      // If buffer is empty, wait for audio, but use timed wait so we don't
      // risk waiting forever (maybe the player stopped while we were waiting)
      timeout = 0;
      while ( !(afd = TAILQ_FIRST(&g_audio_fifo->q)) && 
	       (g_state != SPOTIFY_STATE_STOPPED) &&
	       (g_state != SPOTIFY_STATE_STOPPING) &&
	       (timeout < SPOTIFY_TIMEOUT) )
	{
	  DPRINTF(E_DBG, L_SPOTIFY, "Waiting for audio\n");
#if _POSIX_TIMERS > 0
	  clock_gettime(CLOCK_REALTIME, &ts);
#else
	  struct timeval tv;
	  gettimeofday(&tv, NULL);
	  TIMEVAL_TO_TIMESPEC(&tv, &ts);
#endif
	  ts.tv_sec += 5;
	  timeout += 5;

	  pthread_cond_timedwait(&g_audio_fifo->cond, &g_audio_fifo->mutex, &ts);
	}

      if ((!afd) && (timeout >= SPOTIFY_TIMEOUT))
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Timeout waiting for audio (waited %d sec)\n", timeout);

	  spotify_playback_stop_nonblock();
	}

      if (!afd)
	break;

      TAILQ_REMOVE(&g_audio_fifo->q, afd, link);
      g_audio_fifo->qlen -= afd->nsamples;

      s = afd->nsamples * sizeof(int16_t) * 2;
  
      ret = evbuffer_add(cmd->arg.audio.evbuf, afd->samples, s);
      free(afd);
      afd = NULL;
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for evbuffer (tried to add %d bytes)\n", s);
	  pthread_mutex_unlock(&g_audio_fifo->mutex);
	  return -1;
	}

      processed += s;
    }

  pthread_mutex_unlock(&g_audio_fifo->mutex);

  return processed;
}

static int
artwork_get(struct spotify_command *cmd)
{
  char *path;
  sp_link *link;
  sp_track *track;
  sp_album *album;
  const byte *image_id;
  sp_image *image;
  sp_image_size image_size;
  sp_imageformat imageformat;
  sp_error err;
  const void *data;
  size_t data_size;
  int ret;

  path = cmd->arg.artwork.path;

  // Now begins: path -> link -> track -> album -> image_id -> image -> format -> data
  link = fptr_sp_link_create_from_string(path);
  if (!link)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Getting artwork failed, invalid Spotify link: %s\n", path);
      goto level1_exit;
    }

  track = fptr_sp_link_as_track(link);
  if (!track)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Getting artwork failed, invalid Spotify track: %s\n", path);
      goto level2_exit;
    }

  album = fptr_sp_track_album(track);
  if (!album)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Getting artwork failed, invalid Spotify album: %s\n", path);
      goto level2_exit;
    }

  // Get an image at least the same size as requested
  image_size = SP_IMAGE_SIZE_SMALL; // 64x64
  if ((cmd->arg.artwork.max_w > 64) || (cmd->arg.artwork.max_h > 64))
    image_size = SP_IMAGE_SIZE_NORMAL; // 300x300
  if ((cmd->arg.artwork.max_w > 300) || (cmd->arg.artwork.max_h > 300))
    image_size = SP_IMAGE_SIZE_LARGE; // 640x640

  image_id = fptr_sp_album_cover(album, image_size);
  if (!image_id)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Getting artwork failed, no Spotify image id: %s\n", path);
      goto level2_exit;
    }

  image = fptr_sp_image_create(g_sess, image_id);
  if (!image)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Getting artwork failed, no Spotify image: %s\n", path);
      goto level2_exit;
    }

  // We want to be fast, so no waiting for the image to load
  if (!fptr_sp_image_is_loaded(image))
    goto level3_exit;

  err = fptr_sp_image_error(image);
  if (err != SP_ERROR_OK)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Getting artwork failed, Spotify error: %s\n", fptr_sp_error_message(err));
      goto level3_exit;
    }

  imageformat = fptr_sp_image_format(image);
  if (imageformat != SP_IMAGE_FORMAT_JPEG)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Getting artwork failed, invalid image format from Spotify: %s\n", path);
      goto level3_exit;
    }

  data = fptr_sp_image_data(image, &data_size);
  if (!data || (data_size == 0))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Getting artwork failed, no image data from Spotify: %s\n", path);
      goto level3_exit;
    }

  ret = evbuffer_expand(cmd->arg.artwork.evbuf, data_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for artwork\n");
      goto level3_exit;
    }

  ret = evbuffer_add(cmd->arg.artwork.evbuf, data, data_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not add Spotify image to event buffer\n");
      goto level3_exit;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Spotify artwork loaded ok\n");

  fptr_sp_image_release(image);

  return data_size;

 level3_exit:
  fptr_sp_image_release(image);

 level2_exit:
  fptr_sp_link_release(link);

 level1_exit:
  return -1;
}


/* ---------------------------  SESSION CALLBACKS  ------------------------- */
/**
 * This callback is called when an attempt to login has succeeded or failed.
 *
 * @sa sp_session_callbacks#logged_in
 */
static void
logged_in(sp_session *sess, sp_error error)
{
  cfg_t *spotify_cfg;
  sp_playlist *pl;
  sp_playlistcontainer *pc;
  struct playlist_info pli;
  int ret;
  int i;

  if (SP_ERROR_OK != error)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Login failed: %s\n",	fptr_sp_error_message(error));
      return;
    }

  DPRINTF(E_LOG, L_SPOTIFY, "Login to Spotify succeeded. Reloading playlists.\n");

  db_spotify_purge();

  pl = fptr_sp_session_starred_create(sess);
  fptr_sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);

  spotify_cfg = cfg_getsec(cfg, "spotify");
  if (! cfg_getbool(spotify_cfg, "base_playlist_disable"))
    {
      memset(&pli, 0, sizeof(struct playlist_info));
      pli.title = "Spotify";
      pli.type = PL_FOLDER;
      pli.path = "spotify:playlistfolder";

      ret = db_pl_add(&pli, &g_base_plid);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error adding base playlist\n");
	  return;
	}
    }
  else
    g_base_plid = 0;

  pc = fptr_sp_session_playlistcontainer(sess);

  fptr_sp_playlistcontainer_add_callbacks(pc, &pc_callbacks, NULL);

  DPRINTF(E_DBG, L_SPOTIFY, "Found %d playlists\n", fptr_sp_playlistcontainer_num_playlists(pc));

  for (i = 0; i < fptr_sp_playlistcontainer_num_playlists(pc); i++)
    {
      pl = fptr_sp_playlistcontainer_playlist(pc, i);
      fptr_sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);
    }
}

/**
 * Called when logout has been processed.
 * Either called explicitly if you initialize a logout operation, or implicitly
 * if there is a permanent connection error
 *
 * @sa sp_session_callbacks#logged_out
 */
static void
logged_out(sp_session *sess)
{
  DPRINTF(E_INFO, L_SPOTIFY, "Logout complete\n");

  pthread_mutex_lock(&login_lck);

  pthread_cond_signal(&login_cond);
  pthread_mutex_unlock(&login_lck);
}

/**
 * This callback is used from libspotify whenever there is PCM data available.
 *
 * @sa sp_session_callbacks#music_delivery
 */
static int music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
  audio_fifo_data_t *afd;
  size_t s;

  /* No support for resampling right now */
  if ((format->sample_rate != 44100) || (format->channels != 2))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Got music with unsupported samplerate or channels, stopping playback\n");
      spotify_playback_stop_nonblock();
      return num_frames;
    }

  if (num_frames == 0)
    return 0; // Audio discontinuity, do nothing

  pthread_mutex_lock(&g_audio_fifo->mutex);

  /* Buffer three seconds of audio */
  if (g_audio_fifo->qlen > (3 * format->sample_rate))
    {
      // If the buffer has been full the last 300 times (~about a minute) we
      // assume the player thread paused/died without telling us, so we signal pause
      if (g_audio_fifo->fullcount < 300)
	g_audio_fifo->fullcount++;
      else
	{
	  DPRINTF(E_WARN, L_SPOTIFY, "Buffer full more than 300 times, pausing\n");
	  spotify_playback_pause_nonblock();
	  g_audio_fifo->fullcount = 0;
	}

      pthread_mutex_unlock(&g_audio_fifo->mutex);

      return 0;
    }
  else
    g_audio_fifo->fullcount = 0;

  s = num_frames * sizeof(int16_t) * format->channels;

  afd = malloc(sizeof(*afd) + s);
  memcpy(afd->samples, frames, s);

  afd->nsamples = num_frames;

  TAILQ_INSERT_TAIL(&g_audio_fifo->q, afd, link);
  g_audio_fifo->qlen += num_frames;

  pthread_cond_signal(&g_audio_fifo->cond);
  pthread_mutex_unlock(&g_audio_fifo->mutex);

  return num_frames;
}

/**
 * This callback is called from an internal libspotify thread to ask us to
 * reiterate the main loop. This must not block.
 *
 * @sa sp_session_callbacks#notify_main_thread
 */
static void
notify_main_thread(sp_session *sess)
{
  int dummy = 42;
  int ret;

  ret = write(g_notify_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_SPOTIFY, "Could not write to notify fd: %s\n", strerror(errno));
}

/**
 * Called whenever metadata has been updated
 *
 * If you have metadata cached outside of libspotify, you should purge
 * your caches and fetch new versions.
 *
 * @param[in]  session    Session
 */
static void metadata_updated(sp_session *session)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Session metadata updated\n");
}

/* Misc connection error callbacks */
static void play_token_lost(sp_session *sess)
{
  DPRINTF(E_LOG, L_SPOTIFY, "Music interrupted - some other session is playing on the account\n");

  spotify_playback_stop_nonblock();
}

static void connectionstate_updated(sp_session *session)
{
  if (SP_CONNECTION_STATE_LOGGED_IN == fptr_sp_session_connectionstate(session))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Connection to Spotify (re)established\n");
    }
  else if (g_state == SPOTIFY_STATE_PLAYING)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Music interrupted - connection error or logged out\n");
      spotify_playback_stop_nonblock();
    }
}

/**
 * This callback is used from libspotify when the current track has ended
 *
 * @sa sp_session_callbacks#end_of_track
 */
static void end_of_track(sp_session *sess)
{
  struct spotify_command *cmd;

  DPRINTF(E_DBG, L_SPOTIFY, "End of track\n");

  cmd = (struct spotify_command *)malloc(sizeof(struct spotify_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not allocate spotify_command\n");
      return;
    }
  memset(cmd, 0, sizeof(struct spotify_command));

  cmd->nonblock = 1;

  cmd->func = playback_eot;
  cmd->arg.noarg = NULL;

  nonblock_command(cmd);
}

/**
 * The session callbacks
 */
static sp_session_callbacks session_callbacks = {
  .logged_in = &logged_in,
  .logged_out = &logged_out,
  .connectionstate_updated = &connectionstate_updated,
  .notify_main_thread = &notify_main_thread,
  .music_delivery = &music_delivery,
  .metadata_updated = &metadata_updated,
  .play_token_lost = &play_token_lost,
  .log_message = NULL,
  .end_of_track = &end_of_track,
};

/**
 * The session configuration.
 */
static sp_session_config spconfig = {
  .api_version = SPOTIFY_API_VERSION,
  .cache_location = NULL,
  .settings_location = NULL,
  .application_key = g_appkey,
  .application_key_size = sizeof(g_appkey),
  .user_agent = "forked-daapd",
  .callbacks = &session_callbacks,
  NULL,
};


/* ------------------------------- MAIN LOOP ------------------------------- */
/*                              Thread: spotify                              */

static void *
spotify(void *arg)
{
  int ret;

  DPRINTF(E_DBG, L_SPOTIFY, "Main loop initiating\n");

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error: DB init failed\n");
      pthread_exit(NULL);
    }

  g_state = SPOTIFY_STATE_WAIT;

  event_base_dispatch(evbase_spotify);

  if (g_state != SPOTIFY_STATE_INACTIVE)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Spotify event loop terminated ahead of time!\n");
      g_state = SPOTIFY_STATE_INACTIVE;
    }

  db_perthread_deinit();

  DPRINTF(E_DBG, L_SPOTIFY, "Main loop terminating\n");

  pthread_exit(NULL);
}

static void
exit_cb(int fd, short what, void *arg)
{
  int dummy;
  int ret;

  ret = read(g_exit_pipe[0], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_SPOTIFY, "Error reading from exit pipe\n");

  fptr_sp_session_player_unload(g_sess);
  fptr_sp_session_logout(g_sess);

  event_base_loopbreak(evbase_spotify);

  g_state = SPOTIFY_STATE_INACTIVE;

  event_add(g_exitev, NULL);
}

static void
command_cb(int fd, short what, void *arg)
{
  struct spotify_command *cmd;
  int ret;

  ret = read(g_cmd_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not read command! (read %d): %s\n", ret, (ret < 0) ? strerror(errno) : "-no error-");
      goto readd;
    }

  if (cmd->nonblock)
    {
      cmd->func(cmd);

      free(cmd);
      goto readd;
    }

  pthread_mutex_lock(&cmd->lck);

  g_cmd = cmd;
  ret = cmd->func(cmd);
  cmd->ret = ret;
  g_cmd = NULL;

  pthread_cond_signal(&cmd->cond);
  pthread_mutex_unlock(&cmd->lck);

 readd:
  event_add(g_cmdev, NULL);
}

/* Process events when timeout expires or triggered by libspotify's notify_main_thread */
static void
notify_cb(int fd, short what, void *arg)
{
  struct timeval tv;
  int next_timeout;
  int dummy;
  int ret;

  if (what & EV_READ)
    {
      ret = read(g_notify_pipe[0], &dummy, sizeof(dummy));
      if (ret != sizeof(dummy))
	DPRINTF(E_LOG, L_SPOTIFY, "Error reading from notify pipe\n");
    }

  do
    {
      fptr_sp_session_process_events(g_sess, &next_timeout);
    }
  while (next_timeout == 0);

  tv.tv_sec  = next_timeout / 1000;
  tv.tv_usec = (next_timeout % 1000) * 1000;

  event_add(g_notifyev, &tv);
}


/* ---------------------------- Our Spotify API  --------------------------- */

/* Thread: player */
int
spotify_playback_play(struct media_file_info *mfi)
{
  struct spotify_command cmd;
  sp_link *link;
  int ret;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback request\n");

  link = fptr_sp_link_create_from_string(mfi->path);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, invalid Spotify link: %s\n", mfi->path);
      return -1;
    }

  command_init(&cmd);

  cmd.func = playback_play;
  cmd.arg.link = link;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/* Thread: libspotify */
void
spotify_playback_pause_nonblock(void)
{
  struct spotify_command *cmd;

  DPRINTF(E_DBG, L_SPOTIFY, "Nonblock pause request\n");

  cmd = (struct spotify_command *)malloc(sizeof(struct spotify_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not allocate spotify_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct spotify_command));

  cmd->nonblock = 1;

  cmd->func = playback_pause;
  cmd->arg.noarg = NULL;

  nonblock_command(cmd);
}

/* Not used */
int
spotify_playback_resume(void)
{
  struct spotify_command cmd;
  int ret;

  DPRINTF(E_DBG, L_SPOTIFY, "Resume request\n");

  command_init(&cmd);

  cmd.func = playback_resume;
  cmd.arg.noarg = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/* Thread: player and libspotify */
int
spotify_playback_stop(void)
{
  struct spotify_command cmd;
  int ret;

  DPRINTF(E_DBG, L_SPOTIFY, "Stop request\n");

  command_init(&cmd);

  cmd.func = playback_stop;
  cmd.arg.noarg = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/* Thread: player and libspotify */
void
spotify_playback_stop_nonblock(void)
{
  struct spotify_command *cmd;

  DPRINTF(E_DBG, L_SPOTIFY, "Nonblock stop request\n");

  cmd = (struct spotify_command *)malloc(sizeof(struct spotify_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not allocate spotify_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct spotify_command));

  cmd->nonblock = 1;

  cmd->func = playback_stop;
  cmd->arg.noarg = NULL;

  nonblock_command(cmd);
}

/* Thread: player */
int
spotify_playback_seek(int ms)
{
  struct spotify_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_seek;
  cmd.arg.seek_ms = ms;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  if (ret == 0)
    return ms;
  else
    return -1;
}

/* Thread: player */
int
spotify_audio_get(struct evbuffer *evbuf, int wanted)
{
  struct spotify_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = audio_get;
  cmd.arg.audio.evbuf  = evbuf;
  cmd.arg.audio.wanted = wanted;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/* Thread: httpd (artwork) */
int
spotify_artwork_get(struct evbuffer *evbuf, char *path, int max_w, int max_h)
{
  struct spotify_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = artwork_get;
  cmd.arg.artwork.evbuf  = evbuf;
  cmd.arg.artwork.path = path;
  cmd.arg.artwork.max_w = max_w;
  cmd.arg.artwork.max_h = max_h;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

static int
spotify_file_read(char *path, char **username, char **password)
{
  FILE *fp;
  char *u;
  char *p;
  char buf[256];
  int len;

  fp = fopen(path, "rb");
  if (!fp)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not open Spotify credentials file %s: %s\n", path, strerror(errno));
      return -1;
    }

  u = fgets(buf, sizeof(buf), fp);
  if (!u)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Empty Spotify credentials file %s\n", path);

      fclose(fp);
      return -1;
    }

  len = strlen(u);
  if (buf[len - 1] != '\n')
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Invalid Spotify credentials file %s: username name too long or missing password\n", path);

      fclose(fp);
      return -1;
    }

  while (len)
    {
      if ((buf[len - 1] == '\r') || (buf[len - 1] == '\n'))
	{
	  buf[len - 1] = '\0';
	  len--;
	}
      else
	break;
    }

  if (!len)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Invalid Spotify credentials file %s: empty line where username expected\n", path);

      fclose(fp);
      return -1;
    }

  u = strdup(buf);
  if (!u)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for username while reading %s\n", path);

      fclose(fp);
      return -1;
    }

  p = fgets(buf, sizeof(buf), fp);
  fclose(fp);
  if (!p)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Invalid Spotify credentials file %s: no password\n", path);

      free(u);
      return -1;
    }

  len = strlen(p);

  while (len)
    {
      if ((buf[len - 1] == '\r') || (buf[len - 1] == '\n'))
	{
	  buf[len - 1] = '\0';
	  len--;
	}
      else
	break;
    }

  p = strdup(buf);
  if (!p)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for password while reading %s\n", path);

      free(u);
      return -1;
    }

  DPRINTF(E_LOG, L_SPOTIFY, "Spotify credentials file OK, logging in with username %s\n", u);

  *username = u;
  *password = p;

  return 0;
}

/* Thread: filescanner */
void
spotify_login(char *path)
{
  sp_error err;
  char *username;
  char *password;
  int ret;

  if (!g_sess)
    {
      if (!g_libhandle)
	DPRINTF(E_LOG, L_SPOTIFY, "Can't login! - could not find libspotify\n");
      else
	DPRINTF(E_LOG, L_SPOTIFY, "Can't login! - no valid Spotify session\n");

      return;
    }

  if (SP_CONNECTION_STATE_LOGGED_IN == fptr_sp_session_connectionstate(g_sess))
    {
      pthread_mutex_lock(&login_lck);

      DPRINTF(E_LOG, L_SPOTIFY, "Logging out of Spotify (current state is %d)\n", g_state);

      fptr_sp_session_player_unload(g_sess);
      err = fptr_sp_session_logout(g_sess);

      if (SP_ERROR_OK != err)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Could not logout of Spotify: %s\n", fptr_sp_error_message(err));
	  pthread_mutex_unlock(&login_lck);
	  return;
	}

      pthread_cond_wait(&login_cond, &login_lck);
      pthread_mutex_unlock(&login_lck);
    }

  DPRINTF(E_INFO, L_SPOTIFY, "Logging into Spotify\n");
  if (path)
    {
      ret = spotify_file_read(path, &username, &password);
      if (ret < 0)
	return;

      err = fptr_sp_session_login(g_sess, username, password, 1, NULL);
      free(username);
      free(password);
    }
  else
    {
      err = fptr_sp_session_relogin(g_sess);
    }

  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not login into Spotify: %s\n", fptr_sp_error_message(err));
      return;
    }
}

/* Thread: main */
int
spotify_init(void)
{
  cfg_t *spotify_cfg;
  sp_session *sp;
  sp_error err;
  int ret;

  /* Initialize libspotify */
  g_libhandle = dlopen("libspotify.so", RTLD_LAZY);
  if (!g_libhandle)
    {
      DPRINTF(E_INFO, L_SPOTIFY, "libspotify.so not installed or not found\n");
      goto libspotify_fail;
    }

  ret = fptr_assign_all();
  if (ret < 0)
    goto assign_fail;

# if defined(__linux__)
  ret = pipe2(g_exit_pipe, O_CLOEXEC);
# else
  ret = pipe(g_exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create pipe: %s\n", strerror(errno));
      goto exit_fail;
    }

# if defined(__linux__)
  ret = pipe2(g_cmd_pipe, O_CLOEXEC);
# else
  ret = pipe(g_cmd_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create command pipe: %s\n", strerror(errno));
      goto cmd_fail;
    }

# if defined(__linux__)
  ret = pipe2(g_notify_pipe, O_CLOEXEC);
# else
  ret = pipe(g_notify_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not notify command pipe: %s\n", strerror(errno));
      goto notify_fail;
    }

  evbase_spotify = event_base_new();
  if (!evbase_spotify)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create an event base\n");
      goto evbase_fail;
    }

#ifdef HAVE_LIBEVENT2
  g_exitev = event_new(evbase_spotify, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create exit event\n");
      goto evnew_fail;
    }

  g_cmdev = event_new(evbase_spotify, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create cmd event\n");
      goto evnew_fail;
    }

  g_notifyev = event_new(evbase_spotify, g_notify_pipe[0], EV_READ | EV_TIMEOUT, notify_cb, NULL);
  if (!g_notifyev)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create notify event\n");
      goto evnew_fail;
    }
#else
  g_exitev = (struct event *)malloc(sizeof(struct event));
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create exit event\n");
      goto evnew_fail;
    }
  event_set(g_exitev, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  event_base_set(evbase_spotify, g_exitev);

  g_cmdev = (struct event *)malloc(sizeof(struct event));
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create cmd event\n");
      goto evnew_fail;
    }
  event_set(g_cmdev, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  event_base_set(evbase_spotify, g_cmdev);

  g_notifyev = (struct event *)malloc(sizeof(struct event));
  if (!g_notifyev)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create notify event\n");
      goto evnew_fail;
    }
  event_set(g_notifyev, g_notify_pipe[0], EV_READ | EV_TIMEOUT, notify_cb, NULL);
  event_base_set(evbase_spotify, g_notifyev);
#endif

  event_add(g_exitev, NULL);
  event_add(g_cmdev, NULL);
  event_add(g_notifyev, NULL);

  DPRINTF(E_INFO, L_SPOTIFY, "Spotify session init\n");

  spotify_cfg = cfg_getsec(cfg, "spotify");
  spconfig.settings_location = cfg_getstr(spotify_cfg, "settings_dir");
  spconfig.cache_location = cfg_getstr(spotify_cfg, "cache_dir");

  DPRINTF(E_DBG, L_SPOTIFY, "Creating Spotify session\n");
  err = fptr_sp_session_create(&spconfig, &sp);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create Spotify session: %s\n", fptr_sp_error_message(err));
      goto session_fail;
    }

  g_sess = sp;
  g_state = SPOTIFY_STATE_INACTIVE;

  switch (cfg_getint(spotify_cfg, "bitrate"))
    {
      case 1:
	fptr_sp_session_preferred_bitrate(g_sess, SP_BITRATE_96k);
	break;
      case 2:
	fptr_sp_session_preferred_bitrate(g_sess, SP_BITRATE_160k);
	break;
      case 3:
	fptr_sp_session_preferred_bitrate(g_sess, SP_BITRATE_320k);
	break;
    }

  /* Prepare audio buffer */
  g_audio_fifo = (audio_fifo_t *)malloc(sizeof(audio_fifo_t));
  if (!g_audio_fifo)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for audio buffer\n");
      goto audio_fifo_fail;
    }
  TAILQ_INIT(&g_audio_fifo->q);
  g_audio_fifo->qlen = 0;
  pthread_mutex_init(&g_audio_fifo->mutex, NULL);
  pthread_cond_init(&g_audio_fifo->cond, NULL);

  pthread_mutex_init(&login_lck, NULL);
  pthread_cond_init(&login_cond, NULL);

  /* Spawn thread */
  ret = pthread_create(&tid_spotify, NULL, spotify, NULL);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_SPOTIFY, "Could not spawn Spotify thread: %s\n", strerror(errno));
      goto thread_fail;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Spotify init complete\n");
  return 0;

 thread_fail:
  pthread_cond_destroy(&login_cond);
  pthread_mutex_destroy(&login_lck);

  pthread_cond_destroy(&g_audio_fifo->cond);
  pthread_mutex_destroy(&g_audio_fifo->mutex);
  free(g_audio_fifo);  

 audio_fifo_fail:
  fptr_sp_session_release(g_sess);
  g_sess = NULL;
  
 session_fail:
 evnew_fail:
  event_base_free(evbase_spotify);
  evbase_spotify = NULL;

 evbase_fail:
  close(g_notify_pipe[0]);
  close(g_notify_pipe[1]);

 notify_fail:
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);

 cmd_fail:
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);

 exit_fail:
 assign_fail:
  dlclose(g_libhandle);
  g_libhandle = NULL;

 libspotify_fail:
  return -1;
}

void
spotify_deinit(void)
{
  int ret;

  if (!g_libhandle)
    return;

  /* Send exit signal to thread (if active) */
  if (g_state != SPOTIFY_STATE_INACTIVE)
    {
      thread_exit();

      ret = pthread_join(tid_spotify, NULL);
      if (ret != 0)
	{
	  DPRINTF(E_FATAL, L_SPOTIFY, "Could not join Spotify thread: %s\n", strerror(errno));
	  return;
	}
    }

  /* Release session */
  fptr_sp_session_release(g_sess);

  /* Free event base (should free events too) */
  event_base_free(evbase_spotify);

  /* Close pipes */
  close(g_notify_pipe[0]);
  close(g_notify_pipe[1]);
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);

  /* Destroy locks */
  pthread_cond_destroy(&login_cond);
  pthread_mutex_destroy(&login_lck);

  /* Clear audio fifo */
  pthread_cond_destroy(&g_audio_fifo->cond);
  pthread_mutex_destroy(&g_audio_fifo->mutex);
  free(g_audio_fifo);

  /* Release libspotify handle */
  dlclose(g_libhandle);
}
