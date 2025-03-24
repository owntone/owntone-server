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
%define api.prefix {mpd_}

/* Gives better errors than "syntax error" */
%define parse.error verbose

/* Enables debug mode */
%define parse.trace

/* Adds output parameter to the parser */
%parse-param {struct mpd_result *result}

/* Adds "scanner" as argument to the parses calls to yylex, which is required
   when the lexer is in reentrant mode. The type is void because caller caller
   shouldn't need to know about yyscan_t */
%param {void *scanner}

%code provides {
/* Convenience functions for caller to use instead of interfacing with lexer and
   parser directly */
int mpd_lex_cb(char *input, void (*cb)(int, const char *));
int mpd_lex_parse(struct mpd_result *result, const char *input);
}

/* Implementation of the convenience function and the parsing error function
   required by Bison */
%code {
  #include "mpd_lexer.h"

  int mpd_lex_cb(char *input, void (*cb)(int, const char *))
  {
    int ret;
    yyscan_t scanner;
    YY_BUFFER_STATE buf;
    YYSTYPE val;

    if ((ret = mpd_lex_init(&scanner)) != 0)
      return ret;

    buf = mpd__scan_string(input, scanner);

    while ((ret = mpd_lex(&val, scanner)) > 0)
      cb(ret, mpd_get_text(scanner));

    mpd__delete_buffer(buf, scanner);
    mpd_lex_destroy(scanner);
    return 0;
  }

  int mpd_lex_parse(struct mpd_result *result, const char *input)
  {
    YY_BUFFER_STATE buffer;
    yyscan_t scanner;
    int retval = -1;
    int ret;

    result->errmsg[0] = '\0'; // For safety

    ret = mpd_lex_init(&scanner);
    if (ret != 0)
      goto error_init;

    buffer = mpd__scan_string(input, scanner);
    if (!buffer)
      goto error_buffer;

    ret = mpd_parse(result, scanner);
    if (ret != 0)
      goto error_parse;

    retval = 0;

   error_parse:
    mpd__delete_buffer(buffer, scanner);
   error_buffer:
    mpd_lex_destroy(scanner);
   error_init:
    return retval;
  }

  void mpd_error(struct mpd_result *result, yyscan_t scanner, const char *msg)
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
#define RECURSION_MAX 64
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

/* Definition of struct that will hold the parsing result
 * Some users have sizeable smart playlists, e.g. listing many artist names,
 * which translate to sizeable sql queries.
 */
%code requires {
struct mpd_result_part {
  char str[8192];
  int offset;
};

struct mpd_result {
  struct mpd_result_part where_part;
  struct mpd_result_part order_part;
  struct mpd_result_part group_part;
  char tagtype_buf[64];
  char position_buf[64];

  // Pointers to the strings in mpd_result_part
  const char *where;
  const char *order;
  const char *group;

  const char *tagtype;
  const char *position;

  // Set to 0 if not found
  int offset;
  int limit;

  int err;
  char errmsg[128];

  int recursion_level;
};

enum mpd_type {
  MPD_TYPE_INT,
  MPD_TYPE_STRING,
  MPD_TYPE_SPECIAL,
};

struct mpd_tag_map {
  const char *name;
  const char *dbcol;
  enum mpd_type type;
  int dbmfi_offset;
};

char *mpd_parser_quoted(const char *str);
struct mpd_tag_map *mpd_parser_tag_from_dbcol(const char *dbcol);
void mpd_parser_enum_tagtypes(void (*func)(struct mpd_tag_map *, void *), void *arg);
}

%code {
enum sql_append_type {
  SQL_APPEND_OPERATOR,
  SQL_APPEND_OPERATOR_STR,
  SQL_APPEND_OPERATOR_LIKE,
  SQL_APPEND_FIELD,
  SQL_APPEND_STR,
  SQL_APPEND_INT,
  SQL_APPEND_TIME,
  SQL_APPEND_ORDER,
  SQL_APPEND_PARENS,
};

static struct mpd_tag_map mpd_tag_map[] =
{
  { "Artist",             "f.artist",             MPD_TYPE_STRING,     dbmfi_offsetof(artist),            },
  { "ArtistSort",         "f.artist_sort",        MPD_TYPE_STRING,     dbmfi_offsetof(artist_sort),       },
  { "AlbumArtist",        "f.album_artist",       MPD_TYPE_STRING,     dbmfi_offsetof(album_artist),      },
  { "AlbumArtistSort",    "f.album_artist_sort",  MPD_TYPE_STRING,     dbmfi_offsetof(album_artist_sort), },
  { "Album",              "f.album",              MPD_TYPE_STRING,     dbmfi_offsetof(album),             },
  { "AlbumSort",          "f.album_sort",         MPD_TYPE_STRING,     dbmfi_offsetof(album_sort),        },
  { "Title",              "f.title",              MPD_TYPE_STRING,     dbmfi_offsetof(title),             },
  { "TitleSort",          "f.title_sort",         MPD_TYPE_STRING,     dbmfi_offsetof(title_sort),        },
  { "Genre",              "f.genre",              MPD_TYPE_STRING,     dbmfi_offsetof(genre),             },
  { "Composer",           "f.composer",           MPD_TYPE_STRING,     dbmfi_offsetof(composer),          },
  { "ComposerSort",       "f.composer_sort",      MPD_TYPE_STRING,     dbmfi_offsetof(composer_sort),     },
  { "file",               "f.virtual_path",       MPD_TYPE_SPECIAL,    dbmfi_offsetof(virtual_path),      },

  { "base",               "f.virtual_path",       MPD_TYPE_SPECIAL,    dbmfi_offsetof(virtual_path),      },

  { "Track",              "f.track",              MPD_TYPE_INT,        dbmfi_offsetof(track),             },
  { "Disc",               "f.disc",               MPD_TYPE_INT,        dbmfi_offsetof(disc),              },
  { "Date",               "f.year",               MPD_TYPE_INT,        dbmfi_offsetof(year),              },

  { "modified-since",     "f.time_modified",      MPD_TYPE_SPECIAL,    dbmfi_offsetof(time_modified),     },
  { "added-since",        "f.time_added",         MPD_TYPE_SPECIAL,    dbmfi_offsetof(time_added),        },

  // AudioFormat tag
  { "samplerate",         "f.samplerate",         MPD_TYPE_INT,        dbmfi_offsetof(samplerate),        },
  { "bits_per_sample",    "f.bits_per_sample",    MPD_TYPE_INT,        dbmfi_offsetof(bits_per_sample),   },
  { "channels",           "f.channels",           MPD_TYPE_INT,        dbmfi_offsetof(channels),          },

  { NULL },
};

static const char *
tag_to_dbcol(const char *tag)
{
  struct mpd_tag_map *mapptr;

  for (mapptr = mpd_tag_map; mapptr->name; mapptr++)
    {
      if (strcasecmp(tag, mapptr->name) == 0)
        return mapptr->dbcol;
    }

  return "error"; // Should never happen, means tag_to_db_map is out of sync with lexer
}

struct mpd_tag_map *
mpd_parser_tag_from_dbcol(const char *dbcol)
{
  struct mpd_tag_map *mapptr;

  if (!dbcol)
    return NULL;

  for (mapptr = mpd_tag_map; mapptr->name; mapptr++)
    {
      if (strcasecmp(dbcol, mapptr->dbcol) == 0)
        return mapptr;
    }

  return NULL;
}

void
mpd_parser_enum_tagtypes(void (*func)(struct mpd_tag_map *, void *), void *arg)
{
  struct mpd_tag_map *mapptr;

  for (mapptr = mpd_tag_map; mapptr->name; mapptr++)
    {
      func(mapptr, arg);
    }
}

// Remove any backslash that was used to escape single or double quotes
char *
mpd_parser_quoted(const char *str)
{
  char *out = strdup(str + 1); // Copy from after the first quote
  size_t len = strlen(out);
  const char *src;
  char *dst;

  out[len - 1] = '\0'; // Remove terminating quote

  // Remove escaping backslashes
  for (src = dst = out; *src != '\0'; src++, dst++)
    {
      if (*src == '\\')
	src++;
      if (*src == '\0')
	break;

      *dst = *src;
    }

  *dst = '\0';

  return out;
}

static void sql_from_ast(struct mpd_result *, struct mpd_result_part *, struct ast *);

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

static void sql_append(struct mpd_result *result, struct mpd_result_part *part, const char *fmt, ...)
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

static void sql_append_recursive(struct mpd_result *result, struct mpd_result_part *part, struct ast *a, const char *op, const char *op_not, bool is_not, enum sql_append_type append_type)
{
  char escape_char;

  if (result->recursion_level > RECURSION_MAX)
  {
    snprintf(result->errmsg, sizeof(result->errmsg), "Recursion maximum exceeded");
    result->err = -2;
    return;
  }

  result->recursion_level++;

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
      sql_append(result, part, " %s '%s", is_not ? op_not : op, a->type == MPD_T_STARTSWITH ? "" : "%");
      sql_like_escape((char **)(&a->r->data), &escape_char);
      sql_from_ast(result, part, a->r);
      sql_append(result, part, "%s'", a->type == MPD_T_ENDSWITH ? "" : "%");
      if (escape_char)
        sql_append(result, part, " ESCAPE '%c'", escape_char);
      break;
    case SQL_APPEND_FIELD:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_append(result, part, "%s", tag_to_dbcol((char *)a->data));
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
    case SQL_APPEND_TIME:
      assert(a->l == NULL);
      assert(a->r == NULL);
      sql_str_escape((char **)&a->data);
      // MPD docs say the value can be a unix timestamp or ISO 8601
      sql_append(result, part, "strftime('%%s', datetime('%s', '%s'))", (char *)a->data, strchr((char *)a->data, '-') ? "utc" : "unixepoch");
      break;
    case SQL_APPEND_ORDER:
      assert(a->l == NULL);
      assert(a->r == NULL);
      if (a->data)
        sql_append(result, part, "%s ", tag_to_dbcol((char *)a->data));
      sql_append(result, part, "%s", is_not ? op_not : op);
      break;
    case SQL_APPEND_PARENS:
      assert(a->r == NULL);
      if (is_not ? op_not : op)
        sql_append(result, part, "%s ", is_not ? op_not : op);
      sql_append(result, part, "(");
      sql_from_ast(result, part, a->l);
      sql_append(result, part, ")");
      break;
  }

  result->recursion_level--;
}

/* Creates the parsing result from the AST. Errors are set via result->err. */
static void sql_from_ast(struct mpd_result *result, struct mpd_result_part *part, struct ast *a) {
  if (!a || result->err < 0)
    return;

  bool is_not = (a->type & INVERT_MASK);
  a->type &= ~INVERT_MASK;

  switch (a->type)
  {
    case MPD_T_LESS:
      sql_append_recursive(result, part, a, "<", ">=", is_not, SQL_APPEND_OPERATOR); break;
    case MPD_T_LESSEQUAL:
      sql_append_recursive(result, part, a, "<=", ">", is_not, SQL_APPEND_OPERATOR); break;
    case MPD_T_GREATER:
      sql_append_recursive(result, part, a, ">", ">=", is_not, SQL_APPEND_OPERATOR); break;
    case MPD_T_GREATEREQUAL:
      sql_append_recursive(result, part, a, ">=", "<", is_not, SQL_APPEND_OPERATOR); break;
    case MPD_T_EQUAL:
      sql_append_recursive(result, part, a, "=", "!=", is_not, SQL_APPEND_OPERATOR_STR); break;
    case MPD_T_NOTEQUAL:
      sql_append_recursive(result, part, a, "!=", "=", is_not, SQL_APPEND_OPERATOR_STR); break;
    case MPD_T_CONTAINS:
    case MPD_T_STARTSWITH:
    case MPD_T_ENDSWITH:
      sql_append_recursive(result, part, a, "LIKE", "NOT LIKE", is_not, SQL_APPEND_OPERATOR_LIKE); break;
    case MPD_T_AND:
      sql_append_recursive(result, part, a, "AND", "AND NOT", is_not, SQL_APPEND_OPERATOR); break;
    case MPD_T_OR:
      sql_append_recursive(result, part, a, "OR", "OR NOT", is_not, SQL_APPEND_OPERATOR); break;
    case MPD_T_STRING:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_STR); break;
    case MPD_T_STRTAG:
    case MPD_T_INTTAG:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_FIELD); break;
    case MPD_T_NUM:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_INT); break;
    case MPD_T_TIME:
      sql_append_recursive(result, part, a, NULL, NULL, 0, SQL_APPEND_TIME); break;
    case MPD_T_SORT:
      sql_append_recursive(result, part, a, "ASC", "DESC", is_not, SQL_APPEND_ORDER); break;
    case MPD_T_GROUP:
      sql_append_recursive(result, part, a, ",", ",", is_not, SQL_APPEND_OPERATOR); break;
    case MPD_T_PARENS:
      sql_append_recursive(result, part, a, NULL, "NOT", is_not, SQL_APPEND_PARENS); break;
    default:
      snprintf(result->errmsg, sizeof(result->errmsg), "Parser produced unrecognized AST type %d", a->type);
      result->err = -1;
  }
}

static int result_set(struct mpd_result *result, char *tagtype, struct ast *filter, struct ast *sort, struct ast *window, char *position, struct ast *group)
{
  memset(result, 0, sizeof(struct mpd_result));

  if (tagtype)
    {
      snprintf(result->tagtype_buf, sizeof(result->tagtype_buf), "%s", tag_to_dbcol(tagtype));
      result->tagtype = result->tagtype_buf;
    }

  sql_from_ast(result, &result->where_part, filter);
  if (result->where_part.offset)
    result->where = result->where_part.str;

  sql_from_ast(result, &result->order_part, sort);
  if (result->order_part.offset)
    result->order = result->order_part.str;

  sql_from_ast(result, &result->group_part, group);
  if (tagtype)
    sql_append(result, &result->group_part, result->group_part.offset ? " , %s" : "%s", tag_to_dbcol(tagtype));
  if (result->group_part.offset)
    result->group = result->group_part.str;

  if (position)
    {
      snprintf(result->position_buf, sizeof(result->position_buf), "%s", position);
      result->position = result->position_buf;
    }

  if (window && window->l->ival <= window->r->ival)
    {
      result->offset = window->l->ival;
      result->limit = window->r->ival - window->l->ival + 1;
    }

  free(tagtype);
  free(position);
  ast_free(filter);
  ast_free(sort);
  ast_free(group);
  ast_free(window);

  return result->err;
}

static struct ast * ast_new_strnode(int type, const char *tag, const char *value)
{
  return ast_new(type, ast_data(MPD_T_STRTAG, strdup(tag)), ast_data(MPD_T_STRING, strdup(value))); 
}

/* This creates an OR ast tree with each tag in the tags array */
static struct ast * ast_new_any(int type, const char *value)
{
  const char *tags[] = { "albumartist", "artist", "album", "title", NULL, };
  const char **tagptr = tags;
  struct ast *a;
  int op = (type == MPD_T_NOTEQUAL) ? MPD_T_AND : MPD_T_OR;

  a = ast_new_strnode(type, *tagptr, value);
  for (tagptr++; *tagptr; tagptr++)
    a = ast_new(op, a, ast_new_strnode(type, *tagptr, value));

  // In case the expression will be negated, e.g. !(any contains 'cat'), we must
  // group the result
  return ast_new(MPD_T_PARENS, a, NULL);
}

static struct ast * ast_new_audioformat(int type, const char *value)
{
  const char *tags[] = { "samplerate", "bits_per_sample", "channels", NULL, };
  const char **tagptr = tags;
  char *value_copy = strdup(value);
  char *saveptr;
  char *token;
  struct ast *a;

  token = strtok_r(value_copy, ":", &saveptr);

  a = ast_new_strnode(type, "samplerate", value_copy);
  if (!token)
    goto end; // If there was no ':' we assume we just have a samplerate

  for (tagptr++, token = strtok_r(NULL, ":", &saveptr); *tagptr && token; tagptr++, token = strtok_r(NULL, ":", &saveptr))
    a = ast_new(MPD_T_AND, a, ast_new_strnode(type, *tagptr, token));

 end:
  free(value_copy);

  // In case the expression will be negated we group the result
  return ast_new(MPD_T_PARENS, a, NULL);
}
}

%union {
  unsigned int ival;
  char *str;
  struct ast *ast;
}

/* mpd commands */
%token MPD_T_CMDSEARCH
%token MPD_T_CMDSEARCHADD
%token MPD_T_CMDFIND
%token MPD_T_CMDFINDADD
%token MPD_T_CMDCOUNT
%token MPD_T_CMDSEARCHCOUNT
%token MPD_T_CMDPLAYLISTFIND
%token MPD_T_CMDPLAYLISTSEARCH
%token MPD_T_CMDLIST

%token MPD_T_SORT
%token MPD_T_WINDOW
%token MPD_T_POSITION
%token MPD_T_GROUP

/* A string that was quoted. Quotes were stripped by lexer. */
%token <str> MPD_T_STRING

/* Numbers (integers) */
%token <ival> MPD_T_NUM

/* Since time is a quoted string the lexer will just use a MPD_T_STRING token.
   The parser will recognize it as time based on the keyword and use MPD_T_TIME
   for the AST tree. */
%token MPD_T_TIME

/* The semantic value holds the actual name of the field */
%token <str> MPD_T_STRTAG
%token <str> MPD_T_INTTAG
%token <str> MPD_T_SINCETAG
%token <str> MPD_T_BASETAG

%token MPD_T_ANYTAG
%token MPD_T_AUDIOFORMATTAG
%token MPD_T_PARENS
%token MPD_T_OR
%token MPD_T_AND
%token MPD_T_NOT

/* The below are only ival so we can set intbool, datebool and strbool via the
   default rule for semantic values, i.e. $$ = $1. The semantic value (ival) is
   set to the token value by the lexer. */
%token <ival> MPD_T_CONTAINS
%token <ival> MPD_T_STARTSWITH
%token <ival> MPD_T_ENDSWITH
%token <ival> MPD_T_EQUAL
%token <ival> MPD_T_NOTEQUAL
%token <ival> MPD_T_LESS
%token <ival> MPD_T_LESSEQUAL
%token <ival> MPD_T_GREATER
%token <ival> MPD_T_GREATEREQUAL

%left MPD_T_OR
%left MPD_T_AND
%left MPD_T_NOT

%type <str> tagtype
%type <ast> filter
%type <ast> sort
%type <ast> window
%type <str> position
%type <ast> group
%type <ast> groups
%type <ast> predicate
%type <ival> strbool
%type <ival> intbool

%%

/*
 * Version 0.24:
 * Type cmd_fsw (filter sort window):
 *  playlistfind {FILTER} [sort {TYPE}] [window {START:END}]
 *  playlistsearch {FILTER} [sort {TYPE}] [window {START:END}]
 *  find {FILTER} [sort {TYPE}] [window {START:END}]
 *  search {FILTER} [sort {TYPE}] [window {START:END}]
 * Type fswp (filter sort window position):
 *  findadd {FILTER} [sort {TYPE}] [window {START:END}] [position POS]
 *  searchadd {FILTER} [sort {TYPE}] [window {START:END}] [position POS]
 * Type fg (filter group):
 *  count {FILTER} [group {GROUPTYPE} group {GROUPTYPE} ...]
 *  searchcount {FILTER} [group {GROUPTYPE} group {GROUPTYPE} ...]
 * Type tfg (type filter group):
 *  list {TYPE} {FILTER} [group {GROUPTYPE} group {GROUPTYPE} ...]
 * Not implemented:
 *  searchaddpl {NAME} {FILTER} [sort {TYPE}] [window {START:END}] [position POS]
 *  searchplaylist {NAME} {FILTER} [{START:END}]
 *  case sensitivity for find
 */

command: cmd_fsw filter sort window                        { if (result_set(result, NULL, $2, $3, $4, NULL, NULL) < 0) YYABORT; }
| cmd_fswp filter sort window position                     { if (result_set(result, NULL, $2, $3, $4, $5, NULL) < 0) YYABORT; }
| cmd_fg filter groups                                     { if (result_set(result, NULL, $2, NULL, NULL, NULL, $3) < 0) YYABORT; }
| cmd_fg groups                                            { if (result_set(result, NULL, NULL, NULL, NULL, NULL, $2) < 0) YYABORT; }
| cmd_tfg tagtype filter groups                            { if (result_set(result, $2, $3, NULL, NULL, NULL, $4) < 0) YYABORT; }
| cmd_tfg tagtype groups                                   { if (result_set(result, $2, NULL, NULL, NULL, NULL, $3) < 0) YYABORT; }
;

tagtype: MPD_T_STRTAG                                      { if (asprintf(&($$), "%s", $1) < 0) YYABORT; }
;

filter: filter MPD_T_AND filter                            { $$ = ast_new(MPD_T_AND, $1, $3); }
| filter MPD_T_OR filter                                   { $$ = ast_new(MPD_T_OR, $1, $3); }
| '(' filter ')'                                           { $$ = ast_new(MPD_T_PARENS, $2, NULL); }
| MPD_T_NOT filter                                         { struct ast *a = $2; a->type |= INVERT_MASK; $$ = $2; }
| predicate
;

sort: MPD_T_SORT MPD_T_STRTAG                              { $$ = ast_data(MPD_T_SORT, $2); }
| MPD_T_SORT '-' MPD_T_STRTAG                              { $$ = ast_data(MPD_T_SORT | INVERT_MASK, $3); }
| %empty                                                   { $$ = NULL; }
;

window: MPD_T_WINDOW MPD_T_NUM ':' MPD_T_NUM               { $$ = ast_new(MPD_T_WINDOW, ast_int(MPD_T_NUM, $2), ast_int(MPD_T_NUM, $4)); }
| %empty                                                   { $$ = NULL; }
;

position: MPD_T_POSITION MPD_T_NUM                         { if (asprintf(&($$), "%d", $2) < 0) YYABORT; }
| MPD_T_POSITION '+' MPD_T_NUM                             { if (asprintf(&($$), "+%d", $3) < 0) YYABORT; }
| MPD_T_POSITION '-' MPD_T_NUM                             { if (asprintf(&($$), "-%d", $3) < 0) YYABORT; }
| %empty                                                   { $$ = NULL; }
;

groups: groups group                                       { $$ = $1 ? ast_new(MPD_T_GROUP, $2, $1) : $2; }
| %empty                                                   { $$ = NULL; }
;

group: MPD_T_GROUP MPD_T_STRTAG                            { $$ = ast_data(MPD_T_STRTAG, $2); }
| MPD_T_GROUP MPD_T_INTTAG                                 { $$ = ast_data(MPD_T_INTTAG, $2); }
;

// We accept inttags with numeric and string values, so both date == 2007 and date == '2007'
predicate: '(' MPD_T_STRTAG strbool MPD_T_STRING ')'       { $$ = ast_new($3, ast_data(MPD_T_STRTAG, $2), ast_data(MPD_T_STRING, $4)); }
| '(' MPD_T_INTTAG strbool MPD_T_STRING ')'                { $$ = ast_new($3, ast_data(MPD_T_STRTAG, $2), ast_data(MPD_T_STRING, $4)); }
| '(' MPD_T_INTTAG intbool MPD_T_NUM ')'                   { $$ = ast_new($3, ast_data(MPD_T_INTTAG, $2), ast_int(MPD_T_NUM, $4)); }
| '(' MPD_T_ANYTAG strbool MPD_T_STRING ')'                { $$ = ast_new_any($3, $4); }
| '(' MPD_T_AUDIOFORMATTAG strbool MPD_T_STRING ')'        { $$ = ast_new_audioformat($3, $4); }
| '(' MPD_T_SINCETAG MPD_T_STRING ')'                      { $$ = ast_new(MPD_T_GREATEREQUAL, ast_data(MPD_T_STRTAG, $2), ast_data(MPD_T_TIME, $3)); }
| '(' MPD_T_BASETAG MPD_T_STRING ')'                       { $$ = ast_new(MPD_T_STARTSWITH, ast_data(MPD_T_STRTAG, $2), ast_data(MPD_T_STRING, $3)); }
;

strbool: MPD_T_EQUAL
| MPD_T_NOTEQUAL
| MPD_T_CONTAINS
| MPD_T_STARTSWITH
| MPD_T_ENDSWITH
;

intbool: MPD_T_LESS
| MPD_T_LESSEQUAL
| MPD_T_GREATER
| MPD_T_GREATEREQUAL
;

cmd_fsw: MPD_T_CMDPLAYLISTFIND
| MPD_T_CMDPLAYLISTSEARCH
| MPD_T_CMDSEARCH
| MPD_T_CMDFIND
;

cmd_fswp: MPD_T_CMDFINDADD
| MPD_T_CMDSEARCHADD
;

cmd_fg: MPD_T_CMDCOUNT
| MPD_T_CMDSEARCHCOUNT
;

cmd_tfg: MPD_T_CMDLIST
;

%%

