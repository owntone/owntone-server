/*
 * $Id: $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "compat.h"
#include "mtd-plugins.h"

/* Forwards */
PLUGIN_INFO *plugin_info(void);
void plugin_handler(int, int, void *, int);

#define PIPE_BUFFER_SIZE 4096

/* Globals */
PLUGIN_EVENT_FN _pefn = { plugin_handler };

PLUGIN_INFO _pi = { 
    PLUGIN_VERSION, 
    PLUGIN_EVENT, 
    "w32-event/1.0",
    NULL,
    NULL,
    &_pefn,
    NULL,
    NULL
};

typedef struct tag_plugin_msg {
    int size;
    int event_id;
    int intval;
    char vp[1];
} PLUGIN_MSG;

#define infn ((PLUGIN_INPUT_FN *)(_pi.pi))

PLUGIN_INFO *plugin_info(void) {
    return &_pi;
}

void plugin_handler(int event_id, int intval, void *vp, int len) {
    int total_len = 3 * sizeof(int) + len + 1;
    PLUGIN_MSG *pmsg;

    pmsg = (PLUGIN_MSG*)malloc(total_len);
    if(!pmsg) {
//        infn->log(E_LOG,"Malloc error in w32-event.c/plugin_handler\n");
        return;
    }

    memset(pmsg,0,total_len);
    pmsg->size = total_len;
    pmsg->event_id = event_id;
    pmsg->intval = intval;
    memcpy(&pmsg->vp,vp,len);

    CallNamedPipe("\\\\.\\pipe\\firefly",NULL,0,pmsg,total_len,NULL,NMPWAIT_NOWAIT);

    free(pmsg);
    return;
}
