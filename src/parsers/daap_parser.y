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
%define api.prefix {daap_}

/* Gives better errors than "syntax error" */
%define parse.error verbose

/* Enables debug mode */
%define parse.trace

/* Adds output parameter to the parser */
%parse-param {struct daap_result *result}

/* Adds "scanner" as argument to the parses calls to yylex, which is required
   when the lexer is in reentrant mode. The type is void because caller caller
   shouldn't need to know about yyscan_t */
%param {void *scanner}

%code provides {
/* Convenience functions for caller to use instead of interfacing with lexer and
   parser directly */
int daap_lex_cb(char *input, void (*cb)(int, const char *));
int daap_lex_parse(struct daap_result *result, const char *input);
}

/* Implementation of the convenience function and the parsing error function
   required by Bison */
%code {
  #include "daap_lexer.h"

  int daap_lex_cb(char *input, void (*cb)(int, const char *))
  {
    int ret;
    yyscan_t scanner;
    YY_BUFFER_STATE buf;
    YYSTYPE val;

    if ((ret = daap_lex_init(&scanner)) != 0)
      return ret;

    buf = daap__scan_string(input, scanner);

    while ((ret = daap_lex(&val, scanner)) > 0)
      cb(ret, daap_get_text(scanner));

    daap__delete_buffer(buf, scanner);
    daap_lex_destroy(scanner);
    return 0;
  }

  int daap_lex_parse(struct daap_result *result, const char *input)
  {
    YY_BUFFER_STATE buffer;
    yyscan_t scanner;
    int retval = -1;
    int ret;

    result->errmsg[0] = '\0'; // For safety

    ret = daap_lex_init(&scanner);
    if (ret != 0)
      goto error_init;

    buffer = daap__scan_string(input, scanner);
    if (!buffer)
      goto error_buffer;

    ret = daap_parse(result, scanner);
    if (ret != 0)
      goto error_parse;

    retval = 0;

   error_parse:
    daap__delete_buffer(buffer, scanner);
   error_buffer:
    daap_lex_destroy(scanner);
   error_init:
    return retval;
  }

  void daap_error(struct daap_result *result, yyscan_t scanner, const char *msg)
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
}

/* Dependencies, mocked or real */
%code top {
#ifndef DEBUG_PARSER_MOCK
#include "daap_query_hash.h"
#include "db.h"
#include "misc.h"
#else
#include "owntonefunctions.h"
#endif
}

/* Definition of struct that will hold the parsing result */
%code requires {
struct daap_result {
  char str[1024];
  int offset;
  int err;
  char errmsg[128];
};
}

%code {
static void sql_append(struct daap_result *result, const char *fmt, ...)
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

static bool clause_is_always_true(bool is_equal, const char *key, const char *val)
{
  // This rule is carried over from the old parser, not sure of the background
  if (is_equal && (strcmp(key, "daap.songalbumid") == 0) && val && val[0] == '0')
    return true;

  return false;
}

static bool clause_is_always_false(bool is_equal, const char *key, const char *val)
{
  // The server makes sure there always is an artist/album, so something like
  // 'daap.songartist:' is always false
  if (strcmp(key, "daap.songalbumartist") == 0 || strcmp(key, "daap.songartist") == 0 || strcmp(key, "daap.songalbum") == 0)
    return !val;
  // The server never has any media type 32, so ignore to improve select query
  if ((strcmp(key, "com.apple.itunes.mediakind") == 0 || strcmp(key, "com.apple.itunes.extended-media-kind") == 0) && val && (strcmp(val, "32") == 0))
    return true;

  return false;
}

// Switches the daap '*' to '%', and escapes any '%' or '_' that might be in the
// string
static void sql_like_escape(char **value, char *escape_char)
{
  char *s = *value;
  size_t len = strlen(s);
  char *new;

  *escape_char = 0;

  if (len < 2)
    return; // Shouldn't ever happen since lexer should give strings w/wildcards

  // Fast path, nothing to escape
  if (!strpbrk(s, "_%"))
    {
      s[0] = s[len - 1] = '%';
      return;
    }

  len = 2 * len; // Enough for every char to be escaped
  new = realloc(s, len);
  safe_snreplace(new, len, "%", "\\%");
  safe_snreplace(new, len, "_", "\\_");
  new[0] = new[strlen(new) - 1] = '%';
  *escape_char = '\\';
  *value = new;
}

static void sql_str_escape(char **value)
{
  char *s = *value;

  if (strchr(s, '\''))
    safe_snreplace(s, strlen(s) + 1, "\\'", "'"); // See Kid's audiobooks test case

  *value = db_escape_string(s);
  free(s);
}

static void sql_append_dmap_clause(struct daap_result *result, struct ast *a)
{
  const struct dmap_query_field_map *dqfm;
  struct ast *k = a->l;
  struct ast *v = a->r;
  bool is_equal = (a->type == DAAP_T_EQUAL);
  char escape_char;
  char *key;

  if (!k || k->type != DAAP_T_KEY || !(key = (char *)k->data))
    {
      snprintf(result->errmsg, sizeof(result->errmsg), "Missing key in dmap input");
      result->err = -3;
      return;
    }
  else if (!v || (v->type != DAAP_T_VALUE && v->type != DAAP_T_WILDCARD)) // NULL is ok
    {
      snprintf(result->errmsg, sizeof(result->errmsg), "Missing value in dmap input");
      result->err = -3;
      return;
    }

  if (clause_is_always_true(is_equal, key, (char *)v->data))
    {
      sql_append(result, is_equal ? "(1 = 1)" : "(1 = 0)");
      return;
    }
  else if (clause_is_always_false(is_equal, key, (char *)v->data))
    {
      sql_append(result, is_equal ? "(1 = 0)" : "(1 = 1)");
      return;
    }

  dqfm = daap_query_field_lookup(key, strlen(key));
  if (!dqfm)
    {
      snprintf(result->errmsg, sizeof(result->errmsg), "Could not map dmap input field '%s' to a db column", key);
      result->err = -4;
      return;
    }

  if (!dqfm->as_int && !v->data)
    {
      // If it is a string and there is no value we select for '' OR NULL
      sql_append(result, "(%s %s ''", dqfm->db_col, is_equal ? "=" : "<>");
      sql_append(result, is_equal ? " OR " : " AND ");
      sql_append(result, "%s %s NULL)", dqfm->db_col, is_equal ? "IS" : "IS NOT");
      return;
    }
  else if (!dqfm->as_int && v->type == DAAP_T_WILDCARD)
    {
      sql_like_escape((char **)&v->data, &escape_char);
      sql_str_escape((char **)&v->data);
      sql_append(result, "%s", dqfm->db_col);
      sql_append(result, is_equal ? " LIKE " : " NOT LIKE ");
      sql_append(result, "'%s'", (char *)v->data);
      if (escape_char)
        sql_append(result, " ESCAPE '%c'", escape_char);
      return;
    }
  else if (!v->data)
    {
      snprintf(result->errmsg, sizeof(result->errmsg), "Missing value for int field '%s'", key);
      result->err = -5;
      return;
    }

  sql_append(result, "%s", dqfm->db_col);
  sql_append(result, is_equal ? " = " : " <> ");
  if (!dqfm->as_int)
    {
      sql_str_escape((char **)&v->data);
      sql_append(result, "'%s'", (char *)v->data);
      return;
    }

  sql_append(result, "%s", (char *)v->data);
}

/* Creates the parsing result from the AST */
static void sql_from_ast(struct daap_result *result, struct ast *a)
{
  if (!a || result->err < 0)
    return;

  switch (a->type)
    {
      case DAAP_T_OR:
      case DAAP_T_AND:
        sql_from_ast(result, a->l);
        sql_append(result, a->type == DAAP_T_OR ? " OR " : " AND ");
        sql_from_ast(result, a->r);
        break;
      case DAAP_T_EQUAL:
      case DAAP_T_NOT:
        sql_append_dmap_clause(result, a); // Special handling due to many special rules
        break;
      case DAAP_T_PARENS:
        sql_append(result, "(");
        sql_from_ast(result, a->l);
        sql_append(result, ")");
        break;
      default:
        snprintf(result->errmsg, sizeof(result->errmsg), "Parser produced unrecognized AST type %d", a->type);
        result->err = -1;
  }
}

static int result_set(struct daap_result *result, struct ast *a)
{
  memset(result, 0, sizeof(struct daap_result));
  sql_from_ast(result, a);
  ast_free(a);
  return result->err;
}
}

%union {
  char *str;
  int ival;
  struct ast *ast;
}

%token<str> DAAP_T_KEY
%token<str> DAAP_T_VALUE
%token<str> DAAP_T_WILDCARD

%token DAAP_T_EQUAL
%token DAAP_T_NOT
%token DAAP_T_QUOTE
%token DAAP_T_PARENS
%token DAAP_T_NEWLINE

%left DAAP_T_AND DAAP_T_OR

%type <ast> expr
%type <ival> bool

%%

query:
  expr                           { if (result_set(result, $1) < 0) YYABORT; }
| expr DAAP_T_NEWLINE            { if (result_set(result, $1) < 0) YYABORT; }
;

expr:
  expr DAAP_T_AND expr           { $$ = ast_new(DAAP_T_AND, $1, $3); }
| expr DAAP_T_OR expr            { $$ = ast_new(DAAP_T_OR, $1, $3); }
| '(' expr ')'                   { $$ = ast_new(DAAP_T_PARENS, $2, NULL); }
;

expr:
  DAAP_T_QUOTE DAAP_T_KEY bool DAAP_T_VALUE DAAP_T_QUOTE    { $$ = ast_new($3, ast_data(DAAP_T_KEY, $2), ast_data(DAAP_T_VALUE, $4)); }
| DAAP_T_QUOTE DAAP_T_KEY bool DAAP_T_QUOTE                 { $$ = ast_new($3, ast_data(DAAP_T_KEY, $2), ast_data(DAAP_T_VALUE, NULL)); }
| DAAP_T_QUOTE DAAP_T_KEY bool DAAP_T_WILDCARD DAAP_T_QUOTE { $$ = ast_new($3, ast_data(DAAP_T_KEY, $2), ast_data(DAAP_T_WILDCARD, $4)); }
;

bool:
  DAAP_T_EQUAL                   { $$ = DAAP_T_EQUAL; }
| DAAP_T_NOT                     { $$ = DAAP_T_NOT; }
;

%%

