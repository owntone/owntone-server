%{ 

/* $Id$
 * Simple playlist lexer
 *
 * Copyright (C) 2003 Ron Pedde (ron@pedde.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
\#.*\n

artist                   { yylval.ival=ARTIST; return(ARTIST); }
album                    { yylval.ival=ALBUM; return(ALBUM); }
genre                    { yylval.ival=GENRE; return(GENRE); }
path                     { yylval.ival=PATH; return(PATH); }
composer                 { yylval.ival=COMPOSER; return(COMPOSER); }
orchestra                { yylval.ival=ORCHESTRA; return(ORCHESTRA); }
conductor                { yylval.ival=CONDUCTOR; return(CONDUCTOR); }
grouping                 { yylval.ival=GROUPING; return(GROUPING); }

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
                           if(yylval.cval[strlen(yylval.cval)-1] == '"')
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
    DPRINTF(ERR_LOG,"Playlist error, line %d: %s\n",yylineno, msg);
}


