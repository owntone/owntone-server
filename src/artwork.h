
#ifndef __ARTWORK_H__
#define __ARTWORK_H__

#define ART_FMT_PNG     1
#define ART_FMT_JPEG    2
#define ART_FMT_VP8     3

#define ART_DEFAULT_HEIGHT 600
#define ART_DEFAULT_WIDTH  600

#include <event2/buffer.h>
#include <stdbool.h>

/*
 * Get the artwork image for an individual item (track)
 *
 * @out evbuf    Event buffer that will contain the (scaled) image
 * @in  id       The mfi item id
 * @in  max_w    Requested maximum image width (may not be obeyed)
 * @in  max_h    Requested maximum image height (may not be obeyed)
 * @in  format   Requested format (may not be obeyed)
 * @return       ART_FMT_* on success, -1 on error or no artwork found
 */
int
artwork_get_item(struct evbuffer *evbuf, int id, int max_w, int max_h);
int
artwork_get_item2(struct evbuffer *evbuf, int id, int max_w, int max_h, int format);

/*
 * Get the artwork image for a group (an album or an artist)
 *
 * @out evbuf    Event buffer that will contain the (scaled) image
 * @in  id       The group id (not the persistentid)
 * @in  max_w    Requested maximum image width (may not be obeyed)
 * @in  max_h    Requested maximum image height (may not be obeyed)
 * @in  format   Requested format (may not be obeyed)
 * @return       ART_FMT_* on success, -1 on error or no artwork found
 */
int
artwork_get_group(struct evbuffer *evbuf, int id, int max_w, int max_h);
int
artwork_get_group2(struct evbuffer *evbuf, int id, int max_w, int max_h, int format);

/*
 * Checks if the file is an artwork file (based on user config)
 *
 * @in  filename Name of the file
 * @return       true/false
 */
bool
artwork_file_is_artwork(const char *filename);

/*
 * Checks if the path (or URL) has file extension that is recognized as a
 * supported file type (e.g. ".jpg"). Also supports URL-encoded paths, e.g.
 * http://foo.com/bar.jpg?something
 *
 * @in  path     Path to the file (can also be a URL)
 * @return       true/false
 */
bool
artwork_extension_is_artwork(const char *path);

#endif /* !__ARTWORK_H__ */
