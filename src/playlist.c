/*
 * $Id$
 *
 */

#include <stdio.h>

#include "playlist.h"
#include "parser.h"

/* Globals */
SMART_PLAYLIST pl_smart = { NULL, NULL, NULL };

/* Forwards */
void pl_dump(void);
void pl_dump_node(PL_NODE *pnode, int indent);

/*
 * pl_dump
 *
 * Dump the playlist list for debugging
 */
void pl_dump(void) {
    SMART_PLAYLIST *pcurrent=pl_smart.next;
    
    while(pcurrent) {
	printf("Playlist %s:\n",pcurrent->name);
	pl_dump_node(pcurrent->root,1);
	pcurrent=pcurrent->next;
    }
}

/*
 * pl_dump_node
 *
 * recursively dump a node 
 */
void pl_dump_node(PL_NODE *pnode, int indent) {
    int index;
    int not=0;
    unsigned int boolarg;

    for(index=0;index<indent;index++) {
	printf(" ");
    }

    if(pnode->op == TOK_AND) {
	printf("AND\n");
    } else if (pnode->op == TOK_OR) {
	printf("OR\n");
    }

    if((pnode->op == TOK_AND) || (pnode->op == TOK_OR)) {
	pl_dump_node(pnode->arg1.plval,indent+1);
	pl_dump_node(pnode->arg2.plval,indent+1);
	return;
    }

    switch(pnode->arg1.ival) {
    case TOK_ARTIST:
	printf("ARTIST ");
	break;
    case TOK_ALBUM:
	printf("ALBUM ");
	break;
    case TOK_GENRE:
	printf("GENRE ");
	break;
    default:
	printf ("<unknown tag> ");
	break;
    }

    boolarg=(pnode->op) & 0x7FFFFFFF;
    if(pnode->op & 0x80000000) 
	not=1;

    switch(boolarg) {
    case TOK_IS:
	printf("%s",not? "IS " : "IS NOT ");
	break;
    case TOK_INCLUDES:
	printf("%s",not? "INCLUDES " : "DOES NOT INCLUDE ");
	break;
    default:
	printf("<unknown boolop> ");
	break;
    }

    printf("%s\n",pnode->arg2.cval);
    return;
}
