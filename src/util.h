/*
 * simple utility functions
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#include <sys/types.h>

/* simple hashing functions */
extern uint32_t util_djb_hash_block(unsigned char *data, uint32_t len);
extern uint32_t util_djb_hash_str(char *str);

extern int util_must_exit(void);

#endif /* _UTIL_H_ */

