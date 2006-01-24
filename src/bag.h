/*
 * $Id$
 * Simple collection as linked lists
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

#ifndef _BAG_H_
#define _BAG_H_

#define BAG_TYPE_INT    0
#define BAG_TYPE_STRING 1
#define BAG_TYPE_BAG    2


#define BAG_E_SUCCESS   0
#define BAG_E_MALLOC    1


typedef struct _BAG *BAG_HANDLE;
typedef struct _BAGITEM *BAG_ITEMHANDLE;

extern int bag_create(BAG_HANDLE *bpp);
extern int bag_destroy(BAG_HANDLE bp);
extern int bag_add_item(BAG_HANDLE bp, void* vpval, int ival, int type);

#endif /* _BAG_H_ */

