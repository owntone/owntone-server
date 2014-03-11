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


/* --- Types --- */
typedef struct audio_fifo_data {
  TAILQ_ENTRY(audio_fifo_data) link;
  int nsamples;
  int16_t samples[0];
} audio_fifo_data_t;

typedef struct audio_fifo {
  TAILQ_HEAD(, audio_fifo_data) q;
  int qlen;
  int fullcount;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} audio_fifo_t;

enum spotify_event
  {
    SPOTIFY_EVENT_NONE,
    SPOTIFY_EVENT_LIBCB,
    SPOTIFY_EVENT_PLAY,
    SPOTIFY_EVENT_PAUSE,
    SPOTIFY_EVENT_STOP,
    SPOTIFY_EVENT_SEEK,
    SPOTIFY_EVENT_EXIT,
    SPOTIFY_EVENT_RESUME,
  };

enum spotify_state
  {
    SPOTIFY_STATE_INACTIVE,
    SPOTIFY_STATE_WAIT,
    SPOTIFY_STATE_PLAYING,
    SPOTIFY_STATE_PAUSED,
    SPOTIFY_STATE_STOPPED,
    SPOTIFY_STATE_SEEKED,
    SPOTIFY_STATE_EXITING,
  };

/* Context for communicating with player */
struct spotify_ctx
  {
    sp_link *link;
    int seek_ms;
  };

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

// Spotify thread
static pthread_t tid_spotify;
// Synchronization mutex for the spotify thread
static pthread_mutex_t g_notify_mutex;
// Synchronization condition variable for the spotify thread
static pthread_cond_t g_notify_cond;
// Synchronization mutex for the spotify player state
static pthread_mutex_t g_state_mutex;
// Synchronization condition variable for the spotify player state
static pthread_cond_t g_state_cond;
// Synchronization variable telling the spotify thread to process events
static enum spotify_event g_event;
// Synchronization variable telling the caller thread about the state
static enum spotify_state g_state;

// The global session handle
static sp_session *g_sess;
// The global library handle
static void *g_libhandle;
// The global spotify context
static struct spotify_ctx g_ctx;

// Synchronization mutex for the database (possibly useless)
static pthread_mutex_t g_db_mutex;

// Audio fifo
static audio_fifo_t *g_audio_fifo;


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
typedef sp_playlistcontainer* (*fptr_sp_session_playlistcontainer_t)(sp_session *session);
typedef sp_error     (*fptr_sp_session_player_load_t)(sp_session *session, sp_track *track);
typedef sp_error     (*fptr_sp_session_player_unload_t)(sp_session *session);
typedef sp_error     (*fptr_sp_session_player_play_t)(sp_session *session, bool play);
typedef sp_error     (*fptr_sp_session_player_seek_t)(sp_session *session, int offset);

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

typedef const char*  (*fptr_sp_artist_name_t)(sp_artist *artist);

/* Define actual function pointers */
fptr_sp_error_message_t fptr_sp_error_message;

fptr_sp_session_create_t fptr_sp_session_create;
fptr_sp_session_release_t fptr_sp_session_release;
fptr_sp_session_login_t fptr_sp_session_login;
fptr_sp_session_relogin_t fptr_sp_session_relogin;
fptr_sp_session_logout_t fptr_sp_session_logout;
fptr_sp_session_playlistcontainer_t fptr_sp_session_playlistcontainer;
fptr_sp_session_process_events_t fptr_sp_session_process_events;
fptr_sp_session_player_load_t fptr_sp_session_player_load;
fptr_sp_session_player_unload_t fptr_sp_session_player_unload;
fptr_sp_session_player_play_t fptr_sp_session_player_play;
fptr_sp_session_player_seek_t fptr_sp_session_player_seek;

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

fptr_sp_artist_name_t fptr_sp_artist_name;

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
   && (fptr_sp_playlistcontainer_add_callbacks = dlsym(h, "sp_playlistcontainer_add_callbacks"))
   && (fptr_sp_playlistcontainer_num_playlists = dlsym(h, "sp_playlistcontainer_num_playlists"))
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
   && (fptr_sp_artist_name = dlsym(h, "sp_artist_name"))
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


/* --------------------------  PLAYLIST HELPERS    ------------------------- */

static int
spotify_metadata_get(sp_track *track, struct media_file_info *mfi)
{
  sp_album *album;
  sp_artist *artist;
  sp_albumtype albumtype;

  album = fptr_sp_track_album(track);
  if (!album)
    return -1;

  artist = fptr_sp_album_artist(album);
  if (!artist)
    return -1;

  albumtype = fptr_sp_album_type(album);

  mfi->title       = strdup(fptr_sp_track_name(track));
  mfi->album       = strdup(fptr_sp_album_name(album));
  mfi->artist      = strdup(fptr_sp_artist_name(artist));
  mfi->year        = fptr_sp_album_year(album);
  mfi->song_length = fptr_sp_track_duration(track);
  mfi->track       = fptr_sp_track_index(track);
  mfi->disc        = fptr_sp_track_disc(track);
  mfi->compilation = (albumtype == SP_ALBUMTYPE_COMPILATION);
  mfi->artwork     = ARTWORK_SPOTIFY;
  mfi->type        = strdup("spotify");
  mfi->codectype   = strdup("wav");
  mfi->description = strdup("Spotify audio");

  return 0;
}

static int
spotify_track_save(int plid, sp_track *track)
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

  link = fptr_sp_link_create_from_track(track, 0);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create link for track\n");
      return -1;
    }

  ret = fptr_sp_link_as_string(link, url, sizeof(url));
  if (ret == sizeof(url))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Spotify link truncated: %s\n", url);
    }
  fptr_sp_link_release(link);

  /* Add to playlistitems table */
  ret = db_pl_add_item_bypath(plid, url);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not save playlist item\n");
      return -1;
    }

  memset(&mfi, 0, sizeof(struct media_file_info));

  ret = spotify_metadata_get(track, &mfi);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Metadata missing (but track should be loaded?)\n");
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
  char title[512];
  int plid;
  int num_tracks;
  int ret;
  int i;
  
  if (!fptr_sp_playlist_is_loaded(pl))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist still not loaded - wait for rename callback\n");
      return 0;
    }

  name = fptr_sp_playlist_name(pl);

  DPRINTF(E_DBG, L_SPOTIFY, "Saving playlist: %s\n", name);

  /* Save playlist (playlists table) */
  link = fptr_sp_link_create_from_playlist(pl);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create link for playlist (wait)\n");
      return -1;
    }

  ret = fptr_sp_link_as_string(link, url, sizeof(url));
  if (ret == sizeof(url))
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Spotify link truncated: %s\n", url);
    }
  fptr_sp_link_release(link);

  sleep(1); // Primitive way of preventing database locking (the mutex wasn't working)

  pli = db_pl_fetch_bypath(url);
  snprintf(title, sizeof(title), "[s] %s", name);

  if (pli)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist found (%s, link %s), updating\n", name, url);

      plid = pli->id;

      free_pli(pli, 0);

      ret = db_pl_update(title, url, plid);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error updating playlist (%s, link %s)\n", name, url);
	  return -1;
	}

      db_pl_ping(plid);
      db_pl_clear_items(plid);
    }
  else
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Adding playlist (%s, link %s)\n", name, url);

      ret = db_pl_add(title, url, &plid);
      if ((ret < 0) || (plid < 1))
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error adding playlist (%s, link %s, ret %d, plid %d)\n", name, url, ret, plid);
	  return -1;
	}
    }

  /* Save tracks and playlistitems (files and playlistitems table) */
  num_tracks = fptr_sp_playlist_num_tracks(pl);
  for (i = 0; i < num_tracks; i++)
    {
      track = fptr_sp_playlist_track(pl, i);
      if (!track)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Track %d in playlist %s (id %d) is invalid\n", i, name, plid);
	  continue;
	}

      ret = spotify_track_save(plid, track);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error saving track %d to playlist %s (id %d)\n", i, name, plid);
	  continue;
	}
    }

  return plid;
}

/* --------------------------  AUDIO HELPER  ------------------------------- */

static void
spotify_audio_fifo_flush(void)
{
    audio_fifo_data_t *afd;

    DPRINTF(E_DBG, L_SPOTIFY, "Flushing fifo\n");

    pthread_mutex_lock(&g_audio_fifo->mutex);

    while((afd = TAILQ_FIRST(&g_audio_fifo->q))) {
	TAILQ_REMOVE(&g_audio_fifo->q, afd, link);
	free(afd);
    }

    g_audio_fifo->qlen = 0;
    g_audio_fifo->fullcount = 0;
    pthread_mutex_unlock(&g_audio_fifo->mutex);

    DPRINTF(E_DBG, L_SPOTIFY, "fifo flushed\n");
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
  DPRINTF(E_DBG, L_SPOTIFY, "Playlist update in progress (status %d): %s\n", done, fptr_sp_playlist_name(pl));

  if (done)
    {
      pthread_mutex_lock(&g_db_mutex);
      spotify_playlist_save(pl);
      pthread_mutex_unlock(&g_db_mutex);
    }
}

static void playlist_metadata_updated(sp_playlist *pl, void *userdata)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Playlist metadata updated: %s\n", fptr_sp_playlist_name(pl));

  pthread_mutex_lock(&g_db_mutex);
  spotify_playlist_save(pl);
  pthread_mutex_unlock(&g_db_mutex);
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

  pthread_mutex_lock(&g_db_mutex);
  spotify_playlist_save(pl);
  pthread_mutex_unlock(&g_db_mutex);
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

  pthread_mutex_lock(&g_db_mutex);

  pli = db_pl_fetch_bypath(url);

  if (!pli)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Playlist %s not found, can't delete\n", url);
      pthread_mutex_unlock(&g_db_mutex);
      return;
    }

  plid = pli->id;

  free_pli(pli, 0);

  db_spotify_pl_delete(plid);

  pthread_mutex_unlock(&g_db_mutex);
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
      pthread_exit(NULL);
    }

  DPRINTF(E_LOG, L_SPOTIFY, "Login to Spotify succeeded. Reloading playlists.\n");

  pthread_mutex_lock(&g_db_mutex);
  db_spotify_purge();
  pthread_mutex_unlock(&g_db_mutex);

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
      pthread_mutex_lock(&g_notify_mutex);
      g_event = SPOTIFY_EVENT_STOP;
      pthread_cond_signal(&g_notify_cond);
      pthread_mutex_unlock(&g_notify_mutex);
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
	  DPRINTF(E_WARN, L_SPOTIFY, "Buffer full more than 300 times, signaling pause\n");
	  pthread_mutex_lock(&g_notify_mutex);
	  g_event = SPOTIFY_EVENT_PAUSE;
	  pthread_cond_signal(&g_notify_cond);
	  pthread_mutex_unlock(&g_notify_mutex);
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
 * reiterate the main loop.
 *
 * We notify the main thread using a condition variable and a protected variable.
 *
 * @sa sp_session_callbacks#notify_main_thread
 */
static void
notify_main_thread(sp_session *sess)
{
  DPRINTF(E_SPAM, L_SPOTIFY, "Notify main thread - init\n");
  pthread_mutex_lock(&g_notify_mutex);
  g_event = SPOTIFY_EVENT_LIBCB;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
  DPRINTF(E_SPAM, L_SPOTIFY, "Notify main thread - done\n");
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
  DPRINTF(E_DBG, L_SPOTIFY, "Session metadata updated.\n");
}

/**
 * Notification that some other connection has started playing on this account.
 * Playback has been stopped.
 *
 * @sa sp_session_callbacks#play_token_lost
 */
static void play_token_lost(sp_session *sess)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Play token lost - init\n");
  pthread_mutex_lock(&g_notify_mutex);
  g_event = SPOTIFY_EVENT_STOP;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
  DPRINTF(E_DBG, L_SPOTIFY, "Play token lost - done\n");
}

/**
 * This callback is used from libspotify when the current track has ended
 *
 * @sa sp_session_callbacks#end_of_track
 */
static void end_of_track(sp_session *sess)
{
  DPRINTF(E_DBG, L_SPOTIFY, "End of track - init\n");
  pthread_mutex_lock(&g_notify_mutex);
  g_event = SPOTIFY_EVENT_STOP;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
  DPRINTF(E_DBG, L_SPOTIFY, "End of track - done\n");
}

/**
 * The session callbacks
 */
static sp_session_callbacks session_callbacks = {
  .logged_in = &logged_in,
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
/* -------------------------  END SESSION CALLBACKS  ----------------------- */


/* Thread: spotify */
static int
playback_play(void)
{
  sp_track *track;
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Starting playback\n");

  if (!g_ctx.link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, no Spotify link");
      return -1;
    }

  track = fptr_sp_link_as_track(g_ctx.link);
  if (!track)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, invalid Spotify track");
      return -1;
    }
  
  err = fptr_sp_session_player_load(g_sess, track);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  spotify_audio_fifo_flush();

  err = fptr_sp_session_player_play(g_sess, 1);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Playback started\n");

  return 0;
}

/* Thread: spotify */
static int
playback_pause(void)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Pausing playback\n");

  err = fptr_sp_session_player_play(g_sess, 0);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback pause failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Playback paused\n");

  return 0;
}

/* Thread: spotify */
static int
playback_resume(void)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Resuming playback\n");

  err = fptr_sp_session_player_play(g_sess, 1);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback resume failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Playback resumed\n");

  return 0;
}

/* Thread: spotify */
static int
playback_stop(void)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Stopping playback\n");

  err = fptr_sp_session_player_unload(g_sess);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback stop failed: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Playback stopped\n");

  return 0;
}

/* Thread: spotify */
static int
playback_seek(void)
{
  sp_error err;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback seek\n");

  err = fptr_sp_session_player_seek(g_sess, g_ctx.seek_ms);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not seek: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  spotify_audio_fifo_flush();

  return 0;
}

/* Thread: spotify */
static void *
spotify(void *arg)
{
  struct timespec ts;
  enum spotify_event this_event;
  enum spotify_state state;
  int ret;
  int next_timeout;

  DPRINTF(E_DBG, L_SPOTIFY, "Main loop begin\n");

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  state = SPOTIFY_STATE_WAIT;
  next_timeout = 0;
  for (;;)
    {
      pthread_mutex_lock(&g_notify_mutex);

      if (next_timeout == 0)
	{
	  while(g_event == SPOTIFY_EVENT_NONE)
	    pthread_cond_wait(&g_notify_cond, &g_notify_mutex);
	}
      else
        {
#if _POSIX_TIMERS > 0
	  clock_gettime(CLOCK_REALTIME, &ts);
#else
	  struct timeval tv;
	  gettimeofday(&tv, NULL);
	  TIMEVAL_TO_TIMESPEC(&tv, &ts);
#endif
	  ts.tv_sec += next_timeout / 1000;
	  ts.tv_nsec += (next_timeout % 1000) * 1000000;

	  while (g_event == SPOTIFY_EVENT_NONE)
	    if (pthread_cond_timedwait(&g_notify_cond, &g_notify_mutex, &ts))
	      break;
	}

      this_event = g_event;
      g_event = SPOTIFY_EVENT_NONE;
      pthread_mutex_unlock(&g_notify_mutex);

      switch (this_event)
	{
	  case SPOTIFY_EVENT_PLAY:
	    if ((ret = playback_play()) == 0)
	      state = SPOTIFY_STATE_PLAYING;
	    else
	      state = SPOTIFY_STATE_STOPPED;
	    break;

	  case SPOTIFY_EVENT_PAUSE:
	    if ((ret = playback_pause()) == 0)
	      state = SPOTIFY_STATE_PAUSED;
	    else
	      state = SPOTIFY_STATE_PLAYING;
	    break;

	  case SPOTIFY_EVENT_RESUME:
	    if ((ret = playback_resume()) == 0)
	      state = SPOTIFY_STATE_PLAYING;
	    else
	      state = SPOTIFY_STATE_PAUSED;
	    break;

	  case SPOTIFY_EVENT_STOP:
	    if ((ret = playback_stop()) == 0)
	      state = SPOTIFY_STATE_STOPPED;
	    else
	      state = SPOTIFY_STATE_PLAYING;
	    break;

	  case SPOTIFY_EVENT_SEEK:
	    if ((ret = playback_seek()) == 0)
	      state = SPOTIFY_STATE_SEEKED;
	    break;

	  case SPOTIFY_EVENT_EXIT:
	    ret = playback_stop();
	    fptr_sp_session_logout(g_sess);
	    state = SPOTIFY_STATE_EXITING;
	    break;

	  default:
	    ret = 0;
	    state = 0;
	}

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Playback action failed (event code %d)\n", this_event);
	}

      if (state == SPOTIFY_STATE_EXITING)
	break;

      do
	{
	  fptr_sp_session_process_events(g_sess, &next_timeout);
	}
      while (next_timeout == 0);

      pthread_mutex_lock(&g_state_mutex);
      if (state)
	{
	  g_state = state;
	  DPRINTF(E_LOG, L_SPOTIFY, "Event was %d, new state is %d\n", this_event, g_state);
	  pthread_cond_signal(&g_state_cond);
	}
      pthread_mutex_unlock(&g_state_mutex);
    }

  db_perthread_deinit();

  DPRINTF(E_DBG, L_SPOTIFY, "Main loop end\n");

  pthread_exit(NULL);
}


/* -------------------------  PLAYER API             ----------------------- */

/* Thread: player */
int
spotify_playback_play(struct media_file_info *mfi)
{
  sp_link *link;

  DPRINTF(E_DBG, L_SPOTIFY, "Playback request\n");

  link = fptr_sp_link_create_from_string(mfi->path);
  if (!link)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Playback setup failed, invalid Spotify link: %s\n", mfi->path);
      return -1;
    }

  pthread_mutex_lock(&g_notify_mutex);

  g_state = SPOTIFY_STATE_WAIT;
  g_event = SPOTIFY_EVENT_PLAY;

  if (g_ctx.link)
    fptr_sp_link_release(g_ctx.link);
  g_ctx.link = link;
  
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);

  // Wait until state changed so we know the event was processed
  pthread_mutex_lock(&g_state_mutex);
  while (g_state == SPOTIFY_STATE_WAIT)
    pthread_cond_wait(&g_state_cond, &g_state_mutex);
  pthread_mutex_unlock(&g_state_mutex);

  DPRINTF(E_DBG, L_SPOTIFY, "Playback reply\n");

  if (g_state == SPOTIFY_STATE_PLAYING)
    return 0;
  else
    return -1;
}

/* Thread: player */
//TODO This is not currently used by player.c - should it?
int
spotify_playback_pause(void)
{
  pthread_mutex_lock(&g_notify_mutex);

  g_state = SPOTIFY_STATE_WAIT;
  g_event = SPOTIFY_EVENT_PAUSE;
  
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);

  // Wait until state changed so we know the event was processed
  pthread_mutex_lock(&g_state_mutex);
  while (g_state == SPOTIFY_STATE_WAIT)
    pthread_cond_wait(&g_state_cond, &g_state_mutex);
  pthread_mutex_unlock(&g_state_mutex);

  if (g_state == SPOTIFY_STATE_PAUSED)
    return 0;
  else
    return -1;
}

/* Thread: player */
int
spotify_playback_stop(void)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Stop request\n");

  pthread_mutex_lock(&g_notify_mutex);

  g_state = SPOTIFY_STATE_WAIT;
  g_event = SPOTIFY_EVENT_STOP;
  
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);

  // Wait until state changed so we know the event was processed
  pthread_mutex_lock(&g_state_mutex);
  while (g_state == SPOTIFY_STATE_WAIT)
    pthread_cond_wait(&g_state_cond, &g_state_mutex);
  pthread_mutex_unlock(&g_state_mutex);

  DPRINTF(E_DBG, L_SPOTIFY, "Stop reply\n");

  if (g_state == SPOTIFY_STATE_STOPPED)
    return 0;
  else
    return -1;
}

/* Thread: player */
int
spotify_playback_seek(int ms)
{
  pthread_mutex_lock(&g_notify_mutex);

  g_state = SPOTIFY_STATE_WAIT;
  g_event = SPOTIFY_EVENT_SEEK;
  g_ctx.seek_ms = ms;
  
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);

  // Wait until state changed so we know the event was processed
  pthread_mutex_lock(&g_state_mutex);
  while (g_state == SPOTIFY_STATE_WAIT)
    pthread_cond_wait(&g_state_cond, &g_state_mutex);
  pthread_mutex_unlock(&g_state_mutex);

  return ms;
}

/* Thread: player */
int
spotify_audio_get(struct evbuffer *evbuf, int wanted)
{
  audio_fifo_data_t *afd;
  int processed;
  int ret;
  int s;

  afd = NULL;
  processed = 0;

  // If spotify was paused begin by resuming playback
  if (g_state == SPOTIFY_STATE_PAUSED)
    {
      pthread_mutex_lock(&g_notify_mutex);
      g_event = SPOTIFY_EVENT_RESUME;
      pthread_cond_signal(&g_notify_cond);
      pthread_mutex_unlock(&g_notify_mutex);
    }

  pthread_mutex_lock(&g_audio_fifo->mutex);

  while ((processed < wanted) && (g_state != SPOTIFY_STATE_STOPPED))
  {
    while ((g_state != SPOTIFY_STATE_STOPPED) && !(afd = TAILQ_FIRST(&g_audio_fifo->q)))
      {
	DPRINTF(E_DBG, L_SPOTIFY, "Audio get is blocking now\n");
	pthread_cond_wait(&g_audio_fifo->cond, &g_audio_fifo->mutex); // TODO protect against indefinite wait
	DPRINTF(E_DBG, L_SPOTIFY, "Audio get is released now\n");
      }

    TAILQ_REMOVE(&g_audio_fifo->q, afd, link);
    g_audio_fifo->qlen -= afd->nsamples;

    s = afd->nsamples * sizeof(int16_t) * 2;
  
    ret = evbuffer_add(evbuf, afd->samples, s);
    free(afd);
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

/* Thread: filescanner */
void
spotify_login(char *path)
{
  char buf[256];
  FILE *fp;
  char *username;
  char *password;
  int len;
  int ret;
  sp_error err;

  if (!g_sess)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Can't login! No valid Spotify session.\n");
      return;
    }

  fp = fopen(path, "rb");
  if (!fp)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not open Spotify credentials file %s: %s\n", path, strerror(errno));
      return;
    }

  username = fgets(buf, sizeof(buf), fp);
  if (!username)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Empty Spotify credentials file %s\n", path);

      fclose(fp);
      return;
    }

  len = strlen(username);
  if (buf[len - 1] != '\n')
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Invalid Spotify credentials file %s: username name too long or missing password\n", path);

      fclose(fp);
      return;
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
      return;
    }

  username = strdup(buf);
  if (!username)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for username while reading %s\n", path);

      fclose(fp);
      return;
    }

  password = fgets(buf, sizeof(buf), fp);
  fclose(fp);
  if (!password)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Invalid Spotify credentials file %s: no password\n", path);

      free(username);
      return;
    }

  len = strlen(password);

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

  password = strdup(buf);
  if (!password)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Out of memory for password while reading %s\n", path);

      free(username);
      return;
    }

  if (g_state != SPOTIFY_STATE_INACTIVE)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Killing previous Spotify thread\n");
      pthread_mutex_lock(&g_notify_mutex);
      g_event = SPOTIFY_EVENT_EXIT;
      pthread_cond_signal(&g_notify_cond);
      pthread_mutex_unlock(&g_notify_mutex);

      pthread_join(tid_spotify, NULL);
      g_state = SPOTIFY_STATE_INACTIVE;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Spotify credentials file OK, logging in with %s/%s\n", username, password);

  err = fptr_sp_session_login(g_sess, username, password, 1, NULL);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not login into Spotify: %s\n", fptr_sp_error_message(err));
      return;
    }

  ret = pthread_create(&tid_spotify, NULL, spotify, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not spawn Spotify thread: %s\n", strerror(errno));

      return;
    }
}

/* Thread: main */
int
spotify_init(void)
{
  cfg_t *lib;
  sp_session *sp;
  sp_error err;
  int ret;

  /* Initialize libspotify */
  g_libhandle = dlopen("libspotify.so", RTLD_LAZY);
  if (!g_libhandle)
    {
      DPRINTF(E_INFO, L_SPOTIFY, "libspotify.so not installed or not found\n");
      return -1;
    }

  DPRINTF(E_INFO, L_SPOTIFY, "Spotify session init\n");
  ret = fptr_assign_all();
  if (ret < 0)
    return -1;

  /* Initialize session */
  g_event = SPOTIFY_EVENT_NONE;
  g_state = SPOTIFY_STATE_INACTIVE;

  lib = cfg_getsec(cfg, "spotify");
  spconfig.settings_location = cfg_getstr(lib, "settings_dir");
  spconfig.cache_location = cfg_getstr(lib, "cache_dir");

  DPRINTF(E_DBG, L_SPOTIFY, "Creating Spotify session\n");
  err = fptr_sp_session_create(&spconfig, &sp);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create Spotify session: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  g_sess = sp;

  /* Prepare thread and audio buffer */
  pthread_mutex_init(&g_notify_mutex, NULL);
  pthread_cond_init(&g_notify_cond, NULL);
  pthread_mutex_init(&g_state_mutex, NULL);
  pthread_cond_init(&g_state_cond, NULL);
  pthread_mutex_init(&g_db_mutex, NULL);

  g_audio_fifo = (audio_fifo_t *)malloc(sizeof(audio_fifo_t));
  TAILQ_INIT(&g_audio_fifo->q);
  g_audio_fifo->qlen = 0;

  pthread_mutex_init(&g_audio_fifo->mutex, NULL);
  pthread_cond_init(&g_audio_fifo->cond, NULL);

  /* Log in and spawn thread */
  DPRINTF(E_DBG, L_SPOTIFY, "Logging into Spotify\n");
  err = fptr_sp_session_relogin(sp);
  if (SP_ERROR_OK != err)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not login into Spotify: %s\n", fptr_sp_error_message(err));
      return -1;
    }

  ret = pthread_create(&tid_spotify, NULL, spotify, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not spawn Spotify thread: %s\n", strerror(errno));

      return -1;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Spotify init complete\n");
  return 0;
}

void
spotify_deinit(void)
{
  int ret;

  /* libspotify not installed or no session - just exit */
  if (!g_libhandle || !g_sess)
    return;

  /* Send exit signal to thread (if active) */
  if (g_state != SPOTIFY_STATE_INACTIVE)
    {
      pthread_mutex_lock(&g_notify_mutex);
      g_event = SPOTIFY_EVENT_EXIT;
      pthread_cond_signal(&g_notify_cond);
      pthread_mutex_unlock(&g_notify_mutex);

      ret = pthread_join(tid_spotify, NULL);
      if (ret != 0)
	{
	  DPRINTF(E_FATAL, L_SPOTIFY, "Could not join Spotify thread: %s\n", strerror(errno));
	  return;
	}
    }

  /* Release session and destroy pthread mutex/cond */
  fptr_sp_session_release(g_sess);

  DPRINTF(E_SPAM, L_SPOTIFY, "Destroy pthread mutex and cond\n");
  pthread_cond_destroy(&g_notify_cond);
  pthread_mutex_destroy(&g_notify_mutex);

  pthread_cond_destroy(&g_state_cond);
  pthread_mutex_destroy(&g_state_mutex);

  pthread_mutex_destroy(&g_db_mutex);

  pthread_cond_destroy(&g_audio_fifo->cond);
  pthread_mutex_destroy(&g_audio_fifo->mutex);

  /* Free audio buffer */
  DPRINTF(E_SPAM, L_SPOTIFY, "Free audio fifo\n");
  free(g_audio_fifo);

  /* Release libspotify handle */
  if (g_libhandle)
    dlclose(g_libhandle);
}
