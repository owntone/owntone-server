
#ifndef __CONFFILE_H__
#define __CONFFILE_H__

#include <confuse.h>

#define CONFFILE   CONFDIR "/forked-daapd.conf"

extern cfg_t *cfg;

int
conffile_load(char *file);

void
conffile_unload(void);

#endif /* !__CONFFILE_H__ */
