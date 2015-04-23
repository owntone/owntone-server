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
}

playlist	returns [ pANTLR3_STRING title, pANTLR3_STRING query ]
@init { $title = NULL; $query = NULL; }
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
		}
	;

expression	returns [ pANTLR3_STRING result ]
@init { $result = NULL; }
	:	^(NOT a = expression)
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
			val = $STR.text->toUTF8($STR.text)->chars;
			val++;
			val[strlen((const char *)val) - 1] = '\0';
			
			$result = $STR.text->factory->newRaw($STR.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $STRTAG.text->toUTF8($STRTAG.text));
			$result->append8($result, " LIKE '\%");
			$result->append8($result, sqlite3_mprintf("\%q", (const char *)val));
			$result->append8($result, "\%'");
		}
	|	STRTAG IS STR
		{
			pANTLR3_UINT8 val;
			val = $STR.text->toUTF8($STR.text)->chars;
			val++;
			val[strlen((const char *)val) - 1] = '\0';
			
			$result = $STR.text->factory->newRaw($STR.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $STRTAG.text->toUTF8($STRTAG.text));
			$result->append8($result, " LIKE '");
			$result->append8($result, sqlite3_mprintf("\%q", (const char *)val));
			$result->append8($result, "'");
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
			char str[15];
			sprintf(str, "\%d", $dateval.result);
			
			$result = $DATETAG.text->factory->newRaw($DATETAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $DATETAG.text->toUTF8($DATETAG.text));
			$result->append8($result, " > ");
			$result->append8($result, str);
		}
	|	DATETAG BEFORE dateval
		{
			char str[15];
			sprintf(str, "\%d", $dateval.result);
			
			$result = $DATETAG.text->factory->newRaw($DATETAG.text->factory);
			$result->append8($result, "f.");
			$result->appendS($result, $DATETAG.text->toUTF8($DATETAG.text));
			$result->append8($result, " > ");
			$result->append8($result, str);
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
					sprintf(str, "f.data_kind = \%d", DATA_KIND_URL);
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
			
			$result = $ENUMTAG.text->factory->newRaw($ENUMTAG.text->factory);
			$result->append8($result, str);
		}
	;

dateval		returns [ int result ]
@init { $result = 0; }
	:	DATE
		{
			pANTLR3_UINT8 datval;
			
			datval = $DATE.text->chars;
			
			if (strcmp((char *)datval, "today") == 0)
			{
				$result = time(NULL);
			}
			else if (strcmp((char *)datval, "yesterday") == 0)
			{
				$result = time(NULL) - 24 * 3600;
			}
			else if (strcmp((char *)datval, "last week") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 7;
			}
			else if (strcmp((char *)datval, "last month") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 30;
			}
			else if (strcmp((char *)datval, "last year") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 365;
			}
			else
			{
				struct tm tm;
				char year[5];
				char month[3];
				char day[3];
				
				memset((void*)&tm,0,sizeof(tm));
				memset(year, 0, sizeof(year));
				memset(month, 0, sizeof(month));
				memset(day, 0, sizeof(day));

				strncpy(year, (const char *)datval, 4);
				strncpy(month, (const char *)datval + 5, 2);
				strncpy(day, (const char *)datval + 8, 2);
				
				tm.tm_year = atoi(year) - 1900;
				tm.tm_mon = atoi(month) - 1;
				tm.tm_mday = atoi(day);
				
				$result = mktime(&tm);
			}
		}
	|	interval BEFORE DATE
		{
			pANTLR3_UINT8 datval;
			
			datval = $DATE.text->chars;
			
			if (strcmp((char *)datval, "yesterday") == 0)
			{
				$result = time(NULL) - 24 * 3600;
			}
			else if (strcmp((char *)datval, "last week") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 7;
			}
			else if (strcmp((char *)datval, "last month") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 30;
			}
			else if (strcmp((char *)datval, "last year") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 365;
			}
			else
			{
				$result = time(NULL);
			}
			
			$result = $result - $interval.result;
		}
	|	interval AFTER DATE
		{
			pANTLR3_UINT8 datval;
			
			datval = $DATE.text->chars;
			
			if (strcmp((char *)datval, "yesterday") == 0)
			{
				$result = time(NULL) - 24 * 3600;
			}
			else if (strcmp((char *)datval, "last week") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 7;
			}
			else if (strcmp((char *)datval, "last month") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 30;
			}
			else if (strcmp((char *)datval, "last year") == 0)
			{
				$result = time(NULL) - 24 * 3600 * 365;
			}
			else
			{
				$result = time(NULL);
			}
			
			$result = $result + $interval.result;
		}
	|	interval AGO
		{
			$result = time(NULL) - $interval.result;
		}
	;

interval	returns [ int result ]
@init { $result = 0; }
	:	INT DATINTERVAL
		{
			pANTLR3_UINT8 interval;
			
			$result = atoi((const char *)$INT.text->chars);
			interval = $DATINTERVAL.text->chars;
			
			if (strcmp((char *)interval, "days") == 0)
			{
				$result = $result * 24 * 3600;
			}
			else if (strcmp((char *)interval, "weeks") == 0)
			{
				$result = $result * 24 * 3600 * 7;
			}
			else if (strcmp((char *)interval, "months") == 0)
			{
				$result = $result * 24 * 3600 * 30;
			}
			else if (strcmp((char *)interval, "weeks") == 0)
			{
				$result = $result * 24 * 3600 * 365;
			}
			else
			{
				$result = 0;
			}
		}
	;
