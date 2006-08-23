/*
 * $Id$
 *
 * os glue stuff, for functions that vary too greatly between
 * win32 and unix
 */

#ifndef _OS_WIN32_H_
#define _OS_WIN32_H_

#include "stdlib.h"

#define MAXNAMLEN 255
#define DIRBLKSIZ 512
#define PATHSEP '\\'
#define PATHSEP_STR "\\"

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

#define DT_DIR 1
#define DT_REG 2
#define DT_LNK 4

#define W_OK 2
#define R_OK 4

struct dirent {                         /* data from readdir() */

    long                d_ino;          /* inode number of entry */
    unsigned short      d_reclen;       /* length of this record */
    unsigned short      d_namlen;       /* length of string in d_name */
    int                 d_type;         /* flags */
    char                d_name[MAXNAMLEN+1];    /* name of file */
};

typedef struct {
    int dd_fd;                  /* file descriptor */
    int dd_loc;                 /* offset in block */
    int dd_size;                /* amount of valid data */
    char        dd_buf[DIRBLKSIZ];      /* directory block */
    HANDLE dir_find_handle;
    char   dir_pathname[PATH_MAX+1];
    WIN32_FIND_DATAW dir_find_data;
} DIR;  

/* win32-specific functions -- set up service, etc */
extern int os_register(void);
extern int os_unregister(void);
extern char *os_configpath(void);

/* replacements for socket functions */
extern int os_opensocket(unsigned short port);
extern int os_acceptsocket(int fd, struct in_addr *hostaddr);
extern int os_shutdown(int fd, int how);
extern int os_waitfdtimed(int fd, struct timeval end);
extern int os_close(int fd);
extern int os_open(const char *filename, int oflag);
extern FILE *os_fopen(const char *filename, const char *mode);

extern int os_read(int fd,void *buffer,unsigned int count);
extern int os_write(int fd, void *buffer, unsigned int count);
extern int os_getuid(void);

/* missing win32 functions */
extern char *os_strsep(char **stringp, const char *delim);
extern char *os_realpath(const char *pathname, char *resolved_path);
extern int os_gettimeofday (struct timeval *tv, struct timezone* tz);
extern int os_readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
extern void os_closedir(DIR *dirp);
extern DIR *os_opendir(char *filename);
extern char *os_strerror (int error_no);

#endif /* _OS_WIN32_H_ */
