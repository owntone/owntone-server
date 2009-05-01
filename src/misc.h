
#ifndef __MISC_H__
#define __MISC_H__

#include <stdint.h>

int
safe_atoi(const char *str, int *val);

int
safe_atol(const char *str, long *val);

uint32_t
djb_hash(void *data, size_t len);

char *
b64_decode(const char *b64);

#endif /* !__MISC_H__ */
