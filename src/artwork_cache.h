/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
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

#ifndef ARTWORK_CACHE_H_
#define ARTWORK_CACHE_H_

#include <stddef.h>
#include <stdint.h>

int
artworkcache_ping(char *path, time_t mtime, int del);

int
artworkcache_delete_by_path(char *path);

int
artworkcache_purge_cruft(time_t ref);

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
