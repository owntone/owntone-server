/*
 * $Id$
 * Helper functions for formatting a daap message
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "daap-proto.h"
#include "err.h"
#include "restart.h"

/* Forwards */
DAAP_BLOCK *daap_get_new(void);
DAAP_BLOCK *daap_add_formatted(DAAP_BLOCK *parent, char *tag, 
			       int len, char *value);
int daap_serialmem(DAAP_BLOCK *root, char *where);
int daap_compress(char *input, long in_len, char *output, long *out_len);


/*
 * daap_get_new
 *
 * Initialize a new daap struct
 */
DAAP_BLOCK *daap_get_new(void) {
    DAAP_BLOCK *pnew;

    pnew=(DAAP_BLOCK*)malloc(sizeof(DAAP_BLOCK));
    if(!pnew) {
	DPRINTF(ERR_WARN,"Error mallocing a daap block\n");
	return NULL;
    }

    pnew->value=NULL;
    pnew->parent=NULL;
    pnew->children=NULL;
    pnew->next=NULL;

    return pnew;
}

/*
 * daap_add_formatted
 *
 * add a block exactly as formatted in value.
 *
 * Note that value WILL be freed later in daap_free, so
 * the value paramater must have been malloced
 */
DAAP_BLOCK *daap_add_formatted(DAAP_BLOCK *parent, char *tag, 
			       int size, char *value) {
    DAAP_BLOCK *current,*last;
    DAAP_BLOCK *pnew;

    DPRINTF(ERR_DEBUG,"Adding daap tag %s\n",tag);
    pnew = daap_get_new();
    if(!pnew)
	return NULL;

    pnew->reported_size=size;
    pnew->parent=parent;
    pnew->size=size;
    memcpy(pnew->tag,tag,4);

    if((size <= 4) && (size > 0)) { /* we can just put it in svalue */
	memcpy(pnew->svalue,value,size);
	pnew->free=0;
    } else {
	pnew->value=value;
	pnew->free=1;
    }
    pnew->next=NULL;

    /* walk the child list and put it at the end */
    if(parent) {
	current=last=parent->children;
	while(current) {
	    last=current;
	    current=current->next;
	}
	
	if(last) { /* some there already */
	    last->next=pnew;
	} else {
	    parent->children=pnew;
	}
    }

    /* now, walk the chain and update sizes */
    current=pnew->parent;
    while(current) {
	current->reported_size += (8 + pnew->reported_size);
	current=current->parent;
    }

    return pnew;
}

/*
 * daap_add_int
 *
 * Add an int block to a specific parent
 */
DAAP_BLOCK *daap_add_long(DAAP_BLOCK *parent, char *tag, int v1, int v2) {
    char *ivalue;
    ivalue=(char*)malloc(8);
    if(!ivalue)
	return NULL;

    ivalue[0]=(v1 >> 24) & 0xFF;
    ivalue[1]=(v1 >> 16) & 0xFF;
    ivalue[2]=(v1 >> 8) & 0xFF;
    ivalue[3]=v1 & 0xFF;

    ivalue[4]=(v1 >> 24) & 0xFF;
    ivalue[5]=(v1 >> 16) & 0xFF;
    ivalue[6]=(v1 >> 8) & 0xFF;
    ivalue[7]=v1 & 0xFF;

    return daap_add_formatted(parent,tag,8,ivalue);
}

/*
 * daap_add_int
 *
 * Add an int block to a specific parent
 */
DAAP_BLOCK *daap_add_int(DAAP_BLOCK *parent, char *tag, int value) {
    char ivalue[4];

    ivalue[0]=(value >> 24) & 0xFF;
    ivalue[1]=(value >> 16) & 0xFF;
    ivalue[2]=(value >> 8) & 0xFF;
    ivalue[3]=value & 0xFF;

    return daap_add_formatted(parent,tag,4,ivalue);
}

/*
 * daap_add_short
 *
 * Add an int block to a specific parent
 */
DAAP_BLOCK *daap_add_short(DAAP_BLOCK *parent, char *tag, short int value) {
    char ivalue[2];

    ivalue[0]=(value >> 8) & 0xFF;
    ivalue[1]=value & 0xFF;

    return daap_add_formatted(parent,tag,2,ivalue);
}

/* 
 * daap_add_char
 *
 * Add a single char
 */
DAAP_BLOCK *daap_add_char(DAAP_BLOCK *parent, char *tag, char value) {
    return daap_add_formatted(parent,tag,1,&value);
}

/*
 * daap_add_data
 *
 * Add unstructured data to a specific parent
 */
DAAP_BLOCK *daap_add_data(DAAP_BLOCK *parent, char *tag, 
			  int len, void *value) {
    void *pvalue;

    if(len > 4) {
	pvalue=(void*)malloc(len);
	if(!pvalue)
	    return NULL;

	memcpy(pvalue,value,len);
    
	return daap_add_formatted(parent,tag,len,pvalue);
    } 
    return daap_add_formatted(parent,tag,len,value);
}

/*
 * daap_add_string
 *
 * Add a string element to a specific parent
 */
DAAP_BLOCK *daap_add_string(DAAP_BLOCK *parent, char *tag, char *value) {
    char *newvalue;

    if(value) {
	if(strlen(value) > 4) {
	    newvalue=strdup(value);
	    
	    if(!newvalue)
		return NULL;
	    
	    return daap_add_formatted(parent,tag,strlen(newvalue),newvalue);
	} 
	return daap_add_formatted(parent,tag,strlen(value),value);
    }
    return daap_add_formatted(parent,tag,0,"");
}

/*
 * daap_add_empty
 *
 * add a tag whose only value is to act as an aggregator
 */
DAAP_BLOCK *daap_add_empty(DAAP_BLOCK *parent, char *tag) {
    return daap_add_formatted(parent,tag,0,NULL);
}

/*
 * daap_serialmem
 *
 * Serialize a daap tree to memory, so it can be
 * gzipped
 */
int daap_serialmem(DAAP_BLOCK *root, char *where) {
    DAAP_BLOCK *current;
    char size[4];

    if(!root)
	return 0;

    DPRINTF(ERR_DEBUG,"Serializing %c%c%c%c\n",root->tag[0],root->tag[1],
	    root->tag[2],root->tag[3]);


    memcpy(where,root->tag,4);
    where+=4;

    size[0] = (root->reported_size >> 24) & 0xFF;
    size[1] = (root->reported_size >> 16) & 0xFF;
    size[2] = (root->reported_size >> 8 ) & 0xFF;
    size[3] = (root->reported_size) & 0xFF;

    memcpy(where,&size,4);
    where+=4;

    if(root->size) {
	if(root->free)
	    memcpy(where,root->value,root->size);
	else
	    memcpy(where,root->svalue,root->size);

	where+=root->size;
    }

    if(daap_serialmem(root->children,where))
	return -1;

    if(daap_serialmem(root->next,where))
	return -1;

    return 0;
}


/*
 * daap_serialize
 *
 * Throw the whole daap structure out a fd (depth first),
 * gzipped
 *
 * FIXME: this is gross.  clean this up
 */
int daap_serialize(DAAP_BLOCK *root, int fd, int gzip) {
    char *uncompressed;
    long uncompressed_len;
    char *compressed;
    long compressed_len;
    int err;

    uncompressed_len = root->reported_size + 8;
    uncompressed=(char*)malloc(uncompressed_len);
    if(!uncompressed) {
	DPRINTF(ERR_INFO,"Error allocating serialization block\n");
	return -1;
    }

    daap_serialmem(root,uncompressed);

    if(gzip) {
	/* guarantee enough buffer space */
	compressed_len = uncompressed_len * 101/100 + 12;
	compressed=(char*)malloc(compressed_len);
	if(!compressed) {
	    DPRINTF(ERR_INFO,"Error allocation compression block\n");
	    free(uncompressed);
	    return -1;
	}
	
	/*
	err=daap_compress(uncompressed,uncompressed_len,
			  compressed, &compressed_len);
	*/

	if(err) {
	    DPRINTF(ERR_INFO,"Error compressing: %s\n",strerror(errno));
	    free(uncompressed);
	    free(compressed);
	    return -1;
	}

	if(r_write(fd,compressed,compressed_len) != compressed_len) {
	    DPRINTF(ERR_INFO,"Error writing compressed daap stream\n");
	    free(uncompressed);
	    free(compressed);
	    return -1;
	}
	free(compressed);
	free(uncompressed);
    } else {
	if(r_write(fd,uncompressed,uncompressed_len) != uncompressed_len) {
	    DPRINTF(ERR_INFO,"Error writing uncompressed daap stream\n");
	    free(uncompressed);
	    return -1;
	}
	free(uncompressed);
    }
    return 0;
}

/*
 * daap_compress
 *
 * The zlib library is documented as threadsafe so long as
 * the zalloc and zfree routines are implemented reentrantly.
 *
 * I have no idea what platforms this will be ported to,
 * and even though I do not believe the functions I am using
 * will call zalloc or zfree, I am going to put this function
 * in a critical section.  Someone with more knowledge of zlib
 * than I can determine if it is really necessary.
 *
 * This doesn't actually do gzip encoding -- it does a full
 * gzip-style file, including header info.  This is not what
 * we want
 */

/*
int daap_compress(char *input, long in_len, char *output, long *out_len) {
    int err;

    err=compress(output,out_len,input,in_len);
    switch(err) {
    case Z_OK:
	break;
    case Z_MEM_ERROR:
	errno=ENOMEM;
	break;
    case Z_BUF_ERROR:
	errno=EINVAL;
	break;
    }

    return (err == Z_OK ? 0 : -1);
}
*/

/*
 * daap_free
 *
 * Free an entire daap formatted block
 */
int daap_free(DAAP_BLOCK *root) {
    if((root->size)&&(root->free))
	free(root->value);
    free(root->children);
    free(root->next);
    free(root);
    return 0;
}
