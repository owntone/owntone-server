/*
 * Test harness for the parser
 */

#include <stdio.h>
#include "smart-parser.h"

int main(int argc, char *argv[]) {
    PARSETREE pt;

    printf("Parsing %s\n",argv[1]);

    pt=sp_init();
    sp_parse(pt,argv[1]);
    sp_dispose(pt);

    printf("Done!\n");
    return 0;
}
