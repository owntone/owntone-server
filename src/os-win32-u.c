/**
 * Win32 os functions that require unicode
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "daapd.h"
#include "win32.h"
#include "err.h"
#include "os-win32.h"
#include "util.h"

/* opendir/closedir/readdir emulation taken from emacs. Thanks.  :) */
DIR *os_opendir(char *filename) {
    DIR *dirp;

    /* Opening is done by FindFirstFile.  However, a read is inherent to
    this operation, so we defer the open until read time.  */

    if (!(dirp = (DIR *) malloc (sizeof (DIR))))
        return NULL;
    
    dirp->dir_find_handle = INVALID_HANDLE_VALUE;
    dirp->dd_fd = 0;
    dirp->dd_loc = 0;
    dirp->dd_size = 0;

    strncpy (dirp->dir_pathname, filename,PATH_MAX);
    dirp->dir_pathname[PATH_MAX] = '\0';

    return dirp;
}

void os_closedir(DIR *dirp) {
    /* If we have a find-handle open, close it.  */
    if (dirp->dir_find_handle != INVALID_HANDLE_VALUE) {
        FindClose(dirp->dir_find_handle);
    }
    free((char *) dirp);
}


int os_readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result) {
    char filename[PATH_MAX + 1];
    WCHAR utf16[PATH_MAX + 1];
    int ln;

    if (dirp->dir_find_handle == INVALID_HANDLE_VALUE) {
        /* If we aren't dir_finding, do a find-first, otherwise do a find-next. */
        strncpy (filename, dirp->dir_pathname,PATH_MAX - 3);
        ln = (int) strlen (filename) - 1;
        if(filename[ln] != '\\')
            strcat (filename, "\\");
        strcat (filename, "*");

        /* filename is utf-8... let's convert to unicode */
        util_utf8toutf16((unsigned char *)&utf16,sizeof(utf16),filename,(int)strlen(filename));

        dirp->dir_find_handle = FindFirstFileW(utf16, &dirp->dir_find_data);

        if (dirp->dir_find_handle == INVALID_HANDLE_VALUE) {
            *result=NULL;
            return 2;
        }
    } else {
        if (!FindNextFileW(dirp->dir_find_handle, &dirp->dir_find_data)) {
            *result = NULL;
            return 0;
        }
    }

    /* Emacs never uses this value, so don't bother making it match
    value returned by stat().  */
    entry->d_ino = 1;

    memset(entry->d_name,0,MAXNAMLEN+1);
    util_utf16toutf8(entry->d_name,MAXNAMLEN+1,
        (unsigned char *)&dirp->dir_find_data.cFileName,
        (int)wcslen(dirp->dir_find_data.cFileName)*2);
    entry->d_namlen = (int) strlen (entry->d_name);

    entry->d_reclen = sizeof (struct dirent) - MAXNAMLEN + 3 +
        entry->d_namlen - entry->d_namlen % 4;

    entry->d_type = 0;
    if(dirp->dir_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        entry->d_type |= DT_DIR;
    } else if(dirp->dir_find_data.dwFileAttributes & FILE_ATTRIBUTE_NORMAL) {
        entry->d_type |= DT_REG;
    }

    /*
    if (dir_is_fat)
        _strlwr (dir_static.d_name);
    else if (!NILP (Vw32_downcase_file_names)) {
        register char *p;
        for (p = dir_static.d_name; *p; p++)
            if (*p >= 'a' && *p <= 'z')
                break;
        if (!*p)
            _strlwr (dir_static.d_name);
    }
    */
    *result = entry;
    return 0;
}

/**
 * this is now pretty close to a true realpath implementation
 */
char *os_realpath(const char *pathname, char *resolved_path) {
    char *ptr;
    WCHAR utf16_rel_path[PATH_MAX+1];
    WCHAR utf16_path[PATH_MAX+1];

    /* need to take the utf-8 and convert to utf-16, then _fullpath, then back */
    util_utf8toutf16((unsigned char *)&utf16_rel_path,PATH_MAX * sizeof(WCHAR),(char*)pathname,(int)strlen(pathname));
    if(!_wfullpath(utf16_path,utf16_rel_path,PATH_MAX)) {
        DPRINTF(E_FATAL,L_MISC,"Could not realpath %s\n",pathname);
    }
    util_utf16toutf8((unsigned char *)resolved_path,PATH_MAX,(unsigned char *)&utf16_path,
        util_utf16_byte_len((unsigned char *)utf16_path));

    ptr = resolved_path;
    while(*ptr) {
//        *ptr = tolower(*ptr);
        if(*ptr == '/')
            *ptr = '\\';
        ptr++;
    }

    while(resolved_path[strlen(resolved_path)-1] == '\\') {
        resolved_path[strlen(resolved_path)-1] = '\x0';
    }

    return &resolved_path[0];
}

int os_stat(const char *path, struct _stat *sb) {
    WCHAR utf16_path[PATH_MAX+1];

    memset(utf16_path,0,sizeof(utf16_path));
    util_utf8toutf16((unsigned char *)&utf16_path,PATH_MAX * 2,(char*)path,(int)strlen(path));

    return _wstat(utf16_path,sb);
}

/* FIXME: mode */
int os_open(const char *filename, int oflag) {
    WCHAR utf16_path[PATH_MAX+1];
    int fd;

    memset(utf16_path,0,sizeof(utf16_path));
    util_utf8toutf16((unsigned char *)&utf16_path,PATH_MAX * 2,(char*)filename,(int)strlen(filename));

    fd = _wopen(utf16_path, oflag | O_BINARY);
    return fd;
}

FILE *os_fopen(const char *filename, const char *mode) {
    WCHAR utf16_path[PATH_MAX+1];
    WCHAR utf16_mode[10];

    memset(utf16_path,0,sizeof(utf16_path));
    memset(utf16_mode,0,sizeof(utf16_mode));
    util_utf8toutf16((unsigned char *)&utf16_path,PATH_MAX * 2,(char*)filename,(int)strlen(filename));
    util_utf8toutf16((unsigned char *)&utf16_mode,10 * 2,(char*)mode,(int)strlen(mode));
    return _wfopen((wchar_t *)&utf16_path, (wchar_t *)&utf16_mode);
}


