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
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include <event2/event.h>

#include "misc.h"
#include "logger.h"


#define PTPD_EVENT_PORT 319
#define PTPD_GENERAL_PORT 320
#define PTPD_DOMAIN 0

#define PTPD_SYNC_INTERVAL 1 // seconds
#define PTPD_ANNOUNCE_INTERVAL 2 // seconds
#define PTPD_MAX_SLAVES 10

#define PTPD_MSGTYPE_SYNC 0x00
#define PTPD_MSGTYPE_DELAY_REQ 0x01 // Not implemented
#define PTPD_MSGTYPE_PDELAY_REQ 0x02
#define PTPD_MSGTYPE_PDELAY_RESP 0x03
#define PTPD_MSGTYPE_FOLLOW_UP 0x08
#define PTPD_MSGTYPE_DELAY_RESP 0x09 // Not implemented
#define PTPD_MSGTYPE_PDELAY_RESP_FOLLOW_UP 0x0A
#define PTPD_MSGTYPE_ANNOUNCE 0x0B
#define PTPD_MSGTYPE_SIGNALING 0x0C // Not implemented
#define PTPD_MSGTYPE_MANAGEMENT 0x0D // Not implemented

// Just value chosen to not conflict with EV_xxx values
#define PTPD_EVFLAG_NEW_SLAVE 0x99

// PTP Header (34 bytes)
struct ptp_header
{
  uint8_t messageType; // upper 4 bits are transportSpecific
  uint8_t versionPTP; // upper 4 bits are Reserved
  uint16_t messageLength;
  uint8_t domainNumber;
  uint8_t reserved1;
  uint16_t flags;
  int64_t correctionField; // int64 or uint64?
  uint32_t reserved2;
  uint8_t sourcePortIdentity[10];
  uint16_t sequenceId;
  uint8_t controlField;
  uint8_t logMessageInterval;
} __attribute__((packed));

// Timestamp structure (10 bytes)
struct ptp_timestamp
{
  uint16_t seconds_hi;
  uint32_t seconds_low;
  uint32_t nanoseconds;
} __attribute__((packed));

// Announce Message
struct ptp_announce_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
  int16_t currentUtcOffset;
  uint8_t reserved;
  uint8_t grandmasterPriority1;
  uint32_t grandmasterClockQuality;
  uint8_t grandmasterPriority2;
  uint8_t grandmasterIdentity[8];
  uint16_t stepsRemoved;
  uint8_t timeSource;
} __attribute__((packed));

// Sync Message
struct ptp_sync_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
} __attribute__((packed));

// Follow Up Message
struct ptp_follow_up_message
{
  struct ptp_header header;
  struct ptp_timestamp preciseOriginTimestamp;
} __attribute__((packed));

// Pdelay Response Message
struct ptp_pdelay_resp_message
{
  struct ptp_header header;
  struct ptp_timestamp requestReceiptTimestamp;
  uint8_t requestingPortIdentity[10];
} __attribute__((packed));

// Pdelay Response Follow Up Message
struct ptp_pdelay_resp_follow_up_message
{
  struct ptp_header header;
  struct ptp_timestamp responseOriginTimestamp;
  uint8_t requestingPortIdentity[10];
} __attribute__((packed));


struct ptpd_service
{
  int fd;
  unsigned short port;
  struct event *ev;
};

struct ptpd_slave
{
  uint16_t id;
  union net_sockaddr naddr;
  socklen_t naddr_len;
  char *str_addr; // Human readable for logging/debugging
  bool is_active;
  time_t last_seen;
};

struct ptpd_state
{
  bool is_running;
  pthread_t tid;
  struct event_base *evbase;

  struct ptpd_service event_svc;
  struct ptpd_service general_svc;

  struct event *send_timer;
  uint32_t send_timer_count;

  uint16_t sync_seq;
  uint16_t announce_seq;

  uint64_t clock_id;
  
  pthread_mutex_t mutex;
  struct ptpd_slave slaves[PTPD_MAX_SLAVES];
  int num_slaves;
  uint16_t last_slave_id;
};

static struct ptpd_state ptpd;
static struct timeval ptpd_send_timer_tv = { .tv_sec = 1, .tv_usec = 0 };

/* ================================= Helpers  =============================== */

static void
current_time_get(struct ptp_timestamp *ts)
{
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);

  ts->seconds_hi = ((uint64_t)now.tv_sec) >> 32;
  ts->seconds_low = (uint32_t)now.tv_sec;
  ts->nanoseconds = (uint32_t)now.tv_nsec;

  ts->seconds_hi = htons(ts->seconds_hi);
  ts->seconds_low = htonl(ts->seconds_low);
  ts->nanoseconds = htonl(ts->nanoseconds);
}

static void
port_set(union net_sockaddr *naddr, unsigned short port)
{
  if (naddr->sa.sa_family == AF_INET6)
    naddr->sin6.sin6_port = htons(port);
  else if (naddr->sa.sa_family == AF_INET)
    naddr->sin.sin_port = htons(port);
}


/* =========================== Message construction ========================= */

static void
header_init(struct ptp_header *hdr, uint8_t type, uint16_t msg_len, uint64_t clock_id, uint16_t sequence_id, int8_t log_interval)
{
  uint64_t be64;

  memset(hdr, 0, sizeof(struct ptp_header));
  hdr->messageType = type | 0x10; // 0x10 -> TranSpec = 1 which is expected by nqptp
  hdr->versionPTP = 0x02; // PTPv2
  hdr->messageLength = htons(msg_len);
  hdr->domainNumber = PTPD_DOMAIN;
  hdr->flags = htons(0x0200); // Two-step flag for Sync
  hdr->correctionField = 0;

  // Source port identity: 8 bytes clock ID + 2 bytes port number
  be64 = htobe64(clock_id);
  memcpy(hdr->sourcePortIdentity, &be64, sizeof(be64));
  hdr->sourcePortIdentity[8] = 0x00;
  hdr->sourcePortIdentity[9] = 0x01; // Port 1

  hdr->sequenceId = htons(sequence_id);
  hdr->controlField = 0x00;
  hdr->logMessageInterval = log_interval;
}

static void
msg_announce_make(struct ptp_announce_message *msg, uint64_t clock_id, uint16_t sequence_id, struct ptp_timestamp ts)
{
  uint64_t be64;

  header_init(&msg->header, PTPD_MSGTYPE_ANNOUNCE, sizeof(struct ptp_announce_message), clock_id, sequence_id, 1);

  msg->originTimestamp = ts;

  msg->currentUtcOffset = htons(37); // Current UTC offset TODO check if this correct?
  msg->reserved = 0;
  msg->grandmasterPriority1 = 128;

  // Clock quality: class=6 (GPS), accuracy=0x21 (100ns), variance=0xFFFF
  msg->grandmasterClockQuality = htonl(0x06210000) | htons(0xFFFF);
  msg->grandmasterPriority2 = 128;

  be64 = htobe64(clock_id);
  memcpy(msg->grandmasterIdentity, &be64, sizeof(be64));

  msg->stepsRemoved = 0;
  msg->timeSource = 0x20; // GPS
}

static void
msg_sync_make(struct ptp_sync_message *msg, uint64_t clock_id, uint16_t sequence_id, struct ptp_timestamp ts)
{
  header_init(&msg->header, PTPD_MSGTYPE_SYNC, sizeof(struct ptp_sync_message), clock_id, sequence_id, 0);

  msg->originTimestamp = ts;
}

static void
msg_sync_followup_make(struct ptp_follow_up_message *msg, uint64_t clock_id, uint16_t sequence_id, struct ptp_timestamp ts)
{
  header_init(&msg->header, PTPD_MSGTYPE_FOLLOW_UP, sizeof(struct ptp_follow_up_message), clock_id, sequence_id, 0);

  msg->header.flags = 0; // Clear two-step flag
  msg->preciseOriginTimestamp = ts;
}

static void
msg_pdelay_response_make(struct ptp_pdelay_resp_message *res,
  uint64_t clock_id, uint16_t sequence_id, uint8_t *source_port_id, size_t source_port_id_len, struct ptp_timestamp ts)
{
  header_init(&res->header, PTPD_MSGTYPE_PDELAY_RESP, sizeof(struct ptp_pdelay_resp_message), clock_id, ntohs(sequence_id), 0x7F);

  res->requestReceiptTimestamp = ts;

  assert(source_port_id_len == sizeof(res->requestingPortIdentity));
  memcpy(res->requestingPortIdentity, source_port_id, source_port_id_len);
}

static void
msg_pdelay_followup_response_make(struct ptp_pdelay_resp_follow_up_message *res,
  uint64_t clock_id, uint16_t sequence_id, uint8_t *source_port_id, size_t source_port_id_len, struct ptp_timestamp ts)
{
  header_init(&res->header, PTPD_MSGTYPE_PDELAY_RESP_FOLLOW_UP, sizeof(struct ptp_pdelay_resp_follow_up_message), clock_id, ntohs(sequence_id), 0x7F);

  res->responseOriginTimestamp = ts;

  assert(source_port_id_len == sizeof(res->requestingPortIdentity));
  memcpy(res->requestingPortIdentity, source_port_id, source_port_id_len);
}


/* ============================= Slave handling ============================= */

static void
slave_clear(struct ptpd_slave *slave)
{
  free(slave->str_addr);
  memset(slave, 0, sizeof(struct ptpd_slave));
}

// Called by e.g. player thread
static int
slave_add(struct ptpd_state *state, union net_sockaddr *naddr)
{
  struct ptpd_slave *slave;
  char str_addr[64];
  uint16_t slave_id;
  int ret;

  pthread_mutex_lock(&state->mutex);

  if (state->num_slaves >= PTPD_MAX_SLAVES)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Max number of PTP slaves reached\n");
      goto error;
    }

  // After UINT16_MAX slaves we will start reusing slave id's. We never use
  // slave_id = 0.
  slave_id = state->last_slave_id + 1;
  if (slave_id == 0)
    slave_id = 1;

  slave = &state->slaves[state->num_slaves];
  memset(slave, 0, sizeof(struct ptpd_slave));

  slave->naddr_len = (naddr->sa.sa_family == AF_INET6) ? sizeof(naddr->sin6) : sizeof(naddr->sin);
  memcpy(&slave->naddr, naddr, slave->naddr_len);
  slave->is_active = true;
  slave->last_seen = time(NULL);
  slave->id = slave_id;

  ret = net_address_get(str_addr, sizeof(str_addr), naddr);
  if (ret == 0)
    slave->str_addr = strdup(str_addr);

  state->num_slaves++;
  state->last_slave_id = slave_id;

  DPRINTF(E_DBG, L_AIRPLAY, "Added new slave with address %s\n", slave->str_addr);

  pthread_mutex_unlock(&state->mutex);
  return slave_id;

 error:
  pthread_mutex_unlock(&state->mutex);
  return -1;
}

// Called by e.g. player thread
static void
slave_remove(struct ptpd_state *state, int slave_id)
{
  struct ptpd_slave *slave;
  bool found;
  int i;

  pthread_mutex_lock(&state->mutex);

  for (i = 0, found = false; i < state->num_slaves; i++)
    {
      slave = &state->slaves[i];
      if (!found && (slave_id == slave->id))
	{
	  slave_clear(slave);
	  found = true;
	  continue;
	}

      // Reorder the list
      if (found)
	state->slaves[i - 1] = *slave;
    }

  if (!found)
    {
      DPRINTF(E_DBG, L_AIRPLAY, "Can't remove PTP slave, not in our list\n");
      goto error;
    }

  state->num_slaves--;

 error:
  pthread_mutex_unlock(&state->mutex);
  return;
}

static void
slaves_prune(struct ptpd_state *state)
{
  struct ptpd_slave *slave;
  int i;
  int n_pruned;

  pthread_mutex_lock(&state->mutex);

  for (i = 0, n_pruned = 0; i < state->num_slaves; i++)
    {
      slave = &state->slaves[i];
      if (!slave->is_active)
	{
	  slave_clear(slave);
	  n_pruned++;
	  continue;
	}

      if (n_pruned > 0)
	state->slaves[i - n_pruned] = *slave;
    }

  state->num_slaves -= n_pruned;

  pthread_mutex_unlock(&state->mutex);
}

static void
slaves_msg_send(struct ptpd_state *state, void *msg, size_t msg_len, struct ptpd_service *svc)
{
  struct ptpd_slave *slave;
  ssize_t len;
  union net_sockaddr naddr;
  uint8_t *msg_bin = msg;

  pthread_mutex_lock(&state->mutex);

  for (int i = 0; i < state->num_slaves; i++)
    {
      slave = &state->slaves[i];
      if (!slave->is_active)
	continue;

      // Copy because we don't want to modify list elements, need them to be
      // intact so we can recongnize them when asked to remove a slave
      memcpy(&naddr, &slave->naddr, slave->naddr_len);

      port_set(&naddr, svc->port);
      len = sendto(svc->fd, msg, msg_len, 0, &naddr.sa, slave->naddr_len);
      if (len < 0)
	{
	  DPRINTF(E_LOG, L_AIRPLAY, "Error sending PTP msg %02x to %s port %d\n", msg_bin[0], slave->str_addr, svc->port);
	  slave->is_active = false; // Will be removed deferred by slaves_prune()
	}
      else if (len != msg_len)
	DPRINTF(E_LOG, L_AIRPLAY, "Incomplete send of msg %02x to %s port %d\n", msg_bin[0], slave->str_addr, svc->port);
      else
	DPRINTF(E_DBG, L_AIRPLAY, "Sent PTP msg %02x to %s port %d\n", msg_bin[0], slave->str_addr, svc->port); // TOOD remove or SPAM level
    }

  pthread_mutex_unlock(&state->mutex);
}


/* =========================== Sending and dispatch ========================= */

static void
announce_send(struct ptpd_state *state)
{
  struct ptp_announce_message annnounce;
  struct ptp_timestamp ts;

  current_time_get(&ts);

  msg_announce_make(&annnounce, state->clock_id, state->announce_seq, ts);
  slaves_msg_send(state, &annnounce, sizeof(annnounce), &state->general_svc);

  state->announce_seq++;
}

static void
sync_send(struct ptpd_state *state)
{
  struct ptp_sync_message sync;
  struct ptp_follow_up_message followup;
  struct ptp_timestamp ts;

  current_time_get(&ts);

  msg_sync_make(&sync, state->clock_id, state->sync_seq, ts);
  slaves_msg_send(state, &sync, sizeof(sync), &state->event_svc);

  // Send Follow Up with precise timestamp after a small delay
  usleep(100);
  msg_sync_followup_make(&followup, state->clock_id, state->sync_seq, ts);
  slaves_msg_send(state, &followup, sizeof(followup), &state->general_svc);

  state->sync_seq++;
}

static void
pdelay_req_respond(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  struct ptp_header *hdr = (struct ptp_header *)req;
  struct ptp_pdelay_resp_message res;
  struct ptp_pdelay_resp_follow_up_message followup;
  struct ptp_timestamp ts;
  ssize_t len;

  if (req_len < sizeof(struct ptp_header))
    return;

  current_time_get(&ts);
  msg_pdelay_response_make(&res, state->clock_id, hdr->sequenceId, hdr->sourcePortIdentity, sizeof(hdr->sourcePortIdentity), ts);

  port_set(peer_addr, PTPD_EVENT_PORT);
  len = sendto(state->event_svc.fd, &res, sizeof(res), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(res))
    DPRINTF(E_LOG, L_AIRPLAY, "Incomplete send of struct ptp_pdelay_resp_message\n");

  current_time_get(&ts);
  msg_pdelay_followup_response_make(&followup, state->clock_id, hdr->sequenceId, hdr->sourcePortIdentity, sizeof(hdr->sourcePortIdentity), ts);
    
  port_set(peer_addr, PTPD_GENERAL_PORT);
  len = sendto(state->general_svc.fd, &followup, sizeof(followup), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(followup))
    DPRINTF(E_LOG, L_AIRPLAY, "Incomplete send of struct ptp_pdelay_resp_follow_up_message\n");

  // TODO remove or set to SPAM
  DPRINTF(E_DBG, L_AIRPLAY, "Responded to pdelay_req message\n");
}

// Scheduled for callback each second
static void
ptpd_send_cb(int fd, short what, void *arg)
{
  struct ptpd_state *state = arg;

  if (state->num_slaves == 0)
    return; // Don't reschedule

  if (what == PTPD_EVFLAG_NEW_SLAVE || state->send_timer_count % PTPD_ANNOUNCE_INTERVAL == 0)
    announce_send(state);

  if (what == PTPD_EVFLAG_NEW_SLAVE || state->send_timer_count % PTPD_SYNC_INTERVAL == 0)
    sync_send(state);

  state->send_timer_count++;

  event_add(state->send_timer, &ptpd_send_timer_tv);
}

static void
ptpd_respond_cb(int fd, short what, void *arg)
{
  struct ptpd_state *state = arg;
  const char *svc_name = (fd == state->event_svc.fd) ? "PTP EVENT" : "PTP GENERAL";
  union net_sockaddr peer_addr;
  socklen_t peer_addrlen = sizeof(peer_addr);
  uint8_t req[1024];
  uint8_t msg_type;
  ssize_t len;

  len = recvfrom(fd, req, sizeof(req), 0, &peer_addr.sa, &peer_addrlen);
  if (len <= 0)
    {
      if (len < 0)
	DPRINTF(E_LOG, L_AIRPLAY, "Service %s read error: %s\n", svc_name, strerror(errno));
      return;
    }

  msg_type = req[0] & 0x0F; // Only lower bits are message type

  switch (msg_type)
    {
      case PTPD_MSGTYPE_PDELAY_REQ:
	pdelay_req_respond(state, req, len, &peer_addr, peer_addrlen);
	break;
      default:
	DPRINTF(E_DBG, L_AIRPLAY, "Service %s received unhandled message type: %02x\n", svc_name, msg_type);
	return;
    }
}

static int
service_bind(struct ptpd_service *svc, unsigned short port, const char *logname)
{
  svc->port = port;
  svc->fd = net_bind(&svc->port, SOCK_DGRAM, logname);
  if (svc->fd < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Error binding PTP daemon to port %hu (not root? other PTP daemon?) \n", svc->port);
      return -1;
    }

  return 0;
}

static void
service_stop(struct ptpd_service *svc)
{
  if (svc->ev)
    event_free(svc->ev);

  svc->ev = NULL;
}

static int
service_start(struct ptpd_service *svc, event_callback_fn cb, struct ptpd_state *state, const char *log_service_name)
{
  svc->ev = event_new(state->evbase, svc->fd, EV_READ | EV_PERSIST, cb, state);
  if (!svc->ev)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not create event for '%s' service\n", log_service_name);
      goto error;
    }

  event_add(svc->ev, NULL);
  return 0;

 error:
  service_stop(svc);
  return -1;
}

static void *
run(void *arg)
{
  struct ptpd_state *state = arg;
  int ret;

  thread_setname("ptpd");

  ret = service_start(&state->event_svc, ptpd_respond_cb, state, "ptp events");
  if (ret < 0)
    goto stop;

  ret = service_start(&state->general_svc, ptpd_respond_cb, state, "ptp general");
  if (ret < 0)
    goto stop;

  state->send_timer = evtimer_new(state->evbase, ptpd_send_cb, state);

  state->is_running = true;

  event_base_dispatch(state->evbase);

  if (state->is_running)
    DPRINTF(E_LOG, L_AIRPLAY, "ptpd event loop terminated ahead of time!\n");

 stop:
  if (state->send_timer)
    event_free(state->send_timer);
  service_stop(&state->general_svc);
  service_stop(&state->event_svc);
  pthread_exit(NULL);
}


/* =================================== API ================================== */

uint64_t
ptpd_clock_id_get(void)
{
  return ptpd.clock_id;
}

// TODO uses mutex where the other thread does send i/o, so risk of blocking player thread!
int
ptpd_slave_add(union net_sockaddr *naddr)
{
  int slave_id;

  if (!ptpd.is_running)
    return -1;

  // Now is a good time to kill non-working slaves
  slaves_prune(&ptpd);

  slave_id = slave_add(&ptpd, naddr);
  if (slave_id < 0)
    return -1;

  // Send announce and sync immediately to the new slave
  event_active(ptpd.send_timer, PTPD_EVFLAG_NEW_SLAVE, 0);

  return slave_id;
}

void
ptpd_slave_remove(int slave_id)
{
  if (!ptpd.is_running || slave_id == 0)
    return;

  slave_remove(&ptpd, slave_id);
}

int
ptpd_bind(void)
{
  int ret;

  ret = service_bind(&ptpd.event_svc, PTPD_EVENT_PORT, "PTP events");
  if (ret < 0)
    goto error;

  ret = service_bind(&ptpd.general_svc, PTPD_GENERAL_PORT, "PTP general");
  if (ret < 0)
    goto error;

  return 0;

 error:
  if (ptpd.event_svc.fd >= 0)
    close(ptpd.event_svc.fd);
  if (ptpd.general_svc.fd >= 0)
    close(ptpd.general_svc.fd);
  return -1;
}

int
ptpd_init(uint64_t clock_id_seed)
{
  int ret;

  if (ptpd.event_svc.fd < 0 || ptpd.general_svc.fd < 0)
    return -1;

  CHECK_NULL(L_AIRPLAY, ptpd.evbase = event_base_new());

  // The clock id should be created from the mac address, but since we don't yet
  // know which interface we will be using, that isn't possible. nqptp says to
  // read section 7.5.2.2.2 IEEE EUI-64 clockIdentity values, NOTE 2, I should
  // do that one day.
  ptpd.clock_id = clock_id_seed | 0xFFFE000000;

  ret = pthread_create(&ptpd.tid, NULL, run, &ptpd);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not spawn ptpd thread: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

void
ptpd_deinit(void)
{
  int ret;

  ptpd.is_running = false;

  event_base_loopbreak(ptpd.evbase);

  ret = pthread_join(ptpd.tid, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not join ptpd thread: %s\n", strerror(errno));
      return;
    }

  event_base_free(ptpd.evbase);
}
