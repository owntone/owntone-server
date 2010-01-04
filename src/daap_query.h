
#ifndef __DAAP_QUERY_H__
#define __DAAP_QUERY_H__

#include <stdint.h>

#include "logger.h"
#include "misc.h"

struct dmap_query_field_map {
  uint32_t hash;
  int as_int;
  char *dmap_field;
  char *db_col;
};


struct dmap_query_field_map *
daap_query_field_lookup(char *field);

char *
daap_query_parse_sql(const char *daap_query);

int
daap_query_init(void);

void
daap_query_deinit(void);


static inline int64_t
daap_songalbumid(const char *album_artist, const char *album)
{
  /* album_artist & album are both VARCHAR(1024) + 2 chars + NUL */
  char hashbuf[2052];
  uint64_t hash;
  int ret;

  ret = snprintf(hashbuf, sizeof(hashbuf), "%s==%s", (album_artist) ? album_artist : "", (album) ? album : "");
  if ((ret < 0) || (ret >= sizeof(hashbuf)))
    {
      DPRINTF(E_WARN, L_DAAP, "Not enough room for album_artist==album concatenation\n");

      return 0;
    }

  /* Limit hash length to 63 bits, due to signed type in sqlite */
  hash = murmur_hash64(hashbuf, ret, 0);

  return hash >> 1;
}

#endif /* !__DAAP_QUERY_H__ */
