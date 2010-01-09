
#ifndef __MISC_H__
#define __MISC_H__

#include <stdint.h>

int
safe_atoi(const char *str, int *val);

int
safe_atol(const char *str, long *val);

char *
m_realpath(const char *pathname);

uint32_t
djb_hash(void *data, size_t len);

char *
b64_decode(const char *b64);

uint64_t
murmur_hash64(const void *key, int len, uint32_t seed);

#endif /* !__MISC_H__ */
