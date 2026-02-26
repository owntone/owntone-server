/*
MIT License

Copyright (c) 2026 OwnTone

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// For shm_open
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>

#include "airptp_internal.h"
#include "ptp_definitions.h"
#include "daemon.h"
#include "msg_handle.h"


/* -------------------------------- Globals --------------------------------- */

unsigned short airptp_event_port = PTP_EVENT_PORT;
unsigned short airptp_general_port = PTP_GENERAL_PORT;

struct airptp_callbacks __thread airptp_cb;
const char __thread *airptp_errmsg;

void
hexdump(const char *msg, void *data, size_t data_len)
{
  if (!airptp_cb.hexdump)
    return;

  airptp_cb.hexdump(msg, data, data_len);
}

void
logmsg(const char *fmt, ...)
{
  va_list ap;
  char content[2048];
  int ret;

  if (!airptp_cb.logmsg)
    return;

  va_start(ap, fmt);
  ret = vsnprintf(content, sizeof(content), fmt, ap);
  va_end(ap);

  if (ret < 0)
    airptp_cb.logmsg("[error printing log message]");
  else
    airptp_cb.logmsg("%s", content);
}

void
thread_name_set(const char *name)
{
  if (!airptp_cb.thread_name_set)
    return;

  airptp_cb.thread_name_set(name);
}

/* ----------------------------------- API ---------------------------------- */

void
airptp_callbacks_register(struct airptp_callbacks *cb)
{
  if (cb->thread_name_set)
    airptp_cb.thread_name_set = cb->thread_name_set;
  if (cb->hexdump)
    airptp_cb.hexdump = cb->hexdump;
  if (cb->logmsg)
    airptp_cb.logmsg = cb->logmsg;
}

struct airptp_handle *
airptp_daemon_bind(void)
{
  struct airptp_handle *hdl = NULL;
  int fd_event = -1;
  int fd_general = -1;
  int ret __attribute__((unused));

  fd_event = net_bind(NULL, airptp_event_port);
  if (fd_event < 0)
    RETURN_ERROR(AIRPTP_ERR_INVALID, "Could not bind to event port (usually 319)");

  fd_general = net_bind(NULL, airptp_general_port);
  if (fd_general < 0)
    RETURN_ERROR(AIRPTP_ERR_INVALID, "Could not bind to general port (usually 320)");

  hdl = calloc(1, sizeof(struct airptp_handle));
  if (!hdl)
    RETURN_ERROR(AIRPTP_ERR_OOM, "Out of memory");

  hdl->daemon.event_svc.port = airptp_event_port;
  hdl->daemon.event_svc.fd = fd_event;

  hdl->daemon.general_svc.port = airptp_general_port;
  hdl->daemon.general_svc.fd = fd_general;

  hdl->state = AIRPTP_STATE_PORTS_BOUND;
  hdl->is_daemon = true;

  return hdl;

 error:
  free(hdl);
  if (fd_event >= 0)
    close(fd_event);
  if (fd_general >= 0)
    close(fd_general);
  return NULL;
}

// Starts a PTP daemon. Ports must have been bound already. Starting the daemon
// does not require privileges.
int
airptp_daemon_start(struct airptp_handle *hdl, uint64_t clock_id_seed, bool is_shared)
{
  int ret;

  if (!hdl->is_daemon || hdl->state != AIRPTP_STATE_PORTS_BOUND)
    RETURN_ERROR(AIRPTP_ERR_INVALID, "Can't start daemon, ports not bound not in daemon mode");

  // From IEEE EUI-64 clockIdentity values: "The most significant 3 octets of
  // the clockIdentity shall be an OUI. The least significant two bits of the
  // most significant octet of the OUI shall both be 0. The least significant
  // bit of the most significant octet of the OUI is used to distinguish
  // clockIdentity values specified by this subclause from those specified in
  // 7.5.2.2.3 [Non-IEEE EUI-64 clockIdentity values]".
  // If we had the MAC address at this point we, could make a valid EUI-48 based
  // clocked from mac[0..2] + 0xFFFE + mac[3..5]. However, since we don't, we
  // create a non-EUI-64 clock ID from 0xFFFF + 6 byte seed, ref 7.5.2.2.3.
  hdl->clock_id = clock_id_seed | 0xFFFF000000000000;

  ret = daemon_start(&hdl->daemon, is_shared, hdl->clock_id, airptp_cb);
  if (ret < 0)
    goto error; // errmsg set by daemon_start

  hdl->state = AIRPTP_STATE_RUNNING;

  return 0;

 error:
  return -1;
}

struct airptp_handle *
airptp_daemon_find(void)
{
  struct airptp_handle *hdl = NULL;
  struct airptp_shm_struct *daemon_info = MAP_FAILED;
  time_t now;
  int fd = -1;
  int ret __attribute__((unused));

  fd = shm_open(AIRPTP_SHM_NAME, O_RDONLY, 0);
  if (fd < 0)
    RETURN_ERROR(AIRPTP_ERR_NOTFOUND, "No airptp daemon found");

  daemon_info = mmap(NULL, sizeof(struct airptp_shm_struct), PROT_READ, MAP_SHARED, fd, 0);
  if (daemon_info == MAP_FAILED)
    RETURN_ERROR(AIRPTP_ERR_INTERNAL, "mmap() of shared memory returned an error");

  if (daemon_info->version_major != AIRPTP_SHM_STRUCTS_VERSION_MAJOR)
    RETURN_ERROR(AIRPTP_ERR_NOTFOUND, "The host is running an incompatible airptp daemon");

  now = time(NULL);
  if (daemon_info->ts + AIRPTP_STALE_SECS < now)
    RETURN_ERROR(AIRPTP_ERR_NOTFOUND, "No airptp daemon found (share mem is stale)");

  hdl = calloc(1, sizeof(struct airptp_handle));
  if (!hdl)
    RETURN_ERROR(AIRPTP_ERR_OOM, "Out of memory");

  hdl->clock_id = daemon_info->clock_id;
  hdl->state = AIRPTP_STATE_RUNNING;
  hdl->is_daemon = false;

  munmap(daemon_info, sizeof(struct airptp_shm_struct));
  close(fd);

  return hdl;

 error:
  free(hdl);
  if (daemon_info != MAP_FAILED)
    munmap(daemon_info, sizeof(struct airptp_shm_struct));
  if (fd >= 0)
    close(fd);
  return NULL;
}

int
airptp_peer_add(uint32_t *peer_id, const char *addr, struct airptp_handle *hdl)
{
  struct airptp_peer peer = { 0 };
  int ret;

  if (hdl->state != AIRPTP_STATE_RUNNING)
    RETURN_ERROR(AIRPTP_ERR_INVALID, "Can't add peer, no airptp daemon");

  if (net_sockaddr_get(&peer.naddr, addr, 0) < 0)
    RETURN_ERROR(AIRPTP_ERR_INVALID, "Can't add peer, address is invalid");

  peer.id = djb_hash(addr, strlen(addr));
  peer.naddr_len = (peer.naddr.sa.sa_family == AF_INET6) ? sizeof(peer.naddr.sin6) : sizeof(peer.naddr.sin);

  ret = msg_peer_add_send(&peer, hdl, airptp_general_port);
  if (ret < 0)
    RETURN_ERROR(AIRPTP_ERR_NOCONNECTION, "Can't add peer, connection to airptp daemon broken");

  *peer_id = peer.id;

  return 0;

 error:
  return -1;
}

void
airptp_peer_remove(uint32_t peer_id, struct airptp_handle *hdl)
{
  struct airptp_peer peer = { 0 };

  peer.id = peer_id;

  msg_peer_del_send(&peer, hdl, airptp_general_port);
}

int
airptp_end(struct airptp_handle *hdl)
{
  int ret = 0;

  if (!hdl)
    return 0;

  if (hdl->is_daemon) {
    ret = daemon_stop(&hdl->daemon);
    if (ret < 0)
      goto error; // errmsg will be set by daemon_stop
  }

 error:
  free(hdl);
  return (ret == 0) ? 0 : -1;
}

int
airptp_clock_id_get(uint64_t *clock_id, struct airptp_handle *hdl)
{
  if (hdl->state != AIRPTP_STATE_RUNNING)
    return -1;

  *clock_id = hdl->clock_id;
  return 0;
}

const char *
airptp_errmsg_get(void)
{
  return airptp_errmsg;
}

void
airptp_ports_override(unsigned short event_port, unsigned short general_port)
{
  airptp_event_port = event_port;
  airptp_general_port = general_port;
}
