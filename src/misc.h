
#ifndef __MISC_H__
#define __MISC_H__

#include <stdint.h>
#include <time.h>

struct onekeyval {
  char *name;
  char *value;

  struct onekeyval *next;
  struct onekeyval *sort;
};

struct keyval {
  struct onekeyval *head;
  struct onekeyval *tail;
};


int
safe_atoi32(const char *str, int32_t *val);

int
safe_atou32(const char *str, uint32_t *val);

int
safe_hextou32(const char *str, uint32_t *val);

int
safe_atoi64(const char *str, int64_t *val);

int
safe_atou64(const char *str, uint64_t *val);

int
safe_hextou64(const char *str, uint64_t *val);


/* Key/value functions */
struct keyval *
keyval_alloc(void);

int
keyval_add(struct keyval *kv, const char *name, const char *value);

int
keyval_add_size(struct keyval *kv, const char *name, const char *value, size_t size);

void
keyval_remove(struct keyval *kv, const char *name);

const char *
keyval_get(struct keyval *kv, const char *name);

void
keyval_clear(struct keyval *kv);

void
keyval_sort(struct keyval *kv);


char *
m_realpath(const char *pathname);

char *
unicode_fixup_string(char *str);

char *
trimwhitespace(const char *str);

uint32_t
djb_hash(void *data, size_t len);

char *
b64_decode(const char *b64);

char *
b64_encode(uint8_t *in, size_t len);

uint64_t
murmur_hash64(const void *key, int len, uint32_t seed);

/* Timer function for platforms without hi-res timers */
int
clock_gettime_with_res(clockid_t clock_id, struct timespec *tp, struct timespec *res);

struct timespec
timespec_add(struct timespec time1, struct timespec time2);

int
timespec_cmp(struct timespec time1, struct timespec time2);

#endif /* !__MISC_H__ */
