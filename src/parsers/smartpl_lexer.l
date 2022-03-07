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
%option prefix="smartpl_"

/* Automake's ylwrap expexts the output to have this name */
%option outfile="lex.yy.c"

/* Makes a Bison-compatible yylex */
%option bison-bridge

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "smartpl_parser.h"

/* Unknown why this is required despite using prefix */
#define YYSTYPE SMARTPL_STYPE
%}


/* ========================= NON-BOILERPLATE SECTION =========================*/

%option case-insensitive

/* quoted \"(\\(.|\n)|[^\\"\n])*\" */
quoted \"[^\"\n]*[\"\n]
yyyymmdd  [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]

%%

[\n\t ]+       /* Ignore whitespace */
\#.*\n         /* Ignore comments */

artist         { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
album_artist   { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
album          { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
title          { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
genre          { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
composer       { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
path           { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
type           { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
grouping       { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
artist_id      { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
songartistid   { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
songalbumid    { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
codectype      { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }
comment        { yylval->str = strdup(yytext); return SMARTPL_T_STRTAG; }

play_count     { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
skip_count     { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
rating         { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
year           { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
compilation    { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
track          { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
disc           { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
bitrate        { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
bits_per_sample { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
samplerate     { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
song_length    { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }
usermark       { yylval->str = strdup(yytext); return SMARTPL_T_INTTAG; }

time_added     { yylval->str = strdup(yytext); return SMARTPL_T_DATETAG; }
time_modified  { yylval->str = strdup(yytext); return SMARTPL_T_DATETAG; }
time_played    { yylval->str = strdup(yytext); return SMARTPL_T_DATETAG; }
time_skipped   { yylval->str = strdup(yytext); return SMARTPL_T_DATETAG; }
date_released  { yylval->str = strdup(yytext); return SMARTPL_T_DATETAG; }

track_count    { yylval->str = strdup(yytext); return SMARTPL_T_GROUPTAG; }
album_count    { yylval->str = strdup(yytext); return SMARTPL_T_GROUPTAG; }

data_kind      { yylval->str = strdup(yytext); return SMARTPL_T_ENUMTAG_DATAKIND; }
media_kind     { yylval->str = strdup(yytext); return SMARTPL_T_ENUMTAG_MEDIAKIND; }
scan_kind      { yylval->str = strdup(yytext); return SMARTPL_T_ENUMTAG_SCANKIND; }

files?         { return SMARTPL_T_FILE; }
url            { return SMARTPL_T_URL; }
spotify        { return SMARTPL_T_SPOTIFY; }
pipe           { return SMARTPL_T_PIPE; }
rss            { return SMARTPL_T_RSS; }

music          { return SMARTPL_T_MUSIC; }
movie          { return SMARTPL_T_MOVIE; }
podcast        { return SMARTPL_T_PODCAST; }
audiobook      { return SMARTPL_T_AUDIOBOOK; }
tvshow         { return SMARTPL_T_TVSHOW; }

having         { return SMARTPL_T_HAVING; }
order\ by      { return SMARTPL_T_ORDERBY; }
random         { return SMARTPL_T_RANDOM; }
desc           { return SMARTPL_T_ORDER_DESC; }
asc            { return SMARTPL_T_ORDER_ASC; }
limit          { return SMARTPL_T_LIMIT; }

{yyyymmdd}     { yylval->str = strdup(yytext); return SMARTPL_T_DATE; }
today          { return (yylval->ival = SMARTPL_T_DATE_TODAY); }
yesterday      { return (yylval->ival = SMARTPL_T_DATE_YESTERDAY); }
this\ week     { return (yylval->ival = SMARTPL_T_DATE_THISWEEK); }
last\ week     { return (yylval->ival = SMARTPL_T_DATE_LASTWEEK); }
last\ month    { return (yylval->ival = SMARTPL_T_DATE_LASTMONTH); }
last\ year     { return (yylval->ival = SMARTPL_T_DATE_LASTYEAR); }

days?          { return SMARTPL_T_DAYS; }
weeks?         { return SMARTPL_T_WEEKS; }
months?        { return SMARTPL_T_MONTHS; }
years?         { return SMARTPL_T_YEARS; }

ago            { return (yylval->ival = SMARTPL_T_AGO); }
before         { return (yylval->ival = SMARTPL_T_BEFORE); }
after          { return (yylval->ival = SMARTPL_T_AFTER); }

is             { return (yylval->ival = SMARTPL_T_IS); }
includes       { return (yylval->ival = SMARTPL_T_INCLUDES); }
starts\ with   { return (yylval->ival = SMARTPL_T_STARTSWITH); }
ends\ with     { return (yylval->ival = SMARTPL_T_ENDSWITH); }
=              { return (yylval->ival = SMARTPL_T_EQUAL); }
\<=            { return (yylval->ival = SMARTPL_T_LESSEQUAL); }
\<             { return (yylval->ival = SMARTPL_T_LESS); }
\>=            { return (yylval->ival = SMARTPL_T_GREATEREQUAL); }
\>             { return (yylval->ival = SMARTPL_T_GREATER); }

or             { return SMARTPL_T_OR; }
and            { return SMARTPL_T_AND; }
not            { return SMARTPL_T_NOT; }

{quoted}       { yylval->str=strdup(yytext+1);
                 if(yylval->str[strlen(yylval->str)-1] == '"')
                   yylval->str[strlen(yylval->str)-1] = '\0';
                 return SMARTPL_T_STRING; }

[0-9]+         { yylval->ival=atoi(yytext); return SMARTPL_T_NUM; }

. { return yytext[0]; }

%%

