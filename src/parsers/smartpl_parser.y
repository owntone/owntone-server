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

/* No global variables and yylex has scanner as argument */
%define api.pure true

/* Change prefix of symbols from yy to avoid clashes with any other parsers we
   may want to link */
%define api.prefix {smartpl_}

/* Gives better errors than "syntax error" */
%define parse.error verbose

/* Enables debug mode */
%define parse.trace

/* Adds output parameter to the parser */
%parse-param {struct smartpl_result *result}

/* Adds "scanner" as argument to the parses calls to yylex, which is required
   when the lexer is in reentrant mode. The type is void because caller caller
   shouldn't need to know about yyscan_t */
%param {void *scanner}

%code provides {
/* Convenience functions for caller to use instead of interfacing with lexer and
   parser directly */
int smartpl_lex_cb(char *input, void (*cb)(int, const char *));
int smartpl_lex_parse(struct smartpl_result *result, const char *input);
}

/* Implementation of the convenience function and the parsing error function
   required by Bison */
%code {
  #include "smartpl_lexer.h"

  int smartpl_lex_cb(char *input, void (*cb)(int, const char *))
  {
    int ret;
    yyscan_t scanner;
    YY_BUFFER_STATE buf;
    YYSTYPE val;

    if ((ret = smartpl_lex_init(&scanner)) != 0)
      return ret;

    buf = smartpl__scan_string(input, scanner);

    while ((ret = smartpl_lex(&val, scanner)) > 0)
      cb(ret, smartpl_get_text(scanner));

    smartpl__delete_buffer(buf, scanner);
    smartpl_lex_destroy(scanner);
    return 0;
  }

  int smartpl_lex_parse(struct smartpl_result *result, const char *input)
  {
    YY_BUFFER_STATE buffer;
    yyscan_t scanner;
    int retval = -1;
    int ret;

    result->errmsg[0] = '\0'; // For safety

    ret = smartpl_lex_init(&scanner);
    if (ret != 0)
      goto error_init;

    buffer = smartpl__scan_string(input, scanner);
    if (!buffer)
      goto error_buffer;

    ret = smartpl_parse(result, scanner);
    if (ret != 0)
      goto error_parse;

    retval = 0;

   error_parse:
    smartpl__delete_buffer(buffer, scanner);
   error_buffer:
    smartpl_lex_destroy(scanner);
   error_init:
    return retval;
  }

  void smartpl_error(struct smartpl_result *result, yyscan_t scanner, const char *msg)
  {
    snprintf(result->errmsg, sizeof(result->errmsg), "%s", msg);
  }

}

/* ============ ABSTRACT SYNTAX TREE (AST) BOILERPLATE SECTION ===============*/

%code {
  struct ast
  {
    int type;
    struct ast *l;
    struct ast *r;
    void *data;
    int ival;
  };

  __attribute__((unused)) static struct ast * ast_new(int type, struct ast *l, struct ast *r)
  {
    struct ast *a = calloc(1, sizeof(struct ast));

    a->type = type;
    a->l = l;
    a->r = r;
    return a;
  }

  /* Note *data is expected to be freeable with regular free() */
  __attribute__((unused)) static struct ast * ast_data(int type, void *data)
  {
    struct ast *a = calloc(1, sizeof(struct ast));

    a->type = type;
    a->data = data;
    return a;
  }

  __attribute__((unused)) static struct ast * ast_int(int type, int ival)
  {
    struct ast *a = calloc(1, sizeof(struct ast));

    a->type = type;
    a->ival = ival;
    return a;
  }

  __attribute__((unused)) static void ast_free(struct ast *a)
  {
    if (!a)
      return;

    ast_free(a->l);
    ast_free(a->r);
    free(a->data);
    free(a);
  }
}

%destructor { free($$); } <str>
%destructor { ast_free($$); } <ast>


/* ========================= NON-BOILERPLATE SECTION =========================*/

/* Includes required by the parser rules */
%code top {
#ifndef _GNU_SOURCE
#define _GNU_SOURCE // For asprintf
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h> // For vsnprintf
#include <string.h>
#include <time.h>
#include <assert.h>

#define INVERT_MASK 0x80000000
}

/* Dependencies, mocked or real */
%code top {
#ifndef DEBUG_PARSER_MOCK
#include "db.h"
#include "misc.h"
#else
#include "owntonefunctions.h"
#endif
}

/* Definition of struct that will hold the parsing result */
%code requires {
struct result_part {
  char str[512];
  int offset;
};

struct smartpl_result {
  struct result_part where_part;
  struct result_part order_part;
  struct result_part having_part;
  char title[128];
  const char *where; // Points to where_part.str
  const char *order; // Points to order_part.str
  const char *having; // Points to having_part.str
  int limit;
  int err;
  char errmsg[128];
};
}

%code {
enum sql_append_type {
  SQL_APPEND_OPERATOR,
  SQL_APPEND_OPERATOR_STR,
  SQL_APPEND_OPERATOR_LIKE,
  SQL_APPEND_FIELD,
  SQL_APPEND_STR,
  SQL_APPEND_INT,
  SQL_APPEND_ORDER,
  SQL_APPEND_PARENS,
  SQL_APPEND_DATE_STRFTIME,
  SQL_APPEND_DATE_FIELD,
};

static void sql_from_ast(struct smartpl_result *, struct result_part *, struct ast *);

// Escapes any '%' or '_' that might be in the string
static void sql_like_escape(char **value, char *escape_char)
{
  char *s = *value;
  size_t len = strlen(s);
  char *new;

  *escape_char = 0;

  // Fast path, nothing to escape
  if (!strpbrk(s, "_%"))
    return;

  len = 2 * len; // Enough for every char to be escaped
  new = realloc(s, len);
  safe_snreplace(new, len, "%", "\\%");
  safe_snreplace(new, len, "_", "\\_");
  *escape_char = '\\';
  *value = new;
}

static void sql_str_escape(char **value)
{
  char *old = *value;
  *value = db_escape_string(old);
  free(old);
}

static void sql_append(struct smartpl_result *result, struct result_part *part, const char *fmt, ...)
{
  va_list ap;
  int remaining = sizeof(part->str) - part->offset;
  int ret;

  if (remaining <= 0)
    goto nospace;

  va_start(ap, fmt);
  ret = vsnprintf(part->str + part->offset, remaining, fmt, ap);
  va_end(ap);
  if (ret < 0 || ret >= remaining)
    goto nospace;

  part->offset += ret;
  return;

 nospace:
  snprintf(result->errmsg, sizeof(result->errmsg), "Parser output buffer too small (%zu bytes)", sizeof(part->str));
  result->err = -2;
}

static void sql_append_recursive(struct smartpl_result *result, struct result_part *part, struct ast *a, const char *op, const char *op_not, bool is_not, enum sql_append_type append_type)
{
  char escape_char;

  switch (append_type)
  {
    case SQL_APPEND_OPERATOR:
      sql_from_ast(result, part, a->l);
      sql_append(result, part, " %s ", is_not ? op_not : op);
      sql_from_ast(result, part, a->r);
      break;
    case SQL_APPEND_OPERATOR_STR:
      sql_from_ast(result, part, a->l);
      sql_append(result, part, " %s '", is_not ? op_not : op);
      sql_from_ast(result, part, a->r);
      sql_append(result, part, "'");
      break;
    case SQL_APPEND_OPERATOR_LIKE:
      sql_from_ast(result, part, a->l);
      sql_append(result, part, " %s '%s", is_not ? op_not : op, a->type == SMARTPL_T_STARTSWITH ? "" : "%");
      sql_like_escape((char **)(&a->r->data), &escape_char);
      sql_from_ast(result, part, a->r);
      sql_append(result, part, "%s'", a->type == SMARTPL_T_ENDSWITH ? "" : "%");
      if (escape_char)
        sql_append(result, part, " ESCAPE '%c'", escape_char);
      break;
    case SQL_APPEND_FIELD:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_append(result, part, "%s", (char *)a->data);
      break;
    case SQL_APPEND_STR:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_str_escape((char **)&a->data);
      sql_append(result, part, "%s", (char *)a->data);
      break;
    case SQL_APPEND_INT:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_append(result, part, "%d", a->ival);
      break;
    case SQL_APPEND_ORDER:
      assert(a->l == NULL);
      assert(a->r == NULL);
      if (a->data)
        sql_append(result, part, "%s ", (char *)a->data);
      sql_append(result, part, "%s", is_not ? op_not : op);
      break;
    case SQL_APPEND_PARENS:
      assert(a->r == NULL);
      sql_append(result, part, "(");
      sql_from_ast(result, part, a->l);
      sql_append(result, part, ")");
      break;
    case SQL_APPEND_DATE_STRFTIME:
      sql_append(result, part, "strftime('%%s', datetime(");
      sql_from_ast(result, part, a->l); // Appends the anchor date
      sql_from_ast(result, part, a->r); // Appends interval if there is one
      sql_append(result, part, "'utc'))");
      break;
    case SQL_APPEND_DATE_FIELD:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_append(result, part, "'");
      if (is_not ? op_not : op)
        sql_append(result, part, "%s", is_not ? op_not : op);
      if (a->data)
        sql_append(result, part, "%s", (char *)a->data);
      sql_append(result, part, "', ");
      break;
  }
}

/* Creates the parsing result from the AST. Errors are set via result->err. */
static void sql_from_ast(struct smartpl_result *result, struct result_part *part, struct ast *a) {
  if (!a || result->err < 0)
    return;

  bool is_not = (a->type & INVERT_MASK);
  a->type &= ~INVERT_MASK;

  switch (a->type)
  {
    case SMARTPL_T_EQUAL:
      sql_append_recursive(result, part, a, "=", "!=", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_LESS:
      sql_append_recursive(result, part, a, "<", ">=", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_LESSEQUAL:
      sql_append_recursive(result, part, a, "<=", ">", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_GREATER:
      sql_append_recursive(result, part, a, ">", ">=", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_GREATEREQUAL:
      sql_append_recursive(result, part, a, ">=", "<", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_IS:
      sql_append_recursive(result, part, a, "=", "!=", is_not, SQL_APPEND_OPERATOR_STR); break;
    case SMARTPL_T_INCLUDES:
    case SMARTPL_T_STARTSWITH:
    case SMARTPL_T_ENDSWITH:
      sql_append_recursive(result, part, a, "LIKE", "NOT LIKE", is_not, SQL_APPEND_OPERATOR_LIKE); break;
    case SMARTPL_T_BEFORE:
      sql_append_recursive(result, part, a, "<", ">=", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_AFTER:
      sql_append_recursive(result, part, a, ">", "<=", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_AND:
      sql_append_recursive(result, part, a, "AND", "AND NOT", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_OR:
      sql_append_recursive(result, part, a, "OR", "OR NOT", is_not, SQL_APPEND_OPERATOR); break;
    case SMARTPL_T_DATEEXPR:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_DATE_STRFTIME); break;
    case SMARTPL_T_DATE:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_DATE_TODAY:
      sql_append_recursive(result, part, a, "now', 'start of day", NULL, 0, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_DATE_YESTERDAY:
      sql_append_recursive(result, part, a, "now', 'start of day', '-1 day", NULL, 0, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_DATE_THISWEEK:
      sql_append_recursive(result, part, a, "now', 'start of day', 'weekday 0', '-7 days", NULL, 0, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_DATE_LASTWEEK:
      sql_append_recursive(result, part, a, "now', 'start of day', 'weekday 0', '-13 days", NULL, 0, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_DATE_LASTMONTH:
      sql_append_recursive(result, part, a, "now', 'start of month', '-1 month", NULL, 0, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_DATE_LASTYEAR:
      sql_append_recursive(result, part, a, "now', 'start of year', '-1 year", NULL, 0, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_INTERVAL:
      sql_append_recursive(result, part, a, "-", "+", is_not, SQL_APPEND_DATE_FIELD); break;
    case SMARTPL_T_STRING:
    case SMARTPL_T_GROUPTAG:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_STR); break;
    case SMARTPL_T_STRTAG:
    case SMARTPL_T_INTTAG:
    case SMARTPL_T_DATETAG:
    case SMARTPL_T_ENUMTAG:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_FIELD); break;
    case SMARTPL_T_NUM:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_INT); break;
    case SMARTPL_T_ORDERBY:
      sql_append_recursive(result, part, a, "ASC", "DESC", is_not, SQL_APPEND_ORDER); break;
    case SMARTPL_T_RANDOM:
      sql_append_recursive(result, part, a, "random()", NULL, 0, SQL_APPEND_ORDER); break;
    case SMARTPL_T_PARENS:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_PARENS); break;
    default:
      snprintf(result->errmsg, sizeof(result->errmsg), "Parser produced unrecognized AST type %d", a->type);
      result->err = -1;
  }
}

static int result_set(struct smartpl_result *result, char *title, struct ast *criteria, struct ast *having, struct ast *order, struct ast *limit)
{
  memset(result, 0, sizeof(struct smartpl_result));
  snprintf(result->title, sizeof(result->title), "%s", title); // just silently truncated if too long
  sql_from_ast(result, &result->where_part, criteria);
  sql_from_ast(result, &result->having_part, having);
  sql_from_ast(result, &result->order_part, order);

  result->where  = result->where_part.offset ? result->where_part.str : NULL;
  result->having = result->having_part.offset ? result->having_part.str : NULL;
  result->order  = result->order_part.offset ? result->order_part.str : NULL;
  result->limit  = limit ? limit->ival : 0;

  free(title);
  ast_free(criteria);
  ast_free(having);
  ast_free(order);
  ast_free(limit);

  return result->err;
}
}

%union {
  unsigned int ival;
  char *str;
  struct ast *ast;
}

/* A string that was quoted. Quotes were stripped by lexer. */
%token <str> SMARTPL_T_STRING

/* Numbers (integers) */
%token <ival> SMARTPL_T_NUM

/* The semantic value holds the actual name of the field */
%token <str> SMARTPL_T_STRTAG
%token <str> SMARTPL_T_INTTAG
%token <str> SMARTPL_T_DATETAG
%token <str> SMARTPL_T_GROUPTAG

%token SMARTPL_T_ENUMTAG
%token <str> SMARTPL_T_ENUMTAG_DATAKIND
%token <str> SMARTPL_T_ENUMTAG_MEDIAKIND
%token <str> SMARTPL_T_ENUMTAG_SCANKIND

%token SMARTPL_T_FILE;
%token SMARTPL_T_URL;
%token SMARTPL_T_SPOTIFY;
%token SMARTPL_T_PIPE;
%token SMARTPL_T_RSS;

%token SMARTPL_T_MUSIC;
%token SMARTPL_T_MOVIE;
%token SMARTPL_T_PODCAST;
%token SMARTPL_T_AUDIOBOOK;
%token SMARTPL_T_TVSHOW;

%token SMARTPL_T_DATEEXPR
%token SMARTPL_T_HAVING
%token SMARTPL_T_ORDERBY
%token SMARTPL_T_ORDER_ASC
%token SMARTPL_T_ORDER_DESC
%token SMARTPL_T_LIMIT
%token SMARTPL_T_RANDOM
%token SMARTPL_T_PARENS
%token SMARTPL_T_OR
%token SMARTPL_T_AND
%token SMARTPL_T_NOT

%token SMARTPL_T_DAYS
%token SMARTPL_T_WEEKS
%token SMARTPL_T_MONTHS
%token SMARTPL_T_YEARS
%token SMARTPL_T_INTERVAL

%token <str> SMARTPL_T_DATE
%token <ival> SMARTPL_T_DATE_TODAY
%token <ival> SMARTPL_T_DATE_YESTERDAY
%token <ival> SMARTPL_T_DATE_THISWEEK
%token <ival> SMARTPL_T_DATE_LASTWEEK
%token <ival> SMARTPL_T_DATE_LASTMONTH
%token <ival> SMARTPL_T_DATE_LASTYEAR

/* The below are only ival so we can set intbool, datebool and strbool via the
   default rule for semantic values, i.e. $$ = $1. The semantic value (ival) is
   set to the token value by the lexer. */
%token <ival> SMARTPL_T_IS
%token <ival> SMARTPL_T_INCLUDES
%token <ival> SMARTPL_T_STARTSWITH
%token <ival> SMARTPL_T_ENDSWITH
%token <ival> SMARTPL_T_EQUAL
%token <ival> SMARTPL_T_LESS
%token <ival> SMARTPL_T_LESSEQUAL
%token <ival> SMARTPL_T_GREATER
%token <ival> SMARTPL_T_GREATEREQUAL
%token <ival> SMARTPL_T_BEFORE
%token <ival> SMARTPL_T_AFTER
%token <ival> SMARTPL_T_AGO

%left SMARTPL_T_OR SMARTPL_T_AND

%type <ast> criteria
%type <ast> predicate
%type <ast> enumexpr
%type <ast> dateexpr
%type <ast> interval
%type <ast> having
%type <ast> order
%type <ast> limit
%type <str> time
%type <ival> daterelative
%type <ival> strbool
%type <ival> intbool
%type <ival> datebool

%%

playlist:
  SMARTPL_T_STRING '{' criteria having order limit '}'      { if (result_set(result, $1, $3, $4, $5, $6) < 0) YYABORT; }
| SMARTPL_T_STRING '{' criteria having order '}'            { if (result_set(result, $1, $3, $4, $5, NULL) < 0) YYABORT; }
| SMARTPL_T_STRING '{' criteria having limit '}'            { if (result_set(result, $1, $3, $4, NULL, $5) < 0) YYABORT; }
| SMARTPL_T_STRING '{' criteria having '}'                  { if (result_set(result, $1, $3, $4, NULL, NULL) < 0) YYABORT; }
| SMARTPL_T_STRING '{' criteria order limit '}'             { if (result_set(result, $1, $3, NULL, $4, $5) < 0) YYABORT; }
| SMARTPL_T_STRING '{' criteria order '}'                   { if (result_set(result, $1, $3, NULL, $4, NULL) < 0) YYABORT; }
| SMARTPL_T_STRING '{' criteria limit '}'                   { if (result_set(result, $1, $3, NULL, NULL, $4) < 0) YYABORT; }
| SMARTPL_T_STRING '{' criteria '}'                         { if (result_set(result, $1, $3, NULL, NULL, NULL) < 0) YYABORT; }
;

criteria: criteria SMARTPL_T_AND criteria                   { $$ = ast_new(SMARTPL_T_AND, $1, $3); }
| criteria SMARTPL_T_OR criteria                            { $$ = ast_new(SMARTPL_T_OR, $1, $3); }
| '(' criteria ')'                                          { $$ = ast_new(SMARTPL_T_PARENS, $2, NULL); }
| predicate
;

predicate: SMARTPL_T_STRTAG strbool SMARTPL_T_STRING        { $$ = ast_new($2, ast_data(SMARTPL_T_STRTAG, $1), ast_data(SMARTPL_T_STRING, $3)); }
| SMARTPL_T_INTTAG intbool SMARTPL_T_NUM                    { $$ = ast_new($2, ast_data(SMARTPL_T_INTTAG, $1), ast_int(SMARTPL_T_NUM, $3)); }
| SMARTPL_T_DATETAG datebool dateexpr                       { $$ = ast_new($2, ast_data(SMARTPL_T_DATETAG, $1), $3); }
| SMARTPL_T_NOT predicate                                   { struct ast *a = $2; a->type |= INVERT_MASK; $$ = $2; }
| enumexpr
;

enumexpr:
/* DATA_KIND */
  SMARTPL_T_ENUMTAG_DATAKIND SMARTPL_T_IS SMARTPL_T_FILE        { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, DATA_KIND_FILE)); }
| SMARTPL_T_ENUMTAG_DATAKIND SMARTPL_T_IS SMARTPL_T_URL         { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, DATA_KIND_HTTP)); }
| SMARTPL_T_ENUMTAG_DATAKIND SMARTPL_T_IS SMARTPL_T_SPOTIFY     { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, DATA_KIND_SPOTIFY)); }
| SMARTPL_T_ENUMTAG_DATAKIND SMARTPL_T_IS SMARTPL_T_PIPE        { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, DATA_KIND_PIPE)); }
/* MEDIA_KIND */
| SMARTPL_T_ENUMTAG_MEDIAKIND SMARTPL_T_IS SMARTPL_T_MUSIC      { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, MEDIA_KIND_MUSIC)); }
| SMARTPL_T_ENUMTAG_MEDIAKIND SMARTPL_T_IS SMARTPL_T_MOVIE      { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, MEDIA_KIND_MOVIE)); }
| SMARTPL_T_ENUMTAG_MEDIAKIND SMARTPL_T_IS SMARTPL_T_PODCAST    { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, MEDIA_KIND_PODCAST)); }
| SMARTPL_T_ENUMTAG_MEDIAKIND SMARTPL_T_IS SMARTPL_T_AUDIOBOOK  { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, MEDIA_KIND_AUDIOBOOK)); }
| SMARTPL_T_ENUMTAG_MEDIAKIND SMARTPL_T_IS SMARTPL_T_TVSHOW     { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, MEDIA_KIND_TVSHOW)); }
/* SCAN_KIND */
| SMARTPL_T_ENUMTAG_SCANKIND SMARTPL_T_IS SMARTPL_T_FILE        { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, SCAN_KIND_FILES)); }
| SMARTPL_T_ENUMTAG_SCANKIND SMARTPL_T_IS SMARTPL_T_SPOTIFY     { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, SCAN_KIND_SPOTIFY)); }
| SMARTPL_T_ENUMTAG_SCANKIND SMARTPL_T_IS SMARTPL_T_RSS         { $$ = ast_new(SMARTPL_T_EQUAL, ast_data(SMARTPL_T_ENUMTAG, $1), ast_int(SMARTPL_T_NUM, SCAN_KIND_RSS)); }
;

dateexpr: SMARTPL_T_DATE                                    { $$ = ast_new(SMARTPL_T_DATEEXPR, ast_data(SMARTPL_T_DATE, $1), NULL); }
| daterelative                                              { $$ = ast_new(SMARTPL_T_DATEEXPR, ast_data($1, NULL), NULL); }
| interval SMARTPL_T_DATE                                   { $$ = ast_new(SMARTPL_T_DATEEXPR, ast_data(SMARTPL_T_DATE, $2), $1); }
| interval daterelative                                     { $$ = ast_new(SMARTPL_T_DATEEXPR, ast_data($2, NULL), $1); }
| time SMARTPL_T_AGO                                        { $$ = ast_new(SMARTPL_T_DATEEXPR, ast_data(SMARTPL_T_DATE_TODAY, NULL), ast_data(SMARTPL_T_INTERVAL, $1)); }
;

daterelative: SMARTPL_T_DATE_TODAY
| SMARTPL_T_DATE_YESTERDAY
| SMARTPL_T_DATE_THISWEEK
| SMARTPL_T_DATE_LASTWEEK
| SMARTPL_T_DATE_LASTMONTH
| SMARTPL_T_DATE_LASTYEAR
;

interval: time SMARTPL_T_BEFORE                             { $$ = ast_data(SMARTPL_T_INTERVAL, $1); }
| time SMARTPL_T_AFTER                                      { $$ = ast_data(SMARTPL_T_INTERVAL | INVERT_MASK, $1); }
;

time: SMARTPL_T_NUM SMARTPL_T_DAYS                          { if (asprintf(&($$), "%d days", $1) < 0) YYABORT; }
| SMARTPL_T_NUM SMARTPL_T_WEEKS                             { if (asprintf(&($$), "%d days", 7 * $1) < 0) YYABORT; }
| SMARTPL_T_NUM SMARTPL_T_MONTHS                            { if (asprintf(&($$), "%d months", $1) < 0) YYABORT;  }
| SMARTPL_T_NUM SMARTPL_T_YEARS                             { if (asprintf(&($$), "%d years", $1) < 0) YYABORT;  }
;

having: SMARTPL_T_HAVING SMARTPL_T_GROUPTAG intbool SMARTPL_T_NUM { $$ = ast_new($3, ast_data(SMARTPL_T_GROUPTAG, $2), ast_int(SMARTPL_T_NUM, $4)); }

order: SMARTPL_T_ORDERBY SMARTPL_T_STRTAG                   { $$ = ast_data(SMARTPL_T_ORDERBY, $2); }
| SMARTPL_T_ORDERBY SMARTPL_T_INTTAG                        { $$ = ast_data(SMARTPL_T_ORDERBY, $2); }
| SMARTPL_T_ORDERBY SMARTPL_T_DATETAG                       { $$ = ast_data(SMARTPL_T_ORDERBY, $2); }
| SMARTPL_T_ORDERBY SMARTPL_T_ENUMTAG_DATAKIND              { $$ = ast_data(SMARTPL_T_ORDERBY, $2); }
| SMARTPL_T_ORDERBY SMARTPL_T_ENUMTAG_MEDIAKIND             { $$ = ast_data(SMARTPL_T_ORDERBY, $2); }
| SMARTPL_T_ORDERBY SMARTPL_T_ENUMTAG_SCANKIND              { $$ = ast_data(SMARTPL_T_ORDERBY, $2); }
| SMARTPL_T_ORDERBY SMARTPL_T_RANDOM                        { $$ = ast_data(SMARTPL_T_RANDOM, NULL); }
| order SMARTPL_T_ORDER_ASC                                 { struct ast *a = $1; a->type = SMARTPL_T_ORDERBY; $$ = $1; }
| order SMARTPL_T_ORDER_DESC                                { struct ast *a = $1; a->type |= INVERT_MASK; $$ = $1; }
;

limit: SMARTPL_T_LIMIT SMARTPL_T_NUM                        { $$ = ast_int(SMARTPL_T_LIMIT, $2); }
;

strbool: SMARTPL_T_IS
| SMARTPL_T_INCLUDES
| SMARTPL_T_STARTSWITH
| SMARTPL_T_ENDSWITH
;

intbool: SMARTPL_T_EQUAL
| SMARTPL_T_LESS
| SMARTPL_T_LESSEQUAL
| SMARTPL_T_GREATER
| SMARTPL_T_GREATEREQUAL
;

datebool: SMARTPL_T_BEFORE
| SMARTPL_T_AFTER
;

%%

