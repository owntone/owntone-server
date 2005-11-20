/*
 * $Id$
 * Implementation file for server side format conversion.
 *
 * Copyright (C) 2005 Timo J. Rinne (tri@iki.fi)
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

#ifndef _SCC_H_
#define _SCC_H_

#define SERVER_SIDE_CONVERT_SUFFIX ".-*-ssc-*-.wav"
#define SERVER_SIDE_CONVERT_DESCR  " (converted to WAV)"

extern int server_side_convert(char *codectype);
extern char *server_side_convert_path(char *path);
extern FILE *server_side_convert_open(char *path,
				      off_t offset,
				      unsigned long len_ms,
				      char *codectype);
extern void server_side_convert_close(FILE *f);

#endif /* _SCC_H_ */

