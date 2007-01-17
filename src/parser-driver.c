/*
 * Test harness for the parser
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "daapd.h"
#include "conf.h"
#include "db-generic.h"
#include "err.h"
#include "smart-parser.h"

CONFIG config;
char *scan_winamp_genre[] = { NULL };

void usage(void) {
    printf("Usage:\n\n  parser [-t <type (0/1)>] [-d <debug level>] \"phrase\"\n\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    int option;
    int type=0;
    PARSETREE pt;
    char *configfile = "/etc/mt-daapd.conf";
    int debuglevel=0;
    char db_type[40];
    char db_parms[PATH_MAX];
    int size;
    int err;
    char *perr;

    while((option = getopt(argc, argv, "d:t:c:")) != -1) {
        switch(option) {
        case 'c':
            configfile = optarg;
            break;
        case 'd':
            debuglevel = atoi(optarg);
            break;
        case 't':
            type = atoi(optarg);
            break;
        default:
            fprintf(stderr,"Error: unknown option (%c)\n\n",option);
            usage();
        }
    }

    //    err_setdebugmask("parse");

    if(conf_read(configfile) != CONF_E_SUCCESS) {
        fprintf(stderr,"could not read config file: %s\n",configfile);
        exit(1);
    }

    if(debuglevel) {
        printf("Setting debug level to %d\n",debuglevel);
        err_setlevel(debuglevel);
        err_setdest(LOGDEST_STDERR);
    }

    size = sizeof(db_type);
    conf_get_string("general","db_type","sqlite",db_type,&size);
    size = sizeof(db_parms);
    conf_get_string("general","db_parms","/var/cache/mt-daapd",db_parms,&size);

    err=db_open(&perr,db_type,db_parms);
    if(err != DB_E_SUCCESS) {
        fprintf(stderr,"Error opening db: %s\n",perr);
        free(perr);
        exit(1);
    }

    printf("Parsing %s\n",argv[optind]);

    pt=sp_init();
    if(!sp_parse(pt,argv[optind],type)) {
        printf("%s\n",sp_get_error(pt));
    } else {
        printf("SQL: %s\n",sp_sql_clause(pt));
    }

    sp_dispose(pt);
    conf_close();

    printf("Done!\n");
    return 0;
}
