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

typedef struct tag_stats {
    time_t start_time;
    int songs_served;

    unsigned int gb_served;
    unsigned int bytes_served;
} STATS;

typedef struct tag_config {
    int use_mdns;
    int stop;
    int reload;
    char *configfile;
    char *web_root;
    int port;
    int rescan_interval;
    char *adminpassword;
    char *readpassword;
    char *mp3dir;
    char *servername;
    char *playlist;
    char *runas;
    char *dbdir;
    char *extensions;
    char *artfilename;
    char *logfile;
    STATS stats;
} CONFIG;

extern CONFIG config;

/* Forwards */
extern int drop_privs(char *user);

#endif /* _DAAPD_H_ */
