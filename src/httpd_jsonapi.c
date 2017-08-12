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

#include "httpd_jsonapi.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <json.h>
#include <regex.h>
#include <string.h>

#include "conffile.h"
#include "db.h"
#include "httpd.h"
#include "library.h"
#include "logger.h"
#include "misc_json.h"
#include "remote_pairing.h"
#ifdef HAVE_SPOTIFY_H
# include "spotify_webapi.h"
# include "spotify.h"
#endif

struct uri_map
{
  regex_t preg;
  char *regexp;
  int (*handler)(struct evhttp_request *req, struct evbuffer *evbuf, char *uri, struct evkeyvalq *query);
};



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
jsonapi_reply_config(struct evhttp_request *req, struct evbuffer *evbuf, char *uri, struct evkeyvalq *query)
{
  json_object *reply;
  json_object *buildopts;
  int websocket_port;
  char **buildoptions;
  int i;
  int ret;

  reply = json_object_new_object();

  // Websocket port
#ifdef WEBSOCKET
  websocket_port = cfg_getint(cfg_getsec(cfg, "general"), "websocket_port");
#else
  websocket_port = 0;
#endif
  json_object_object_add(reply, "websocket_port", json_object_new_int(websocket_port));

  // forked-daapd version
  json_object_object_add(reply, "version", json_object_new_string(VERSION));

  // enabled build options
  buildopts = json_object_new_array();
  buildoptions = buildopts_get();
  for (i = 0; buildoptions[i]; i++)
    {
      json_object_array_add(buildopts, json_object_new_string(buildoptions[i]));
    }
  json_object_object_add(reply, "buildoptions", buildopts);

  ret = evbuffer_add_printf(evbuf, "%s", json_object_to_json_string(reply));
  jparse_free(reply);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "config: Couldn't add config data to response buffer.\n");
      return -1;
    }

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
jsonapi_reply_library(struct evhttp_request *req, struct evbuffer *evbuf, char *uri, struct evkeyvalq *query)
{
  struct query_params qp;
  struct filecount_info fci;
  int artists;
  int albums;
  bool is_scanning;
  json_object *reply;
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

  reply = json_object_new_object();

  json_object_object_add(reply, "artists", json_object_new_int(artists));
  json_object_object_add(reply, "albums", json_object_new_int(albums));
  json_object_object_add(reply, "songs", json_object_new_int(fci.count));
  json_object_object_add(reply, "db_playtime", json_object_new_int64((fci.length / 1000)));
  json_object_object_add(reply, "updating", json_object_new_boolean(is_scanning));

  ret = evbuffer_add_printf(evbuf, "%s", json_object_to_json_string(reply));
  jparse_free(reply);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "library: Couldn't add library information data to response buffer.\n");
      return -1;
    }

  return 0;
}

/*
 * Endpoint to trigger a library rescan
 */
static int
jsonapi_reply_update(struct evhttp_request *req, struct evbuffer *evbuf, char *uri, struct evkeyvalq *query)
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
jsonapi_reply_spotify(struct evhttp_request *req, struct evbuffer *evbuf, char *uri, struct evkeyvalq *query)
{
  int httpd_port;
  char __attribute__((unused)) redirect_uri[256];
  char *oauth_uri;
  json_object *reply;
  int ret;

  reply = json_object_new_object();

#ifdef HAVE_SPOTIFY_H
  struct spotify_status_info info;

  json_object_object_add(reply, "enabled", json_object_new_boolean(true));

  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");
  snprintf(redirect_uri, sizeof(redirect_uri), "http://forked-daapd.local:%d/oauth/spotify", httpd_port);

  oauth_uri = spotifywebapi_oauth_uri_get(redirect_uri);
  if (!uri)
    {
      DPRINTF(E_LOG, L_WEB, "Cannot display Spotify oauth interface (http_form_uriencode() failed)\n");
    }
  else
    {
      json_object_object_add(reply, "oauth_uri", json_object_new_string(oauth_uri));
      free(oauth_uri);
    }

  spotify_status_info_get(&info);
  json_object_object_add(reply, "libspotify_installed", json_object_new_boolean(info.libspotify_installed));
  json_object_object_add(reply, "libspotify_logged_in", json_object_new_boolean(info.libspotify_logged_in));
  json_object_object_add(reply, "libspotify_user", json_object_new_string(info.libspotify_user));
  json_object_object_add(reply, "webapi_token_valid", json_object_new_boolean(info.webapi_token_valid));
  json_object_object_add(reply, "webapi_user", json_object_new_string(info.webapi_user));

#else
  json_object_object_add(reply, "enabled", json_object_new_boolean(false));
#endif

  ret = evbuffer_add_printf(evbuf, "%s", json_object_to_json_string(reply));
  jparse_free(reply);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "spotify: Couldn't add spotify information data to response buffer.\n");
      return -1;
    }

  return 0;
}

static int
jsonapi_reply_spotify_login(struct evhttp_request *req, struct evbuffer *evbuf, char *uri, struct evkeyvalq *query)
{
  struct evbuffer *in_evbuf;
  json_object* request;
  const char *user;
  const char *password;
  char *errmsg = NULL;
  json_object* reply;
  json_object* errors;
  int ret;

  DPRINTF(E_DBG, L_WEB, "Received spotify login request\n");

#ifdef HAVE_SPOTIFY_H
  in_evbuf = evhttp_request_get_input_buffer(req);
  request = jparse_obj_from_evbuffer(in_evbuf);
  if (!request)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request\n");
      return -1;
    }

  reply = json_object_new_object();

  user = jparse_str_from_obj(request, "user");
  password = jparse_str_from_obj(request, "password");
  if (user && strlen(user) > 0 && password && strlen(password) > 0)
    {
      ret = spotify_login_user(user, password, &errmsg);
      if (ret < 0)
	{
	  json_object_object_add(reply, "success", json_object_new_boolean(false));
	  errors = json_object_new_object();
	  json_object_object_add(errors, "error", json_object_new_string(errmsg));
	  json_object_object_add(reply, "errors", errors);
	}
      else
	{
	  json_object_object_add(reply, "success", json_object_new_boolean(true));
	}
      free(errmsg);
    }
  else
    {
      DPRINTF(E_LOG, L_WEB, "No user or password in spotify login post request\n");

      json_object_object_add(reply, "success", json_object_new_boolean(false));
      errors = json_object_new_object();
      if (!user || strlen(user) == 0)
	json_object_object_add(errors, "user", json_object_new_string("Username is required"));
      if (!password || strlen(password) == 0)
	json_object_object_add(errors, "password", json_object_new_string("Password is required"));
      json_object_object_add(reply, "errors", errors);
    }

  ret = evbuffer_add_printf(evbuf, "%s", json_object_to_json_string(reply));
  jparse_free(reply);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "spotify: Couldn't add spotify login data to response buffer.\n");
      return -1;
    }

#else
  DPRINTF(E_LOG, L_WEB, "Received spotify login request but was not compiled with enable-spotify\n");
#endif

  return 0;
}

/*
 * Endpoint to kickoff pairing of a daap/dacp client
 *
 * Expects the paring pin to be present in the post request body, e. g.:
 *
 * {
 *   "pin": "1234"
 * }
 */
static int
pairing_kickoff(struct evhttp_request* req)
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
 * Endpoint to retrieve pairing information
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
  json_object *reply;
  int ret;

  remote_name = remote_pairing_get_name();

  reply = json_object_new_object();

  if (remote_name)
    {
      json_object_object_add(reply, "active", json_object_new_boolean(true));
      json_object_object_add(reply, "remote", json_object_new_string(remote_name));
    }
  else
    {
      json_object_object_add(reply, "active", json_object_new_boolean(false));
    }

  ret = evbuffer_add_printf(evbuf, "%s", json_object_to_json_string(reply));
  jparse_free(reply);
  free(remote_name);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "pairing: Couldn't add pairing information data to response buffer.\n");
      return -1;
    }

  return 0;
}

/*
 * Endpoint to pair daap/dacp client
 *
 * If request is a GET request, returns information about active pairing remote.
 * If request is a POST request, tries to pair the active remote with the given pin.
 */
static int
jsonapi_reply_pairing(struct evhttp_request *req, struct evbuffer *evbuf, char *uri, struct evkeyvalq *query)
{
  if (evhttp_request_get_command(req) == EVHTTP_REQ_POST)
    {
      return pairing_kickoff(req);
    }

  return pairing_get(evbuf);
}

static struct uri_map adm_handlers[] =
  {
    { .regexp = "^/api/config", .handler = jsonapi_reply_config },
    { .regexp = "^/api/library", .handler = jsonapi_reply_library },
    { .regexp = "^/api/update", .handler = jsonapi_reply_update },
    { .regexp = "^/api/spotify-login", .handler = jsonapi_reply_spotify_login },
    { .regexp = "^/api/spotify", .handler = jsonapi_reply_spotify },
    { .regexp = "^/api/pairing", .handler = jsonapi_reply_pairing },
    { .regexp = NULL, .handler = NULL }
  };

void
jsonapi_request(struct evhttp_request *req)
{
  char *full_uri;
  char *uri;
  char *ptr;
  struct evbuffer *evbuf;
  struct evkeyvalq query;
  struct evkeyvalq *headers;
  int handler;
  int ret;
  int i;

  /* Check authentication */
  if (!httpd_admin_check_auth(req))
    {
      DPRINTF(E_DBG, L_WEB, "JSON api request denied;\n");
      return;
    }

  memset(&query, 0, sizeof(struct evkeyvalq));

  full_uri = httpd_fixup_uri(req);
  if (!full_uri)
    {
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  ptr = strchr(full_uri, '?');
  if (ptr)
    *ptr = '\0';

  uri = strdup(full_uri);
  if (!uri)
    {
      free(full_uri);
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  if (ptr)
    *ptr = '?';

  ptr = uri;
  uri = evhttp_decode_uri(uri);
  free(ptr);

  DPRINTF(E_DBG, L_WEB, "Web admin request: %s\n", full_uri);

  handler = -1;
  for (i = 0; adm_handlers[i].handler; i++)
    {
      ret = regexec(&adm_handlers[i].preg, uri, 0, NULL, 0);
      if (ret == 0)
	{
	  handler = i;
	  break;
	}
    }

  if (handler < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized web admin request\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");

      free(uri);
      free(full_uri);
      return;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_WEB, "Could not allocate evbuffer for Web Admin reply\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      free(uri);
      free(full_uri);
      return;
    }

  evhttp_parse_query(full_uri, &query);

  headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "DAAP-Server", "forked-daapd/" VERSION);

  ret = adm_handlers[handler].handler(req, evbuf, uri, &query);
  if (ret < 0)
    {
      evhttp_send_error(req, 500, "Internal Server Error");
    }
  else
    {
      headers = evhttp_request_get_output_headers(req);
      evhttp_add_header(headers, "Content-Type", "application/json");
      httpd_send_reply(req, HTTP_OK, "OK", evbuf, 0);
    }

  evbuffer_free(evbuf);
  evhttp_clear_headers(&query);
  free(uri);
  free(full_uri);
}

int
jsonapi_is_request(struct evhttp_request *req, char *uri)
{
  if (strncmp(uri, "/api/", strlen("/api/")) == 0)
    return 1;
  if (strcmp(uri, "/api") == 0)
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

	  DPRINTF(E_FATAL, L_WEB, "Admin web interface init failed; regexp error: %s\n", buf);
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

