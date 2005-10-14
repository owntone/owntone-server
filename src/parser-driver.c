/*
 * Test harness for the parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "err.h"
#include "smart-parser.h"

void usage(void) {
    printf("Usage:\n\n  parser [-d <debug level>] \"phrase\"\n\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    int option;
    PARSETREE pt;

    while((option = getopt(argc, argv, "d:")) != -1) {
        switch(option) {
        case 'd':
            err_debuglevel = atoi(optarg);
            break;
        default:
            fprintf(stderr,"Error: unknown option (%c)\n\n",option);
            usage();
        }
    }


    printf("Parsing %s\n",argv[optind]);

    pt=sp_init();
    sp_parse(pt,argv[optind]);
    sp_dispose(pt);

    printf("Done!\n");
    return 0;
}
