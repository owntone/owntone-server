/*
 * $Id$
 */

#ifndef _SMART_PARSER_H_
#define _SMART_PARSER_H_

typedef void* PARSETREE;

extern PARSETREE sp_init(void);
extern int sp_parse(PARSETREE *tree, char *term);
extern int sp_dispose(PARSETREE tree);
extern char *sp_geterror(PARSETREE tree);
char *sp_sql_clause(PARSETREE tree);

#endif /* _SMART_PARSER_H_ */

