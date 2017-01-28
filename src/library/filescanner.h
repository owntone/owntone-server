
#ifndef __FILESCANNER_H__
#define __FILESCANNER_H__

#include "db.h"

#define F_SCAN_TYPE_FILE         (1 << 0)
#define F_SCAN_TYPE_PODCAST      (1 << 1)
#define F_SCAN_TYPE_AUDIOBOOK    (1 << 2)
#define F_SCAN_TYPE_COMPILATION  (1 << 3)
#define F_SCAN_TYPE_URL          (1 << 4)
#define F_SCAN_TYPE_SPOTIFY      (1 << 5)
#define F_SCAN_TYPE_PIPE         (1 << 6)


/* Actual scanners */
int
scan_metadata_ffmpeg(const char *file, struct media_file_info *mfi);

void
scan_playlist(char *file, time_t mtime, int dir_id);

void
scan_smartpl(char *file, time_t mtime, int dir_id);

#ifdef ITUNES
void
scan_itunes_itml(char *file);
#endif

#endif /* !__FILESCANNER_H__ */
