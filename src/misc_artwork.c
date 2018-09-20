#include "misc_artwork.h"

#include <stdlib.h>
#include <stdio.h>
#include "misc.h"

char*
artworkapi_url_byid(uint32_t dbmfi_id)
{
  char *url = NULL;

  /* max lengh based on "/artwork/track/4294967295", uint32 dbmfi->id
   */
  url = (char*)malloc(32);
  snprintf(url, 32, "/artwork/track/%u", dbmfi_id);

  return url;
}

char*
artworkapi_url(const char* dbmfi_id)
{
  uint32_t  tmp;
  return safe_atou32(dbmfi_id, &tmp) < 0 ? NULL : artworkapi_url_byid(tmp);
}

