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
#include "daap_query.h"

#include "DAAPLexer.h"
#include "DAAPParser.h"
#include "DAAP2SQL.h"


static struct dmap_query_field_map dmap_query_fields[] =
{
  { 0, 0, "dmap.itemname",              "title" },
  { 0, 1, "dmap.itemid",                "id" },
  { 0, 0, "daap.songalbum",             "album" },
  { 0, 1, "daap.songalbumid",           "songalbumid" },
  { 0, 0, "daap.songartist",            "artist" },
  { 0, 0, "daap.songalbumartist",       "album_artist" },
  { 0, 1, "daap.songbitrate",           "bitrate" },
  { 0, 0, "daap.songcomment",           "comment" },
  { 0, 1, "daap.songcompilation",       "compilation" },
  { 0, 0, "daap.songcomposer",          "composer" },
  { 0, 1, "daap.songdatakind",          "data_kind" },
  { 0, 0, "daap.songdataurl",           "url" },
  { 0, 1, "daap.songdateadded",         "time_added" },
  { 0, 1, "daap.songdatemodified",      "time_modified" },
  { 0, 0, "daap.songdescription",       "description" },
  { 0, 1, "daap.songdisccount",         "total_discs" },
  { 0, 1, "daap.songdiscnumber",        "disc" },
  { 0, 0, "daap.songformat",            "type" },
  { 0, 0, "daap.songgenre",             "genre" },
  { 0, 1, "daap.songsamplerate",        "samplerate" },
  { 0, 1, "daap.songsize",              "file_size" },
  { 0, 1, "daap.songstoptime",          "song_length" },
  { 0, 1, "daap.songtime",              "song_length" },
  { 0, 1, "daap.songtrackcount",        "total_tracks" },
  { 0, 1, "daap.songtracknumber",       "track" },
  { 0, 1, "daap.songyear",              "year" },
  { 0, 1, "com.apple.itunes.mediakind", "media_kind" },

  { -1, -1, NULL, NULL }
};

static avl_tree_t *dmap_query_fields_hash;


static int
dmap_query_field_map_compare(const void *aa, const void *bb)
{
  struct dmap_query_field_map *a = (struct dmap_query_field_map *)aa;
  struct dmap_query_field_map *b = (struct dmap_query_field_map *)bb;

  if (a->hash < b->hash)
    return -1;

  if (a->hash > b->hash)
    return 1;

  return 0;
}


struct dmap_query_field_map *
daap_query_field_lookup(char *field)
{
  struct dmap_query_field_map dqfm;
  avl_node_t *node;

  dqfm.hash = djb_hash(field, strlen(field));

  node = avl_search(dmap_query_fields_hash, &dqfm);
  if (!node)
    return NULL;

  return (struct dmap_query_field_map *)node->item;
}

char *
daap_query_parse_sql(const char *daap_query)
{
  /* Input DAAP query, fed to the lexer */
  pANTLR3_INPUT_STREAM query;

  /* Lexer and the resulting token stream, fed to the parser */
  pDAAPLexer lxr;
  pANTLR3_COMMON_TOKEN_STREAM tkstream;

  /* Parser and the resulting AST, fed to the tree parser */
  pDAAPParser psr;
  DAAPParser_query_return qtree;
  pANTLR3_COMMON_TREE_NODE_STREAM nodes;

  /* Tree parser and the resulting SQL query string */
  pDAAP2SQL sqlconv;
  pANTLR3_STRING sql;

  char *ret = NULL;

  DPRINTF(E_DBG, L_DAAP, "Trying DAAP query -%s-\n", daap_query);

  query = antlr3NewAsciiStringInPlaceStream ((pANTLR3_UINT8)daap_query, (ANTLR3_UINT64)strlen(daap_query), (pANTLR3_UINT8)"DAAP query");
  if (!query)
    {
      DPRINTF(E_DBG, L_DAAP, "Could not create input stream\n");
      return NULL;
    }

  lxr = DAAPLexerNew(query);
  if (!lxr)
    {
      DPRINTF(E_DBG, L_DAAP, "Could not create DAAP lexer\n");
      goto lxr_fail;
    }

  tkstream = antlr3CommonTokenStreamSourceNew(ANTLR3_SIZE_HINT, TOKENSOURCE(lxr));
  if (!tkstream)
    {
      DPRINTF(E_DBG, L_DAAP, "Could not create DAAP token stream\n");
      goto tkstream_fail;
    }

  psr = DAAPParserNew(tkstream);
  if (!psr)
    {
      DPRINTF(E_DBG, L_DAAP, "Could not create DAAP parser\n");
      goto psr_fail;
    }

  qtree = psr->query(psr);

  /* Check for parser errors */
  if (psr->pParser->rec->state->errorCount > 0)
    {
      DPRINTF(E_LOG, L_DAAP, "DAAP query parser terminated with %d errors\n", psr->pParser->rec->state->errorCount);
      goto psr_error;
    }

  DPRINTF(E_SPAM, L_DAAP, "DAAP query AST:\n\t%s\n", qtree.tree->toStringTree(qtree.tree)->chars);

  nodes = antlr3CommonTreeNodeStreamNewTree(qtree.tree, ANTLR3_SIZE_HINT);
  if (!nodes)
    {
      DPRINTF(E_DBG, L_DAAP, "Could not create node stream\n");
      goto psr_error;
    }

  sqlconv = DAAP2SQLNew(nodes);
  if (!sqlconv)
    {
      DPRINTF(E_DBG, L_DAAP, "Could not create SQL converter\n");
      goto sql_fail;
    }

  sql = sqlconv->query(sqlconv);

  /* Check for tree parser errors */
  if (sqlconv->pTreeParser->rec->state->errorCount > 0)
    {
      DPRINTF(E_LOG, L_DAAP, "DAAP query tree parser terminated with %d errors\n", sqlconv->pTreeParser->rec->state->errorCount);
      goto sql_error;
    }

  if (sql)
    {
      DPRINTF(E_DBG, L_DAAP, "DAAP SQL query: -%s-\n", sql->chars);
      ret = strdup((char *)sql->chars);
    }
  else
    {
      DPRINTF(E_LOG, L_DAAP, "Invalid DAAP query\n");
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
daap_query_init(void)
{
  avl_node_t *node;
  struct dmap_query_field_map *dqfm;
  int i;

  dmap_query_fields_hash = avl_alloc_tree(dmap_query_field_map_compare, NULL);
  if (!dmap_query_fields_hash)
    {
      DPRINTF(E_FATAL, L_DAAP, "DAAP query init could not allocate AVL tree\n");

      return -1;
    }

  for (i = 0; dmap_query_fields[i].hash == 0; i++)
    {
      dmap_query_fields[i].hash = djb_hash(dmap_query_fields[i].dmap_field, strlen(dmap_query_fields[i].dmap_field));

      node = avl_insert(dmap_query_fields_hash, &dmap_query_fields[i]);
      if (!node)
        {
          if (errno != EEXIST)
            DPRINTF(E_FATAL, L_DAAP, "DAAP query init failed; AVL insert error: %s\n", strerror(errno));
          else
            {
              node = avl_search(dmap_query_fields_hash, &dmap_query_fields[i]);
              dqfm = node->item;

              DPRINTF(E_FATAL, L_DAAP, "DAAP query init failed; WARNING: duplicate hash key\n");
              DPRINTF(E_FATAL, L_DAAP, "Hash %x, string %s\n", dmap_query_fields[i].hash, dmap_query_fields[i].dmap_field);

              DPRINTF(E_FATAL, L_DAAP, "Hash %x, string %s\n", dqfm->hash, dqfm->dmap_field);
            }

          goto avl_insert_fail;
        }
    }

  return 0;

 avl_insert_fail:
  avl_free_tree(dmap_query_fields_hash);

  return -1;
}

void
daap_query_deinit(void)
{
  avl_free_tree(dmap_query_fields_hash);
}
