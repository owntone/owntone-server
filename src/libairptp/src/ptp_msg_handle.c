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
#include <time.h>
#include <assert.h>

#include "airptp_internal.h"
#include "ptp_definitions.h"
#include "daemon.h" // TODO get rid of?

// Debugging
#define AIRPTP_LOG_RECEIVED 0
#define AIRPTP_LOG_SENT 0

// Forward tlv handlers
static int tlv_handle_org_subtype_generic(struct airptp_daemon *, const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);
static int tlv_handle_org_subtype_message_internal(struct airptp_daemon *, const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);
static int tlv_handle_org_subtype_peer_add(struct airptp_daemon *, const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);
static int tlv_handle_org_subtype_peer_del(struct airptp_daemon *, const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);

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

static struct ptp_tlv_org_subtype_map ptp_tlv_own_subtypes[] =
{
  { PTP_TLV_ORG_OWN_PEER_ADD, { 0x00, 0x00, 0x01 }, "Add peer", tlv_handle_org_subtype_peer_add },
  { PTP_TLV_ORG_OWN_PEER_DEL, { 0x00, 0x00, 0x02 }, "Remove peer", tlv_handle_org_subtype_peer_del },
};

static struct ptp_tlv_org_map ptp_tlv_orgs[] =
{
  { PTP_TLV_ORG_IEEE, { 0x00, 0x80, 0xc2 }, "IEEE 802.1 Chair", ptp_tlv_ieee_subtypes, ARRAY_SIZE(ptp_tlv_ieee_subtypes) },
  { PTP_TLV_ORG_APPLE, { 0x00, 0x0d, 0x93 }, "Apple, Inc", ptp_tlv_apple_subtypes, ARRAY_SIZE(ptp_tlv_apple_subtypes) },
  { PTP_TLV_ORG_OWN, { 0x99, 0x99, 0x99 }, "OwnTone Ltd", ptp_tlv_own_subtypes, ARRAY_SIZE(ptp_tlv_own_subtypes) },
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
port_set(union utils_net_sockaddr *naddr, unsigned short port)
{
  if (naddr->sa.sa_family == AF_INET6)
    naddr->sin6.sin6_port = htons(port);
  else if (naddr->sa.sa_family == AF_INET)
    naddr->sin.sin_port = htons(port);
}

#if AIRPTP_LOG_RECEIVED
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

  airptp_logmsg("Received %s from clock %" PRIx64 ", logint=%" PRIi8 " with timestamp %" PRIu64 ".%" PRIu32, name, clock_id, logint, tv_sec, tv_nsec);
}
#else
static void
log_received(const char *name, struct ptp_header *header, uint64_t clock_id, struct ptp_timestamp *ts)
{
  return;
}
#endif

#if AIRPTP_LOG_SENT
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
      case PTP_MSGTYPE_SIGNALING:
	name = "PTP_MSGTYPE_SIGNALING";
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

  airptp_logmsg("Sent %s to port %hu, clock_id=%" PRIx64 ", ts=%" PRIu64 ".%" PRIu32, name, port, clock_id, tv_sec, tv_nsec);
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
  hdr->domainNumber = AIRPTP_DOMAIN;
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

  header_init(&msg->header, PTP_MSGTYPE_ANNOUNCE, sizeof(struct ptp_announce_message), clock_id, sequence_id, AIRPTP_LOGMESSAGEINT_ANNOUNCE, flags);

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

  header_init(&msg->header, PTP_MSGTYPE_SIGNALING, sizeof(struct ptp_signaling_message), clock_id, sequence_id, AIRPTP_LOGMESSAGEINT_SIGNALING, flags);

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

  header_init(&msg->header, PTP_MSGTYPE_SYNC, sizeof(struct ptp_sync_message), clock_id, sequence_id, AIRPTP_LOGMESSAGEINT_SYNC, flags);

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

  header_init(&msg->header, PTP_MSGTYPE_FOLLOW_UP, sizeof(struct ptp_follow_up_message), clock_id, sequence_id, AIRPTP_LOGMESSAGEINT_SYNC, flags);

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

  header_init(&msg->header, PTP_MSGTYPE_DELAY_RESP, sizeof(struct ptp_delay_resp_message), clock_id, sequence_id, AIRPTP_LOGMESSAGEINT_DELAY_RESP, flags);

  msg->receiveTimestamp = ptp_timestamp_htobe(&ts);

  port_id_htobe(msg->requestingPortIdentity, req_header->sourcePortIdentity);
}

// Haven't seen these messages from iOS, so the implementation is a guess
static void
msg_pdelay_resp_make(struct ptp_pdelay_resp_message *msg,
 uint64_t clock_id, uint16_t sequence_id, struct ptp_header *req_header, struct ptp_timestamp ts)
{
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE | PTP_FLAG_TWO_STEP;

  header_init(&msg->header, PTP_MSGTYPE_PDELAY_RESP, sizeof(struct ptp_pdelay_resp_message), clock_id, sequence_id, AIRPTP_LOGMESSAGEINT_SYNC, flags);

  msg->requestReceiptTimestamp = ptp_timestamp_htobe(&ts);

  port_id_htobe(msg->requestingPortIdentity, req_header->sourcePortIdentity);
}

// Haven't seen these messages from iOS, so the implementation is a guess
static void
msg_pdelay_resp_follow_up_make(struct ptp_pdelay_resp_follow_up_message *msg,
 uint64_t clock_id, uint16_t sequence_id, struct ptp_header *req_header, struct ptp_timestamp ts)
{
  uint16_t flags = PTP_FLAG_UNICAST | PTP_FLAG_TIMESCALE;

  header_init(&msg->header, PTP_MSGTYPE_PDELAY_RESP_FOLLOW_UP, sizeof(struct ptp_pdelay_resp_follow_up_message), clock_id, sequence_id, AIRPTP_LOGMESSAGEINT_SYNC, flags);

  msg->responseOriginTimestamp = ptp_timestamp_htobe(&ts);

  port_id_htobe(msg->requestingPortIdentity, req_header->sourcePortIdentity);
}

static void
msg_peer_add_make(struct ptp_peer_signaling_message *msg, struct airptp_peer *peer, uint64_t clock_id)
{
  uint8_t peerinfo[sizeof(msg->tlv_peer_info) - 4] = { 0 };
  uint32_t be32_peer_id = htobe32(peer->id);
  uint8_t addr_len = peer->naddr_len;
  uint8_t *ptr;

  header_init(&msg->header, PTP_MSGTYPE_SIGNALING, sizeof(struct ptp_peer_signaling_message), clock_id, 0, 0, PTP_FLAG_UNICAST);

  memset(msg->targetPortIdentity, 0, PTP_PORT_ID_SIZE);

  assert(addr_len <= 28); // sizeof(struct sockaddr_in6)

  ptr = peerinfo;
  memcpy(ptr, ptp_tlv_orgs[PTP_TLV_ORG_OWN].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE; // 3
  memcpy(ptr, ptp_tlv_own_subtypes[PTP_TLV_ORG_OWN_PEER_ADD].code, PTP_TLV_ORG_CODE_SIZE);  // Add
  ptr += PTP_TLV_ORG_CODE_SIZE; // 3
  memcpy(ptr, &be32_peer_id, sizeof(be32_peer_id));
  ptr += sizeof(be32_peer_id); // 4
  memcpy(ptr, &addr_len, sizeof(addr_len));
  ptr += sizeof(addr_len); // 1
  memcpy(ptr, &peer->naddr, addr_len);
  msg_tlv_write(msg->tlv_peer_info, sizeof(msg->tlv_peer_info), PTP_TLV_ORG_EXTENSION, sizeof(peerinfo), peerinfo);
}

static void
msg_peer_del_make(struct ptp_peer_signaling_message *msg, struct airptp_peer *peer, uint64_t clock_id)
{
  uint8_t peerinfo[sizeof(msg->tlv_peer_info) - 4] = { 0 };
  uint32_t be32_peer_id = htobe32(peer->id);
  uint8_t *ptr;

  header_init(&msg->header, PTP_MSGTYPE_SIGNALING, sizeof(struct ptp_peer_signaling_message), clock_id, 0, 0, PTP_FLAG_UNICAST);

  memset(msg->targetPortIdentity, 0, PTP_PORT_ID_SIZE);

  ptr = peerinfo;
  memcpy(ptr, ptp_tlv_orgs[PTP_TLV_ORG_OWN].code, PTP_TLV_ORG_CODE_SIZE);
  ptr += PTP_TLV_ORG_CODE_SIZE; // 3
  memcpy(ptr, ptp_tlv_own_subtypes[PTP_TLV_ORG_OWN_PEER_DEL].code, PTP_TLV_ORG_CODE_SIZE); // Delete
  ptr += PTP_TLV_ORG_CODE_SIZE; // 3
  memcpy(ptr, &be32_peer_id, sizeof(be32_peer_id));
  msg_tlv_write(msg->tlv_peer_info, sizeof(msg->tlv_peer_info), PTP_TLV_ORG_EXTENSION, sizeof(peerinfo), peerinfo);
}


/* ======================== Incoming message handling ======================= */

static void
sync_handle(struct airptp_daemon *daemon, uint8_t *req, ssize_t req_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addr_len)
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
follow_up_handle(struct airptp_daemon *daemon, uint8_t *req, ssize_t req_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addr_len)
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
delay_msg_handle(struct airptp_daemon *daemon, uint8_t *req, ssize_t req_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addr_len)
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
  msg_delay_resp_make(&delay_resp, daemon->clock_id, delay_req.header.sequenceId, &delay_req.header, ts);

  port_set(peer_addr, daemon->general_svc.port);
  len = sendto(daemon->general_svc.fd, &delay_resp, sizeof(delay_resp), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(delay_resp))
    airptp_logmsg("Incomplete send of struct ptp_pdelay_resp_follow_up_message");

  log_sent((uint8_t *)&delay_resp, daemon->general_svc.port);
}

// Since we are announcing ourselves as a very precise clock we always expect to
// become master clock. Given that assumption holds true, we can just ignore
// other announcements.
static void
announce_handle(struct airptp_daemon *daemon, uint8_t *req, ssize_t req_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
#if AIRPTP_LOG_RECEIVED
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

  airptp_logmsg("Recevied Announce message from %" PRIx64 ", gm %" PRIx64 ", p1=%u p2=%u, src=%s, class=%u (%s), acc=0x%02X, logint=%" PRIi8,
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
pdelay_msg_handle(struct airptp_daemon *daemon, uint8_t *req, ssize_t req_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addr_len)
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
  msg_pdelay_resp_make(&resp, daemon->clock_id, header.sequenceId, &header, ts);

  port_set(peer_addr, daemon->event_svc.port);
  len = sendto(daemon->event_svc.fd, &resp, sizeof(resp), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(resp))
    airptp_logmsg("Incomplete send of struct ptp_pdelay_resp_message");

  log_sent((uint8_t *)&resp, daemon->event_svc.port);

  ts = current_time_get();
  msg_pdelay_resp_follow_up_make(&followup, daemon->clock_id, header.sequenceId, &header, ts);

  port_set(peer_addr, daemon->general_svc.port);
  len = sendto(daemon->general_svc.fd, &followup, sizeof(followup), 0, &peer_addr->sa, peer_addr_len);
  if (len != sizeof(followup))
    airptp_logmsg("Incomplete send of struct ptp_pdelay_resp_follow_up_message");

  log_sent((uint8_t *)&followup, daemon->general_svc.port);
}

static int
tlv_handle_org_subtype_generic(struct airptp_daemon *daemon, const char *org, struct ptp_tlv_org_subtype_map *subtype, uint8_t *data, size_t len)
{
  airptp_logmsg("Received '%s' TLV org extension, subtype '%s', length %zu", org, subtype->name, len);
  return 0;
}

static int
tlv_handle_org_subtype_message_internal(struct airptp_daemon *daemon, const char *org, struct ptp_tlv_org_subtype_map *subtype, uint8_t *data, size_t len)
{
  if (len < 6)
    return -1;

  airptp_logmsg("Ignoring PTP signaling linkDelayInterval=%hhd, timeSyncInterval=%hhd, announceInterval=%hhd", data[0], data[1], data[2]);
  return 0;
}

static int
tlv_handle_org_subtype_peer_add(struct airptp_daemon *daemon, const char *org, struct ptp_tlv_org_subtype_map *subtype, uint8_t *data, size_t len)
{
  uint32_t be32_peer_id;
  uint8_t addr_len;
  struct airptp_peer peer = { 0 };
  uint8_t *ptr;

  if (len < sizeof(be32_peer_id) + sizeof(addr_len))
    goto error;

  ptr = data;
  memcpy(&be32_peer_id, ptr, sizeof(be32_peer_id));
  ptr += sizeof(be32_peer_id);
  memcpy(&addr_len, ptr, sizeof(addr_len));
  ptr += sizeof(addr_len);

  if (len < (ptr - data) + addr_len)
    goto error;

  memcpy(&peer.naddr, ptr, addr_len);
  peer.id = be32toh(be32_peer_id);
  peer.naddr_len = addr_len;

  daemon_peer_add(daemon, &peer);
  return 0;

 error:
  return -1;
}

static int
tlv_handle_org_subtype_peer_del(struct airptp_daemon *daemon, const char *org, struct ptp_tlv_org_subtype_map *subtype, uint8_t *data, size_t len)
{
  uint32_t be32_peer_id;
  struct airptp_peer peer = { 0 };

  if (len < sizeof(be32_peer_id))
    goto error;

  memcpy(&be32_peer_id, data, sizeof(be32_peer_id));

  peer.id = be32toh(be32_peer_id);

  daemon_peer_del(daemon, &peer);
  return 0;

 error:
  return -1;
}

static int
tlv_handle_org_extension(struct airptp_daemon *daemon, uint8_t *data, uint16_t len)
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

	  return org->subtypes[j].handler(daemon, org->name, &org->subtypes[j], data + offset, len - offset);
	}
    }

  return -1;
}

static int
tlv_handle_path_trace(struct airptp_daemon *daemon, uint8_t *data, uint16_t len)
{
  // Normally we get the 8 byte clock ID here, log if something unexpected
  if (len != 8)
    airptp_hexdump("TLV path trace with unexpected length", data, len);

  return 0;
}

// Returns length of tlv consumed (0 if no tlv), negative if error
static ssize_t
tlv_handle(struct airptp_daemon *daemon, uint8_t *tlv, ssize_t tlv_max_size)
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
    ret = tlv_handle_org_extension(daemon, ptr, len);
  else if (type == PTP_TLV_PATH_TRACE)
    ret = tlv_handle_path_trace(daemon, ptr, len);
  else
    ret = -1;

  return (ret < 0) ? ret : PTP_TLV_MIN_SIZE + len;
}

static void
signaling_handle(struct airptp_daemon *daemon, uint8_t *req, ssize_t req_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  // 34 bytes header and then 10 bytes targetPortIdentity
  size_t tlv_offset = sizeof(struct ptp_header) + PTP_PORT_ID_SIZE;
  ssize_t req_remaining = req_len - tlv_offset;
  ssize_t tlv_size;

  while ((tlv_size = tlv_handle(daemon, req + tlv_offset, req_remaining)) > 0)
    {
      req_remaining -= tlv_size;
      tlv_offset += tlv_size;
    }

  if (tlv_size < 0)
    airptp_hexdump("Received invalid or unknown PTP_MSGTYPE_SIGNALING", req, req_len);
}

static void
management_handle(struct airptp_daemon *daemon, uint8_t *req, ssize_t req_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addr_len)
{
  airptp_hexdump("Received PTP_MSGTYPE_MANAGEMENT", req, req_len);
}


/* ============================= Message sending ============================ */

static int
localhost_msg_send(void *msg, size_t msg_len, unsigned short port)
{
  struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM };
  struct addrinfo *info = NULL;
  char strport[8];
  int fd = -1;
  ssize_t len = -1;

  snprintf(strport, sizeof(strport), "%hu", port);
  if (getaddrinfo("localhost", strport, &hints, &info) < 0)
    goto error;

  fd = socket(info->ai_family, SOCK_DGRAM, 0);
  if (fd < 0)
    goto error;

  len = sendto(fd, msg, msg_len, 0, info->ai_addr, info->ai_addrlen);

 error:
  if (fd >= 0)
    close(fd);
  if (info)
    freeaddrinfo(info);

  return (len == msg_len) ? 0 : -1;
}

static void
peers_msg_send(struct airptp_daemon *daemon, void *msg, size_t msg_len, struct airptp_service *svc)
{
  struct airptp_peer *peer;
  ssize_t len;
  union utils_net_sockaddr naddr;
  uint8_t *msg_bin = msg;
  uint64_t now = time(NULL);

  for (int i = 0; i < daemon->num_peers; i++)
    {
      peer = &daemon->peers[i];
      if (peer->last_seen + AIRPTP_STALE_SECS < now)
	peer->is_active = false; // Mark for removal

      if (!peer->is_active)
	continue;

      // Copy because we don't want to modify list elements
      memcpy(&naddr, &peer->naddr, peer->naddr_len);

      port_set(&naddr, svc->port);
      len = sendto(svc->fd, msg, msg_len, 0, &naddr.sa, peer->naddr_len);
      if (len < 0)
	{
	  airptp_logmsg("Error sending PTP msg %02x", msg_bin[0]);
	  peer->is_active = false; // Will be removed deferred by peers_prune()
	}
      else if (len != msg_len)
	airptp_logmsg("Incomplete send of msg %02x", msg_bin[0]);
      else
	log_sent(msg_bin, svc->port);
    }
}

void
ptp_msg_announce_send(struct airptp_daemon *daemon)
{
  struct ptp_announce_message annnounce;
  struct ptp_timestamp ts = { 0 };

  // iOS just sends 0 as originTimestamp, we do the same
  msg_announce_make(&annnounce, daemon->clock_id, daemon->announce_seq, ts);
  peers_msg_send(daemon, &annnounce, sizeof(annnounce), &daemon->general_svc);

  daemon->announce_seq++;
}

void
ptp_msg_signaling_send(struct airptp_daemon *daemon)
{
  struct ptp_signaling_message signaling;

  // TODO iOS sets targetPortIdentity, we probably also should
  msg_signaling_make(&signaling, daemon->clock_id, daemon->signaling_seq, NULL);
  peers_msg_send(daemon, &signaling, sizeof(signaling), &daemon->general_svc);

  daemon->signaling_seq++;
}

void
ptp_msg_sync_send(struct airptp_daemon *daemon)
{
  struct ptp_sync_message sync;
  struct ptp_follow_up_message followup;
  struct ptp_timestamp ts = { 0 };

  // Two-step PTP is a Sync with a 0 ts and then a Follow-Up with the ts of Sync
  msg_sync_make(&sync, daemon->clock_id, daemon->sync_seq, ts);
  ts = current_time_get();

  peers_msg_send(daemon, &sync, sizeof(sync), &daemon->event_svc);

  // Send Follow Up with precise timestamp after a small delay
  usleep(100);
  msg_sync_follow_up_make(&followup, daemon->clock_id, daemon->sync_seq, ts);
  peers_msg_send(daemon, &followup, sizeof(followup), &daemon->general_svc);

  daemon->sync_seq++;
}

int
ptp_msg_peer_add_send(struct airptp_peer *peer, struct airptp_handle *hdl, unsigned short port)
{
  struct ptp_peer_signaling_message msg;

  msg_peer_add_make(&msg, peer, hdl->clock_id);
  return localhost_msg_send(&msg, sizeof(msg), port);
}

int
ptp_msg_peer_del_send(struct airptp_peer *peer, struct airptp_handle *hdl, unsigned short port)
{
  struct ptp_peer_signaling_message msg;

  msg_peer_del_make(&msg, peer, hdl->clock_id);
  return localhost_msg_send(&msg, sizeof(msg), port);
}

/* ============================= Message handler ============================ */

void
ptp_msg_handle(struct airptp_daemon *daemon, uint8_t *msg, size_t msg_len, union utils_net_sockaddr *peer_addr, socklen_t peer_addrlen)
{
  uint8_t msg_type = msg[0] & 0x0F; // Only lower bits are message type

  switch (msg_type)
    {
      case PTP_MSGTYPE_ANNOUNCE:
	announce_handle(daemon, msg, msg_len, peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_SYNC:
	sync_handle(daemon, msg, msg_len, peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_FOLLOW_UP:
	follow_up_handle(daemon, msg, msg_len, peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_DELAY_REQ:
	delay_msg_handle(daemon, msg, msg_len, peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_PDELAY_REQ:
	pdelay_msg_handle(daemon, msg, msg_len, peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_SIGNALING:
	signaling_handle(daemon, msg, msg_len, peer_addr, peer_addrlen);
	break;
      case PTP_MSGTYPE_MANAGEMENT:
	management_handle(daemon, msg, msg_len, peer_addr, peer_addrlen);
	break;
      default:
	airptp_hexdump("Received unhandled message", msg, msg_len);
	return;
    }
}

int
ptp_msg_handle_init(void)
{
  int i;
  int n;

  // Check alignment of enum and array indices
  n = 0;
  for (i = 0, n++; i < ARRAY_SIZE(ptp_tlv_apple_subtypes); i++)
    assert(ptp_tlv_apple_subtypes[i].index == i);
  for (i = 0, n++; i < ARRAY_SIZE(ptp_tlv_ieee_subtypes); i++)
    assert(ptp_tlv_ieee_subtypes[i].index == i);
  for (i = 0, n++; i < ARRAY_SIZE(ptp_tlv_own_subtypes); i++)
    assert(ptp_tlv_own_subtypes[i].index == i);
  for (i = 0; i < ARRAY_SIZE(ptp_tlv_orgs); i++)
    assert(ptp_tlv_orgs[i].index == i);
  assert(n == i);

  return 0;
}
