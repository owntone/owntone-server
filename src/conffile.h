
#ifndef __CONFFILE_H__
#define __CONFFILE_H__

#include <confuse.h>

extern cfg_t *cfg;

int
conffile_load(char *file);

void
conffile_unload(void);

#endif /* !__CONFFILE_H__ */
