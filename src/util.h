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

//extern char *util_utf16toutf8(unsigned char *utf16, int len);
//int util_utf8toutf16(unsigned char *utf16, size_t dlen, unsigned char *utf8, size_t slen);
//int util_utf16toutf8(unsigned char *utf8, size_t dlen, unsigned char *utf16, size_t slen);
//unsigned char *util_alloc_utf16toutf8(unsigned char *utf16, int slen);

extern unsigned char *util_utf8toutf16_alloc(unsigned char *utf8);
extern unsigned char *util_utf16touft8_alloc(unsigned char *utf16, int len);
extern int util_utf8toutf16_len(unsigned char *utf8);
extern int util_utf16toutf8_len(unsigned char *utf16, int len);
extern int util_utf8toutf16(unsigned char *utf16, int dlen, unsigned char *utf8, int len);
extern int util_utf16toutf8(unsigned char *utf8, int dlen, unsigned char *utf16, int len);
extern int util_utf16_byte_len(unsigned char *utf16);



#endif /* _UTIL_H_ */

