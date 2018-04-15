
#ifndef __SMARTPL_QUERY_H__
#define __SMARTPL_QUERY_H__


struct smartpl {
  char *title;
  char *query_where;
  char *having;
  char *order;
  int limit;
};

int
smartpl_query_parse_file(struct smartpl *smartpl, const char *file);

int
smartpl_query_parse_string(struct smartpl *smartpl, const char *expression);

void
free_smartpl(struct smartpl *smartpl, int content_only);

#endif /* __SMARTPL_QUERY_H__ */
