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
%option prefix="mpd_"

/* Automake's ylwrap expexts the output to have this name */
%option outfile="lex.yy.c"

/* Makes a Bison-compatible yylex */
%option bison-bridge

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mpd_parser.h"

/* Unknown why this is required despite using prefix */
#define YYSTYPE MPD_STYPE
%}


/* ========================= NON-BOILERPLATE SECTION =========================*/

%option case-insensitive

singlequoted '(\\.|[^'\\])*'
doublequoted \"(\\.|[^"\\])*\"
yyyymmdd  [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]

%%

[\n\r\t ]+     /* Ignore whitespace */

^playlistfind    { return MPD_T_CMDPLAYLISTFIND; }
^playlistsearch  { return MPD_T_CMDPLAYLISTSEARCH; }
^count           { return MPD_T_CMDCOUNT; }
^find            { return MPD_T_CMDFIND; }
^findadd         { return MPD_T_CMDFINDADD; }
^list            { return MPD_T_CMDLIST; }
^search          { return MPD_T_CMDSEARCH; }
^searchadd       { return MPD_T_CMDSEARCHADD; }
^searchcount     { return MPD_T_CMDSEARCHCOUNT; }

sort             { return MPD_T_SORT; }
window           { return MPD_T_WINDOW; }
position         { return MPD_T_POSITION; }
group            { return MPD_T_GROUP; }

artist           { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
artistsort       { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
albumartist      { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
albumartistsort  { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
album            { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
albumsort        { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
title            { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
titlesort        { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
composer         { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
composersort     { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
genre            { yylval->str = strdup(yytext); return MPD_T_STRTAG; }
file             { yylval->str = strdup(yytext); return MPD_T_STRTAG; }

base             { yylval->str = strdup(yytext); return MPD_T_BASETAG; }

track            { yylval->str = strdup(yytext); return MPD_T_INTTAG; }
disc             { yylval->str = strdup(yytext); return MPD_T_INTTAG; }
date             { yylval->str = strdup(yytext); return MPD_T_INTTAG; }

modified-since   { yylval->str = strdup(yytext); return MPD_T_SINCETAG; }
added-since      { yylval->str = strdup(yytext); return MPD_T_SINCETAG; }

contains         { return (yylval->ival = MPD_T_CONTAINS); }
starts_with      { return (yylval->ival = MPD_T_STARTSWITH); }
ends_with        { return (yylval->ival = MPD_T_ENDSWITH); }
==               { return (yylval->ival = MPD_T_EQUAL); }
!=               { return (yylval->ival = MPD_T_NOTEQUAL); }
\<=              { return (yylval->ival = MPD_T_LESSEQUAL); }
\<               { return (yylval->ival = MPD_T_LESS); }
\>=              { return (yylval->ival = MPD_T_GREATEREQUAL); }
\>               { return (yylval->ival = MPD_T_GREATER); }

audioformat      { return MPD_T_AUDIOFORMATTAG; }
any              { return MPD_T_ANYTAG; }

or               { return MPD_T_OR; }
and              { return MPD_T_AND; }
not              { return MPD_T_NOT; }
!                { return MPD_T_NOT; }

{singlequoted}   { yylval->str = mpd_parser_quoted(yytext); return MPD_T_STRING; }
{doublequoted}   { yylval->str = mpd_parser_quoted(yytext); return MPD_T_STRING; }

[0-9]+           { yylval->ival=atoi(yytext); return MPD_T_NUM; }

. { return yytext[0]; }

%%

