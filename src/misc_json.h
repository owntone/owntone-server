/*
 * Copyright (C) 2017 Christian Meffert <christian.meffert@googlemail.com>
 *
 * Some code included below is in the public domain, check comments
 * in the file.
 *
 * Pieces of code adapted from mt-daapd:
 * Copyright (C) 2003-2007 Ron Pedde (ron@pedde.com)
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


#ifndef SRC_MISC_JSON_H_
#define SRC_MISC_JSON_H_


#include <event2/event.h>
#include <json.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

void
jparse_free(json_object *haystack);

bool
jparse_contains_key(json_object *haystack, const char *key, json_type type);

int
jparse_array_from_obj(json_object *haystack, const char *key, json_object **needle);

const char *
jparse_str_from_obj(json_object *haystack, const char *key);

int
jparse_int_from_obj(json_object *haystack, const char *key);

int
jparse_bool_from_obj(json_object *haystack, const char *key);

time_t
jparse_time_from_obj(json_object *haystack, const char *key);

const char *
jparse_str_from_array(json_object *array, int index, const char *key);

json_object *
jparse_obj_from_evbuffer(struct evbuffer *evbuf);

#endif /* SRC_MISC_JSON_H_ */
