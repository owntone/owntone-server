
#ifndef __ARTWORK_H__
#define __ARTWORK_H__

#define ART_FMT_PNG     1
#define ART_FMT_JPEG    2

#include <event2/buffer.h>

/*
 * Get the artwork image for an individual item (track)
 *
 * @out evbuf    Event buffer that will contain the (scaled) image
 * @in  id       The mfi item id
 * @in  max_w    Requested maximum image width (may not be obeyed)
 * @in  max_h    Requested maximum image height (may not be obeyed)
 * @return       ART_FMT_* on success, -1 on error or no artwork found
 */
int
artwork_get_item(struct evbuffer *evbuf, int id, int max_w, int max_h);

/*
 * Get the artwork image for a group (an album or an artist)
 *
 * @out evbuf    Event buffer that will contain the (scaled) image
 * @in  id       The group id (not the persistentid)
 * @in  max_w    Requested maximum image width (may not be obeyed)
 * @in  max_h    Requested maximum image height (may not be obeyed)
 * @return       ART_FMT_* on success, -1 on error or no artwork found
 */
int
artwork_get_group(struct evbuffer *evbuf, int id, int max_w, int max_h);

/*
 * Checks if the file is an artwork file (based on user config)
 *
 * @in  filename Name of the file
 * @return       1 if true, 0 if false
 */
int
artwork_file_is_artwork(const char *filename);

#endif /* !__ARTWORK_H__ */
