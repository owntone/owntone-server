
#ifndef __RSP_QUERY_H__
#define __RSP_QUERY_H__

#include <stdint.h>

#define RSP_TYPE_STRING 0
#define RSP_TYPE_INT    1
#define RSP_TYPE_DATE   2

struct rsp_query_field_map {
  uint32_t hash;
  int field_type;
  char *rsp_field;
  /* RSP fields are named after the DB columns - or vice versa */
};


struct rsp_query_field_map *
rsp_query_field_lookup(char *field);

char *
rsp_query_parse_sql(const char *rsp_query);

int
rsp_query_init(void);

void
rsp_query_deinit(void);

#endif /* !__RSP_QUERY_H__ */
