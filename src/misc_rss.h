#ifndef MISC_RSS_H
#define MISC_RSS_H

#include <time.h>

// relevant fields from playlist tbl
struct rss_file_item {
  int id;
  char *title;
  char *url;
  time_t lastupd;

  struct rss_file_item *next;
};


struct rss_file_item*
rfi_alloc();

struct rss_file_item*
rfi_add(struct rss_file_item* head);

void
free_rfi(struct rss_file_item* rfi);


int
rss_add(const char *name, const char *url, long limit);

int
rss_remove(const char *name, const char *url);

// limit <= 0 means take all recent items on the RSS feed
int
rss_feed_refresh(int pl_id, time_t mtime, const char *url, unsigned *nadded, long limit);

#endif
