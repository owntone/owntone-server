/*
 * $Id$
 */

#include <stdio.h>

#define os_opensyslog()
#define os_syslog(a,b)
#define os_closesyslog()

#include "daapd.h"
#include "conf.h"
#include "err.h"


int main(int argc, char *argv[]) {
    int err;

    printf("Reading %s\n",argv[1]);

    if((err=conf_read(argv[1])) != CONF_E_SUCCESS) {
        printf("Error reading config: %d\n",err);
        conf_close();
    } else {
        printf("Read config!\n");
        conf_set_string("general","stupid","lalala");
        conf_set_int("potato","yummy",0);
        if(conf_iswritable()) {
            printf("writing config\n");
            conf_write();
        }
    }
    conf_close();
}
