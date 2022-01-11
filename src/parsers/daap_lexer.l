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
%option prefix="daap_"

/* Automake's ylwrap expexts the output to have this name */
%option outfile="lex.yy.c"

/* Makes a Bison-compatible yylex */
%option bison-bridge

%{
#include <stdio.h>
#include "daap_parser.h"

/* Unknown why this is required despite using prefix */
#define YYSTYPE DAAP_STYPE

%}

/* ========================= NON-BOILERPLATE SECTION =========================*/

re_quote    '
re_key      [[:alnum:]\.\-]+
re_value    (\\.|[^'])+
re_operator (!?[:@])

%x IN_CRITERIA IN_CRITERIA_VALUE

%%

{re_quote} { BEGIN IN_CRITERIA; return DAAP_T_QUOTE; }

<IN_CRITERIA>{re_key}/{re_operator} { yylval->str = strdup(yytext); return DAAP_T_KEY; }
<IN_CRITERIA>{re_operator} { BEGIN IN_CRITERIA_VALUE; return (*yytext == '!' ? DAAP_T_NOT : DAAP_T_EQUAL); }
<IN_CRITERIA>. { return *yytext; }

<IN_CRITERIA_VALUE>\*{re_value}\*/{re_quote} { yylval->str = strdup(yytext); return DAAP_T_WILDCARD; }
<IN_CRITERIA_VALUE>{re_value}/{re_quote} { yylval->str = strdup(yytext); return DAAP_T_VALUE; }
<IN_CRITERIA_VALUE>{re_quote} { BEGIN INITIAL; return DAAP_T_QUOTE; }
<IN_CRITERIA_VALUE>. { return *yytext; }

"+"|" "         { return DAAP_T_AND; }
","             { return DAAP_T_OR; }
"\r"?"\n"       { return DAAP_T_NEWLINE; }
.               { return *yytext; }

%%

