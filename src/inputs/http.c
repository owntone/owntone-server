/*
 * Copyright (C) 2017-2020 Espen Jurgensen
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h> // strcasestr
#include <strings.h> // strcasecmp
#include <pthread.h> // mutex

#include <event2/buffer.h>

#include "transcode.h"
#include "http.h"
#include "misc.h"
#include "misc_json.h"
#include "settings.h"
#include "logger.h"
#include "artwork.h"
#include "worker.h"
#include "input.h"

struct prepared_metadata
{
  // Parsed metadata goes here
  struct input_metadata parsed;
  // Mutex to share the parsed metadata
  pthread_mutex_t lock;
} prepared_metadata;


/* ------- Handling/parsing of StreamUrl tags from some http streams ---------*/

struct streamurl_map
{
  const char *setting;
  enum json_type jtype;
  int (*parser)(struct input_metadata *, const char *, json_object *);
  char *words;
};

static int
streamurl_parse_artwork_url(struct input_metadata *metadata, const char *key, json_object *val)
{
  const char *url = json_object_get_string(val);

  if (metadata->artwork_url)
    return -1; // Already found artwork

  if (!artwork_extension_is_artwork(url))
    return -1;

  metadata->artwork_url = strdup(url);
  return 0;
}

static int
streamurl_parse_length(struct input_metadata *metadata, const char *key, json_object *val)
{
  int len = json_object_get_int(val);

  if (len <= 0 || len > 7200)
    return -1; // We expect seconds, so if it is longer than 2 hours we are probably wrong

  metadata->len_ms = len * 1000;
  metadata->pos_is_updated = true;
  metadata->pos_ms = 0;
  return 0;
}

// Lookup is case-insensitive and partial, first occurrence takes precedence
static struct streamurl_map streamurl_map[] =
  {
    { "streamurl_keywords_artwork_url", json_type_string, streamurl_parse_artwork_url },
    { "streamurl_keywords_length",      json_type_int,    streamurl_parse_length },
  };

static void
streamurl_field_parse(struct input_metadata *metadata, struct streamurl_map *map, const char *jkey, json_object *jval)
{
  char *word;
  char *ptr;

  if (!map->words)
    return;

  for (word = atrim(strtok_r(map->words, ",", &ptr)); word; free(word), word = atrim(strtok_r(NULL, ",", &ptr)))
    {
      if (json_object_get_type(jval) != map->jtype)
	continue;

      if (!strcasestr(jkey, word)) // True if e.g. word="duration" and jkey="eventDuration"
	continue;

      map->parser(metadata, jkey, jval);
    }
}

static int
streamurl_json_parse(struct input_metadata *metadata, const char *body)
{
  json_object *jresponse;
  int i;

  jresponse = json_tokener_parse(body);
  if (!jresponse)
    return -1;

  json_object_object_foreach(jresponse, jkey, jval)
    {
      for (i = 0; i < ARRAY_SIZE(streamurl_map); i++)
	{
	  streamurl_field_parse(metadata, &streamurl_map[i], jkey, jval);
	}
    }

  jparse_free(jresponse);

  return 0;
}

static void
streamurl_settings_unload(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(streamurl_map); i++)
    {
      free(streamurl_map[i].words);
      streamurl_map[i].words = NULL;
    }
}

static int
streamurl_settings_load(void)
{
  struct settings_category *category;
  bool enabled;
  int i;

  category = settings_category_get("misc");
  if (!category)
    return -1;

  for (i = 0, enabled = false; i < ARRAY_SIZE(streamurl_map); i++)
    {
      streamurl_map[i].words = settings_option_getstr(settings_option_get(category, streamurl_map[i].setting));
      if (streamurl_map[i].words)
	enabled = true;
    }

  return enabled ? 0 : -1;
}

static int
streamurl_process(struct input_metadata *metadata, const char *url)
{
  struct http_client_ctx client = { 0 };
  struct keyval kv = { 0 };
  struct evbuffer *evbuf;
  const char *content_type;
  char *body;
  int ret;

  // If the user didn't configure any keywords to look for then we can stop now
  ret = streamurl_settings_load();
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Ignoring StreamUrl resource '%s', no settings\n", url);
      return -1;
    }

  DPRINTF(E_DBG, L_PLAYER, "Downloading StreamUrl resource '%s'\n", url);

  CHECK_NULL(L_PLAYER, evbuf = evbuffer_new());

  client.url = url;
  client.input_headers = &kv;
  client.input_body = evbuf;

  ret = http_client_request(&client);
  if (ret < 0 || client.response_code != HTTP_OK)
    {
      DPRINTF(E_WARN, L_PLAYER, "Request for StreamUrl resource '%s' failed, response code %d\n", url, client.response_code);
      ret = -1;
      goto out;
    }

  // 0-terminate for safety
  evbuffer_add(evbuf, "", 1);
  body = (char *)evbuffer_pullup(evbuf, -1);

  content_type = keyval_get(&kv, "Content-Type");
  if (content_type && strcasecmp(content_type, "application/json") == 0)
    {
      ret = streamurl_json_parse(metadata, body);
    }
  else
    {
      DPRINTF(E_WARN, L_PLAYER, "No handler for StreamUrl resource '%s' with content type '%s'\n", url, content_type);
      ret = -1;
    }

 out:
  keyval_clear(&kv);
  evbuffer_free(evbuf);
  streamurl_settings_unload();
  return ret;
}

// Thread: worker
static void
streamurl_cb(void *arg)
{
  struct input_metadata metadata = { 0 };
  char *url = arg;
  int ret;

  ret = streamurl_process(&metadata, url);
  if (ret < 0) // Only negative on error/unconfigured (not if no metadata)
    return;

  pthread_mutex_lock(&prepared_metadata.lock);

  swap_pointers(&prepared_metadata.parsed.artwork_url, &metadata.artwork_url);
  prepared_metadata.parsed.pos_is_updated = metadata.pos_is_updated;
  prepared_metadata.parsed.pos_ms = metadata.pos_ms;
  prepared_metadata.parsed.len_ms = metadata.len_ms;

  pthread_mutex_unlock(&prepared_metadata.lock);

  input_metadata_free(&metadata, 1);

  input_write(NULL, NULL, INPUT_FLAG_METADATA);
}


/*-------------------------------- http metadata -----------------------------*/

// Checks if there is new metadata, which means getting the ICY data plus the
// StreamTitle and StreamUrl fields from libav. If StreamUrl is not an artwork
// link then we also kick off async downloading of it.
static int
metadata_prepare(struct input_source *source)
{
  struct http_icy_metadata *m;
  int changed;

  m = transcode_metadata(source->input_ctx, &changed);
  if (!m)
    return -1;

  if (!changed)
    {
      http_icy_metadata_free(m, 0);
      return -1;
    }

  swap_pointers(&prepared_metadata.parsed.artist, &m->artist);
  // Note we map title to album, because clients should show stream name as title
  swap_pointers(&prepared_metadata.parsed.album, &m->title);

  // In this case we have to go async to download the url and process the content
  if (m->url && !artwork_extension_is_artwork(m->url))
    worker_execute(streamurl_cb, m->url, strlen(m->url) + 1, 0);
  else
    swap_pointers(&prepared_metadata.parsed.artwork_url, &m->url);

  http_icy_metadata_free(m, 0);
  return 0;
}


/*---------------------------- Input implementation --------------------------*/

// Important! If you change any of the below then consider if the change also
// should be made in file.c

static int
setup(struct input_source *source)
{
  struct transcode_ctx *ctx;
  char *url;

  if (http_stream_setup(&url, source->path) < 0)
    return -1;

  free(source->path);
  source->path = url;

  ctx = transcode_setup(XCODE_PCM_NATIVE, NULL, source->data_kind, source->path, source->len_ms, NULL);
  if (!ctx)
    return -1;

  CHECK_NULL(L_PLAYER, source->evbuf = evbuffer_new());

  source->quality.sample_rate = transcode_encode_query(ctx->encode_ctx, "sample_rate");
  source->quality.bits_per_sample = transcode_encode_query(ctx->encode_ctx, "bits_per_sample");
  source->quality.channels = transcode_encode_query(ctx->encode_ctx, "channels");

  source->input_ctx = ctx;

  return 0;
}

static int
stop(struct input_source *source)
{
  struct transcode_ctx *ctx = source->input_ctx;

  transcode_cleanup(&ctx);

  if (source->evbuf)
    evbuffer_free(source->evbuf);

  source->input_ctx = NULL;
  source->evbuf = NULL;

  return 0;
}

static int
play(struct input_source *source)
{
  struct transcode_ctx *ctx = source->input_ctx;
  int icy_timer;
  int ret;
  short flags;

  // We set "wanted" to 1 because the read size doesn't matter to us
  // TODO optimize?
  ret = transcode(source->evbuf, &icy_timer, ctx, 1);
  if (ret == 0)
    {
      input_write(source->evbuf, &source->quality, INPUT_FLAG_EOF);
      stop(source);
      return -1;
    }
  else if (ret < 0)
    {
      input_write(NULL, NULL, INPUT_FLAG_ERROR);
      stop(source);
      return -1;
    }

  flags = (icy_timer && metadata_prepare(source) == 0) ? INPUT_FLAG_METADATA : 0;

  input_write(source->evbuf, &source->quality, flags);

  return 0;
}

static int
seek(struct input_source *source, int seek_ms)
{
  // Stream is live/unknown length so can't seek. We return 0 anyway, because
  // it is valid for the input to request a seek, since the input is not
  // supposed to concern itself about this.
  if (source->len_ms == 0)
    return 0;

  return transcode_seek(source->input_ctx, seek_ms);
}

static int
metadata_get(struct input_metadata *metadata, struct input_source *source)
{
  pthread_mutex_lock(&prepared_metadata.lock);

  *metadata = prepared_metadata.parsed;

  // Ownership transferred to caller, null all pointers in the struct
  memset(&prepared_metadata.parsed, 0, sizeof(struct input_metadata));

  pthread_mutex_unlock(&prepared_metadata.lock);

  return 0;
}

static int
init(void)
{
  CHECK_ERR(L_PLAYER, mutex_init(&prepared_metadata.lock));
  return 0;
}

static void
deinit(void)
{
  CHECK_ERR(L_PLAYER, pthread_mutex_destroy(&prepared_metadata.lock));
}

struct input_definition input_http =
{
  .name = "http",
  .type = INPUT_TYPE_HTTP,
  .disabled = 0,
  .setup = setup,
  .play = play,
  .stop = stop,
  .metadata_get = metadata_get,
  .seek = seek,
  .init = init,
  .deinit = deinit,
};
