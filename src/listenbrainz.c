/*
 * Copyright (C) 2025 Christian Meffert <christian.meffert@googlemail.com>
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
#include <config.h>
#endif

#include <event2/event.h>
#include <stdbool.h>
#include <stddef.h>

#include "conffile.h"
#include "db.h"
#include "http.h"
#include "listenbrainz.h"
#include "logger.h"
#include "misc_json.h"

static const char *listenbrainz_submit_listens_url = "https://api.listenbrainz.org/1/submit-listens";
static const char *listenbrainz_validate_token_url = "https://api.listenbrainz.org/1/validate-token";
static bool listenbrainz_disabled = true;
static char *listenbrainz_token = NULL;
static time_t listenbrainz_rate_limited_until = 0;

static int
submit_listens(struct media_file_info *mfi)
{
  struct http_client_ctx ctx = { 0 };
  struct keyval kv_out = { 0 };
  struct keyval kv_in = { 0 };
  char auth_token[1024];
  json_object *request_body;
  json_object *listens;
  json_object *listen;
  json_object *track_metadata;
  json_object *additional_info;
  const char *x_rate_limit_reset_in;
  int32_t rate_limit_seconds = -1;
  int ret;

  ctx.url = listenbrainz_submit_listens_url;

  // Set request headers
  ctx.output_headers = &kv_out;
  snprintf(auth_token, sizeof(auth_token), "Token %s", listenbrainz_token);
  keyval_add(ctx.output_headers, "Authorization", auth_token);
  keyval_add(ctx.output_headers, "Content-Type", "application/json");

  // Set request body
  request_body = json_object_new_object();
  json_object_object_add(request_body, "listen_type", json_object_new_string("single"));
  listens = json_object_new_array();
  json_object_object_add(request_body, "payload", listens);
  listen = json_object_new_object();
  json_object_array_add(listens, listen);
  json_object_object_add(listen, "listened_at", json_object_new_int64((int64_t)time(NULL)));
  track_metadata = json_object_new_object();
  json_object_object_add(listen, "track_metadata", track_metadata);
  json_object_object_add(track_metadata, "artist_name", json_object_new_string(mfi->artist));
  json_object_object_add(track_metadata, "release_name", json_object_new_string(mfi->album));
  json_object_object_add(track_metadata, "track_name", json_object_new_string(mfi->title));
  additional_info = json_object_new_object();
  json_object_object_add(track_metadata, "additional_info", additional_info);
  json_object_object_add(additional_info, "media_player", json_object_new_string(PACKAGE_NAME));
  json_object_object_add(additional_info, "media_player_version", json_object_new_string(PACKAGE_VERSION));
  json_object_object_add(additional_info, "submission_client", json_object_new_string(PACKAGE_NAME));
  json_object_object_add(additional_info, "submission_client_version", json_object_new_string(PACKAGE_VERSION));
  json_object_object_add(additional_info, "duration_ms", json_object_new_int((int32_t)mfi->song_length));
  ctx.output_body = json_object_to_json_string(request_body);

  // Create input evbuffer for the response body and keyval for response headers
  ctx.input_headers = &kv_in;

  // Send POST request for submit-listens endpoint
  ret = http_client_request(&ctx, NULL);

  // Process response
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCROBBLE, "lbrainz: Failed to scrobble '%s' by '%s'\n", mfi->title, mfi->artist);
      goto out;
    }

  if (ctx.response_code == HTTP_OK)
    {
      DPRINTF(E_INFO, L_SCROBBLE, "lbrainz: Scrobbled '%s' by '%s'\n", mfi->title, mfi->artist);
      listenbrainz_rate_limited_until = 0;
    }
  else if (ctx.response_code == 401)
    {
      DPRINTF(E_LOG, L_SCROBBLE, "lbrainz: Failed to scrobble '%s' by '%s', unauthorized, disable scrobbling\n", mfi->title,
          mfi->artist);
      listenbrainz_disabled = true;
    }
  else if (ctx.response_code == 429)
    {
      x_rate_limit_reset_in = keyval_get(ctx.input_headers, "X-RateLimit-Reset-In");
      ret = safe_atoi32(x_rate_limit_reset_in, &rate_limit_seconds);
      if (ret == 0 && rate_limit_seconds > 0)
	{
	  listenbrainz_rate_limited_until = time(NULL) + rate_limit_seconds;
	}
      DPRINTF(E_INFO, L_SCROBBLE, "lbrainz: Failed to scrobble '%s' by '%s', rate limited for %d seconds\n", mfi->title,
          mfi->artist, rate_limit_seconds);
    }
  else
    {
      DPRINTF(E_LOG, L_SCROBBLE, "lbrainz: Failed to scrobble '%s' by '%s', response code: %d\n", mfi->title, mfi->artist,
          ctx.response_code);
    }

out:

  // Clean up
  jparse_free(request_body);
  keyval_clear(ctx.output_headers);
  keyval_clear(ctx.input_headers);

  return ret;
}

static int
validate_token(struct listenbrainz_status *status)
{
  struct http_client_ctx ctx = { 0 };
  struct keyval kv_out = { 0 };
  char auth_token[1024];
  char *response_body;
  json_object *json_response = NULL;
  int ret = 0;

  if (!listenbrainz_token)
    return -1;

  ctx.url = listenbrainz_validate_token_url;

  // Set request headers
  ctx.output_headers = &kv_out;
  snprintf(auth_token, sizeof(auth_token), "Token %s", listenbrainz_token);
  keyval_add(ctx.output_headers, "Authorization", auth_token);

  // Create input evbuffer for the response body
  ctx.input_body = evbuffer_new();

  // Send GET request for validate-token endpoint
  ret = http_client_request(&ctx, NULL);

  // Parse response
  // 0-terminate for safety
  evbuffer_add(ctx.input_body, "", 1);

  response_body = (char *)evbuffer_pullup(ctx.input_body, -1);
  if (!response_body || (strlen(response_body) == 0))
    {
      DPRINTF(E_LOG, L_SCROBBLE, "lbrainz: Request for '%s' failed, response was empty\n", ctx.url);
      goto out;
    }

  json_response = json_tokener_parse(response_body);
  if (!json_response)
    DPRINTF(E_LOG, L_SCROBBLE, "lbrainz: JSON parser returned an error for '%s'\n", ctx.url);

  status->user_name = safe_strdup(jparse_str_from_obj(json_response, "user_name"));
  status->token_valid = jparse_bool_from_obj(json_response, "valid");
  status->message = safe_strdup(jparse_str_from_obj(json_response, "message"));
  listenbrainz_disabled = !status->token_valid;

out:

  // Clean up
  if (ctx.input_body)
    evbuffer_free(ctx.input_body);
  keyval_clear(ctx.output_headers);

  return ret;
}

/* Thread: worker */
int
listenbrainz_scrobble(int mfi_id)
{
  struct media_file_info *mfi;
  int ret;

  if (listenbrainz_disabled)
    return -1;

  if (listenbrainz_rate_limited_until > 0 && time(NULL) < listenbrainz_rate_limited_until)
    {
      DPRINTF(E_INFO, L_SCROBBLE, "lbrainz: Rate limited, not scrobbling\n");
      return -2;
    }

  mfi = db_file_fetch_byid(mfi_id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_SCROBBLE, "lbrainz: Scrobble failed, track id %d is unknown\n", mfi_id);
      return -1;
    }

  // Don't scrobble songs which are shorter than 30 sec
  if (mfi->song_length < 30000)
    goto noscrobble;

  // Don't scrobble non-music and radio stations
  if ((mfi->media_kind != MEDIA_KIND_MUSIC) || (mfi->data_kind == DATA_KIND_HTTP))
    goto noscrobble;

  // Don't scrobble songs with unknown artist
  if (strcmp(mfi->artist, CFG_NAME_UNKNOWN_ARTIST) == 0)
    goto noscrobble;

  ret = submit_listens(mfi);
  return ret;

noscrobble:
  free_mfi(mfi, 0);

  return -1;
}

int
listenbrainz_token_set(const char *token)
{
  int ret;

  if (!token)
    {
      DPRINTF(E_DBG, L_SCROBBLE, "lbrainz: Failed to update ListenBrainz token, no token provided\n");
      return -1;
    }

  ret = db_admin_set(DB_ADMIN_LISTENBRAINZ_TOKEN, token);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_SCROBBLE, "lbrainz: Failed to update ListenBrainz token, DB update failed\n");
    }
  else
    {
      free(listenbrainz_token);
      listenbrainz_token = NULL;
      ret = db_admin_get(&listenbrainz_token, DB_ADMIN_LISTENBRAINZ_TOKEN);
      if (ret == 0)
	listenbrainz_disabled = false;
    }

  return ret;
}

int
listenbrainz_token_delete(void)
{
  int ret;

  ret = db_admin_delete(DB_ADMIN_LISTENBRAINZ_TOKEN);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_SCROBBLE, "lbrainz: Failed to delete ListenBrainz token, DB delete query failed\n");
    }
  else
    {
      free(listenbrainz_token);
      listenbrainz_token = NULL;
      listenbrainz_disabled = true;
    }

  return ret;
}

int
listenbrainz_status_get(struct listenbrainz_status *status)
{
  int ret = 0;

  memset(status, 0, sizeof(struct listenbrainz_status));

  if (listenbrainz_disabled)
    {
      status->disabled = true;
    }
  else
    {
      ret = validate_token(status);
    }
  return ret;
}

void
listenbrainz_status_free(struct listenbrainz_status *status, bool content_only)
{
  free(status->user_name);
  free(status->message);
  if (!content_only)
    free(status);
}

/* Thread: main */
int
listenbrainz_init(void)
{
  int ret;

  ret = db_admin_get(&listenbrainz_token, DB_ADMIN_LISTENBRAINZ_TOKEN);
  listenbrainz_disabled = (ret < 0);

  if (listenbrainz_disabled)
    {
      DPRINTF(E_DBG, L_SCROBBLE, "lbrainz: No valid ListenBrainz token\n");
    }

  return 0;
}
