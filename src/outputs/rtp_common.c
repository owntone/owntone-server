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
#include "conffile.h"
#include "misc.h"
#include "player.h"
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
rtp_session_new(struct media_quality *quality, int pktbuf_size, int sync_each_nsamples, int buffer_duration)
{
  struct rtp_session *session;

  CHECK_NULL(L_PLAYER, session = calloc(1, sizeof(struct rtp_session)));

  // Random SSRC ID, RTP time start and sequence start
  gcry_randomize(&session->ssrc_id, sizeof(session->ssrc_id), GCRY_STRONG_RANDOM);
  gcry_randomize(&session->pos, sizeof(session->pos), GCRY_STRONG_RANDOM);
  gcry_randomize(&session->seqnum, sizeof(session->seqnum), GCRY_STRONG_RANDOM);

  session->quality = *quality;

  session->pktbuf_size = pktbuf_size;
  CHECK_NULL(L_PLAYER, session->pktbuf = calloc(session->pktbuf_size, sizeof(struct rtp_packet)));

  if (sync_each_nsamples > 0)
    session->sync_each_nsamples = sync_each_nsamples;
  else if (sync_each_nsamples == 0)
    session->sync_each_nsamples = quality->sample_rate;

  session->buffer_duration = buffer_duration;

  session->is_virgin = true;

  return session;
}

void
rtp_session_free(struct rtp_session *session)
{
  int i;

  for (i = 0; i < session->pktbuf_size; i++)
    free(session->pktbuf[i].data);

  free(session->sync_packet_next.data);

  free(session);
}

void
rtp_session_restart(struct rtp_session *session, struct timespec *ts)
{
  session->is_virgin = true;
  session->start_time = *ts;
  session->pktbuf_len = 0;
  session->sync_counter = 0;
}

// We don't want the caller to malloc payload for every packet, so instead we
// will get him a packet from the ring buffer, thus in most cases reusing memory
struct rtp_packet *
rtp_packet_next(struct rtp_session *session, size_t payload_len, int samples)
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

  // RTP Header
  pkt->header[0] = 0x80; // Version = 2, P, X and CC are 0
  pkt->header[1] = (session->is_virgin) ? 0xe0 : 0x60; // TODO allow other payloads

  seq = htobe16(session->seqnum);
  memcpy(pkt->header + 2, &seq, 2);

  rtptime = htobe32(session->pos);
  memcpy(pkt->header + 4, &rtptime, 4);

  ssrc_id = htobe32(session->ssrc_id);
  memcpy(pkt->header + 8, &ssrc_id, 4);

/*  DPRINTF(E_DBG, L_PLAYER, "RTP PACKET seqnum %u, rtptime %u, payload 0x%x, pktbuf_s %zu\n",
    session->seqnum,
    session->pos,
    pkt->header[1],
    session->pktbuf_len
    );
*/
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

  session->is_virgin = false;
}

struct rtp_packet *
rtp_packet_get(struct rtp_session *session, uint16_t seqnum)
{
  uint16_t first;
  uint16_t last;
  size_t idx;

  if (!session->seqnum || !session->pktbuf_len)
    return NULL;

  last = session->seqnum - 1;
  first = session->seqnum - session->pktbuf_len;
  if (seqnum < first || seqnum > last)
    {
      DPRINTF(E_DBG, L_PLAYER, "Seqnum %" PRIu16 " not in buffer (have seqnum %" PRIu16 " to %" PRIu16 ")\n", seqnum, first, last);
      return NULL;
    }

  idx = (session->pktbuf_next - (session->seqnum - seqnum)) % session->pktbuf_size;

  return &session->pktbuf[idx];
}

bool
rtp_sync_check(struct rtp_session *session, struct rtp_packet *pkt)
{
  if (!session->sync_each_nsamples)
    {
      return false;
    }

  if (session->sync_counter > session->sync_each_nsamples)
    {
      session->sync_counter = 0;
      return true;
    }

  session->sync_counter += pkt->samples; // TODO Should this move to a sync_commit function?
  return false;
}

struct rtp_packet *
rtp_sync_packet_next(struct rtp_session *session)
{
  struct timespec ts;
  struct ntp_timestamp cur_stamp;
  uint64_t elapsed_usec;
  uint64_t elapsed_samples;
  uint32_t rtptime;
  uint32_t cur_pos;
  int ret;

  if (!session->sync_packet_next.data)
    {
      CHECK_NULL(L_PLAYER, session->sync_packet_next.data = malloc(RTCP_SYNC_PACKET_LEN));
      session->sync_packet_next.data_len = RTCP_SYNC_PACKET_LEN;
    }

  memset(session->sync_packet_next.data, 0, session->sync_packet_next.data_len); // TODO remove this and just zero byte 3 instead?

  session->sync_packet_next.data[0] = (session->is_virgin) ? 0x90 : 0x80;
  session->sync_packet_next.data[1] = 0xd4;
  session->sync_packet_next.data[3] = 0x07;

  if (session->is_virgin)
    {
      session->sync_last_check.pos = session->pos - session->buffer_duration * session->quality.sample_rate;
      session->sync_last_check.ts = session->start_time;
      timespec_to_ntp(&session->start_time, &cur_stamp);
    }
  else
    {
      ret = player_get_time(&ts);
      if (ret < 0)
	return NULL;

      elapsed_usec = (ts.tv_sec - session->sync_last_check.ts.tv_sec) * 1000000 + (ts.tv_nsec - session->sync_last_check.ts.tv_nsec) / 1000;

      // How many samples should have been played since last check
      elapsed_samples = (elapsed_usec * session->quality.sample_rate) / 1000000;

      session->sync_last_check.pos += elapsed_samples; // TODO should updating sync_last_check move to a commit function?
      session->sync_last_check.ts = ts;
      timespec_to_ntp(&ts, &cur_stamp);
    }

  cur_pos = htobe32(session->sync_last_check.pos);
  memcpy(session->sync_packet_next.data + 4, &cur_pos, 4);

  cur_stamp.sec = htobe32(cur_stamp.sec);
  cur_stamp.frac = htobe32(cur_stamp.frac);
  memcpy(session->sync_packet_next.data + 8, &cur_stamp.sec, 4);
  memcpy(session->sync_packet_next.data + 12, &cur_stamp.frac, 4);

  rtptime = htobe32(session->pos);
  memcpy(session->sync_packet_next.data + 16, &rtptime, 4);

/*  DPRINTF(E_DBG, L_PLAYER, "SYNC PACKET ts:%ld.%ld, next_pkt:%u, cur_pos:%u, payload:0x%x, sync_counter:%d, init:%d\n",
    ts.tv_sec, ts.tv_nsec,
    session->pos,
    session->sync_last_check.pos,
    session->sync_packet_next.data[0],
    session->sync_counter,
    session->is_virgin
    );
*/
  return &session->sync_packet_next;
}

