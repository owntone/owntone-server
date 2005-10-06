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


#define TT_INT      0
#define TT_STRING   1
#define TT_DATE     2

SP_TOKENS sp_tokenlist[] = {
    { T_ID, TT_INT, { "id" } },
    { T_PATH, TT_STRING, { "path" } },
    { T_TITLE, TT_STRING, { "title" } },
    { T_ARTIST, TT_STRING, { "artist" } },
    { T_ALBUM, TT_STRING, { "album" } },
    { T_GENRE, TT_STRING, { "genre" } },
    { T_COMMENT, TT_STRING, { "comment" } },
    { T_TYPE, TT_STRING, { "type" } },
    { T_COMPOSER, TT_STRING, { "composer" } },
    { T_ORCHESTRA, TT_STRING, { "orchestra" } },
    { T_GROUPING, TT_STRING, { "grouping" } },
    { T_URL, TT_STRING, { "url" } },
    { T_BITRATE, TT_INT, { "bitrate" } },
    { T_SAMPLERATE, TT_INT, { "samplerate" } },
    { T_SONG_LENGTH, TT_INT, { "songlength" } },
    { T_FILE_SIZE, TT_INT, { "filesize" } },
    { T_YEAR, TT_INT, { "year" } },
    { T_TRACK, TT_INT, { "track" } },
    { T_TOTAL_TRACKS, TT_INT, { "totaltracks" } },
    { T_DISC, TT_INT, { "disc" } },
    { T_TOTAL_DISCS, TT_INT, { "totaldiscs" } },
    { T_BPM, TT_INT, { "bpm" } },
    { T_COMPILATION, TT_INT, { "compilation" } },
    { T_RATING, TT_INT, { "rating" } },
    { T_PLAYCOUNT, TT_INT, { "playcount"} },
    { T_DATA_KIND, TT_INT, { "datakind" } },
    { T_ITEM_KIND, TT_INT, { "itemkind" } },
    { T_DESCRIPTION, TT_STRING, { "description" } },
    { 0, 0, { NULL } }
};

typedef struct tag_parsetree {    
    char *term;
    char *current;
    int token;
    int next_token;
} PARSESTRUCT, *PARSETREE;

#define SP_TOK_BOF     0x0
#define SP_TOK_EOF     0x1

int sp_scan(PARSETREE tree) {
    char *tail;
    int done=0;

    tree->token=tree->next_token;

    if(tree->token == SP_TOK_EOF)
	return SP_TOK_EOF;

    /* keep advancing until we have a token */
    while(strchr(" \t\n\r",*current))
	current++;
	
    if(!current) {
	tree->next_token = SP_TOK_EOF;
	return tree->token;
    }

    /* check singletons */

    
    return tree->token;
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
    tree->token=SP_TOK_BOF;
    tree->next_token=SP_TOK_BOF;
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

