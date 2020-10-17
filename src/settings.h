
#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>


enum settings_type {
  SETTINGS_TYPE_INT,
  SETTINGS_TYPE_BOOL,
  SETTINGS_TYPE_STR,
  SETTINGS_TYPE_CATEGORY,
};

union settings_default_value {
  int intval;
  bool boolval;
  char *strval;
};

struct settings_option {
  const char *name;
  enum settings_type type;
  union settings_default_value default_value;
};

struct settings_category {
  const char *name;
  struct settings_option *options;
  int count_options;
};


int
settings_categories_count(void);

struct settings_category *
settings_category_get_byindex(int index);

struct settings_category *
settings_category_get(const char *name);

int
settings_option_count(struct settings_category *category);

struct settings_option *
settings_option_get(struct settings_category *category, const char *name);

struct settings_option *
settings_option_get_byindex(struct settings_category *category, int index);


int
settings_option_getint(struct settings_option *option);

bool
settings_option_getbool(struct settings_option *option);

char *
settings_option_getstr(struct settings_option *option);

#define SETTINGS_GETINT(category, name) settings_option_getint(settings_option_get((category), (name)))
#define SETTINGS_GETBOOL(category, name) settings_option_getbool(settings_option_get((category), (name)))
#define SETTINGS_GETSTR(category, name) settings_option_getstr(settings_option_get((category), (name)))

int
settings_option_setint(struct settings_option *option, int value);

int
settings_option_setbool(struct settings_option *option, bool value);

int
settings_option_setstr(struct settings_option *option, const char *value);

#define SETTINGS_SETINT(category, name, value) settings_option_setint(settings_option_get((category), (name)), (value))
#define SETTINGS_SETBOOL(category, name, value) settings_option_setbool(settings_option_get((category), (name)), (value))
#define SETTINGS_SETSTR(category, name, value) settings_option_setstr(settings_option_get((category), (name)), (value))


int
settings_option_delete(struct settings_option *option);

#define SETTINGS_DELETE(category, name) settings_option_delete(settings_option_get((category), (name)))

#endif /* __SETTINGS_H__ */
