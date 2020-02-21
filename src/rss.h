#ifndef RSS_H
#define RSS_H

int
rss_add(const char *name, const char *url, long limit);

int
rss_remove(const char *url);

#endif
