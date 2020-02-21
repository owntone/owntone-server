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

#include "rss.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <time.h>

#include <event2/event.h>
#include <mxml.h>
#include "mxml-compat.h"

#include "conffile.h"
#include "logger.h"
#include "db.h"
#include "http.h"
#include "misc.h"
#include "misc_json.h"
#include "library.h"
#include "library/filescanner.h"


static struct event *rssev;
static struct timeval rss_refresh_interval = { 60, 0 };
static bool scanning;

void
free_rfi(struct rss_file_item* rfi)
{
  if (!rfi) return;

  struct rss_file_item *tmp;
  while (rfi)
  {
    tmp = rfi->next;
    free(rfi->url);
    free(rfi->title);
    free(rfi);
    rfi = tmp;
  }
}

// should only be called by rfi_add() and if you are getting a new list
struct rss_file_item*
rfi_alloc()
{
  struct rss_file_item *obj = malloc(sizeof(struct rss_file_item));
  memset(obj, 0, sizeof(struct rss_file_item));
  return obj;
}

// returns the newly alloc'd / added item at end of list
struct rss_file_item*
rfi_add(struct rss_file_item* head)
{
  struct rss_file_item *curr = head;
  while (curr->next)
    curr = curr->next;

  curr->next = rfi_alloc();
  return curr->next;
}


static int
rss_playlist_fill(struct playlist_info *pli, const char *path, const char *name)
{
  memset(pli, 0, sizeof(struct playlist_info));

  pli->type  = PL_RSS;
  pli->path  = strdup(path);
  pli->title = strdup(name);
  pli->virtual_path = malloc(strlen(path)+2);
  sprintf(pli->virtual_path, "/%s", path);

  return 0;
}

static int
rss_playlist_add(const char *path, const char *name)
{
  struct playlist_info pli;
  int ret;

  ret = rss_playlist_fill(&pli, path, name);
  if (ret < 0)
    return -1;

  ret = library_playlist_save(&pli);
  if (ret < 0)
    {
      free_pli(&pli, 1);
      return -1;
    }

  free_pli(&pli, 1);

  return ret;
}

static int
rss_playlist_prepare(const char *path, const char *name, time_t mtime, bool *isnew)
{
  struct playlist_info *pli;
  int pl_id;

  pli = db_pl_fetch_bypath(path);
  if (!pli)
    {
      DPRINTF(E_LOG, L_RSS, "New RSS found, processing '%s'\n", path);
      *isnew = true;

      pl_id = rss_playlist_add(path, name);
      if (pl_id < 0)
        {
          DPRINTF(E_LOG, L_RSS, "Error adding RSS '%s'\n", path);
          return -1;
        }

      DPRINTF(E_INFO, L_RSS, "Added new RSS as id %d\n", pl_id);
      return pl_id;
    }
  *isnew = false;

  pl_id = pli->id;
  free_pli(pli, 0);

  return pl_id;
}

// RSS spec: https://validator.w3.org/feed/docs/rss2.html

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
process_apple_rss(const char *rss_url)
{
  struct http_client_ctx ctx;
  struct evbuffer *evbuf;
  char url[100];
  char *buf = NULL;
  unsigned podid;  // apple podcast id
  json_object *json = NULL;
  json_object *jsonra = NULL;
  const char *feedURL;
  const char *ptr;
  int ret;

  // ask for the json to get feedUrl
  // https://itunes.apple.com/lookup?id=974722423

  ptr = strrchr(rss_url, '/');
  if (!ptr)
    {
      DPRINTF(E_LOG, L_RSS, "Could not parse Apple Podcast RSS ID from '%s'\n", rss_url);
      return NULL;
    }
  if (sscanf(ptr, "/id%u", &podid) != 1)
    {
      DPRINTF(E_LOG, L_RSS, "Could not parse Apple Podcast RSS ID from '%s'\n", rss_url);
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
  if (ret < 0 || (ret && ctx.response_code != HTTP_OK))
    {
      evbuffer_free(evbuf);
      return NULL;
    }

  json = jparse_obj_from_evbuffer(evbuf);
  if (!json)
    {
      DPRINTF(E_LOG, L_RSS, "Could not parse RSS apple response, podcast id %u\n", podid);
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
          buf = strcpy(malloc(strlen(feedURL)+1), feedURL);
	  DPRINTF(E_DBG, L_RSS, "mapped apple podcast URL: %s -> %s\n", rss_url, buf);
        }
      else
        DPRINTF(E_DBG, L_RSS, "Could not parse feedURL from RSS apple, podcast id %u\n", podid);
    }

  jparse_free(json);
  evbuffer_free(evbuf);
  return buf;
}

#ifdef RSS_DEBUG
static void
rss_playlist_items(int plid)
{
  struct query_params qp;
  struct db_media_file_info dbpli;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_PLITEMS;
  qp.idx_type = I_NONE;
  qp.id = plid;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      return;
    }
  while (((ret = db_query_fetch_file(&qp, &dbpli)) == 0) && (dbpli.id))
    {
      DPRINTF(E_LOG, L_RSS, "plid=%u  { id=%s title=%s path=%s }\n", plid, dbpli.id, dbpli.title, dbpli.path);
    }
  db_query_end(&qp);

  return;
}
#endif


int
rss_feed_refresh(int pl_id, time_t mtime, const char *url, unsigned *nadded, long limit)
{
  struct media_file_info mfi;
  char *vpath = NULL;
  int feed_file_id;
  struct tm tm;
  unsigned vpathlen = 0;
  unsigned len = 0;

  char *apple_url = NULL;

  const char *rss_xml = NULL;
  mxml_node_t *tree = NULL;
  mxml_node_t *channel;
  mxml_node_t *node;
  mxml_node_t *item;
  const char *rss_feed_title = NULL;
  const char *rss_feed_author = NULL;
  const char *rss_item_title = NULL;
  const char *rss_item_pubDate = NULL;
  const char *rss_item_url = NULL;
  const char *rss_item_link = NULL;
  const char *rss_item_type = NULL;

  struct http_client_ctx ctx;
  struct evbuffer *evbuf;

  int ret = -1;

  DPRINTF(E_DBG, L_RSS, "Refreshing RSS id: %u url: %s limit: %ld\n", pl_id, url, limit);
  db_pl_ping(pl_id);
  db_pl_ping_items_bymatch("http://", pl_id);
  db_pl_ping_items_bymatch("https://", pl_id);

  evbuf = evbuffer_new();
  if (!evbuf)
    goto cleanup;

  // Is it an apple podcast stream?
  // ie https://podcasts.apple.com/is/podcast/cgp-grey/id974722423
  if (strncmp(url, "https://podcasts.apple.com/", 27) == 0)
    apple_url = process_apple_rss(url);

  memset(&ctx, 0, sizeof(struct http_client_ctx));
  ctx.url = apple_url ? apple_url : url;
  ctx.input_body = evbuf;

  ret = http_client_request(&ctx);
  if (ret < 0 || (ret && ctx.response_code != HTTP_OK))
    {
      DPRINTF(E_WARN, L_RSS, "Failed to fetch RSS id: %u url: %s resp: %d\n", pl_id, url, ctx.response_code);
      goto cleanup;
    }

  evbuffer_add(ctx.input_body, "", 1);
  rss_xml = (const char*)evbuffer_pullup(ctx.input_body, -1);
  if (!rss_xml || strlen(rss_xml) == 0)
    {
      DPRINTF(E_WARN, L_RSS, "Failed to fetch valid RSS/xml data RSS id: %u url: %sn", pl_id, url);
      goto cleanup;
    }

  tree = mxmlLoadString(NULL, rss_xml, MXML_OPAQUE_CALLBACK);

  channel = mxmlFindElement(tree, tree, "channel", NULL, NULL, MXML_DESCEND);
  if (channel == NULL)
    {
      DPRINTF(E_WARN, L_RSS, "Invalid RSS/xml, missing 'channel' node - RSS id: %u url: %s\n", pl_id, url);
      DPRINTF(E_DBG, L_RSS, "RSS xml len: %ld xml: { %s }\n", strlen(rss_xml), rss_xml);
      goto cleanup;
    }

  node = mxmlFindElement(channel, channel, "title", NULL, NULL, MXML_DESCEND);
  if (!node)
    {
      DPRINTF(E_WARN, L_RSS, "Invalid RSS/xml, missing 'title' - RSS id: %u url: %s\n", pl_id, url);
      goto cleanup;
    }
  rss_feed_title = mxmlGetOpaque(node);

  node = mxmlFindElement(channel, channel, "itunes:author", NULL, NULL, MXML_DESCEND);
  if (node)
    rss_feed_author = mxmlGetOpaque(node);

  memset(&mfi, 0, sizeof(struct media_file_info));
  for (node = mxmlFindElement(channel, channel, "item", NULL, NULL, MXML_DESCEND); 
       node != NULL; 
       node = mxmlFindElement(node, channel, "item", NULL, NULL, MXML_DESCEND))
  {
    item = mxmlFindElement(node, node, "title", NULL, NULL, MXML_DESCEND);
    rss_item_title = mxmlGetOpaque(item);

    item = mxmlFindElement(node, node, "pubDate", NULL, NULL, MXML_DESCEND);
    rss_item_pubDate = mxmlGetOpaque(item);

    item = mxmlFindElement(node, node, "link", NULL, NULL, MXML_DESCEND);
    rss_item_link = mxmlGetOpaque(item);

    item = mxmlFindElement(node, node, "enclosure", NULL, NULL, MXML_DESCEND);
    rss_item_url = mxmlElementGetAttr(item, "url");
    rss_item_type = mxmlElementGetAttr(item, "type");

    DPRINTF(E_DBG, L_RSS, "Feed provides RSS id: %d name: '%s' pubDate: %s url: %s title: '%s'\n", pl_id, rss_feed_title, rss_item_pubDate, rss_item_url, rss_item_title);
    if (!rss_item_url)
      continue;

 
    len = strlen(rss_item_url)+2;
    if (len > vpathlen)
      {
	vpathlen = len;
	free(vpath);
	vpath = malloc(len);
      }
    sprintf(vpath, "/%s", rss_item_url);

    // check if this item is already in the db - if so, we can stop since the RSS is given to us as LIFO stream
    if ((feed_file_id = db_file_id_by_virtualpath_match(vpath)) > 0)
      {
	DPRINTF(E_DBG, L_RSS, "Most recent DB RSS id: %d name: '%s' url: %s file_id: %d pubdate: %s title: '%s'\n", pl_id, rss_feed_title, url, feed_file_id, rss_item_pubDate, rss_item_title);
	break;
      }
    DPRINTF(E_INFO, L_RSS, "Adding item to RSS id: %d name: '%s' url: %s pubdate: %s title: '%s'\n", pl_id, rss_feed_title, rss_item_url, rss_item_pubDate, rss_item_title);

    scan_metadata_stream(&mfi, rss_item_url);

    if (mfi.song_length == 0 && mfi.file_size == 0)
      {
	DPRINTF(E_INFO, L_RSS, "Ignoring item (empty media) RSS id: %d name: '%s' url: %s pubdate: %s title: '%s'\n", pl_id, rss_feed_title, rss_item_url, rss_item_pubDate, rss_item_title);
	free_mfi(&mfi, 1);
	continue;
      }

    // Always take the meta from media file if possible; some podcasts
    // (apple) can use mp4 streams which tend not to have decent tags so 
    // in those cases take info from the RSS and not the stream
    if (!mfi.artist) mfi.artist = safe_strdup(rss_feed_author);
    if (!mfi.album)  mfi.album  = safe_strdup(rss_feed_title);
    if (!mfi.url)    mfi.url    = safe_strdup(rss_item_link);
    if (!mfi.genre)  mfi.genre  = strdup("Podcast");

    // Title not valid on most mp4 (it becomes the url obj) so take from RSS feed
    if (rss_item_type && strncmp("video", rss_item_type, 5) == 0)
      {
	free(mfi.title);
	mfi.title = safe_strdup(rss_item_title);
      }

    // Ignore this - some can be very verbose - we don't show use these
    // on the podcast
    free(mfi.comment); mfi.comment = NULL;

    // date is always from the RSS feed info
    rss_date(&tm, rss_item_pubDate);
    mfi.date_released = mktime(&tm);
    mfi.year = 1900 + tm.tm_year;

    mfi.media_kind = MEDIA_KIND_PODCAST;

    // Fake the time - useful when we are adding a new stream - since the
    // newest podcasts are added first (the stream is most recent first)
    // having time_added date which is older on the most recent episodes
    // makes no sense so make all the dates the same for a singleu update
    mfi.time_added = mtime;

    mfi.id = db_file_id_bypath(rss_item_url);

    ret = library_media_save(&mfi);
    db_pl_add_item_bypath(pl_id, rss_item_url);

    *nadded = *nadded +1;
    if (*nadded%50 == 0)
      {
	DPRINTF(E_INFO, L_RSS, "RSS added %d entries...\n", *nadded);
      }

    free_mfi(&mfi, 1);

    if (limit > 0 && *nadded == limit)
      {
	DPRINTF(E_INFO, L_RSS, "RSS added limit reached, added %d entries...\n", *nadded);
	break;
      }
  }


cleanup:
  evbuffer_free(evbuf);
  mxmlDelete(tree);
  free(vpath);
  free(apple_url);

  return ret;
}

int
rss_add(const char *name, const char *feed_url, long limit)
{
  int pl_id = -1;
  time_t now;
  unsigned nadded = 0;
  bool isnew = false;
  int ret;

  DPRINTF(E_DBG, L_RSS, "RSS working on: '%s' '%s'\n", name, feed_url);
  if (strncmp(feed_url, "http://", 7) != 0 && strncmp(feed_url, "https://", 8) != 0)
    {
      DPRINTF(E_LOG, L_RSS, "Invalid RSS url '%s'\n", feed_url);
      return -1;
    }

  time(&now);
  pl_id = rss_playlist_prepare(feed_url, name, now, &isnew);
  if (pl_id < 0)
    return -1;

  if (!isnew)
    {
      DPRINTF(E_LOG, L_RSS, "Duplicate RSS exists id: %d url: %s\n", pl_id, feed_url);
      return -1;
    }

  ret = rss_feed_refresh(pl_id, now, feed_url, &nadded, limit);
  if (ret < 0) 
    {
      DPRINTF(E_LOG, L_RSS, "Failed to add RSS %s\n", feed_url);
      db_pl_delete(pl_id);
      return -1;
    }

  DPRINTF(E_LOG, L_RSS, "Done processing RSS %s added/modified %u items\n", feed_url, nadded);

  return 0;
}

int
rss_remove(const char *feed_url)
{
  int pl_id;

  DPRINTF(E_DBG, L_RSS, "removing RSS: '%s'\n", feed_url);
  pl_id = db_pl_id_bypath(feed_url);
  if (pl_id < 0)
    {
      DPRINTF(E_INFO, L_RSS, "Cannot remove RSS - No such RSS feed: '%s'\n", feed_url);
      return -1;
    }

  db_pl_clear_items(pl_id);
  db_pl_delete(pl_id);

  return 0;
}

 
static void
rss_protect_feeds()
{
  struct query_params query_params;
  struct db_playlist_info dbpli;
  unsigned feeds = 0;
  int pl_id;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));

  DPRINTF(E_DBG, L_RSS, "Protecting RSS feeds\n");

  query_params.type = Q_PL;
  query_params.sort = S_PLAYLIST;
  query_params.filter = db_mprintf("(f.type = %d)", PL_RSS);

  ret = db_query_start(&query_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RSS, "Failed to find current RSS feeds from db\n");
      goto error;
    }

  while (((ret = db_query_fetch_pl(&query_params, &dbpli)) == 0) && (dbpli.id))
    {
      pl_id = atoi(dbpli.id);

      DPRINTF(E_DBG, L_RSS, "Protecting feed id: %d '%s' at %s\n", pl_id, dbpli.title, dbpli.path);

      db_pl_ping(pl_id);
      db_pl_ping_items_bymatch("http://", pl_id);
      db_pl_ping_items_bymatch("https://", pl_id);

      ++feeds;
    }
  db_query_end(&query_params);

  DPRINTF(E_DBG, L_RSS, "Completed protecing RSS feeds: %u\n", feeds);

 error:
  free(query_params.filter);
}

 
static int
rss_refresh()
{
  struct query_params query_params;
  struct db_playlist_info dbpli;
  struct rss_file_item *rfi = NULL;
  struct rss_file_item *head = NULL;
  time_t  now;
  unsigned feeds = 0;
  unsigned nadded = 0;
  int ret = 0;

  memset(&query_params, 0, sizeof(struct query_params));

  DPRINTF(E_INFO, L_RSS, "Refreshing RSS feeds\n");
  scanning = true;

  query_params.type = Q_PL;
  query_params.sort = S_PLAYLIST;
  query_params.filter = db_mprintf("(f.type = %d)", PL_RSS);

  ret = db_query_start(&query_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RSS, "Failed to find current RSS feeds from db\n");
      goto error;
    }

  while (((ret = db_query_fetch_pl(&query_params, &dbpli)) == 0) && (dbpli.id))
    {
      if (!rfi) 
      {
	rfi = rfi_alloc();
	head = rfi;
      }
      else
	rfi = rfi_add(rfi);

      rfi->id = atol(dbpli.id);
      rfi->title = safe_strdup(dbpli.title);
      rfi->url = safe_strdup(dbpli.path);
      rfi->lastupd = atol(dbpli.db_timestamp);
    }
  db_query_end(&query_params);

  rfi = head;
  while (rfi)
  {
    DPRINTF(E_DBG, L_RSS, "Sync'ing %s  last update: %s", rfi->title, ctime(&(rfi->lastupd)));
    db_transaction_begin();
    rss_feed_refresh(rfi->id, time(&now), rfi->url, &nadded, -1);
    db_transaction_end();

    rfi = rfi->next;
    ++feeds;
  }
  scanning = false;

  DPRINTF(E_INFO, L_RSS, "Completed refreshing RSS feeds: %u items: %u\n", feeds, nadded);

 error:
  free(query_params.filter);
  free_rfi(head);

  evtimer_add(rssev, &rss_refresh_interval);
  return ret;
}

static void
rss_refresh_cb(int fd, short what, void *arg)
{
  rss_refresh();
}

/* Thread: library */
static int
rss_rescan()
{
  time_t start;
  time_t end;
  int ret;

  if (scanning)
    {
      DPRINTF(E_DBG, L_RSS, "Scan already in progress, rescan ignored\n");
      return 0;
    }

  start = time(NULL);
  scanning = true;

  ret = rss_refresh();

  scanning = false;
  end = time(NULL);

  DPRINTF(E_LOG, L_RSS, "RSS scan completed in %.f sec\n", difftime(end, start));
  return ret;
}

static int
rss_metarescan()
{
  time_t start;
  time_t end;

  if (scanning)
    {
      DPRINTF(E_DBG, L_RSS, "Scan already in progress, meta rescan ignored\n");
      return 0;
    }

  start = time(NULL);
  scanning = true;

  rss_protect_feeds();

  scanning = false;
  end = time(NULL);

  DPRINTF(E_LOG, L_RSS, "RSS meta scan completed in %.f sec\n", difftime(end, start));
  return 0;
}

static int
rss_fullrescan()
{
  time_t start;
  time_t end;
  int ret;

  if (scanning)
    {
      DPRINTF(E_DBG, L_RSS, "Scan already in progress, fullscan ignored\n");
      return 0;
    }

  start = time(NULL);
  scanning = true;

  // TODO - how to protect??
  //db_rss_tmp_clone();
  //db_purge_all(); // Clears files, playlists, playlistitems, inotify and groups
  //db_rss_tmp_restore();

  ret = rss_refresh();

  scanning = false;
  end = time(NULL);

  DPRINTF(E_LOG, L_RSS, "RSS fullscan completed in %.f sec\n", difftime(end, start));
  return ret;
}

static int
init()
{
  rss_refresh_interval.tv_sec = cfg_getint(cfg_getsec(cfg, "rss"), "refresh_period");
  if (rss_refresh_interval.tv_sec < 60)
    {
      DPRINTF(E_LOG, L_RSS, "RSS 'refresh_period' too low, defaulting to 60 seconds\n");
      rss_refresh_interval.tv_sec = 60;
    }
  DPRINTF(E_INFO, L_RSS, "RSS refresh_period: %lu seconds\n", rss_refresh_interval.tv_sec);

  scanning = false;
  rssev = NULL;

  return 0;
}

static void
deinit()
{
  event_free(rssev);
}

static int
rss_events(struct event_base *evbase_lib)
{
  if (rssev)
    return -1;

  rssev = evtimer_new(evbase_lib, rss_refresh_cb, NULL);
  if (!rssev)
    {
      DPRINTF(E_FATAL, L_RSS, "Failed to create timer event\n");
      return -1;
    }
  evtimer_add(rssev, &rss_refresh_interval);
  return 0;
}

struct library_source rssscanner =
{
  .name = "RSS feed source",
  .disabled = 0,
  .init = init,
  .deinit = deinit,
  .rescan = rss_rescan,
  .metarescan = rss_metarescan,
  .initscan = rss_rescan,
  .fullrescan = rss_fullrescan,
  .register_events = rss_events,
};
