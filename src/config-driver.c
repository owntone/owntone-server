/*
 * $Id$
 */

#include <stdio.h>

#include "config.h"

int main(int argc, char *argv[]) {
    if(config_read(argv[1]) != CONFIG_E_SUCCESS) {
        printf("Read config!\n");
    } else {
        printf("Error reading config\n");
    }
    config_close();
}
