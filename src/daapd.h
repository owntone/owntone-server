/*
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

/**
 * \file daapd.h
 *
 * header file for main.c.  Why it isn't main.h, I don't know.
 * In fact...
 *
 * \todo make daapd.h into main.h
 */

#ifndef _DAAPD_H_
#define _DAAPD_H_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#define PATHSEP '/'
#define PATHSEP_STR "/"

#ifdef DEBUG
#  ifndef ASSERT
#    define ASSERT(f)         \
        if(f)                 \
            {}                \
        else                  \
            err_log(0,"Assert error in %s, line %d\n",__FILE__,__LINE__)
#  endif /* ndef ASSERT */
#else /* ndef DEBUG */
#  ifndef ASSERT
#    define ASSERT(f)
#  endif
#endif


/* Event bases */
extern struct event_base *evbase_main;

#endif /* _DAAPD_H_ */
