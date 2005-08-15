/*
 * $Id$
 *
 * This is really two parts -- the lexer and the parser.  Converting
 * a parse tree back to a format that works with the database backend
 * is left to the db backend.
 *
 * Oh, and this is called "smart-parser" because it parses terms for
 * specifying smart playlists, not because it is particularly smart.  :)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "err.h"


typedef struct tag_tokens {
    int token_id;
    int token_type;
    union {
	char *cvalue;
	int ivalue;
    } data;
} SP_TOKENS;

#define T_ID 0

#define TT_INT 0


SP_TOKENS sp_tokenlist[] = {
    { T_ID, TT_INT, { "id" } }
};

typedef struct tag_parsetree {
    char *term;
    int token;
    int next_token;
} PARSESTRUCT, *PARSETREE;

#define SP_TOK_EOF     0

int sp_scan(PARSETREE tree) {
    return SP_TOK_EOF;
}


/**
 * set up the initial parse tree
 * 
 * @returns opaque parsetree struct
 */
PARSETREE sp_init(void) {
    PARSETREE ptree;

    ptree = (PARSETREE)malloc(sizeof(PARSESTRUCT));
    if(!ptree) 
	DPRINTF(E_FATAL,L_PARSE,"Alloc error\n");

    memset(ptree,0,sizeof(PARSESTRUCT));
    return ptree;
}

/**
 * parse a term or phrase into a tree.
 *
 * @param tree parsetree previously created with sp_init
 * @param term term or phrase to parse
 * @returns 1 if successful, 0 if not
 */
int sp_parse(PARSETREE tree, char *term) {
    tree->term = strdup(term); /* will be destroyed by parsing */
    while(sp_scan(tree)) {
	if(tree->token == SP_TOK_EOF)
	    return 1; /* valid tree! */

	/* otherwise, keep scanning until done or error */
    }

    return 0;
}

/**
 * dispose of an initialized tree
 *
 * @param tree tree to dispose
 * @returns 1
 */
int sp_dispose(PARSETREE tree) {
    if(tree->term)
	free(tree->term);

    free(tree);
    return 1;
}


/**
 * if there was an error in a previous action (parsing?)
 * then return that error to the client.  This does not 
 * clear the error condition -- multiple calls to sp_geterror
 * will return the same value.
 *
 * memory handling is done on the smart-parser side.
 *
 * @param tree tree that generated the last error
 * @returns text of the last error
 */
char *sp_geterror(PARSETREE tree) {
    return "blah";
}

