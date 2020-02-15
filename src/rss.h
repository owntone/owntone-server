#ifndef FD_RSS_H
#define FD_RSS_H

/* Start RSS scanning thread
 * @return       0 on success, -1 on error
 */
int
rss_init(void);

void
rss_deinit(void);

int
rss_feed_create(const char *name, const char* url);

int
rss_feed_delete(const char *name, const char* url);

#endif
