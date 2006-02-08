/*
 * $Id$
 */

#include <stdio.h>

#include "conf.h"
#include "err.h"

int main(int argc, char *argv[]) {
    int err;

    err_debuglevel = 9;

    printf("Reading %s\n",argv[1]);

    if((err=config_read(argv[1])) != CONF_E_SUCCESS) {
        printf("Error reading config: %d\n",err);
    } else {
        printf("Read config!\n");
    }
    config_close();
}
