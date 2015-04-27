/*
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
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

tree grammar DAAP2SQL;

options {
	tokenVocab = DAAP;
	ASTLabelType = pANTLR3_BASE_TREE;
	language = C;
}

@header {
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#include <limits.h>
	#include <errno.h>

	#include "logger.h"
	#include "db.h"
	#include "daap_query.h"
}

@members {
	struct dmap_query_field_map {
	  char *dmap_field;
	  char *db_col;
	  int as_int;
	};

	/* gperf static hash, daap_query.gperf */
	#include "daap_query_hash.c"
}

query	returns [ pANTLR3_STRING result ]
@init { $result = NULL; }
	:	e = expr
		{
			if (!$e.valid)
			{
				$result = NULL;
			}
			else
			{
				$result = $e.result->factory->newRaw($e.result->factory);
				$result->append8($result, "(");
				$result->appendS($result, $e.result);
				$result->append8($result, ")");
			}
		}
	;

expr	returns [ pANTLR3_STRING result, int valid ]
@init { $result = NULL; $valid = 1; }
	:	^(OPAND a = expr b = expr)
		{
			if ($a.valid && $b.valid)
			{
				$result = $a.result->factory->newRaw($a.result->factory);
				$result->append8($result, "(");
				$result->appendS($result, $a.result);
				$result->append8($result, " AND ");
				$result->appendS($result, $b.result);
				$result->append8($result, ")");
			}
			else if ($a.valid)
			{
				$result = $a.result->factory->newRaw($a.result->factory);
				$result->appendS($result, $a.result);
			}
			else if ($b.valid)
			{
				$result = $b.result->factory->newRaw($b.result->factory);
				$result->appendS($result, $b.result);
			}
			else
			{
				$valid = 0;
			}
		}
	|	^(OPOR a = expr b = expr)
		{
			if ($a.valid && $b.valid)
			{
				$result = $a.result->factory->newRaw($a.result->factory);
				$result->append8($result, "(");
				$result->appendS($result, $a.result);
				$result->append8($result, " OR ");
				$result->appendS($result, $b.result);
				$result->append8($result, ")");
			}
			else if ($a.valid)
			{
				$result = $a.result->factory->newRaw($a.result->factory);
				$result->appendS($result, $a.result);
			}
			else if ($b.valid)
			{
				$result = $b.result->factory->newRaw($b.result->factory);
				$result->appendS($result, $b.result);
			}
			else
			{
				$valid = 0;
			}
		}
	|	STR
		{
			pANTLR3_STRING str;
			pANTLR3_UINT8 field;
			pANTLR3_UINT8 val;
			pANTLR3_UINT8 escaped;
			ANTLR3_UINT8 op;
			int neg_op;
			const struct dmap_query_field_map *dqfm;
			char *end;
			long long llval;

			escaped = NULL;

			$result = $STR.text->factory->newRaw($STR.text->factory);

			str = $STR.text->toUTF8($STR.text);

			/* NOTE: the lexer delivers the string without quotes
			which may not be obvious from the grammar due to embedded code
			*/

			/* Make daap.songalbumid:0 a no-op */
			if (strcmp((char *)str->chars, "daap.songalbumid:0") == 0)
			{
				$result->append8($result, "1 = 1");

				goto STR_out;
			}

			field = str->chars;

			val = field;
			while ((*val != '\0') && ((*val == '.')
				|| (*val == '-')
				|| ((*val >= 'a') && (*val <= 'z'))
				|| ((*val >= 'A') && (*val <= 'Z'))
				|| ((*val >= '0') && (*val <= '9'))))
			{
				val++;
			}

			if (*field == '\0')
			{
				DPRINTF(E_LOG, L_DAAP, "No field name found in clause '\%s'\n", field);
				$valid = 0;
				goto STR_result_valid_0; /* ABORT */
			}

			if (*val == '\0')
			{
				DPRINTF(E_LOG, L_DAAP, "No operator found in clause '\%s'\n", field);
				$valid = 0;
				goto STR_result_valid_0; /* ABORT */
			}

			op = *val;
			*val = '\0';
			val++;

			if (op == '!')
			{
				if (*val == '\0')
				{
					DPRINTF(E_LOG, L_DAAP, "Negation found but operator missing in clause '\%s\%c'\n", field, op);
					$valid = 0;
					goto STR_result_valid_0; /* ABORT */
				}

				neg_op = 1;
				op = *val;
				val++;
			}
			else
				neg_op = 0;

			/* Lookup DMAP field in the query field map */
			dqfm = daap_query_field_lookup((char *)field, strlen((char *)field));
			if (!dqfm)
			{
				DPRINTF(E_LOG, L_DAAP, "DMAP field '\%s' is not a valid field in queries\n", field);
				$valid = 0;
				goto STR_result_valid_0; /* ABORT */
			}

			/* Empty values OK for string fields, NOK for integer */
			if (*val == '\0')
			{
				if (dqfm->as_int)
				{
					DPRINTF(E_LOG, L_DAAP, "No value given in clause '\%s\%s\%c'\n", field, (neg_op) ? "!" : "", op);
					$valid = 0;
					goto STR_result_valid_0; /* ABORT */
				}

				/* No need to exclude empty artist and album, as forked-daapd makes sure there always exists an artist/album. */
				if (neg_op && op == ':'
					&& (strcmp((char *)field, "daap.songalbumartist") == 0 
						|| strcmp((char *)field, "daap.songartist") == 0 
						|| strcmp((char *)field, "daap.songalbum") == 0))
				{
					DPRINTF(E_DBG, L_DAAP, "Ignoring clause '\%s\%s\%c'\n", field, (neg_op) ? "!" : "", op);
					$valid = 0;
					goto STR_result_valid_0;
				}
				
				/* Need to check against NULL too */
				if (op == ':')
					$result->append8($result, "(");
			}

			/* Int field: check integer conversion */
			if (dqfm->as_int)
			{
				errno = 0;
				llval = strtoll((const char *)val, &end, 10);

				if (((errno == ERANGE) && ((llval == LLONG_MAX) || (llval == LLONG_MIN)))
					|| ((errno != 0) && (llval == 0)))
				{
					DPRINTF(E_LOG, L_DAAP, "Value '\%s' in clause '\%s\%s\%c\%s' does not convert to an integer type\n",
					val, field, (neg_op) ? "!" : "", op, val);
					$valid = 0;
					goto STR_result_valid_0; /* ABORT */
				}

				if (end == (char *)val)
				{
					DPRINTF(E_LOG, L_DAAP, "Value '\%s' in clause '\%s\%s\%c\%s' does not represent an integer value\n",
					val, field, (neg_op) ? "!" : "", op, val);
					$valid = 0;
					goto STR_result_valid_0; /* ABORT */
				}

				*end = '\0'; /* Cut out potential garbage - we're being kind */

				/* forked-daapd only has media_kind = 1 for music - so remove media_kind = 32 to imporve select query performance. */
				if (llval == 32
					&& (strcmp((char *)field, "com.apple.itunes.mediakind") == 0 
						|| strcmp((char *)field, "com.apple.itunes.extended-media-kind") == 0))
				{
					DPRINTF(E_DBG, L_DAAP, "Ignoring clause '\%s\%s\%c\%s'\n", field, (neg_op) ? "!" : "", op, val);
					
					if (neg_op)
						$result->append8($result, "1 = 1");
					else
						$result->append8($result, "1 = 0");
					
					goto STR_out;
				}
			}
			/* String field: escape string, check for '*' */
			else
			{
				if (op != ':')
				{
					DPRINTF(E_LOG, L_DAAP, "Operation '\%c' not valid for string values\n", op);
					$valid = 0;
					goto STR_result_valid_0; /* ABORT */
				}

				escaped = (pANTLR3_UINT8)db_escape_string((char *)val);
				if (!escaped)
				{
					DPRINTF(E_LOG, L_DAAP, "Could not escape value\n");
					$valid = 0;
					goto STR_result_valid_0; /* ABORT */
				}

				val = escaped;

				if (val[0] == '*')
				{
					op = '\%';
					val[0] = '\%';
				}

				if (val[0] && val[1] && val[strlen((char *)val) - 1] == '*')
				{
					op = '\%';
					val[strlen((char *)val) - 1] = '\%';
				}
			}
			
			$result->append8($result, dqfm->db_col);

			switch(op)
			{
				case ':':
					if (neg_op)
						$result->append8($result, " <> ");
					else
						$result->append8($result, " = ");
					break;

				case '+':
					if (neg_op)
						$result->append8($result, " <= ");
					else
						$result->append8($result, " > ");
					break;

				case '-':
					if (neg_op)
						$result->append8($result, " >= ");
					else
						$result->append8($result, " < ");
					break;

				case '\%':
					$result->append8($result, " LIKE ");
					break;

				default:
					if (neg_op)
						DPRINTF(E_LOG, L_DAAP, "Missing or unknown operator '\%c' in clause '\%s!\%c\%s'\n", op, field, op, val);
					else
						DPRINTF(E_LOG, L_DAAP, "Unknown operator '\%c' in clause '\%s\%c\%s'\n", op, field, op, val);
					$valid = 0;
					goto STR_result_valid_0; /* ABORT */
					break;
			}

			if (!dqfm->as_int)
				$result->append8($result, "'");
	
			$result->append8($result, (const char *)val);
	
			if (!dqfm->as_int)
				$result->append8($result, "'");

			/* For empty string value, we need to check against NULL too */
			if ((*val == '\0') && (op == ':'))
			{
				if (neg_op)
					$result->append8($result, " AND ");
				else
					$result->append8($result, " OR ");

				$result->append8($result, dqfm->db_col);

				if (neg_op)
					$result->append8($result, " IS NOT NULL");
				else
					$result->append8($result, " IS NULL");

				$result->append8($result, ")");
			}

			STR_result_valid_0: /* bail out label */
				;

			if (escaped)
				free(escaped);

			STR_out: /* get out of here */
				;
		}
	;
