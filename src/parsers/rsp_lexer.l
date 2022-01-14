/*
 * Copyright (C) 2021-2022 Espen Jürgensen <espenjurgensen@gmail.com>
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

/* =========================== BOILERPLATE SECTION ===========================*/

/* This is to avoid compiler warnings about unused functions. More options are
   noyyalloc noyyrealloc noyyfree. */
%option noyywrap nounput noinput

/* Thread safe scanner */
%option reentrant

/* To avoid symbol name conflicts with multiple lexers */
%option prefix="rsp_"

/* Automake's ylwrap expexts the output to have this name */
%option outfile="lex.yy.c"

/* Makes a Bison-compatible yylex */
%option bison-bridge

%{
#include <stdio.h>
#include "rsp_parser.h"

/* Unknown why this is required despite using prefix */
#define YYSTYPE RSP_STYPE

%}

/* ========================= NON-BOILERPLATE SECTION =========================*/

/* quoted  \"(\\.|[^\\"])*\" */
quoted \"(\\.|[^"])+\"
yyyymmdd  [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]

%%

[\n\t ]+       /* Ignore whitespace */

artist         { yylval->str = strdup(yytext); return RSP_T_STRTAG; }
album_artist   { yylval->str = strdup(yytext); return RSP_T_STRTAG; }
album          { yylval->str = strdup(yytext); return RSP_T_STRTAG; }
title          { yylval->str = strdup(yytext); return RSP_T_STRTAG; }
genre          { yylval->str = strdup(yytext); return RSP_T_STRTAG; }
composer       { yylval->str = strdup(yytext); return RSP_T_STRTAG; }

id             { yylval->str = strdup(yytext); return RSP_T_INTTAG; }

includes       { return RSP_T_INCLUDES; }
=              { return RSP_T_EQUAL; }

or             { return RSP_T_OR; }
and            { return RSP_T_AND; }
not            { return RSP_T_NOT; }

{quoted}       { yylval->str=strdup(yytext+1);
                 if(yylval->str[strlen(yylval->str)-1] == '"')
                   yylval->str[strlen(yylval->str)-1] = '\0';
                 return RSP_T_STRING; }

[0-9]+         { yylval->ival=atoi(yytext); return RSP_T_NUM; }

.              { return yytext[0]; }

%%

