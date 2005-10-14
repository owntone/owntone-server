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

typedef struct tag_token {
    int token_id;
    union {
        char *cvalue;
        int ivalue;
    } data;
} SP_TOKEN;

/*
#define T_ID            0x00
#define T_PATH          0x01
#define T_TITLE         0x02
#define T_ARTIST        0x03
#define T_ALBUM         0x04
#define T_GENRE         0x05
#define T_COMMENT       0x06
#define T_TYPE          0x07
#define T_COMPOSER      0x08
#define T_ORCHESTRA     0x09
#define T_GROUPING      0x0a
#define T_URL           0x0b
#define T_BITRATE       0x0c
#define T_SAMPLERATE    0x0d
#define T_SONG_LENGTH   0x0e
#define T_FILE_SIZE     0x0f
#define T_YEAR          0x10
#define T_TRACK         0x11
#define T_TOTAL_TRACKS  0x12
#define T_DISC          0x13
#define T_TOTAL_DISCS   0x14
#define T_BPM           0x15
#define T_COMPILATION   0x16
#define T_RATING        0x17
#define T_PLAYCOUNT     0x18
#define T_DATA_KIND     0x19
#define T_ITEM_KIND     0x1a
#define T_DESCRIPTION   0x1b
#define T_TIME_ADDED    0x1c
#define T_TIME_MODIFIED 0x0d
#define T_TIME_PLAYED   0x1d
#define T_TIME_STAMP    0x1e
#define T_DISABLED      0x1f
#define T_SAMPLE_COUNT  0x1e
#define T_FORCE_UPDATE  0x1f
#define T_CODECTYPE     0x20
#define T_IDX           0x21
*/

/** 
 * high 4 bits:
 *
 * 0x8000 - 
 * 0x4000 -
 * 0x2000 - data is string
 * 0x1000 - data is int
 */

#define T_STRING        0x2001
#define T_INT_FIELD     0x2002
#define T_STRING_FIELD  0x2003
#define T_DATE_FIELD    0x2004

#define T_OPENPAREN     0x0005
#define T_CLOSEPAREN    0x0006
#define T_QUOTE         0x0007
#define T_LESS          0x0008
#define T_LESSEQUAL     0x0009
#define T_GREATER       0x000A
#define T_GREATEREQUAL  0x000B
#define T_EQUAL         0x000C

#define T_EOF           0x000D
#define T_BOF           0x000E

typedef struct tag_fieldlookup {
    int type;
    char *name;
} FIELDLOOKUP;

FIELDLOOKUP sp_fields[] = {
    { T_INT_FIELD, "id" },
    { T_STRING_FIELD, "path" },
    { T_STRING_FIELD, "title" },
    { T_STRING_FIELD, "artist" },
    { T_STRING_FIELD, "album" },
    { T_STRING_FIELD, "genre" },
    { T_STRING_FIELD, "comment" },
    { T_STRING_FIELD, "type" },
    { T_STRING_FIELD, "composer" },
    { T_STRING_FIELD, "orchestra" },
    { T_STRING_FIELD, "grouping" },
    { T_STRING_FIELD, "url" },
    { T_INT_FIELD, "bitrate" },
    { T_INT_FIELD, "samplerate" },
    { T_INT_FIELD, "songlength" },
    { T_INT_FIELD, "filesize" },
    { T_INT_FIELD, "year" },
    { T_INT_FIELD, "track" },
    { T_INT_FIELD, "totaltracks" },
    { T_INT_FIELD, "disc" },
    { T_INT_FIELD, "totaldiscs" },
    { T_INT_FIELD, "bpm" },
    { T_INT_FIELD, "compilation" },
    { T_INT_FIELD, "rating" },
    { T_INT_FIELD, "playcount" },
    { T_INT_FIELD, "datakind" },
    { T_INT_FIELD, "itemkind" },
    { T_STRING_FIELD, "description" },
    { 0, NULL },
};

typedef struct tag_parsetree {    
    char *term;
    char *current;
    SP_TOKEN token;
    SP_TOKEN next_token;
} PARSESTRUCT, *PARSETREE;

/**
 * scan the input, returning the next available token.
 *
 * @param tree current working parse tree.
 * @returns next token (token, not the value)
 */
int sp_scan(PARSETREE tree) {
    char *tail;
    int advance=0;
    FIELDLOOKUP *pfield=sp_fields;
    int len;

    if(tree->token.token_id & 0x2000) {
	if(tree->token.data.cvalue)
	    free(tree->token.data.cvalue);
    }

    tree->token=tree->next_token;

    if(tree->token.token_id == T_EOF)
        return T_EOF;

    /* keep advancing until we have a token */
    while(*(tree->current) && strchr(" \t\n\r",*(tree->current)))
        tree->current++;
        
    if(!*(tree->current)) {
        tree->next_token.token_id = T_EOF;
        return tree->token.token_id;
    }
    
    DPRINTF(E_SPAM,L_PARSE,"Current offset: %d, char: %c\n",
        tree->current - tree->term, *(tree->current));

    /* check singletons */
    switch(*(tree->current)) {
    case '=':
        advance=1;
        tree->next_token.token_id = T_EQUAL;
        break;
    case '<':
        if((*(tree->current + 1)) == '=') {
            advance = 2;
            tree->next_token.token_id = T_LESSEQUAL;
        } else {
            advance = 1;
            tree->next_token.token_id = T_LESS;
        }
        break;
    case '>':
        if((*(tree->current + 1)) == '=') {
            advance = 2;
            tree->next_token.token_id = T_GREATEREQUAL;
        } else {
            advance = 1;
            tree->next_token.token_id = T_GREATER;
        }
        break;

    case '(':
        advance=1;
        tree->next_token.token_id = T_OPENPAREN;
        break;

    case ')':
        advance=1;
        tree->next_token.token_id = T_CLOSEPAREN;
        break;

    case '"':
        advance=1;
        tree->next_token.token_id = T_QUOTE;
        break;
    }

    if(advance) {
        tree->current += advance;
    } else { /* either a keyword token or a quoted string */
        DPRINTF(E_SPAM,L_PARSE,"keyword or string!\n");
        
        /* walk to a terminator */
        tail = tree->current;
        while((*tail) && (!strchr(" \t\n\r\"<>=()",*tail))) {
            tail++;
        }

        /* let's see what we have... */
        pfield=sp_fields;
        len = tail - tree->current;
        DPRINTF(E_SPAM,L_PARSE,"Len is %d\n",len);
        while(pfield->name) {
            if(strlen(pfield->name) == len) {
                if(strncasecmp(pfield->name,tree->current,len) == 0)
                    break;
            }
            pfield++;
        }
        
        if(pfield->name) {
            tree->next_token.token_id = pfield->type;
        } else {
            tree->next_token.token_id = T_STRING;
        }
        tree->next_token.data.cvalue = malloc(len + 1);
        if(!tree->next_token.data.cvalue) {
            /* fail on malloc error */
            DPRINTF(E_FATAL,L_PARSE,"Malloc error.\n");
        }
        strncpy(tree->next_token.data.cvalue,tree->current,len);
        tree->next_token.data.cvalue[len] = '\x0';

        tree->current=tail;
    }
    
    return tree->token.token_id;
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
    tree->current=tree->term;
    tree->token.token_id=T_BOF;
    tree->next_token.token_id=T_BOF;
    sp_scan(tree);
    while(sp_scan(tree)) {
        DPRINTF(E_SPAM,L_PARSE,"Got token %04X\n",tree->token.token_id);
        if(tree->token.token_id & 0x2000) {
            DPRINTF(E_SPAM,L_PARSE,"  Str val: %s\n",tree->token.data.cvalue);
        } else if(tree->token.token_id & 0x1000) {
            DPRINTF(E_SPAM,L_PARSE,"  Int val: %d (0x%04X)\n",
                tree->token.data.ivalue,tree->token.data.ivalue);
        }
        
        if((tree->token.token_id == T_EOF))
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

