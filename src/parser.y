%{

#include <stdio.h>
#include "playlist.h"

#define YYERROR_VERBOSE 1

/* Forwards */

extern PL_NODE *pl_newpredicate(int tag, int op, char *value);
extern PL_NODE *pl_newexpr(PL_NODE *arg1, int op, PL_NODE *arg2);
extern int pl_addplaylist(char *name, PL_NODE *root);

%}

%left TOK_OR TOK_AND

%union {
    unsigned int ival;
    char *cval;
    PL_NODE *plval;    
}

%token <ival> TOK_ARTIST 
%token <ival> TOK_ALBUM 
%token <ival> TOK_GENRE

%token <ival> TOK_IS 
%token <ival> TOK_INCLUDES

%token <ival> TOK_OR 
%token <ival> TOK_AND
%token <ival> TOK_NOT

%token <cval> TOK_ID

%type <plval> expression
%type <plval> predicate
%type <ival> idtag
%type <ival> boolarg
%type <cval> value
%type <ival> playlist

%%

playlistlist: playlist
| playlistlist playlist
;

playlist: TOK_ID '{' expression '}' { $$ = pl_addplaylist($1, $3); }
;

expression: expression TOK_AND expression { $$=pl_newexpr($1,$2,$3); }
| expression TOK_OR expression { $$=pl_newexpr($1,$2,$3); }
| '(' expression ')' { $$=$2; }
| predicate
;

predicate: idtag boolarg value { $$=pl_newpredicate($1, $2, $3); }

idtag: TOK_ARTIST
| TOK_ALBUM
| TOK_GENRE
;

boolarg: TOK_IS
| TOK_INCLUDES
| TOK_NOT boolarg { $$=$2 | 0x80000000; }
;

value: TOK_ID
;


%%

PL_NODE *pl_newpredicate(int tag, int op, char *value) {
    PL_NODE *pnew;

    pnew=(PL_NODE*)malloc(sizeof(PL_NODE));
    if(!pnew)
	return NULL;

    pnew->op=op;
    pnew->arg1.ival=tag;
    pnew->arg2.cval=value;
    return pnew;
}

PL_NODE *pl_newexpr(PL_NODE *arg1, int op, PL_NODE *arg2) {
    PL_NODE *pnew;

    pnew=(PL_NODE*)malloc(sizeof(PL_NODE));
    if(!pnew)
	return NULL;

    pnew->op=op;
    pnew->arg1.plval=arg1;
    pnew->arg2.plval=arg2;
    return pnew;
}

int pl_addplaylist(char *name, PL_NODE *root) {
    SMART_PLAYLIST *pnew;

    pnew=(SMART_PLAYLIST *)malloc(sizeof(SMART_PLAYLIST));
    if(!pnew)
	return -1;

    pnew->next=pl_smart.next;
    pnew->name=name;
    pnew->root=root;
    pl_smart.next=pnew;

    return 0;
}
