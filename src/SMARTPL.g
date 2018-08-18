/*
 * Copyright (C) 2015 Christian Meffert <christian.meffert@googlemail.com>
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

grammar SMARTPL;

options {
	output = AST;
	ASTLabelType = pANTLR3_BASE_TREE;
	language = C;
}

playlist	:	STR '{' expression '}' EOF
			;

expression	:	aexpr (OR^ aexpr)* (HAVING^ aexpr)? (ORDERBY^ orderexpr)? (LIMIT^ limitexpr)?
			;

orderexpr	:	ordertag SORTDIR
			;

ordertag	:	STRTAG
			|	INTTAG
			|	DATETAG
			|	ENUMTAG
			|	(XXX)? NeverUsedRule
			;

NeverUsedRule: /* antlr3 seems to have a problem with ordertag, introducing the NeverUsedRule fixes it. See: https://stackoverflow.com/questions/20057063/follow-set-in-is-undefined-in-generated-parser */
			;

XXX			:	'XXX' /**/
			;

limitexpr	:	INT
			;

aexpr		:	nexpr (AND^ nexpr)*
			;

nexpr		:	NOT^ crit
			|	crit
			;

crit		:	LPAR expression RPAR	->	expression
			|	STRTAG (INCLUDES|IS|STARTSWITH) STR
			|	INTTAG INTBOOL INT
			|	DATETAG	(AFTER|BEFORE) dateval
			|	ENUMTAG IS ENUMVAL
			|	GROUPTAG INTBOOL INT
			;

dateval		:	DATE
			|	interval BEFORE DATE
			|	interval AFTER DATE
			|	interval AGO
			;

interval	:	INT DATINTERVAL
			;

STRTAG		:	'artist'
			|	'album_artist'
			|	'album'
			|	'title'
			|	'genre'
			|	'composer'
			|	'path'
			|	'type'
			|	'grouping'
			;

INTTAG		:	'play_count'
			|	'skip_count'
			|	'rating'
			|	'year'
			|	'compilation'
			;

DATETAG		:	'time_added'
			|	'time_modified'
			|	'time_played'
			|	'time_skipped'
			;

ENUMTAG		:	'data_kind'
			|	'media_kind'
			;

GROUPTAG	:	'track_count'
			|	'album_count'
			;

INCLUDES	:	'includes'
			;

IS			:	'is'
			;

STARTSWITH	:	'starts with'
			;

INTBOOL		:	(GREATER|GREATEREQUAL|LESS|LESSEQUAL|EQUAL)
			;

fragment
GREATER		:	'>'
			;

fragment
GREATEREQUAL:	'>='
			;

fragment
LESS		:	'<'
			;

fragment
LESSEQUAL	:	'<='
			;

fragment
EQUAL		:	'='
			;

AFTER		:	'after'
			;

BEFORE		:	'before'
			;

AGO			:	'ago'
			;

AND			:	'AND'
			|	'and'
			;

OR			:	'OR'
			|	'or'
			;

NOT			:	'NOT'
			|	'not'
			;

LPAR		:	'('
			;

RPAR		:	')'
			;

DATE		:	('0'..'9')('0'..'9')('0'..'9')('0'..'9')'-'('0'..'1')('0'..'9')'-'('0'..'3')('0'..'9')
			|	'today'
			|	'yesterday'
			|	'last week'
			|	'last month'
			|	'last year'
			;

DATINTERVAL	:	'days'
			|	'weeks'
			|	'months'
			|	'years'
			;

ENUMVAL		:	'music'
			|	'movie'
			|	'podcast'
			|	'audiobook'
			|	'tvshow'
			|	'file'
			|	'url'
			|	'spotify'
			|	'pipe'
			;

ORDERBY		:	'order by'
			|	'ORDER BY'
			;

SORTDIR		:	'asc'
			|	'ASC'
			|	'desc'
			|	'DESC'
			;

LIMIT		:	'limit'
			|	'LIMIT'
			;

HAVING		:	'having'
			|	'HAVING'
			;

STR			:	'"' ~('"')+ '"'
			;

INT			:	('0'..'9')+
			;

WHITESPACE	:	('\t'|' '|'\r'|'\n'|'\u000C') { $channel = HIDDEN; }
			;


