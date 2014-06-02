
#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

#include <event.h>
#ifdef HAVE_LIBEVENT2
# include <event2/http.h>
#else
# include "evhttp/evhttp.h"
#endif

struct transcode_ctx;

int
transcode(struct transcode_ctx *ctx, struct evbuffer *evbuf, int wanted);

int
transcode_seek(struct transcode_ctx *ctx, int ms);

int
transcode_setup(struct transcode_ctx **nctx, struct media_file_info *mfi, off_t *est_size, int wavhdr);

void
transcode_cleanup(struct transcode_ctx *ctx);

int
transcode_needed(struct evkeyvalq *headers, char *file_codectype);

#endif /* !__TRANSCODE_H__ */
