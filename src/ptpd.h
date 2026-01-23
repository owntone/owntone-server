#ifndef __PTPD_H__
#define __PTPD_H__

uint64_t
ptpd_clock_id_get(void);

int
ptpd_slave_add(uint32_t *slave_id, const char *addr);

void
ptpd_slave_remove(uint32_t slave_id);

// Binds priviliged ports 319 and 320, so must be called before the server drops
// priviliges
int
ptpd_bind(void);

int
ptpd_init(uint64_t clock_id_seed);

void
ptpd_deinit(void);

#endif /* !__PTPD_H__ */
