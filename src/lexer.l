%{ 

/* $Id$
 * 
 * Simple playlist lexer
 */

#include <string.h>

#include "playlist.h"
#include "parser.h"

extern int yydebug;

%}

%option yylineno

%%

[\n\t ]+

artist                   { yylval.ival=TOK_ARTIST; return(TOK_ARTIST); }
album                    { yylval.ival=TOK_ALBUM; return(TOK_ALBUM); }
genre                    { yylval.ival=TOK_GENRE; return(TOK_GENRE); }

is |
=                        { yylval.ival=TOK_IS; return(TOK_IS); }

includes |
=~ |
~=                       { yylval.ival=TOK_INCLUDES; return(TOK_INCLUDES); }

or |
\|\|                     { yylval.ival=TOK_OR; return(TOK_OR); }

and |
&&                       { yylval.ival=TOK_AND; return(TOK_AND); }

not |
!                        { yylval.ival=1; return(TOK_NOT); }


\"[A-Za-z0-9 ]*\"        { yylval.cval=strdup(yytext); return(TOK_ID); }

. { return yytext[0]; }

%%

int yywrap(void) {
    return 1;
}

void yyerror(char *msg) {
    printf("\nParser Error:  Line %d: %s\n",yylineno, msg);
    printf("Wish I could be more helpful.  :(\n\n");
    printf("If you know how to generate good yacc errors, please\n");
    printf("Let me know!\n\n");
    exit(0);
}


/*
int main(int argc, char *argv[]) {
    FILE *fin;
    int result;

    yydebug=1;

    fin=fopen(argv[1],"r");
    if(!fin) {
	perror("fopen");
	return 1;
    }
    
    yyin=fin;
    result=yyparse();
    printf("Parsed... result = %d\n", result);

    pl_dump();

    return 0;
}
*/
