/*
 * $Id$
 * Error related routines
 *
 * Copyright (C) 2003 Ron Pedde (ron@pedde.com)
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

/**
 * \file err.h
 *
 * Header file for err.c
 */

#ifndef __ERR_H__
#define __ERR_H__

/** @anchor log_dests */
#define LOGDEST_STDERR       0  /**< Log to stderr */
#define LOGDEST_SYSLOG       1  /**< Log to syslog */
#define LOGDEST_LOGFILE      2  /**< Log to logfile */

/** @anchor log_levels */
#define ERR_EXCESSIVE        10  /**< Logorrhea! */
#define ERR_DEBUG            9   /**< Way too verbose */
#define ERR_INFO             5   /**< Good info, not too much spam */
#define ERR_WARN             2   /**< Reasonably important, but not enough to log */
#define ERR_LOG              1   /**< Something that should go in a log file */
#define ERR_FATAL            0   /**< Log and force an exit */

/** @anchor log_categories */
#define LOG_CONFIG           0x0001 /**< configfile.c */
#define LOG_WEBSERVER        0x0002 /**< webserver.c */
#define LOG_DATABASE         0x0004 /**< db-* */
#define LOG_SCAN             0x0008 /**< mp3-scanner.c */
#define LOG_QUERY            0x0010 /**< query.c */
#define LOG_INDEX            0x0020 /**< daap.c */
#define LOG_BROWSE           0x0040 /**< daap.c, query.c */
#define LOG_PLAYLIST         0x0080 /**< playlist.c, lexer.l, parser.y */


extern int err_debuglevel;

extern void log_err(int quit, char *fmt, ...);
extern void log_setdest(char *app, int destination);

/**
 * Print a debugging or log message
 */
#ifdef DEBUG
# define DPRINTF(level, fmt, arg...) \
    { log_err(level,"%s, %d: ",__FILE__,__LINE__); log_err(level,fmt,##arg); }
#else
# define DPRINTF(level, fmt, arg...) \
    { log_err(level,fmt,##arg); }
#endif

#ifdef DEBUG_MEMORY
# ifndef __IN_ERR__
#  define malloc(x) err_malloc(__FILE__,__LINE__,x)
#  define strdup(x) err_strdup(__FILE__,__LINE__,x)
#  define free(x) err_free(__FILE__,__LINE__,x)
# endif /* __IN_ERR__ */

# define MEMNOTIFY(x) err_notify(__FILE__,__LINE__,x)

extern void *err_malloc(char *file, int line, size_t size);
extern char *err_strdup(char *file, int line, const char *str);
extern void err_free(char *file, int line, void *ptr);
extern void err_notify(char *file, int line, void *ptr);
extern void err_leakcheck(void);
#else
/**
 * Notify the leak checking system of externally allocated memory.
 */
# define MEMNOTIFY(x)
#endif /* DEBUG_MEMORY */
#endif /* __ERR_H__ */
