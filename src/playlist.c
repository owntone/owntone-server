/*
 * $Id$
 *
 */

#include <stdio.h>

#include "err.h"
#include "mp3-scanner.h"
#include "playlist.h"
#include "parser.h"

/* Globals */
SMART_PLAYLIST pl_smart = { NULL, NULL, NULL };
int pl_error=0;

/* Forwards */
void pl_dump(void);
void pl_dump_node(PL_NODE *pnode, int indent);
int pl_load(char *file);
int pl_eval_node(MP3FILE *pmp3, PL_NODE *pnode);

extern FILE *yyin;

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

    if(pnode->op == AND) {
	printf("AND\n");
    } else if (pnode->op == OR) {
	printf("OR\n");
    }

    if((pnode->op == AND) || (pnode->op == OR)) {
	pl_dump_node(pnode->arg1.plval,indent+1);
	pl_dump_node(pnode->arg2.plval,indent+1);
	return;
    }

    switch(pnode->arg1.ival) {
    case ARTIST:
	printf("ARTIST ");
	break;
    case ALBUM:
	printf("ALBUM ");
	break;
    case GENRE:
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
    case IS:
	printf("%s",not? "IS NOT " : "IS ");
	break;
    case INCLUDES:
	printf("%s",not? "DOES NOT INCLUDE " : "INCLUDES ");
	break;
    default:
	printf("<unknown boolop> ");
	break;
    }

    printf("%s\n",pnode->arg2.cval);
    return;
}

/*
 * pl_load
 *
 * Load a smart playlist
 */
int pl_load(char *file) {
    FILE *fin;
    SMART_PLAYLIST *pcurrent;
    int result;

    fin=fopen(file,"r");
    if(!fin) {
	return -1;
    }

    yyin=fin;
    result=yyparse();
    fclose(fin);

    if(pl_error) {
	return -1;
    }

    /* register the playlists */
    DPRINTF(ERR_INFO,"Finished loading smart playlists\n");
    pcurrent=pl_smart.next;
    while(pcurrent) {
	DPRINTF(ERR_INFO,"Adding smart playlist %s as %d\n",pcurrent->name,pcurrent->id)
	db_add_playlist(pcurrent->id, pcurrent->name);
	pcurrent=pcurrent->next;
    }

    return 0;
}

/*
 * pl_eval
 *
 * Run each MP3 file through the smart playlists
 */
void pl_eval(MP3FILE *pmp3) {
    SMART_PLAYLIST *pcurrent;

    pcurrent=pl_smart.next;
    while(pcurrent) {
	if(pl_eval_node(pmp3,pcurrent->root)) {
	    DPRINTF(ERR_DEBUG,"Matched song to playlist %s (%d)\n",pcurrent->name,pcurrent->id);
	    db_add_playlist_song(pcurrent->id, pmp3->id);
	}

	pcurrent=pcurrent->next;
    }
}


/*
 * pl_eval_node
 *
 * Test node status
 */
int pl_eval_node(MP3FILE *pmp3, PL_NODE *pnode) {
    int r_arg,r_arg2;
    int argtypec;
    char *argc;
    int boolarg;
    int not=0;
    int retval=0;

    if((pnode->op == AND) || (pnode->op == OR)) {
	r_arg=pl_eval_node(pmp3,pnode->arg1.plval);
	if((pnode->op == AND) && !r_arg)
	    return 0;
	if((pnode->op == OR) && r_arg)
	    return 1;

	r_arg2=pl_eval_node(pmp3,pnode->arg2.plval);
	if(pnode->op == AND)
	   return r_arg && r_arg2;

	return r_arg || r_arg2;
    }

    /* Not an AND/OR node, so let's eval */
    switch(pnode->arg1.ival) {
    case ALBUM:
	argtypec=1;
	argc=pmp3->album;
	break;
    case ARTIST:
	argtypec=1;
	argc=pmp3->artist;
	break;
    case GENRE:
	argtypec=1;
	argc=pmp3->genre;
	break;
    }

    boolarg=(pnode->op) & 0x7FFFFFFF;
    if(pnode->op & 0x80000000)
	not=1;

    if(argtypec) {
	if(!argc)
	    return not;

	DPRINTF(ERR_DEBUG,"Matching %s to %s\n",argc,pnode->arg2.cval);

	switch(boolarg) {
	case IS:
	    r_arg=strcasecmp(argc,pnode->arg2.cval);
	    retval = not ? r_arg : !r_arg;
	    break;
	case INCLUDES:
	    r_arg=strcasestr(argc,pnode->arg2.cval);
	    retval = not ? !r_arg : r_arg;
	    break;
	}
    }

    /* can't get here */
    DPRINTF(ERR_DEBUG,"Returning %d\n",retval);
    return retval;
}



