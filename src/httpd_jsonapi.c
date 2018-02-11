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
#include <json.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>

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
#ifdef HAVE_SPOTIFY_H
# include "spotify_webapi.h"
# include "spotify.h"
#endif


/* -------------------------------- HELPERS --------------------------------- */

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
  int artists;
  int albums;
  bool is_scanning;
  json_object *jreply;
  int ret;

  // Fetch values for response

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_COUNT_ITEMS;
  ret = db_filecount_get(&fci, &qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "library: failed to get file count info\n");
      return HTTP_INTERNAL;
    }

  artists = db_files_get_artist_count();
  albums = db_files_get_album_count();

  is_scanning = library_is_scanning();

  // Build json response
  CHECK_NULL(L_WEB, jreply = json_object_new_object());

  json_object_object_add(jreply, "artists", json_object_new_int(artists));
  json_object_object_add(jreply, "albums", json_object_new_int(albums));
  json_object_object_add(jreply, "songs", json_object_new_int(fci.count));
  json_object_object_add(jreply, "db_playtime", json_object_new_int64((fci.length / 1000)));
  json_object_object_add(jreply, "updating", json_object_new_boolean(is_scanning));

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
  json_object_object_add(jreply, "libspotify_user", json_object_new_string(info.libspotify_user));
  json_object_object_add(jreply, "webapi_token_valid", json_object_new_boolean(info.webapi_token_valid));
  json_object_object_add(jreply, "webapi_user", json_object_new_string(info.webapi_user));

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
jsonapi_reply_player_play(struct httpd_request *hreq)
{
  int ret;

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
jsonapi_reply_player(struct httpd_request *hreq)
{
  struct player_status status;
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

  json_object_object_add(reply, "item_id", json_object_new_int(status.item_id));
  json_object_object_add(reply, "item_length_ms", json_object_new_int(status.len_ms));
  json_object_object_add(reply, "item_progress_ms", json_object_new_int(status.pos_ms));

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(reply)));

  jparse_free(reply);

  return HTTP_OK;
}

static json_object *
queue_item_to_json(struct db_queue_item *queue_item)
{
  json_object *item;

  item = json_object_new_object();

  json_object_object_add(item, "id", json_object_new_int(queue_item->id));
  json_object_object_add(item, "position", json_object_new_int(queue_item->pos));
  json_object_object_add(item, "shuffle_position", json_object_new_int(queue_item->shuffle_pos));

  json_object_object_add(item, "file_id", json_object_new_int(queue_item->file_id));
  json_object_object_add(item, "path", json_object_new_string(queue_item->path));
  json_object_object_add(item, "virtual_path", json_object_new_string(queue_item->virtual_path));

  json_object_object_add(item, "title", json_object_new_string(queue_item->title));
  json_object_object_add(item, "artist", json_object_new_string(queue_item->artist));
  json_object_object_add(item, "albumartist", json_object_new_string(queue_item->album_artist));
  json_object_object_add(item, "album", json_object_new_string(queue_item->album));
  json_object_object_add(item, "genre", json_object_new_string(queue_item->genre));
  json_object_object_add(item, "artist_sort", json_object_new_string(queue_item->artist_sort));
  json_object_object_add(item, "albumartist_sort", json_object_new_string(queue_item->album_artist_sort));
  json_object_object_add(item, "album_sort", json_object_new_string(queue_item->album_sort));
  json_object_object_add(item, "year", json_object_new_int(queue_item->year));
  json_object_object_add(item, "length_ms", json_object_new_int(queue_item->song_length));

  return item;
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
  struct db_queue_item queue_item;
  json_object *reply;
  json_object *items;
  json_object *item;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));
  reply = json_object_new_object();

  version = db_admin_getint(DB_ADMIN_QUEUE_VERSION);
  count = db_queue_get_count();

  json_object_object_add(reply, "version", json_object_new_int(version));
  json_object_object_add(reply, "count", json_object_new_int(count));

  items = json_object_new_array();
  json_object_object_add(reply, "items", items);

  param = evhttp_find_header(hreq->query, "sort");
  if (param && strcmp(param, "shuffle") == 0)
    {
      query_params.sort = S_SHUFFLE_POS;
    }

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
      item = queue_item_to_json(&queue_item);
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

static struct httpd_uri_map adm_handlers[] =
  {
    { EVHTTP_REQ_GET,    "^/api/config$",               jsonapi_reply_config },
    { EVHTTP_REQ_GET,    "^/api/library$",              jsonapi_reply_library },
    { EVHTTP_REQ_GET,    "^/api/update$",               jsonapi_reply_update },
    { EVHTTP_REQ_POST,   "^/api/spotify-login$",        jsonapi_reply_spotify_login },
    { EVHTTP_REQ_GET,    "^/api/spotify$",              jsonapi_reply_spotify },
    { EVHTTP_REQ_GET,    "^/api/pairing$",              jsonapi_reply_pairing_get },
    { EVHTTP_REQ_POST,   "^/api/pairing$",              jsonapi_reply_pairing_kickoff },
    { EVHTTP_REQ_POST,   "^/api/lastfm-login$",         jsonapi_reply_lastfm_login },
    { EVHTTP_REQ_GET,    "^/api/lastfm-logout$",        jsonapi_reply_lastfm_logout },
    { EVHTTP_REQ_GET,    "^/api/lastfm$",               jsonapi_reply_lastfm },
    { EVHTTP_REQ_POST,   "^/api/verification$",         jsonapi_reply_verification },

    { EVHTTP_REQ_GET,    "^/api/outputs$",              jsonapi_reply_outputs },
    { EVHTTP_REQ_PUT,    "^/api/outputs/set$",          jsonapi_reply_outputs_set },
    { EVHTTP_REQ_POST,   "^/api/select-outputs$",       jsonapi_reply_outputs_set }, // deprecated: use "/api/outputs/set"
    { EVHTTP_REQ_GET,    "^/api/outputs/[[:digit:]]+$", jsonapi_reply_outputs_get_byid },
    { EVHTTP_REQ_PUT,    "^/api/outputs/[[:digit:]]+$", jsonapi_reply_outputs_put_byid },

    { EVHTTP_REQ_PUT,    "^/api/player/play$",          jsonapi_reply_player_play },
    { EVHTTP_REQ_PUT,    "^/api/player/pause$",         jsonapi_reply_player_pause },
    { EVHTTP_REQ_PUT,    "^/api/player/stop$",          jsonapi_reply_player_stop },
    { EVHTTP_REQ_PUT,    "^/api/player/next$",          jsonapi_reply_player_next },
    { EVHTTP_REQ_PUT,    "^/api/player/previous$",      jsonapi_reply_player_previous },
    { EVHTTP_REQ_PUT,    "^/api/player/shuffle$",       jsonapi_reply_player_shuffle },
    { EVHTTP_REQ_PUT,    "^/api/player/repeat$",        jsonapi_reply_player_repeat },
    { EVHTTP_REQ_PUT,    "^/api/player/consume$",       jsonapi_reply_player_consume },
    { EVHTTP_REQ_PUT,    "^/api/player/volume$",        jsonapi_reply_player_volume },
    { EVHTTP_REQ_GET,    "^/api/player$",               jsonapi_reply_player },

    { EVHTTP_REQ_GET,    "^/api/queue$",                jsonapi_reply_queue },

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

  headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "DAAP-Server", "forked-daapd/" VERSION);

  CHECK_NULL(L_WEB, hreq->reply = evbuffer_new());

  status_code = hreq->handler(hreq);

  switch (status_code)
    {
      case HTTP_OK:                  /* 200 OK */
	evhttp_add_header(headers, "Content-Type", "application/json");
	httpd_send_reply(req, status_code, "OK", hreq->reply, 0);
	break;
      case HTTP_NOCONTENT:           /* 204 No Content */
	httpd_send_reply(req, status_code, "No Content", hreq->reply, 0);
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
