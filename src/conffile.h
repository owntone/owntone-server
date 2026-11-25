
#ifndef __CONFFILE_H__
#define __CONFFILE_H__

#include <sys/types.h>
#include <stdint.h>

#include <confuse.h>

#define CONFFILE   CONFDIR "/owntone.conf"

// Some shorthand macros for poor man's
#define CFG_NAME_UNKNOWN_TITLE (cfg_getstr(cfg_getsec(cfg, "library"), "name_unknown_title"))
#define CFG_NAME_UNKNOWN_ARTIST (cfg_getstr(cfg_getsec(cfg, "library"), "name_unknown_artist"))
#define CFG_NAME_UNKNOWN_ALBUM (cfg_getstr(cfg_getsec(cfg, "library"), "name_unknown_album"))
#define CFG_NAME_UNKNOWN_GENRE (cfg_getstr(cfg_getsec(cfg, "library"), "name_unknown_genre"))
#define CFG_NAME_UNKNOWN_COMPOSER (cfg_getstr(cfg_getsec(cfg, "library"), "name_unknown_composer"))

extern cfg_t *cfg;
extern uint64_t libhash;
extern uid_t runas_uid;
extern gid_t runas_gid;

int
conffile_load(char *file);

void
conffile_unload(void);

/* Applies any configured alias to a library path, e.g. "/srv/music/a/b.mp3"
 * becomes "/Music/a/b.mp3". Paths that are not below an aliased library
 * directory are copied unchanged. Returns -1 if buf is too small.
 */
int
conffile_alias_apply(char *buf, size_t buflen, const char *path);

/* Reverse of conffile_alias_apply(). Returns a newly allocated path, or NULL if
 * vpath does not start with a configured alias, in which case the caller should
 * use vpath as-is.
 */
char *
conffile_alias_resolve(const char *vpath);

/* Returns the alias configured for a library directory, or NULL if it has none.
 * The path must match the config file entry.
 */
const char *
conffile_alias_get(const char *path);

#endif /* !__CONFFILE_H__ */
