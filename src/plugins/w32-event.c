/*
 * $Id: $
 */

#include "compat.h"
#include "ff-plugins.h"

/* Forwards */
void plugin_handler(int, int, void *, int);

#define PIPE_BUFFER_SIZE 4096

/* Globals */
PLUGIN_EVENT_FN _pefn = { plugin_handler };

PLUGIN_INFO _pi = { 
    PLUGIN_VERSION,        /* version */
    PLUGIN_EVENT,          /* type */
    "w32-event/" VERSION,  /* server */
    NULL,                  /* output fns */
    &_pefn,                /* event fns */
    NULL,                  /* transocde fns */
    NULL,                  /* rend info */
    NULL                   /* codec list */
};

typedef struct tag_plugin_msg {
    int size;
    int event_id;
    int intval;
    char vp[1];
} PLUGIN_MSG;


PLUGIN_INFO *plugin_info(void) {
    return &_pi;
}

#define MAILSLOT_NAME "\\\\.\\mailslot\\FireflyMediaServer--67A72768-4154-417e-BFA0-FA9B50C342DE"
/** NO LOG IN HERE!  We'll go into an endless loop.  :) */
void plugin_handler(int event_id, int intval, void *vp, int len) {
    HANDLE h = CreateFile(MAILSLOT_NAME, GENERIC_WRITE,
                          FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD bytes_written;
        const int packet_size = 12 + len;
        unsigned char *buffer = (unsigned char *)malloc(packet_size);
        if(!buffer)
            return;
        memset(buffer, 0, packet_size);

        buffer[0] = packet_size & 0xff;
        buffer[1] = (packet_size >> 8) & 0xff;
        buffer[2] = (packet_size >> 16) & 0xff;
        buffer[3] = (packet_size >> 24) & 0xff;

        buffer[4] = event_id & 0xff;
        buffer[5] = (event_id >> 8) & 0xff;
        buffer[6] = (event_id >> 16) & 0xff;
        buffer[7] = (event_id >> 24) & 0xff;

        buffer[8] = intval & 0xff;
        buffer[9] = (intval >> 8) & 0xff;
        buffer[10] = (intval >> 16) & 0xff;
        buffer[11] = (intval >> 24) & 0xff;

        memcpy(buffer + 12, vp, len);

        /* If this fails then there's nothing we can do about it anyway. */
        WriteFile(h, buffer, packet_size, &bytes_written, NULL);
        CloseHandle(h);
    }
}
