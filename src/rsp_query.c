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
#include "rsp_query.h"

#include "RSPLexer.h"
#include "RSPParser.h"
#include "RSP2SQL.h"


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

#if ANTLR3C_NEW_INPUT
  query = antlr3StringStreamNew ((pANTLR3_UINT8)rsp_query, ANTLR3_ENC_8BIT, (ANTLR3_UINT64)strlen(rsp_query), (pANTLR3_UINT8)"RSP query");
#else
  query = antlr3NewAsciiStringInPlaceStream ((pANTLR3_UINT8)rsp_query, (ANTLR3_UINT64)strlen(rsp_query), (pANTLR3_UINT8)"RSP query");
#endif
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
