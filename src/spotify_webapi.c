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
#include <json.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "db.h"
#include "http.h"
#include "library.h"
#include "logger.h"
#include "misc_json.h"



// Credentials for the web api
static char *spotify_access_token;
static char *spotify_refresh_token;
static char *spotify_user_country;
static char *spotify_user;

static int32_t expires_in = 3600;
static time_t token_requested = 0;

// Endpoints and credentials for the web api
static const char *spotify_client_id     = "0e684a5422384114a8ae7ac020f01789";
static const char *spotify_client_secret = "232af95f39014c9ba218285a5c11a239";
static const char *spotify_auth_uri      = "https://accounts.spotify.com/authorize";
static const char *spotify_token_uri     = "https://accounts.spotify.com/api/token";
static const char *spotify_playlist_uri	 = "https://api.spotify.com/v1/users/%s/playlists/%s";
static const char *spotify_me_uri        = "https://api.spotify.com/v1/me";


/*--------------------- HELPERS FOR SPOTIFY WEB API -------------------------*/
/*                 All the below is in the httpd thread                      */

static void
free_http_client_ctx(struct http_client_ctx *ctx)
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

static int
request_uri(struct spotify_request *request, const char *uri)
{
  char bearer_token[1024];
  int ret;

  memset(request, 0, sizeof(struct spotify_request));

  if (0 > spotifywebapi_token_refresh(NULL))
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

//  DPRINTF(E_DBG, L_SPOTIFY, "Wep api response for '%s'\n%s\n", uri, request->response_body);

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
  free_http_client_ctx(request->ctx);
  jparse_free(request->haystack);
}

int
spotifywebapi_request_next(struct spotify_request *request, const char *uri, bool append_market)
{
  char *next_uri;
  int ret;

  if (request->ctx && !request->next_uri)
    {
      // Reached end of paging requests, terminate loop
      return -1;
    }

  if (!request->ctx)
    {
      // First paging request
      if (append_market && spotify_user_country)
	{
	  if (strchr(uri, '?'))
	    next_uri = safe_asprintf("%s&market=%s", uri, spotify_user_country);
	  else
	    next_uri = safe_asprintf("%s?market=%s", uri, spotify_user_country);
	}
      else
	next_uri = strdup(uri);
    }
  else
    {
      // Next paging request
      next_uri = strdup(request->next_uri);
      spotifywebapi_request_end(request);
    }

  ret = request_uri(request, next_uri);
  free(next_uri);

  if (ret < 0)
    return ret;

  request->total = jparse_int_from_obj(request->haystack, "total");
  request->next_uri = jparse_str_from_obj(request->haystack, "next");

  if (jparse_array_from_obj(request->haystack, "items", &request->items) < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "No items in reply from Spotify. See:\n%s\n", request->response_body);
      return -1;
    }

  request->count = json_object_array_length(request->items);

  DPRINTF(E_DBG, L_SPOTIFY, "Got %d items\n", request->count);
  return 0;
}

static void
parse_metadata_track(json_object* jsontrack, struct spotify_track* track)
{
  json_object* jsonalbum;
  json_object* jsonartists;
  json_object* needle;

  if (json_object_object_get_ex(jsontrack, "album", &jsonalbum))
    {
      track->album = jparse_str_from_obj(jsonalbum, "name");
      if (json_object_object_get_ex(jsonalbum, "artists", &jsonartists))
	{
	  track->album_artist = jparse_str_from_array(jsonartists, 0, "name");
	}
    }
  if (json_object_object_get_ex(jsontrack, "artists", &jsonartists))
    {
      track->artist = jparse_str_from_array(jsonartists, 0, "name");
    }
  track->disc_number = jparse_int_from_obj(jsontrack, "disc_number");
  track->album_type = jparse_str_from_obj(jsonalbum, "album_type");
  track->is_compilation = (track->album_type && 0 == strcmp(track->album_type, "compilation"));
  track->duration_ms = jparse_int_from_obj(jsontrack, "duration_ms");
  track->name = jparse_str_from_obj(jsontrack, "name");
  track->track_number = jparse_int_from_obj(jsontrack, "track_number");
  track->uri = jparse_str_from_obj(jsontrack, "uri");
  track->id = jparse_str_from_obj(jsontrack, "id");

  // "is_playable" is only returned for a request with a market parameter, default to true if it is not in the response
  if (json_object_object_get_ex(jsontrack, "is_playable", NULL))
    {
      track->is_playable = jparse_bool_from_obj(jsontrack, "is_playable");
      if (json_object_object_get_ex(jsontrack, "restrictions", &needle))
	track->restrictions = json_object_to_json_string(needle);
      if (json_object_object_get_ex(jsontrack, "linked_from", &needle))
	track->linked_from_uri = jparse_str_from_obj(needle, "uri");
    }
  else
    track->is_playable = true;
}

static int
get_year_from_date(const char *date)
{
  char tmp[5];
  uint32_t year = 0;

  if (date && strlen(date) >= 4)
    {
      strncpy(tmp, date, sizeof(tmp));
      tmp[4] = '\0';
      safe_atou32(tmp, &year);
    }

  return year;
}

static void
parse_metadata_album(json_object *jsonalbum, struct spotify_album *album)
{
  json_object* jsonartists;

  if (json_object_object_get_ex(jsonalbum, "artists", &jsonartists))
    {
      album->artist = jparse_str_from_array(jsonartists, 0, "name");
    }
  album->name = jparse_str_from_obj(jsonalbum, "name");
  album->uri = jparse_str_from_obj(jsonalbum, "uri");
  album->id = jparse_str_from_obj(jsonalbum, "id");

  album->album_type = jparse_str_from_obj(jsonalbum, "album_type");
  album->is_compilation = (album->album_type && 0 == strcmp(album->album_type, "compilation"));

  album->label = jparse_str_from_obj(jsonalbum, "label");

  album->release_date = jparse_str_from_obj(jsonalbum, "release_date");
  album->release_date_precision = jparse_str_from_obj(jsonalbum, "release_date_precision");
  album->release_year = get_year_from_date(album->release_date);

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

  parse_metadata_album(jsonalbum, album);

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

  parse_metadata_track(jsontrack, track);

  return 0;
}

static void
parse_metadata_playlist(json_object *jsonplaylist, struct spotify_playlist *playlist)
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
      DPRINTF(E_DBG, L_SPOTIFY, "All playlists processed\n");
      return -1;
    }

  jsonplaylist = json_object_array_get_idx(request->items, request->index);
  if (!jsonplaylist)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error fetching playlist at index '%d'\n", request->index);
      return -1;
    }

  parse_metadata_playlist(jsonplaylist, playlist);
  request->index++;

  return 0;
}

/*
 * Extracts the owner and the id from a spotify playlist uri
 *
 * Playlist-uri has the following format: spotify:user:[owner]:playlist:[id]
 * Owner and plid must be freed by the caller.
 */
static int
get_owner_plid_from_uri(const char *uri, char **owner, char **plid)
{
  char *ptr1;
  char *ptr2;
  char *tmp;
  size_t len;

  ptr1 = strchr(uri, ':');
  if (!ptr1)
    return -1;
  ptr1++;
  ptr1 = strchr(ptr1, ':');
  if (!ptr1)
    return -1;
  ptr1++;
  ptr2 = strchr(ptr1, ':');

  len = ptr2 - ptr1;

  tmp = malloc(sizeof(char) * (len + 1));
  strncpy(tmp, ptr1, len);
  tmp[len] = '\0';
  *owner = tmp;

  ptr2++;
  ptr1 = strchr(ptr2, ':');
  if (!ptr1)
    {
      free(tmp);
      return -1;
    }
  ptr1++;
  *plid = strdup(ptr1);

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

  parse_metadata_track(jsontrack, track);
  track->added_at = jparse_str_from_obj(item, "added_at");
  track->mtime = jparse_time_from_obj(item, "added_at");

  request->index++;

  return 0;
}

int
spotifywebapi_playlist_start(struct spotify_request *request, const char *path, struct spotify_playlist *playlist)
{
  char uri[1024];
  char *owner;
  char *id;
  int ret;

  ret = get_owner_plid_from_uri(path, &owner, &id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error extracting owner and id from playlist uri '%s'\n", path);
      return -1;
    }

  ret = snprintf(uri, sizeof(uri), spotify_playlist_uri, owner, id);
  if (ret < 0 || ret >= sizeof(uri))
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error creating playlist endpoint uri for playlist '%s'\n", path);
      free(owner);
      free(id);
      return -1;
    }

  ret = request_uri(request, uri);
  if (ret < 0)
    {
      free(owner);
      free(id);
      return -1;
    }

  request->haystack = json_tokener_parse(request->response_body);
  parse_metadata_playlist(request->haystack, playlist);

  free(owner);
  free(id);
  return 0;
}

static int
request_user_info()
{
  struct spotify_request request;
  int ret;

  free(spotify_user_country);
  spotify_user_country = NULL;
  free(spotify_user);
  spotify_user = NULL;

  ret = request_uri(&request, spotify_me_uri);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Failed to read user country\n");
    }
  else
    {
      spotify_user = safe_strdup(jparse_str_from_obj(request.haystack, "id"));
      spotify_user_country = safe_strdup(jparse_str_from_obj(request.haystack, "country"));

      DPRINTF(E_DBG, L_SPOTIFY, "User '%s', country '%s'\n", spotify_user, spotify_user_country);
    }

  spotifywebapi_request_end(&request);

  return 0;
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
	  (keyval_add(&kv, "scope", "user-read-private playlist-read-private user-library-read") == 0) &&
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

  free(spotify_access_token);
  spotify_access_token = NULL;

  tmp = jparse_str_from_obj(haystack, "access_token");
  if (tmp)
    spotify_access_token = strdup(tmp);

  tmp = jparse_str_from_obj(haystack, "refresh_token");
  if (tmp)
    {
      free(spotify_refresh_token);
      spotify_refresh_token = strdup(tmp);
    }

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

  if (spotify_refresh_token)
    db_admin_set("spotify_refresh_token", spotify_refresh_token);

  request_user_info();

  ret = 0;

 out_free_input_body:
  evbuffer_free(ctx.input_body);
  free(param);
 out_clear_kv:

  return ret;
}

int
spotifywebapi_token_get(const char *code, const char *redirect_uri, char **user, const char **err)
{
  struct keyval kv;
  int ret;

  *err = "";
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

  if (user && ret == 0)
    {
      *user = safe_strdup(spotify_user);
    }
  keyval_clear(&kv);

  return ret;
}

int
spotifywebapi_token_refresh(char **user)
{
  struct keyval kv;
  char *refresh_token;
  const char *err;
  int ret;

  if (token_requested && difftime(time(NULL), token_requested) < expires_in)
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

  if (user && ret == 0)
    {
      *user = safe_strdup(spotify_user);
    }
  free(refresh_token);
  keyval_clear(&kv);

  return ret;
}

