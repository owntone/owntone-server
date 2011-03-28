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

tree grammar RSP2SQL;

options {
	tokenVocab = RSP;
	ASTLabelType = pANTLR3_BASE_TREE;
	language = C;
}

@header {
	/* Needs #define _GNU_SOURCE for strptime() */

	#include <stdio.h>
	#include <string.h>
	#include <time.h>
	#include <stdint.h>

	#include "logger.h"
	#include "db.h"
	#include "misc.h"
	#include "rsp_query.h"
}

@members {
	#define RSP_TYPE_STRING 0
	#define RSP_TYPE_INT    1
	#define RSP_TYPE_DATE   2

	struct rsp_query_field_map {
	  char *rsp_field;
	  int field_type;
	  /* RSP fields are named after the DB columns - or vice versa */
	};

	/* gperf static hash, rsp_query.gperf */
	#include "rsp_query_hash.c"
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
	:	^(AND a = expr b = expr)
		{
			if (!$a.valid || !$b.valid)
			{
				$valid = 0;
			}
			else
			{
				$result = $a.result->factory->newRaw($a.result->factory);
				$result->append8($result, "(");
				$result->appendS($result, $a.result);
				$result->append8($result, " AND ");
				$result->appendS($result, $b.result);
				$result->append8($result, ")");
			}
		}
	|	^(OR a = expr b = expr)
		{
			if (!$a.valid || !$b.valid)
			{
				$valid = 0;
			}
			else
			{
				$result = $a.result->factory->newRaw($a.result->factory);
				$result->append8($result, "(");
				$result->appendS($result, $a.result);
				$result->append8($result, " OR ");
				$result->appendS($result, $b.result);
				$result->append8($result, ")");
			}
		}
	|	c = strcrit
		{
			$valid = $c.valid;
			$result = $c.result;
		}
	|	^(NOT c = strcrit)
		{
			if (!$c.valid)
			{
				$valid = 0;
			}
			else
			{
				$result = $c.result->factory->newRaw($c.result->factory);
				$result->append8($result, "(NOT ");
				$result->appendS($result, $c.result);
				$result->append8($result, ")");
			}
		}
	|	i = intcrit
		{
			$valid = $i.valid;
			$result = $i.result;
		}
	|	^(NOT i = intcrit)
		{
			if (!$i.valid)
			{
				$valid = 0;
			}
			else
			{
				$result = $i.result->factory->newRaw($i.result->factory);
				$result->append8($result, "(NOT ");
				$result->appendS($result, $i.result);
				$result->append8($result, ")");
			}
		}
	|	d = datecrit
		{
			$valid = $d.valid;
			$result = $d.result;
		}
	;

strcrit	returns [ pANTLR3_STRING result, int valid ]
@init { $result = NULL; $valid = 1; }
	:	^(o = strop f = FIELD s = STR)
		{
			char *op;
			const struct rsp_query_field_map *rqfp;
			pANTLR3_STRING field;
			char *escaped;
			ANTLR3_UINT32 optok;

			escaped = NULL;

			op = NULL;
			optok = $o.op->getType($o.op);
			switch (optok)
			{
				case EQUAL:
					op = " = ";
					break;

				case INCLUDES:
				case STARTSW:
				case ENDSW:
					op = " LIKE ";
					break;
			}

			field = $f->getText($f);

			/* Field lookup */
			rqfp = rsp_query_field_lookup((char *)field->chars, strlen((char *)field->chars));
			if (!rqfp)
			{
				DPRINTF(E_LOG, L_RSP, "Field '\%s' is not a valid field in queries\n", field->chars);
				$valid = 0;
				goto strcrit_valid_0; /* ABORT */
			}

			/* Check field type */
			if (rqfp->field_type != RSP_TYPE_STRING)
			{
				DPRINTF(E_LOG, L_RSP, "Field '\%s' is not a string field\n", field->chars);
				$valid = 0;
				goto strcrit_valid_0; /* ABORT */
			}

			escaped = db_escape_string((char *)$s->getText($s)->chars);
			if (!escaped)
			{
				DPRINTF(E_LOG, L_RSP, "Could not escape value\n");
				$valid = 0;
				goto strcrit_valid_0; /* ABORT */
			}

			$result = field->factory->newRaw(field->factory);
			$result->append8($result, "f.");
			$result->appendS($result, field);
			$result->append8($result, op);
			$result->append8($result, "'");
			if ((optok == INCLUDES) || (optok == STARTSW))
				$result->append8($result, "\%");

			$result->append8($result, escaped);

			if ((optok == INCLUDES) || (optok == ENDSW))
				$result->append8($result, "\%");
			$result->append8($result, "'");

			strcrit_valid_0:
				;

			if (escaped)
				free(escaped);
		}
	;

strop	returns [ pANTLR3_COMMON_TOKEN op ]
@init { $op = NULL; }
	:	n = EQUAL
		{ $op = $n->getToken($n); }
	|	n = INCLUDES
		{ $op = $n->getToken($n); }
	|	n = STARTSW
		{ $op = $n->getToken($n); }
	|	n = ENDSW
		{ $op = $n->getToken($n); }
	;

intcrit	returns [ pANTLR3_STRING result, int valid ]
@init { $result = NULL; $valid = 1; }
	:	^(o = intop f = FIELD i = INT)
		{
			char *op;
			const struct rsp_query_field_map *rqfp;
			pANTLR3_STRING field;

			op = NULL;
			switch ($o.op->getType($o.op))
			{
				case EQUAL:
					op = " = ";
					break;

				case LESS:
					op = " < ";
					break;

				case GREATER:
					op = " > ";
					break;

				case LTE:
					op = " <= ";
					break;

				case GTE:
					op = " >= ";
					break;
			}

			field = $f->getText($f);

			/* Field lookup */
			rqfp = rsp_query_field_lookup((char *)field->chars, strlen((char *)field->chars));
			if (!rqfp)
			{
				DPRINTF(E_LOG, L_RSP, "Field '\%s' is not a valid field in queries\n", field->chars);
				$valid = 0;
				goto intcrit_valid_0; /* ABORT */
			}

			/* Check field type */
			if (rqfp->field_type != RSP_TYPE_INT)
			{
				DPRINTF(E_LOG, L_RSP, "Field '\%s' is not an integer field\n", field->chars);
				$valid = 0;
				goto intcrit_valid_0; /* ABORT */
			}

			$result = field->factory->newRaw(field->factory);
			$result->append8($result, "f.");
			$result->appendS($result, field);
			$result->append8($result, op);
			$result->appendS($result, $i->getText($i));

			intcrit_valid_0:
				;
		}
	;

intop	returns [ pANTLR3_COMMON_TOKEN op ]
@init { $op = NULL; }
	:	n = EQUAL
		{ $op = $n->getToken($n); }
	|	n = LESS
		{ $op = $n->getToken($n); }
	|	n = GREATER
		{ $op = $n->getToken($n); }
	|	n = LTE
		{ $op = $n->getToken($n); }
	|	n = GTE
		{ $op = $n->getToken($n); }
	;

datecrit	returns [ pANTLR3_STRING result, int valid ]
@init { $result = NULL; $valid = 1; }
	:	^(o = dateop f = FIELD d = datespec)
		{
			char *op;
			const struct rsp_query_field_map *rqfp;
			pANTLR3_STRING field;
			char buf[32];
			int ret;

			op = NULL;
			switch ($o.op->getType($o.op))
			{
				case BEFORE:
					op = " < ";
					break;

				case AFTER:
					op = " > ";
					break;
			}

			field = $f->getText($f);

			/* Field lookup */
			rqfp = rsp_query_field_lookup((char *)field->chars, strlen((char *)field->chars));
			if (!rqfp)
			{
				DPRINTF(E_LOG, L_RSP, "Field '\%s' is not a valid field in queries\n", field->chars);
				$valid = 0;
				goto datecrit_valid_0; /* ABORT */
			}

			/* Check field type */
			if (rqfp->field_type != RSP_TYPE_DATE)
			{
				DPRINTF(E_LOG, L_RSP, "Field '\%s' is not a date field\n", field->chars);
				$valid = 0;
				goto datecrit_valid_0; /* ABORT */
			}

			ret = snprintf(buf, sizeof(buf), "\%ld", $d.date);
			if ((ret < 0) || (ret >= sizeof(buf)))
			{
				DPRINTF(E_LOG, L_RSP, "Date \%ld too large for buffer, oops!\n", $d.date);
				$valid = 0;
				goto datecrit_valid_0; /* ABORT */
			}

			$result = field->factory->newRaw(field->factory);
			$result->append8($result, "f.");
			$result->appendS($result, field);
			$result->append8($result, op);
			$result->append8($result, buf);

			datecrit_valid_0:
				;
		}
	;

dateop	returns [ pANTLR3_COMMON_TOKEN op ]
@init { $op = NULL; }
	:	n = BEFORE
		{ $op = $n->getToken($n); }
	|	n = AFTER
		{ $op = $n->getToken($n); }
	;

datespec	returns [ time_t date, int valid ]
@init { $date = 0; $valid = 1; }
	:	r = dateref
		{
			if (!$r.valid)
				$valid = 0;
			else
				$date = $r.date;
		}
	|	^(o = dateop r = dateref m = INT i = dateintval)
		{
			int32_t val;
			int ret;

			if (!$r.valid || !$i.valid)
			{
				$valid = 0;
				goto datespec_valid_0; /* ABORT */
			}

			ret = safe_atoi32((char *)$m->getText($m)->chars, &val);
			if (ret < 0)
			{
				DPRINTF(E_LOG, L_RSP, "Could not convert '\%s' to integer\n", (char *)$m->getText($m));
				$valid = 0;
				goto datespec_valid_0; /* ABORT */
			}

			switch ($o.op->getType($o.op))
			{
				case BEFORE:
					$date = $r.date - (val * $i.period);
					break;

				case AFTER:
					$date = $r.date + (val * $i.period);
					break;
			}

			datespec_valid_0:
				;
		}
	;

dateref	returns [ time_t date, int valid ]
@init { $date = 0; $valid = 1; }
	:	n = DATE
		{
			struct tm tm;
			char *ret;

			ret = strptime((char *)$n->getText($n), "\%Y-\%m-\%d", &tm);
			if (!ret)
			{
				DPRINTF(E_LOG, L_RSP, "Date '\%s' could not be interpreted\n", (char *)$n->getText($n));
				$valid = 0;
				goto dateref_valid_0; /* ABORT */
			}
			else
			{
				if (*ret != '\0')
					DPRINTF(E_LOG, L_RSP, "Garbage at end of date '\%s' ?!\n", (char *)$n->getText($n));

				$date = mktime(&tm);
				if ($date == (time_t) -1)
				{
					DPRINTF(E_LOG, L_RSP, "Date '\%s' could not be converted to an epoch\n", (char *)$n->getText($n));
					$valid = 0;
					goto dateref_valid_0; /* ABORT */
				}
			}

			dateref_valid_0:
				;
		}
	|	TODAY
		{ $date = time(NULL); }
	;

dateintval	returns [ time_t period, int valid ]
@init { $period = 0; $valid = 1; }
	:	DAY
		{ $period = 24 * 60 * 60; }
	|	WEEK
		{ $period = 7 * 24 * 60 * 60; }
	|	MONTH
		{ $period = 30 * 24 * 60 * 60; }
	|	YEAR
		{ $period = 365 * 24 * 60 * 60; }
	;
