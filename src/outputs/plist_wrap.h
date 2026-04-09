/*
 * Convenience wrappers for libplist
 *
 */

#include <plist/plist.h>

#include "config.h"

__attribute__((unused)) static void
wplist_dict_add_uint(plist_t node, const char *key, uint64_t val)
{
  plist_t add = plist_new_uint(val);
  plist_dict_set_item(node, key, add);
}

__attribute__((unused)) static void
wplist_dict_add_int(plist_t node, const char *key, int64_t val)
{
#if HAVE_DECL_PLIST_NEW_INT
  plist_t add = plist_new_int(val);
#else
  plist_t add = plist_new_uint((uint64_t)val);
#endif
  plist_dict_set_item(node, key, add);
}

__attribute__((unused)) static void
wplist_dict_add_string(plist_t node, const char *key, const char *val)
{
  plist_t add = plist_new_string(val);
  plist_dict_set_item(node, key, add);
}

__attribute__((unused)) static void
wplist_dict_add_bool(plist_t node, const char *key, bool val)
{
  plist_t add = plist_new_bool(val);
  plist_dict_set_item(node, key, add);
}

__attribute__((unused)) static void
wplist_dict_add_data(plist_t node, const char *key, uint8_t *data, size_t len)
{
  plist_t add = plist_new_data((const char *)data, len);
  plist_dict_set_item(node, key, add);
}

__attribute__((unused)) static int
wplist_to_bin(uint8_t **data, size_t *len, plist_t node)
{
  char *out = NULL;
  uint32_t out_len = 0;

  plist_to_bin(node, &out, &out_len);
  if (!out)
    return -1;

  *data = (uint8_t *)out;
  *len = out_len;

  return 0;
}

__attribute__((unused)) static char *
wplist_to_xml(plist_t node)
{
  char *out;
  uint32_t out_len;

  plist_to_xml(node, &out, &out_len); // out is a C string
  return out;
}

__attribute__((unused)) static int
wplist_from_evbuf(plist_t *node, struct evbuffer *evbuf)
{
  uint8_t *data = evbuffer_pullup(evbuf, -1);
  size_t len = evbuffer_get_length(evbuf);
  plist_t out = NULL;

  plist_from_bin((char *)data, (uint32_t)len, &out);
  if (!out)
    return -1;

  *node = out;

  return 0;
}
