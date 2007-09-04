/*
 * simple utility functions
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <pthread.h>

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

# include <iconv.h>

#include "daapd.h"
#include "err.h"
#include "util.h"


/* Globals */
pthread_mutex_t util_locks[(int)l_last];
pthread_mutex_t util_mutex = PTHREAD_MUTEX_INITIALIZER;
int _util_initialized=0;


/* Forwards */
void _util_mutex_init(void);

/**
 * Simple hash generator
 */
uint32_t util_djb_hash_block(unsigned char *data, uint32_t len) {
    uint32_t hash = 5381;
    unsigned char *pstr = data;

    while(len--) {
        hash = ((hash << 5) + hash) + *pstr;
        pstr++;
    }
    return hash;
}

/**
 * simple hash generator
 */
uint32_t util_djb_hash_str(char *str) {
    uint32_t len;

    len = (uint32_t)strlen(str);
    return util_djb_hash_block((unsigned char *)str,len);
}

/**
 * Dumb utility function that should probably be somehwere else
 */
int util_must_exit(void) {
    return config.stop;
}

int util_utf8toutf16(unsigned char *utf16, int dlen, unsigned char *utf8, int slen) {
    int result;
    DPRINTF(E_DBG,L_MISC,"Converting %s to utf-16le (slen=%d, dlen=%d)\n",utf8,slen,dlen);

    result=util_xtoy(utf16, dlen, utf8, slen, "UTF-8","UTF-16LE");
    DPRINTF(E_DBG,L_MISC,"Result: %d\n",result);
//    _util_hexdump(utf16,32);
    return result;
}

int util_utf16toutf8(unsigned char *utf8, int dlen, unsigned char *utf16, int slen) {
    int result;

    DPRINTF(E_DBG,L_MISC,"Converting *something* to utf-8 (slen=%d, dlen=%d)\n",slen,dlen);
//    _util_hexdump(utf16,32);
    result = util_xtoy(utf8, dlen, utf16, slen, "UTF-16LE","UTF-8");

    DPRINTF(E_DBG,L_MISC,"Converted to %s\n",utf8);

    return result;
}

int util_utf16_byte_len(unsigned char *utf16) {
    unsigned char *src = utf16;
    int len = 0;

    while(1) {
        if((src[0] == 0) && (src[1]==0))
            return len;
        len += 2;
        src += 2;
    }
  return len; /* ?? */
}


unsigned char *util_utf8toutf16_alloc(unsigned char *utf8) {
    unsigned char *utf16;

    utf16 = calloc(1,strlen((char*)utf8) * 4 + 1);
    if(util_xtoy(utf16,strlen((char*)utf8) * 4 + 1, utf8, strlen((char*)utf8),"UTF-8","UTF-16LE")) {
        return utf16;
    }

    free(utf16);
    return NULL;
}

unsigned char *util_utf16toutf8_alloc(unsigned char *utf16, int slen) {
    unsigned char *utf8;

    utf8=calloc(1, slen * 2 + 1);
    if(util_xtoy(utf8,slen * 2 + 1,utf16,slen,"UTF-16LE","UTF-8")) {
        return utf8;
    }

    free(utf8);
    return NULL;
}

unsigned char *util_xtoutf8_alloc(unsigned char *x, int slen, char *from) {
    unsigned char *utf8;

    utf8 = calloc(1, slen * 4 + 1);
    if(util_xtoy(utf8,slen * 4 + 1, x, slen, from, "UTF-8")) {
        return utf8;
    }
    free(utf8);
    return NULL;
}

int util_xtoy(unsigned char *dbuffer, int dlen, unsigned char *sbuffer, int slen, char *from, char *to) {
    iconv_t iv;
    size_t csize;

    /* type punning warnings */
    size_t st_dlen = dlen;
    size_t st_slen = slen;
    char *st_dbuffer = dbuffer;
    ICONV_CONST char *st_sbuffer = sbuffer;

    memset(dbuffer,0,dlen);

    iv=iconv_open(to,from);
    if(iv == (iconv_t)-1) {
        DPRINTF(E_LOG,L_MISC,"iconv error: iconv_open failed with %d\n",errno);
    }

    csize = iconv(iv,&st_sbuffer,&st_slen,
                  &st_dbuffer,&st_dlen);
    if(csize == (size_t)-1) {
        switch(errno) {
            case EILSEQ:
                DPRINTF(E_LOG,L_MISC,"iconv error: Invalid multibyte sequence\n");
                break;
            case EINVAL:
                DPRINTF(E_LOG,L_MISC,"iconv error: Incomplete multibyte sequence\n");
                break;
            case E2BIG:
                DPRINTF(E_LOG,L_MISC,"iconv error: Insufficient buffer size\n");
                break;
            default:
                DPRINTF(E_LOG,L_MISC,"iconv error: unknown (%d)\n",errno);
                break;
        }
    }
    iconv_close(iv);

    return (csize != (size_t)-1);
}

void util_hexdump(unsigned char *block, int len) {
    char charmap[256];
    int index;
    int row, offset;
    char output[80];
    char tmp[20];

    memset(charmap,'.',sizeof(charmap));

    for(index=' ';index<'~';index++) charmap[index]=index;
    for(row=0;row<(len+15)/16;row++) {
        sprintf(output,"%04X: ",row*16);
        for(offset=0; offset < 16; offset++) {
            if(row * 16 + offset < len) {
                sprintf(tmp,"%02X ",block[row*16 + offset]);
            } else {
                sprintf(tmp,"   ");
            }
            strcat(output,tmp);
        }

        for(offset=0; offset < 16; offset++) {
            if(row * 16 + offset < len) {
                sprintf(tmp,"%c",charmap[block[row*16 + offset]]);
            } else {
                sprintf(tmp," ");
            }
            strcat(output,tmp);
        }

        DPRINTF(E_LOG,L_MISC,"%s\n",output);
    }
}

/**
 * simple mutex wrapper for better debugging
 */
void util_mutex_lock(ff_lock_t which) {
    if(!_util_initialized)
        _util_mutex_init();

    if(pthread_mutex_lock(&util_locks[(int)which])) {
        fprintf(stderr,"Cannot lock mutex\n");
        exit(-1);
    }
}

/**
 * simple mutex wrapper for better debugging
 */
void util_mutex_unlock(ff_lock_t which) {
    if(pthread_mutex_unlock(&util_locks[(int)which])) {
        fprintf(stderr,"Cannot unlock mutex\n");
        exit(-1);
    }

}

/**
 * mutex initializer.  This might should be done from the
 * main thread.
 */
void _util_mutex_init(void) {
    int err;
    ff_lock_t lock;

    if((err = pthread_mutex_lock(&util_mutex))) {
        fprintf(stderr,"Error locking mutex\n");
        exit(-1);
    }

    if(!_util_initialized) {
        /* now, walk through and manually initialize the mutexes */
        for(lock=(ff_lock_t)0; lock < l_last; lock++) {
            if((err = pthread_mutex_init(&util_locks[(int)lock],NULL))) {
                fprintf(stderr,"Error initializing mutex\n");
                exit(-1);
            }
        }
        _util_initialized=1;
    }

    pthread_mutex_unlock(&util_mutex);
}

/**
 * split a string on delimiter boundaries, filling
 * a string-pointer array.
 *
 * The user must free both the first element in the array,
 * and the array itself.
 *
 * @param s string to split
 * @param delimiters boundaries to split on
 * @param argvp an argv array to be filled
 * @returns number of tokens
 */
int util_split(char *s, char *delimiters, char ***argvp) {
    int i;
    int numtokens;
    const char *snew;
    char *t;
    char *tokptr;
    char *tmp;
    char *fix_src, *fix_dst;

    if ((s == NULL) || (delimiters == NULL) || (argvp == NULL))
        return -1;
    *argvp = NULL;
    snew = s + strspn(s, delimiters);
    if ((t = malloc(strlen(snew) + 1)) == NULL)
        return -1;

    strcpy(t, snew);
    numtokens = 1;
    tokptr = NULL;
    tmp = t;

    tmp = s;
    while(*tmp) {
        if(strchr(delimiters,*tmp) && (*(tmp+1) == *tmp)) {
            tmp += 2;
        } else if(strchr(delimiters,*tmp)) {
            numtokens++;
            tmp++;
        } else {
            tmp++;
        }
    }

    DPRINTF(E_DBG,L_CONF,"Found %d tokens in %s\n",numtokens,s);

    if ((*argvp = malloc((numtokens + 1)*sizeof(char *))) == NULL) {
        free(t);
        return -1;
    }

    if (numtokens == 0)
        free(t);
    else {
        tokptr = t;
        tmp = t;
        for (i = 0; i < numtokens; i++) {
            while(*tmp) {
                if(strchr(delimiters,*tmp) && (*(tmp+1) != *tmp))
                    break;
                if(strchr(delimiters,*tmp)) {
                    tmp += 2;
                } else {
                    tmp++;
                }
            }
            *tmp = '\0';
            tmp++;
            (*argvp)[i] = tokptr;

            fix_src = fix_dst = tokptr;
            while(*fix_src) {
                if(strchr(delimiters,*fix_src) && (*(fix_src+1) == *fix_src)) {
                    fix_src++;
                }
                *fix_dst++ = *fix_src++;
            }
            *fix_dst = '\0';

            tokptr = tmp;
            DPRINTF(E_DBG,L_CONF,"Token %d: %s\n",i+1,(*argvp)[i]);
        }
    }

    *((*argvp) + numtokens) = NULL;
    return numtokens;
}

/**
 * dispose of the argv set that was created in util_split
 *
 * @param argv string array to delete
 */
void util_dispose_split(char **argv) {
    if(!argv)
        return;

    if(argv[0])
        free(argv[0]);

    free(argv);
}

/**
 * Write a formatted string to an allocated string.  Leverage
 * the existing util_vasprintf to do so
 */
char *util_asprintf(char *fmt, ...) {
    char *outbuf;
    va_list ap;

    ASSERT(fmt);

    if(!fmt)
        return NULL;

    va_start(ap,fmt);
    outbuf = util_vasprintf(fmt, ap);
    va_end(ap);

    return outbuf;
}

/**
 * Write a formatted string to an allocated string.  This deals with
 * versions of vsnprintf that return either the C99 way, or the pre-C99
 * way, by increasing the buffer until it works.
 *
 * @param
 * @param fmt format string of print (compatible with printf(2))
 * @returns TRUE on success
 */

#ifdef HAVE_VA_COPY
# define VA_COPY(a,b) va_copy((a),(b))
#else
# ifdef HAVE___VA_COPY
#  define VA_COPY(a,b) __va_copy((a),(b))
# else
#  define VA_COPY(a,b) memcpy((a),(b),sizeof(b))
# endif
#endif

char *util_vasprintf(char *fmt, va_list ap) {
    char *outbuf;
    char *newbuf;
    va_list ap2;
    int size=200;
    int new_size;

    outbuf = (char*)malloc(size);
    if(!outbuf)
        DPRINTF(E_FATAL,L_MISC,"Could not allocate buffer in vasprintf\n");

    ap2=ap; /* shut up lint warnings */
    VA_COPY(ap2,ap);

    while(1) {
        new_size=vsnprintf(outbuf,size,fmt,ap);

        if(new_size > -1 && new_size < size)
            break;

        if(new_size > -1)
            size = new_size + 1;
        else
            size *= 2;

        if((newbuf = realloc(outbuf,size)) == NULL) {
            free(outbuf);
            DPRINTF(E_FATAL,L_MISC,"malloc error in vasprintf\n");
            exit(1);
        }
        outbuf = newbuf;
        VA_COPY(ap,ap2);
    }

    return outbuf;
}


#ifdef DEBUG_MEM
void *util_malloc(char *file, char *line, size_t size);
void *util_calloc(char *file, char *line, size_t count, size_t size);
void *util_realloc(char *file, char *line, void *ptr, size_t size);
void util_free(void *ptr);
#endif
