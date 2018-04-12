
#ifndef __FILESCANNER_H__
#define __FILESCANNER_H__

#include "db.h"


/* --------------------------- Actual scanners ---------------------------- */

int
scan_metadata_ffmpeg(const char *file, struct media_file_info *mfi);

void
scan_metadata_stream(const char *path, struct media_file_info *mfi);

void
scan_playlist(const char *file, time_t mtime, int dir_id);

void
scan_smartpl(const char *file, time_t mtime, int dir_id);

#ifdef ITUNES
void
scan_itunes_itml(const char *file, time_t mtime, int dir_id);
#endif


/* ------------  Common utility functions used by the scanners ------------ */

/* Returns a pointer to the filename part of path.
 *
 * @in path        the complete path
 * @return         pointer to filename
 */
const char *
filename_from_path(const char *path);

/* Returns path without file extension. Caller must free result.
 *
 * @in path        the complete path
 * @return         modified path
 */
char *
strip_extension(const char *path);

/* Iterate up a file path.
 *
 * Example of three calls where path is '/foo/bar/file.mp3', and starting with
 * current = NULL:
 *   ret = parent_dir(&current, path) -> ret = 0, current = /bar/file.mp3
 *   ret = parent_dir(&current, path) -> ret = 0, current = /foo/bar/file.mp3
 *   ret = parent_dir(&current, path) -> ret = -1, current = /foo/bar/file.mp3
 *
 * @in/out current if the input pointer is not a null pointer, it will be moved
 *                 one level up, otherwise it will be set to the point at the
 *                 file's directory
 * @in path        the complete path
 * @return         0 if parent dir found, otherwise -1
 */
int
parent_dir(const char **current, const char *path);

#endif /* !__FILESCANNER_H__ */
