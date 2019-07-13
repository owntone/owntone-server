
#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>


enum settings_type {
  SETTINGS_TYPE_INT,
  SETTINGS_TYPE_BOOL,
  SETTINGS_TYPE_STR,
  SETTINGS_TYPE_CATEGORY,
};

struct settings_option {
  const char *name;
  enum settings_type type;
};

struct settings_category {
  const char *name;
  struct settings_option *options;
  int count_options;
};


int
settings_categories_count();

struct settings_category *
settings_category_get_byindex(int index);

struct settings_category *
settings_category_get(const char *name);

int
settings_option_count(struct settings_category *category);

struct settings_option *
settings_option_get_byindex(struct settings_category *category, int index);

struct settings_option *
settings_option_get(struct settings_category *category, const char *name);

int
settings_option_getint(struct settings_option *option);


bool
settings_option_getbool(struct settings_option *option);

char *
settings_option_getstr(struct settings_option *option);

int
settings_option_setint(struct settings_option *option, int value);

int
settings_option_setbool(struct settings_option *option, bool value);

int
settings_option_setstr(struct settings_option *option, const char *value);


#endif /* __SETTINGS_H__ */
