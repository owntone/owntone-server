/*
 * Copyright (C) 2019- Espen Jürgensen <espenjurgensen@gmail.com>
 *
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/param.h>

#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)
# include <sys/endian.h>
#elif defined(HAVE_LIBKERN_OSBYTEORDER_H)
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#endif

#include <gcrypt.h>

#include "logger.h"
#include "misc.h"
#include "rtp_common.h"

#define RTP_HEADER_LEN        12
#define RTCP_SYNC_PACKET_LEN  20 

// NTP timestamp definitions
#define FRAC             4294967296. // 2^32 as a double
#define NTP_EPOCH_DELTA  0x83aa7e80  // 2208988800 - that's 1970 - 1900 in seconds

struct ntp_timestamp
{
  uint32_t sec;
  uint32_t frac;
};


static inline void
timespec_to_ntp(struct timespec *ts, struct ntp_timestamp *ns)
{
  /* Seconds since NTP Epoch (1900-01-01) */
  ns->sec = ts->tv_sec + NTP_EPOCH_DELTA;

  ns->frac = (uint32_t)((double)ts->tv_nsec * 1e-9 * FRAC);
}

static inline void
ntp_to_timespec(struct ntp_timestamp *ns, struct timespec *ts)
{
  /* Seconds since Unix Epoch (1970-01-01) */
  ts->tv_sec = ns->sec - NTP_EPOCH_DELTA;

  ts->tv_nsec = (long)((double)ns->frac / (1e-9 * FRAC));
}

struct rtp_session *
rtp_session_new(struct media_quality *quality, int pktbuf_size, int sync_each_nsamples)
{
  struct rtp_session *session;

  CHECK_NULL(L_PLAYER, session = calloc(1, sizeof(struct rtp_session)));

  // Random SSRC ID, RTP time start and sequence start
  gcry_randomize(&session->ssrc_id, sizeof(session->ssrc_id), GCRY_STRONG_RANDOM);
  gcry_randomize(&session->pos, sizeof(session->pos), GCRY_STRONG_RANDOM);
  gcry_randomize(&session->seqnum, sizeof(session->seqnum), GCRY_STRONG_RANDOM);

  if (quality)
    session->quality = *quality;

  session->pktbuf_size = pktbuf_size;
  CHECK_NULL(L_PLAYER, session->pktbuf = calloc(session->pktbuf_size, sizeof(struct rtp_packet)));

  if (sync_each_nsamples > 0)
    session->sync_each_nsamples = sync_each_nsamples;
  else if (sync_each_nsamples == 0 && quality)
    session->sync_each_nsamples = quality->sample_rate;

  return session;
}

void
rtp_session_free(struct rtp_session *session)
{
  int i;

  for (i = 0; i < session->pktbuf_size; i++)
    free(session->pktbuf[i].data);

  free(session->pktbuf);
  free(session->sync_packet_next.data);
  free(session);
}

void
rtp_session_flush(struct rtp_session *session)
{
  session->pktbuf_len = 0;
  session->sync_counter = 0;
}

// We don't want the caller to malloc payload for every packet, so instead we
// will get him a packet from the ring buffer, thus in most cases reusing memory
struct rtp_packet *
rtp_packet_next(struct rtp_session *session, size_t payload_len, int samples, char payload_type, char marker_bit)
{
  struct rtp_packet *pkt;
  uint16_t seq;
  uint32_t rtptime;
  uint32_t ssrc_id;

  pkt = &session->pktbuf[session->pktbuf_next];

  // When first filling up the buffer we malloc, but otherwise the existing data
  // allocation should in most cases suffice. If not, we realloc.
  if (!pkt->data || payload_len > pkt->payload_size)
    {
      pkt->data_size = RTP_HEADER_LEN + payload_len;
      if (!pkt->data)
	CHECK_NULL(L_PLAYER, pkt->data = malloc(pkt->data_size));
      else
	CHECK_NULL(L_PLAYER, pkt->data = realloc(pkt->data, pkt->data_size));
      pkt->header  = pkt->data;
      pkt->payload = pkt->data + RTP_HEADER_LEN;
      pkt->payload_size = payload_len;
    }

  pkt->samples     = samples;
  pkt->payload_len = payload_len;
  pkt->data_len    = RTP_HEADER_LEN + payload_len;
  pkt->seqnum      = session->seqnum;


  // The RTP header is made of these 12 bytes (RFC 3550):
  //    0                   1                   2                   3
  //    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |                           timestamp                           |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |           synchronization source (SSRC) identifier            |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  pkt->header[0] = 0x80; // Version = 2, P, X and CC are 0
  pkt->header[1] = (marker_bit << 7) | payload_type; // M and payload type

  seq = htobe16(session->seqnum);
  memcpy(pkt->header + 2, &seq, 2);

  rtptime = htobe32(session->pos);
  memcpy(pkt->header + 4, &rtptime, 4);

  ssrc_id = htobe32(session->ssrc_id);
  memcpy(pkt->header + 8, &ssrc_id, 4);

  return pkt;
}

void
rtp_packet_commit(struct rtp_session *session, struct rtp_packet *pkt)
{
  // Increase size of retransmit buffer since we just wrote a packet
  if (session->pktbuf_len < session->pktbuf_size)
    session->pktbuf_len++;

  // Advance counters to prepare for next packet
  session->pktbuf_next = (session->pktbuf_next + 1) % session->pktbuf_size;
  session->seqnum++;
  session->pos += pkt->samples;
  session->sync_counter += pkt->samples;
}

struct rtp_packet *
rtp_packet_get(struct rtp_session *session, uint16_t seqnum)
{
  uint16_t first;
  uint16_t last;
  uint16_t delta;
  size_t idx;

  if (session->pktbuf_len == 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Seqnum %" PRIu16 " requested, but buffer is empty\n", seqnum);
      return NULL;
    }

  last = session->seqnum - 1;
  first = session->seqnum - session->pktbuf_len;

  // Rules of programming:
  // 1) Make your code easy to read
  // 2) Disregard rule number one if you can use XOR
  //
  // The below should be the same as this (a check that works with int wrap-around):
  // (first <= last && (first <= seqnum && seqnum <= last)) || (first > last && (first <= seqnum || seqnum <= last))
  if (! ((first <= last) ^ (first <= seqnum) ^ (seqnum <= last)))
    {
      DPRINTF(E_DBG, L_PLAYER, "Seqnum %" PRIu16 " not in buffer (have seqnum %" PRIu16 " to %" PRIu16 ")\n", seqnum, first, last);
      return NULL;
    }

  // Distance from current seqnum (which is at pktbuf_next) to the requested seqnum
  delta = session->seqnum - seqnum;

  // Adding pktbuf_size so we don't have to deal with "negative" pktbuf_next - delta
  idx = (session->pktbuf_next + session->pktbuf_size - delta) % session->pktbuf_size;

  return &session->pktbuf[idx];
}

bool
rtp_sync_is_time(struct rtp_session *session)
{
  if (session->sync_each_nsamples && session->sync_counter > session->sync_each_nsamples)
    {
      session->sync_counter = 0;
      return true;
    }

  return false;
}

struct rtp_packet *
rtp_sync_packet_next(struct rtp_session *session, struct rtcp_timestamp cur_stamp, char type)
{
  struct ntp_timestamp cur_ts;
  uint32_t rtptime;
  uint32_t cur_pos;

  if (!session->sync_packet_next.data)
    {
      CHECK_NULL(L_PLAYER, session->sync_packet_next.data = malloc(RTCP_SYNC_PACKET_LEN));
      session->sync_packet_next.data_len = RTCP_SYNC_PACKET_LEN;
    }

  session->sync_packet_next.data[0] = type;
  session->sync_packet_next.data[1] = 0xd4;
  session->sync_packet_next.data[2] = 0x00;
  session->sync_packet_next.data[3] = 0x07;

  timespec_to_ntp(&cur_stamp.ts, &cur_ts);

  cur_pos = htobe32(cur_stamp.pos);
  memcpy(session->sync_packet_next.data + 4, &cur_pos, 4);

  cur_ts.sec = htobe32(cur_ts.sec);
  cur_ts.frac = htobe32(cur_ts.frac);
  memcpy(session->sync_packet_next.data + 8, &cur_ts.sec, 4);
  memcpy(session->sync_packet_next.data + 12, &cur_ts.frac, 4);

  rtptime = htobe32(session->pos);
  memcpy(session->sync_packet_next.data + 16, &rtptime, 4);

/*  DPRINTF(E_DBG, L_PLAYER, "SYNC PACKET cur_ts:%ld.%ld, cur_pos:%u, rtptime:%u, type:0x%x, sync_counter:%d\n",
    cur_stamp.ts.tv_sec, cur_stamp.ts.tv_nsec,
    cur_stamp.pos,
    session->pos,
    session->sync_packet_next.data[0],
    session->sync_counter
    );
*/
  return &session->sync_packet_next;
}

