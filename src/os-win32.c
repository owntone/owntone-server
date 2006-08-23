/* $Id$
 *
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "daapd.h"
#include "win32.h"
#include "err.h"
#include "os-win32.h"
#include "plugin.h"
#include "w32-eventlog.h"
#include "w32-service.h"
#include "util.h"

/* Globals */
static WSADATA w32_wsadata;
static os_serviceflag = 0;
static pthread_t os_service_tid;
static os_initialized=0;
static pthread_mutex_t os_mutex=PTHREAD_MUTEX_INITIALIZER;

/* Forwards */
static void _os_socket_startup(void);
static void _os_socket_shutdown(void);
static int _os_sock_to_fd(SOCKET sock);
static void _os_lock(void);
static void _os_unlock(void);
static BOOL WINAPI _os_cancelhandler(DWORD dwCtrlType);
static void _os_phandle_dump(void);

extern int gettimeout(struct timeval end,struct timeval *timeoutp);

#define OSFI_OPEN     1
#define OSFI_SHUTDOWN 2

#define NOTSOCK (fd < MAXDESC)
#define REALSOCK (file_info[fd - MAXDESC].sock)
#define SOCKSTATE (file_info[fd - MAXDESC].state)

#define MAXBACKLOG    5

typedef struct tag_osfileinfo {
    SOCKET sock;
    int state;
} OSFILEINFO;

/* Globals */
OSFILEINFO file_info[MAXDESC];
char os_config_file[_MAX_PATH];
char *os_w32_socket_states[] = {
    "Closed/Unused",
    "Open/Listening",
    "Shutdown, not closed"
};

/* "official" os interface functions */

/**
 * initialize the os-specific stuff.  this would include
 * backgrounding (or starting as service), setting up
 * signal handlers (or ctrl-c handlers), etc
 *
 * @param background whether or not to start in background (service)
 * @param runas we'll ignore this, as it's a unix thang
 * @returns TRUE on success, FALSE otherwise
 */
int os_init(int foreground, char *runas) {
    int err;

    _os_socket_startup();
    
    if(!os_initialized) {
        _os_lock();
        if(!os_initialized) {
            memset((void*)&file_info,0,sizeof(file_info));
        }
        os_initialized=1;
        _os_unlock();
    }

    if(!foreground) {
        /* startup as service */
        os_serviceflag = 1;
        if((err=pthread_create(&os_service_tid,NULL,service_startup,NULL))) {
            DPRINTF(E_LOG,L_MISC,"Could not spawn thread: %s\n",strerror(err));
            return FALSE;
        }
    } else {
        /* let's set a ctrl-c handler! */
        SetConsoleCtrlHandler(_os_cancelhandler,TRUE);
    }
    return TRUE;
}

/**
 * dump pseudo-handles
 */
void _os_phandle_dump(void) {
    int fd;
    /* walk through and log the different sockets */
    fd=1; /* skip the main listen socket */
    while(fd < MAXDESC) {
        if(file_info[fd].state) {
            DPRINTF(E_LOG,L_MISC,"Socket %d (%d): State %s\n",fd,fd+MAXDESC,
                os_w32_socket_states[file_info[fd].state]);
        }
        fd++;
    }
}

/**
 * shutdown the system-specific stuff started in os_init.
 */
void os_deinit(void) {

    _os_socket_shutdown();

    _os_phandle_dump();

    if(os_serviceflag) {
        /* then we need to stop the service */
        SetConsoleCtrlHandler(_os_cancelhandler,FALSE);
        service_shutdown(0);
    }
}

/**
 * open the syslog (eventlog)
 */
int os_opensyslog(void) {
    elog_register();
    return elog_init();
}

/**
 * close the syslog (eventlog)
 */
int os_closesyslog(void) {
    return elog_deinit();
}

/**
 * write a message to the syslog
 *
 * @param level what level of message (1-10)
 * @param msg message to write
 * @return TRUE on success, FALSE otherwise
 */
int os_syslog(int level, char *msg) {
    return elog_message(level, msg);
}

/**
 * change the owner of a file to a specific user.  This is
 * ignored on windows
 */
extern int os_chown(char *path, char *user) {
    return TRUE;
}


int os_register(void) {
    service_register();
    elog_register();

    return TRUE;
}

int os_unregister(void) {
    service_unregister();
    elog_unregister();

    return TRUE;
}

static BOOL WINAPI _os_cancelhandler(DWORD dwCtrlType) {
    DPRINTF(E_LOG,L_MISC,"Shutting down with a console event\n");
    config.stop = 1;
    return TRUE;
}


int _os_sock_to_fd(SOCKET sock) {
    int fd;

    if(sock == INVALID_SOCKET)
        return -1;

    DPRINTF(E_DBG,L_MISC,"Converting socket to fd\n");

    /* I was doing strange osfhandle stuff here, but it seemed
     * to be leaking file handles, and I don't really know what
     * it was doing.  Thanks, Microsoft.  Anyway, since I'm handling
     * reads, writes, opens and closes, I might just as well hand out
     * FAKE fds and swap them out to SOCKETS when I need to.  No
     * more fd leaks.  Problem solved.
     */
/*
    fd=_open_osfhandle(sock,O_RDWR|O_BINARY);
//    fd=_open_osfhandle(sock,0);
    if(fd > 0) {
        file_info[fd].flags = OSFI_SOCKET | OSFI_OPEN;
        file_info[fd].sock=sock;
    } else {
        DPRINTF(E_LOG,L_MISC,"Could not fd for socket osfhandle\n");
    }
*/
    _os_lock();
    fd=0;
    while((fd < MAXDESC) && (file_info[fd].state)) {
        fd++;
    }

    if(fd == MAXDESC) {
        _os_unlock();
        _os_phandle_dump();
        DPRINTF(E_FATAL,L_MISC,"Out of pseudo file handles.  See ya\n");
    }

    DPRINTF(E_DBG,L_MISC,"Returning fd %d\n",fd + MAXDESC);
    file_info[fd].sock = sock;
    file_info[fd].state = OSFI_OPEN;
    _os_unlock();
    return fd + MAXDESC;
}

int os_acceptsocket(int fd, struct in_addr *hostaddr) {
    socklen_t len = sizeof(struct sockaddr);
    struct sockaddr_in netclient;
    SOCKET retval;

    DPRINTF(E_SPAM,L_MISC,"Accepting socket %d -- %d\n",fd,REALSOCK);

    if(NOTSOCK || (SOCKSTATE != OSFI_OPEN)) {
        DPRINTF(E_LOG,L_MISC,"Bad socket passed to accept\n");
        return -1;
    }

    while (((retval =
        accept(REALSOCK,(struct sockaddr *)(&netclient), &len)) == SOCKET_ERROR) &&
               (WSAGetLastError() == WSAEINTR));  

    if (retval == INVALID_SOCKET) {
        DPRINTF(E_LOG,L_MISC,"Error accepting...\n");
        return _os_sock_to_fd(retval);
    }
   
    *hostaddr = netclient.sin_addr;
    return _os_sock_to_fd(retval);
}

int os_waitfdtimed(int fd, struct timeval end) {
    fd_set readset;
    int retval;
    struct timeval timeout;
    SOCKET sock;

    DPRINTF(E_SPAM,L_MISC,"Timed wait on fd %d\n");
    if(NOTSOCK || (SOCKSTATE != OSFI_OPEN))
        return -1;

    sock = REALSOCK;
 
    /*
    if ((fd < 0) || (fd >= FD_SETSIZE)) {
        errno = EINVAL;
        return -1;
    } 
    */

    FD_ZERO(&readset);
    FD_SET(sock, &readset);
    if (gettimeout(end, &timeout) == -1)
        return -1;
    while (((retval = select(1, &readset, NULL, NULL, NULL)) == SOCKET_ERROR)
           && (WSAGetLastError() == WSAEINTR)) {
        if (gettimeout(end, &timeout) == -1)
            return -1;
        FD_ZERO(&readset);
        FD_SET(fd, &readset);
    }
    if (retval == 0) {
        errno = ETIME;
        return -1;
    }
    if (retval == -1)
        return -1;
    DPRINTF(E_SPAM,L_MISC,"Timed wait successful\n");
    return 0;
}

/* from the gnu c library */
char *os_strsep(char **stringp, const char *delim) {
    char *begin, *end;

    begin = *stringp;
    if (begin == NULL)
        return NULL;

    /* A frequent case is when the delimiter string contains only one
    character.  Here we don't need to call the expensive `strpbrk'
    function and instead work using `strchr'.  */
    if (delim[0] == '\0' || delim[1] == '\0') {
        char ch = delim[0];

        if (ch == '\0') {
            end = NULL;
        } else {
            if (*begin == ch)
                end = begin;
            else if (*begin == '\0')
                end = NULL;
            else
                end = strchr (begin + 1, ch);
        }
    } else {
        /* Find the end of the token.  */
        end = strpbrk (begin, delim);
    }

    if (end) {
        /* Terminate the token and set *STRINGP past NUL character.  */
        *end++ = '\0';
        *stringp = end;
    } else {
        /* No more delimiters; this is the last token.  */
        *stringp = NULL;
    }
    return begin;
}

int os_opensocket(unsigned short port) {
    int error;  
    struct sockaddr_in server;
    SOCKET sock;
    int true = 1;
    int fd;

    DPRINTF(E_SPAM,L_MISC,"Opening socket\n");
    /* make sure the file_info struct is initialized */
    if(!os_initialized) {
        _os_lock();
        if(!os_initialized) {
            memset((void*)&file_info,0,sizeof(file_info));
            os_initialized=1;
        }
        _os_unlock();
    }

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&true,
                  sizeof(true)) == SOCKET_ERROR) {
        error = WSAGetLastError();
        while ((closesocket(sock) == SOCKET_ERROR) && (WSAGetLastError() == WSAEINTR)); 
        errno = EINVAL; /* windows errnos suck */
        return -1;
    }
 
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons((short)port);
    if ((bind(sock, (struct sockaddr *)&server, sizeof(server)) == -1) ||
        (listen(sock, MAXBACKLOG) == -1)) {
        error = errno;
        while ((closesocket(sock) == SOCKET_ERROR) && (WSAGetLastError() == WSAEINTR)); 
        errno = WSAGetLastError();
        return -1;
    }

    fd=_os_sock_to_fd(sock);
    DPRINTF(E_SPAM,L_MISC,"created socket %d\n",fd);
    return fd;
}

int os_write(int fd, void *buffer, unsigned int count) {
    int retval;
    
    if(NOTSOCK) {
        retval = _write(fd,buffer,count);
    } else {
        if(SOCKSTATE != OSFI_OPEN) {
            DPRINTF(E_LOG,L_MISC,"Write to socket with status: %d\n",
                file_info[fd-MAXDESC].state);
            return -1;
        }
        retval=send(REALSOCK,buffer,count,0);
    }
    return retval;
}

int os_read(int fd,void *buffer,unsigned int count) {
    int retval;

//    DPRINTF(E_SPAM,L_MISC,"Reading %d bytes from %d\n",count,fd);
    if(NOTSOCK) {
        retval = _read(fd,buffer,count);
    } else {
        if(SOCKSTATE != OSFI_OPEN) {
            DPRINTF(E_LOG,L_MISC,"Read to socket with status: %d\n",
                file_info[fd-MAXDESC].state);
            return -1;
        }
        retval=recv(REALSOCK,buffer,count,0);
//        DPRINTF(E_SPAM,L_MISC,"Actually returning %d\n",retval);
    }
    return retval;
}

int os_shutdown(int fd, int how) {
    if(NOTSOCK || (SOCKSTATE != OSFI_OPEN))
        return -1;

    SOCKSTATE = OSFI_SHUTDOWN;
    shutdown(REALSOCK,how);
    return 0;
}


int os_close(int fd) {
    if(NOTSOCK) {
        _close(fd);
    } else { /* socket */
        if(SOCKSTATE == OSFI_OPEN) {
            os_shutdown(fd,SHUT_RDWR);
        }
        if(SOCKSTATE == OSFI_SHUTDOWN) {
            closesocket(REALSOCK);
            SOCKSTATE = 0;
        }
    } 
    return 0;
}


/**
 * get uid of current user.  this is really stubbed, as it's only used
 * as a check during startup (it fails normally if you run non-root, as it means
 * that it can't drop privs, can't write pidfile, etc)
 */
int os_getuid(void) {
    return 0;
}


int os_gettimeofday (struct timeval *tv, struct timezone* tz) {
    union {
        long long ns100; /*time since 1 Jan 1601 in 100ns units */
        FILETIME ft;
    } now;

    GetSystemTimeAsFileTime (&now.ft);
    tv->tv_usec = (long) ((now.ns100 / 10LL) % 1000000LL);
    tv->tv_sec = (long) ((now.ns100 - 116444736000000000LL) / 10000000LL);

    if(tz) {
        tz->tz_minuteswest = _timezone;
    }
    return (0);
} 


/**
 * initialize winsock 
 */
void _os_socket_startup(void) {
    WORD minver;
    int err;
 
    minver = MAKEWORD( 2, 2 );
 
    err = WSAStartup( minver, &w32_wsadata );
    if ( err != 0 ) {
        DPRINTF(E_FATAL,L_MISC,"Could not initialize winsock\n");
    }
}

/**
 * deinitialize winsock
 */
void _os_socket_shutdown(void) {
    WSACleanup();
}

/* COMPAT FUNCTIONS */


/* can't be worse then strerror */
char *os_strerror (int error_no) {
    static char buf[500];

    if (error_no == 0)
        error_no = GetLastError ();

    buf[0] = '\0';
    if (!FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, NULL,
                        error_no,
                        0, /* choose most suitable language */
                        buf, sizeof (buf), NULL))
    sprintf (buf, "w32 error %u", error_no);
    return buf;
}

/**
 * get the default config path.  there might be an argument to be made
 * for using the install path as determined by registry, but might
 * just be easiest to grab the directory the executable is running from
 *
 * @returns path to config file (from static buffer)
 */
char *os_configpath(void) {
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char working_dir[_MAX_PATH];

    GetModuleFileName(NULL,os_config_file,_MAX_PATH);
    _splitpath(os_config_file,drive,dir,NULL,NULL);
    _makepath(os_config_file,drive,dir,"mt-daapd","conf");
    _makepath(working_dir,drive,dir,NULL,NULL);

    if(_chdir(working_dir) == -1) {
        DPRINTF(E_LOG,L_MISC,"Could not chdir to %s... using c:\\\n",working_dir);
        if(_chdir("c:\\") == -1) {
            DPRINTF(E_FATAL,L_MISC,"Could not chdir to c:\\... aborting\n");
        }
    }
    DPRINTF(E_DBG,L_MISC,"Using config file %s\n",os_config_file);
    return os_config_file;
}

/**
 * get the path of the executable.  Caller must free.
 *
 */
char *os_apppath(char *junk) {
    char app_path[_MAX_PATH];

    GetModuleFileName(NULL,app_path,_MAX_PATH);
    return strdup(app_path);
}


/**
 * Determine if an address is local or not
 *
 * @param hostaddr the address to test for locality
 */
int os_islocaladdr(char *hostaddr) {
    char hostname[256];
    struct hostent *ht;
    int index;

    DPRINTF(E_DBG,L_MISC,"Checking if %s is local\n",hostaddr);
    if(strncmp(hostaddr,"127.",4) == 0)
        return TRUE;

    gethostname(hostname, sizeof(hostname));
    ht=gethostbyname(hostname);

    index=0;
    while(ht->h_addr_list[index] != NULL) {
/*
        if(memcmp(&hostaddr,h_addr_list[index],4) == 0)
            return TRUE;
*/
        if(strcmp(inet_ntoa(*(struct in_addr *)ht->h_addr_list[index]),hostaddr) == 0) {
            DPRINTF(E_DBG,L_MISC,"Yup!\n");
            return TRUE;
        }
        index++;
    }
    
    DPRINTF(E_DBG,L_MISC,"Nope!\n");
    return FALSE;
}


/**
 * Lock the mutex.  This is used for initialization stuff, among
 * other things (?)
 */
void _os_lock(void) {
    int err;

    if((err=pthread_mutex_lock(&os_mutex))) {
        DPRINTF(E_FATAL,L_MISC,"Cannot lock mutex\n");
    }
}

/**
 * Unlock the os mutex
 */
void _os_unlock(void) {
    int err;

    if((err=pthread_mutex_unlock(&os_mutex))) {
        DPRINTF(E_FATAL,L_MISC,"Cannot unlock mutex\n");
    }
}

/**
 * load a loadable library
 */
void *os_loadlib(char **pe, char *path) {
    void *retval;
    UINT old_mode;

    old_mode = SetErrorMode(SEM_NOOPENFILEERRORBOX);
    retval = (void*)LoadLibrary(path);
    if(!retval) {
        if(pe) *pe = strdup(os_strerror(0));
    }
    SetErrorMode(old_mode);
    return retval;
}

void *os_libfunc(char **pe, void *handle, char *function) {
    void *retval;

    retval = GetProcAddress((HMODULE)handle, function);
    if(!retval) {
        if(pe) *pe = strdup(os_strerror(0));
    }
    return retval;
}

int os_unload(void *handle) {
    FreeLibrary(handle);
    return TRUE;
}


