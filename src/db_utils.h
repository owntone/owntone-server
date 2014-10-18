/*
 * db_utils.h
 *
 *  Created on: Oct 18, 2014
 *      Author: asiate
 */

#ifndef DB_UTILS_H_
#define DB_UTILS_H_

#include <sqlite3.h>

int
dbutils_blocking_step(sqlite3 *db_hdl, sqlite3_stmt *stmt);

int
dbutils_blocking_prepare_v2(sqlite3 *db_hdl, const char *query, int len, sqlite3_stmt **stmt, const char **end);

int
dbutils_exec(sqlite3 *db_hdl, const char *query, char **errmsg);

int
dbutils_pragma_get_cache_size(sqlite3 *db_hdl);

int
dbutils_pragma_set_cache_size(sqlite3 *db_hdl, int pages);

int
dbutils_pragma_get_page_size(sqlite3 *db_hdl);

int
dbutils_pragma_set_page_size(sqlite3 *db_hdl, int bytes);

char *
dbutils_pragma_set_journal_mode(sqlite3 *db_hdl, char *mode);

int
dbutils_pragma_get_synchronous(sqlite3 *db_hdl);

int
dbutils_pragma_set_synchronous(sqlite3 *db_hdl, int synchronous);


#endif /* DB_UTILS_H_ */
