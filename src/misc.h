
#ifndef __MISC_H__
#define __MISC_H__

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdint.h>
#include <time.h>
#include <pthread.h>

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
unicode_fixup_string(char *str, const char *fromcode);

char *
trimwhitespace(const char *str);

uint32_t
djb_hash(const void *data, size_t len);

char *
b64_decode(const char *b64);

char *
b64_encode(const uint8_t *in, size_t len);

uint64_t
murmur_hash64(const void *key, int len, uint32_t seed);

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

/* mutex wrappers with checks */
void
fork_mutex_init(pthread_mutex_t *mutex);
void
fork_mutex_lock(pthread_mutex_t *mutex);
void
fork_mutex_unlock(pthread_mutex_t *mutex);
void
fork_mutex_destroy(pthread_mutex_t *mutex);

/* condition wrappers with checks */
void
fork_cond_init(pthread_cond_t *cond);
void
fork_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int
fork_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                    const struct timespec *ts);
void
fork_cond_signal(pthread_cond_t *cond);
void
fork_cond_destroy(pthread_cond_t *cond);

#endif /* !__MISC_H__ */
