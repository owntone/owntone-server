/*
 * Copyright (C) 2017 Christian Meffert <christian.meffert@googlemail.com>
 *
 * Adapted from httpd_adm.c:
 * Copyright (C) 2015 Stuart NAIFEH <stu@naifeh.org>
 *
 * Adapted from httpd_daap.c and httpd.c:
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2010 Kai Elwert <elwertk@googlemail.com>
 *
 * Adapted from mt-daapd:
 * Copyright (C) 2003-2007 Ron Pedde <ron@pedde.com>
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

#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "httpd_jsonapi.h"
#include "conffile.h"
#include "db.h"
#ifdef LASTFM
# include "lastfm.h"
#endif
#include "library.h"
#include "logger.h"
#include "misc.h"
#include "misc_json.h"
#include "player.h"
#include "remote_pairing.h"
#include "smartpl_query.h"
#ifdef HAVE_SPOTIFY_H
# include "spotify_webapi.h"
# include "spotify.h"
#endif

/* -------------------------------- HELPERS --------------------------------- */

static inline void
safe_json_add_string(json_object *obj, const char *key, const char *value)
{
  if (value)
    json_object_object_add(obj, key, json_object_new_string(value));
}

static inline void
safe_json_add_int_from_string(json_object *obj, const char *key, const char *value)
{
  int intval;
  int ret;

  if (!value)
    return;

  ret = safe_atoi32(value, &intval);
  if (ret == 0)
    json_object_object_add(obj, key, json_object_new_int(intval));
}

static inline void
safe_json_add_time_from_string(json_object *obj, const char *key, const char *value, bool with_time)
{
  uint32_t tmp;
  time_t timestamp;
  struct tm tm;
  char result[32];

  if (!value)
    return;

  if (safe_atou32(value, &tmp) != 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error converting timestamp to uint32_t: %s\n", value);
      return;
    }

  if (!tmp)
    return;

  timestamp = tmp;
  if (gmtime_r(&timestamp, &tm) == NULL)
    {
      DPRINTF(E_LOG, L_WEB, "Error converting timestamp to gmtime: %s\n", value);
      return;
    }

  if (with_time)
    strftime(result, sizeof(result), "%FT%TZ", &tm);
  else
    strftime(result, sizeof(result), "%F", &tm);

  json_object_object_add(obj, key, json_object_new_string(result));
}

static json_object *
artist_to_json(struct db_group_info *dbgri)
{
  json_object *item;
  char uri[100];
  int ret;

  item = json_object_new_object();

  safe_json_add_string(item, "id", dbgri->persistentid);
  safe_json_add_string(item, "name", dbgri->itemname);
  safe_json_add_string(item, "name_sort", dbgri->itemname_sort);
  safe_json_add_int_from_string(item, "album_count", dbgri->groupalbumcount);
  safe_json_add_int_from_string(item, "track_count", dbgri->itemcount);
  safe_json_add_int_from_string(item, "length_ms", dbgri->song_length);

  ret = snprintf(uri, sizeof(uri), "%s:%s:%s", "library", "artist", dbgri->persistentid);
  if (ret < sizeof(uri))
    json_object_object_add(item, "uri", json_object_new_string(uri));

  return item;
}

static json_object *
album_to_json(struct db_group_info *dbgri)
{
  json_object *item;
  char uri[100];
  int ret;

  item = json_object_new_object();

  safe_json_add_string(item, "id", dbgri->persistentid);
  safe_json_add_string(item, "name", dbgri->itemname);
  safe_json_add_string(item, "name_sort", dbgri->itemname_sort);
  safe_json_add_string(item, "artist", dbgri->songalbumartist);
  safe_json_add_string(item, "artist_id", dbgri->songartistid);
  safe_json_add_int_from_string(item, "track_count", dbgri->itemcount);
  safe_json_add_int_from_string(item, "length_ms", dbgri->song_length);

  ret = snprintf(uri, sizeof(uri), "%s:%s:%s", "library", "album", dbgri->persistentid);
  if (ret < sizeof(uri))
    json_object_object_add(item, "uri", json_object_new_string(uri));

  return item;
}

static json_object *
track_to_json(struct db_media_file_info *dbmfi)
{
  json_object *item;
  char uri[100];
  int intval;
  int ret;

  item = json_object_new_object();

  safe_json_add_int_from_string(item, "id", dbmfi->id);
  safe_json_add_string(item, "title", dbmfi->title);
  safe_json_add_string(item, "artist", dbmfi->artist);
  safe_json_add_string(item, "artist_sort", dbmfi->artist_sort);
  safe_json_add_string(item, "album", dbmfi->album);
  safe_json_add_string(item, "album_sort", dbmfi->album_sort);
  safe_json_add_string(item, "album_id", dbmfi->songalbumid);
  safe_json_add_string(item, "album_artist", dbmfi->album_artist);
  safe_json_add_string(item, "album_artist_sort", dbmfi->album_artist_sort);
  safe_json_add_string(item, "album_artist_id", dbmfi->songartistid);
  safe_json_add_string(item, "genre", dbmfi->genre);
  safe_json_add_int_from_string(item, "year", dbmfi->year);
  safe_json_add_int_from_string(item, "track_number", dbmfi->track);
  safe_json_add_int_from_string(item, "disc_number", dbmfi->disc);
  safe_json_add_int_from_string(item, "length_ms", dbmfi->song_length);

  safe_json_add_int_from_string(item, "play_count", dbmfi->play_count);
  safe_json_add_int_from_string(item, "skip_count", dbmfi->skip_count);
  safe_json_add_time_from_string(item, "time_played", dbmfi->time_played, true);
  safe_json_add_time_from_string(item, "time_skipped", dbmfi->time_skipped, true);
  safe_json_add_time_from_string(item, "time_added", dbmfi->time_added, true);
  safe_json_add_time_from_string(item, "date_released", dbmfi->date_released, false);
  safe_json_add_int_from_string(item, "seek_ms", dbmfi->seek);

  ret = safe_atoi32(dbmfi->media_kind, &intval);
  if (ret == 0)
    safe_json_add_string(item, "media_kind", db_media_kind_label(intval));

  ret = safe_atoi32(dbmfi->data_kind, &intval);
  if (ret == 0)
    safe_json_add_string(item, "data_kind", db_data_kind_label(intval));

  safe_json_add_string(item, "path", dbmfi->path);

  ret = snprintf(uri, sizeof(uri), "%s:%s:%s", "library", "track", dbmfi->id);
  if (ret < sizeof(uri))
    json_object_object_add(item, "uri", json_object_new_string(uri));

  return item;
}

static json_object *
playlist_to_json(struct db_playlist_info *dbpli)
{
  json_object *item;
  char uri[100];
  int intval;
  int ret;

  item = json_object_new_object();

  safe_json_add_int_from_string(item, "id", dbpli->id);
  safe_json_add_string(item, "name", dbpli->title);
  safe_json_add_string(item, "path", dbpli->path);
  ret = safe_atoi32(dbpli->type, &intval);
  if (ret == 0)
    json_object_object_add(item, "smart_playlist", json_object_new_boolean(intval == PL_SMART));

  ret = snprintf(uri, sizeof(uri), "%s:%s:%s", "library", "playlist", dbpli->id);
  if (ret < sizeof(uri))
    json_object_object_add(item, "uri", json_object_new_string(uri));

  return item;
}

static int
fetch_tracks(struct query_params *query_params, json_object *items, int *total)
{
  struct db_media_file_info dbmfi;
  json_object *item;
  int ret = 0;

  ret = db_query_start(query_params);
  if (ret < 0)
    goto error;

  while (((ret = db_query_fetch_file(query_params, &dbmfi)) == 0) && (dbmfi.id))
    {
      item = track_to_json(&dbmfi);
      if (!item)
	{
	  ret = -1;
	  goto error;
	}

      json_object_array_add(items, item);
    }

  if (total)
    *total = query_params->results;

 error:
  db_query_end(query_params);

  return ret;
}

static int
fetch_artists(struct query_params *query_params, json_object *items, int *total)
{
  struct db_group_info dbgri;
  json_object *item;
  int ret = 0;

  ret = db_query_start(query_params);
  if (ret < 0)
    goto error;

  while ((ret = db_query_fetch_group(query_params, &dbgri)) == 0)
    {
      /* Don't add item if no name (eg blank album name) */
      if (strlen(dbgri.itemname) == 0)
	continue;

      item = artist_to_json(&dbgri);
      if (!item)
	{
	  ret = -1;
	  goto error;
	}

      json_object_array_add(items, item);
    }

  if (total)
    *total = query_params->results;

 error:
  db_query_end(query_params);

  return ret;
}

static json_object *
fetch_artist(const char *artist_id)
{
  struct query_params query_params;
  json_object *artist;
  struct db_group_info dbgri;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));
  artist = NULL;

  query_params.type = Q_GROUP_ARTISTS;
  query_params.sort = S_ARTIST;
  query_params.filter = db_mprintf("(f.songartistid = %s)", artist_id);

  ret = db_query_start(&query_params);
  if (ret < 0)
    goto error;

  if ((ret = db_query_fetch_group(&query_params, &dbgri)) == 0)
    {
      artist = artist_to_json(&dbgri);
    }

 error:
  db_query_end(&query_params);

  return artist;
}

static int
fetch_albums(struct query_params *query_params, json_object *items, int *total)
{
  struct db_group_info dbgri;
  json_object *item;
  int ret = 0;

  ret = db_query_start(query_params);
  if (ret < 0)
    goto error;

  while ((ret = db_query_fetch_group(query_params, &dbgri)) == 0)
    {
      /* Don't add item if no name (eg blank album name) */
      if (strlen(dbgri.itemname) == 0)
	continue;

      item = album_to_json(&dbgri);
      if (!item)
	{
	  ret = -1;
	  goto error;
	}

      json_object_array_add(items, item);
    }

  if (total)
    *total = query_params->results;

 error:
  db_query_end(query_params);

  return ret;
}

static json_object *
fetch_album(const char *album_id)
{
  struct query_params query_params;
  json_object *album;
  struct db_group_info dbgri;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));
  album = NULL;

  query_params.type = Q_GROUP_ALBUMS;
  query_params.sort = S_ALBUM;
  query_params.filter = db_mprintf("(f.songalbumid = %s)", album_id);

  ret = db_query_start(&query_params);
  if (ret < 0)
    goto error;

  if ((ret = db_query_fetch_group(&query_params, &dbgri)) == 0)
    {
      album = album_to_json(&dbgri);
    }

 error:
  db_query_end(&query_params);

  return album;
}

static int
fetch_playlists(struct query_params *query_params, json_object *items, int *total)
{
  struct db_playlist_info dbpli;
  json_object *item;
  int ret = 0;

  ret = db_query_start(query_params);
  if (ret < 0)
    goto error;

  while (((ret = db_query_fetch_pl(query_params, &dbpli, 0)) == 0) && (dbpli.id))
    {
      item = playlist_to_json(&dbpli);
      if (!item)
	{
	  ret = -1;
	  goto error;
	}

      json_object_array_add(items, item);
    }

  if (total)
    *total = query_params->results;

 error:
  db_query_end(query_params);

  return ret;
}

static json_object *
fetch_playlist(const char *playlist_id)
{
  struct query_params query_params;
  json_object *playlist;
  struct db_playlist_info dbpli;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));
  playlist = NULL;

  query_params.type = Q_PL;
  query_params.sort = S_PLAYLIST;
  query_params.filter = db_mprintf("(f.id = %s)", playlist_id);

  ret = db_query_start(&query_params);
  if (ret < 0)
    goto error;

  if (((ret = db_query_fetch_pl(&query_params, &dbpli, 0)) == 0) && (dbpli.id))
    {
      playlist = playlist_to_json(&dbpli);
    }

 error:
  db_query_end(&query_params);

  return playlist;
}

static int
query_params_limit_set(struct query_params *query_params, struct httpd_request *hreq)
{
  const char *param;

  query_params->idx_type = I_NONE;
  query_params->limit = -1;
  query_params->offset = 0;

  param = evhttp_find_header(hreq->query, "limit");
  if (param)
    {
      query_params->idx_type = I_SUB;

      if (safe_atoi32(param, &query_params->limit) < 0)
        {
	  DPRINTF(E_LOG, L_WEB, "Invalid value for query parameter 'limit' (%s)\n", param);
	  return -1;
	}

      param = evhttp_find_header(hreq->query, "offset");
      if (param && safe_atoi32(param, &query_params->offset) < 0)
        {
	  DPRINTF(E_LOG, L_WEB, "Invalid value for query parameter 'offset' (%s)\n", param);
	  return -1;
	}
    }

  return 0;
}

/* --------------------------- REPLY HANDLERS ------------------------------- */

/*
 * Endpoint to retrieve configuration values
 *
 * Example response:
 *
 * {
 *  "websocket_port": 6603,
 *  "version": "25.0"
 * }
 */
static int
jsonapi_reply_config(struct httpd_request *hreq)
{
  json_object *jreply;
  json_object *buildopts;
  int websocket_port;
  char **buildoptions;
  int i;

  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  // library name
  json_object_object_add(jreply, "library_name", json_object_new_string(cfg_getstr(cfg_getsec(cfg, "library"), "name")));

  // hide singles
  json_object_object_add(jreply, "hide_singles", json_object_new_boolean(cfg_getbool(cfg_getsec(cfg, "library"), "hide_singles")));

  // Websocket port
#ifdef HAVE_LIBWEBSOCKETS
  websocket_port = cfg_getint(cfg_getsec(cfg, "general"), "websocket_port");
#else
  websocket_port = 0;
#endif
  json_object_object_add(jreply, "websocket_port", json_object_new_int(websocket_port));

  // forked-daapd version
  json_object_object_add(jreply, "version", json_object_new_string(VERSION));

  // enabled build options
  buildopts = json_object_new_array();
  buildoptions = buildopts_get();
  for (i = 0; buildoptions[i]; i++)
    {
      json_object_array_add(buildopts, json_object_new_string(buildoptions[i]));
    }
  json_object_object_add(jreply, "buildoptions", buildopts);

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);

  return HTTP_OK;
}

/*
 * Endpoint to retrieve informations about the library
 *
 * Example response:
 *
 * {
 *  "artists": 84,
 *  "albums": 151,
 *  "songs": 3085,
 *  "db_playtime": 687824,
 *  "updating": false
 *}
 */
static int
jsonapi_reply_library(struct httpd_request *hreq)
{
  struct query_params qp;
  struct filecount_info fci;
  json_object *jreply;
  int ret;


  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_COUNT_ITEMS;
  ret = db_filecount_get(&fci, &qp);
  if (ret == 0)
    {
      json_object_object_add(jreply, "songs", json_object_new_int(fci.count));
      json_object_object_add(jreply, "db_playtime", json_object_new_int64((fci.length / 1000)));
      json_object_object_add(jreply, "artists", json_object_new_int(fci.artist_count));
      json_object_object_add(jreply, "albums", json_object_new_int(fci.album_count));
    }
  else
    {
      DPRINTF(E_LOG, L_WEB, "library: failed to get file count info\n");
    }

  safe_json_add_time_from_string(jreply, "started_at", db_admin_get(DB_ADMIN_START_TIME), true);
  safe_json_add_time_from_string(jreply, "updated_at", db_admin_get(DB_ADMIN_DB_UPDATE), true);

  json_object_object_add(jreply, "updating", json_object_new_boolean(library_is_scanning()));

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));
  jparse_free(jreply);

  return HTTP_OK;
}

/*
 * Endpoint to trigger a library rescan
 */
static int
jsonapi_reply_update(struct httpd_request *hreq)
{
  library_rescan();
  return HTTP_NOCONTENT;
}

/*
 * Endpoint to retrieve information about the spotify integration
 *
 * Exampe response:
 *
 * {
 *  "enabled": true,
 *  "oauth_uri": "https://accounts.spotify.com/authorize/?client_id=...
 * }
 */
static int
jsonapi_reply_spotify(struct httpd_request *hreq)
{
  json_object *jreply;

  CHECK_NULL(L_WEB, jreply = json_object_new_object());

#ifdef HAVE_SPOTIFY_H
  int httpd_port;
  char redirect_uri[256];
  char *oauth_uri;
  struct spotify_status_info info;
  struct spotifywebapi_status_info webapi_info;
  struct spotifywebapi_access_token webapi_token;

  json_object_object_add(jreply, "enabled", json_object_new_boolean(true));

  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");
  snprintf(redirect_uri, sizeof(redirect_uri), "http://forked-daapd.local:%d/oauth/spotify", httpd_port);

  oauth_uri = spotifywebapi_oauth_uri_get(redirect_uri);
  if (!oauth_uri)
    {
      DPRINTF(E_LOG, L_WEB, "Cannot display Spotify oauth interface (http_form_uriencode() failed)\n");
      jparse_free(jreply);
      return HTTP_INTERNAL;
    }

  json_object_object_add(jreply, "oauth_uri", json_object_new_string(oauth_uri));
  free(oauth_uri);

  spotify_status_info_get(&info);
  json_object_object_add(jreply, "libspotify_installed", json_object_new_boolean(info.libspotify_installed));
  json_object_object_add(jreply, "libspotify_logged_in", json_object_new_boolean(info.libspotify_logged_in));
  safe_json_add_string(jreply, "libspotify_user", info.libspotify_user);

  spotifywebapi_status_info_get(&webapi_info);
  json_object_object_add(jreply, "webapi_token_valid", json_object_new_boolean(webapi_info.token_valid));
  safe_json_add_string(jreply, "webapi_user", webapi_info.user);
  safe_json_add_string(jreply, "webapi_country", webapi_info.country);

  spotifywebapi_access_token_get(&webapi_token);
  safe_json_add_string(jreply, "webapi_token", webapi_token.token);
  json_object_object_add(jreply, "webapi_token_expires_in", json_object_new_int(webapi_token.expires_in));

#else
  json_object_object_add(jreply, "enabled", json_object_new_boolean(false));
#endif

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);

  return HTTP_OK;
}

static int
jsonapi_reply_spotify_login(struct httpd_request *hreq)
{
#ifdef HAVE_SPOTIFY_H
  struct evbuffer *in_evbuf;
  json_object* request;
  const char *user;
  const char *password;
  char *errmsg = NULL;
  json_object* jreply;
  json_object* errors;
  int ret;

  DPRINTF(E_DBG, L_WEB, "Received Spotify login request\n");

  in_evbuf = evhttp_request_get_input_buffer(hreq->req);

  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return HTTP_BADREQUEST;
    }

  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  user = jparse_str_from_obj(request, "user");
  password = jparse_str_from_obj(request, "password");
  if (user && strlen(user) > 0 && password && strlen(password) > 0)
    {
      ret = spotify_login_user(user, password, &errmsg);
      if (ret < 0)
	{
	  json_object_object_add(jreply, "success", json_object_new_boolean(false));
	  errors = json_object_new_object();
	  json_object_object_add(errors, "error", json_object_new_string(errmsg));
	  json_object_object_add(jreply, "errors", errors);
	}
      else
	{
	  json_object_object_add(jreply, "success", json_object_new_boolean(true));
	}
      free(errmsg);
    }
  else
    {
      DPRINTF(E_LOG, L_WEB, "No user or password in spotify login post request\n");

      json_object_object_add(jreply, "success", json_object_new_boolean(false));
      errors = json_object_new_object();
      if (!user || strlen(user) == 0)
	json_object_object_add(errors, "user", json_object_new_string("Username is required"));
      if (!password || strlen(password) == 0)
	json_object_object_add(errors, "password", json_object_new_string("Password is required"));
      json_object_object_add(jreply, "errors", errors);
    }

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);

#else
  DPRINTF(E_LOG, L_WEB, "Received spotify login request but was not compiled with enable-spotify\n");
#endif

  return HTTP_OK;
}

static int
jsonapi_reply_lastfm(struct httpd_request *hreq)
{
  json_object *jreply;
  bool enabled = false;
  bool scrobbling_enabled = false;

#ifdef LASTFM
  enabled = true;
  scrobbling_enabled = lastfm_is_enabled();
#endif

  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  json_object_object_add(jreply, "enabled", json_object_new_boolean(enabled));
  json_object_object_add(jreply, "scrobbling_enabled", json_object_new_boolean(scrobbling_enabled));

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);

  return HTTP_OK;
}

/*
 * Endpoint to log into LastFM
 */
static int
jsonapi_reply_lastfm_login(struct httpd_request *hreq)
{
#ifdef LASTFM
  struct evbuffer *in_evbuf;
  json_object *request;
  const char *user;
  const char *password;
  char *errmsg = NULL;
  json_object *jreply;
  json_object *errors;
  int ret;

  DPRINTF(E_DBG, L_WEB, "Received LastFM login request\n");

  in_evbuf = evhttp_request_get_input_buffer(hreq->req);
  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return HTTP_BADREQUEST;
    }

  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  user = jparse_str_from_obj(request, "user");
  password = jparse_str_from_obj(request, "password");
  if (user && strlen(user) > 0 && password && strlen(password) > 0)
    {
      ret = lastfm_login_user(user, password, &errmsg);
      if (ret < 0)
        {
	  json_object_object_add(jreply, "success", json_object_new_boolean(false));
	  errors = json_object_new_object();
	  if (errmsg)
	    json_object_object_add(errors, "error", json_object_new_string(errmsg));
	  else
	    json_object_object_add(errors, "error", json_object_new_string("Unknown error"));
	  json_object_object_add(jreply, "errors", errors);
	}
      else
        {
	  json_object_object_add(jreply, "success", json_object_new_boolean(true));
	}
      free(errmsg);
    }
  else
    {
      DPRINTF(E_LOG, L_WEB, "No user or password in LastFM login post request\n");

      json_object_object_add(jreply, "success", json_object_new_boolean(false));
      errors = json_object_new_object();
      if (!user || strlen(user) == 0)
	json_object_object_add(errors, "user", json_object_new_string("Username is required"));
      if (!password || strlen(password) == 0)
	json_object_object_add(errors, "password", json_object_new_string("Password is required"));
      json_object_object_add(jreply, "errors", errors);
    }

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);

#else
  DPRINTF(E_LOG, L_WEB, "Received LastFM login request but was not compiled with enable-lastfm\n");
#endif

  return HTTP_OK;
}

static int
jsonapi_reply_lastfm_logout(struct httpd_request *hreq)
{
#ifdef LASTFM
  lastfm_logout();
#endif
  return HTTP_NOCONTENT;
}

/*
 * Kicks off pairing of a daap/dacp client
 *
 * Expects the paring pin to be present in the post request body, e. g.:
 *
 * {
 *   "pin": "1234"
 * }
 */
static int
jsonapi_reply_pairing_kickoff(struct httpd_request *hreq)
{
  struct evbuffer *evbuf;
  json_object* request;
  const char* message;

  evbuf = evhttp_request_get_input_buffer(hreq->req);
  request = jparse_obj_from_evbuffer(evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return HTTP_BADREQUEST;
    }

  DPRINTF(E_DBG, L_WEB, "Received pairing post request: %s\n", json_object_to_json_string(request));

  message = jparse_str_from_obj(request, "pin");
  if (message)
    remote_pairing_kickoff((char **)&message);
  else
    DPRINTF(E_LOG, L_WEB, "Missing pin in request body: %s\n", json_object_to_json_string(request));

  jparse_free(request);

  return HTTP_NOCONTENT;
}

/*
 * Retrieves pairing information
 *
 * Example response:
 *
 * {
 *  "active": true,
 *  "remote": "remote name"
 * }
 */
static int
jsonapi_reply_pairing_get(struct httpd_request *hreq)
{
  char *remote_name;
  json_object *jreply;

  remote_name = remote_pairing_get_name();

  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  if (remote_name)
    {
      json_object_object_add(jreply, "active", json_object_new_boolean(true));
      json_object_object_add(jreply, "remote", json_object_new_string(remote_name));
    }
  else
    {
      json_object_object_add(jreply, "active", json_object_new_boolean(false));
    }

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);
  free(remote_name);

  return HTTP_OK;
}

struct outputs_param
{
  json_object *output;
  uint64_t output_id;
};

static json_object *
speaker_to_json(struct spk_info *spk)
{
  json_object *output;
  char output_id[21];

  output = json_object_new_object();

  snprintf(output_id, sizeof(output_id), "%" PRIu64, spk->id);
  json_object_object_add(output, "id", json_object_new_string(output_id));
  json_object_object_add(output, "name", json_object_new_string(spk->name));
  json_object_object_add(output, "type", json_object_new_string(spk->output_type));
  json_object_object_add(output, "selected", json_object_new_boolean(spk->selected));
  json_object_object_add(output, "has_password", json_object_new_boolean(spk->has_password));
  json_object_object_add(output, "requires_auth", json_object_new_boolean(spk->requires_auth));
  json_object_object_add(output, "needs_auth_key", json_object_new_boolean(spk->needs_auth_key));
  json_object_object_add(output, "volume", json_object_new_int(spk->absvol));

  return output;
}

static void
speaker_enum_cb(struct spk_info *spk, void *arg)
{
  json_object *outputs;
  json_object *output;

  outputs = arg;

  output = speaker_to_json(spk);
  json_object_array_add(outputs, output);
}

static void
speaker_get_cb(struct spk_info *spk, void *arg)
{
  struct outputs_param *outputs_param = arg;

  if (outputs_param->output_id == spk->id)
    {
      outputs_param->output = speaker_to_json(spk);
    }
}

/*
 * GET /api/outputs/[output_id]
 */
static int
jsonapi_reply_outputs_get_byid(struct httpd_request *hreq)
{
  struct outputs_param outputs_param;
  uint64_t output_id;
  int ret;

  ret = safe_atou64(hreq->uri_parsed->path_parts[2], &output_id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid output id given to outputs endpoint '%s'\n", hreq->uri_parsed->path);

      return HTTP_BADREQUEST;
    }

  outputs_param.output_id = output_id;
  outputs_param.output = NULL;

  player_speaker_enumerate(speaker_get_cb, &outputs_param);

  if (!outputs_param.output)
    {
      DPRINTF(E_LOG, L_WEB, "No output found for '%s'\n", hreq->uri_parsed->path);

      return HTTP_BADREQUEST;
    }

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(outputs_param.output)));

  jparse_free(outputs_param.output);

  return HTTP_OK;
}

/*
 * PUT /api/outputs/[output_id]
 */
static int
jsonapi_reply_outputs_put_byid(struct httpd_request *hreq)
{
  uint64_t output_id;
  struct evbuffer *in_evbuf;
  json_object* request;
  bool selected;
  int volume;
  int ret;

  ret = safe_atou64(hreq->uri_parsed->path_parts[2], &output_id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid output id given to outputs endpoint '%s'\n", hreq->uri_parsed->path);

      return HTTP_BADREQUEST;
    }

  in_evbuf = evhttp_request_get_input_buffer(hreq->req);
  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");

      return HTTP_BADREQUEST;
    }

  ret = 0;

  if (jparse_contains_key(request, "selected", json_type_boolean))
    {
      selected = jparse_bool_from_obj(request, "selected");
      if (selected)
	ret = player_speaker_enable(output_id);
      else
	ret = player_speaker_disable(output_id);
    }

  if (ret == 0 && jparse_contains_key(request, "volume", json_type_int))
    {
      volume = jparse_int_from_obj(request, "volume");
      ret = player_volume_setabs_speaker(output_id, volume);
    }

  jparse_free(request);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_NOCONTENT;
}

/*
 * Endpoint "/api/outputs"
 */
static int
jsonapi_reply_outputs(struct httpd_request *hreq)
{
  json_object *outputs;
  json_object *jreply;

  outputs = json_object_new_array();

  player_speaker_enumerate(speaker_enum_cb, outputs);

  jreply = json_object_new_object();
  json_object_object_add(jreply, "outputs", outputs);

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);

  return HTTP_OK;
}

static int
jsonapi_reply_verification(struct httpd_request *hreq)
{
  struct evbuffer *in_evbuf;
  json_object* request;
  const char* message;

  in_evbuf = evhttp_request_get_input_buffer(hreq->req);
  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return HTTP_BADREQUEST;
    }

  DPRINTF(E_DBG, L_WEB, "Received verification post request: %s\n", json_object_to_json_string(request));

  message = jparse_str_from_obj(request, "pin");
  if (message)
    player_raop_verification_kickoff((char **)&message);
  else
    DPRINTF(E_LOG, L_WEB, "Missing pin in request body: %s\n", json_object_to_json_string(request));

  jparse_free(request);

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_outputs_set(struct httpd_request *hreq)
{
  struct evbuffer *in_evbuf;
  json_object *request;
  json_object *outputs;
  json_object *output_id;
  int nspk, i, ret;
  uint64_t *ids;

  in_evbuf = evhttp_request_get_input_buffer(hreq->req);
  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return HTTP_BADREQUEST;
    }

  DPRINTF(E_DBG, L_WEB, "Received select-outputs post request: %s\n", json_object_to_json_string(request));

  ret = jparse_array_from_obj(request, "outputs", &outputs);
  if (ret == 0)
    {
      nspk = json_object_array_length(outputs);

      ids = calloc((nspk + 1), sizeof(uint64_t));
      ids[0] = nspk;

      ret = 0;
      for (i = 0; i < nspk; i++)
	{
	  output_id = json_object_array_get_idx(outputs, i);
	  ret = safe_atou64(json_object_get_string(output_id), &ids[i + 1]);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_WEB, "Failed to convert output id: %s\n", json_object_to_json_string(request));
	      break;
	    }
	}

      if (ret == 0)
	player_speaker_set(ids);

      free(ids);
    }
  else
    DPRINTF(E_LOG, L_WEB, "Missing outputs in request body: %s\n", json_object_to_json_string(request));

  jparse_free(request);

  return HTTP_NOCONTENT;
}

static int
play_item_with_id(const char *param)
{
  uint32_t item_id;
  struct db_queue_item *queue_item;
  int ret;

  ret = safe_atou32(param, &item_id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid item id given '%s'\n", param);

      return HTTP_BADREQUEST;
    }

  queue_item = db_queue_fetch_byitemid(item_id);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_WEB, "No queue item with item id '%d'\n", item_id);

      return HTTP_BADREQUEST;
    }

  player_playback_stop();
  ret = player_playback_start_byitem(queue_item);
  free_queue_item(queue_item, 0);

  return HTTP_NOCONTENT;
}

static int
play_item_at_position(const char *param)
{
  uint32_t position;
  struct db_queue_item *queue_item;
  int ret;

  ret = safe_atou32(param, &position);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid position given '%s'\n", param);

      return HTTP_BADREQUEST;
    }

  queue_item = db_queue_fetch_bypos(position, 0);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_WEB, "No queue item at position '%d'\n", position);

      return HTTP_BADREQUEST;
    }

  player_playback_stop();
  ret = player_playback_start_byitem(queue_item);
  free_queue_item(queue_item, 0);

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_play(struct httpd_request *hreq)
{
  const char *param;
  int ret;

  if ((param = evhttp_find_header(hreq->query, "item_id")))
    {
      return play_item_with_id(param);
    }
  else if ((param = evhttp_find_header(hreq->query, "position")))
    {
      return play_item_at_position(param);
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error starting playback.\n");
      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_pause(struct httpd_request *hreq)
{
  int ret;

  ret = player_playback_pause();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error pausing playback.\n");
      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_stop(struct httpd_request *hreq)
{
  int ret;

  ret = player_playback_stop();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error stopping playback.\n");
      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_next(struct httpd_request *hreq)
{
  int ret;

  ret = player_playback_next();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error switching to next item.\n");
      return HTTP_INTERNAL;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error starting playback after switching to next item.\n");
      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_previous(struct httpd_request *hreq)
{
  int ret;

  ret = player_playback_prev();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error switching to previous item.\n");
      return HTTP_INTERNAL;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error starting playback after switching to previous item.\n");
      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_seek(struct httpd_request *hreq)
{
  const char *param;
  int position_ms;
  int ret;

  param = evhttp_find_header(hreq->query, "position_ms");
  if (!param)
    return HTTP_BADREQUEST;

  ret = safe_atoi32(param, &position_ms);
  if (ret < 0)
    return HTTP_BADREQUEST;

  ret = player_playback_seek(position_ms);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error seeking to position %d.\n", position_ms);
      return HTTP_INTERNAL;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Error starting playback after seeking to position %d.\n", position_ms);
      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player(struct httpd_request *hreq)
{
  struct player_status status;
  struct db_queue_item *queue_item;
  json_object *reply;

  player_get_status(&status);

  reply = json_object_new_object();

  switch (status.status)
    {
      case PLAY_PAUSED:
	json_object_object_add(reply, "state", json_object_new_string("pause"));
	break;

      case PLAY_PLAYING:
	json_object_object_add(reply, "state", json_object_new_string("play"));
	break;

      default:
	json_object_object_add(reply, "state", json_object_new_string("stop"));
	break;
    }

  switch (status.repeat)
    {
      case REPEAT_SONG:
	json_object_object_add(reply, "repeat", json_object_new_string("single"));
	break;

      case REPEAT_ALL:
	json_object_object_add(reply, "repeat", json_object_new_string("all"));
	break;

      default:
	json_object_object_add(reply, "repeat", json_object_new_string("off"));
	break;
    }

  json_object_object_add(reply, "consume", json_object_new_boolean(status.consume));
  json_object_object_add(reply, "shuffle", json_object_new_boolean(status.shuffle));
  json_object_object_add(reply, "volume", json_object_new_int(status.volume));

  if (status.item_id)
    {
      json_object_object_add(reply, "item_id", json_object_new_int(status.item_id));
      json_object_object_add(reply, "item_length_ms", json_object_new_int(status.len_ms));
      json_object_object_add(reply, "item_progress_ms", json_object_new_int(status.pos_ms));
    }
  else
    {
      queue_item = db_queue_fetch_bypos(0, status.shuffle);

      if (queue_item)
	{
	  json_object_object_add(reply, "item_id", json_object_new_int(queue_item->id));
	  json_object_object_add(reply, "item_length_ms", json_object_new_int(queue_item->song_length));
	  json_object_object_add(reply, "item_progress_ms", json_object_new_int(0));
	  free_queue_item(queue_item, 0);
	}
      else
	{
	  json_object_object_add(reply, "item_id", json_object_new_int(0));
	  json_object_object_add(reply, "item_length_ms", json_object_new_int(0));
	  json_object_object_add(reply, "item_progress_ms", json_object_new_int(0));
	}
    }

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply)));

  jparse_free(reply);

  return HTTP_OK;
}

static json_object *
queue_item_to_json(struct db_queue_item *queue_item, char shuffle)
{
  json_object *item;
  char uri[100];
  int ret;

  item = json_object_new_object();

  json_object_object_add(item, "id", json_object_new_int(queue_item->id));
  if (shuffle)
    json_object_object_add(item, "position", json_object_new_int(queue_item->shuffle_pos));
  else
    json_object_object_add(item, "position", json_object_new_int(queue_item->pos));

  json_object_object_add(item, "track_id", json_object_new_int(queue_item->file_id));

  safe_json_add_string(item, "title", queue_item->title);
  safe_json_add_string(item, "artist", queue_item->artist);
  safe_json_add_string(item, "artist_sort", queue_item->artist_sort);
  safe_json_add_string(item, "album", queue_item->album);
  safe_json_add_string(item, "album_sort", queue_item->album_sort);
  safe_json_add_string(item, "album_artist", queue_item->album_artist);
  safe_json_add_string(item, "album_artist_sort", queue_item->album_artist_sort);
  safe_json_add_string(item, "genre", queue_item->genre);

  json_object_object_add(item, "year", json_object_new_int(queue_item->year));
  json_object_object_add(item, "track_number", json_object_new_int(queue_item->track));
  json_object_object_add(item, "disc_number", json_object_new_int(queue_item->disc));
  json_object_object_add(item, "length_ms", json_object_new_int(queue_item->song_length));

  safe_json_add_string(item, "media_kind", db_media_kind_label(queue_item->media_kind));
  safe_json_add_string(item, "data_kind", db_data_kind_label(queue_item->data_kind));

  safe_json_add_string(item, "path", queue_item->path);

  if (queue_item->file_id > 0)
    {
      ret = snprintf(uri, sizeof(uri), "%s:%s:%d", "library", "track", queue_item->file_id);
      if (ret < sizeof(uri))
	json_object_object_add(item, "uri", json_object_new_string(uri));
    }
  else
    {
      safe_json_add_string(item, "uri", queue_item->path);
    }

  return item;
}

static int
queue_tracks_add_artist(const char *id)
{
  struct query_params query_params;
  struct player_status status;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));

  query_params.type = Q_ITEMS;
  query_params.sort = S_ALBUM;
  query_params.idx_type = I_NONE;
  query_params.filter = db_mprintf("(f.songartistid = %q)", id);

  player_get_status(&status);

  ret = db_queue_add_by_query(&query_params, status.shuffle, status.item_id);

  free(query_params.filter);

  return ret;
}

static int
queue_tracks_add_album(const char *id)
{
  struct query_params query_params;
  struct player_status status;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));

  query_params.type = Q_ITEMS;
  query_params.sort = S_ALBUM;
  query_params.idx_type = I_NONE;
  query_params.filter = db_mprintf("(f.songalbumid = %q)", id);

  player_get_status(&status);

  ret = db_queue_add_by_query(&query_params, status.shuffle, status.item_id);

  free(query_params.filter);

  return ret;
}

static int
queue_tracks_add_track(const char *id)
{
  struct query_params query_params;
  struct player_status status;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));

  query_params.type = Q_ITEMS;
  query_params.sort = S_ALBUM;
  query_params.idx_type = I_NONE;
  query_params.filter = db_mprintf("(f.id = %q)", id);

  player_get_status(&status);

  ret = db_queue_add_by_query(&query_params, status.shuffle, status.item_id);

  free(query_params.filter);

  return ret;
}

static int
queue_tracks_add_playlist(const char *id)
{
  struct player_status status;
  int playlist_id;
  int ret;

  ret = safe_atoi32(id, &playlist_id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid playlist id given '%s'\n", id);

      return HTTP_BADREQUEST;
    }

  player_get_status(&status);

  ret = db_queue_add_by_playlistid(playlist_id, status.shuffle, status.item_id);

  return ret;
}

static int
jsonapi_reply_queue_tracks_add(struct httpd_request *hreq)
{
  const char *param;
  char *uris;
  char *uri;
  const char *id;
  int ret = 0;

  param = evhttp_find_header(hreq->query, "uris");
  if (!param)
    {
      DPRINTF(E_LOG, L_WEB, "Missing query parameter 'uris'\n");

      return HTTP_BADREQUEST;
    }

  uris = strdup(param);
  uri = strtok(uris, ",");

  do
    {
      if (strncmp(uri, "library:artist:", strlen("library:artist:")) == 0)
	{
	  id = uri + (strlen("library:artist:"));
	  queue_tracks_add_artist(id);
	}
      else if (strncmp(uri, "library:album:", strlen("library:album:")) == 0)
	{
	  id = uri + (strlen("library:album:"));
	  queue_tracks_add_album(id);
	}
      else if (strncmp(uri, "library:track:", strlen("library:track:")) == 0)
	{
	  id = uri + (strlen("library:track:"));
	  queue_tracks_add_track(id);
	}
      else if (strncmp(uri, "library:playlist:", strlen("library:playlist:")) == 0)
	{
	  id = uri + (strlen("library:playlist:"));
	  queue_tracks_add_playlist(id);
	}
      else
	{
	  ret = library_queue_add(uri);
	  if (ret != LIBRARY_OK)
	    {
	      DPRINTF(E_LOG, L_WEB, "Invalid uri '%s'\n", uri);
	      break;
	    }
	}
    }
  while ((uri = strtok(NULL, ",")));

  free(uris);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_queue_tracks_move(struct httpd_request *hreq)
{
  uint32_t item_id;
  uint32_t new_position;
  const char *param;
  struct player_status status;
  int ret;

  ret = safe_atou32(hreq->uri_parsed->path_parts[3], &item_id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid item id given '%s'\n", hreq->uri_parsed->path);

      return HTTP_BADREQUEST;
    }

  param = evhttp_find_header(hreq->query, "new_position");
  if (!param)
    {
      DPRINTF(E_LOG, L_WEB, "Missing parameter 'new_position'\n");

      return HTTP_BADREQUEST;
    }
  if (safe_atou32(param, &new_position) < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid item new_position '%s'\n", param);

      return HTTP_BADREQUEST;
    }

  player_get_status(&status);
  ret = db_queue_move_byitemid(item_id, new_position, status.shuffle);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Moving item '%d' to new position %d failed\n", item_id, new_position);

      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_queue_tracks_delete(struct httpd_request *hreq)
{
  uint32_t item_id;
  int ret;

  ret = safe_atou32(hreq->uri_parsed->path_parts[3], &item_id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid item id given '%s'\n", hreq->uri_parsed->path);

      return HTTP_BADREQUEST;
    }

  ret = db_queue_delete_byitemid(item_id);
  if (ret < 0)
    {
      return HTTP_INTERNAL;
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_queue_clear(struct httpd_request *hreq)
{
  player_playback_stop();
  db_queue_clear(0);

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_queue(struct httpd_request *hreq)
{
  struct query_params query_params;
  const char *param;
  uint32_t item_id;
  int start_pos, end_pos;
  int version;
  int count;
  char etag[21];
  struct player_status status;
  struct db_queue_item queue_item;
  json_object *reply;
  json_object *items;
  json_object *item;
  int ret = 0;

  version = db_admin_getint(DB_ADMIN_QUEUE_VERSION);
  count = db_queue_get_count();

  snprintf(etag, sizeof(etag), "%d", version);
  if (httpd_request_etag_matches(hreq->req, etag))
    return HTTP_NOTMODIFIED;

  memset(&query_params, 0, sizeof(struct query_params));
  reply = json_object_new_object();

  json_object_object_add(reply, "version", json_object_new_int(version));
  json_object_object_add(reply, "count", json_object_new_int(count));

  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  player_get_status(&status);
  if (status.shuffle)
    query_params.sort = S_SHUFFLE_POS;

  param = evhttp_find_header(hreq->query, "id");
  if (param && safe_atou32(param, &item_id) == 0)
    {
      query_params.filter = db_mprintf("id = %d", item_id);
    }
  else
    {
      param = evhttp_find_header(hreq->query, "start");
      if (param && safe_atoi32(param, &start_pos) == 0)
	{
	  param = evhttp_find_header(hreq->query, "end");
	  if (!param || safe_atoi32(param, &end_pos) != 0)
	    {
	      end_pos = start_pos + 1;
	    }

	  if (query_params.sort == S_SHUFFLE_POS)
	    query_params.filter = db_mprintf("shuffle_pos >= %d AND shuffle_pos < %d", start_pos, end_pos);
	  else
	    query_params.filter = db_mprintf("pos >= %d AND pos < %d", start_pos, end_pos);
	}
    }

  ret = db_queue_enum_start(&query_params);
  if (ret < 0)
    goto db_start_error;

  while ((ret = db_queue_enum_fetch(&query_params, &queue_item)) == 0 && queue_item.id > 0)
    {
      item = queue_item_to_json(&queue_item, status.shuffle);
      if (!item)
	goto error;

      json_object_array_add(items, item);
    }

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "outputs: Couldn't add outputs to response buffer.\n");

 error:
  db_queue_enum_end(&query_params);
 db_start_error:
  jparse_free(reply);
  free(query_params.filter);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_player_repeat(struct httpd_request *hreq)
{
  const char *param;

  param = evhttp_find_header(hreq->query, "state");
  if (!param)
    return HTTP_BADREQUEST;

  if (strcmp(param, "single") == 0)
    {
      player_repeat_set(REPEAT_SONG);
    }
  else if (strcmp(param, "all") == 0)
    {
      player_repeat_set(REPEAT_ALL);
    }
  else if (strcmp(param, "off") == 0)
    {
      player_repeat_set(REPEAT_OFF);
    }

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_shuffle(struct httpd_request *hreq)
{
  const char *param;
  bool shuffle;

  param = evhttp_find_header(hreq->query, "state");
  if (!param)
    return HTTP_BADREQUEST;

  shuffle = (strcmp(param, "true") == 0);
  player_shuffle_set(shuffle);

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_consume(struct httpd_request *hreq)
{
  const char *param;
  bool consume;

  param = evhttp_find_header(hreq->query, "state");
  if (!param)
    return HTTP_BADREQUEST;

  consume = (strcmp(param, "true") == 0);
  player_consume_set(consume);

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_player_volume(struct httpd_request *hreq)
{
  const char *param;
  uint64_t output_id;
  int volume;
  int ret;

  param = evhttp_find_header(hreq->query, "volume");
  if (!param)
    return HTTP_BADREQUEST;

  ret = safe_atoi32(param, &volume);
  if (ret < 0)
    return HTTP_BADREQUEST;

  if (volume < 0 || volume > 100)
    return HTTP_BADREQUEST;

  param = evhttp_find_header(hreq->query, "output_id");
  if (param)
    {
      ret = safe_atou64(param, &output_id);
      if (ret < 0)
	return HTTP_BADREQUEST;

      ret = player_volume_setabs_speaker(output_id, volume);
    }
  else
    {
      ret = player_volume_set(volume);
    }

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_NOCONTENT;
}

static int
jsonapi_reply_library_artists(struct httpd_request *hreq)
{
  time_t db_update;
  struct query_params query_params;
  const char *param;
  enum media_kind media_kind;
  json_object *reply;
  json_object *items;
  int total;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  media_kind = 0;
  param = evhttp_find_header(hreq->query, "media_kind");
  if (param)
    {
      media_kind = db_media_kind_enum(param);
      if (!media_kind)
	{
	  DPRINTF(E_LOG, L_WEB, "Invalid media kind '%s'\n", param);
	  return HTTP_BADREQUEST;
	}
    }

  reply = json_object_new_object();
  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto error;

  query_params.type = Q_GROUP_ARTISTS;
  query_params.sort = S_ARTIST;

  if (media_kind)
    query_params.filter = db_mprintf("(f.media_kind = %d)", media_kind);

  ret = fetch_artists(&query_params, items, &total);
  if (ret < 0)
    goto error;

  json_object_object_add(reply, "total", json_object_new_int(total));
  json_object_object_add(reply, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(reply, "limit", json_object_new_int(query_params.limit));

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add artists to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_artist(struct httpd_request *hreq)
{
  time_t db_update;
  const char *artist_id;
  json_object *reply;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  artist_id = hreq->uri_parsed->path_parts[3];

  reply = fetch_artist(artist_id);
  if (!reply)
    {
      ret = -1;
      goto error;
    }

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add artists to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_artist_albums(struct httpd_request *hreq)
{
  time_t db_update;
  struct query_params query_params;
  const char *artist_id;
  json_object *reply;
  json_object *items;
  int total;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  artist_id = hreq->uri_parsed->path_parts[3];

  reply = json_object_new_object();
  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto error;

  query_params.type = Q_GROUP_ALBUMS;
  query_params.sort = S_ALBUM;
  query_params.filter = db_mprintf("(f.songartistid = %q)", artist_id);

  ret = fetch_albums(&query_params, items, &total);
  free(query_params.filter);

  if (ret < 0)
    goto error;

  json_object_object_add(reply, "total", json_object_new_int(total));
  json_object_object_add(reply, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(reply, "limit", json_object_new_int(query_params.limit));

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add albums to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_albums(struct httpd_request *hreq)
{
  time_t db_update;
  struct query_params query_params;
  const char *param;
  enum media_kind media_kind;
  json_object *reply;
  json_object *items;
  int total;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  media_kind = 0;
  param = evhttp_find_header(hreq->query, "media_kind");
  if (param)
    {
      media_kind = db_media_kind_enum(param);
      if (!media_kind)
	{
	  DPRINTF(E_LOG, L_WEB, "Invalid media kind '%s'\n", param);
	  return HTTP_BADREQUEST;
	}
    }

  reply = json_object_new_object();
  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto error;

  query_params.type = Q_GROUP_ALBUMS;
  query_params.sort = S_ALBUM;

  if (media_kind)
    query_params.filter = db_mprintf("(f.media_kind = %d)", media_kind);

  ret = fetch_albums(&query_params, items, &total);
  if (ret < 0)
    goto error;

  json_object_object_add(reply, "total", json_object_new_int(total));
  json_object_object_add(reply, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(reply, "limit", json_object_new_int(query_params.limit));

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add albums to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_album(struct httpd_request *hreq)
{
  time_t db_update;
  const char *album_id;
  json_object *reply;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  album_id = hreq->uri_parsed->path_parts[3];

  reply = fetch_album(album_id);
  if (!reply)
    {
      ret = -1;
      goto error;
    }

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add artists to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_album_tracks(struct httpd_request *hreq)
{
  time_t db_update;
  struct query_params query_params;
  const char *album_id;
  json_object *reply;
  json_object *items;
  int total;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  album_id = hreq->uri_parsed->path_parts[3];

  reply = json_object_new_object();
  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto error;

  query_params.type = Q_ITEMS;
  query_params.sort = S_ALBUM;
  query_params.filter = db_mprintf("(f.songalbumid = %q)", album_id);

  ret = fetch_tracks(&query_params, items, &total);
  free(query_params.filter);

  if (ret < 0)
    goto error;

  json_object_object_add(reply, "total", json_object_new_int(total));
  json_object_object_add(reply, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(reply, "limit", json_object_new_int(query_params.limit));

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add tracks to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_playlists(struct httpd_request *hreq)
{
  time_t db_update;
  struct query_params query_params;
  json_object *reply;
  json_object *items;
  int total;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  reply = json_object_new_object();
  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto error;

  query_params.type = Q_PL;
  query_params.sort = S_PLAYLIST;
  query_params.filter = db_mprintf("(f.type = %d OR f.type = %d)", PL_PLAIN, PL_SMART);

  ret = fetch_playlists(&query_params, items, &total);
  free(query_params.filter);

  if (ret < 0)
    goto error;

  json_object_object_add(reply, "total", json_object_new_int(total));
  json_object_object_add(reply, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(reply, "limit", json_object_new_int(query_params.limit));

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add playlists to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_playlist(struct httpd_request *hreq)
{
  time_t db_update;
  const char *playlist_id;
  json_object *reply;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  playlist_id = hreq->uri_parsed->path_parts[3];

  reply = fetch_playlist(playlist_id);
  if (!reply)
    {
      ret = -1;
      goto error;
    }

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "browse: Couldn't add playlist to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_playlist_tracks(struct httpd_request *hreq)
{
  time_t db_update;
  struct query_params query_params;
  json_object *reply;
  json_object *items;
  int playlist_id;
  int total;
  int ret = 0;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;


  ret = safe_atoi32(hreq->uri_parsed->path_parts[3], &playlist_id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "No valid playlist id given '%s'\n", hreq->uri_parsed->path);

      return HTTP_BADREQUEST;
    }

  reply = json_object_new_object();
  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto error;

  query_params.type = Q_PLITEMS;
  query_params.id = playlist_id;

  ret = fetch_tracks(&query_params, items, &total);
  if (ret < 0)
    goto error;

  json_object_object_add(reply, "total", json_object_new_int(total));
  json_object_object_add(reply, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(reply, "limit", json_object_new_int(query_params.limit));

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "playlist tracks: Couldn't add tracks to response buffer.\n");

 error:
  jparse_free(reply);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static int
jsonapi_reply_library_count(struct httpd_request *hreq)
{
  time_t db_update;
  const char *param_expression;
  char *expression;
  struct smartpl smartpl_expression;
  struct query_params qp;
  struct filecount_info fci;
  json_object *jreply;
  int ret;


  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_COUNT_ITEMS;

  param_expression = evhttp_find_header(hreq->query, "expression");
  if (param_expression)
    {
      memset(&smartpl_expression, 0, sizeof(struct smartpl));
      expression = safe_asprintf("\"query\" { %s }", param_expression);
      ret = smartpl_query_parse_string(&smartpl_expression, expression);
      free(expression);

      if (ret < 0)
	return HTTP_BADREQUEST;

      qp.filter = strdup(smartpl_expression.query_where);
      free_smartpl(&smartpl_expression, 1);
    }

  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  ret = db_filecount_get(&fci, &qp);
  if (ret == 0)
    {
      json_object_object_add(jreply, "tracks", json_object_new_int(fci.count));
      json_object_object_add(jreply, "artists", json_object_new_int(fci.artist_count));
      json_object_object_add(jreply, "albums", json_object_new_int(fci.album_count));
      json_object_object_add(jreply, "db_playtime", json_object_new_int64((fci.length / 1000)));
    }
  else
    {
      DPRINTF(E_LOG, L_WEB, "library: failed to get count info\n");
    }

  free(qp.filter);

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));
  jparse_free(jreply);

  return HTTP_OK;
}

static int
search_tracks(json_object *reply, struct httpd_request *hreq, const char *param_query, struct smartpl *smartpl_expression, enum media_kind media_kind)
{
  json_object *type;
  json_object *items;
  struct query_params query_params;
  int total;
  int ret;

  memset(&query_params, 0, sizeof(struct query_params));

  type = json_object_new_object();
  json_object_object_add(reply, "tracks", type);
  items = json_object_new_array();
  json_object_object_add(type, "items", items);

  query_params.type = Q_ITEMS;
  query_params.sort = S_NAME;

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto out;

  if (param_query)
    {
      if (media_kind)
	query_params.filter = db_mprintf("(f.title LIKE '%%%q%%' AND f.media_kind = %d)", param_query, media_kind);
      else
	query_params.filter = db_mprintf("(f.title LIKE '%%%q%%')", param_query);
    }
  else
    {
      query_params.filter = strdup(smartpl_expression->query_where);
      query_params.order = safe_strdup(smartpl_expression->order);

      if (smartpl_expression->limit > 0)
	{
	  query_params.idx_type = I_SUB;
	  query_params.limit = smartpl_expression->limit;
	  query_params.offset = 0;
	}
    }

  ret = fetch_tracks(&query_params, items, &total);
  if (ret < 0)
    goto out;

  json_object_object_add(type, "total", json_object_new_int(total));
  json_object_object_add(type, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(type, "limit", json_object_new_int(query_params.limit));

 out:
  free_query_params(&query_params, 1);

  return ret;
}

static int
search_artists(json_object *reply, struct httpd_request *hreq, const char *param_query, struct smartpl *smartpl_expression, enum media_kind media_kind)
{
  json_object *type;
  json_object *items;
  struct query_params query_params;
  int total;
  int ret;

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto out;

  type = json_object_new_object();
  json_object_object_add(reply, "artists", type);
  items = json_object_new_array();
  json_object_object_add(type, "items", items);

  query_params.type = Q_GROUP_ARTISTS;
  query_params.sort = S_ARTIST;

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto out;

  if (param_query)
    {
      if (media_kind)
	query_params.filter = db_mprintf("(f.album_artist LIKE '%%%q%%' AND f.media_kind = %d)", param_query, media_kind);
      else
	query_params.filter = db_mprintf("(f.album_artist LIKE '%%%q%%')", param_query);
    }
  else
    {
      query_params.filter = strdup(smartpl_expression->query_where);
      query_params.having = safe_strdup(smartpl_expression->having);
      query_params.order = safe_strdup(smartpl_expression->order);

      if (smartpl_expression->limit > 0)
	{
	  query_params.idx_type = I_SUB;
	  query_params.limit = smartpl_expression->limit;
	  query_params.offset = 0;
	}
    }

  ret = fetch_artists(&query_params, items, &total);
  if (ret < 0)
    goto out;

  json_object_object_add(type, "total", json_object_new_int(total));
  json_object_object_add(type, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(type, "limit", json_object_new_int(query_params.limit));

 out:
  free_query_params(&query_params, 1);

  return ret;
}

static int
search_albums(json_object *reply, struct httpd_request *hreq, const char *param_query, struct smartpl *smartpl_expression, enum media_kind media_kind)
{
  json_object *type;
  json_object *items;
  struct query_params query_params;
  int total;
  int ret;

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto out;

  type = json_object_new_object();
  json_object_object_add(reply, "albums", type);
  items = json_object_new_array();
  json_object_object_add(type, "items", items);

  query_params.type = Q_GROUP_ALBUMS;
  query_params.sort = S_ALBUM;

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto out;

  if (param_query)
    {
      if (media_kind)
	query_params.filter = db_mprintf("(f.album LIKE '%%%q%%' AND f.media_kind = %d)", param_query, media_kind);
      else
	query_params.filter = db_mprintf("(f.album LIKE '%%%q%%')", param_query);
    }
  else
    {
      query_params.filter = strdup(smartpl_expression->query_where);
      query_params.having = safe_strdup(smartpl_expression->having);
      query_params.order = safe_strdup(smartpl_expression->order);

      if (smartpl_expression->limit > 0)
	{
	  query_params.idx_type = I_SUB;
	  query_params.limit = smartpl_expression->limit;
	  query_params.offset = 0;
	}
    }

  ret = fetch_albums(&query_params, items, &total);
  if (ret < 0)
    goto out;

  json_object_object_add(type, "total", json_object_new_int(total));
  json_object_object_add(type, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(type, "limit", json_object_new_int(query_params.limit));

 out:
  free_query_params(&query_params, 1);

  return ret;
}

static int
search_playlists(json_object *reply, struct httpd_request *hreq, const char *param_query)
{
  json_object *type;
  json_object *items;
  struct query_params query_params;
  int total;
  int ret;

  if (!param_query)
    return 0;

  memset(&query_params, 0, sizeof(struct query_params));

  ret = query_params_limit_set(&query_params, hreq);
  if (ret < 0)
    goto out;

  type = json_object_new_object();
  json_object_object_add(reply, "playlists", type);
  items = json_object_new_array();
  json_object_object_add(type, "items", items);

  query_params.type = Q_PL;
  query_params.sort = S_PLAYLIST;
  query_params.filter = db_mprintf("((f.type = %d OR f.type = %d) AND f.title LIKE '%%%q%%')", PL_PLAIN, PL_SMART, param_query);

  ret = fetch_playlists(&query_params, items, &total);
  if (ret < 0)
    goto out;

  json_object_object_add(type, "total", json_object_new_int(total));
  json_object_object_add(type, "offset", json_object_new_int(query_params.offset));
  json_object_object_add(type, "limit", json_object_new_int(query_params.limit));

 out:
  free_query_params(&query_params, 1);

  return ret;
}

static int
jsonapi_reply_search(struct httpd_request *hreq)
{
  const char *param_type;
  const char *param_query;
  const char *param_expression;
  const char *param_media_kind;
  enum media_kind media_kind;
  char *expression;
  struct smartpl smartpl_expression;
  json_object *reply;
  int ret = 0;

  reply = NULL;

  param_type = evhttp_find_header(hreq->query, "type");
  param_query = evhttp_find_header(hreq->query, "query");
  param_expression = evhttp_find_header(hreq->query, "expression");

  if (!param_type || (!param_query && !param_expression))
    {
      DPRINTF(E_LOG, L_WEB, "Missing request parameter\n");

      return HTTP_BADREQUEST;
    }

  media_kind = 0;
  param_media_kind = evhttp_find_header(hreq->query, "media_kind");
  if (param_media_kind)
    {
      media_kind = db_media_kind_enum(param_media_kind);
      if (!media_kind)
      {
	DPRINTF(E_LOG, L_WEB, "Invalid media kind '%s'\n", param_media_kind);
	return HTTP_BADREQUEST;
      }
    }

  memset(&smartpl_expression, 0, sizeof(struct smartpl));

  if (param_expression)
    {
      expression = safe_asprintf("\"query\" { %s }", param_expression);
      ret = smartpl_query_parse_string(&smartpl_expression, expression);
      free(expression);

      if (ret < 0)
	return HTTP_BADREQUEST;
    }

  reply = json_object_new_object();

  if (strstr(param_type, "track"))
    {
      ret = search_tracks(reply, hreq, param_query, &smartpl_expression, media_kind);
      if (ret < 0)
	goto error;
    }

  if (strstr(param_type, "artist"))
    {
      ret = search_artists(reply, hreq, param_query, &smartpl_expression, media_kind);
      if (ret < 0)
	goto error;
    }

  if (strstr(param_type, "album"))
    {
      ret = search_albums(reply, hreq, param_query, &smartpl_expression, media_kind);
      if (ret < 0)
	goto error;
    }

  if (strstr(param_type, "playlist") && param_query)
    {
      ret = search_playlists(reply, hreq, param_query);
      if (ret < 0)
	goto error;
    }

  ret = evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply));
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "playlist tracks: Couldn't add tracks to response buffer.\n");

 error:
  jparse_free(reply);
  free_smartpl(&smartpl_expression, 1);

  if (ret < 0)
    return HTTP_INTERNAL;

  return HTTP_OK;
}

static struct httpd_uri_map adm_handlers[] =
  {
    { EVHTTP_REQ_GET,    "^/api/config$",                                jsonapi_reply_config },
    { EVHTTP_REQ_GET,    "^/api/library$",                               jsonapi_reply_library },
    { EVHTTP_REQ_GET,    "^/api/update$",                                jsonapi_reply_update },
    { EVHTTP_REQ_POST,   "^/api/spotify-login$",                         jsonapi_reply_spotify_login },
    { EVHTTP_REQ_GET,    "^/api/spotify$",                               jsonapi_reply_spotify },
    { EVHTTP_REQ_GET,    "^/api/pairing$",                               jsonapi_reply_pairing_get },
    { EVHTTP_REQ_POST,   "^/api/pairing$",                               jsonapi_reply_pairing_kickoff },
    { EVHTTP_REQ_POST,   "^/api/lastfm-login$",                          jsonapi_reply_lastfm_login },
    { EVHTTP_REQ_GET,    "^/api/lastfm-logout$",                         jsonapi_reply_lastfm_logout },
    { EVHTTP_REQ_GET,    "^/api/lastfm$",                                jsonapi_reply_lastfm },
    { EVHTTP_REQ_POST,   "^/api/verification$",                          jsonapi_reply_verification },

    { EVHTTP_REQ_GET,    "^/api/outputs$",                               jsonapi_reply_outputs },
    { EVHTTP_REQ_PUT,    "^/api/outputs/set$",                           jsonapi_reply_outputs_set },
    { EVHTTP_REQ_POST,   "^/api/select-outputs$",                        jsonapi_reply_outputs_set }, // deprecated: use "/api/outputs/set"
    { EVHTTP_REQ_GET,    "^/api/outputs/[[:digit:]]+$",                  jsonapi_reply_outputs_get_byid },
    { EVHTTP_REQ_PUT,    "^/api/outputs/[[:digit:]]+$",                  jsonapi_reply_outputs_put_byid },

    { EVHTTP_REQ_GET,    "^/api/player$",                                jsonapi_reply_player },
    { EVHTTP_REQ_PUT,    "^/api/player/play$",                           jsonapi_reply_player_play },
    { EVHTTP_REQ_PUT,    "^/api/player/pause$",                          jsonapi_reply_player_pause },
    { EVHTTP_REQ_PUT,    "^/api/player/stop$",                           jsonapi_reply_player_stop },
    { EVHTTP_REQ_PUT,    "^/api/player/next$",                           jsonapi_reply_player_next },
    { EVHTTP_REQ_PUT,    "^/api/player/previous$",                       jsonapi_reply_player_previous },
    { EVHTTP_REQ_PUT,    "^/api/player/shuffle$",                        jsonapi_reply_player_shuffle },
    { EVHTTP_REQ_PUT,    "^/api/player/repeat$",                         jsonapi_reply_player_repeat },
    { EVHTTP_REQ_PUT,    "^/api/player/consume$",                        jsonapi_reply_player_consume },
    { EVHTTP_REQ_PUT,    "^/api/player/volume$",                         jsonapi_reply_player_volume },
    { EVHTTP_REQ_PUT,    "^/api/player/seek$",                           jsonapi_reply_player_seek },

    { EVHTTP_REQ_GET,    "^/api/queue$",                                 jsonapi_reply_queue },
    { EVHTTP_REQ_PUT,    "^/api/queue/clear$",                           jsonapi_reply_queue_clear },
    { EVHTTP_REQ_POST,   "^/api/queue/items/add$",                       jsonapi_reply_queue_tracks_add },
    { EVHTTP_REQ_PUT,    "^/api/queue/items/[[:digit:]]+$",              jsonapi_reply_queue_tracks_move },
    { EVHTTP_REQ_DELETE, "^/api/queue/items/[[:digit:]]+$",              jsonapi_reply_queue_tracks_delete },

    { EVHTTP_REQ_GET,    "^/api/library/playlists$",                     jsonapi_reply_library_playlists },
    { EVHTTP_REQ_GET,    "^/api/library/playlists/[[:digit:]]+$",        jsonapi_reply_library_playlist },
    { EVHTTP_REQ_GET,    "^/api/library/playlists/[[:digit:]]+/tracks$", jsonapi_reply_library_playlist_tracks },
//    { EVHTTP_REQ_POST,   "^/api/library/playlists/[[:digit:]]+/tracks$", jsonapi_reply_library_playlists_tracks },
//    { EVHTTP_REQ_DELETE, "^/api/library/playlists/[[:digit:]]+$",        jsonapi_reply_library_playlist_tracks },
    { EVHTTP_REQ_GET,    "^/api/library/artists$",                       jsonapi_reply_library_artists },
    { EVHTTP_REQ_GET,    "^/api/library/artists/[[:digit:]]+$",          jsonapi_reply_library_artist },
    { EVHTTP_REQ_GET,    "^/api/library/artists/[[:digit:]]+/albums$",   jsonapi_reply_library_artist_albums },
    { EVHTTP_REQ_GET,    "^/api/library/albums$",                        jsonapi_reply_library_albums },
    { EVHTTP_REQ_GET,    "^/api/library/albums/[[:digit:]]+$",           jsonapi_reply_library_album },
    { EVHTTP_REQ_GET,    "^/api/library/albums/[[:digit:]]+/tracks$",    jsonapi_reply_library_album_tracks },
    { EVHTTP_REQ_GET,    "^/api/library/count$",                         jsonapi_reply_library_count },

    { EVHTTP_REQ_GET,    "^/api/search$",                                jsonapi_reply_search },

    { 0, NULL, NULL }
  };


/* ------------------------------- JSON API --------------------------------- */

void
jsonapi_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct httpd_request *hreq;
  struct evkeyvalq *headers;
  int status_code;

  DPRINTF(E_DBG, L_WEB, "JSON api request: '%s'\n", uri_parsed->uri);

  if (!httpd_admin_check_auth(req))
    return;

  hreq = httpd_request_parse(req, uri_parsed, NULL, adm_handlers);
  if (!hreq)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized path '%s' in JSON api request: '%s'\n", uri_parsed->path, uri_parsed->uri);

      httpd_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  CHECK_NULL(L_WEB, hreq->reply = evbuffer_new());

  status_code = hreq->handler(hreq);

  switch (status_code)
    {
      case HTTP_OK:                  /* 200 OK */
	headers = evhttp_request_get_output_headers(req);
	evhttp_add_header(headers, "Content-Type", "application/json");
	httpd_send_reply(req, status_code, "OK", hreq->reply, HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_NOCONTENT:           /* 204 No Content */
	httpd_send_reply(req, status_code, "No Content", hreq->reply, HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_NOTMODIFIED:         /* 304 Not Modified */
	httpd_send_reply(req, HTTP_NOTMODIFIED, NULL, NULL, HTTPD_SEND_NO_GZIP);
	break;

      case HTTP_BADREQUEST:          /* 400 Bad Request */
	httpd_send_error(req, status_code, "Bad Request");
	break;
      case HTTP_NOTFOUND:            /* 404 Not Found */
	httpd_send_error(req, status_code, "Not Found");
	break;
      case HTTP_INTERNAL:            /* 500 Internal Server Error */
      default:
	httpd_send_error(req, HTTP_INTERNAL, "Internal Server Error");
	break;
    }

  evbuffer_free(hreq->reply);
  free(hreq);
}

int
jsonapi_is_request(const char *path)
{
  if (strncmp(path, "/api/", strlen("/api/")) == 0)
    return 1;
  if (strcmp(path, "/api") == 0)
    return 1;

  return 0;
}

int
jsonapi_init(void)
{
  char buf[64];
  int i;
  int ret;

  for (i = 0; adm_handlers[i].handler; i++)
    {
      ret = regcomp(&adm_handlers[i].preg, adm_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
	{
	  regerror(ret, &adm_handlers[i].preg, buf, sizeof(buf));

	  DPRINTF(E_FATAL, L_WEB, "JSON api init failed; regexp error: %s\n", buf);
	  return -1;
	}
    }

  return 0;
}

void
jsonapi_deinit(void)
{
  int i;

  for (i = 0; adm_handlers[i].handler; i++)
    regfree(&adm_handlers[i].preg);
}
