/*
 * $Id$
 *
 */

#ifndef _PL_H_
#define _PL_H_

typedef struct tag_pl_node {
    int op;
    union { 
	int ival;
	struct tag_pl_node *plval;
    } arg1;
    union {
	char *cval;
	struct tag_pl_node *plval;
    } arg2;
} PL_NODE;

typedef struct tag_smart_playlist {
    char *name;
    PL_NODE *root;
    struct tag_smart_playlist *next;
} SMART_PLAYLIST;

extern SMART_PLAYLIST pl_smart;
extern void pl_dump(void);

#endif /* _PL_H_ */


