/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
