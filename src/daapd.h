/*
 * $Id$
 * Header info for daapd server
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

#ifndef _DAAPD_H_
#define _DAAPD_H_

typedef struct tag_songentry {
    int index;
    char *file;
    
    struct tag_songentry *next;
} SONGENTRY;

typedef struct tag_config {
    int use_mdns;
    int stop;
    char *configfile;
    char *web_root;
    int port;
    char *adminpassword;
    char *readpassword;
    char *mp3dir;
    char *servername;
    char *playlist;
    char *runas;
    char *dbdir;

    SONGENTRY songlist;
} CONFIG;

extern CONFIG config;

/* Forwards */
extern int drop_privs(char *user);

#endif /* _DAAPD_H_ */
