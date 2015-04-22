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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "logger.h"
#include "misc.h"
#include "daap_query.h"

#include "DAAPLexer.h"
#include "DAAPParser.h"
#include "DAAP2SQL.h"


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

  if (!daap_query)
    {
      DPRINTF(E_LOG, L_DAAP, "DAAP query is null\n");
      return NULL;
    }

  DPRINTF(E_DBG, L_DAAP, "Trying DAAP query -%s-\n", daap_query);

#if ANTLR3C_NEW_INPUT
  query = antlr3StringStreamNew ((pANTLR3_UINT8)daap_query, ANTLR3_ENC_8BIT, (ANTLR3_UINT64)strlen(daap_query), (pANTLR3_UINT8)"DAAP query");
#else
  query = antlr3NewAsciiStringInPlaceStream ((pANTLR3_UINT8)daap_query, (ANTLR3_UINT64)strlen(daap_query), (pANTLR3_UINT8)"DAAP query");
#endif
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
      DPRINTF(E_LOG, L_DAAP, "Invalid DAAP query -%s-\n", daap_query);
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
