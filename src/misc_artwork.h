#ifndef MISC_ARTWORk_H
#define MISC_ARTWORk_H

#include <stdint.h>

/* utility functions to generate the URL as expected by httpd_artwork i/f
 * non null return if supplied id is unit32
 * caller responsible for free'ing returned buffer
 */

char*
artworkapi_url_byid(uint32_t dbmfi_id);

char*
artworkapi_url(const char* dbmfi_id);

#endif
