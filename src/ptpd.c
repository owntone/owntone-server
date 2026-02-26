/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "libairptp/airptp.h"
#include "misc.h"
#include "logger.h"

static struct airptp_handle *ptpd_hdl;
static bool airptp_use_shared_daemon = false;

static void
logmsg(const char *fmt, ...)
{
  char fmt_newline[1024];
  va_list ap;

  snprintf(fmt_newline, sizeof(fmt_newline), "%s\n", fmt);

  va_start(ap, fmt);
  DVPRINTF(E_DBG, L_AIRPLAY, fmt_newline, ap);
  va_end(ap);
}

static void
hexdump(const char *msg, uint8_t *mem, size_t len)
{
  DHEXDUMP(E_DBG, L_AIRPLAY, mem, len, msg);
}

uint64_t
ptpd_clock_id_get(void)
{
  uint64_t clock_id;
  int ret;

  ret = airptp_clock_id_get(&clock_id, ptpd_hdl);
  return (ret == 0) ? clock_id : -1;
}

int
ptpd_slave_add(uint32_t *slave_id, const char *addr)
{
  return airptp_peer_add(slave_id, addr, ptpd_hdl);
}

void
ptpd_slave_remove(uint32_t slave_id)
{
  airptp_peer_remove(slave_id, ptpd_hdl);
}

// Thread: main (root priviliges may be required for binding)
int
ptpd_bind(void)
{
  // Check if the host has an instance of airptp running we can use, otherwise
  // try to bind  ourselves
  ptpd_hdl = airptp_daemon_find();
  if (!ptpd_hdl)
    {
      DPRINTF(E_INFO, L_AIRPLAY, "Creating own ptp daemon\n");
      ptpd_hdl = airptp_daemon_bind();
    }
  else
    {
      DPRINTF(E_INFO, L_AIRPLAY, "Using host's ptp daemon\n");
      airptp_use_shared_daemon = true;
    }

  return ptpd_hdl ? 0 : -1;
}

// Thread: main (normal priviliges)
int
ptpd_init(uint64_t clock_id_seed)
{
  struct airptp_callbacks cb = { .logmsg = logmsg, .hexdump = hexdump, .thread_name_set = thread_setname };

  if (!ptpd_hdl)
    return -1;

  airptp_callbacks_register(&cb);
  if (airptp_use_shared_daemon)
    return 0;

  return airptp_daemon_start(ptpd_hdl, clock_id_seed, false);
}

// Thread: main (normal priviliges)
void
ptpd_deinit(void)
{
  airptp_end(ptpd_hdl);
}
