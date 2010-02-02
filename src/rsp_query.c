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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <avl.h>

#include "logger.h"
#include "misc.h"
#include "rsp_query.h"

#include "RSPLexer.h"
#include "RSPParser.h"
#include "RSP2SQL.h"


static struct rsp_query_field_map rsp_query_fields[] =
{
  { 0, RSP_TYPE_INT,    "id" },
  { 0, RSP_TYPE_STRING, "path" },
  { 0, RSP_TYPE_STRING, "fname" },
  { 0, RSP_TYPE_STRING, "title" },
  { 0, RSP_TYPE_STRING, "artist" },
  { 0, RSP_TYPE_STRING, "album" },
  { 0, RSP_TYPE_STRING, "genre" },
  { 0, RSP_TYPE_STRING, "comment" },
  { 0, RSP_TYPE_STRING, "type" },
  { 0, RSP_TYPE_STRING, "composer" },
  { 0, RSP_TYPE_STRING, "orchestra" },
  { 0, RSP_TYPE_STRING, "grouping" },
  { 0, RSP_TYPE_STRING, "url" },
  { 0, RSP_TYPE_INT,    "bitrate" },
  { 0, RSP_TYPE_INT,    "samplerate" },
  { 0, RSP_TYPE_INT,    "song_length" },
  { 0, RSP_TYPE_INT,    "file_size" },
  { 0, RSP_TYPE_INT,    "year" },
  { 0, RSP_TYPE_INT,    "track" },
  { 0, RSP_TYPE_INT,    "total_tracks" },
  { 0, RSP_TYPE_INT,    "disc" },
  { 0, RSP_TYPE_INT,    "total_discs" },
  { 0, RSP_TYPE_INT,    "bpm" },
  { 0, RSP_TYPE_INT,    "compilation" },
  { 0, RSP_TYPE_INT,    "rating" },
  { 0, RSP_TYPE_INT,    "play_count" },
  { 0, RSP_TYPE_INT,    "data_kind" },
  { 0, RSP_TYPE_INT,    "item_kind" },
  { 0, RSP_TYPE_STRING, "description" },
  { 0, RSP_TYPE_DATE,   "time_added" },
  { 0, RSP_TYPE_DATE,   "time_modified" },
  { 0, RSP_TYPE_DATE,   "time_played" },
  { 0, RSP_TYPE_DATE,   "db_timestamp" },
  { 0, RSP_TYPE_INT,    "sample_count" },
  { 0, RSP_TYPE_STRING, "codectype" },
  { 0, RSP_TYPE_INT,    "idx" },
  { 0, RSP_TYPE_INT,    "has_video" },
  { 0, RSP_TYPE_INT,    "contentrating" },
  { 0, RSP_TYPE_INT,    "bits_per_sample" },
  { 0, RSP_TYPE_STRING, "album_artist" },

  { -1, -1, NULL }
};

static avl_tree_t *rsp_query_fields_hash;


static int
rsp_query_field_map_compare(const void *aa, const void *bb)
{
  struct rsp_query_field_map *a = (struct rsp_query_field_map *)aa;
  struct rsp_query_field_map *b = (struct rsp_query_field_map *)bb;

  if (a->hash < b->hash)
    return -1;

  if (a->hash > b->hash)
    return 1;

  return 0;
}


struct rsp_query_field_map *
rsp_query_field_lookup(char *field)
{
  struct rsp_query_field_map rqfm;
  avl_node_t *node;

  rqfm.hash = djb_hash(field, strlen(field));

  node = avl_search(rsp_query_fields_hash, &rqfm);
  if (!node)
    return NULL;

  return (struct rsp_query_field_map *)node->item;
}

char *
rsp_query_parse_sql(const char *rsp_query)
{
  /* Input RSP query, fed to the lexer */
  pANTLR3_INPUT_STREAM query;

  /* Lexer and the resulting token stream, fed to the parser */
  pRSPLexer lxr;
  pANTLR3_COMMON_TOKEN_STREAM tkstream;

  /* Parser and the resulting AST, fed to the tree parser */
  pRSPParser psr;
  RSPParser_query_return qtree;
  pANTLR3_COMMON_TREE_NODE_STREAM nodes;

  /* Tree parser and the resulting SQL query string */
  pRSP2SQL sqlconv;
  pANTLR3_STRING sql;

  char *ret = NULL;

  DPRINTF(E_DBG, L_RSP, "Trying RSP query -%s-\n", rsp_query);

  query = antlr3NewAsciiStringInPlaceStream ((pANTLR3_UINT8)rsp_query, (ANTLR3_UINT64)strlen(rsp_query), (pANTLR3_UINT8)"RSP query");
  if (!query)
    {
      DPRINTF(E_DBG, L_RSP, "Could not create input stream\n");
      return NULL;
    }

  lxr = RSPLexerNew(query);
  if (!lxr)
    {
      DPRINTF(E_DBG, L_RSP, "Could not create RSP lexer\n");
      goto lxr_fail;
    }

  tkstream = antlr3CommonTokenStreamSourceNew(ANTLR3_SIZE_HINT, TOKENSOURCE(lxr));
  if (!tkstream)
    {
      DPRINTF(E_DBG, L_RSP, "Could not create RSP token stream\n");
      goto tkstream_fail;
    }

  psr = RSPParserNew(tkstream);
  if (!psr)
    {
      DPRINTF(E_DBG, L_RSP, "Could not create RSP parser\n");
      goto psr_fail;
    }

  qtree = psr->query(psr);

  /* Check for parser errors */
  if (psr->pParser->rec->state->errorCount > 0)
    {
      DPRINTF(E_LOG, L_RSP, "RSP query parser terminated with %d errors\n", psr->pParser->rec->state->errorCount);
      goto psr_error;
    }

  DPRINTF(E_SPAM, L_RSP, "RSP query AST:\n\t%s\n", qtree.tree->toStringTree(qtree.tree)->chars);

  nodes = antlr3CommonTreeNodeStreamNewTree(qtree.tree, ANTLR3_SIZE_HINT);
  if (!nodes)
    {
      DPRINTF(E_DBG, L_RSP, "Could not create node stream\n");
      goto psr_error;
    }

  sqlconv = RSP2SQLNew(nodes);
  if (!sqlconv)
    {
      DPRINTF(E_DBG, L_RSP, "Could not create SQL converter\n");
      goto sql_fail;
    }

  sql = sqlconv->query(sqlconv);

  /* Check for tree parser errors */
  if (sqlconv->pTreeParser->rec->state->errorCount > 0)
    {
      DPRINTF(E_LOG, L_RSP, "RSP query tree parser terminated with %d errors\n", sqlconv->pTreeParser->rec->state->errorCount);
      goto sql_error;
    }

  if (sql)
    {
      DPRINTF(E_DBG, L_RSP, "RSP SQL query: -%s-\n", sql->chars);
      ret = strdup((char *)sql->chars);
    }
  else
    {
      DPRINTF(E_LOG, L_RSP, "Invalid RSP query\n");
      ret = NULL;
    }

 sql_error:
  sqlconv->free(sqlconv);
 sql_fail:
  nodes->free(nodes);
 psr_error:
  psr->free(psr);
 psr_fail:
  tkstream->free(tkstream);
 tkstream_fail:
  lxr->free(lxr);
 lxr_fail:
  query->close(query);

  return ret;
}

int
rsp_query_init(void)
{
  avl_node_t *node;
  struct rsp_query_field_map *rqfm;
  int i;

  rsp_query_fields_hash = avl_alloc_tree(rsp_query_field_map_compare, NULL);
  if (!rsp_query_fields_hash)
    {
      DPRINTF(E_FATAL, L_RSP, "RSP query init could not allocate AVL tree\n");

      return -1;
    }

  for (i = 0; rsp_query_fields[i].hash == 0; i++)
    {
      rsp_query_fields[i].hash = djb_hash(rsp_query_fields[i].rsp_field, strlen(rsp_query_fields[i].rsp_field));

      node = avl_insert(rsp_query_fields_hash, &rsp_query_fields[i]);
      if (!node)
        {
          if (errno != EEXIST)
            DPRINTF(E_FATAL, L_RSP, "RSP query init failed; AVL insert error: %s\n", strerror(errno));
          else
            {
              node = avl_search(rsp_query_fields_hash, &rsp_query_fields[i]);
              rqfm = node->item;

              DPRINTF(E_FATAL, L_RSP, "RSP query init failed; WARNING: duplicate hash key\n");
              DPRINTF(E_FATAL, L_RSP, "Hash %x, string %s\n", rsp_query_fields[i].hash, rsp_query_fields[i].rsp_field);

              DPRINTF(E_FATAL, L_RSP, "Hash %x, string %s\n", rqfm->hash, rqfm->rsp_field);
            }

          goto avl_insert_fail;
        }
    }

  return 0;

 avl_insert_fail:
  avl_free_tree(rsp_query_fields_hash);

  return -1;
}

void
rsp_query_deinit(void)
{
  avl_free_tree(rsp_query_fields_hash);
}
