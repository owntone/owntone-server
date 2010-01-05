/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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

grammar RSP;

options {
	output = AST;
	ASTLabelType = pANTLR3_BASE_TREE;
	language = C;
}

query	:	expr NEWLINE? EOF		->	expr
	;
	
expr	:	aexpr (OR^ aexpr)*
	;

aexpr	:	crit (AND^ crit)*
	;

crit	:	LPAR expr RPAR			->	expr
	|	strcrit
	|	intcrit
	|	datecrit
	;

strcrit	:	FIELD strop STR			->	^(strop FIELD STR)
	|	FIELD NOT strop STR		->	^(NOT ^(strop FIELD STR))
	;

strop	:	EQUAL
	|	INCLUDES
	|	STARTSW
	|	ENDSW
	;

intcrit	:	FIELD intop INT			->	^(intop FIELD INT)
	|	FIELD NOT intop INT		->	^(NOT ^(intop FIELD INT))
	;

intop	:	EQUAL
	|	LESS
	|	GREATER
	|	LTE
	|	GTE
	;

datecrit:	FIELD dateop datespec		->	^(dateop FIELD datespec)
	;

dateop	:	BEFORE
	|	AFTER
	;

datespec:	dateref
	|	INT dateintval dateop dateref	->	^(dateop dateref INT dateintval)
	;

dateref	:	DATE
	|	TODAY
	;

dateintval
	:	DAY
	|	WEEK
	|	MONTH
	|	YEAR
	;

QUOTE	:	'"';
LPAR	:	'(';
RPAR	:	')';

AND	:	'and';
OR	:	'or';
NOT	:	'!';

/* Both string & int */
EQUAL	:	'=';

/* String */
INCLUDES:	'includes';
STARTSW	:	'startswith';
ENDSW	:	'endswith';

/* Int */
GREATER	:	'>';
LESS	:	'<';
GTE	:	'>=';
LTE	:	'<=';

/* Date */
BEFORE	:	'before';
AFTER	:	'after';
DAY	:	'day'	| 'days';
WEEK	:	'week'	| 'weeks';
MONTH	:	'month'	| 'months';
YEAR	:	'year'	| 'years';
TODAY	:	'today';

NEWLINE	:	'\r'? '\n';

WS	:	(' ' | '\t') { $channel = HIDDEN; };

FIELD	:	'a'..'z' ('a'..'z' | '_')* 'a'..'z';

INT	:	DIGIT19 DIGIT09*;

/* YYYY-MM-DD */
DATE	:	DIGIT19 DIGIT09 DIGIT09 DIGIT09 '-' ('0' DIGIT19 | '1' '0'..'2') '-' ('0' DIGIT19 | '1'..'2' DIGIT09 | '3' '0'..'1');

/*
Unescaping adapted from (ported to the C runtime)
<http://stackoverflow.com/questions/504402/how-to-handle-escape-sequences-in-string-literals-in-antlr-3>
*/
STR
@init{ pANTLR3_STRING unesc = GETTEXT()->factory->newRaw(GETTEXT()->factory); }
	:	QUOTE ( reg = ~('\\' | '"') { unesc->addc(unesc, reg); }
			| esc = ESCAPED { unesc->appendS(unesc, GETTEXT()); } )+ QUOTE { SETTEXT(unesc); }
	;

fragment
ESCAPED	:	'\\'
		( '\\' { SETTEXT(GETTEXT()->factory->newStr8(GETTEXT()->factory, (pANTLR3_UINT8)"\\")); }
		| '"' { SETTEXT(GETTEXT()->factory->newStr8(GETTEXT()->factory, (pANTLR3_UINT8)"\"")); }
		)
	;

fragment
DIGIT09	:	'0'..'9';

fragment
DIGIT19	:	'1'..'9';
