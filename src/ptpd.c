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
#include <inttypes.h>
#include <assert.h>

#include <event2/event.h>

#include "misc.h"
#include "logger.h"
#include "commands.h"

#define PTPD_EVENT_PORT 319
#define PTPD_GENERAL_PORT 320
#define PTPD_DOMAIN 0

// The log2 of the announce message interval in seconds. The ATV uses -2, which
// would be 0.25 sec, my amp uses 0, so 1 sec, as does nqptp.
// See nqptp-ptp-definitions.h.
#define PTPD_LOGMESSAGEINT_ANNOUNCE 0
#define PTPD_INTERVAL_MS_ANNOUNCE 1000
// Both iOS, ATV, amp and nqptp use -3, so 0.125 sec.
#define PTPD_LOGMESSAGEINT_SYNC -3
#define PTPD_INTERVAL_MS_SYNC 125
// Used by iOS
#define PTPD_LOGMESSAGEINT_SIGNALING -128
#define PTPD_INTERVAL_MS_SIGNALING 1000
#define PTPD_LOGMESSAGEINT_DELAY_RESP -3

#define PTPD_MAX_SLAVES 32

// Debugging
#define PTPD_LOG_RECEIVED 0
#define PTPD_LOG_SENT 0

/* ============================= PTP definitions ============================ */

#define PTP_PORT_ID_SIZE 10

enum ptp_msgtype
{
  PTP_MSGTYPE_SYNC = 0x00,
  PTP_MSGTYPE_DELAY_REQ = 0x01,
  PTP_MSGTYPE_PDELAY_REQ = 0x02,
  PTP_MSGTYPE_PDELAY_RESP = 0x03,
  PTP_MSGTYPE_FOLLOW_UP = 0x08,
  PTP_MSGTYPE_DELAY_RESP = 0x09,
  PTP_MSGTYPE_PDELAY_RESP_FOLLOW_UP = 0x0A,
  PTP_MSGTYPE_ANNOUNCE = 0x0B,
  PTP_MSGTYPE_SIGNALING = 0x0C,
  PTP_MSGTYPE_MANAGEMENT = 0x0D,
};

// From Wireshark, doesn't seem to be in IEEE1588-2008, maybe its in ptp v1?
enum ptp_flag
{
  PTP_FLAG_LI_61 = (1 << 0),
  PTP_FLAG_LI_59 = (1 << 1),
  PTP_FLAG_UTC_UNREASONABLE = (1 << 2),
  PTP_FLAG_TIMESCALE = (1 << 3),
  PTP_FLAG_TIME_TRACEABLE = (1 << 4),
  PTP_FLAG_FREQUENCY_TRACEABLE = (1 << 5),
  PTP_FLAG_SYNCRONIZATION_UNCERTAIN = (1 << 6),
  PTP_FLAG_ALTERNATE_MASTER = (1 << 8),
  PTP_FLAG_TWO_STEP = (1 << 9),
  PTP_FLAG_UNICAST = (1 << 10),
  PTP_FLAG_PROFILE_SPECIFIC2 = (1 << 13),
  PTP_FLAG_PROFILE_SPECIFIC1 = (1 << 14),
  PTP_FLAG_SECURITY = (1 << 15),
};

// Not used currently
struct ptp_scaled_ns
{
  uint16_t ns_hi;
  uint64_t ns_lo;
  uint16_t ns_frac;
} __attribute__((packed));

// Timestamp structure (10 bytes)
struct ptp_timestamp
{
  uint16_t seconds_hi;
  uint32_t seconds_low;
  uint32_t nanoseconds;
} __attribute__((packed));

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
  uint8_t sourcePortIdentity[PTP_PORT_ID_SIZE];
  uint16_t sequenceId;
  uint8_t controlField;
  int8_t logMessageInterval;
} __attribute__((packed));

// Message 0x00
struct ptp_sync_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
} __attribute__((packed));

// Message 0x01
struct ptp_delay_req_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
} __attribute__((packed));

// Message 0x02
struct ptp_pdelay_req_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
  uint8_t reserved[10];
} __attribute__((packed));

// Message 0x03
struct ptp_pdelay_resp_message
{
  struct ptp_header header;
  struct ptp_timestamp requestReceiptTimestamp;
  uint8_t requestingPortIdentity[PTP_PORT_ID_SIZE];
} __attribute__((packed));

// Message 0x08
struct ptp_follow_up_message
{
  struct ptp_header header;
  struct ptp_timestamp preciseOriginTimestamp;
  uint8_t tlv_apple1[32];
  uint8_t tlv_apple2[20];
} __attribute__((packed));

// Message 0x09
struct ptp_delay_resp_message
{
  struct ptp_header header;
  struct ptp_timestamp receiveTimestamp;
  uint8_t requestingPortIdentity[PTP_PORT_ID_SIZE];
} __attribute__((packed));

// Message 0x0A
struct ptp_pdelay_resp_follow_up_message
{
  struct ptp_header header;
  struct ptp_timestamp responseOriginTimestamp;
  uint8_t requestingPortIdentity[PTP_PORT_ID_SIZE];
} __attribute__((packed));

// Message 0x0B
struct ptp_announce_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
  int16_t currentUtcOffset;
  uint8_t reserved;
  uint8_t grandmasterPriority1;
  uint32_t grandmasterClockQuality;
  uint8_t grandmasterPriority2;
  uint64_t grandmasterIdentity;
  uint16_t stepsRemoved;
  uint8_t timeSource;
  uint8_t tlv_path_trace[12]; // Apple speciality
} __attribute__((packed));

// Message 0x0C
struct ptp_signaling_message
{
  struct ptp_header header;
  uint8_t targetPortIdentity[PTP_PORT_ID_SIZE];
  uint8_t tlv_apple1[26];
  uint8_t tlv_apple2[36];
} __attribute__((packed));


#define PTP_TLV_MIN_SIZE 4 // 2 bytes type + 2 bytes length
#define PTP_TLV_ORG_CODE_SIZE 3
#define PTP_TLV_ORG_EXTENSION 0x0003
#define PTP_TLV_PATH_TRACE 0x0008

enum ptp_tlv_org
{
  PTP_TLV_ORG_IEEE = 0,
  PTP_TLV_ORG_APPLE = 1,
};

enum ptp_tlv_org_ieee_subtype
{
  PTP_TLV_ORG_IEEE_FOLLOW_UP_INFO = 0,
  PTP_TLV_ORG_IEEE_MESSAGE_INTERNAL_REQUEST = 1,
};

enum ptp_tlv_org_apple_subtype
{
  PTP_TLV_ORG_APPLE_UNKNOWN1 = 0,
  PTP_TLV_ORG_APPLE_CLOCK_ID = 1,
  PTP_TLV_ORG_APPLE_UNKNOWN5 = 2,
};

struct ptp_tlv_org_subtype_map
{
  int index;
  uint8_t code[PTP_TLV_ORG_CODE_SIZE];
  char *name;
  int (*handler)(const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);
};

struct ptp_tlv_org_map
{
  enum ptp_tlv_org index;
  uint8_t code[PTP_TLV_ORG_CODE_SIZE];
  char *name;
  struct ptp_tlv_org_subtype_map *subtypes;
  int n_subtypes;
};

// Forward tlv handlers
static int tlv_handle_org_subtype_generic(const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);
static int tlv_handle_org_subtype_message_internal(const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);

static struct ptp_tlv_org_subtype_map ptp_tlv_ieee_subtypes[] =
{
  { PTP_TLV_ORG_IEEE_FOLLOW_UP_INFO, { 0x00, 0x00, 0x01 }, "Follow_Up information TLV", tlv_handle_org_subtype_generic },
  { PTP_TLV_ORG_IEEE_MESSAGE_INTERNAL_REQUEST, { 0x00, 0x00, 0x02 }, "Message internal request TLV", tlv_handle_org_subtype_message_internal },
};

static struct ptp_tlv_org_subtype_map ptp_tlv_apple_subtypes[] =
{
  { PTP_TLV_ORG_APPLE_UNKNOWN1, { 0x00, 0x00, 0x01 }, "Unknown subtype 1", tlv_handle_org_subtype_generic },
  { PTP_TLV_ORG_APPLE_CLOCK_ID, { 0x00, 0x00, 0x04 }, "Clock ID TLV", tlv_handle_org_subtype_generic },
  { PTP_TLV_ORG_APPLE_UNKNOWN5, { 0x00, 0x00, 0x05 }, "Unknown subtype 5", tlv_handle_org_subtype_generic },
};

static struct ptp_tlv_org_map ptp_tlv_orgs[] =
{
  { PTP_TLV_ORG_IEEE, { 0x00, 0x80, 0xc2 }, "IEEE 802.1 Chair", ptp_tlv_ieee_subtypes, ARRAY_SIZE(ptp_tlv_ieee_subtypes) },
  { PTP_TLV_ORG_APPLE, { 0x00, 0x0d, 0x93 }, "Apple, Inc", ptp_tlv_apple_subtypes, ARRAY_SIZE(ptp_tlv_apple_subtypes) },
};


/* ========================= ptpd structs and globals  ====================== */

struct ptpd_service
{
  int fd;
  unsigned short port;
  struct event *ev;
};

struct ptpd_slave
{
  uint32_t id;
  union net_sockaddr naddr;
  socklen_t naddr_len;
  char *str_addr; // Human readable for logging/debugging
  bool is_active;
};

struct ptpd_state
{
  bool is_running;
  pthread_t tid;
  struct event_base *evbase;
  struct commands_base *cmdbase;

  struct ptpd_service event_svc;
  struct ptpd_service general_svc;

  struct event *send_announce_timer;
  struct event *send_signaling_timer;
  struct event *send_sync_timer;

  uint16_t announce_seq;
  uint16_t signaling_seq;
  uint16_t sync_seq;

  uint64_t clock_id;

  struct ptpd_slave slaves[PTPD_MAX_SLAVES];
  int num_slaves;
};

// Daemon global state, incl. list of slaves
static struct ptpd_state ptpd;

static struct timeval ptpd_send_announce_tv =
{
  .tv_sec = PTPD_INTERVAL_MS_ANNOUNCE / 1000,
  .tv_usec = (PTPD_INTERVAL_MS_ANNOUNCE % 1000) * 1000
};
static struct timeval ptpd_send_signaling_tv =
{
  .tv_sec = PTPD_INTERVAL_MS_SIGNALING / 1000,
  .tv_usec = (PTPD_INTERVAL_MS_SIGNALING % 1000) * 1000
};
static struct timeval ptpd_send_sync_tv =
{
  .tv_sec = PTPD_INTERVAL_MS_SYNC / 1000,
  .tv_usec = (PTPD_INTERVAL_MS_SYNC % 1000) * 1000
};


/* ================================= Helpers  =============================== */

static inline struct ptp_timestamp
ptp_timestamp_betoh(struct ptp_timestamp *in)
{
  struct ptp_timestamp out;

  out.seconds_hi = be16toh(in->seconds_hi);
  out.seconds_low = be32toh(in->seconds_low);
  out.nanoseconds = be32toh(in->nanoseconds);
  return out;
}

static inline struct ptp_timestamp
ptp_timestamp_htobe(struct ptp_timestamp *in)
{
  struct ptp_timestamp out;

  out.seconds_hi = htobe16(in->seconds_hi);
  out.seconds_low = htobe32(in->seconds_low);
  out.nanoseconds = htobe32(in->nanoseconds);
  return out;
}

static inline struct ptp_timestamp
current_time_get(void)
{
  struct timespec now;
  struct ptp_timestamp out;

  clock_gettime(CLOCK_MONOTONIC, &now);
  out.seconds_hi = ((uint64_t)now.tv_sec) >> 32;
  out.seconds_low = (uint32_t)now.tv_sec;
  out.nanoseconds = (uint32_t)now.tv_nsec;
  return out;
}

static void
port_id_htobe(uint8_t *out, uint8_t *in)
{
  uint64_t be64;
  uint64_t clock_id;

  memcpy(&clock_id, in, sizeof(clock_id));
  be64 = htobe64(clock_id);
  memcpy(out, &be64, sizeof(be64));
  memcpy(out + sizeof(be64), in + sizeof(be64), PTP_PORT_ID_SIZE - sizeof(be64)); // The last two bytes
}

static void
port_set(union net_sockaddr *naddr, unsigned short port)
{
  if (naddr->sa.sa_family == AF_INET6)
    naddr->sin6.sin6_port = htons(port);
  else if (naddr->sa.sa_family == AF_INET)
    naddr->sin.sin_port = htons(port);
}

#if PTPD_LOG_RECEIVED
static void
log_received(const char *name, struct ptp_header *header, uint64_t clock_id, struct ptp_timestamp *ts)
{
  uint64_t tv_sec;
  uint32_t tv_nsec;

  tv_sec = ts->seconds_hi;
  tv_sec = (tv_sec << 32);
  tv_sec += ts->seconds_low;
  tv_nsec = ts->nanoseconds;

  int8_t logint = header->logMessageInterval;

  DPRINTF(E_DBG, L_AIRPLAY, "Received %s from clock %" PRIx64 ", logint=%" PRIi8 " with timestamp %" PRIu64 ".%" PRIu32 "\n", name, clock_id, logint, tv_sec, tv_nsec);
}
#else
static void
log_received(const char *name, struct ptp_header *header, uint64_t clock_id, struct ptp_timestamp *ts)
{
  return;
}
#endif

#if PTPD_LOG_SENT
static void
log_sent(uint8_t *msg, uint16_t port)
{
  const char *name;
  struct ptp_timestamp bets = { 0 };
  struct ptp_timestamp ts;
  uint64_t tv_sec;
  uint32_t tv_nsec;
  uint64_t clock_id;
  uint64_t be64;

  switch(msg[0] & 0x0F)
    {
      case PTP_MSGTYPE_SYNC:
	name = "PTP_MSGTYPE_SYNC";
	bets = ((struct ptp_sync_message *)msg)->originTimestamp;
	break;
      case PTP_MSGTYPE_DELAY_REQ:
	name = "PTP_MSGTYPE_DELAY_REQ";
	bets = ((struct ptp_delay_req_message *)msg)->originTimestamp;
	break;
      case PTP_MSGTYPE_PDELAY_REQ:
	name = "PTP_MSGTYPE_PDELAY_REQ";
	bets = ((struct ptp_pdelay_req_message *)msg)->originTimestamp;
	break;
      case PTP_MSGTYPE_PDELAY_RESP:
	name = "PTP_MSGTYPE_PDELAY_RESP";
	bets = ((struct ptp_pdelay_resp_message *)msg)->requestReceiptTimestamp;
	break;
      case PTP_MSGTYPE_FOLLOW_UP:
	name = "PTP_MSGTYPE_FOLLOW_UP";
	bets = ((struct ptp_follow_up_message *)msg)->preciseOriginTimestamp;
	break;
      case PTP_MSGTYPE_DELAY_RESP:
	name = "PTP_MSGTYPE_DELAY_RESP";
	bets = ((struct ptp_delay_resp_message *)msg)->receiveTimestamp;
	break;
      case PTP_MSGTYPE_PDELAY_RESP_FOLLOW_UP:
	name = "PTP_MSGTYPE_PDELAY_RESP_FOLLOW_UP";
	bets = ((struct ptp_pdelay_resp_follow_up_message *)msg)->responseOriginTimestamp;
	break;
      case PTP_MSGTYPE_ANNOUNCE:
	name = "PTP_MSGTYPE_ANNOUNCE";
	bets = ((struct ptp_announce_message *)msg)->originTimestamp;
	break;
      default:
	name = "unknown";
    }

  ts = ptp_timestamp_betoh(&bets);
  tv_sec = ts.seconds_hi;
  tv_sec = (tv_sec << 32);
  tv_sec += ts.seconds_low;
  tv_nsec = ts.nanoseconds;

  memcpy(&be64, ((struct ptp_header *)msg)->sourcePortIdentity, sizeof(be64));
  clock_id = be64toh(be64);

  DPRINTF(E_DBG, L_AIRPLAY, "Sent %s to port %hu, clock_id=%" PRIx64 ", ts=%" PRIu64 ".%" PRIu32 "\n", name, port, clock_id, tv_sec, tv_nsec);
}
#else
static void
log_sent(uint8_t *msg, uint16_t port)
{
  return;
}
#endif


/* =========================== Message construction ========================= */

static void
header_init(struct ptp_header *hdr, uint8_t type, uint16_t msg_len, uint64_t clock_id, uint16_t sequence_id, int8_t log_interval, uint16_t flags)
{
  uint64_t be64;

  memset(hdr, 0, sizeof(struct ptp_header));
  hdr->messageType = type | 0x10; // 0x10 -> TranSpec = 1 which is expected by nqptp
  hdr->versionPTP = 0x02; // PTPv2
  hdr->messageLength = htobe16(msg_len);
  hdr->domainNumber = PTPD_DOMAIN;
  hdr->flags = htobe16(flags);
  hdr->correctionField = 0;

  // Source port identity: 8 bytes clock ID + 2 bytes port number
  be64 = htobe64(clock_id);
  memcpy(hdr->sourcePortIdentity, &be64, sizeof(be64));
  // Same as iOS
  hdr->sourcePortIdentity[8] = 0x80;
  hdr->sourcePortIdentity[9] = 0x05;

  hdr->sequenceId = htobe16(sequence_id);
  hdr->controlField = 0x00;
  hdr->logMessageInterval = log_interval;
}

static void
header_read(struct ptp_header *hdr, uint64_t *clock_id_ptr, uint8_t *req)
{
  struct ptp_header *in = (struct ptp_header *)req;
  uint64_t be64;
  uint64_t clock_id;

  memcpy(hdr, in, sizeof(struct ptp_header));

  hdr->messageLength = be16toh(in->messageLength);
  hdr->flags = be16toh(in->flags);
  hdr->correctionField = be64toh(in->correctionField);
  hdr->sequenceId = be16toh(in->sequenceId);

  memcpy(&be64, in->sourcePortIdentity, sizeof(be64));
  clock_id = be64toh(be64);
  memcpy(hdr->sourcePortIdentity, &clock_id, sizeof(clock_id));

  if (clock_id_ptr)
    *clock_id_ptr = clock_id;
}

static void
msg_tlv_write(uint8_t *tlv_dst, size_t tlv_dst_size, uint16_t type, uint16_t length, void *data)
{
  uint16_t be16;

  assert(tlv_dst_size == 2 * sizeof(be16) + length);

  be16 = htobe16(type);
  memcpy(tlv_dst, &be16, sizeof(be16));
  tlv_dst += sizeof(be16);

  be16 = htobe16(length);
  memcpy(tlv_dst, &be16, sizeof(be16));
  tlv_dst += sizeof(be16);

  memcpy(tlv_dst, data, length);
}

static void
msg_announce_make(struct ptp_announce_message *msg, uint64_t clock_id, uint16_t sequence_id, struct ptp_timestamp ts)
{
  uint64_t be64_clock_id = htobe64(clock_id);
  // iOS sets flags to 0x0408 -> UNICAST and TIMESCALE
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE;

  header_init(&msg->header, PTP_MSGTYPE_ANNOUNCE, sizeof(struct ptp_announce_message), clock_id, sequence_id, PTPD_LOGMESSAGEINT_ANNOUNCE, flags);

  msg->originTimestamp = ptp_timestamp_htobe(&ts);

  msg->currentUtcOffset = 0;
  msg->reserved = 0;
  msg->grandmasterPriority1 = 128;

  // Clock quality: class=6 (GPS), accuracy=0x21 (100ns), variance=0x436A (same as used by Apple)
  msg->grandmasterClockQuality = htobe32(0x06210000 | 0x436A);
  msg->grandmasterPriority2 = 128;

  msg->grandmasterIdentity = be64_clock_id;

  msg->stepsRemoved = 0;
  msg->timeSource = 0x20; // GPS

  // iOS adding the clock ID again as TLV, wtf?
  msg_tlv_write(msg->tlv_path_trace, sizeof(msg->tlv_path_trace), PTP_TLV_PATH_TRACE, sizeof(be64_clock_id), &be64_clock_id);
}

static void
msg_signaling_make(struct ptp_signaling_message *msg, uint64_t clock_id, uint16_t sequence_id, uint8_t *target_port_id)
{
  uint8_t apple_val1[sizeof(msg->tlv_apple1) - 4] = { 0 }; // 22 bytes
  uint8_t apple_val2[sizeof(msg->tlv_apple2) - 4] = { 0 }; // 32 bytes
  uint8_t apple_unknown[4] = { 0x00, 0x00, 0x03, 0x01 };
  // iOS sets flags to 0x0408 -> UNICAST and TIMESCALE
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE;
  uint8_t *ptr;

  header_init(&msg->header, PTP_MSGTYPE_SIGNALING, sizeof(struct ptp_signaling_message), clock_id, sequence_id, PTPD_LOGMESSAGEINT_SIGNALING, flags);

  msg->header.controlField = 0x05; // Other Message

  if (target_port_id)
    port_id_htobe(msg->targetPortIdentity, target_port_id);
  else
    memset(msg->targetPortIdentity, 0, sizeof(msg->targetPortIdentity));

  // TLV 1
  // Some fixed value, no clue what it means
  ptr = apple_val1;
  memcpy(ptr, ptp_tlv_orgs[PTP_TLV_ORG_APPLE].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE;
  memcpy(ptr, ptp_tlv_apple_subtypes[PTP_TLV_ORG_APPLE_UNKNOWN1].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE;
  memcpy(ptr, apple_unknown, sizeof(apple_unknown));
  msg_tlv_write(msg->tlv_apple1, sizeof(msg->tlv_apple1), PTP_TLV_ORG_EXTENSION, sizeof(apple_val1), apple_val1);

  // TLV 2
  // Same unknown value, but this is a longer field
  ptr = apple_val2;
  memcpy(ptr, ptp_tlv_orgs[PTP_TLV_ORG_APPLE].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE;
  memcpy(ptr, ptp_tlv_apple_subtypes[PTP_TLV_ORG_APPLE_UNKNOWN5].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE;
  memcpy(ptr, apple_unknown, sizeof(apple_unknown));
  msg_tlv_write(msg->tlv_apple2, sizeof(msg->tlv_apple2), PTP_TLV_ORG_EXTENSION, sizeof(apple_val2), apple_val2);
}

static void
msg_sync_make(struct ptp_sync_message *msg, uint64_t clock_id, uint16_t sequence_id, struct ptp_timestamp ts)
{
  // iOS sets flags to 0x0608 -> UNICAST and TIMESCALE and TWO_STEP
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE | PTP_FLAG_TWO_STEP;

  header_init(&msg->header, PTP_MSGTYPE_SYNC, sizeof(struct ptp_sync_message), clock_id, sequence_id, PTPD_LOGMESSAGEINT_SYNC, flags);

  msg->originTimestamp = ptp_timestamp_htobe(&ts);
}

static void
msg_sync_follow_up_make(struct ptp_follow_up_message *msg, uint64_t clock_id, uint16_t sequence_id, struct ptp_timestamp ts)
{
  uint8_t apple_val1[sizeof(msg->tlv_apple1) - 4] = { 0 };
  uint8_t apple_val2[sizeof(msg->tlv_apple2) - 4] = { 0 };
  uint64_t be64_clock_id = htobe64(clock_id);
  // iOS sets flags to 0x0408 -> UNICAST and TIMESCALE
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE;
  uint8_t *ptr;

  header_init(&msg->header, PTP_MSGTYPE_FOLLOW_UP, sizeof(struct ptp_follow_up_message), clock_id, sequence_id, PTPD_LOGMESSAGEINT_SYNC, flags);

  msg->preciseOriginTimestamp = ptp_timestamp_htobe(&ts);

  // TLV 1
  // iOS sets pos 6->9 (4 bytes) all zeros, Wireshark says it's "cumulativeScaledRateOffset"
  // iOS sets pos 10->11 (2 bytes) all zeros, Wireshark says it's "gmTimeBaseIndicator",
  // which is index identifying the grandmaster's time source.

  // iOS sets pos 12->23 (12 bytes), Wireshark says it's "lastGmPhaseChange"
  // Claude says it "contains information about the last discontinuous change
  // in the phase (time offset) of the Grandmaster clock" and is struct ptp_scaled_ns.
  // iOS example: 0x0000 0000fff117f85390 fadc -> 281410,954351504 (excl. ns_frac)
  // but clock is 145864, so that's strange? Zero here since we don't know better.

  // iOS sets pos 24->27 (4 bytes), Wireshark says it's "scaledLastGmFreqChange"
  // Positive val means GM is running faster than true time (how would it know?)
  // by (scaledLastGmFreqChange / 2^41) nanoseconds per second. Example:
  // 0xf9a33395 = -106744939 -> -106,744,939 / 2,199,023,255,552 = 0.000048 ns/s
  // We set zero because we have no idea if our rate is off from true time and
  // frankly don't care.
  ptr = apple_val1;
  memcpy(ptr, ptp_tlv_orgs[PTP_TLV_ORG_IEEE].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE;
  memcpy(ptr, ptp_tlv_ieee_subtypes[PTP_TLV_ORG_IEEE_FOLLOW_UP_INFO].code, PTP_TLV_ORG_CODE_SIZE);
  msg_tlv_write(msg->tlv_apple1, sizeof(msg->tlv_apple1), PTP_TLV_ORG_EXTENSION, sizeof(apple_val1), apple_val1);

  // TLV 2
  // Apple TLV with clock ID, who knows why
  ptr = apple_val2;
  memcpy(ptr, ptp_tlv_orgs[PTP_TLV_ORG_APPLE].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE;
  memcpy(ptr, ptp_tlv_apple_subtypes[PTP_TLV_ORG_APPLE_CLOCK_ID].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE;
  memcpy(ptr, &be64_clock_id, sizeof(be64_clock_id));
  msg_tlv_write(msg->tlv_apple2, sizeof(msg->tlv_apple2), PTP_TLV_ORG_EXTENSION, sizeof(apple_val2), apple_val2);
}

static void
msg_delay_resp_make(struct ptp_delay_resp_message *msg,
 uint64_t clock_id, uint16_t sequence_id, struct ptp_header *req_header, struct ptp_timestamp ts)
{
  // iOS sets flags to 0x0608 -> UNICAST and TIMESCALE and TWO_STEP
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE | PTP_FLAG_TWO_STEP;

  header_init(&msg->header, PTP_MSGTYPE_DELAY_RESP, sizeof(struct ptp_delay_resp_message), clock_id, sequence_id, PTPD_LOGMESSAGEINT_DELAY_RESP, flags);

  msg->receiveTimestamp = ptp_timestamp_htobe(&ts);

  port_id_htobe(msg->requestingPortIdentity, req_header->sourcePortIdentity);
}

// Haven't seen these messages from iOS, so the implementation is a guess
static void
msg_pdelay_resp_make(struct ptp_pdelay_resp_message *msg,
 uint64_t clock_id, uint16_t sequence_id, struct ptp_header *req_header, struct ptp_timestamp ts)
{
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE | PTP_FLAG_TWO_STEP;

  header_init(&msg->header, PTP_MSGTYPE_PDELAY_RESP, sizeof(struct ptp_pdelay_resp_message), clock_id, sequence_id, PTPD_LOGMESSAGEINT_SYNC, flags);

  msg->requestReceiptTimestamp = ptp_timestamp_htobe(&ts);

  port_id_htobe(msg->requestingPortIdentity, req_header->sourcePortIdentity);
}

// Haven't seen these messages from iOS, so the implementation is a guess
static void
msg_pdelay_resp_follow_up_make(struct ptp_pdelay_resp_follow_up_message *msg,
 uint64_t clock_id, uint16_t sequence_id, struct ptp_header *req_header, struct ptp_timestamp ts)
{
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE;

  header_init(&msg->header, PTP_MSGTYPE_PDELAY_RESP_FOLLOW_UP, sizeof(struct ptp_pdelay_resp_follow_up_message), clock_id, sequence_id, PTPD_LOGMESSAGEINT_SYNC, flags);

  msg->responseOriginTimestamp = ptp_timestamp_htobe(&ts);

  port_id_htobe(msg->requestingPortIdentity, req_header->sourcePortIdentity);
}


/* ======================== Incoming message handling ======================= */

static void
sync_handle(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  struct ptp_sync_message *in = (struct ptp_sync_message *)req;
  struct ptp_sync_message sync = { 0 };
  uint64_t clock_id;

  if (req_len < sizeof(struct ptp_sync_message))
    return;

  header_read(&sync.header, &clock_id, req);
  sync.originTimestamp = ptp_timestamp_betoh(&in->originTimestamp);

  log_received("Sync", &sync.header, clock_id, &sync.originTimestamp);
}

static void
follow_up_handle(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  struct ptp_follow_up_message *in = (struct ptp_follow_up_message *)req;
  struct ptp_follow_up_message follow_up = { 0 };
  uint64_t clock_id;

  if (req_len < sizeof(struct ptp_follow_up_message))
    return;

  header_read(&follow_up.header, &clock_id, req);
  follow_up.preciseOriginTimestamp = ptp_timestamp_betoh(&in->preciseOriginTimestamp);

  log_received("Follow Up", &follow_up.header, clock_id, &follow_up.preciseOriginTimestamp);
}

static void
delay_req_handle(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  struct ptp_delay_req_message *in = (struct ptp_delay_req_message *)req;
  struct ptp_delay_req_message delay_req = { 0 };
  uint64_t clock_id;
  struct ptp_delay_resp_message	delay_resp;
  struct ptp_timestamp ts;
  ssize_t len;

  if (req_len < sizeof(struct ptp_delay_req_message))
    return;

  header_read(&delay_req.header, &clock_id, req);
  delay_req.originTimestamp = ptp_timestamp_betoh(&in->originTimestamp);

  log_received("Delay Req", &delay_req.header, clock_id, &delay_req.originTimestamp);

  ts = current_time_get();
  msg_delay_resp_make(&delay_resp, state->clock_id, delay_req.header.sequenceId, &delay_req.header, ts);

  port_set(peer_addr, PTPD_GENERAL_PORT);
  len = sendto(state->general_svc.fd, &delay_resp, sizeof(delay_resp), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(delay_resp))
    DPRINTF(E_LOG, L_AIRPLAY, "Incomplete send of struct ptp_pdelay_resp_follow_up_message\n");

  log_sent((uint8_t *)&delay_resp, PTPD_GENERAL_PORT);
}

// Since we are announcing ourselves as a very precise clock we always expect to
// become master clock. Given that assumption holds true, we can just ignore
// other announcements.
static void
announce_handle(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
#if PTPD_LOG_RECEIVED
  struct ptp_announce_message *in = (struct ptp_announce_message *)req;
  struct ptp_announce_message announce = { 0 };
  uint64_t clock_id;

  if (req_len < sizeof(struct ptp_announce_message))
    return;

  memcpy(&announce, req, sizeof(struct ptp_announce_message));

  header_read(&announce.header, &clock_id, req);

  announce.grandmasterIdentity = be64toh(in->grandmasterIdentity);
  announce.grandmasterClockQuality = be32toh(in->grandmasterClockQuality);
  announce.stepsRemoved = be16toh(in->stepsRemoved);
  announce.currentUtcOffset = be16toh(in->currentUtcOffset);

  uint8_t clock_class = (announce.grandmasterClockQuality >> 24) & 0xFF;
  uint8_t clock_accuracy = (announce.grandmasterClockQuality >> 16) & 0xFF;
//  uint16_t offset_scaled_log_variance = announce.grandmasterClockQuality & 0xFFFF;

  const char *time_source_str;
  switch (announce.timeSource) {
    case 0x10: time_source_str = "ATOMIC_CLOCK"; break;
    case 0x20: time_source_str = "GPS"; break;
    case 0x30: time_source_str = "TERRESTRIAL_RADIO"; break;
    case 0x40: time_source_str = "PTP"; break;
    case 0x50: time_source_str = "NTP"; break;
    case 0x60: time_source_str = "HAND_SET"; break;
    case 0x90: time_source_str = "OTHER"; break;
    case 0xA0: time_source_str = "INTERNAL_OSCILLATOR"; break;
    default: time_source_str = "UNKNOWN"; break;
  }

  // Determine clock class description
  const char *clock_class_desc;
  if (clock_class == 6) clock_class_desc = "Primary reference (GPS sync)";
  else if (clock_class == 7) clock_class_desc = "Primary reference";
  else if (clock_class >= 13 && clock_class <= 58) clock_class_desc = "Application-specific";
  else if (clock_class >= 187 && clock_class <= 193) clock_class_desc = "Degraded";
  else if (clock_class == 248) clock_class_desc = "Default";
  else if (clock_class == 255) clock_class_desc = "Slave-only";
  else clock_class_desc = "Reserved";

  int8_t logint = announce.header.logMessageInterval;

  DPRINTF(E_DBG, L_AIRPLAY, "Recevied Announce message from %" PRIx64 ", gm %" PRIx64 ", p1=%u p2=%u, src=%s, class=%u (%s), acc=0x%02X, logint=%" PRIi8 "\n",
    clock_id, announce.grandmasterIdentity, announce.grandmasterPriority1, announce.grandmasterPriority2, time_source_str, clock_class, clock_class_desc, clock_accuracy, logint);

/*
    printf("  Offset Scaled Log Variance: 0x%04X\n", offset_scaled_log_variance);
    printf("  Steps Removed: %u\n", steps_removed);
    printf("  Time Source: 0x%02X (%s)\n", time_source, time_source_str);
    printf("  UTC Offset: %d seconds\n", utc_offset);
*/
#endif
}

static void
pdelay_req_handle(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  struct ptp_header header;
  struct ptp_pdelay_resp_message resp;
  struct ptp_pdelay_resp_follow_up_message followup;
  struct ptp_timestamp ts;
  ssize_t len;

  if (req_len < sizeof(struct ptp_header))
    return;

  header_read(&header, NULL, req);

  ts = current_time_get();
  msg_pdelay_resp_make(&resp, state->clock_id, header.sequenceId, &header, ts);

  port_set(peer_addr, PTPD_EVENT_PORT);
  len = sendto(state->event_svc.fd, &resp, sizeof(resp), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(resp))
    DPRINTF(E_LOG, L_AIRPLAY, "Incomplete send of struct ptp_pdelay_resp_message\n");

  log_sent((uint8_t *)&resp, PTPD_EVENT_PORT);

  ts = current_time_get();
  msg_pdelay_resp_follow_up_make(&followup, state->clock_id, header.sequenceId, &header, ts);

  port_set(peer_addr, PTPD_GENERAL_PORT);
  len = sendto(state->general_svc.fd, &followup, sizeof(followup), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(followup))
    DPRINTF(E_LOG, L_AIRPLAY, "Incomplete send of struct ptp_pdelay_resp_follow_up_message\n");

  log_sent((uint8_t *)&followup, PTPD_GENERAL_PORT);
}

static int
tlv_handle_org_subtype_generic(const char *org, struct ptp_tlv_org_subtype_map *subtype, uint8_t *data, size_t len)
{
  DPRINTF(E_DBG, L_AIRPLAY, "Received '%s' TLV org extension, subtype '%s', length %zu\n", org, subtype->name, len);
  return 0;
}

static int
tlv_handle_org_subtype_message_internal(const char *org, struct ptp_tlv_org_subtype_map *subtype, uint8_t *data, size_t len)
{
  if (len < 6)
    return -1;

  DPRINTF(E_DBG, L_AIRPLAY, "Ignoring PTP signaling linkDelayInterval=%hhd, timeSyncInterval=%hhd, announceInterval=%hhd\n", data[0], data[1], data[2]);
  return 0;
}

static int
tlv_handle_org_extension(uint8_t *data, uint16_t len)
{
  uint8_t orgcode[PTP_TLV_ORG_CODE_SIZE];
  uint8_t subtype[PTP_TLV_ORG_CODE_SIZE];
  uint16_t offset = sizeof(orgcode) + sizeof(subtype);
  struct ptp_tlv_org_map *org;

  if (len < offset)
    return -1;

  memcpy(orgcode, data, PTP_TLV_ORG_CODE_SIZE);
  memcpy(subtype, data + PTP_TLV_ORG_CODE_SIZE, PTP_TLV_ORG_CODE_SIZE);

  for (int i = 0; i < ARRAY_SIZE(ptp_tlv_orgs); i++)
    {
      org = &ptp_tlv_orgs[i];

      if (memcmp(orgcode, org->code, PTP_TLV_ORG_CODE_SIZE) != 0)
	continue;

      for (int j = 0; j < org->n_subtypes; j++)
	{
	  if (memcmp(subtype, org->subtypes[j].code, PTP_TLV_ORG_CODE_SIZE) != 0)
	    continue;

	  return org->subtypes[j].handler(org->name, &org->subtypes[j], data + offset, len - offset);
	}
    }

  return -1;
}

static int
tlv_handle_path_trace(uint8_t *data, uint16_t len)
{
  // Normally we get the 8 byte clock ID here, log if something unexpected
  if (len != 8)
    DHEXDUMP(E_WARN, L_AIRPLAY, data, len, "TLV path trace with unexpected length\n");

  return 0;
}

// Returns length of tlv consumed (0 if no tlv), negative if error
static ssize_t
tlv_handle(uint8_t *tlv, ssize_t tlv_max_size)
{
  uint8_t *ptr = tlv;
  uint16_t be16;
  uint16_t type;
  uint16_t len;
  int ret;

  if (tlv_max_size == 0)
    return 0;
  else if (tlv_max_size < PTP_TLV_MIN_SIZE)
    return -1;

  memcpy(&be16, ptr, sizeof(be16));
  ptr += sizeof(be16);
  type = be16toh(be16);

  memcpy(&be16, ptr, sizeof(be16));
  ptr += sizeof(be16);
  len = be16toh(be16);

  if (tlv_max_size < PTP_TLV_MIN_SIZE + len)
    return -1;

  if (type == PTP_TLV_ORG_EXTENSION)
    ret = tlv_handle_org_extension(ptr, len);
  else if (type == PTP_TLV_PATH_TRACE)
    ret = tlv_handle_path_trace(ptr, len);
  else
    ret = -1;

  return (ret < 0) ? ret : PTP_TLV_MIN_SIZE + len;
}

static void
signaling_handle(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  // 34 bytes header and then 10 bytes targetPortIdentity
  size_t tlv_offset = sizeof(struct ptp_header) + PTP_PORT_ID_SIZE;
  ssize_t req_remaining = req_len - tlv_offset;
  ssize_t tlv_size;

  while ((tlv_size = tlv_handle(req + tlv_offset, req_remaining)) > 0)
    {
      req_remaining -= tlv_size;
      tlv_offset += tlv_size;
    }

  if (tlv_size < 0)
    DHEXDUMP(E_WARN, L_AIRPLAY, req, req_len, "Received invalid or unknown PTP_MSGTYPE_SIGNALING\n");
}

static void
management_handle(struct ptpd_state *state, uint8_t *req, ssize_t req_len, union net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  DHEXDUMP(E_DBG, L_AIRPLAY, req, req_len, "Received PTP_MSGTYPE_MANAGEMENT\n");
}


/* ============================= Slave handling ============================= */

static void
slave_clear(struct ptpd_slave *slave)
{
  free(slave->str_addr);
  memset(slave, 0, sizeof(struct ptpd_slave));
}

static void
slaves_prune(struct ptpd_state *state)
{
  struct ptpd_slave *slave;
  int i;
  int n_pruned;

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
}

static enum command_state
slave_add(void *arg, int *ret)
{
  struct ptpd_state *state = &ptpd;
  struct ptpd_slave *slave = arg;

  // Clean up dead slaves
  slaves_prune(state);

  if (state->num_slaves >= PTPD_MAX_SLAVES)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Max number of PTP slaves reached\n");
      return COMMAND_END;
    }

  DPRINTF(E_DBG, L_AIRPLAY, "Adding new slave with address %s\n", slave->str_addr);

  slave->is_active = true;
  memcpy(&state->slaves[state->num_slaves], slave, sizeof(struct ptpd_slave));
  state->num_slaves++;

  // Trigger announce and signaling immediately
  event_active(state->send_announce_timer, 0, 0);
  event_active(state->send_signaling_timer, 0, 0);

  // We should send sync's at specific interval, so if already running don't
  // disturb the rhythm. I.e. only trigger if not running already.
  if (!event_pending(state->send_sync_timer, EV_TIMEOUT, NULL))
    event_add(state->send_sync_timer, &ptpd_send_sync_tv);

  return COMMAND_END;
}

static enum command_state
slave_remove(void *arg, int *ret)
{
  struct ptpd_state *state = &ptpd;
  struct ptpd_slave *slave = arg;
  uint32_t slave_id = slave->id;
  bool found;
  int i;

  for (i = 0, found = false; i < state->num_slaves; i++)
    {
      slave = &state->slaves[i];
      if (!found && (slave_id == slave->id))
	{
	  slave_clear(slave);
	  found = true;
	  continue;
	}

      // Make sure the list is sequential
      if (found)
	state->slaves[i - 1] = *slave;
    }

  if (!found)
    {
      DPRINTF(E_DBG, L_AIRPLAY, "Can't remove PTP slave, not in our list\n");
      return COMMAND_END;
    }

  state->num_slaves--;

  return COMMAND_END;
}

static void
slaves_msg_send(struct ptpd_state *state, void *msg, size_t msg_len, struct ptpd_service *svc)
{
  struct ptpd_slave *slave;
  ssize_t len;
  union net_sockaddr naddr;
  uint8_t *msg_bin = msg;

  for (int i = 0; i < state->num_slaves; i++)
    {
      slave = &state->slaves[i];
      if (!slave->is_active)
	continue;

      // Copy because we don't want to modify list elements
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
	log_sent(msg_bin, svc->port);
    }
}


/* =========================== Sending and dispatch ========================= */

static void
announce_send(struct ptpd_state *state)
{
  struct ptp_announce_message annnounce;
  struct ptp_timestamp ts = { 0 };

  // iOS just sends 0 as originTimestamp, we do the same
  msg_announce_make(&annnounce, state->clock_id, state->announce_seq, ts);
  slaves_msg_send(state, &annnounce, sizeof(annnounce), &state->general_svc);

  state->announce_seq++;
}

static void
signaling_send(struct ptpd_state *state)
{
  struct ptp_signaling_message signaling;

  // TODO iOS sets targetPortIdentity, we probably also should
  msg_signaling_make(&signaling, state->clock_id, state->signaling_seq, NULL);
  slaves_msg_send(state, &signaling, sizeof(signaling), &state->general_svc);

  state->signaling_seq++;
}

static void
sync_send(struct ptpd_state *state)
{
  struct ptp_sync_message sync;
  struct ptp_follow_up_message followup;
  struct ptp_timestamp ts = { 0 };

  // Two-step PTP is a Sync with a 0 ts and then a Follow-Up with the ts of Sync
  msg_sync_make(&sync, state->clock_id, state->sync_seq, ts);
  ts = current_time_get();

  slaves_msg_send(state, &sync, sizeof(sync), &state->event_svc);

  // Send Follow Up with precise timestamp after a small delay
  usleep(100);
  msg_sync_follow_up_make(&followup, state->clock_id, state->sync_seq, ts);
  slaves_msg_send(state, &followup, sizeof(followup), &state->general_svc);

  state->sync_seq++;
}

static void
ptpd_send_announce_cb(int fd, short what, void *arg)
{
  struct ptpd_state *state = arg;

  if (state->num_slaves == 0)
    return; // Don't reschedule

  announce_send(state);

  event_add(state->send_announce_timer, &ptpd_send_announce_tv);
}

static void
ptpd_send_signaling_cb(int fd, short what, void *arg)
{
  struct ptpd_state *state = arg;

  if (state->num_slaves == 0)
    return; // Don't reschedule

  signaling_send(state);

  event_add(state->send_signaling_timer, &ptpd_send_signaling_tv);
}

static void
ptpd_send_sync_cb(int fd, short what, void *arg)
{
  struct ptpd_state *state = arg;

  if (state->num_slaves == 0)
    return; // Don't reschedule

  sync_send(state);

  event_add(state->send_sync_timer, &ptpd_send_sync_tv);
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

  // Shouldn't be necessary, but silences scan-build complaint about sa_family
  // possibly being garbage after recvfrom()
  peer_addr.sa.sa_family = AF_UNSPEC;

  len = recvfrom(fd, req, sizeof(req), 0, &peer_addr.sa, &peer_addrlen);
  if (len <= 0 || peer_addr.sa.sa_family == AF_UNSPEC)
    {
      if (len < 0)
	DPRINTF(E_LOG, L_AIRPLAY, "Service %s read error: %s\n", svc_name, strerror(errno));
      return;
    }

  msg_type = req[0] & 0x0F; // Only lower bits are message type

  switch (msg_type)
    {
      case PTP_MSGTYPE_ANNOUNCE:
	announce_handle(state, req, len, &peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_SYNC:
	sync_handle(state, req, len, &peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_FOLLOW_UP:
	follow_up_handle(state, req, len, &peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_DELAY_REQ:
	delay_req_handle(state, req, len, &peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_PDELAY_REQ:
	pdelay_req_handle(state, req, len, &peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_SIGNALING:
	signaling_handle(state, req, len, &peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_MANAGEMENT:
	management_handle(state, req, len, &peer_addr, peer_addrlen);
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

  state->send_announce_timer = evtimer_new(state->evbase, ptpd_send_announce_cb, state);
  state->send_signaling_timer = evtimer_new(state->evbase, ptpd_send_signaling_cb, state);
  state->send_sync_timer = evtimer_new(state->evbase, ptpd_send_sync_cb, state);

  state->is_running = true;

  event_base_dispatch(state->evbase);

  if (state->is_running)
    DPRINTF(E_LOG, L_AIRPLAY, "ptpd event loop terminated ahead of time!\n");

 stop:
  if (state->send_announce_timer)
    event_free(state->send_announce_timer);
  if (state->send_signaling_timer)
    event_free(state->send_signaling_timer);
  if (state->send_sync_timer)
    event_free(state->send_sync_timer);
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

// Thread: player (must not block!)
int
ptpd_slave_add(uint32_t *slave_id, const char *addr)
{
  struct ptpd_slave *slave;

  if (!ptpd.is_running)
    return -1;

  CHECK_NULL(L_AIRPLAY, slave = calloc(1, sizeof(struct ptpd_slave)));

  if (net_sockaddr_get(&slave->naddr, addr, 0) < 0)
    {
      DPRINTF(E_DBG, L_AIRPLAY, "Ignoring PTP address %s\n", addr);
      free(slave);
      return -1;
    }

  slave->id = djb_hash(addr, strlen(addr));
  slave->naddr_len = (slave->naddr.sa.sa_family == AF_INET6) ? sizeof(slave->naddr.sin6) : sizeof(slave->naddr.sin);
  slave->str_addr = strdup(addr);

  *slave_id = slave->id;

  commands_exec_async(ptpd.cmdbase, slave_add, slave);
  return 0;
}

// Thread: player (must not block!)
void
ptpd_slave_remove(uint32_t slave_id)
{
  struct ptpd_slave *slave;

  if (!ptpd.is_running)
    return;

  if (slave_id == 0)
    return;

  CHECK_NULL(L_AIRPLAY, slave = calloc(1, sizeof(struct ptpd_slave)));

  slave->id = slave_id;

  commands_exec_async(ptpd.cmdbase, slave_remove, slave);
}

// Thread: main (with root priviliges)
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

// Thread: main (normal priviliges)
int
ptpd_init(uint64_t clock_id_seed)
{
  int i;

  // Check alignment of enum and array indices
  for (i = 0; i < ARRAY_SIZE(ptp_tlv_orgs); i++)
    assert(ptp_tlv_orgs[i].index == i);
  for (i = 0; i < ARRAY_SIZE(ptp_tlv_apple_subtypes); i++)
    assert(ptp_tlv_apple_subtypes[i].index == i);
  for (i = 0; i < ARRAY_SIZE(ptp_tlv_ieee_subtypes); i++)
    assert(ptp_tlv_ieee_subtypes[i].index == i);

  // No point in starting if ptpd_bind() failed
  if (ptpd.event_svc.fd < 0 || ptpd.general_svc.fd < 0)
    return -1;

  // From IEEE EUI-64 clockIdentity values: "The most significant 3 octets of
  // the clockIdentity shall be an OUI. The least significant two bits of the
  // most significant octet of the OUI shall both be 0. The least significant
  // bit of the most significant octet of the OUI is used to distinguish
  // clockIdentity values specified by this subclause from those specified in
  // 7.5.2.2.3 [Non-IEEE EUI-64 clockIdentity values]".
  // If we had the MAC address at this point we, could make a valid EUI-48 based
  // clocked from mac[0..2] + 0xFFFE + mac[3..5]. However, since we don't, we
  // create a non-EUI-64 clock ID from 0xFFFF + 6 byte seed, ref 7.5.2.2.3.
  ptpd.clock_id = clock_id_seed | 0xFFFF000000000000;

  CHECK_NULL(L_AIRPLAY, ptpd.evbase = event_base_new());
  CHECK_NULL(L_AIRPLAY, ptpd.cmdbase = commands_base_new(ptpd.evbase, NULL));
  CHECK_ERR(L_AIRPLAY, pthread_create(&ptpd.tid, NULL, run, &ptpd));

  return 0;
}

// Thread: main (normal priviliges)
void
ptpd_deinit(void)
{
  int ret;

  ptpd.is_running = false;

  commands_base_destroy(ptpd.cmdbase);

  ret = pthread_join(ptpd.tid, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not join ptpd thread: %s\n", strerror(errno));
      return;
    }

  event_base_free(ptpd.evbase);
}
