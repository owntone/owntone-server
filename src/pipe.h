
#ifndef __PIPE_H__
#define __PIPE_H__

#include <event2/buffer.h>
#include "db.h"

int
pipe_setup(struct media_file_info *mfi);

void
pipe_cleanup(void);

int
pipe_audio_get(struct evbuffer *evbuf, int wanted);

#endif /* !__PIPE_H__ */
