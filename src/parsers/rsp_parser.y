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
%define api.prefix {rsp_}

/* Gives better errors than "syntax error" */
%define parse.error verbose

/* Enables debug mode */
%define parse.trace

/* Adds output parameter to the parser */
%parse-param {struct rsp_result *result}

/* Adds "scanner" as argument to the parses calls to yylex, which is required
   when the lexer is in reentrant mode. The type is void because caller caller
   shouldn't need to know about yyscan_t */
%param {void *scanner}

%code provides {
/* Convenience functions for caller to use instead of interfacing with lexer and
   parser directly */
int rsp_lex_cb(char *input, void (*cb)(int, const char *));
int rsp_lex_parse(struct rsp_result *result, const char *input);
}

/* Implementation of the convenience function and the parsing error function
   required by Bison */
%code {
  #include "rsp_lexer.h"

  int rsp_lex_cb(char *input, void (*cb)(int, const char *))
  {
    int ret;
    yyscan_t scanner;
    YY_BUFFER_STATE buf;
    YYSTYPE val;

    if ((ret = rsp_lex_init(&scanner)) != 0)
      return ret;

    buf = rsp__scan_string(input, scanner);

    while ((ret = rsp_lex(&val, scanner)) > 0)
      cb(ret, rsp_get_text(scanner));

    rsp__delete_buffer(buf, scanner);
    rsp_lex_destroy(scanner);
    return 0;
  }

  int rsp_lex_parse(struct rsp_result *result, const char *input)
  {
    YY_BUFFER_STATE buffer;
    yyscan_t scanner;
    int retval = -1;
    int ret;

    result->errmsg[0] = '\0'; // For safety

    ret = rsp_lex_init(&scanner);
    if (ret != 0)
      goto error_init;

    buffer = rsp__scan_string(input, scanner);
    if (!buffer)
      goto error_buffer;

    ret = rsp_parse(result, scanner);
    if (ret != 0)
      goto error_parse;

    retval = 0;

   error_parse:
    rsp__delete_buffer(buffer, scanner);
   error_buffer:
    rsp_lex_destroy(scanner);
   error_init:
    return retval;
  }

  void rsp_error(struct rsp_result *result, yyscan_t scanner, const char *msg)
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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h> // For vsnprintf
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
struct rsp_result {
  char str[1024];
  int offset;
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
  SQL_APPEND_PARENS,
};

static void sql_from_ast(struct rsp_result *, struct ast *);

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
  char *s = *value;

  if (strchr(s, '\"'))
    safe_snreplace(s, strlen(s) + 1, "\\\"", "\""); // See test case 3

  *value = db_escape_string(s);
  free(s);
}

static void sql_append(struct rsp_result *result, const char *fmt, ...)
{
  va_list ap;
  int remaining = sizeof(result->str) - result->offset;
  int ret;

  if (remaining <= 0)
    goto nospace;

  va_start(ap, fmt);
  ret = vsnprintf(result->str + result->offset, remaining, fmt, ap);
  va_end(ap);
  if (ret < 0 || ret >= remaining)
    goto nospace;

  result->offset += ret;
  return;

 nospace:
  snprintf(result->errmsg, sizeof(result->errmsg), "Parser output buffer too small (%zu bytes)", sizeof(result->str));
  result->err = -2;
}

static void sql_append_recursive(struct rsp_result *result, struct ast *a, const char *op, const char *op_not, bool is_not, enum sql_append_type append_type)
{
  char escape_char;

  switch (append_type)
  {
    case SQL_APPEND_OPERATOR:
      sql_from_ast(result, a->l);
      sql_append(result, " %s ", is_not ? op_not : op);
      sql_from_ast(result, a->r);
      break;
    case SQL_APPEND_OPERATOR_STR:
      sql_from_ast(result, a->l);
      sql_append(result, " %s '", is_not ? op_not : op);
      sql_from_ast(result, a->r);
      sql_append(result, "'");
      break;
    case SQL_APPEND_OPERATOR_LIKE:
      sql_from_ast(result, a->l);
      sql_append(result, " %s '%%", is_not ? op_not : op);
      sql_like_escape((char **)(&a->r->data), &escape_char);
      sql_from_ast(result, a->r);
      sql_append(result, "%%'");
      if (escape_char)
        sql_append(result, " ESCAPE '%c'", escape_char);
      break;
    case SQL_APPEND_FIELD:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_append(result, "f.%s", (char *)a->data);
      break;
    case SQL_APPEND_STR:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_str_escape((char **)&a->data);
      sql_append(result, "%s", (char *)a->data);
      break;
    case SQL_APPEND_INT:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_append(result, "%d", a->ival);
      break;
    case SQL_APPEND_PARENS:
      assert(a->r == NULL);
      sql_append(result, "(");
      sql_from_ast(result, a->l);
      sql_append(result, ")");
      break;
  }
}

static void sql_from_ast(struct rsp_result *result, struct ast *a)
{
  if (!a || result->err < 0)
    return;

  // Not currently used since grammar below doesn't ever set with INVERT_MASK
  bool is_not = (a->type & INVERT_MASK);
  a->type &= ~INVERT_MASK;

  switch (a->type)
  {
    case RSP_T_OR:
      sql_append_recursive(result, a, "OR", "OR NOT", is_not, SQL_APPEND_OPERATOR); break;
    case RSP_T_AND:
      sql_append_recursive(result, a, "AND", "AND NOT", is_not, SQL_APPEND_OPERATOR); break;
    case RSP_T_EQUAL:
      sql_append_recursive(result, a, "=", "!=", is_not, SQL_APPEND_OPERATOR); break;
    case RSP_T_IS:
      sql_append_recursive(result, a, "=", "!=", is_not, SQL_APPEND_OPERATOR_STR); break;
    case RSP_T_INCLUDES:
      sql_append_recursive(result, a, "LIKE", "NOT LIKE", is_not, SQL_APPEND_OPERATOR_LIKE); break;
    case RSP_T_STRTAG:
    case RSP_T_INTTAG:
      sql_append_recursive(result, a, NULL, NULL, 0, SQL_APPEND_FIELD); break;
    case RSP_T_NUM:
      sql_append_recursive(result, a, NULL, NULL, 0, SQL_APPEND_INT); break;
    case RSP_T_STRING:
      sql_append_recursive(result, a, NULL, NULL, 0, SQL_APPEND_STR); break;
      break;
    case RSP_T_PARENS:
      sql_append_recursive(result, a, NULL, NULL, 0, SQL_APPEND_PARENS); break;
    default:
      snprintf(result->errmsg, sizeof(result->errmsg), "Parser produced unrecognized AST type %d", a->type);
      result->err = -1;
  }
}

static int result_set(struct rsp_result *result, struct ast *a)
{
  memset(result, 0, sizeof(struct rsp_result));
  sql_from_ast(result, a);
  ast_free(a);
  return result->err;
}
}

%union {
  unsigned int ival;
  char *str;
  struct ast *ast;
}

/* A string that was quoted. Quotes were stripped by lexer. */
%token <str> RSP_T_STRING

/* A number (integer) */
%token <ival> RSP_T_NUM

/* The semantic value holds the actual name of the field */
%token <str> RSP_T_STRTAG
%token <str> RSP_T_INTTAG

%token RSP_T_PARENS
%token RSP_T_OR
%token RSP_T_AND
%token RSP_T_NOT

%token RSP_T_EQUAL
%token RSP_T_IS
%token RSP_T_INCLUDES

%left RSP_T_OR RSP_T_AND

%type <ast> criteria
%type <ast> predicate

%%

query:
  criteria                                              { if (result_set(result, $1) < 0) YYABORT; }
;

criteria: criteria RSP_T_AND criteria                   { $$ = ast_new(RSP_T_AND, $1, $3); }
| criteria RSP_T_OR criteria                            { $$ = ast_new(RSP_T_OR, $1, $3); }
| '(' criteria ')'                                      { $$ = ast_new(RSP_T_PARENS, $2, NULL); }
| predicate
;

predicate: RSP_T_STRTAG RSP_T_EQUAL RSP_T_STRING        { $$ = ast_new(RSP_T_IS, ast_data(RSP_T_STRTAG, $1), ast_data(RSP_T_STRING, $3)); }
| RSP_T_STRTAG RSP_T_INCLUDES RSP_T_STRING              { $$ = ast_new(RSP_T_INCLUDES, ast_data(RSP_T_STRTAG, $1), ast_data(RSP_T_STRING, $3)); }
| RSP_T_INTTAG RSP_T_EQUAL RSP_T_NUM                    { $$ = ast_new(RSP_T_EQUAL, ast_data(RSP_T_INTTAG, $1), ast_int(RSP_T_NUM, $3)); }
;

%%

