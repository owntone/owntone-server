/*
 * $Id$
 *
 * The only platform I know without atoll is OSX 10.2, and it has
 * 32 bit inodes, so this will be a fine workaround until such time
 * as I move away from inodes as db index.
 *
 * Weak, I know.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#if !HAVE_ATOLL

long long atoll(const char *nptr) {
    return (long long)atol(nptr);
}

#endif /* !HAVE_ATOLL */
