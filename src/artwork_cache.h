/*
 * artwork_cache.h
 *
 *  Created on: Oct 12, 2014
 *      Author: asiate
 */

#ifndef ARTWORK_CACHE_H_
#define ARTWORK_CACHE_H_

#include <stddef.h>
#include <stdint.h>


int
artworkcache_add(int64_t peristentid, int max_w, int max_h, int format, char *filename, char *data, int datalen);

int
artworkcache_get(int64_t persistentid, int max_w, int max_h, int *cached, int *format, char **data, int *datalen);

int
artworkcache_perthread_init(void);

void
artworkcache_perthread_deinit(void);

int
artworkcache_init(void);


#endif /* ARTWORK_CACHE_H_ */
