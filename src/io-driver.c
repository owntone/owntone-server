
#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_SECURE_NO_DEPRECATE 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef WIN32
# include "getopt.h"
#endif

#include "io.h"

struct testinfo {
    char *name;
    int files;
    int restart;
    int (*handler)(void);
};

int test_readfile(void);
int test_readfile_timeout(void);
int test_servefile(void);
int test_udpserver(void);
int test_udpclient(void);
int test_buffer(void);

struct testinfo tests[] = {
    { "Read file, showing block size [uri to read]", 1, 0, test_readfile },
    { "Read file, with 10s timeout [uri to read]", 1, 0, test_readfile_timeout },
    { "Serve a file [listen://port] [uri to serve]", 2, 1, test_servefile },
    { "UDP echo server [udplisten://port]", 1, 0, test_udpserver },
    { "UDP echo client [udp://server:port]", 1, 0, test_udpclient },
    { "Buffered line read [uri]", 1, 0, test_buffer }
};

int debuglevel=1;
char *files[20];

int test_udpclient(void) {
    IOHANDLE ioclient;
    unsigned char buffer[256];
    uint32_t len;

    ioclient = io_new();
    if(!io_open(ioclient,files[0])) {
        printf("Can't open udp connection: %s\n", io_errstr(ioclient));
        return FALSE;
    }

    strncpy((char*)buffer,"foo",sizeof(buffer));
    len = (uint32_t)strlen((char*)buffer);
    if(!io_write(ioclient,buffer,&len)) {
        printf("Write error: %s\n",io_errstr(ioclient));
        return FALSE;
    }

    /* wait for return */
    len = sizeof(buffer);
    if(!io_read(ioclient, buffer, &len)) {
        printf("Read error: %s\n",io_errstr(ioclient));
        return FALSE;
    }

    buffer[len] = '\0';
    printf("Read %d bytes: %s\n",len,buffer);
    return TRUE;
}

int test_udpserver(void) {
    IOHANDLE ioserver;
    IO_WAITHANDLE iow;
    struct sockaddr_in si_from;
    socklen_t si_len;
    unsigned char buffer[256];
    uint32_t len;
    uint32_t timeout;

    ioserver = io_new();
    if(!io_open(ioserver,files[0])) {
        printf("Can't open listener: %s\n", io_errstr(ioserver));
        return FALSE;
    }

    si_len = sizeof(si_from);
    iow = io_wait_new();
    io_wait_add(iow, ioserver, IO_WAIT_READ | IO_WAIT_ERROR);

    timeout = 30000; /* 30 seconds */
    io_wait(iow, &timeout);

    if(timeout == 0) {
        printf("Timeout!\n");
        return TRUE;
    }
    
    if(io_wait_status(iow, ioserver) & IO_WAIT_ERROR) {
        printf("Error in ioserver socket\n");
        return FALSE;
    }
    
    len = sizeof(buffer);
    if(!io_udp_recvfrom(ioserver, buffer, &len, &si_from, &si_len)) {
        printf("Error in recvfrom: %s\n",io_errstr(ioserver));
        return FALSE;
    }
    
    buffer[len] = '\0';
    printf("Got %s\n",buffer);
    printf("Returning...\n");
    io_udp_sendto(ioserver,buffer,&len,&si_from,si_len);

    io_close(ioserver);
    io_dispose(ioserver);
    io_wait_dispose(iow);

    return TRUE;
}


int test_servefile(void) {
    IOHANDLE ioserver, ioclient, iofile;
    IO_WAITHANDLE iow;
    uint32_t timeout;
    unsigned char buffer[256];
    uint32_t len = sizeof(buffer);

    ioserver = io_new();
    if(!io_open(ioserver,files[0])) {
        printf("Can't open listener: %s\n",io_errstr(ioserver));
        return FALSE;
    }

    printf("Making new waiter...\n");
    iow = io_wait_new();
    printf("Adding io object to waiter\n");
    io_wait_add(iow, ioserver, IO_WAIT_READ | IO_WAIT_ERROR);

    timeout = 30000; /* 30 seconds */
    printf("Waiting...\n");
    while(io_wait(iow, &timeout)) {
        printf("Done waiting.\n");
        if(io_wait_status(iow, ioserver) & IO_WAIT_ERROR) {
            printf("Got a status of IO_WAIT_ERROR\n");
            break;
        }
        if(io_wait_status(iow, ioserver) & IO_WAIT_READ) {
            /* got a client */
            printf("Got a client...\n");
            iofile = io_new();
            if(!io_open(iofile,files[1])) {
                printf("Can't open file to serve: %s\n",io_errstr(iofile));
                io_dispose(iofile);
                io_dispose(ioserver);
                return FALSE;
            }
            printf("Opened %s to serve\n",files[1]);

            /* jet it out to the client */
            ioclient = io_new();
            io_listen_accept(ioserver,ioclient,NULL);

            printf("Got client socket\n");
            len = sizeof(buffer);
            while(io_read(iofile,buffer,&len) && len) {
                printf("Read %d bytes\n",len);
                if(!io_write(ioclient,buffer,&len)) {
                    printf("write error: %s\n",io_errstr(ioclient));
                }

                len = sizeof(buffer);
            }
            io_close(ioclient);
            io_dispose(ioclient);
            printf("Closing client connection\n");
            io_close(iofile);
            io_dispose(iofile);
            printf("Looping to wait again.\n");
        }
        timeout = 30000; /* 30 seconds */
    }

    printf("Wait failed: timeout: %d\n",timeout);

    if(!timeout) {
        printf("Timeout waiting for socket\n");
        return TRUE;
    }

    /* socket error? */
    printf("Socket error: %s\n",io_errstr(ioserver));
    return FALSE;
}

int test_readfile(void) {
    IOHANDLE ioh;
    unsigned char buffer[256];
    uint32_t len = sizeof(buffer);
    uint64_t file_len;

    ioh = io_new();
    if(ioh != INVALID_HANDLE) {
        if(!io_open(ioh,files[0])) {
            printf("Can't open file: %s\n",io_errstr(ioh));
            return FALSE;
        }
        io_size(ioh, &file_len);
        printf("File size: %ld bytes\n",file_len);
        while(io_read(ioh,buffer,&len) && len) {
            printf("Read %d bytes\n",len);
            len = sizeof(buffer);
        }
        if(!len) {
            printf("EOF!\n");
        } else {
            printf("Read error: %s\n",io_errstr(ioh));
        }
        io_close(ioh);
    } else {
        return FALSE;
    }

    return TRUE;
}

int test_buffer(void) {
    IOHANDLE ioh;
    unsigned char buffer[256];
    uint32_t len = sizeof(buffer);
    uint64_t file_len;
    int line=1;

    ioh = io_new();
    if(ioh != INVALID_HANDLE) {
        if(!io_open(ioh,files[0])) {
            printf("Can't open file: %s\n",io_errstr(ioh));
            return FALSE;
        }
        io_size(ioh, &file_len);
        io_buffer(ioh);

        printf("File size: %ld bytes\n",file_len);
        while(io_readline(ioh,buffer,&len) && len) {
            printf("Read %d bytes\n",len);
            len = sizeof(buffer);
            printf("Line %d: %s\n",line,buffer);
            line++;
        }
        if(!len) {
            printf("EOF!\n");
        } else {
            printf("Read error: %s\n",io_errstr(ioh));
        }
        io_close(ioh);
    } else {
        return FALSE;
    }

    return TRUE;
}

int test_readfile_timeout(void) {
    IOHANDLE ioh;
    unsigned char buffer[256];
    uint32_t len = sizeof(buffer);
    uint32_t timeout;

    ioh = io_new();
    if(ioh != INVALID_HANDLE) {
        if(!io_open(ioh,files[0])) {
            printf("Can't open file: %s\n",io_errstr(ioh));
            return FALSE;
        }
        timeout = 1000;
        while(io_read_timeout(ioh,buffer,&len,&timeout) && len) {
            printf("Read %d bytes\n",len);
            len = sizeof(buffer);
            timeout = 1000;
        }
        if(!len) {
            printf("EOF!\n");
        } else {
            if(!timeout) {
                printf("Timeout\n");
            } else {
                printf("Read error: %s\n",io_errstr(ioh));
            }
        }
        io_close(ioh);
    } else {
        return FALSE;
    }

    return TRUE;
}

void errhandler(int level, char *msg) {
    if(level <= debuglevel) {
        fprintf(stderr,"L%d: %s",level,msg);
    }

    if(!level) {
        fflush(stdout);
        exit(1);
    }
}

void usage(void) {
    int ntests;
    int test;

    fprintf(stderr,"io -t<n> [[file] [...]]\n\n");
    
    ntests = (sizeof(tests)/sizeof(struct testinfo));
    for(test=0; test < ntests; test++) {
        fprintf(stderr, "Test %02d: %s\n",test+1,tests[test].name);
    }
    
    exit(-1);
}

int main(int argc, char *argv[]) {
    int option;
    int test = 0;
    int nfiles=0;
    int retval;

    while((option = getopt(argc, argv, "t:d:")) != -1) {
        switch(option) {
        case 't':
            test = atoi(optarg);
            break;
        case 'd':
            debuglevel = atoi(optarg);
            break;
        }
    }

    if(!test || (test > (sizeof(tests)/sizeof(struct testinfo))))
        usage();

    test--;

    while((optind + nfiles) != argc) {
        files[nfiles] = argv[optind + nfiles];
        nfiles++;
    }

    if(tests[test].files != nfiles) {
        fprintf(stderr,"Test %d requres %d files, only got %d\n",
                test, tests[test].files, nfiles);
        exit(-1);
    }
    
    io_init();
    io_set_errhandler(errhandler);

    printf("Executing test: %s\n",tests[test].name);
    retval = tests[test].handler();
    io_deinit();

    return (retval == FALSE);
}
