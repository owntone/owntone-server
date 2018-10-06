
#ifndef __MISC_H__
#define __MISC_H__

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/* Samples to bytes, bytes to samples */
#define STOB(s) ((s) * 4)
#define BTOS(b) ((b) / 4)

#define ARRAY_SIZE(x) ((unsigned int)(sizeof(x) / sizeof((x)[0])))

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


char **
buildopts_get(void);

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

char *
safe_strdup(const char *str);

char *
safe_asprintf(const char *fmt, ...);

int
safe_snprintf_cat(char *dst, size_t n, const char *fmt, ...);


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


char **
m_readfile(const char *path, int num_lines);

char *
unicode_fixup_string(char *str, const char *fromcode);

// Modifies str so it is trimmed. Returns pointer to str.
char *
trim(char *str);

// Copies the trimmed part of str to a newly allocated string (caller must free)
char *
atrim(const char *str);

void
swap_pointers(char **a, char **b);

uint32_t
djb_hash(const void *data, size_t len);

char *
b64_decode(const char *b64);

char *
b64_encode(const uint8_t *in, size_t len);

uint64_t
murmur_hash64(const void *key, int len, uint32_t seed);

// Checks if the address is in a network that is configured as trusted
bool
peer_address_is_trusted(const char *addr);


#ifndef HAVE_CLOCK_GETTIME

#ifndef CLOCK_REALTIME
#  define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#  define CLOCK_MONOTONIC 1
#endif

typedef int clockid_t;

int
clock_gettime(clockid_t clock_id, struct timespec *tp);

int
clock_getres(clockid_t clock_id, struct timespec *res);
#endif

#ifndef HAVE_TIMER_SETTIME

struct itimerspec {
  struct timespec it_interval;
  struct timespec it_value;
};
typedef uint64_t timer_t;

int
timer_create(clockid_t clock_id, void *sevp, timer_t *timer_id);

int
timer_delete(timer_t timer_id);

int
timer_settime(timer_t timer_id, int flags, const struct itimerspec *tp,
              struct itimerspec *old);

int
timer_getoverrun(timer_t timer_id);

#endif

/* Timer function for platforms without hi-res timers */
int
clock_gettime_with_res(clockid_t clock_id, struct timespec *tp, struct timespec *res);

struct timespec
timespec_add(struct timespec time1, struct timespec time2);

int
timespec_cmp(struct timespec time1, struct timespec time2);

struct timespec
timespec_reltoabs(struct timespec relative);

/* initialize mutex with error checking (not default on all platforms) */
int
mutex_init(pthread_mutex_t *mutex);

/* Check that the function returns 0, logging a fatal error referencing
   returned error (type errno) if it fails, and aborts the process.
   Example: CHECK_ERR(L_MAIN, my_function()); */
#define CHECK_ERR(d, f) \
  do { int chk_err; \
    if ( (chk_err = (f)) != 0) \
      log_fatal_err(d, #f, __LINE__, chk_err); \
  } while(0)

/* Check that the function returns 0 or okval, logging a fatal
   error referencing returned erro (type errno) if not, and aborts the process.
   Example: int err; CHECK_ERR_EXCEPT(L_MAIN, my_wait(), err, ETIMEDOUT); */
#define CHECK_ERR_EXCEPT(d, f, var, okval) \
  do { (var) = (f);                             \
    if (! (((var) == (okval)) || ((var) == 0))) \
      log_fatal_err(d, #f, __LINE__, (var)); \
  } while(0)

/* Check that the function returns value >= 0, logging a fatal error
   referencing errno if it not, and aborts the process.
   Example: int ret; CHECK_ERRNO(L_MAIN, ret = my_function()); */
#define CHECK_ERRNO(d, f) \
  do { \
    if ( (f) < 0 ) \
      log_fatal_errno(d, #f, __LINE__); \
  } while(0)

/* Check that the function returns non-NULL, logging a fatal error if not,
   and aborts the process.
   Example: void *ptr; CHECK_NULL(L_MAIN, ptr = my_create()); */
#define CHECK_NULL(d, f) \
  do { \
    if ( (f) == NULL ) \
      log_fatal_null(d, #f, __LINE__); \
  } while(0)

/* Used by CHECK_*() macros */
void
log_fatal_err(int domain, const char *func, int line, int err) __attribute__((__noreturn__));

void
log_fatal_errno(int domain, const char *func, int line)        __attribute__((__noreturn__));

void
log_fatal_null(int domain, const char *func, int line)         __attribute__((__noreturn__));

#endif /* !__MISC_H__ */
