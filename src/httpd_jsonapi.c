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
pairing_kickoff(struct evhttp_request *req)
{
  struct evbuffer *evbuf;
  json_object* request;
  const char* message;

  evbuf = evhttp_request_get_input_buffer(req);
  request = jparse_obj_from_evbuffer(evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return -1;
    }

  DPRINTF(E_DBG, L_WEB, "Received pairing post request: %s\n", json_object_to_json_string(request));

  message = jparse_str_from_obj(request, "pin");
  if (message)
    remote_pairing_kickoff((char **)&message);
  else
    DPRINTF(E_LOG, L_WEB, "Missing pin in request body: %s\n", json_object_to_json_string(request));

  jparse_free(request);

  return 0;
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
pairing_get(struct evbuffer *evbuf)
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

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(evbuf, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);
  free(remote_name);

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

  return 0;
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
      return -1;
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

  return 0;
}

/*
 * Endpoint to trigger a library rescan
 */
static int
jsonapi_reply_update(struct httpd_request *hreq)
{
  library_rescan();
  return 0;
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
      return -1;
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

  return 0;
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
      return -1;
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

  return 0;
}

/*
 * Endpoint to pair daap/dacp client
 *
 * If request is a GET request, returns information about active pairing remote.
 * If request is a POST request, tries to pair the active remote with the given pin.
 */
static int
jsonapi_reply_pairing(struct httpd_request *hreq)
{
  if (evhttp_request_get_command(hreq->req) == EVHTTP_REQ_POST)
    {
      return pairing_kickoff(hreq->req);
    }

  return pairing_get(hreq->reply);
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

  return 0;
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
      return -1;
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

  return 0;
}

static int
jsonapi_reply_lastfm_logout(struct httpd_request *hreq)
{
#ifdef LASTFM
  lastfm_logout();
#endif
  return 0;
}

static void
speaker_enum_cb(uint64_t id, const char *name, const char *output_type, int relvol, int absvol, struct spk_flags flags, void *arg)
{
  json_object *outputs;
  json_object *output;
  char output_id[21];

  outputs = arg;
  output = json_object_new_object();

  snprintf(output_id, sizeof(output_id), "%" PRIu64, id);
  json_object_object_add(output, "id", json_object_new_string(output_id));
  json_object_object_add(output, "name", json_object_new_string(name));
  json_object_object_add(output, "type", json_object_new_string(output_type));
  json_object_object_add(output, "selected", json_object_new_boolean(flags.selected));
  json_object_object_add(output, "has_password", json_object_new_boolean(flags.has_password));
  json_object_object_add(output, "requires_auth", json_object_new_boolean(flags.requires_auth));
  json_object_object_add(output, "needs_auth_key", json_object_new_boolean(flags.needs_auth_key));
  json_object_object_add(output, "volume", json_object_new_int(absvol));

  json_object_array_add(outputs, output);
}

static int
jsonapi_reply_outputs(struct httpd_request *hreq)
{
  json_object *jreply;
  json_object *outputs;

  outputs = json_object_new_array();

  player_speaker_enumerate(speaker_enum_cb, outputs);

  jreply = json_object_new_object();
  json_object_object_add(jreply, "outputs", outputs);

  CHECK_ERRNO(L_WEB, evbuffer_add_printf(hreq->reply, "%s", json_object_to_json_string(jreply)));

  jparse_free(jreply);

  return 0;
}

static int
jsonapi_reply_verification(struct httpd_request *hreq)
{
  struct evbuffer *in_evbuf;
  json_object* request;
  const char* message;

  if (evhttp_request_get_command(hreq->req) != EVHTTP_REQ_POST)
    {
      DPRINTF(E_LOG, L_WEB, "Verification: request is not a POST request\n");
      return -1;
    }

  in_evbuf = evhttp_request_get_input_buffer(hreq->req);
  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return -1;
    }

  DPRINTF(E_DBG, L_WEB, "Received verification post request: %s\n", json_object_to_json_string(request));

  message = jparse_str_from_obj(request, "pin");
  if (message)
    player_raop_verification_kickoff((char **)&message);
  else
    DPRINTF(E_LOG, L_WEB, "Missing pin in request body: %s\n", json_object_to_json_string(request));

  jparse_free(request);

  return 0;
}

static int
jsonapi_reply_select_outputs(struct httpd_request *hreq)
{
  struct evbuffer *in_evbuf;
  json_object *request;
  json_object *outputs;
  json_object *output_id;
  int nspk, i, ret;
  uint64_t *ids;

  if (evhttp_request_get_command(hreq->req) != EVHTTP_REQ_POST)
    {
      DPRINTF(E_LOG, L_WEB, "Select outputs: request is not a POST request\n");
      return -1;
    }

  in_evbuf = evhttp_request_get_input_buffer(hreq->req);
  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return -1;
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

  return 0;
}

static struct httpd_uri_map adm_handlers[] =
  {
    { .regexp = "^/api/config", .handler = jsonapi_reply_config },
    { .regexp = "^/api/library", .handler = jsonapi_reply_library },
    { .regexp = "^/api/update", .handler = jsonapi_reply_update },
    { .regexp = "^/api/spotify-login", .handler = jsonapi_reply_spotify_login },
    { .regexp = "^/api/spotify", .handler = jsonapi_reply_spotify },
    { .regexp = "^/api/pairing", .handler = jsonapi_reply_pairing },
    { .regexp = "^/api/lastfm-login", .handler = jsonapi_reply_lastfm_login },
    { .regexp = "^/api/lastfm-logout", .handler = jsonapi_reply_lastfm_logout },
    { .regexp = "^/api/lastfm", .handler = jsonapi_reply_lastfm },
    { .regexp = "^/api/outputs", .handler = jsonapi_reply_outputs },
    { .regexp = "^/api/select-outputs", .handler = jsonapi_reply_select_outputs },
    { .regexp = "^/api/verification", .handler = jsonapi_reply_verification },
    { .regexp = NULL, .handler = NULL }
  };


/* ------------------------------- JSON API --------------------------------- */

void
jsonapi_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct httpd_request *hreq;
  struct evkeyvalq *headers;
  int ret;

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

  ret = hreq->handler(hreq);
  if (ret < 0)
    {
      httpd_send_error(req, 500, "Internal Server Error");
      goto error;
    }

  evhttp_add_header(headers, "Content-Type", "application/json");

  httpd_send_reply(req, HTTP_OK, "OK", hreq->reply, 0);

 error:
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
