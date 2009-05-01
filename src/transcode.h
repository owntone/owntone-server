
#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

#include <evhttp.h>

struct transcode_ctx;

int
transcode(struct transcode_ctx *ctx, struct evbuffer *evbuf, int wanted);

struct transcode_ctx *
transcode_setup(struct media_file_info *mfi, size_t *est_size);

void
transcode_cleanup(struct transcode_ctx *ctx);

int
transcode_needed(struct evkeyvalq *headers, char *file_codectype);

#endif /* !__TRANSCODE_H__ */
