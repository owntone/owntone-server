
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

#endif /* !__CONFFILE_H__ */
