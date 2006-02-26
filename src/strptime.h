/******************************************************************************
**
**  Copyright (C) 2001  - the shmoo group -
**
**  This program is free software; you can redistribute it and/or
**  modify it, however, you cannot sell it.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**
**  You should have received a copy of the license attached to the
**  use of this software.  If not, visit www.shmoo.com/osiris for
**  details.
**
******************************************************************************/

/******************************************************************************
**
**    The Shmoo Group (TSG)
**
**    File:      strptime.h
**    Author:    Brian Wotring
**
**    Date:      June 22, 2001.
**    Project:   osiris
**
******************************************************************************/

#ifndef STRPTIME_H
#define STRPTIME_H

#ifndef HAVE_STRPTIME
char * strptime( char *buf, char *fmt, struct tm *tm );
#endif

#endif


