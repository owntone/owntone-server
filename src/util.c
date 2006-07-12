/*
 * simple utility functions
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

uint32_t util_djb_hash_block(unsigned char *data, uint32_t len) {
    uint32_t hash = 5381;
    unsigned char *pstr = data;

    while(len--) {
        hash = ((hash << 5) + hash) + *pstr;
        pstr++;
    }
    return hash;
}


uint32_t util_djb_hash_str(char *str) {
    uint32_t len;

    len = (uint32_t)strlen(str);
    return util_djb_hash_block((unsigned char *)str,len);
}

