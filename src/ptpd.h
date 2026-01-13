#ifndef __PTPD_H__
#define __PTPD_H__

#include "misc.h"

struct ptpd_slave;

uint64_t
ptpd_clock_id_get(void);

// Returns slave id or negative on error
int
ptpd_slave_add(union net_sockaddr *naddr);

void
ptpd_slave_remove(int slave_id);

// Binds priviliged ports 319 and 320, so must be called before the server drops
// priviliges
int
ptpd_bind(void);

int
ptpd_init(uint64_t clock_id_seed);

void
ptpd_deinit(void);

#endif /* !__PTPD_H__ */
