
#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

#include "evhttp/evhttp.h"

struct transcode_ctx;

int
transcode(struct transcode_ctx *ctx, struct evbuffer *evbuf, int wanted);

int
transcode_seek(struct transcode_ctx *ctx, int ms);

struct transcode_ctx *
transcode_setup(struct media_file_info *mfi, off_t *est_size, int wavhdr);

void
transcode_cleanup(struct transcode_ctx *ctx);

int
transcode_needed(struct evkeyvalq *headers, char *file_codectype);

#endif /* !__TRANSCODE_H__ */
