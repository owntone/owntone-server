/*
 * Copyright (C) 2020 whatdoineed2d/Ray
 * based heavily on filescanner_playlist.c
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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <time.h>

#include <event2/event.h>
#include <mrss.h>

#include "logger.h"
#include "db.h"
#include "http.h"
#include "library/filescanner.h"
#include "misc.h"
#include "misc_json.h"
#include "library.h"

// from filescanner_playlist.c
extern int playlist_prepare(const char *path, time_t mtime, enum pl_type type);

// RSS spec: https://validator.w3.org/feed/docs/rss2.html

enum rss_type {
  RSS_UNKNOWN = 0,
  RSS_FILE,
  RSS_HTTP
};

static enum rss_type
rss_type(const char *path)
{
  char *ptr;

  ptr = strrchr(path, '.');
  if (!ptr)
    return RSS_UNKNOWN;

  if (strcasecmp(ptr, ".rss") == 0)
    return RSS_FILE;
  else if (strcasecmp(ptr, ".rss_url") == 0)
    return RSS_HTTP;
  else
    return RSS_UNKNOWN;
}


static bool
rss_date(struct tm *tm, const char *date)
{
  // RFC822 https://tools.ietf.org/html/rfc822#section-5
  // ie Fri, 07 Feb 2020 18:58:00 +0000
  //    ^^^^                      ^^^^^
  //    optional                  ^^^^^
  //                              could also be GMT/UT/EST/A..I/M..Z

  char *ptr;
  time_t t;

  memset(tm, 0, sizeof(struct tm));
  ptr = strptime(date, "%a,%n", tm);
  ptr = strptime(ptr ? ptr : date, "%d%n%b%n%Y%n%H:%M:%S%n", tm);
  if (!ptr)
    {
      // date is junk, using current time
      time(&t);
      gmtime_r(&t, tm);
      return false;
    }

  // TODO - adjust the TZ?
  return true;
}

// uses incoming buf for result but if too smal, returns new buf
static char*
process_apple_rss(char* buf, unsigned bufsz, const char *file)
{
  struct http_client_ctx ctx;
  struct evbuffer *evbuf;
  char url[100];
  unsigned podid;  // apple podcast id
  json_object *json = NULL;
  json_object *jsonra = NULL;
  const char *feedURL;
  char *buf1;
  const char *ptr;
  unsigned len;
  int ret;

  // ask for the json to get feedUrl
  // https://itunes.apple.com/lookup?id=974722423

  ptr = strrchr(buf, '/');
  if (!ptr)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not parse Apple Podcast RSS ID from '%s'\n", file);
      return NULL;
    }
  if (sscanf(ptr, "/id%u", &podid) != 1)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not parse Apple Podcast RSS ID from '%s'\n", file);
      return NULL;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    return false;

  snprintf(url, sizeof(url), "https://itunes.apple.com/lookup?id=%u", podid);

  memset(&ctx, 0, sizeof(struct http_client_ctx));
  ctx.url = url;
  ctx.input_body = evbuf;

  ret = http_client_request(&ctx);
  if (ret < 0 || ret && ctx.response_code != HTTP_OK)
    {
      evbuffer_free(evbuf);
      return NULL;
    }

  buf1 = buf;
  json = jparse_obj_from_evbuffer(evbuf);
  if (!json)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not parse RSS apple response, podcast id %u\n", podid);
    }
  else
    {
      /* expect json resp - get feedUrl
       * {
       *  "resultCount": 1,
       *  "results": [
       *    {
       *      "wrapperType": "track",
       *      "kind": "podcast",
       *      ...
       *      "collectionViewUrl": "https://podcasts.apple.com/us/podcast/cgp-grey/id974722423?uo=4",
       *      "feedUrl": "http://cgpgrey.libsyn.com/rss",
       *      ...
       *      "genres": [
       *        "Education",
       *        "Podcasts",
       *        "News"
       *      ]
       *    }
       *  ]
       *}
       */
      if (json_object_object_get_ex(json, "results", &jsonra) && (feedURL = jparse_str_from_array(jsonra, 0, "feedUrl")) )
        {
          if ((len = strlen(feedURL)+1) > bufsz)
            buf1 = malloc(len);
          strcpy(buf1, feedURL);
        }
      else
        DPRINTF(E_DBG, L_SCAN, "Could not parse feedURL from RSS apple, podcast id %u\n", podid);
    }

  jparse_free(json);
  evbuffer_free(evbuf);
  return buf1;
}

void
scan_rss(const char *file, time_t mtime, int dir_id)
{
  FILE *fp;
  struct media_file_info mfi;
  struct stat sb;
  char buf[2048];
  enum rss_type rss_format;
  int pl_id;
  unsigned nadded;
  struct tm tm;
  char *url = NULL;

  mrss_t *data = NULL;
  mrss_error_t ret;
  mrss_item_t *item = NULL;
  CURLcode code;

  rss_format = rss_type(file);
  if (rss_format == RSS_UNKNOWN)
    return;

  ret = stat(file, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not stat() '%s': %s\n", file, strerror(errno));
      return;
    }
  if (sb.st_size == 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Ingoring empty RSS file '%s'\n", file);
      return;
    }

  fp = fopen(file, "r");
  if (!fp)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not open RSS '%s': %s\n", file, strerror(errno));
      return;
    }

  // Will create or update the playlist entry in the database
  pl_id = playlist_prepare(file, mtime, PL_RSS);
  if (pl_id < 0)
    return; // Not necessarily an error, could also be that the playlist hasn't changed

  memset(&mfi, 0, sizeof(struct media_file_info));
  nadded = 0;

  switch (rss_format)
    {
      case RSS_HTTP:
        {
          url = buf;
          if (fgets(buf, sizeof(buf), fp) == NULL)
            {
              DPRINTF(E_LOG, L_SCAN, "Could not read RSS url from '%s': %s\n", file, strerror(errno));
              goto cleanup;
            }

          if (strncmp (buf, "http://", 7) != 0 && strncmp(buf, "https://", 8) != 0)
            {
              DPRINTF(E_LOG, L_SCAN, "Could not read valid RSS url from '%s'\n", file);
              goto cleanup;
            }

          // Is it an apple podcast stream? ie https://podcasts.apple.com/is/podcast/cgp-grey/id974722423
          if (strncmp(buf, "https://podcasts.apple.com/", 27) == 0)
            url = process_apple_rss(buf, sizeof(buf), file);

          trim(url);
          ret = mrss_parse_url_with_options_and_error(url, &data, NULL, &code);
          if (ret)
            {
              DPRINTF(E_LOG, L_SCAN, "Could not parse RSS from '%s': %s\n", url, ret==MRSS_ERR_DOWNLOAD ? mrss_curl_strerror(code) : mrss_strerror(ret));
              goto cleanup;
            }
        } break;

      case RSS_FILE:
        {
          ret = mrss_parse_file((char*)file, &data);
          if (ret)
            {
              DPRINTF(E_LOG, L_SCAN, "Could not parse RSS from '%s': %s\n", file, mrss_strerror(ret));
              goto cleanup;
            }
        } break;

      default:
        DPRINTF(E_LOG, L_SCAN, "BUG: unhandled RSS type %d for file '%s'\n", rss_format, file);
        goto cleanup;
    }

  /* Expect: 'Channel' (used as 'album name') followed by sequence of 'Items' (used as 'tracks' with artist,title,enclosure_url)
   * Look at pubDate in items to determine if it's new
   *
        Channel:
                title: CGP Grey
                description: CGP Grey explains videos.
                link: http://www.cgpgrey.com
                language: en
                rating: (null)
                copyright: 2011-2015
                pubDate: Sun, 26 Jan 2020 15:43:07 +0000
                lastBuildDate: Sun, 09 Feb 2020 15:48:27 +0000
                docs: http://www.cgpgrey.com
                managingeditor: (null)
                webMaster: (null)
                generator: Libsyn WebEngine 2.0
                ttl: 0


        Items:
                title: Your Theme
                link: http://cgpgrey.libsyn.com/your-theme
                description:
                author: (null)
                comments: (null)
                pubDate: Sun, 26 Jan 2020 15:43:07 +0000
                guid: 8d394457-02b5-4bc8-bce8-619aa87d395c
                guid_isPermaLink: 0
                source: (null)
                source_url: (null)
                enclosure: (null)
                enclosure_url: http://traffic.libsyn.com/cgpgrey/Your_New_Years_Resolution_Has_Already_Failed.mp4?dest-id=256939
                enclosure_length: 25467531
                enclosure_type: video/mp4
   */

  db_transaction_begin();

  item = data->item;
  while (item)
    {
      DPRINTF(E_SPAM, L_SCAN, "Channel '%s' item={ PubDate '%s' Author '%s' Title '%s' Src '%s' Type '%s'\n", data->title, item->pubDate, item->author, item->title, item->enclosure_url, item->enclosure_type);

      if (item->enclosure_url)
        {
          scan_metadata_stream(&mfi, item->enclosure_url);

          // always take the main info from the RSS and not the stream
          free(mfi.artist); mfi.artist = safe_strdup(item->author);
          free(mfi.title);  mfi.title  = safe_strdup(item->title);
          free(mfi.album);  mfi.album  = safe_strdup(data->title);
          free(mfi.url);    mfi.url    = safe_strdup(item->link);
          free(mfi.comment); mfi.comment = NULL;

          rss_date(&tm, item->pubDate);
          mfi.date_released = mktime(&tm);
          mfi.year = 1900 + tm.tm_year;
          mfi.track = nadded +1;
          mfi.media_kind = MEDIA_KIND_PODCAST;

          mfi.id = db_file_id_bypath(item->enclosure_url);

          ret = library_media_save(&mfi);
          db_pl_add_item_bypath(pl_id, item->enclosure_url);

          if (++nadded%50 == 0)
            {
                DPRINTF(E_LOG, L_SCAN, "RSS added %d entries...\n", nadded);
                db_transaction_end();
                db_transaction_begin();
            }
        }
      item = item->next;
    }

  db_transaction_end();

cleanup:
  free_mfi(&mfi, 1);
  mrss_free(data);
  if (url != buf) free(url);

  DPRINTF(E_LOG, L_SCAN, "Done processing RSS, added/modified %u items\n", nadded);

  fclose(fp);
}
