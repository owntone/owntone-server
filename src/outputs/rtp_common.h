#ifndef __RTP_COMMON_H__
#define __RTP_COMMON_H__

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

struct rtcp_timestamp
{
  uint32_t pos;
  struct timespec ts;
};

struct rtp_packet
{
  uint16_t seqnum;     // Sequence number
  int samples;         // Number of samples in the packet

  uint8_t *header;     // Pointer to the RTP header

  uint8_t *payload;    // Pointer to the RTP payload
  size_t payload_size; // Size of allocated memory for RTP payload
  size_t payload_len;  // Length of payload (must of course not exceed size)

  uint8_t *data;       // Pointer to the complete packet data
  size_t data_size;    // Size of packet data
  size_t data_len;     // Length of actual packet data
};

// An RTP session is characterised by all the receivers belonging to the session
// getting the same RTP and RTCP packets. So if you have clients that require
// different sample rates or where only some can accept encrypted payloads then
// you need multiple sessions.
struct rtp_session
{
  uint32_t ssrc_id;
  uint32_t pos;
  uint16_t seqnum;

  struct media_quality quality;

  // Packet buffer (ring buffer), used for retransmission
  struct rtp_packet *pktbuf;
  size_t pktbuf_next;
  size_t pktbuf_size;
  size_t pktbuf_len;

  // Number of samples to elapse before sync'ing. If 0 we set it to the s/r, so
  // we sync once a second. If negative we won't sync.
  int sync_each_nsamples;
  int sync_counter;
  struct rtp_packet sync_packet_next;
};


struct rtp_session *
rtp_session_new(struct media_quality *quality, int pktbuf_size, int sync_each_nsamples);

void
rtp_session_free(struct rtp_session *session);

void
rtp_session_flush(struct rtp_session *session);


/* Gets the next packet from the packet buffer, pkt->payload will be allocated
 * to a size of payload_len (or larger).
 *
 * @in  session       RTP session
 * @in  payload_len   Length of payload the packet needs to hold
 * @in  samples       Number of samples in packet
 * @in  payload_type  RTP payload type
 * @in  marker_bit    Marker bit, see RFC3551
 * @return            Pointer to the next packet in the packet buffer
 */
struct rtp_packet *
rtp_packet_next(struct rtp_session *session, size_t payload_len, int samples, char payload_type, char marker_bit);

/* Call this after finalizing a packet, i.e. writing the payload and possibly
 * sending. Registers the packet as final, i.e. it can now be retrieved with
 * rtp_packet_get() for retransmission, if required. Also advances RTP position
 * (seqnum and RTP time).
 *
 * @in  session       RTP session
 * @in  pkt           RTP packet to commit
 */
void
rtp_packet_commit(struct rtp_session *session, struct rtp_packet *pkt);

/* Get a previously committed packet from the packet buffer
 *
 * @in  session       RTP session
 * @in  seqnum        Packet sequence number
 */
struct rtp_packet *
rtp_packet_get(struct rtp_session *session, uint16_t seqnum);

bool
rtp_sync_is_time(struct rtp_session *session);

struct rtp_packet *
rtp_sync_packet_next(struct rtp_session *session, struct rtcp_timestamp cur_stamp, char type);

#endif  /* !__RTP_COMMON_H__ */
