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

tree grammar SMARTPL2SQL;

options {
	tokenVocab = SMARTPL;
	ASTLabelType = pANTLR3_BASE_TREE;
	language = C;
}

@header {
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#include <limits.h>
	#include <errno.h>
	#include <time.h>
	#include <sqlite3.h>

	#include "logger.h"
	#include "db.h"
}

@members {
	static void append_date(pANTLR3_STRING result, const char *datval, char beforeorafter, const char *interval)
	{
		if (strcmp((char *)datval, "today") == 0)
		{
			result->append8(result, "strftime('\%s', datetime('now', 'start of day'");
		}
		else if (strcmp((char *)datval, "yesterday") == 0)
		{
			result->append8(result, "strftime('\%s', datetime('now', 'start of day', '-1 day'");
		}
		else if (strcmp((char *)datval, "last week") == 0)
		{
			result->append8(result, "strftime('\%s', datetime('now', 'start of day', 'weekday 0', '-13 days'");
		}
		else if (strcmp((char *)datval, "last month") == 0)
		{
			result->append8(result, "strftime('\%s', datetime('now', 'start of month', '-1 month'");
		}
		else if (strcmp((char *)datval, "last year") == 0)
		{
			result->append8(result, "strftime('\%s', datetime('now', 'start of year', '-1 year'");
		}
		else
		{
			result->append8(result, "strftime('\%s', datetime(\'");
			result->append8(result, datval);
			result->append8(result, "\'");
		}

		if (beforeorafter)
		{
			result->append8(result, ", '");
			result->addc(result, beforeorafter);
			result->append8(result, interval);
			result->addc(result, '\'');
		}
		result->append8(result, ", 'utc'))");
	}
}

playlist	returns [ pANTLR3_STRING title, pANTLR3_STRING query, pANTLR3_STRING orderby, pANTLR3_STRING having, int limit ]
@init { $title = NULL; $query = NULL; $orderby = NULL; $having = NULL; $limit = -1; }
	:	STR '{' e = expression '}'
		{
			pANTLR3_UINT8 val;
			val = $STR.text->toUTF8($STR.text)->chars;
			val++;
			val[strlen((const char *)val) - 1] = '\0';
			
			$title = $STR.text->factory->newRaw($STR.text->factory);
			$title->append8($title, (const char *)val);
			
			$query = $e.result->factory->newRaw($e.result->factory);
			$query->append8($query, "(");
			$query->appendS($query, $e.result);
			$query->append8($query, ")");
			
			$limit = $e.limit;
			
			$orderby = $e.result->factory->newRaw($e.result->factory);
			$orderby->appendS($orderby, $e.orderby);
			
			$having = $e.result->factory->newRaw($e.result->factory);
			$having->appendS($having, $e.having);
		}
	;

expression	returns [ pANTLR3_STRING result, pANTLR3_STRING orderby, pANTLR3_STRING having, int limit ]
@init { $result = NULL; $orderby = NULL; $having = NULL; $limit = -1; }
	:	^(LIMIT a = expression INT)
		{
			$result = $a.result->factory->newRaw($a.result->factory);
			$result->appendS($result, $a.result);
			
			$having = $a.result->factory->newRaw($a.result->factory);
			$having->appendS($having, $a.having);
			
			$orderby = $a.result->factory->newRaw($a.result->factory);
			$orderby->appendS($orderby, $a.orderby);
			
			$limit = atoi((const char *)$INT.text->chars);
		}
	|	^(ORDERBY a = expression o = ordertag SORTDIR)
		{
			$result = $a.result->factory->newRaw($a.result->factory);
			$result->appendS($result, $a.result);
			
			$having = $a.result->factory->newRaw($a.result->factory);
			$having->appendS($having, $a.having);
			
			$orderby = $o.result->factory->newRaw($o.result->factory);
			$orderby->appendS($orderby, $o.result);
			$orderby->append8($orderby, " ");
			$orderby->appendS($orderby, $SORTDIR.text->toUTF8($SORTDIR.text));
		}
	|	^(HAVING a = expression b = expression)
		{
			$result = $a.result->factory->newRaw($a.result->factory);
			$result->appendS($result, $a.result);
			
			$having = $b.result->factory->newRaw($b.result->factory);
			$having->appendS($having, $b.result);
		}
	|	^(NOT a = expression)
		{
			$result = $a.result->factory->newRaw($a.result->factory);
			$result->append8($result, "NOT(");
			$result->appendS($result, $a.result);
			$result->append8($result, ")");
		}
	|	^(AND a = expression b = expression)
		{
			$result = $a.result->factory->newRaw($a.result->factory);
			$result->append8($result, "(");
			$result->appendS($result, $a.result);
			$result->append8($result, " AND ");
			$result->appendS($result, $b.result);
			$result->append8($result, ")");
		}
	|	^(OR a = expression b = expression)
		{
			$result = $a.result->factory->newRaw($a.result->factory);
			$result->append8($result, "(");
			$result->appendS($result, $a.result);
			$result->append8($result, " OR ");
			$result->appendS($result, $b.result);
			$result->append8($result, ")");
		}
	|	STRTAG INCLUDES STR
		{
			pANTLR3_UINT8 val;
			char *tmp;
			
			val = $STR.text->toUTF8($STR.text)->chars;
			val++;
			val[strlen((const char *)val) - 1] = '\0';
			
			tmp = sqlite3_mprintf("\%q", (const char *)val);
			
			$result = $STR.text->factory->newRaw($STR.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $STRTAG.text->toUTF8($STRTAG.text));
			$result->append8($result, " LIKE '\%");
			$result->append8($result, tmp);
			$result->append8($result, "\%'");
			
			sqlite3_free(tmp);
		}
	|	STRTAG IS STR
		{
			pANTLR3_UINT8 val;
			char *tmp;
			
			val = $STR.text->toUTF8($STR.text)->chars;
			val++;
			val[strlen((const char *)val) - 1] = '\0';
			
			tmp = sqlite3_mprintf("\%q", (const char *)val);
			
			$result = $STR.text->factory->newRaw($STR.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $STRTAG.text->toUTF8($STRTAG.text));
			$result->append8($result, " LIKE '");
			$result->append8($result, tmp);
			$result->append8($result, "'");
			
			sqlite3_free(tmp);
		}
	|	STRTAG STARTSWITH STR
		{
			pANTLR3_UINT8 val;
			char *tmp;
			
			val = $STR.text->toUTF8($STR.text)->chars;
			val++;
			val[strlen((const char *)val) - 1] = '\0';
			
			tmp = sqlite3_mprintf("\%q", (const char *)val);
			
			$result = $STR.text->factory->newRaw($STR.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $STRTAG.text->toUTF8($STRTAG.text));
			$result->append8($result, " LIKE '");
			$result->append8($result, tmp);
			$result->append8($result, "\%'");
			
			sqlite3_free(tmp);
		}
	|	INTTAG INTBOOL INT
		{
			$result = $INTTAG.text->factory->newRaw($INTTAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $INTTAG.text->toUTF8($INTTAG.text));
			$result->append8($result, " ");
			$result->appendS($result, $INTBOOL.text->toUTF8($INTBOOL.text));
			$result->append8($result, " ");
			$result->appendS($result, $INT.text->toUTF8($INT.text));
		}
	|	DATETAG AFTER dateval
		{
			$result = $DATETAG.text->factory->newRaw($DATETAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $DATETAG.text->toUTF8($DATETAG.text));
			$result->append8($result, " > ");
			$result->append8($result, (const char*)$dateval.result->chars);
		}
	|	DATETAG BEFORE dateval
		{
			$result = $DATETAG.text->factory->newRaw($DATETAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $DATETAG.text->toUTF8($DATETAG.text));
			$result->append8($result, " < ");
			$result->append8($result, (const char*)$dateval.result->chars);
		}
	|	ENUMTAG IS ENUMVAL
		{
			pANTLR3_UINT8 tag;
			pANTLR3_UINT8 val;
			char str[20];
			
			sprintf(str, "1=1");
			
			tag = $ENUMTAG.text->chars;
			val = $ENUMVAL.text->chars;
			if (strcmp((char *)tag, "media_kind") == 0)
			{
				if (strcmp((char *)val, "music") == 0)
				{
					sprintf(str, "f.media_kind = \%d", MEDIA_KIND_MUSIC);
				}
				else if (strcmp((char *)val, "movie") == 0)
				{
					sprintf(str, "f.media_kind = \%d", MEDIA_KIND_MOVIE);
				}
				else if (strcmp((char *)val, "podcast") == 0)
				{
					sprintf(str, "f.media_kind = \%d", MEDIA_KIND_PODCAST);
				}
				else if (strcmp((char *)val, "audiobook") == 0)
				{
					sprintf(str, "f.media_kind = \%d", MEDIA_KIND_AUDIOBOOK);
				}
				else if (strcmp((char *)val, "tvshow") == 0)
				{
					sprintf(str, "f.media_kind = \%d", MEDIA_KIND_TVSHOW);
				}
			}
			else if (strcmp((char *)tag, "data_kind") == 0)
			{
				if (strcmp((char *)val, "file") == 0)
				{
					sprintf(str, "f.data_kind = \%d", DATA_KIND_FILE);
				}
				else if (strcmp((char *)val, "url") == 0)
				{
					sprintf(str, "f.data_kind = \%d", DATA_KIND_HTTP);
				}
				else if (strcmp((char *)val, "spotify") == 0)
				{
					sprintf(str, "f.data_kind = \%d", DATA_KIND_SPOTIFY);
				}
				else if (strcmp((char *)val, "pipe") == 0)
				{
					sprintf(str, "f.data_kind = \%d", DATA_KIND_PIPE);
				}
			}
			else if (strcmp((char *)tag, "scan_kind") == 0)
			{
				if (strcmp((char *)val, "files") == 0)
				{
					sprintf(str, "f.scan_kind = \%d", SCAN_KIND_FILES);
				}
				else if (strcmp((char *)val, "spotify") == 0)
				{
					sprintf(str, "f.scan_kind = \%d", SCAN_KIND_SPOTIFY);
				}
				else if (strcmp((char *)val, "rss") == 0)
				{
					sprintf(str, "f.scan_kind = \%d", SCAN_KIND_RSS);
				}
			}
			
			$result = $ENUMTAG.text->factory->newRaw($ENUMTAG.text->factory);
			$result->append8($result, str);
		}
	|	GROUPTAG INTBOOL INT
		{
			$result = $GROUPTAG.text->factory->newRaw($GROUPTAG.text->factory);
			$result->appendS($result, $GROUPTAG.text->toUTF8($GROUPTAG.text));
			$result->append8($result, " ");
			$result->appendS($result, $INTBOOL.text->toUTF8($INTBOOL.text));
			$result->append8($result, " ");
			$result->appendS($result, $INT.text->toUTF8($INT.text));
		}
	;

ordertag	returns [ pANTLR3_STRING result ]
@init { $result = NULL; }
	:	STRTAG
		{
			$result = $STRTAG.text->factory->newRaw($STRTAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $STRTAG.text->toUTF8($STRTAG.text));
		}
	|	INTTAG
		{
			$result = $INTTAG.text->factory->newRaw($INTTAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $INTTAG.text->toUTF8($INTTAG.text));
		}
	|	DATETAG
		{
			$result = $DATETAG.text->factory->newRaw($DATETAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $DATETAG.text->toUTF8($DATETAG.text));
		}
	|	ENUMTAG
		{
			$result = $ENUMTAG.text->factory->newRaw($ENUMTAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $ENUMTAG.text->toUTF8($ENUMTAG.text));
		}
	|	RANDOMTAG
		{
			$result = $RANDOMTAG.text->factory->newRaw($RANDOMTAG.text->factory);
			$result->append8($result, "random()");
		}
	;

dateval		returns [ pANTLR3_STRING result ]
@init { $result = NULL; }
	:	DATE
		{
			pANTLR3_UINT8 datval;
			
			datval = $DATE.text->chars;
			$result = $DATE.text->factory->newRaw($DATE.text->factory);
			
			append_date($result, (const char *)datval, 0, NULL);
		}
	|	interval BEFORE DATE
		{
			$result = $DATE.text->factory->newRaw($DATE.text->factory);
			append_date($result, (const char *)$DATE.text->chars, '-', (const char *)$interval.result->chars);
		}
	|	interval AFTER DATE
		{
			$result = $DATE.text->factory->newRaw($DATE.text->factory);
			append_date($result, (const char *)$DATE.text->chars, '+', (const char *)$interval.result->chars);
		}
	|	interval AGO
		{
			$result = $AGO.text->factory->newRaw($AGO.text->factory);
			append_date($result, "today", '-', (const char *)$interval.result->chars);
		}
	;

interval	returns [ pANTLR3_STRING result ]
@init { $result = NULL; }
	:	INT DATINTERVAL
		{
			pANTLR3_UINT8 interval;
			int intval;
			char buf[25];

			$result = $DATINTERVAL.text->factory->newRaw($DATINTERVAL.text->factory);

			// SQL doesnt have a modifer for 'week' but for day/hr/min/sec/month/yr
			interval = $DATINTERVAL.text->chars;
			if (strcmp((char *)interval, "weeks") == 0)
			{
				intval = atoi((const char *)$INT.text->chars) * 7;
				snprintf(buf, sizeof(buf), "\%d days", intval);

				$result->append8($result, buf);
			}
			else
			{
				$result->append8($result, (const char *)$INT.text->chars);
				$result->append8($result, " ");
				$result->append8($result, (const char *)$DATINTERVAL.text->chars);
			}
			return $result;
		}
	;


