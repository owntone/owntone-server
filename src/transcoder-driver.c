#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daapd.h"
#include "conf.h"
#include "err.h"
#include "os.h"
#include "plugin.h"
#include "webserver.h"

char *av0;
CONFIG config;
char *scan_winamp_genre[] = { NULL };

void usage(void) {
    fprintf(stderr,"Usage: %s [options]\n\n",av0);
    fprintf(stderr,"options:\n\n");
    fprintf(stderr,"  -d level    set debuglevel (9 is highest)\n");
    fprintf(stderr,"  -c config   read config file\n");
    fprintf(stderr,"  -f file     file to transcode\n");
    fprintf(stderr,"  -p plugin   plugin to use\n");
    fprintf(stderr,"  -t codectype\n");

    fprintf(stderr,"\n\n");
    exit(-1);
}

int main(int argc, char *argv[]) {
    int option;
    int debuglevel;
    char *plugin=NULL;
    char *file=NULL;
    char *codectype=NULL;
    char *configfile="mt-daapd.conf";
    int bytes_read;
    char *pe;
    WS_CONNINFO wsc;

    if(strchr(argv[0],'/')) {
        av0 = strrchr(argv[0],'/')+1;
    } else {
        av0 = argv[0];
    }

    while((option = getopt(argc, argv, "d:c:f:p:t:")) != -1) {
        switch(option) {
        case 'd':
            debuglevel = atoi(optarg);
            break;
        case 'c':
            configfile=optarg;
            break;
        case 'f':
            file = optarg;
            break;
        case 'p':
            plugin = optarg;
            break;
        case 't':
            codectype = optarg;
            break;
        default:
            fprintf(stderr,"Error: unknown option (%c)\n\n",option);
            usage();
        }
    }

    if((!plugin) || (!file) || (!codectype)) {
        usage();
    }

    printf("Reading config file %s\n",configfile);
    if(conf_read(configfile) != CONF_E_SUCCESS) {
        fprintf(stderr,"Bummer.\n");
        exit(-1);
    }

    err_setdest(LOGDEST_STDERR);
    err_setlevel(debuglevel);

    plugin_init();
    if(plugin_load(&pe, plugin) != PLUGIN_E_SUCCESS) {
        fprintf(stderr,"Could not load %s: %s\n",plugin, pe);
        exit(-1);
    }

    /* fake up a wsconninfo */
    memset(&wsc,0,sizeof(wsc));
    wsc.fd = open("out.wav",O_WRONLY | O_CREAT, 0666);
    if(wsc.fd == -1) {
        fprintf(stderr,"Error opening output file\n");
        exit(-1);
    }

    bytes_read=plugin_ssc_transcode(&wsc,file,codectype,60*1000*3, 0,0);
    close(wsc.fd);

    if(bytes_read < 1) {
        fprintf(stderr,"Could not transcode\n");
        exit(-1);
    }

    fprintf(stderr,"Transcoded %d bytes\n",bytes_read);

    plugin_deinit();
}

