
#ifndef __FILESCANNER_H__
#define __FILESCANNER_H__

#include "db.h"

int
filescanner_init(void);

void
filescanner_deinit(void);

void
process_media_file(char *file, time_t mtime, off_t size, int compilation, int url);

/* Actual scanners */
int
scan_metadata_ffmpeg(char *file, struct media_file_info *mfi);

int
scan_metadata_icy(char *url, struct media_file_info *mfi);

void
scan_m3u_playlist(char *file, time_t mtime);

#ifdef ITUNES
void
scan_itunes_itml(char *file);
#endif

#endif /* !__FILESCANNER_H__ */
