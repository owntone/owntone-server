%{ 

/* $Id$
 * 
 * Simple playlist lexer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "err.h"
#include "playlist.h"
#include "parser.h"

extern int yydebug;

%}

%option yylineno
%option case-insensitive

qstring \"[^\"\n]*[\"\n]

%%

[\n\t ]+

artist                   { yylval.ival=ARTIST; return(ARTIST); }
album                    { yylval.ival=ALBUM; return(ALBUM); }
genre                    { yylval.ival=GENRE; return(GENRE); }

year                     { yylval.ival=YEAR; return(YEAR); }

is                       { yylval.ival=IS; return(IS); }
includes                 { yylval.ival=INCLUDES; return(INCLUDES); }
=                        { yylval.ival=EQUALS; return(EQUALS); }

or |
\|\|                     { yylval.ival=OR; return(OR); }

and |
&&                       { yylval.ival=AND; return(AND); }

not |
!                        { yylval.ival=1; return(NOT); }

\<=                      { yylval.ival=LESSEQUAL; return(LESSEQUAL); }
\<                       { yylval.ival=LESS; return(LESS); }
\>=                      { yylval.ival=GREATEREQUAL; return(GREATEREQUAL); }
\>                       { yylval.ival=GREATER; return(GREATER); }

{qstring}                { yylval.cval=strdup(yytext+1);
                           if(yylval.cval[strlen(yylval.cval)-1] == '"');
                               yylval.cval[strlen(yylval.cval)-1] = '\0';
                           return(ID); }

[0-9]+                   { yylval.ival=atoi(yytext); return(NUM); }

. { return yytext[0]; }

%%

int yywrap(void) {
    return 1;
}

void yyerror(char *msg) {
    pl_error=1;
    log_err(0,"Line %d: %s\n",yylineno, msg);
}


