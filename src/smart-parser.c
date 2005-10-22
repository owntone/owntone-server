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

typedef struct tag_sp_node {
    union {
        struct tag_sp_node *node;
        char *field;
    } left;

    int op;
    int op_type;

    union {
        struct tag_sp_node *node;
        int ivalue;
        char *cvalue;
    } right;
} SP_NODE;


#define SP_OPTYPE_ANDOR  0
#define SP_OPTYPE_STRING 1
#define SP_OPTYPE_INT    2
#define SP_OPTYPE_DATE   3

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
 *
 * 0x0800 -
 * 0x0400 -
 * 0x0200 -
 * 0x0100 -
 */

#define T_STRING        0x2001
#define T_INT_FIELD     0x2002
#define T_STRING_FIELD  0x2003
#define T_DATE_FIELD    0x2004

#define T_OPENPAREN     0x0005
#define T_CLOSEPAREN    0x0006
#define T_LESS          0x0007
#define T_LESSEQUAL     0x0008
#define T_GREATER       0x0009
#define T_GREATEREQUAL  0x000a
#define T_EQUAL         0x000b
#define T_OR            0x000c
#define T_AND           0x000d
#define T_QUOTE         0x000e
#define T_NUMBER        0x000f
#define T_LAST          0x0010

#define T_EOF           0x00fd
#define T_BOF           0x00fe
#define T_ERROR         0x00ff

char *sp_token_descr[] = {
    "unknown",
    "literal string",
    "integer field",
    "string field",
    "date field",
    "(",
    ")",
    "<",
    "<=",
    ">",
    ">=",
    "=",
    "or",
    "and",
    "quote",
    "number"
};

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

    /* end of db fields */
    { T_OR, "or" },
    { T_AND, "and" },

    /* end */
    { 0, NULL },
};

typedef struct tag_parsetree {
    int in_string;
    char *term;
    char *current;
    SP_TOKEN token;
    int token_pos;
    SP_NODE *tree;
    char *error;
    char level;
} PARSESTRUCT, *PARSETREE;

#define SP_E_SUCCESS       0
#define SP_E_CLOSE         1
#define SP_E_FIELD         2
#define SP_E_STRCMP        3
#define SP_E_CLOSEQUOTE    4
#define SP_E_STRING        5
#define SP_E_OPENQUOTE     6
#define SP_E_INTCMP        7
#define SP_E_NUMBER        8

char *sp_errorstrings[] = {
    "Success",
    "Expecting ')'",
    "Expecting field name",
    "Expecting string comparison operator (=, includes)",
    "Expecting '\"' (closing quote)",
    "Expecting literal string",
    "Expecting '\"' (opening quote)",
    "Expecting integer comparison operator (=,<,>, etc)",    
    "Expecting integer"
};

/* Forwards */
SP_NODE *sp_parse_phrase(PARSETREE tree);
SP_NODE *sp_parse_oexpr(PARSETREE tree);
SP_NODE *sp_parse_aexpr(PARSETREE tree);
SP_NODE *sp_parse_expr(PARSETREE tree);
SP_NODE *sp_parse_criterion(PARSETREE tree);
SP_NODE *sp_parse_string_criterion(PARSETREE tree);
SP_NODE *sp_parse_int_criterion(PARSETREE tree);
SP_NODE *sp_parse_date_criterion(PARSETREE tree);
void sp_free_node(SP_NODE *node);
int sp_node_size(SP_NODE *node);
void sp_set_error(PARSETREE tree,int error);


/**
 * simple logging funcitons
 *
 * @param tree tree ew are parsing
 * @param function funtion entering/exiting
 * @param result result of param (if exiting)
 */
void sp_enter_exit(PARSETREE tree, char *function, int enter, void *result) {
    char *str_result = result ? "success" : "failure";

    if(enter) {
        tree->level++;
        DPRINTF(E_DBG,L_PARSE,"%*s Entering %s\n",tree->level," ",function);
    } else {
        DPRINTF(E_DBG,L_PARSE,"%*s Exiting %s (%s)\n",tree->level," ",
            function, str_result);
        tree->level--;
    }
}
    
    
/**
 * see if a string is actually a number
 *
 * @param string string to check
 * @returns 1 if the string is numeric, 0 otherwise
 */
int sp_isnumber(char *string) {
    char *current=string;
    
    while(*current && (*current >= '0') && (*current <= '9')) { 
        current++;
    }
        
    return *current ? 0 : 1;
}


/**
 * scan the input, returning the next available token.
 *
 * @param tree current working parse tree.
 * @returns next token (token, not the value)
 */
int sp_scan(PARSETREE tree) {
    char *terminator=NULL;
    char *tail;
    int advance=0;
    FIELDLOOKUP *pfield=sp_fields;
    int len;
    int found;
    int numval;

    if(tree->token.token_id & 0x2000) {
        if(tree->token.data.cvalue)
            free(tree->token.data.cvalue);
    }

    if(tree->token.token_id == T_EOF) {
        DPRINTF(E_DBG,L_PARSE,"%*s Returning token T_EOF\n",tree->level," ");
        return T_EOF;
    }

    /* keep advancing until we have a token */
    while(*(tree->current) && strchr(" \t\n\r",*(tree->current)))
        tree->current++;

    tree->token_pos = tree->current - tree->term;

    if(!*(tree->current)) {
        tree->token.token_id = T_EOF;
        DPRINTF(E_DBG,L_PARSE,"%*s Returning token %04x\n",tree->level," ",
            tree->token.token_id);
        return tree->token.token_id;
    }

    DPRINTF(E_SPAM,L_PARSE,"Current offset: %d, char: %c\n",
        tree->token_pos, *(tree->current));

    /* check singletons */
    if(!tree->in_string) {
        switch(*(tree->current)) {
        case '|':
            if((*(tree->current + 1) == '|')) {
                advance = 2;
                tree->token.token_id = T_OR;
            }
            break;

        case '&':
            if((*(tree->current + 1) == '&')) {
                advance = 2;
                tree->token.token_id = T_AND;
            }
            break;

        case '=':
            advance=1;
            tree->token.token_id = T_EQUAL;
            break;

        case '<':
            if((*(tree->current + 1)) == '=') {
                advance = 2;
                tree->token.token_id = T_LESSEQUAL;
            } else {
                advance = 1;
                tree->token.token_id = T_LESS;
            }
            break;

        case '>':
            if((*(tree->current + 1)) == '=') {
                advance = 2;
                tree->token.token_id = T_GREATEREQUAL;
            } else {
                advance = 1;
                tree->token.token_id = T_GREATER;
            }
            break;

        case '(':
            advance=1;
            tree->token.token_id = T_OPENPAREN;
            break;

        case ')':
            advance=1;
            tree->token.token_id = T_CLOSEPAREN;
            break;
        }
    } 

    if(*tree->current == '"') {
        advance = 1;
        tree->in_string = !tree->in_string;
        tree->token.token_id = T_QUOTE;
    }

    if(advance) { /* singleton */
        tree->current += advance;
    } else { /* either a keyword token or a quoted string */
        DPRINTF(E_SPAM,L_PARSE,"keyword or string!\n");

        /* walk to a terminator */
        tail = tree->current;

        terminator = " \t\n\r\"<>=()|&";
        if(tree->in_string) {
            terminator="\"";
        }

        while((*tail) && (!strchr(terminator,*tail))) {
            tail++;
        }

        found=0;
        len = tail - tree->current;

        if(!tree->in_string) {
            /* find it in the token list */
            pfield=sp_fields;
            DPRINTF(E_SPAM,L_PARSE,"Len is %d\n",len);
            while(pfield->name) {
                if(strlen(pfield->name) == len) {
                    if(strncasecmp(pfield->name,tree->current,len) == 0) {
                        found=1;
                        break;
                    }
                }
                pfield++;
            }
        }

        if(found) {
            tree->token.token_id = pfield->type;
        } else {
            tree->token.token_id = T_STRING;
        }

        if(tree->token.token_id & 0x2000) {
            tree->token.data.cvalue = malloc(len + 1);
            if(!tree->token.data.cvalue) {
                /* fail on malloc error */
                DPRINTF(E_FATAL,L_PARSE,"Malloc error.\n");
            }
            strncpy(tree->token.data.cvalue,tree->current,len);
            tree->token.data.cvalue[len] = '\x0';
        }

        /* check for numberic? */
        if(tree->token.token_id == T_STRING && 
            sp_isnumber(tree->token.data.cvalue)) {
            /* woops! */
            numval = atoi(tree->token.data.cvalue);
            free(tree->token.data.cvalue);
            tree->token.data.ivalue = numval;
            tree->token.token_id = T_NUMBER;
        }
        
        tree->current=tail;
    }

    DPRINTF(E_DBG,L_PARSE,"%*s Returning token %04x\n",tree->level," ",
        tree->token.token_id);
    if(tree->token.token_id & 0x2000)
        DPRINTF(E_SPAM,L_PARSE,"String val: %s\n",tree->token.data.cvalue);
    if(tree->token.token_id & 0x1000)
        DPRINTF(E_SPAM,L_PARSE,"Int val: %d\n",tree->token.data.ivalue);

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
 * I'm not a language expert, so I'd welcome suggestions on the
 * following production rules:
 *
 * phrase -> oexpr T_EOF
 * oexpr -> aexpr { T_AND aexpr }
 * aexpr -> expr { T_OR expr }
 * expr -> T_OPENPAREN oexpr T_CLOSEPAREN | criterion
 * criterion -> field op value
 *
 * field -> T_STRINGFIELD, T_INTFIELD, T_DATEFIELD
 * op -> T_EQUAL, T_GREATEREQUAL, etc
 * value -> T_NUMBER, T_STRING, or T_DATE, as appropriate
 *
 * @param tree parsetree previously created with sp_init
 * @param term term or phrase to parse
 * @returns 1 if successful, 0 if not
 */
int sp_parse(PARSETREE tree, char *term) {
    tree->term = strdup(term); /* will be destroyed by parsing */
    tree->current=tree->term;
    tree->token.token_id=T_BOF;

    if(tree->tree) 
        sp_free_node(tree->tree);
        
    sp_scan(tree);
    tree->tree = sp_parse_phrase(tree);

    if(tree->tree) {
        DPRINTF(E_SPAM,L_PARSE,"Parsed successfully\n");
    } else {
        DPRINTF(E_SPAM,L_PARSE,"Parsing error\n");
    }

    return tree->tree ? 1 : 0;
}


/**
 * parse for a phrase
 *
 * phrase -> aexpr T_EOF
 *
 * @param tree tree we are parsing (and building)
 * @returns new SP_NODE * if successful, NULL otherwise
 */

SP_NODE *sp_parse_phrase(PARSETREE tree) {
    SP_NODE *expr;

    sp_enter_exit(tree,"sp_parse_phrase",1,NULL);
    
    DPRINTF(E_SPAM,L_PARSE,"%*s Entering sp_parse_phrase\n",tree->level," ");
    tree->level++;
    
    expr = sp_parse_oexpr(tree);
    if((!expr) || (tree->token.token_id != T_EOF)) {
        sp_free_node(expr);
        expr = NULL;
    }

    sp_enter_exit(tree,"sp_parse_phrase",0,expr);
    return expr;
}

/**
 * parse for an ANDed expression
 *
 * aexpr -> expr { T_AND expr }
 *
 * @param tree tree we are building
 * @returns new SP_NODE pointer if successful, NULL otherwise
 */
SP_NODE *sp_parse_aexpr(PARSETREE tree) {
    SP_NODE *expr;
    SP_NODE *pnew;
    
    sp_enter_exit(tree,"sp_parse_aexpr",1,NULL);

    expr = sp_parse_expr(tree);
    
    while(expr && (tree->token.token_id == T_AND)) {
        pnew = (SP_NODE*)malloc(sizeof(SP_NODE));
        if(!pnew) {
            DPRINTF(E_FATAL,L_PARSE,"Malloc error\n");
        }
        
        memset(pnew,0x00,sizeof(SP_NODE));
        pnew->op=T_AND;
        pnew->op_type = SP_OPTYPE_ANDOR;
        
        pnew->left.node = expr;
        sp_scan(tree);
        pnew->right.node = sp_parse_expr(tree);
    
        if(!pnew->right.node) {
            sp_free_node(pnew);
            pnew=NULL;
        }
        
        expr=pnew;
    }

    sp_enter_exit(tree,"sp_parse_aexpr",0,NULL);
    return expr;
}

/**
 * parse for an ORed expression
 *
 * oexpr -> aexpr { T_OR aexpr }
 *
 * @param tree tree we are building
 * @returns new SP_NODE pointer if successful, NULL otherwise
 */
SP_NODE *sp_parse_oexpr(PARSETREE tree) {
    SP_NODE *expr;
    SP_NODE *pnew;
    
    sp_enter_exit(tree,"sp_parse_oexpr",1,NULL);
    
    expr = sp_parse_aexpr(tree);
    
    while(expr && (tree->token.token_id == T_OR)) {
        pnew = (SP_NODE*)malloc(sizeof(SP_NODE));
        if(!pnew) {
            DPRINTF(E_FATAL,L_PARSE,"Malloc error\n");
        }
        
        memset(pnew,0x00,sizeof(SP_NODE));
        pnew->op=T_OR;
        pnew->op_type = SP_OPTYPE_ANDOR;
        
        pnew->left.node = expr;
        sp_scan(tree);
        pnew->right.node = sp_parse_aexpr(tree);
    
        if(!pnew->right.node) {
            sp_free_node(pnew);
            pnew=NULL;
        }
        
        expr=pnew;
    }

    sp_enter_exit(tree,"sp_parse_oexptr",0,expr);
    return expr;
}

/**
 * parse for an expression
 *
 * expr -> T_OPENPAREN oexpr T_CLOSEPAREN | criteria
 *
 * @param tree tree we are building
 * @returns pointer to new SP_NODE if successful, NULL otherwise
 */
SP_NODE *sp_parse_expr(PARSETREE tree) {
    SP_NODE *expr;

    sp_enter_exit(tree,"sp_parse_expr",1,NULL);
    
    if(tree->token.token_id == T_OPENPAREN) {
        sp_scan(tree);
        expr = sp_parse_oexpr(tree);
        if((expr) && (tree->token.token_id == T_CLOSEPAREN)) {
            sp_scan(tree);
        } else {
            /* Error: expecting close paren */
            sp_set_error(tree,SP_E_CLOSE);
            sp_free_node(expr);
            expr=NULL;
        }
    } else {
        expr = sp_parse_criterion(tree);
    }

    sp_enter_exit(tree,"sp_parse_expr",0,expr);
    return expr;
}

/**
 * parse for a criterion
 *
 * criterion -> field op value
 *
 * @param tree tree we are building
 * @returns pointer to new SP_NODE if successful, NULL otherwise.
 */
SP_NODE *sp_parse_criterion(PARSETREE tree) {
    SP_NODE *expr=NULL;
    
    sp_enter_exit(tree,"sp_parse_criterion",1,expr);
    
    switch(tree->token.token_id) {
    case T_STRING_FIELD:
        expr = sp_parse_string_criterion(tree);
        break;

    case T_INT_FIELD:
        expr = sp_parse_int_criterion(tree);
        break;

    case T_DATE_FIELD:
        expr = sp_parse_date_criterion(tree);
        break;

    default:
        /* Error: expecting field */
        sp_set_error(tree,SP_E_FIELD);
        expr = NULL;
        break;
    }

    sp_enter_exit(tree,"sp_parse_criterion",0,expr);
    return expr;
}

/**
 * parse for a string criterion
 *
 * @param tree tree we are building
 * @returns pointer to new SP_NODE if successful, NULL otherwise
 */
 SP_NODE *sp_parse_string_criterion(PARSETREE tree) {
    int result=0;
    SP_NODE *pnew = NULL;

    sp_enter_exit(tree,"sp_parse_string_criterion",1,NULL);
    
    pnew = malloc(sizeof(SP_NODE));
    if(!pnew) {
        DPRINTF(E_FATAL,L_PARSE,"Malloc Error\n");
    }
    memset(pnew,0x00,sizeof(SP_NODE));
    pnew->left.field = strdup(tree->token.data.cvalue);

    sp_scan(tree); /* scan past the string field we know is there */

    switch(tree->token.token_id) {
    case T_EQUAL:
        pnew->op=tree->token.token_id;
        pnew->op_type = SP_OPTYPE_STRING;
        result = 1;
        break;
    default:
        /* Error: expecting legal string comparison operator */
        sp_set_error(tree,SP_E_STRCMP);
        break;
    }

    if(result) {
        sp_scan(tree);
        /* should be sitting on quote literal string quote */

        if(tree->token.token_id == T_QUOTE) {
            sp_scan(tree);
            if(tree->token.token_id == T_STRING) {
                pnew->right.cvalue=strdup(tree->token.data.cvalue);
                sp_scan(tree);
                if(tree->token.token_id == T_QUOTE) {
                    result=1;
                    sp_scan(tree);
                } else {
                    sp_set_error(tree,SP_E_CLOSEQUOTE);
                    DPRINTF(E_SPAM,L_PARSE,"Expecting closign quote\n");
                }
            } else {
                sp_set_error(tree,SP_E_STRING);
                DPRINTF(E_SPAM,L_PARSE,"Expecting literal string\n");
            }
        } else {
            sp_set_error(tree,SP_E_OPENQUOTE);
            DPRINTF(E_SPAM,L_PARSE,"Expecting opening quote\n");
        }
    }


    if(!result) {
        sp_free_node(pnew);
        pnew=NULL;        
    }

    sp_enter_exit(tree,"sp_parse_string_criterion",0,pnew);
    return pnew;
 }

/**
 * parse for an int criterion
 *
 * @param tree tree we are building
 * @returns address of new SP_NODE if successful, NULL otherwise
 */
 SP_NODE *sp_parse_int_criterion(PARSETREE tree) {
    int result=0;
    SP_NODE *pnew = NULL;
    
    sp_enter_exit(tree,"sp_parse_int_criterion",1,pnew);
    pnew = malloc(sizeof(SP_NODE));
    if(!pnew) {
        DPRINTF(E_FATAL,L_PARSE,"Malloc Error\n");
    }
    memset(pnew,0x00,sizeof(SP_NODE));
    pnew->left.field = strdup(tree->token.data.cvalue);

    sp_scan(tree); /* scan past the int field we know is there */

    switch(tree->token.token_id) {
    case T_LESSEQUAL:
    case T_LESS:
    case T_GREATEREQUAL:
    case T_GREATER:
    case T_EQUAL:
        result = 1;
        pnew->op=tree->token.token_id;
        pnew->op_type = SP_OPTYPE_INT;
        break;
    default:
        /* Error: expecting legal int comparison operator */
        sp_set_error(tree,SP_E_INTCMP);
        DPRINTF(E_LOG,L_PARSE,"Expecting int comparison op, got %04X\n",
            tree->token.token_id);
        break;
    }

    if(result) {
        sp_scan(tree);
        /* should be sitting on a literal string */
        if(tree->token.token_id == T_NUMBER) {
            result = 1;
            pnew->right.ivalue=tree->token.data.ivalue;
            sp_scan(tree);
        } else {
            /* Error: Expecting number */
            sp_set_error(tree,SP_E_NUMBER);
            DPRINTF(E_LOG,L_PARSE,"Expecting number, got %04X\n",
                tree->token.token_id);
            result = 0;
        }
    }

    if(!result) {
        sp_free_node(pnew);
        pnew=NULL;
    }
        
    sp_enter_exit(tree,"sp_parse_int_criterion",0,pnew);

    return pnew;
 }


/**
 * parse for a date criterion
 *
 * @param tree tree we are building
 * @returns pointer to new SP_NODE if successful, NULL otherwise
 */
 SP_NODE *sp_parse_date_criterion(PARSETREE tree) {
    SP_NODE *pnew=NULL;

    sp_enter_exit(tree,"sp_parse_date_criterion",1,pnew);

    sp_enter_exit(tree,"sp_parse_date_criterion",0,pnew);

    return pnew;
 }

/**
 * free a node, and all left/right subnodes
 *
 * @param node node to free
 */
void sp_free_node(SP_NODE *node) {
    if(!node)
        return;

    if(node->op_type == SP_OPTYPE_ANDOR) {
        if(node->left.node) {
            sp_free_node(node->left.node);
            node->left.node = NULL;
        }            
        
        if(node->right.node) {
            sp_free_node(node->right.node);
            node->right.node = NULL;
        }
    } else {
        if(node->left.field) {
            free(node->left.field);
            node->left.field = NULL;
        }
        
        if(node->op_type == SP_OPTYPE_STRING) {
            if(node->right.cvalue) {
                free(node->right.cvalue);
                node->right.cvalue = NULL;
            }
        }
    }


    free(node);
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
        
    if(tree->token.token_id & 0x2000)
        free(tree->token.data.cvalue);
        
    if(tree->error)
        free(tree->error);
        
    free(tree);
    return 1;
}

/**
 * calculate the size required to render the tree as a 
 * sql query.
 *
 * @parameter node node/tree to calculate
 * @returns size in bytes of sql "where" clause
 */
int sp_node_size(SP_NODE *node) {
    int size;
    
    if(node->op_type == SP_OPTYPE_ANDOR) {
        size = sp_node_size(node->left.node);
        size += sp_node_size(node->right.node);
        size += 7; /* (xxx AND xxx) */
    } else {
        size = 4; /* parens, plus spaces around op */
        size += strlen(node->left.field);
        if((node->op & 0x0FFF) > T_LAST) {
            DPRINTF(E_FATAL,L_PARSE,"Can't determine node size:  op %04x\n",
                node->op);
        } else {
            size += strlen(sp_token_descr[node->op & 0x0FFF]);
        }
        if(node->op_type == SP_OPTYPE_STRING)
            size += (2 + strlen(node->right.cvalue));
        if(node->op_type == SP_OPTYPE_INT)
            size += ((node->right.ivalue/10) + 1);
    }
     
    return size;
}

/**
 * serialize node into pre-allocated string
 *
 * @param node node to serialize
 * @param string string to generate
 */
void sp_serialize_sql(SP_NODE *node, char *string) {
    char buffer[40];
    
    if(node->op_type == SP_OPTYPE_ANDOR) {
        strcat(string,"(");
        sp_serialize_sql(node->left.node,string);
        if(node->op == T_AND) strcat(string," and ");
        if(node->op == T_OR) strcat(string," or ");
        sp_serialize_sql(node->right.node,string);
        strcat(string,")");
    } else {
        strcat(string,"(");
        strcat(string,node->left.field);
        strcat(string," ");
        strcat(string,sp_token_descr[node->op & 0x0FFF]);
        strcat(string," ");
        if(node->op_type == SP_OPTYPE_STRING) {
            strcat(string,"'");
            strcat(string,node->right.cvalue);
            strcat(string,"'");
        }
        
        if(node->op_type == SP_OPTYPE_INT) {
            sprintf(buffer,"%d",node->right.ivalue);
            strcat(string,buffer);
        }
        strcat(string,")");
        
    }
}



/**
 * generate sql "where" clause
 *
 * @param node node/tree to calculate
 * @returns sql string.  Must be freed by caller
 */
char *sp_sql_clause(PARSETREE tree) {
    int size;
    char *sql;

    size = sp_node_size(tree->tree);
    sql = (char*)malloc(size+1);
    
    memset(sql,0x00,size+1);
    sp_serialize_sql(tree->tree,sql);
    
    return sql;
}



/**
 * if there was an error in a previous action (parsing?)
 * then return that error to the client.  This does not
 * clear the error condition -- multiple calls to sp_geterror
 * will return the same value.  Also, if you want to keep an error,
 * you must strdup it before it disposing the parse tree...
 *
 * memory handling is done on the smart-parser side.
 *
 * @param tree tree that generated the last error
 * @returns text of the last error
 */
char *sp_get_error(PARSETREE tree) {
    return tree->error;
}

/**
 * set the parse tree error for retrieval above
 *
 * @param tree tree we are setting error for
 * @param error error code
 */
void sp_set_error(PARSETREE tree, int error) {
    int len;
    
    if(tree->error)
        free(tree->error);
        
    len = 10 + (tree->token_pos / 10) + 1 + strlen(sp_errorstrings[error]) + 1;
    tree->error = (char*)malloc(len);
    if(!tree->error) {
        DPRINTF(E_FATAL,L_PARSE,"Malloc error");
        return;
    }
    
    sprintf(tree->error,"Offset %d:  %s",tree->token_pos + 1,sp_errorstrings[error]);
    return;
}

