
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

#endif /* !__DAAP_QUERY_H__ */
