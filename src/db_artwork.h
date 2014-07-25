
#ifndef __DB_ARTWORK_H__
#define __DB_ARTWORK_H__

#include <time.h>
#include <stddef.h>
#include <stdint.h>

#include <sqlite3.h>


/* Images */
int
db_artwork_add(int itemid, int groupid, int max_w, int max_h, int dataid);

int
db_artwork_file_add(int format, char *filename, int max_w, int max_h, char *data, int datalen);

int
db_artwork_get(int itemid, int groupid, int max_w, int max_h, int *cached, int *dataid);

int
db_artwork_file_get(int id, int *format, char **data, int *datalen);

int
db_artwork_perthread_init(void);

void
db_artwork_perthread_deinit(void);

int
db_artwork_init(void);

#endif /* !__DB_ARTWORK_H__ */
