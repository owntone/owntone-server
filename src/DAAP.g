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

grammar DAAP;

options {
	output = AST;
	ASTLabelType = pANTLR3_BASE_TREE;
	language = C;
}

query	:	expr NEWLINE? EOF	->	expr
	;
	
expr	:	aexpr (OPOR^ aexpr)*
	;

aexpr	:	crit (OPAND^ crit)*
	;

crit	:	LPAR expr RPAR		->	expr
	|	STR
	;

QUOTE	:	'\'';
LPAR	:	'(';
RPAR	:	')';

OPAND	:	'+' | ' ';
OPOR	:	',';

NEWLINE	:	'\r'? '\n';

/*
Unescaping adapted from (ported to the C runtime)
<http://stackoverflow.com/questions/504402/how-to-handle-escape-sequences-in-string-literals-in-antlr-3>
*/
STR
@init{ pANTLR3_STRING unesc = GETTEXT()->factory->newRaw(GETTEXT()->factory); }
	:	QUOTE ( reg = ~('\\' | '\'') { unesc->addc(unesc, reg); }
			| esc = ESCAPED { unesc->appendS(unesc, GETTEXT()); } )+ QUOTE { SETTEXT(unesc); };

fragment
ESCAPED	:	'\\'
		( '\\' { SETTEXT(GETTEXT()->factory->newStr8(GETTEXT()->factory, (pANTLR3_UINT8)"\\")); }
		| '\'' { SETTEXT(GETTEXT()->factory->newStr8(GETTEXT()->factory, (pANTLR3_UINT8)"\'")); }
		)
	;
