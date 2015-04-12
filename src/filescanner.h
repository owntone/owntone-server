
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

int
filescanner_init(void);

void
filescanner_deinit(void);

void
filescanner_process_media(char *path, time_t mtime, off_t size, int type, struct media_file_info *external_mfi);

/* Actual scanners */
int
scan_metadata_ffmpeg(char *file, struct media_file_info *mfi);

int
scan_metadata_icy(char *url, struct media_file_info *mfi);

void
scan_playlist(char *file, time_t mtime);

void
scan_smartpl(char *file, time_t mtime);

#ifdef ITUNES
void
scan_itunes_itml(char *file);
#endif

void
filescanner_trigger_initscan(void);

void
filescanner_trigger_fullrescan(void);

int
filescanner_scanning(void);

#endif /* !__FILESCANNER_H__ */
