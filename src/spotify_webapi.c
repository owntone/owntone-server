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

#include "spotify_webapi.h"

#include <event2/event.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_JSON_C_OLD
# include <json/json.h>
#else
# include <json-c/json.h>
#endif

#include "db.h"
#include "http.h"
#include "library.h"
#include "logger.h"



// Credentials for the web api
static char *spotify_access_token;
static char *spotify_refresh_token;

static int32_t expires_in = 3600;
static time_t token_requested = 0;

// Endpoints and credentials for the web api
static const char *spotify_client_id     = "0e684a5422384114a8ae7ac020f01789";
static const char *spotify_client_secret = "232af95f39014c9ba218285a5c11a239";
static const char *spotify_auth_uri      = "https://accounts.spotify.com/authorize";
static const char *spotify_token_uri     = "https://accounts.spotify.com/api/token";


/*--------------------- HELPERS FOR SPOTIFY WEB API -------------------------*/
/*                 All the below is in the httpd thread                      */


static void
jparse_free(json_object* haystack)
{
  if (haystack)
    {
#ifdef HAVE_JSON_C_OLD
      json_object_put(haystack);
#else
      if (json_object_put(haystack) != 1)
        DPRINTF(E_LOG, L_SPOTIFY, "Memleak: JSON parser did not free object\n");
#endif
    }
}

static int
jparse_array_from_obj(json_object *haystack, const char *key, json_object **needle)
{
  if (! (json_object_object_get_ex(haystack, key, needle) && json_object_get_type(*needle) == json_type_array) )
    return -1;
  else
    return 0;
}

static const char *
jparse_str_from_obj(json_object *haystack, const char *key)
{
  json_object *needle;

  if (json_object_object_get_ex(haystack, key, &needle) && json_object_get_type(needle) == json_type_string)
    return json_object_get_string(needle);
  else
    return NULL;
}

static int
jparse_int_from_obj(json_object *haystack, const char *key)
{
  json_object *needle;

  if (json_object_object_get_ex(haystack, key, &needle) && json_object_get_type(needle) == json_type_int)
    return json_object_get_int(needle);
  else
    return 0;
}

static time_t
jparse_time_from_obj(json_object *haystack, const char *key)
{
  const char *tmp;
  struct tm tp;
  time_t parsed_time;

  memset(&tp, 0, sizeof(struct tm));

  tmp = jparse_str_from_obj(haystack, key);
  if (!tmp)
    return 0;

  strptime(tmp, "%Y-%m-%dT%H:%M:%SZ", &tp);
  parsed_time = mktime(&tp);
  if (parsed_time < 0)
    return 0;

  return parsed_time;
}

static char *
jparse_artist_from_obj(json_object *artists)
{
  char artistnames[1024];
  json_object *artist;
  int count;
  int i;
  const char *tmp;

  count = json_object_array_length(artists);
  if (count == 0)
    {
      return NULL;
    }

  for (i = 0; i < count; i++)
    {
      artist = json_object_array_get_idx(artists, i);
      tmp = jparse_str_from_obj(artist, "name");

      if (i == 0)
        {
	  strcpy(artistnames, tmp);
	}
      else
        {
	  strncat(artistnames, ", ", sizeof(artistnames) - 2);
	  strncat(artistnames, tmp, sizeof(artistnames) - strlen(artistnames));
	}
    }

  return strdup(artistnames);
}

static void
http_client_ctx_free(struct http_client_ctx *ctx)
{
  if (!ctx)
    return;

  if (ctx->input_body)
    evbuffer_free(ctx->input_body);
  if (ctx->output_headers)
    {
      keyval_clear(ctx->output_headers);
      free(ctx->output_headers);
    }
  free(ctx);
}

char *
spotifywebapi_oauth_uri_get(const char *redirect_uri)
{
  struct keyval kv;
  char *param;
  char *uri;
  int uri_len;
  int ret;

  uri = NULL;
  memset(&kv, 0, sizeof(struct keyval));
  ret = ( (keyval_add(&kv, "client_id", spotify_client_id) == 0) &&
	  (keyval_add(&kv, "response_type", "code") == 0) &&
	  (keyval_add(&kv, "redirect_uri", redirect_uri) == 0) &&
	  (keyval_add(&kv, "scope", "playlist-read-private user-library-read") == 0) &&
	  (keyval_add(&kv, "show_dialog", "false") == 0) );
  if (!ret)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Cannot display Spotify oath interface (error adding parameters to keyval)\n");
      goto out_clear_kv;
    }

  param = http_form_urlencode(&kv);
  if (param)
    {
      uri_len = strlen(spotify_auth_uri) + strlen(param) + 3;
      uri = calloc(uri_len, sizeof(char));
      snprintf(uri, uri_len, "%s/?%s", spotify_auth_uri, param);

      free(param);
    }

 out_clear_kv:
  keyval_clear(&kv);

  return uri;
}

static int
tokens_get(struct keyval *kv, const char **err)
{
  struct http_client_ctx ctx;
  char *param;
  char *body;
  json_object *haystack;
  const char *tmp;
  int ret;

  param = http_form_urlencode(kv);
  if (!param)
    {
      *err = "http_form_uriencode() failed";
      ret = -1;
      goto out_clear_kv;
    }

  memset(&ctx, 0, sizeof(struct http_client_ctx));
  ctx.url = (char *)spotify_token_uri;
  ctx.output_body = param;
  ctx.input_body = evbuffer_new();

  ret = http_client_request(&ctx);
  if (ret < 0)
    {
      *err = "Did not get a reply from Spotify";
      goto out_free_input_body;
    }

  // 0-terminate for safety
  evbuffer_add(ctx.input_body, "", 1);

  body = (char *)evbuffer_pullup(ctx.input_body, -1);
  if (!body || (strlen(body) == 0))
    {
      *err = "The reply from Spotify is empty or invalid";
      ret = -1;
      goto out_free_input_body;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Token reply: %s\n", body);

  haystack = json_tokener_parse(body);
  if (!haystack)
    {
      *err = "JSON parser returned an error";
      ret = -1;
      goto out_free_input_body;
    }

  tmp = jparse_str_from_obj(haystack, "access_token");
  if (tmp)
    spotify_access_token  = strdup(tmp);

  tmp = jparse_str_from_obj(haystack, "refresh_token");
  if (tmp)
    spotify_refresh_token = strdup(tmp);

  expires_in = jparse_int_from_obj(haystack, "expires_in");
  if (expires_in == 0)
    expires_in = 3600;

  jparse_free(haystack);

  if (!spotify_access_token)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not find access token in reply: %s\n", body);

      *err = "Could not find access token in Spotify reply (see log)";
      ret = -1;
      goto out_free_input_body;
    }

  token_requested = time(NULL);

  DPRINTF(E_LOG, L_SPOTIFY, "token: '%s'\n", spotify_access_token);
  DPRINTF(E_LOG, L_SPOTIFY, "refresh-token: '%s'\n", spotify_refresh_token);
  DPRINTF(E_LOG, L_SPOTIFY, "expires in: %d\n", expires_in);

  if (spotify_refresh_token)
    db_admin_set("spotify_refresh_token", spotify_refresh_token);

  ret = 0;

 out_free_input_body:
  evbuffer_free(ctx.input_body);
  free(param);
 out_clear_kv:

  return ret;
}

int
spotifywebapi_token_get(const char *code, const char *redirect_uri, const char **err)
{
  struct keyval kv;
  int ret;

  memset(&kv, 0, sizeof(struct keyval));
  ret = ( (keyval_add(&kv, "grant_type", "authorization_code") == 0) &&
          (keyval_add(&kv, "code", code) == 0) &&
          (keyval_add(&kv, "client_id", spotify_client_id) == 0) &&
          (keyval_add(&kv, "client_secret", spotify_client_secret) == 0) &&
          (keyval_add(&kv, "redirect_uri", redirect_uri) == 0) );

  if (!ret)
    {
      *err = "Add parameters to keyval failed";
      ret = -1;
    }
  else
    ret = tokens_get(&kv, err);

  keyval_clear(&kv);

  return ret;
}

int
spotifywebapi_token_refresh()
{
  struct keyval kv;
  char *refresh_token;
  const char *err;
  int ret;

  if (token_requested && difftime(token_requested, time(NULL)) < expires_in)
    {
      DPRINTF(E_DBG, L_SPOTIFY, "Spotify token still valid\n");
      return 0;
    }

  refresh_token = db_admin_get("spotify_refresh_token");
  if (!refresh_token)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "No spotify refresh token found\n");
      return -1;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Spotify refresh-token: '%s'\n", refresh_token);

  memset(&kv, 0, sizeof(struct keyval));
  ret = ( (keyval_add(&kv, "grant_type", "refresh_token") == 0) &&
	  (keyval_add(&kv, "client_id", spotify_client_id) == 0) &&
	  (keyval_add(&kv, "client_secret", spotify_client_secret) == 0) &&
          (keyval_add(&kv, "refresh_token", refresh_token) == 0) );
  if (!ret)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Add parameters to keyval failed");
      ret = -1;
    }
  else
    ret = tokens_get(&kv, &err);

  free(refresh_token);
  keyval_clear(&kv);

  return ret;
}

int
spotifywebapi_request_uri(struct spotify_request *request, const char *uri)
{
  char bearer_token[1024];
  int ret;

  memset(request, 0, sizeof(struct spotify_request));

  if (0 > spotifywebapi_token_refresh())
    {
      return -1;
    }

  request->ctx = calloc(1, sizeof(struct http_client_ctx));
  request->ctx->output_headers = calloc(1, sizeof(struct keyval));
  request->ctx->input_body = evbuffer_new();
  request->ctx->url = uri;

  snprintf(bearer_token, sizeof(bearer_token), "Bearer %s", spotify_access_token);
  if (keyval_add(request->ctx->output_headers, "Authorization", bearer_token) < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Add bearer_token to keyval failed\n");
      return -1;
    }

  ret = http_client_request(request->ctx);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Request for saved tracks/albums failed\n");
      return -1;
    }

  // 0-terminate for safety
  evbuffer_add(request->ctx->input_body, "", 1);

  request->response_body = (char *) evbuffer_pullup(request->ctx->input_body, -1);
  if (!request->response_body || (strlen(request->response_body) == 0))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Request for saved tracks/albums failed, response was empty\n");
      return -1;
    }

  //DPRINTF(E_DBG, L_SPOTIFY, "Wep api response for '%s'\n%s\n", uri, request->response_body);

  request->haystack = json_tokener_parse(request->response_body);
  if (!request->haystack)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "JSON parser returned an error\n");
      return -1;
    }

  DPRINTF(E_DBG, L_SPOTIFY, "Got response for '%s'\n", uri);
  return 0;
}

void
spotifywebapi_request_end(struct spotify_request *request)
{
  http_client_ctx_free(request->ctx);
  jparse_free(request->haystack);
}

int
spotifywebapi_request_next(struct spotify_request *request, const char *uri)
{
  char *next_uri;
  const char *tmp;
  int ret;

  if (request->ctx && !request->next_uri)
    {
      // Reached end of paging requests, terminate loop
      return -1;
    }

  if (!request->ctx)
    {
      // First paging request
      next_uri = strdup (uri);
    }
  else
    {
      // Next paging request
      next_uri = strdup(request->next_uri);
      spotifywebapi_request_end(request);
    }

  ret = spotifywebapi_request_uri(request, next_uri);
  free(next_uri);

  if (ret < 0)
    return ret;

  request->total = jparse_int_from_obj(request->haystack, "total");
  tmp = jparse_str_from_obj(request->haystack, "next");
  if (tmp)
    request->next_uri = strdup(tmp);

  if (jparse_array_from_obj(request->haystack, "items", &request->items) < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "No items in reply from Spotify. See:\n%s\n", request->response_body);
      return -1;
    }

  request->count = json_object_array_length(request->items);

  DPRINTF(E_DBG, L_SPOTIFY, "Got %d items\n", request->count);
  return 0;
}

void
track_metadata(json_object* jsontrack, struct spotify_track* track)
{
  json_object* jsonalbum;
  json_object* jsonartists;

  if (json_object_object_get_ex(jsontrack, "album", &jsonalbum))
    {
      track->album = jparse_str_from_obj(jsonalbum, "name");
      if (json_object_object_get_ex(jsonalbum, "artists", &jsonartists) && json_object_get_type(jsonartists) == json_type_array)
	{
	  track->album_artist = jparse_artist_from_obj(jsonartists);
	}
    }
  if (json_object_object_get_ex(jsontrack, "artists", &jsonartists) && json_object_get_type(jsonartists) == json_type_array)
    {
      track->artist = jparse_artist_from_obj(jsonartists);
    }
  track->disc_number = jparse_int_from_obj(jsontrack, "disc_number");
  track->album_type = jparse_str_from_obj(jsonalbum, "album_type");
  track->is_compilation = (track->album_type && 0 == strcmp(track->album_type, "compilation"));
  track->duration_ms = jparse_int_from_obj(jsontrack, "duration_ms");
  track->name = jparse_str_from_obj(jsontrack, "name");
  track->track_number = jparse_int_from_obj(jsontrack, "track_number");
  track->uri = jparse_str_from_obj(jsontrack, "uri");
  track->id = jparse_str_from_obj(jsontrack, "id");
}

int
spotifywebapi_saved_tracks_fetch(struct spotify_request *request, struct spotify_track *track)
{
  json_object *item;
  json_object *jsontrack;

  memset(track, 0, sizeof(struct spotify_track));

  if (request->index >= request->count)
    {
      return -1;
    }

  item = json_object_array_get_idx(request->items, request->index);
  if (!(item && json_object_object_get_ex(item, "track", &jsontrack)))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Unexpected JSON: Item %d did not have 'track'->'uri'\n", request->index);
      request->index++;
      return -1;
    }

  track_metadata(jsontrack, track);
  track->added_at = jparse_str_from_obj(item, "added_at");
//  if (track->added_at)
//    strptime("%Y-%m-%dT%H:%M:%SZ");

  request->index++;

  return 0;
}

int
spotifywebapi_track(const char *path, struct spotify_track *track)
{
//  char uri[1024];
//  struct http_client_ctx *ctx;
//  char *response_body;
//  json_object *haystack;
//
//  memset(track, 0, sizeof(struct spotify_track));
//
//  if (strlen(path) < 15) // spotify:track:3RZwWEcQHD0Sy1hN1xNYeh
//    return -1;
//
//  snprintf(uri, sizeof(uri), "%s%s", spotify_track_uri, (path + 14));
//  ctx = request_uri(uri);
//
//  if (!ctx)
//    return -1;
//
//  response_body = (char *) evbuffer_pullup(ctx->input_body, -1);
//  if (!response_body || (strlen(response_body) == 0))
//    {
//      DPRINTF(E_LOG, L_SPOTIFY, "Request for track failed, response was empty");
//      http_client_ctx_free(ctx);
//      return -1;
//    }
//
//  haystack = json_tokener_parse(response_body);
//  track_metadata(haystack, track);
//
//  http_client_ctx_free(ctx);
//  jparse_free(haystack);

  return 0;
}

void
album_metadata(json_object *jsonalbum, struct spotify_album *album)
{
  json_object* jsonartists;

  if (json_object_object_get_ex(jsonalbum, "artists", &jsonartists) && json_object_get_type(jsonartists) == json_type_array)
    {
      album->artist = jparse_artist_from_obj(jsonartists);
    }
  album->name = jparse_str_from_obj(jsonalbum, "name");
  album->uri = jparse_str_from_obj(jsonalbum, "uri");
  album->id = jparse_str_from_obj(jsonalbum, "id");

  album->album_type = jparse_str_from_obj(jsonalbum, "album_type");
  album->is_compilation = (album->album_type && 0 == strcmp(album->album_type, "compilation"));

  album->label = jparse_str_from_obj(jsonalbum, "label");
  album->release_date = jparse_str_from_obj(jsonalbum, "release_date");

  // TODO Genre is an array of strings ('genres'), but it is always empty (https://github.com/spotify/web-api/issues/157)
  //album->genre = jparse_str_from_obj(jsonalbum, "genre");
}

int
spotifywebapi_saved_albums_fetch(struct spotify_request *request, json_object **jsontracks, int *track_count, struct spotify_album *album)
{
  json_object *jsonalbum;
  json_object *item;
  json_object *needle;

  memset(album, 0, sizeof(struct spotify_album));
  *track_count = 0;

  if (request->index >= request->count)
    {
      return -1;
    }

  item = json_object_array_get_idx(request->items, request->index);
  if (!(item && json_object_object_get_ex(item, "album", &jsonalbum)))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Unexpected JSON: Item %d did not have 'album'->'uri'\n", request->index);
      request->index++;
      return -1;
    }

  album_metadata(jsonalbum, album);

  album->added_at = jparse_str_from_obj(item, "added_at");
  album->mtime = jparse_time_from_obj(item, "added_at");

  if (json_object_object_get_ex(jsonalbum, "tracks", &needle))
    {
      if (jparse_array_from_obj(needle, "items", jsontracks) == 0)
	{
	  *track_count = json_object_array_length(*jsontracks);
	}
    }

  request->index++;

  return 0;
}

int
spotifywebapi_album_track_fetch(json_object *jsontracks, int index, struct spotify_track *track)
{
  json_object *jsontrack;

  memset(track, 0, sizeof(struct spotify_track));

  jsontrack = json_object_array_get_idx(jsontracks, index);

  if (!jsontrack)
    {
      return -1;
    }

  track_metadata(jsontrack, track);

  return 0;
}

void
playlist_metadata(json_object *jsonplaylist, struct spotify_playlist *playlist)
{
  json_object *needle;

  playlist->name = jparse_str_from_obj(jsonplaylist, "name");
  playlist->uri = jparse_str_from_obj(jsonplaylist, "uri");
  playlist->id = jparse_str_from_obj(jsonplaylist, "id");
  playlist->href = jparse_str_from_obj(jsonplaylist, "href");

  if (json_object_object_get_ex(jsonplaylist, "owner", &needle))
    {
      playlist->owner = jparse_str_from_obj(needle, "id");
    }

  if (json_object_object_get_ex(jsonplaylist, "tracks", &needle))
    {
      playlist->tracks_href = jparse_str_from_obj(needle, "href");
      playlist->tracks_count = jparse_int_from_obj(needle, "total");
    }
}

int
spotifywebapi_playlists_fetch(struct spotify_request *request, struct spotify_playlist *playlist)
{
  json_object *jsonplaylist;

  memset(playlist, 0, sizeof(struct spotify_playlist));

  if (request->index >= request->count)
    {
      return -1;
    }

  jsonplaylist = json_object_array_get_idx(request->items, request->index);

  playlist_metadata(jsonplaylist, playlist);

  request->index++;

  return 0;
}

int
spotifywebapi_playlisttracks_fetch(struct spotify_request *request, struct spotify_track *track)
{
  json_object *item;
  json_object *jsontrack;

  memset(track, 0, sizeof(struct spotify_track));

  if (request->index >= request->count)
    {
      return -1;
    }

  item = json_object_array_get_idx(request->items, request->index);
  if (!(item && json_object_object_get_ex(item, "track", &jsontrack)))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Unexpected JSON: Item %d did not have 'track'->'uri'\n", request->index);
      request->index++;
      return -1;
    }

  track_metadata(jsontrack, track);
  track->added_at = jparse_str_from_obj(item, "added_at");
  track->mtime = jparse_time_from_obj(item, "added_at");

  request->index++;

  return 0;
}
