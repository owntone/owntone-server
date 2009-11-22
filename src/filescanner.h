
#ifndef __FILESCANNER_H__
#define __FILESCANNER_H__

#include "db.h"

int
filescanner_init(void);

void
filescanner_deinit(void);

/* Actual scanners */
int
scan_metadata_ffmpeg(char *file, struct media_file_info *mfi);

int
scan_url_file(char *file, struct media_file_info *mfi);

void
scan_m3u_playlist(char *file);

#ifdef ITUNES
void
scan_itunes_itml(char *file);
#endif

#endif /* !__FILESCANNER_H__ */
