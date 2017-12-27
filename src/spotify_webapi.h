/*
 * Copyright (C) 2016 Espen Jürgensen <espenjurgensen@gmail.com>
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

#ifndef SRC_SPOTIFY_WEBAPI_H_
#define SRC_SPOTIFY_WEBAPI_H_

#include <event2/event.h>
#include <json.h>
#include <stdbool.h>

#include "http.h"

#define SPOTIFY_WEBAPI_SAVED_ALBUMS "https://api.spotify.com/v1/me/albums?limit=50"
#define SPOTIFY_WEBAPI_SAVED_PLAYLISTS "https://api.spotify.com/v1/me/playlists?limit=50"

struct spotify_album
{
  const char *added_at;
  time_t mtime;

  const char *album_type;
  bool is_compilation;
  const char *artist;
  const char *genre;
  const char *id;
  const char *label;
  const char *name;
  const char *release_date;
  const char *release_date_precision;
  int release_year;
  const char *uri;
};

struct spotify_track
{
  const char *added_at;
  time_t mtime;

  const char *album;
  const char *album_artist;
  const char *artist;
  int disc_number;
  const char *album_type;
  bool is_compilation;
  int duration_ms;
  const char *id;
  const char *name;
  int track_number;
  const char *uri;

  bool is_playable;
  const char *restrictions;
  const char *linked_from_uri;
};

struct spotify_playlist
{
  const char *id;
  const char *name;
  const char *owner;
  const char *uri;

  const char *href;

  const char *tracks_href;
  int tracks_count;
};

struct spotify_request
{
  struct http_client_ctx *ctx;
  char *response_body;
  json_object *haystack;
  json_object *items;
  int count;
  int total;
  const char *next_uri;

  int index;
};

char *
spotifywebapi_oauth_uri_get(const char *redirect_uri);
int
spotifywebapi_token_get(const char *code, const char *redirect_uri, char **user, const char **err);
int
spotifywebapi_token_refresh(char **user);

void
spotifywebapi_request_end(struct spotify_request *request);
int
spotifywebapi_request_next(struct spotify_request *request, const char *uri, bool append_market);
int
spotifywebapi_saved_albums_fetch(struct spotify_request *request, json_object **jsontracks, int *track_count, struct spotify_album *album);
int
spotifywebapi_album_track_fetch(json_object *jsontracks, int index, struct spotify_track *track);
int
spotifywebapi_playlists_fetch(struct spotify_request *request, struct spotify_playlist* playlist);
int
spotifywebapi_playlisttracks_fetch(struct spotify_request *request, struct spotify_track *track);
int
spotifywebapi_playlist_start(struct spotify_request *request, const char *path, struct spotify_playlist *playlist);

#endif /* SRC_SPOTIFY_WEBAPI_H_ */
