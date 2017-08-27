/*
 * Copyright (C) 2016 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <stdbool.h>
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
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif

#include <dlfcn.h>
#include <libspotify/api.h>
#include <json.h>

#include "spotify.h"
#include "spotify_webapi.h"
#include "logger.h"
#include "misc.h"
#include "http.h"
#include "conffile.h"
#include "cache.h"
#include "commands.h"
#include "library.h"
#include "input.h"
#include "listener.h"

/* TODO for the web api:
 * - UI should be prettier
 * - map "added_at" to time_added
 * - what to do about the lack of push?
 * - use the web api more, implement proper init
*/

/* A few words on our reloading sequence of saved tracks
 *
 *   1. libspotify will not tell us about the user's saved tracks when loading
 *      so we keep track of them with the special playlist spotify:savedtracks.
 *   2. spotify_login will copy all paths in spotify:savedtracks to a temporary
 *      spotify_reload_list before all Spotify items in the database get purged.
 *   3. when the connection to Spotify is established after login, we register
 *      all the paths with libspotify, and we also add them back to the
 *      spotify:savedtracks playlist - however, that's just for the
 *      playlistsitems table. Adding the items to the files table is done when
 *      libspotify calls back with metadata - see spotify_pending_process().
 *   4. if the user reloads saved tracks, we first clear all items in the
 *      playlist, then add those back that are returned from the web api, and
 *      then use our normal cleanup of stray files to tidy db and cache.
 */

// How long to wait for artwork (in sec) before giving up
#define SPOTIFY_ARTWORK_TIMEOUT 3
// An upper limit on sequential requests to Spotify's web api
// - each request will return 50 objects (tracks)
#define SPOTIFY_WEB_REQUESTS_MAX 20

/* --- Types --- */
enum spotify_state
{
  SPOTIFY_STATE_INACTIVE,
  SPOTIFY_STATE_WAIT,
  SPOTIFY_STATE_PLAYING,
  SPOTIFY_STATE_PAUSED,
  SPOTIFY_STATE_STOPPING,
  SPOTIFY_STATE_STOPPED,
};

struct artwork_get_param
{
  struct evbuffer *evbuf;
  char *path;
  int max_w;
  int max_h;

  sp_image *image;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int is_loaded;
};

/* --- Globals --- */
// Spotify thread
static pthread_t tid_spotify;

// Used to make sure no login is attempted before the logout cb from Spotify
static pthread_mutex_t login_lck;
static pthread_cond_t login_cond;

// Event base, pipes and events
struct event_base *evbase_spotify;
static int g_notify_pipe[2];
static struct event *g_notifyev;

static struct commands_base *cmdbase;

// The session handle
static sp_session *g_sess;
// The library handle
static void *g_libhandle;
// The state telling us what the thread is currently doing
static enum spotify_state g_state;
// The base playlist id for all Spotify playlists in the db
static int spotify_base_plid;
// Flag telling us if access to the web api was granted
static bool spotify_access_token_valid;
// The base playlist id for Spotify saved tracks in the db
static int spotify_saved_plid;

// Flag to avoid triggering playlist change events while the (re)scan is running
static bool scanning;

static pthread_mutex_t status_lck;
static struct spotify_status_info spotify_status_info;

// Timeout timespec
static struct timespec spotify_artwork_timeout = { SPOTIFY_ARTWORK_TIMEOUT, 0 };

// Audio buffer
static struct evbuffer *spotify_audio_buffer;

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
typedef const char*  (*fptr_sp_session_user_name_t)(sp_session *session);

typedef sp_error     (*fptr_sp_playlistcontainer_add_callbacks_t)(sp_playlistcontainer *pc, sp_playlistcontainer_callbacks *callbacks, void *userdata);
typedef int          (*fptr_sp_playlistcontainer_num_playlists_t)(sp_playlistcontainer *pc);
typedef sp_playlist* (*fptr_sp_playlistcontainer_playlist_t)(sp_playlistcontainer *pc, int index);

typedef sp_error     (*fptr_sp_playlist_add_callbacks_t)(sp_playlist *playlist, sp_playlist_callbacks *callbacks, void *userdata);
typedef const char*  (*fptr_sp_playlist_name_t)(sp_playlist *playlist);
typedef sp_error     (*fptr_sp_playlist_remove_callbacks_t)(sp_playlist *playlist, sp_playlist_callbacks *callbacks, void *userdata);
typedef int          (*fptr_sp_playlist_num_tracks_t)(sp_playlist *playlist);
typedef sp_track*    (*fptr_sp_playlist_track_t)(sp_playlist *playlist, int index);
typedef bool         (*fptr_sp_playlist_is_loaded_t)(sp_playlist *playlist);
typedef int          (*fptr_sp_playlist_track_create_time_t)(sp_playlist *playlist, int index);
typedef sp_user*     (*fptr_sp_playlist_owner_t)(sp_playlist *playlist);

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
typedef sp_error     (*fptr_sp_image_add_load_callback_t)(sp_image *image, image_loaded_cb *callback, void *userdata);
typedef sp_error     (*fptr_sp_image_remove_load_callback_t)(sp_image *image, image_loaded_cb *callback, void *userdata);

typedef const char*  (*fptr_sp_user_display_name_t)(sp_user *user);
typedef const char*  (*fptr_sp_user_canonical_name_t)(sp_user *user);

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
fptr_sp_session_user_name_t fptr_sp_session_user_name;

fptr_sp_playlistcontainer_add_callbacks_t fptr_sp_playlistcontainer_add_callbacks;
fptr_sp_playlistcontainer_num_playlists_t fptr_sp_playlistcontainer_num_playlists;
fptr_sp_playlistcontainer_playlist_t fptr_sp_playlistcontainer_playlist;

fptr_sp_playlist_add_callbacks_t fptr_sp_playlist_add_callbacks;
fptr_sp_playlist_name_t fptr_sp_playlist_name;
fptr_sp_playlist_remove_callbacks_t fptr_sp_playlist_remove_callbacks;
fptr_sp_playlist_num_tracks_t fptr_sp_playlist_num_tracks;
fptr_sp_playlist_track_t fptr_sp_playlist_track;
fptr_sp_playlist_is_loaded_t fptr_sp_playlist_is_loaded;
fptr_sp_playlist_track_create_time_t fptr_sp_playlist_track_create_time;
fptr_sp_playlist_owner_t fptr_sp_playlist_owner;

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
fptr_sp_image_add_load_callback_t fptr_sp_image_add_load_callback;
fptr_sp_image_remove_load_callback_t fptr_sp_image_remove_load_callback;

fptr_sp_user_display_name_t fptr_sp_user_display_name;
fptr_sp_user_canonical_name_t fptr_sp_user_canonical_name;

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
   && (fptr_sp_session_user_name = dlsym(h, "sp_session_user_name"))
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
   && (fptr_sp_playlist_track_create_time = dlsym(h, "sp_playlist_track_create_time"))
   && (fptr_sp_playlist_owner = dlsym(h, "sp_playlist_owner"))
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
   && (fptr_sp_image_add_load_callback = dlsym(h, "sp_image_add_load_callback"))
   && (fptr_sp_image_remove_load_callback = dlsym(h, "sp_image_remove_load_callback"))
   && (fptr_sp_user_display_name = dlsym(h, "sp_user_display_name"))
   && (fptr_sp_user_canonical_name = dlsym(h, "sp_user_canonical_name"))
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


static enum command_state
webapi_scan(void *arg, int *ret);
static enum command_state
webapi_pl_save(void *arg, int *ret);
static enum command_state
webapi_pl_remove(void *arg, int *ret);
static void
create_base_playlist();


/* --------------------------  PLAYLIST HELPERS    ------------------------- */
/*            Should only be called from within the spotify thread           */

static int
spotify_metadata_get(sp_track *track, struct media_file_info *mfi, const char *pltitle, int time_added)
{
  cfg_t *spotify_cfg;
  bool artist_override;
  bool album_override;
  sp_album *album;
  sp_artist *artist;
  sp_albumtype albumtype;
  bool starred;
  int compilation;
  char *albumname;

  spotify_cfg = cfg_getsec(cfg, "spotify");
  artist_override = cfg_getbool(spotify_cfg, "artist_override");
  album_override = cfg_getbool(spotify_cfg, "album_override");

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
		  || artist_override);

  if (album_override && pltitle)
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
  mfi->time_added  = time_added;

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

/*
 * Returns the directory id for /spotify:/<artist>/<album>, if the directory (or the parent
 * directories) does not yet exist, they will be created.
 * If an error occured the return value is -1.
 *
 * @return directory id for the given artist/album directory
 */
static int
prepare_directories(const char *artist, const char *album)
{
  int dir_id;
  char virtual_path[PATH_MAX];
  int ret;

  ret = snprintf(virtual_path, sizeof(virtual_path), "/spotify:/%s", artist);
  if ((ret < 0) || (ret >= sizeof(virtual_path)))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Virtual path exceeds PATH_MAX (/spotify:/%s)\n", artist);
      return -1;
    }
  dir_id = db_directory_addorupdate(virtual_path, 0, DIR_SPOTIFY);
  if (dir_id <= 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not add or update directory '%s'\n", virtual_path);
      return -1;
    }
  ret = snprintf(virtual_path, sizeof(virtual_path), "/spotify:/%s/%s", artist, album);
  if ((ret < 0) || (ret >= sizeof(virtual_path)))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Virtual path exceeds PATH_MAX (/spotify:/%s/%s)\n", artist, album);
      return -1;
    }
  dir_id = db_directory_addorupdate(virtual_path, 0, dir_id);
  if (dir_id <= 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not add or update directory '%s'\n", virtual_path);
      return -1;
    }

  return dir_id;
}

static int
spotify_track_save(int plid, sp_track *track, const char *pltitle, int time_added)
{
  struct media_file_info mfi;
  sp_link *link;
  char url[1024];
  int ret;
  char virtual_path[PATH_MAX];
  int dir_id;
  time_t stamp;
  int id;

  memset(&mfi, 0, sizeof(struct media_file_info));

  if (!fptr_sp_track_is_loaded(track))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Track appears to no longer have the proper status\n");
      return -1;
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
  if (plid)
    {
      ret = db_pl_add_item_bypath(plid, url);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Could not save playlist item: '%s'\n", url);
	  goto fail;
	}
    }

  ret = spotify_metadata_get(track, &mfi, pltitle, time_added);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Metadata missing (but track should be loaded?): '%s'\n", fptr_sp_track_name(track));
      goto fail;
    }

  dir_id = prepare_directories(mfi.artist, mfi.album);
  if (dir_id <= 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not add or update directory for item: '%s'\n", url);
      goto fail;
    }

//  DPRINTF(E_DBG, L_SPOTIFY, "Saving track '%s': '%s' by %s (%s)\n", url, mfi.title, mfi.artist, mfi.album);

  db_file_stamp_bypath(url, &stamp, &id);

  mfi.id = id;
  mfi.path = strdup(url);
  mfi.fname = strdup(url);
  mfi.time_modified = time(NULL);
  mfi.data_kind = DATA_KIND_SPOTIFY;
  snprintf(virtual_path, PATH_MAX, "/spotify:/%s/%s/%s", mfi.album_artist, mfi.album, mfi.title);
  mfi.virtual_path = strdup(virtual_path);
  mfi.directory_id = dir_id;

  library_add_media(&mfi);

  free_mfi(&mfi, 1);

  return 0;

 fail:
  free_mfi(&mfi, 1);
  return -1;
}

static int
spotify_cleanup_files(void)
{
  struct query_params qp;
  char *path;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_BROWSE_PATH;
  qp.sort = S_NONE;
  qp.filter = "f.path LIKE 'spotify:%%' AND NOT f.path IN (SELECT filepath FROM playlistitems)";

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      return -1;
    }

  while (((ret = db_query_fetch_string(&qp, &path)) == 0) && (path))
    {
      cache_artwork_delete_by_path(path);
    }

  db_query_end(&qp);

  db_spotify_files_delete();

  return 0;
}

static int
spotify_playlist_save(sp_playlist *pl)
{
  struct playlist_info *pli;
  sp_track *track;
  sp_link *link;
  sp_user *owner;
  char url[1024];
  const char *name;
  const char *ownername;
  int plid;
  int num_tracks;
  char virtual_path[PATH_MAX];
  int created;
  int ret;
  int i;
  
  if (!fptr_sp_playlist_is_loaded(pl))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist still not loaded - will wait for next callback\n");
      return 0;
    }

  name = fptr_sp_playlist_name(pl);
  num_tracks = fptr_sp_playlist_num_tracks(pl);

  // The starred playlist has an empty name, set it manually to "Starred"
  if (*name == '\0')
    name = "Starred";

  for (i = 0; i < num_tracks; i++)
    {
      track = fptr_sp_playlist_track(pl, i);

      if (track && !fptr_sp_track_is_loaded(track))
	{
	  DPRINTF(E_DBG, L_SPOTIFY, "All playlist tracks not loaded (will wait for next callback): %s\n", name);
	  return 0;
	}
    }

  DPRINTF(E_LOG, L_SPOTIFY, "Saving playlist (%d tracks): '%s'\n", num_tracks, name);

  // Save playlist (playlists table)
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

  owner = fptr_sp_playlist_owner(pl);
  if (owner)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist '%s' owner: '%s' (canonical) / '%s' (display)\n",
	  name, fptr_sp_user_canonical_name(owner), fptr_sp_user_display_name(owner));

      ownername = fptr_sp_user_canonical_name(owner);

      snprintf(virtual_path, PATH_MAX, "/spotify:/%s (%s)", name, ownername);
    }
  else
    {
      snprintf(virtual_path, PATH_MAX, "/spotify:/%s", name);
    }


  pli = db_pl_fetch_bypath(url);

  if (pli)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist found ('%s', link %s), updating\n", name, url);

      plid = pli->id;

      free(pli->title);
      pli->title = strdup(name);
      free(pli->virtual_path);
      pli->virtual_path = strdup(virtual_path);
      pli->directory_id = DIR_SPOTIFY;

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
      pli->parent_id = spotify_base_plid;
      pli->directory_id = DIR_SPOTIFY;

      ret = db_pl_add(pli, &plid);
      if ((ret < 0) || (plid < 1))
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error adding playlist ('%s', link %s, ret %d, plid %d)\n", name, url, ret, plid);

	  free_pli(pli, 0);
	  return -1;
	}
    }

  free_pli(pli, 0);

  // Save tracks and playlistitems (files and playlistitems table)
  db_transaction_begin();
  for (i = 0; i < num_tracks; i++)
    {
      track = fptr_sp_playlist_track(pl, i);
      if (!track)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Track %d in playlist '%s' (id %d) is invalid\n", i, name, plid);
	  continue;
	}

      created = fptr_sp_playlist_track_create_time(pl, i);

      ret = spotify_track_save(plid, track, name, created);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error saving track %d to playlist '%s' (id %d)\n", i, name, plid);
	  continue;
	}
    }

  spotify_cleanup_files();
  db_transaction_end();

  return plid;
}

// Registers a track with libspotify, which will make it start loading the track
// metadata. When that is done metadata_updated() is called (but we won't be
// told which track it was...). Note that this function will result in a ref
// count on the sp_link, which the caller must decrease with sp_link_release.
static enum command_state
uri_register(void *arg, int *retval)
{
  sp_link *link;
  sp_track *track;

  char *uri = arg;

  if (SP_CONNECTION_STATE_LOGGED_IN != fptr_sp_session_connectionstate(g_sess))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Can't register music, not connected and logged in to Spotify\n");
      *retval = -1;
      return COMMAND_END;
    }

  link = fptr_sp_link_create_from_string(uri);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Invalid Spotify link: '%s'\n", uri);
      *retval = -1;
      return COMMAND_END;
    }

  track = fptr_sp_link_as_track(link);
  if (!track)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Invalid Spotify track: '%s'\n", uri);
      *retval = -1;
      return COMMAND_END;
    }

  *retval = 0;
  return COMMAND_END;
}

static void
webapi_playlist_updated(sp_playlist *pl)
{
  sp_link *link;
  char url[1024];
  int ret;

  if (!scanning)
    {
      // Run playlist save in the library thread
      link = fptr_sp_link_create_from_playlist(pl);
      if (!link)
        {
	  DPRINTF(E_LOG, L_SPOTIFY, "Could not create link for playlist: '%s'\n", fptr_sp_playlist_name(pl));
	  return;
	}

      ret = fptr_sp_link_as_string(link, url, sizeof(url));
      if (ret == sizeof(url))
        {
	  DPRINTF(E_DBG, L_SPOTIFY, "Spotify link truncated: %s\n", url);
	}
      fptr_sp_link_release(link);

      library_exec_async(webapi_pl_save, strdup(url));
    }
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

      if (spotify_access_token_valid)
	{
	  webapi_playlist_updated(pl);
	}
      else
	{
	  spotify_playlist_save(pl);
	}
    }
}

static void playlist_metadata_updated(sp_playlist *pl, void *userdata)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Playlist metadata updated: %s\n", fptr_sp_playlist_name(pl));

  if (spotify_access_token_valid)
    {
      //TODO Update disabled to prevent multiple triggering of updates e. g. on adding a playlist
      //webapi_playlist_updated(pl);
    }
  else
    {
      spotify_playlist_save(pl);
    }
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

  if (spotify_access_token_valid)
    {
      webapi_playlist_updated(pl);
    }
  else
    {
      spotify_playlist_save(pl);
    }
}

static int
playlist_remove(const char *uri)
{
  struct playlist_info *pli;
  int plid;

  pli = db_pl_fetch_bypath(uri);

  if (!pli)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playlist '%s' not found, can't delete\n", uri);
      return -1;
    }

  DPRINTF(E_LOG, L_SPOTIFY, "Removing playlist '%s' (%s)\n", pli->title, uri);

  plid = pli->id;

  free_pli(pli, 0);

  db_spotify_pl_delete(plid);
  spotify_cleanup_files();
  return 0;
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
  sp_link *link;
  char url[1024];
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

  if (spotify_access_token_valid)
    {
      // Run playlist remove in the library thread
      if (!scanning)
	library_exec_async(webapi_pl_remove, strdup(url));
    }
  else
    {
      playlist_remove(url);
    }
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

static enum command_state
playback_setup(void *arg, int *retval)
{
  sp_link *link;
  sp_track *track;
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Setting up for playback\n");

  link = (sp_link *) arg;

  if (SP_CONNECTION_STATE_LOGGED_IN != fptr_sp_session_connectionstate(g_sess))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Can't play music, not connected and logged in to Spotify\n");
      *retval = -1;
      return COMMAND_END;
    }

  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, no Spotify link\n");
      *retval = -1;
      return COMMAND_END;
    }

  track = fptr_sp_link_as_track(link);
  if (!track)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, invalid Spotify track\n");
      *retval = -1;
      return COMMAND_END;
    }

  err = fptr_sp_session_player_load(g_sess, track);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed: %s\n", fptr_sp_error_message(err));
      *retval = -1;
      return COMMAND_END;
    }

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_play(void *arg, int *retval)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Starting playback\n");

  err = fptr_sp_session_player_play(g_sess, 1);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback failed: %s\n", fptr_sp_error_message(err));
      *retval = -1;
      return COMMAND_END;
    }

  g_state = SPOTIFY_STATE_PLAYING;

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_pause(void *arg, int *retval)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Pausing playback\n");

  err = fptr_sp_session_player_play(g_sess, 0);
  DPRINTF(E_DBG, L_SPOTIFY, "Playback paused\n");

  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback pause failed: %s\n", fptr_sp_error_message(err));
      *retval = -1;
      return COMMAND_END;
    }

  g_state = SPOTIFY_STATE_PAUSED;

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_stop(void *arg, int *retval)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Stopping playback\n");

  err = fptr_sp_session_player_unload(g_sess);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback stop failed: %s\n", fptr_sp_error_message(err));
      *retval = -1;
      return COMMAND_END;
    }

  g_state = SPOTIFY_STATE_STOPPED;

  evbuffer_drain(spotify_audio_buffer, evbuffer_get_length(spotify_audio_buffer));

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_seek(void *arg, int *retval)
{
  int seek_ms;
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback seek\n");

  seek_ms = *((int *) arg);

  err = fptr_sp_session_player_seek(g_sess, seek_ms);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not seek: %s\n", fptr_sp_error_message(err));
      *retval = -1;
      return COMMAND_END;
    }

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_eot(void *arg, int *retval)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback end of track\n");

  err = fptr_sp_session_player_unload(g_sess);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback end of track failed: %s\n", fptr_sp_error_message(err));
      *retval = -1;
      return COMMAND_END;
    }

  g_state = SPOTIFY_STATE_STOPPING;

  // TODO 1) This will block for a while, but perhaps ok?
  input_write(spotify_audio_buffer, INPUT_FLAG_EOF);

  *retval = 0;
  return COMMAND_END;
}

static void
artwork_loaded_cb(sp_image *image, void *userdata)
{
  struct artwork_get_param *artwork;

  artwork = userdata;

  CHECK_ERR(L_SPOTIFY, pthread_mutex_lock(&artwork->mutex));

  artwork->is_loaded = 1;

  CHECK_ERR(L_SPOTIFY, pthread_cond_signal(&artwork->cond));
  CHECK_ERR(L_SPOTIFY, pthread_mutex_unlock(&artwork->mutex));
}

static enum command_state
artwork_get_bh(void *arg, int *retval)
{
  struct artwork_get_param *artwork;
  sp_imageformat imageformat;
  sp_error err;
  const void *data;
  size_t data_size;
  int ret;

  artwork = arg;
  sp_image *image = artwork->image;
  char *path = artwork->path;

  fptr_sp_image_remove_load_callback(image, artwork_loaded_cb, artwork);

  err = fptr_sp_image_error(image);
  if (err != SP_ERROR_OK)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Getting artwork (%s) failed, Spotify error: %s\n", path, fptr_sp_error_message(err));
      goto fail;
    }

  if (!fptr_sp_image_is_loaded(image))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Load callback returned, but no image? Possible bug: %s\n", path);
      goto fail;
    }

  imageformat = fptr_sp_image_format(image);
  if (imageformat != SP_IMAGE_FORMAT_JPEG)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Getting artwork failed, invalid image format from Spotify: %s\n", path);
      goto fail;
    }

  data = fptr_sp_image_data(image, &data_size);
  if (!data || (data_size == 0))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Getting artwork failed, no image data from Spotify: %s\n", path);
      goto fail;
    }

  ret = evbuffer_expand(artwork->evbuf, data_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for artwork\n");
      goto fail;
    }

  ret = evbuffer_add(artwork->evbuf, data, data_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not add Spotify image to event buffer\n");
      goto fail;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Spotify artwork loaded ok\n");

  fptr_sp_image_release(image);

  *retval = 0;
  return COMMAND_END;

 fail:
  fptr_sp_image_release(image);

  *retval = -1;
  return COMMAND_END;
}

static enum command_state
artwork_get(void *arg, int *retval)
{
  struct artwork_get_param *artwork;
  char *path;
  sp_link *link;
  sp_track *track;
  sp_album *album;
  const byte *image_id;
  sp_image *image;
  sp_image_size image_size;
  sp_error err;

  artwork = arg;
  path = artwork->path;

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
  if ((artwork->max_w > 64) || (artwork->max_h > 64))
    image_size = SP_IMAGE_SIZE_NORMAL; // 300x300
  if ((artwork->max_w > 300) || (artwork->max_h > 300))
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

  fptr_sp_link_release(link);

  artwork->image = image;
  artwork->is_loaded = fptr_sp_image_is_loaded(image);

  /* If the image is ready we can return it straight away, otherwise we will
   * let the calling thread wait, since the Spotify thread should not wait
   */
  if (artwork->is_loaded)
    return artwork_get_bh(artwork, retval);

  DPRINTF(E_SPAM, L_SPOTIFY, "Will wait for Spotify to call artwork_loaded_cb\n");

  /* Async - we will return to spotify_artwork_get which will wait for callback */
  err = fptr_sp_image_add_load_callback(image, artwork_loaded_cb, artwork);
  if (err != SP_ERROR_OK)
    {
      DPRINTF(E_WARN, L_SPOTIFY, "Adding artwork cb failed, Spotify error: %s\n", fptr_sp_error_message(err));
      *retval = -1;
      return COMMAND_END;
    }

  *retval = 0;
  return COMMAND_END;

 level2_exit:
  fptr_sp_link_release(link);

 level1_exit:
  *retval = -1;
  return COMMAND_END;
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
  sp_playlist *pl;
  sp_playlistcontainer *pc;
  int i;

  if (SP_ERROR_OK != error)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Login failed: %s\n",	fptr_sp_error_message(error));

      CHECK_ERR(L_REMOTE, pthread_mutex_lock(&status_lck));
      spotify_status_info.libspotify_logged_in = false;
      spotify_status_info.libspotify_user[0] = '\0';
      CHECK_ERR(L_REMOTE, pthread_mutex_unlock(&status_lck));

      listener_notify(LISTENER_SPOTIFY);

      return;
    }

  DPRINTF(E_LOG, L_SPOTIFY, "Login to Spotify succeeded, reloading playlists\n");

  if (!spotify_access_token_valid)
    create_base_playlist();

  db_directory_enable_bypath("/spotify:");

  pl = fptr_sp_session_starred_create(sess);
  fptr_sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);

  pc = fptr_sp_session_playlistcontainer(sess);

  fptr_sp_playlistcontainer_add_callbacks(pc, &pc_callbacks, NULL);

  DPRINTF(E_DBG, L_SPOTIFY, "Found %d playlists\n", fptr_sp_playlistcontainer_num_playlists(pc));

  for (i = 0; i < fptr_sp_playlistcontainer_num_playlists(pc); i++)
    {
      pl = fptr_sp_playlistcontainer_playlist(pc, i);
      fptr_sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);
    }

  CHECK_ERR(L_REMOTE, pthread_mutex_lock(&status_lck));
  spotify_status_info.libspotify_logged_in = true;
  snprintf(spotify_status_info.libspotify_user, sizeof(spotify_status_info.libspotify_user), "%s", fptr_sp_session_user_name(sess));
  spotify_status_info.libspotify_user[sizeof(spotify_status_info.libspotify_user) - 1] = '\0';
  CHECK_ERR(L_REMOTE, pthread_mutex_unlock(&status_lck));

  listener_notify(LISTENER_SPOTIFY);
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

  CHECK_ERR(L_REMOTE, pthread_mutex_lock(&status_lck));
  spotify_status_info.libspotify_logged_in = false;
  spotify_status_info.libspotify_user[0] = '\0';
  CHECK_ERR(L_REMOTE, pthread_mutex_unlock(&status_lck));

  CHECK_ERR(L_SPOTIFY, pthread_mutex_lock(&login_lck));

  CHECK_ERR(L_SPOTIFY, pthread_cond_signal(&login_cond));
  CHECK_ERR(L_SPOTIFY, pthread_mutex_unlock(&login_lck));

  listener_notify(LISTENER_SPOTIFY);
}

/**
 * This callback is used from libspotify whenever there is PCM data available.
 *
 * @sa sp_session_callbacks#music_delivery
 */
static int music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
  size_t size;
  int ret;

  /* No support for resampling right now */
  if ((format->sample_rate != 44100) || (format->channels != 2))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Got music with unsupported samplerate or channels, stopping playback\n");
      spotify_playback_stop_nonblock();
      return num_frames;
    }

  // Audio discontinuity, e.g. seek
  if (num_frames == 0)
    {
      evbuffer_drain(spotify_audio_buffer, evbuffer_get_length(spotify_audio_buffer));
      return 0;
    }

  size = num_frames * sizeof(int16_t) * format->channels;

  ret = evbuffer_add(spotify_audio_buffer, frames, size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory adding audio to buffer\n");
      return num_frames;
    }

  // The input buffer only accepts writing when it is approaching depletion, and
  // because we use NONBLOCK it will just return if this is not the case. So in
  // most cases no actual write is made and spotify_audio_buffer will just grow.
  input_write(spotify_audio_buffer, INPUT_FLAG_NONBLOCK);

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
      DPRINTF(E_LOG, L_SPOTIFY, "Connection to Spotify (re)established, reloading saved tracks\n");
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
  DPRINTF(E_DBG, L_SPOTIFY, "End of track\n");

  commands_exec_async(cmdbase, playback_eot, NULL);
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
exit_cb()
{
  fptr_sp_session_player_unload(g_sess);
  fptr_sp_session_logout(g_sess);
  g_state = SPOTIFY_STATE_INACTIVE;
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
spotify_playback_setup(const char *path)
{
  sp_link *link;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback setup request\n");

  link = fptr_sp_link_create_from_string(path);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, invalid Spotify link: %s\n", path);
      return -1;
    }

  return commands_exec_sync(cmdbase, playback_setup, NULL, link);
}

int
spotify_playback_play()
{
  DPRINTF(E_DBG, L_SPOTIFY, "Playback request\n");

  return commands_exec_sync(cmdbase, playback_play, NULL, NULL);
}

int
spotify_playback_pause()
{
  DPRINTF(E_DBG, L_SPOTIFY, "Pause request\n");

  return commands_exec_sync(cmdbase, playback_pause, NULL, NULL);
}

/* Thread: libspotify */
void
spotify_playback_pause_nonblock(void)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Nonblock pause request\n");

  commands_exec_async(cmdbase, playback_pause, NULL);
}

/* Thread: player and libspotify */
int
spotify_playback_stop(void)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Stop request\n");

  return commands_exec_sync(cmdbase, playback_stop, NULL, NULL);
}

/* Thread: player and libspotify */
void
spotify_playback_stop_nonblock(void)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Nonblock stop request\n");

  commands_exec_async(cmdbase, playback_stop, NULL);
}

/* Thread: player */
int
spotify_playback_seek(int ms)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_seek, NULL, &ms);

  if (ret == 0)
    return ms;
  else
    return -1;
}

/* Thread: httpd (artwork) and worker */
int
spotify_artwork_get(struct evbuffer *evbuf, char *path, int max_w, int max_h)
{
  struct artwork_get_param artwork;
  struct timespec ts;
  int ret;

  artwork.evbuf  = evbuf;
  artwork.path = path;
  artwork.max_w = max_w;
  artwork.max_h = max_h;

  CHECK_ERR(L_SPOTIFY, mutex_init(&artwork.mutex));
  CHECK_ERR(L_SPOTIFY, pthread_cond_init(&artwork.cond, NULL));

  ret = commands_exec_sync(cmdbase, artwork_get, NULL, &artwork);

  // Artwork was not ready, wait for callback from libspotify
  if (ret == 0)
    {
      CHECK_ERR(L_SPOTIFY, pthread_mutex_lock(&artwork.mutex));
      ts = timespec_reltoabs(spotify_artwork_timeout);
      while ((!artwork.is_loaded) && (ret != ETIMEDOUT))
	CHECK_ERR_EXCEPT(L_SPOTIFY, pthread_cond_timedwait(&artwork.cond, &artwork.mutex, &ts), ret, ETIMEDOUT);
      if (ret == ETIMEDOUT)
	DPRINTF(E_LOG, L_SPOTIFY, "Timeout waiting for artwork from Spotify\n");
      CHECK_ERR(L_SPOTIFY, pthread_mutex_unlock(&artwork.mutex));

      ret = commands_exec_sync(cmdbase, artwork_get_bh, NULL, &artwork);
    }

  return ret;
}

/* Thread: httpd */
void
spotify_oauth_interface(struct evbuffer *evbuf, const char *redirect_uri)
{
  char *uri;

  uri = spotifywebapi_oauth_uri_get(redirect_uri);
  if (!uri)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Cannot display Spotify oath interface (http_form_uriencode() failed)\n");
      return;
    }

  evbuffer_add_printf(evbuf, "<a href=\"%s\">Click here to authorize forked-daapd with Spotify</a>\n", uri);

  free(uri);
}

/* Thread: httpd */
void
spotify_oauth_callback(struct evbuffer *evbuf, struct evkeyvalq *param, const char *redirect_uri)
{
  const char *code;
  const char *err;
  char *user = NULL;
  int ret;

  code = evhttp_find_header(param, "code");
  if (!code)
    {
      evbuffer_add_printf(evbuf, "Error: Didn't receive a code from Spotify\n");
      return;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Received OAuth code: %s\n", code);

  evbuffer_add_printf(evbuf, "<p>Requesting access token from Spotify...\n");

  ret = spotifywebapi_token_get(code, redirect_uri, &user, &err);
  if (ret < 0)
    {
      evbuffer_add_printf(evbuf, "failed</p>\n<p>Error: %s</p>\n", err);
      return;
    }

  // Received a valid access token
  spotify_access_token_valid = true;

  CHECK_ERR(L_REMOTE, pthread_mutex_lock(&status_lck));
  spotify_status_info.webapi_token_valid = spotify_access_token_valid;
  if (user)
    {
      snprintf(spotify_status_info.webapi_user, sizeof(spotify_status_info.webapi_user), "%s", user);
      spotify_status_info.webapi_user[sizeof(spotify_status_info.webapi_user) - 1] = '\0';
      free(user);
    }
  CHECK_ERR(L_REMOTE, pthread_mutex_unlock(&status_lck));

  // Trigger scan after successful access to spotifywebapi
  library_exec_async(webapi_scan, NULL);

  evbuffer_add_printf(evbuf, "ok, all done</p>\n");

  listener_notify(LISTENER_SPOTIFY);

  return;
}

static void
spotify_uri_register(const char *uri)
{
  char *tmp;

  tmp = strdup(uri);
  commands_exec_async(cmdbase, uri_register, tmp);
}

void
spotify_status_info_get(struct spotify_status_info *info)
{
  CHECK_ERR(L_REMOTE, pthread_mutex_lock(&status_lck));
  memcpy(info, &spotify_status_info, sizeof(struct spotify_status_info));
  CHECK_ERR(L_REMOTE, pthread_mutex_unlock(&status_lck));
}

/* Thread: library, httpd */
int
spotify_login_user(const char *user, const char *password, char **errmsg)
{
  sp_error err;

  if (!g_sess)
    {
      if (!g_libhandle)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Can't login! - could not find libspotify\n");
	  if (errmsg)
	    *errmsg = safe_asprintf("Could not find libspotify");
	}
      else
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Can't login! - no valid Spotify session\n");
	  if (errmsg)
	    *errmsg = safe_asprintf("No valid Spotify session");
	}

      return -1;
    }

  if (SP_CONNECTION_STATE_LOGGED_IN == fptr_sp_session_connectionstate(g_sess))
    {
      CHECK_ERR(L_SPOTIFY, pthread_mutex_lock(&login_lck));

      DPRINTF(E_LOG, L_SPOTIFY, "Logging out of Spotify (current state is %d)\n", g_state);

      fptr_sp_session_player_unload(g_sess);
      err = fptr_sp_session_logout(g_sess);

      if (SP_ERROR_OK != err)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Could not logout of Spotify: %s\n", fptr_sp_error_message(err));
	  if (errmsg)
	    *errmsg = safe_asprintf("Could not logout of Spotify: %s", fptr_sp_error_message(err));

	  CHECK_ERR(L_SPOTIFY, pthread_mutex_unlock(&login_lck));
	  return -1;
	}

      CHECK_ERR(L_SPOTIFY, pthread_cond_wait(&login_cond, &login_lck));
      CHECK_ERR(L_SPOTIFY, pthread_mutex_unlock(&login_lck));
    }

  if (user && password)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Spotify credentials file OK, logging in with username %s\n", user);
      err = fptr_sp_session_login(g_sess, user, password, 1, NULL);
    }
  else
    {
      DPRINTF(E_INFO, L_SPOTIFY, "Relogin to Spotify\n");
      err = fptr_sp_session_relogin(g_sess);
    }

  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not login into Spotify: %s\n", fptr_sp_error_message(err));
      if (errmsg)
	*errmsg = safe_asprintf("Could not login into Spotify: %s", fptr_sp_error_message(err));
      return -1;
    }

  return 0;
}

/* Thread: library */
void
spotify_login(char **arglist)
{
  if (arglist)
    spotify_login_user(arglist[0], arglist[1], NULL);
  else
    spotify_login_user(NULL, NULL, NULL);
}

static void
map_track_to_mfi(const struct spotify_track *track, struct media_file_info* mfi)
{
  mfi->title = safe_strdup(track->name);
  mfi->album = safe_strdup(track->album);
  mfi->artist = safe_strdup(track->artist);
  mfi->album_artist = safe_strdup(track->album_artist);
  mfi->disc = track->disc_number;
  mfi->song_length = track->duration_ms;
  mfi->track = track->track_number;
  mfi->compilation = track->is_compilation;

  mfi->artwork     = ARTWORK_SPOTIFY;
  mfi->type        = strdup("spotify");
  mfi->codectype   = strdup("wav");
  mfi->description = strdup("Spotify audio");

  mfi->path = strdup(track->uri);
  mfi->fname = strdup(track->uri);
}

static void
map_album_to_mfi(const struct spotify_album *album, struct media_file_info* mfi)
{
  mfi->album = safe_strdup(album->name);
  mfi->album_artist = safe_strdup(album->artist);
  mfi->genre = safe_strdup(album->genre);
  mfi->compilation = album->is_compilation;
  mfi->year = album->release_year;
  mfi->time_modified = album->mtime;
}

/* Thread: library */
static int
scan_saved_albums()
{
  struct spotify_request request;
  json_object *jsontracks;
  int track_count;
  struct spotify_album album;
  struct spotify_track track;
  struct media_file_info mfi;
  char virtual_path[PATH_MAX];
  int dir_id;
  time_t stamp;
  int id;
  int i;
  int count;
  int ret;

  count = 0;
  memset(&request, 0, sizeof(struct spotify_request));

  while (0 == spotifywebapi_request_next(&request, SPOTIFY_WEBAPI_SAVED_ALBUMS, false))
    {
      while (0 == spotifywebapi_saved_albums_fetch(&request, &jsontracks, &track_count, &album))
	{
	  DPRINTF(E_DBG, L_SPOTIFY, "Got saved album: '%s' - '%s' (%s) - track-count: %d\n",
		  album.artist, album.name, album.uri, track_count);

	  db_transaction_begin();

	  dir_id = prepare_directories(album.artist, album.name);
	  ret = 0;
	  for (i = 0; i < track_count && ret == 0; i++)
	    {
	      ret = spotifywebapi_album_track_fetch(jsontracks, i, &track);
	      if (ret == 0 && track.uri)
		{
		  db_file_stamp_bypath(track.uri, &stamp, &id);
		  if (stamp && (stamp >= track.mtime))
		    {
		      db_file_ping(id);
		    }
		  else
		    {
		      memset(&mfi, 0, sizeof(struct media_file_info));

		      mfi.id = id;

		      map_track_to_mfi(&track, &mfi);
		      map_album_to_mfi(&album, &mfi);

		      mfi.data_kind = DATA_KIND_SPOTIFY;
		      snprintf(virtual_path, PATH_MAX, "/spotify:/%s/%s/%s", mfi.album_artist, mfi.album, mfi.title);
		      mfi.virtual_path = strdup(virtual_path);
		      mfi.directory_id = dir_id;

		      library_add_media(&mfi);

		      free_mfi(&mfi, 1);
		    }

		  spotify_uri_register(track.uri);

		  cache_artwork_ping(track.uri, album.mtime, 0);

		  if (spotify_saved_plid)
		    db_pl_add_item_bypath(spotify_saved_plid, track.uri);
		}
	    }

	  db_transaction_end();

	  count++;
	  if (count >= request.total || (count % 10 == 0))
	    DPRINTF(E_LOG, L_SPOTIFY, "Scanned %d of %d saved albums\n", count, request.total);
	}
    }

  spotifywebapi_request_end(&request);

  return 0;
}

/* Thread: library */
static int
scan_playlisttracks(struct spotify_playlist *playlist, int plid)
{
  cfg_t *spotify_cfg;
  bool artist_override;
  bool album_override;
  struct spotify_request request;
  struct spotify_track track;
  struct media_file_info mfi;
  char virtual_path[PATH_MAX];
  int dir_id;
  time_t stamp;
  int id;

  memset(&request, 0, sizeof(struct spotify_request));

  spotify_cfg = cfg_getsec(cfg, "spotify");
  artist_override = cfg_getbool(spotify_cfg, "artist_override");
  album_override = cfg_getbool(spotify_cfg, "album_override");

  while (0 == spotifywebapi_request_next(&request, playlist->tracks_href, true))
    {
      db_transaction_begin();

//      DPRINTF(E_DBG, L_SPOTIFY, "Playlist tracks\n%s\n", request.response_body);
      while (0 == spotifywebapi_playlisttracks_fetch(&request, &track))
	{
	  DPRINTF(E_DBG, L_SPOTIFY, "Got playlist track: '%s' (%s) \n", track.name, track.uri);

	  if (!track.is_playable)
	    {
	      DPRINTF(E_LOG, L_SPOTIFY, "Track not available for playback: '%s' - '%s' (%s) (restrictions: %s)\n", track.artist, track.name, track.uri, track.restrictions);
	      continue;
	    }

	  if (track.uri)
	    {
	      if (track.linked_from_uri)
		DPRINTF(E_DBG, L_SPOTIFY, "Track '%s' (%s) linked from %s\n", track.name, track.uri, track.linked_from_uri);

	      db_file_stamp_bypath(track.uri, &stamp, &id);
	      if (stamp)
		{
		  db_file_ping(id);
		}
	      else
		{
		  memset(&mfi, 0, sizeof(struct media_file_info));

		  mfi.id = id;

		  dir_id = prepare_directories(track.album_artist, track.album);

		  map_track_to_mfi(&track, &mfi);

		  mfi.compilation = (track.is_compilation || artist_override);
		  if (album_override)
		    {
		      free(mfi.album);
		      mfi.album = strdup(playlist->name);
		    }

		  mfi.time_modified = time(NULL);
		  mfi.data_kind = DATA_KIND_SPOTIFY;
		  snprintf(virtual_path, PATH_MAX, "/spotify:/%s/%s/%s", mfi.album_artist, mfi.album, mfi.title);
		  mfi.virtual_path = strdup(virtual_path);
		  mfi.directory_id = dir_id;

		  library_add_media(&mfi);
		  free_mfi(&mfi, 1);
		}

	      spotify_uri_register(track.uri);
	      cache_artwork_ping(track.uri, 1, 0);
	      db_pl_add_item_bypath(plid, track.uri);
	    }
	}

      db_transaction_end();
    }

  spotifywebapi_request_end(&request);

  return 0;
}

/* Thread: library */
static int
scan_playlists()
{
  struct spotify_request request;
  struct spotify_playlist playlist;
  char virtual_path[PATH_MAX];
  int plid;
  int count;
  int trackcount;

  count = 0;
  trackcount = 0;
  memset(&request, 0, sizeof(struct spotify_request));

  while (0 == spotifywebapi_request_next(&request, SPOTIFY_WEBAPI_SAVED_PLAYLISTS, false))
    {
      while (0 == spotifywebapi_playlists_fetch(&request, &playlist))
	{
	  DPRINTF(E_DBG, L_SPOTIFY, "Got playlist: '%s' with %d tracks (%s) \n", playlist.name, playlist.tracks_count, playlist.uri);

	  if (!playlist.uri || !playlist.name || playlist.tracks_count == 0)
	    {
	      DPRINTF(E_LOG, L_SPOTIFY, "Ignoring playlist '%s' with %d tracks (%s)\n", playlist.name, playlist.tracks_count, playlist.uri);
	      continue;
	    }

	  if (playlist.owner)
	    {
	      snprintf(virtual_path, PATH_MAX, "/spotify:/%s (%s)", playlist.name, playlist.owner);
	    }
	  else
	    {
	      snprintf(virtual_path, PATH_MAX, "/spotify:/%s", playlist.name);
	    }

	  db_transaction_begin();
	  plid = library_add_playlist_info(playlist.uri, playlist.name, virtual_path, PL_PLAIN, spotify_base_plid, DIR_SPOTIFY);
	  db_transaction_end();

	  if (plid > 0)
	    scan_playlisttracks(&playlist, plid);
	  else
	    DPRINTF(E_LOG, L_SPOTIFY, "Error adding playlist: '%s' (%s) \n", playlist.name, playlist.uri);

	  count++;
	  trackcount += playlist.tracks_count;
	  DPRINTF(E_LOG, L_SPOTIFY, "Scanned %d of %d saved playlists (%d tracks)\n", count, request.total, trackcount);
	}
    }

  spotifywebapi_request_end(&request);

  return 0;
}

/* Thread: library */
static int
scan_playlist(const char *uri)
{
  struct spotify_request request;
  struct spotify_playlist playlist;
  char virtual_path[PATH_MAX];
  int plid;

  memset(&request, 0, sizeof(struct spotify_request));

  if (0 == spotifywebapi_playlist_start(&request, uri, &playlist))
    {
      if (!playlist.uri)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Got playlist with missing uri for path:: '%s'\n", uri);
	}
      else
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Saving playlist '%s' with %d tracks (%s) \n", playlist.name, playlist.tracks_count, playlist.uri);

	  if (playlist.owner)
	    {
	      snprintf(virtual_path, PATH_MAX, "/spotify:/%s (%s)", playlist.name, playlist.owner);
	    }
	  else
	    {
	      snprintf(virtual_path, PATH_MAX, "/spotify:/%s", playlist.name);
	    }

	  db_transaction_begin();
	  plid = library_add_playlist_info(playlist.uri, playlist.name, virtual_path, PL_PLAIN, spotify_base_plid, DIR_SPOTIFY);
	  db_transaction_end();

	  if (plid > 0)
	    scan_playlisttracks(&playlist, plid);
	  else
	    DPRINTF(E_LOG, L_SPOTIFY, "Error adding playlist: '%s' (%s) \n", playlist.name, playlist.uri);
	}
    }

  spotifywebapi_request_end(&request);

  return 0;
}

static void
create_saved_tracks_playlist()
{
  spotify_saved_plid = library_add_playlist_info("spotify:savedtracks", "Spotify Saved", "/spotify:/Spotify Saved", PL_PLAIN, spotify_base_plid, DIR_SPOTIFY);

  if (spotify_saved_plid <= 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error adding playlist for saved tracks\n");
      spotify_saved_plid = 0;
    }
}

/*
 * Add or update playlist folder for all spotify playlists (if enabled in config)
 */
static void
create_base_playlist()
{
  cfg_t *spotify_cfg;
  int ret;

  spotify_base_plid = 0;
  spotify_cfg = cfg_getsec(cfg, "spotify");
  if (!cfg_getbool(spotify_cfg, "base_playlist_disable"))
    {
      ret = library_add_playlist_info("spotify:playlistfolder", "Spotify", NULL, PL_FOLDER, 0, 0);
      if (ret < 0)
	DPRINTF(E_LOG, L_SPOTIFY, "Error adding base playlist\n");
      else
	spotify_base_plid = ret;
    }
}

/* Thread: library */
static int
initscan()
{
  char *user = NULL;

  scanning = true;

  /* Refresh access token for the spotify webapi */
  spotify_access_token_valid = (0 == spotifywebapi_token_refresh(&user));
  if (!spotify_access_token_valid)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Spotify webapi token refresh failed. "
	  "In order to use the web api, authorize forked-daapd to access "
	  "your saved tracks by visiting http://forked-daapd.local:3689/oauth\n");

      db_spotify_purge();
    }
  CHECK_ERR(L_REMOTE, pthread_mutex_lock(&status_lck));
  spotify_status_info.webapi_token_valid = spotify_access_token_valid;
  if (user)
    {
      snprintf(spotify_status_info.webapi_user, sizeof(spotify_status_info.webapi_user), "%s", user);
      spotify_status_info.webapi_user[sizeof(spotify_status_info.webapi_user) - 1] = '\0';
      free(user);
    }
  CHECK_ERR(L_REMOTE, pthread_mutex_unlock(&status_lck));

  spotify_saved_plid = 0;

  /*
   * Login to spotify needs to be done before scanning tracks from the web api.
   * (Scanned tracks need to be registered with libspotify for playback)
   */
  spotify_login(NULL);

  /*
   * Scan saved tracks from the web api
   */
  if (spotify_access_token_valid)
    {
      create_base_playlist();
      create_saved_tracks_playlist();
      scan_saved_albums();
      scan_playlists();
    }

  scanning = false;

  return 0;
}

/* Thread: library */
static int
rescan()
{
  scanning = true;

  create_base_playlist();

  /*
   * Scan saved tracks from the web api
   */
  if (spotify_access_token_valid)
    {
      create_saved_tracks_playlist();
      scan_saved_albums();
      scan_playlists();
    }
  else
    {
      db_transaction_begin();
      db_file_ping_bymatch("spotify:", 0);
      db_pl_ping_bymatch("spotify:", 0);
      db_directory_ping_bymatch("/spotify:");
      db_transaction_end();
    }

  scanning = false;

  return 0;
}

/* Thread: library */
static int
fullrescan()
{
  scanning = true;

  create_base_playlist();

  /*
   * Scan saved tracks from the web api
   */
  if (spotify_access_token_valid)
    {
      create_saved_tracks_playlist();
      scan_saved_albums();
      scan_playlists();
    }
  else
    {
      spotify_login(NULL);
    }

  scanning = false;

  return 0;
}

/* Thread: library */
static enum command_state
webapi_scan(void *arg, int *ret)
{
  db_spotify_purge();

  *ret = rescan();
  return COMMAND_END;
}

/* Thread: library */
static enum command_state
webapi_pl_save(void *arg, int *ret)
{
  const char *uri = arg;

  *ret = scan_playlist(uri);
  return COMMAND_END;
}

/* Thread: library */
static enum command_state
webapi_pl_remove(void *arg, int *ret)
{
  const char *uri = arg;

  *ret = playlist_remove(uri);
  return COMMAND_END;
}

/* Thread: main */
int
spotify_init(void)
{
  cfg_t *spotify_cfg;
  sp_session *sp;
  sp_error err;
  int ret;

  spotify_access_token_valid = false;
  scanning = false;

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

#ifdef HAVE_PIPE2
  ret = pipe2(g_notify_pipe, O_CLOEXEC);
#else
  ret = pipe(g_notify_pipe);
#endif
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

  g_notifyev = event_new(evbase_spotify, g_notify_pipe[0], EV_READ | EV_TIMEOUT, notify_cb, NULL);
  if (!g_notifyev)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create notify event\n");
      goto evnew_fail;
    }

  event_add(g_notifyev, NULL);

  cmdbase = commands_base_new(evbase_spotify, exit_cb);
  if (!cmdbase)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create command base\n");
      goto cmd_fail;
    }

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

  CHECK_ERR(L_REMOTE, pthread_mutex_lock(&status_lck));
  spotify_status_info.libspotify_installed = true;
  CHECK_ERR(L_REMOTE, pthread_mutex_unlock(&status_lck));

  spotify_audio_buffer = evbuffer_new();

  CHECK_ERR(L_SPOTIFY, evbuffer_enable_locking(spotify_audio_buffer, NULL));

  CHECK_ERR(L_SPOTIFY, mutex_init(&login_lck));
  CHECK_ERR(L_SPOTIFY, pthread_cond_init(&login_cond, NULL));

  /* Spawn thread */
  ret = pthread_create(&tid_spotify, NULL, spotify, NULL);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_SPOTIFY, "Could not spawn Spotify thread: %s\n", strerror(errno));
      goto thread_fail;
    }

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_spotify, "spotify");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_spotify, "spotify");
#endif

  DPRINTF(E_DBG, L_SPOTIFY, "Spotify init complete\n");
  return 0;

 thread_fail:
  CHECK_ERR(L_SPOTIFY, pthread_cond_destroy(&login_cond));
  CHECK_ERR(L_SPOTIFY, pthread_mutex_destroy(&login_lck));

  evbuffer_free(spotify_audio_buffer);

  fptr_sp_session_release(g_sess);
  g_sess = NULL;
  
 session_fail:
 cmd_fail:
 evnew_fail:
  commands_base_free(cmdbase);
  event_base_free(evbase_spotify);
  evbase_spotify = NULL;

 evbase_fail:
  close(g_notify_pipe[0]);
  close(g_notify_pipe[1]);

 notify_fail:
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
      commands_base_destroy(cmdbase);
      g_state = SPOTIFY_STATE_INACTIVE;

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

  /* Destroy locks */
  CHECK_ERR(L_SPOTIFY, pthread_cond_destroy(&login_cond));
  CHECK_ERR(L_SPOTIFY, pthread_mutex_destroy(&login_lck));

  /* Free audio buffer */
  evbuffer_free(spotify_audio_buffer);

  /* Release libspotify handle */
  dlclose(g_libhandle);
}

struct library_source spotifyscanner =
{
  .name = "spotifyscanner",
  .disabled = 0,
  .init = spotify_init,
  .deinit = spotify_deinit,
  .rescan = rescan,
  .initscan = initscan,
  .fullrescan = fullrescan,
};
