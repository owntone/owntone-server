/*
 * $Id$
 *
 */

#ifndef _PL_H_
#define _PL_H_

#include "mp3-scanner.h"

#define T_INT  0
#define T_STR  1

typedef struct tag_pl_node {
    int op;
    int type;
    union { 
	int ival;
	struct tag_pl_node *plval;
    } arg1;
    union {
	char *cval;
	int ival;
	struct tag_pl_node *plval;
    } arg2;
} PL_NODE;

typedef struct tag_smart_playlist {
    char *name;
    unsigned int id;
    PL_NODE *root;
    struct tag_smart_playlist *next;
} SMART_PLAYLIST;

extern SMART_PLAYLIST pl_smart;
extern int pl_error;

extern void pl_dump(void);
extern int pl_load(char *file);
extern void pl_eval(MP3FILE *pmp3);

#endif /* _PL_H_ */


