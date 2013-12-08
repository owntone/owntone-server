
#ifndef __FILESCANNER_H__
#define __FILESCANNER_H__

#include "db.h"

#define F_SCAN_TYPE_PODCAST      (1 << 0)
#define F_SCAN_TYPE_AUDIOBOOK    (1 << 1)
#define F_SCAN_TYPE_COMPILATION  (1 << 2)
#define F_SCAN_TYPE_URL          (1 << 3)

int
filescanner_init(void);

void
filescanner_deinit(void);

struct extinf_ctx
{
  char *artist;
  char *title;
  int found;
};

void
process_media_file(char *file, time_t mtime, off_t size, int type, struct extinf_ctx *extinf);

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
