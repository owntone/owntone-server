%{

#include <stdio.h>
#include "playlist.h"

#define YYERROR_VERBOSE 1

/* Forwards */

extern PL_NODE *pl_newpredicate(int tag, int op, char *value);
extern PL_NODE *pl_newexpr(PL_NODE *arg1, int op, PL_NODE *arg2);
extern int pl_addplaylist(char *name, PL_NODE *root);

/* Globals */

int pl_number=2;

%}

%left OR AND

%union {
    unsigned int ival;
    char *cval;
    PL_NODE *plval;    
}

%token <ival> ARTIST 
%token <ival> ALBUM 
%token <ival> GENRE

%token <ival> IS 
%token <ival> INCLUDES

%token <ival> OR 
%token <ival> AND
%token <ival> NOT

%token <cval> ID

%type <plval> expression
%type <plval> predicate
%type <ival> idtag
%type <ival> boolarg
%type <cval> value
%type <ival> playlist

%%

playlistlist: playlist {}
| playlistlist playlist {}
;

playlist: ID '{' expression '}' { $$ = pl_addplaylist($1, $3); }
;

expression: expression AND expression { $$=pl_newexpr($1,$2,$3); }
| expression OR expression { $$=pl_newexpr($1,$2,$3); }
| '(' expression ')' { $$=$2; }
| predicate
;

predicate: idtag boolarg value { $$=pl_newpredicate($1, $2, $3); }

idtag: ARTIST
| ALBUM
| GENRE
;

boolarg: IS { $$=$1; }
| INCLUDES { $$=$1; }
| NOT boolarg { $$=$2 | 0x80000000; }
;

value: ID
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
    pnew->id=pl_number++;
    pl_smart.next=pnew;

    return 0;
}
