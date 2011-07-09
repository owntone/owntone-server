
#ifndef __CONFFILE_H__
#define __CONFFILE_H__

#include <sys/types.h>

#include <confuse.h>

#define CONFFILE   CONFDIR "/forked-daapd.conf"

extern cfg_t *cfg;
extern uint64_t libhash;
extern uid_t runas_uid;
extern gid_t runas_gid;

int
conffile_load(char *file);

void
conffile_unload(void);

#endif /* !__CONFFILE_H__ */
