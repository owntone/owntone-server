/*
 * $Id$
 *
 * Win32 compatibility stuff
 */

#ifndef _WIN32_H_
#define _WIN32_H_

#include <windows.h>
//#include <winsock2.h>
#include <io.h>
#include <stddef.h>
#include <fcntl.h>
#include <direct.h>

#include "os-win32.h"

#ifndef TRUE
# define TRUE 1
# define FALSE 0
#endif

/* Funtion fixups */
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define usleep Sleep
#define sleep(a) Sleep((a) * 1000)
#define read(a,b,c) os_read((a),(b),(int)(c))
#define write(a,b,c) os_write((a),(b),(int)(c))
#define strncasecmp strnicmp
#define strcasecmp stricmp
#define mkdir(a,b) _mkdir((a))
#define popen _popen
#define pclose _pclose
#define strtoll strtol 

#define realpath os_realpath
#define close os_close
#define strsep os_strsep
#define open os_open
#define waitfdtimed os_waitfdtimed

#define readdir_r os_readdir_r
#define closedir os_closedir
#define opendir os_opendir

#define getuid os_getuid
#define strerror os_strerror

/* override the uici stuff */
#define u_open os_opensocket
#define u_accept os_acceptsocket

/* privately implemented functions: @see os-win32.c */
#define gettimeofday os_gettimeofday

/* Type fixups */
#define mode_t int
#define ssize_t int
#define socklen_t int

/* Consts */
#define PIPE_BUF 256 /* What *should* this be on win32? */
#define MAXDESC 512 /* http://msdn.microsoft.com/en-us/library/kdfaxaay.aspx */
#define SHUT_RDWR 2
#define MAX_NAME_LEN _MAX_PATH
#define ETIME 101
#define PATH_MAX _MAX_PATH

#define HOST "unknown-windows-ick"
#define SERVICENAME "Multithreaded DAAP Server"

#define CONFFILE os_configpath()

extern char *os_configpath(void);

#endif /* _WIN32_H_ */

