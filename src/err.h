/*
 * $Id$
 * Error related routines
 *
 * Copyright (C) 2003 Ron Pedde (ron@corbey.com)
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

#ifndef __ERR_H__
#define __ERR_H__

#define LOGDEST_STDERR       0
#define LOGDEST_SYSLOG       1

#define ERR_DEBUG            9
#define ERR_INFO             5
#define ERR_WARN             2
#define ERR_FATAL            0

extern int err_debuglevel;
extern int err_logdestination;

extern void log_err(int quit, char *fmt, ...);
extern void log_setdest(char *app, int destination);

#ifdef DEBUG
#define DPRINTF(level, fmt, arg...) \
    { if((level) <= err_debuglevel) { log_err(0,"%s: ",__FILE__); log_err(0,fmt,##arg); }}
#else
#define DPRINTF(level, fmt, arg...)
#endif /* DEBUG */

#endif /* __ERR_H__ */
