
#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

#ifdef HAVE_LIBEVENT2
# include <event2/buffer.h>
#else
# include <event.h>
#endif
#include "http.h"

struct transcode_ctx;

int
transcode(struct transcode_ctx *ctx, struct evbuffer *evbuf, int wanted, int *icy_timer);

int
transcode_seek(struct transcode_ctx *ctx, int ms);

int
transcode_setup(struct transcode_ctx **nctx, struct media_file_info *mfi, off_t *est_size, int wavhdr);

void
transcode_cleanup(struct transcode_ctx *ctx);

int
transcode_needed(const char *user_agent, const char *client_codecs, char *file_codectype);

void
transcode_metadata(struct transcode_ctx *ctx, struct http_icy_metadata **metadata, int *changed);

void
transcode_metadata_artwork_url(struct transcode_ctx *ctx, char **artwork_url);

#endif /* !__TRANSCODE_H__ */
