#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>

#include "airptp.h"

static void
hexdump(const char *msg, uint8_t *mem, size_t len) {
  int i, j;
  int hexdump_cols = 16;

  if (msg)
    printf("%s\n", msg);

  for (i = 0; i < len + ((len % hexdump_cols) ? (hexdump_cols - len % hexdump_cols) : 0); i++)
    {
      if(i % hexdump_cols == 0)
	printf("0x%06x: ", i);

      if (i < len)
	printf("%02x ", 0xFF & ((char*)mem)[i]);
      else
	printf("   ");

      if (i % hexdump_cols == (hexdump_cols - 1))
	{
	  for (j = i - (hexdump_cols - 1); j <= i; j++)
	    {
	      if (j >= len)
		putchar(' ');
	      else if (isprint(((char*)mem)[j]))
		putchar(0xFF & ((char*)mem)[j]);
	      else
		putchar('.');
	    }

	  putchar('\n');
	}
    }
}

static void
logmsg(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);

  printf("\n");
}


// TODO catch signals and end
int
main(int argc, char * argv[])
{
  struct airptp_handle *hdl;
  struct airptp_callbacks cb = { .hexdump = hexdump, .logmsg = logmsg, };
  int ret;

  airptp_callbacks_register(&cb);
  airptp_ports_override(30319, 30320);

  hdl = airptp_daemon_find();
  if (!hdl)
    goto error;

  uint64_t clock_id;
  ret = airptp_clock_id_get(&clock_id, hdl);
  if (ret < 0)
    goto error;

  printf("client.c found clock_id=%" PRIx64 "\n", clock_id);

  uint32_t peer_id;
  ret = airptp_peer_add(&peer_id, "192.168.1.10", hdl);
  if (ret < 0)
    goto error;

  printf("client.c added peer_id=%" PRIu32 "\n", peer_id);

  airptp_end(hdl);

  return 0;

 error:
  printf("client.c error: %s\n", airptp_errmsg_get());
  return -1;
}
