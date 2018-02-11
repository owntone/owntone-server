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

#include <event2/buffer.h>
#include <event2/event.h>
#include <json.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "logger.h"

void
jparse_free(json_object* haystack)
{
  if (haystack)
    {
#ifdef HAVE_JSON_C_OLD
      json_object_put(haystack);
#else
      if (json_object_put(haystack) != 1)
        DPRINTF(E_LOG, L_MISC, "Memleak: JSON parser did not free object\n");
#endif
    }
}

bool
jparse_contains_key(json_object *haystack, const char *key, json_type type)
{
  json_object *needle;

  return json_object_object_get_ex(haystack, key, &needle) && json_object_get_type(needle) == type;
}

int
jparse_array_from_obj(json_object *haystack, const char *key, json_object **needle)
{
  if (! (json_object_object_get_ex(haystack, key, needle) && json_object_get_type(*needle) == json_type_array) )
    return -1;
  else
    return 0;
}

const char *
jparse_str_from_obj(json_object *haystack, const char *key)
{
  json_object *needle;

  if (json_object_object_get_ex(haystack, key, &needle) && json_object_get_type(needle) == json_type_string)
    return json_object_get_string(needle);
  else
    return NULL;
}

int
jparse_int_from_obj(json_object *haystack, const char *key)
{
  json_object *needle;

  if (json_object_object_get_ex(haystack, key, &needle) && json_object_get_type(needle) == json_type_int)
    return json_object_get_int(needle);
  else
    return 0;
}

int
jparse_bool_from_obj(json_object *haystack, const char *key)
{
  json_object *needle;

  if (json_object_object_get_ex(haystack, key, &needle) && json_object_get_type(needle) == json_type_boolean)
    return json_object_get_boolean(needle);
  else
    return false;
}

time_t
jparse_time_from_obj(json_object *haystack, const char *key)
{
  const char *tmp;
  struct tm tp;
  time_t parsed_time;

  memset(&tp, 0, sizeof(struct tm));

  tmp = jparse_str_from_obj(haystack, key);
  if (!tmp)
    return 0;

  strptime(tmp, "%Y-%m-%dT%H:%M:%SZ", &tp);
  parsed_time = mktime(&tp);
  if (parsed_time < 0)
    return 0;

  return parsed_time;
}

const char *
jparse_str_from_array(json_object *array, int index, const char *key)
{
  json_object *item;
  int count;

  if (json_object_get_type(array) != json_type_array)
    return NULL;

  count = json_object_array_length(array);
  if (count <= 0 || count <= index)
    return NULL;

  item = json_object_array_get_idx(array, index);
  return jparse_str_from_obj(item, key);
}

json_object *
jparse_obj_from_evbuffer(struct evbuffer *evbuf)
{
  char *json_str;

  // 0-terminate for safety
  evbuffer_add(evbuf, "", 1);

  json_str = (char *) evbuffer_pullup(evbuf, -1);
  if (!json_str || (strlen(json_str) == 0))
    {
      DPRINTF(E_LOG, L_MISC, "Failed to parse JSON from input buffer\n");
      return NULL;
    }

  return json_tokener_parse(json_str);
}
