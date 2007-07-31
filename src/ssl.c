/*
 * $Id: $
 * SSL Routines
 *
 * Copyright (C) 2006 Ron Pedde (rpedde@users.sourceforge.net)
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "daapd.h"
#include "err.h"
#include "webserver.h"
#include "wsprivate.h"

/* Globals */
static SSL_CTX *ws_ssl_ctx = NULL;
static char *ws_ssl_pass = NULL;

/* Forwards */
static void ws_ssl_print_error(int loglevel);
static int ws_ssl_pw_cb(char *buffer, int num, int rwflag, void *userdata);

/*
 * password callback for the passphrase on the priv key
 */
static int ws_ssl_pw_cb(char *buff, int num, int rwflag, void *userdata) {
    if(num < strlen(ws_ssl_pass) + 1)
        return 0;

    strcpy(buff,ws_ssl_pass);
    return (int) strlen(ws_ssl_pass);
}

/*
 * initialize ssl library
 */
int ws_ssl_init(char *keyfile, char *cert, char *password) {
    SSL_METHOD *meth;

    if(ws_ssl_ctx) {
        return TRUE;
    }

    SSL_library_init();
    SSL_load_error_strings();

    /* Create our context*/
    meth=SSLv23_method();
    ws_ssl_ctx=SSL_CTX_new(meth);

    /* Load our keys and certificates*/
    if(!(SSL_CTX_use_certificate_chain_file(ws_ssl_ctx,cert))) {
        DPRINTF(E_LOG,L_WS,"Can't read certificate file; ssl disabled\n");
        return FALSE;
    }

    ws_ssl_pass=password;
    SSL_CTX_set_default_passwd_cb(ws_ssl_ctx,ws_ssl_pw_cb);
    if(!(SSL_CTX_use_PrivateKey_file(ws_ssl_ctx,keyfile,SSL_FILETYPE_PEM))) {
        DPRINTF(E_LOG,L_WS,"Can't read key file; ssl disabled\n");
        return FALSE;
    }

    return TRUE;
}


/*
 * finish the ssl stuff
 */
void ws_ssl_deinit(void) {
    if(ws_ssl_ctx)
        SSL_CTX_free(ws_ssl_ctx);
}

/*
 * this gets called immediately after an accept from the
 * underlying socket.
 *
 * @returns 1 if handshake completed, 0 if the connection was terminated,
 * but normally, and -1 if there was an error
 */
int ws_ssl_sock_init(WS_CONNINFO *pwsc, int fd) {
    SSL *pssl;
    int err;

    if(pwsc->secure) {
        if(!pwsc->secure_storage) {
            pssl = SSL_new(ws_ssl_ctx);
            pwsc->secure_storage = pssl;
        }
        pssl = (SSL*) pwsc->secure_storage;
        SSL_set_fd(pssl,pwsc->fd);
        err = SSL_accept(pssl);

        if(err == -1) {
            ws_ssl_print_error(E_LOG);
        }

        return err;
    } else {
        return 1;
    }
}

/*
 * print any error associated with this thread
 */
void ws_ssl_print_error(int loglevel) {
    unsigned long err;
    char buffer[120];

    while((err = ERR_get_error())) {
        ERR_error_string(err,buffer);
        DPRINTF(E_LOG,loglevel,"%s\n",buffer);
    }
}

/*
 * write to ssl sock
 */


/*
 *
 */
void ws_ssl_shutdown(WS_CONNINFO *pwsc) {
    SSL *pssl;

    if((pwsc->secure) && (!pwsc->secure_storage)) {
        pssl = (SSL*)pwsc->secure_storage;
        SSL_shutdown(pssl);
        SSL_free(pssl);
        pwsc->secure_storage = NULL;
    }
    ws_socket_shutdown(pwsc);
}


/*
 *
 */
int ws_ssl_read(WS_CONNINFO *pwsc, unsigned char *buffer, int len) {
    SSL *pssl;
    int result;

    if((pwsc->secure) && (!pwsc->secure_storage)) {
        pssl = (SSL*)pwsc->secure_storage;
        result = SSL_read(pssl, buffer, len);
        if(len <= 0)
            ws_ssl_print_error(E_LOG);
    } else {
        result =  ws_socket_read(pwsc, buffer, len);
    }

    return result;
}

int ws_ssl_write(WS_CONNINFO *pwsc, unsigned char *buffer, int len) {
    SSL *pssl;
    int result;

    if((pwsc->secure) && (!pwsc->secure_storage)) {
        pssl = (SSL*)pwsc->secure_storage;
        result = SSL_write(pssl, buffer, len);
        if(len <= 0)
            ws_ssl_print_error(E_LOG);
    } else {
        result =  ws_socket_write(pwsc, buffer, len);
    }

    return result;
}

