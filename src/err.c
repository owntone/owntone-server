/*
 * $Id$
 * Generic error handling
 *
 * Copyright (C) 2003 Ron Pedde (ron@corbey.com)
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
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <stdlib.h>

#include "err.h"


int err_debuglevel=0;
int err_logdestination=LOGDEST_STDERR;

/****************************************************
 * log_err
 ****************************************************/
void log_err(int quit, char *fmt, ...)
{
    va_list ap;
    char errbuf[256];

    va_start(ap, fmt);
    vsnprintf(errbuf, sizeof(errbuf), fmt, ap);
    va_end(ap);

    switch(err_logdestination) {
    case LOGDEST_STDERR:
        fprintf(stderr, errbuf);
        break;
    case LOGDEST_SYSLOG:
        syslog(LOG_INFO, errbuf);
        break;
    }

    if(quit) {
        exit(EXIT_FAILURE);
    }
}

/****************************************************
 * log_setdest
 ****************************************************/
void log_setdest(char *app, int destination) {
    switch(destination) {
    case LOGDEST_SYSLOG:
	if(err_logdestination != LOGDEST_SYSLOG) {
	    openlog(app,LOG_PID,LOG_DAEMON);
	}
	break;
    case LOGDEST_STDERR:
	if(err_logdestination == LOGDEST_SYSLOG) {
	    /* close the syslog */
	    closelog();
	}
	break;
    }
}
