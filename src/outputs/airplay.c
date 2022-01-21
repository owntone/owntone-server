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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <gcrypt.h>

#include "plist_wrap.h"

#include "evrtsp/evrtsp.h"
#include "conffile.h"
#include "logger.h"
#include "mdns.h"
#include "misc.h"
#include "player.h"
#include "db.h"
#include "artwork.h"
#include "dmap_common.h"
#include "rtp_common.h"
#include "transcode.h"
#include "outputs.h"

#include "airplay_events.h"
#include "pair_ap/pair.h"

/* List of TODO's for AirPlay 2
 *
 * inplace encryption
 * latency needs different handling
 * support ipv6, e.g. in SETPEERS
 *
 */

// Airplay 2 has a gazallion parameters, many of them unknown to us. With the
// below it is possible to easily try different variations.
#define AIRPLAY_USE_STREAMID                 0
#define AIRPLAY_USE_PAIRING_TRANSIENT        1
#define AIRPLAY_USE_AUTH_SETUP               0

// Full traffic dumps in the log in debug mode
#define AIRPLAY_DUMP_TRAFFIC                 0

#define AIRPLAY_QUALITY_SAMPLE_RATE_DEFAULT     44100
#define AIRPLAY_QUALITY_BITS_PER_SAMPLE_DEFAULT 16
#define AIRPLAY_QUALITY_CHANNELS_DEFAULT        2

// AirTunes v2 number of samples per packet
// Probably using this value because 44100/352 and 48000/352 has good 32 byte
// alignment, which improves performance of some encoders
#define AIRPLAY_SAMPLES_PER_PACKET              352

#define AIRPLAY_RTP_PAYLOADTYPE                 0x60

// For transient pairing the key_len will be 64 bytes, but only 32 are used for
// audio payload encryption. For normal pairing the key is 32 bytes.
#define AIRPLAY_AUDIO_KEY_LEN 32

// How many RTP packets keep in a buffer for retransmission
#define AIRPLAY_PACKET_BUFFER_SIZE    1000

#define AIRPLAY_MD_DELAY_STARTUP      15360
#define AIRPLAY_MD_DELAY_SWITCH       (AIRPLAY_MD_DELAY_STARTUP * 2)
#define AIRPLAY_MD_WANTS_TEXT         (1 << 0)
#define AIRPLAY_MD_WANTS_ARTWORK      (1 << 1)
#define AIRPLAY_MD_WANTS_PROGRESS     (1 << 2)

// ATV4 and Homepod disconnect for reasons that are not clear, but sending them
// progress metadata at regular intervals reduces the problem. The below
// interval was determined via testing, see:
// https://github.com/owntone/owntone-server/issues/734#issuecomment-622959334
#define AIRPLAY_KEEP_ALIVE_INTERVAL   25

// This is an arbitrary value which just needs to be kept in sync with the config
#define AIRPLAY_CONFIG_MAX_VOLUME     11

/* Keep in sync with const char *airplay_devtype */
enum airplay_devtype {
  AIRPLAY_DEV_APEX2_80211N,
  AIRPLAY_DEV_APEX3_80211N,
  AIRPLAY_DEV_APPLETV,
  AIRPLAY_DEV_APPLETV4,
  AIRPLAY_DEV_HOMEPOD,
  AIRPLAY_DEV_OTHER,
};

// Session is starting up
#define AIRPLAY_STATE_F_STARTUP    (1 << 13)
// Streaming is up (connection established)
#define AIRPLAY_STATE_F_CONNECTED  (1 << 14)
// Couldn't start device
#define AIRPLAY_STATE_F_FAILED     (1 << 15)

enum airplay_state {
  // Device is stopped (no session)
  AIRPLAY_STATE_STOPPED   = 0,
  // Session startup
  AIRPLAY_STATE_INFO      = AIRPLAY_STATE_F_STARTUP | 0x01,
  AIRPLAY_STATE_ENCRYPTED = AIRPLAY_STATE_F_STARTUP | 0x02,
  AIRPLAY_STATE_SETUP     = AIRPLAY_STATE_F_STARTUP | 0x03,
  AIRPLAY_STATE_RECORD    = AIRPLAY_STATE_F_STARTUP | 0x04,
  // Session established
  // - streaming ready (RECORD sent and acked, connection established)
  // - commands (SET_PARAMETER) are possible
  AIRPLAY_STATE_CONNECTED = AIRPLAY_STATE_F_CONNECTED | 0x01,
  // Media data is being sent
  AIRPLAY_STATE_STREAMING = AIRPLAY_STATE_F_CONNECTED | 0x02,
  // Session teardown in progress (-> going to STOPPED state)
  AIRPLAY_STATE_TEARDOWN  = AIRPLAY_STATE_F_CONNECTED | 0x03,
  // Session is failed, couldn't startup or error occurred
  AIRPLAY_STATE_FAILED    = AIRPLAY_STATE_F_FAILED | 0x01,
  // Pending PIN or password
  AIRPLAY_STATE_AUTH      = AIRPLAY_STATE_F_FAILED | 0x02,
};

enum airplay_seq_type
{
  AIRPLAY_SEQ_ABORT = -1,
  AIRPLAY_SEQ_START,
  AIRPLAY_SEQ_START_PLAYBACK,
  AIRPLAY_SEQ_PROBE,
  AIRPLAY_SEQ_FLUSH,
  AIRPLAY_SEQ_STOP,
  AIRPLAY_SEQ_FAILURE,
  AIRPLAY_SEQ_PIN_START,
  AIRPLAY_SEQ_SEND_VOLUME,
  AIRPLAY_SEQ_SEND_TEXT,
  AIRPLAY_SEQ_SEND_PROGRESS,
  AIRPLAY_SEQ_SEND_ARTWORK,
  AIRPLAY_SEQ_PAIR_SETUP,
  AIRPLAY_SEQ_PAIR_VERIFY,
  AIRPLAY_SEQ_PAIR_TRANSIENT,
  AIRPLAY_SEQ_FEEDBACK,
  AIRPLAY_SEQ_CONTINUE, // Must be last element
};

// From https://openairplay.github.io/airplay-spec/status_flags.html
enum airplay_status_flags
{
  AIRPLAY_FLAG_PROBLEM_DETECTED               = (1 << 0),
  AIRPLAY_FLAG_NOT_CONFIGURED                 = (1 << 1),
  AIRPLAY_FLAG_AUDIO_CABLE_ATTACHED           = (1 << 2),
  AIRPLAY_FLAG_PIN_REQUIRED                   = (1 << 3),
  AIRPLAY_FLAG_SUPPORTS_FROM_CLOUD            = (1 << 6),
  AIRPLAY_FLAG_PASSWORD_REQUIRED              = (1 << 7),
  AIRPLAY_FLAG_ONE_TIME_PAIRING_REQUIRED      = (1 << 9),
  AIRPLAY_FLAG_SETUP_HK_ACCESS_CTRL           = (1 << 10),
  AIRPLAY_FLAG_SUPPORTS_RELAY                 = (1 << 11),
  AIRPLAY_FLAG_SILENT_PRIMARY                 = (1 << 12),
  AIRPLAY_FLAG_TIGHT_SYNC_IS_GRP_LEADER       = (1 << 13),
  AIRPLAY_FLAG_TIGHT_SYNC_BUDDY_NOT_REACHABLE = (1 << 14),
  AIRPLAY_FLAG_IS_APPLE_MUSIC_SUBSCRIBER      = (1 << 15),
  AIRPLAY_FLAG_CLOUD_LIBRARY_ON               = (1 << 16),
  AIRPLAY_FLAG_RECEIVER_IS_BUSY               = (1 << 17),
};

// Info about the device, which is not required by the player, only internally
struct airplay_extra
{
  enum airplay_devtype devtype;

  char *mdns_name;

  uint16_t wanted_metadata;
  bool supports_auth_setup;
  bool supports_pairing_transient;
};

struct airplay_master_session
{
  struct evbuffer *input_buffer;
  int input_buffer_samples;

  // ALAC encoder and buffer for encoded data
  struct encode_ctx *encode_ctx;
  struct evbuffer *encoded_buffer;

  struct rtp_session *rtp_session;

  struct rtcp_timestamp cur_stamp;

  uint8_t *rawbuf;
  size_t rawbuf_size;
  int samples_per_packet;

  struct media_quality quality;

  // Number of samples that we tell the output to buffer (this will mean that
  // the position that we send in the sync packages are offset by this amount
  // compared to the rtptimes of the corresponding RTP packages we are sending)
  int output_buffer_samples;

  struct airplay_master_session *next;
};

struct airplay_session
{
  uint64_t device_id;
  int callback_id;

  struct airplay_master_session *master_session;

  struct evrtsp_connection *ctrl;

  enum airplay_state state;

  enum airplay_seq_type next_seq;

  uint64_t statusflags;
  uint16_t wanted_metadata;
  bool req_has_auth;
  bool supports_auth_setup;

  struct event *deferredev;

  int reqs_in_flight;
  int cseq;

  uint32_t session_id;
  char session_url[128];
  char session_uuid[37];

  char *realm;
  char *nonce;
  const char *password;

  char *devname;
  char *address;
  int family;

  union net_sockaddr naddr;

  int volume;

  char *local_address;
  unsigned short data_port;
  unsigned short control_port;
  unsigned short events_port;
  unsigned short timing_port; // ATV4 has this set to 0, but it is not used by us anyway

  /* Pairing, see pair.h */
  enum pair_type pair_type;
  struct pair_cipher_context *control_cipher_ctx;
  struct pair_verify_context *pair_verify_ctx;
  struct pair_setup_context *pair_setup_ctx;

  uint8_t shared_secret[64];
  size_t shared_secret_len; // 32 or 64, see AIRPLAY_AUDIO_KEY_LEN for comment

  gcry_cipher_hd_t packet_cipher_hd;

  int server_fd;

  struct airplay_service *timing_svc;
  struct airplay_service *control_svc;

  struct airplay_session *next;
};

struct airplay_metadata
{
  struct evbuffer *metadata;
  struct evbuffer *artwork;
  int artwork_fmt;
};

struct airplay_service
{
  int fd;
  unsigned short port;
  struct event *ev;
};

/* NTP timestamp definitions */
#define FRAC             4294967296. /* 2^32 as a double */
#define NTP_EPOCH_DELTA  0x83aa7e80  /* 2208988800 - that's 1970 - 1900 in seconds */

// TODO move to rtp_common
struct ntp_stamp
{
  uint32_t sec;
  uint32_t frac;
};


/* --------------------------- SEQUENCE DEFINITIONS ------------------------- */

struct airplay_seq_definition
{
  enum airplay_seq_type seq_type;

  // Called when a sequence ends, successfully or not. Shoulds also, if
  // required, take care of notifying  player and free the session.
  void (*on_success)(struct airplay_session *rs);
  void (*on_error)(struct airplay_session *rs);
};

struct airplay_seq_request
{
  enum airplay_seq_type seq_type;
  const char *name; // Name of request (for logging)
  enum evrtsp_cmd_type rtsp_type;
  int (*payload_make)(struct evrtsp_request *req, struct airplay_session *rs, void *arg);
  enum airplay_seq_type (*response_handler)(struct evrtsp_request *req, struct airplay_session *rs);
  const char *content_type;
  const char *uri;
  bool proceed_on_rtsp_not_ok; // If true return code != RTSP_OK will not abort the sequence
};

struct airplay_seq_ctx
{
  struct airplay_seq_request *cur_request;
  void (*on_success)(struct airplay_session *rs);
  void (*on_error)(struct airplay_session *rs);
  struct airplay_session *session;
  void *payload_make_arg;
  const char *log_caller;
};


/* ------------------------------ MISC GLOBALS ------------------------------ */

#if AIRPLAY_USE_AUTH_SETUP
static const uint8_t airplay_auth_setup_pubkey[] =
  "\x59\x02\xed\xe9\x0d\x4e\xf2\xbd\x4c\xb6\x8a\x63\x30\x03\x82\x07"
  "\xa9\x4d\xbd\x50\xd8\xaa\x46\x5b\x5d\x8c\x01\x2a\x0c\x7e\x1d\x4e";
#endif

struct features_type_map
{
  uint32_t bit;
  char *name;
};

// List of features announced by AirPlay 2 speakers
// Credit @invano, see https://emanuelecozzi.net/docs/airplay2
static const struct features_type_map features_map[] =
  {
    { 0, "SupportsAirPlayVideoV1" },
    { 1, "SupportsAirPlayPhoto" },
    { 5, "SupportsAirPlaySlideshow" },
    { 7, "SupportsAirPlayScreen" },
    { 9, "SupportsAirPlayAudio" },
    { 11, "AudioRedunant" },
    { 14, "Authentication_4" }, // FairPlay authentication
    { 15, "MetadataFeatures_0" }, // Send artwork image to receiver
    { 16, "MetadataFeatures_1" }, // Send track progress status to receiver
    { 17, "MetadataFeatures_2" }, // Send NowPlaying info via DAAP
    { 18, "AudioFormats_0" },
    { 19, "AudioFormats_1" },
    { 20, "AudioFormats_2" },
    { 21, "AudioFormats_3" },
    { 23, "Authentication_1" }, // RSA authentication (NA)
    { 26, "Authentication_8" }, // 26 || 51, MFi authentication
    { 27, "SupportsLegacyPairing" },
    { 30, "HasUnifiedAdvertiserInfo" },
    { 32, "IsCarPlay" },
    { 32, "SupportsVolume" }, // !32
    { 33, "SupportsAirPlayVideoPlayQueue" },
    { 34, "SupportsAirPlayFromCloud" }, // 34 && flags_6_SupportsAirPlayFromCloud
    { 35, "SupportsTLS_PSK" },
    { 38, "SupportsUnifiedMediaControl" },
    { 40, "SupportsBufferedAudio" }, // srcvers >= 354.54.6 && 40
    { 41, "SupportsPTP" }, // srcvers >= 366 && 41
    { 42, "SupportsScreenMultiCodec" },
    { 43, "SupportsSystemPairing" },
    { 44, "IsAPValeriaScreenSender" },
    { 46, "SupportsHKPairingAndAccessControl" },
    { 48, "SupportsCoreUtilsPairingAndEncryption" }, // 38 || 46 || 43 || 48
    { 49, "SupportsAirPlayVideoV2" },
    { 50, "MetadataFeatures_3" }, // Send NowPlaying info via bplist
    { 51, "SupportsUnifiedPairSetupAndMFi" },
    { 52, "SupportsSetPeersExtendedMessage" },
    { 54, "SupportsAPSync" },
    { 55, "SupportsWoL" }, // 55 || 56
    { 56, "SupportsWoL" }, // 55 || 56
    { 58, "SupportsHangdogRemoteControl" }, // ((isAppleTV || isAppleAudioAccessory) && 58) || (isThirdPartyTV && flags_10)
    { 59, "SupportsAudioStreamConnectionSetup" }, // 59 && !disableStreamConnectionSetup
    { 60, "SupportsAudioMediaDataControl" }, // 59 && 60 && !disableMediaDataControl
    { 61, "SupportsRFC2198Redundancy" },
  };

/* Keep in sync with enum airplay_devtype */
static const char *airplay_devtype[] =
{
  "AirPort Express 2 - 802.11n",
  "AirPort Express 3 - 802.11n",
  "AppleTV",
  "AppleTV4",
  "HomePod",
  "Other",
};

/* Struct with default quality levels */
static struct media_quality airplay_quality_default =
{
  AIRPLAY_QUALITY_SAMPLE_RATE_DEFAULT,
  AIRPLAY_QUALITY_BITS_PER_SAMPLE_DEFAULT,
  AIRPLAY_QUALITY_CHANNELS_DEFAULT
};

/* From player.c */
extern struct event_base *evbase_player;

/* AirTunes v2 time synchronization */
static struct airplay_service airplay_timing_svc;

/* AirTunes v2 playback synchronization / control */
static struct airplay_service airplay_control_svc;

/* Metadata */
static struct output_metadata *airplay_cur_metadata;

/* Keep-alive timer - hack for ATV's with tvOS 10 */
static struct event *keep_alive_timer;
static struct timeval keep_alive_tv = { AIRPLAY_KEEP_ALIVE_INTERVAL, 0 };

/* Sessions */
static struct airplay_master_session *airplay_master_sessions;
static struct airplay_session *airplay_sessions;

/* Our own device ID */
static uint64_t airplay_device_id;

// Forwards
static int
airplay_device_start(struct output_device *rd, int callback_id);
static void
sequence_start(enum airplay_seq_type seq_type, struct airplay_session *rs, void *arg, const char *log_caller);
static void
sequence_continue(struct airplay_seq_ctx *seq_ctx);


/* ------------------------------- MISC HELPERS ----------------------------- */

static inline int
alac_encode(struct evbuffer *evbuf, struct encode_ctx *encode_ctx, uint8_t *rawbuf, size_t rawbuf_size, int nsamples, struct media_quality *quality)
{
  transcode_frame *frame;
  int len;

  frame = transcode_frame_new(rawbuf, rawbuf_size, nsamples, quality);
  if (!frame)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not convert raw PCM to frame (bufsize=%zu)\n", rawbuf_size);
      return -1;
    }

  len = transcode_encode(evbuf, encode_ctx, frame, 0);
  transcode_frame_free(frame);
  if (len < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not ALAC encode frame\n");
      return -1;
    }

  return len;
}

// AirTunes v2 time synchronization helpers
static inline void
timespec_to_ntp(struct timespec *ts, struct ntp_stamp *ns)
{
  // Seconds since NTP Epoch (1900-01-01)
  ns->sec = ts->tv_sec + NTP_EPOCH_DELTA;

  ns->frac = (uint32_t)((double)ts->tv_nsec * 1e-9 * FRAC);
}

/*
static inline void
ntp_to_timespec(struct ntp_stamp *ns, struct timespec *ts)
{
  // Seconds since Unix Epoch (1970-01-01)
  ts->tv_sec = ns->sec - NTP_EPOCH_DELTA;

  ts->tv_nsec = (long)((double)ns->frac / (1e-9 * FRAC));
}
*/

static inline int
timing_get_clock_ntp(struct ntp_stamp *ns)
{
  struct timespec ts;
  int ret;

  ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Couldn't get clock: %s\n", strerror(errno));

      return -1;
    }

  timespec_to_ntp(&ts, ns);

  return 0;
}

// Converts uint64t libhash -> AA:BB:CC:DD:EE:FF:11:22
static void
device_id_colon_make(char *id_str, int size, uint64_t id)
{
  int r, w;

  snprintf(id_str, size, "%016" PRIX64, id);

  for (r = strlen(id_str) - 1, w = size - 2; r != w; r--, w--)
    {
      id_str[w] = id_str[r];
      if (r % 2 == 0)
        {
	  w--;
	  id_str[w] = ':';
	}
    }

  id_str[size - 1] = 0; // Zero terminate
}

// Converts AA:BB:CC:DD:EE:FF -> AABBCCDDEEFF -> uint64 id
static int
device_id_colon_parse(uint64_t *id, const char *id_str)
{
  char *s;
  char *ptr;
  int ret;

  CHECK_NULL(L_AIRPLAY, s = calloc(1, strlen(id_str) + 1));

  for (ptr = s; *id_str != '\0'; id_str++)
    {
      if (*id_str == ':')
	continue;

      *ptr = *id_str;
      ptr++;
    }

  ret = safe_hextou64(s, id);
  free(s);

  return ret;
}

static int
device_id_find_byname(uint64_t *id, const char *name)
{
  struct output_device *device;
  struct airplay_extra *re;

  for (device = outputs_list(); device; device = device->next)
    {
      if (device->type != OUTPUT_TYPE_AIRPLAY)
	continue;

      re = device->extra_device_info;
      if (strcmp(name, re->mdns_name) == 0)
	break;
    }

  if (!device)
    return -1;

  *id = device->id;
  return 0;
}


/* ------------------------------- Crypto ----------------------------------- */

static void
chacha_close(gcry_cipher_hd_t hd)
{
  if (!hd)
    return;

  gcry_cipher_close(hd);
}

static gcry_cipher_hd_t
chacha_open(const uint8_t *key, size_t key_len)
{
  gcry_cipher_hd_t hd;

  if (gcry_cipher_open(&hd, GCRY_CIPHER_CHACHA20, GCRY_CIPHER_MODE_POLY1305, 0) != GPG_ERR_NO_ERROR)
    goto error;

  if (gcry_cipher_setkey(hd, key, key_len) != GPG_ERR_NO_ERROR)
    goto error;

  return hd;

 error:
  chacha_close(hd);
  return NULL;
}

static int
chacha_encrypt(uint8_t *cipher, uint8_t *plain, size_t plain_len, const void *ad, size_t ad_len, uint8_t *tag, size_t tag_len, uint8_t *nonce, size_t nonce_len, gcry_cipher_hd_t hd)
{
  if (gcry_cipher_setiv(hd, nonce, nonce_len) != GPG_ERR_NO_ERROR)
    return -1;

  if (gcry_cipher_authenticate(hd, ad, ad_len) != GPG_ERR_NO_ERROR)
    return -1;

  if (gcry_cipher_encrypt(hd, cipher, plain_len, plain, plain_len) != GPG_ERR_NO_ERROR)
    return -1;

  if (gcry_cipher_gettag(hd, tag, tag_len) != GPG_ERR_NO_ERROR)
    return -1;

  return 0;
}


/* --------------------- Helpers for sending RTSP requests ------------------ */

// TODO Not sure if the below is still valid for AirPlay 2
static int
request_header_auth_add(struct evrtsp_request *req, struct airplay_session *rs, const char *method, const char *uri)
{
  char ha1[33];
  char ha2[33];
  char ebuf[64];
  char auth[256];
  const char *hash_fmt;
  const char *username;
  uint8_t *hash_bytes;
  size_t hashlen;
  gcry_md_hd_t hd;
  gpg_error_t gc_err;
  int i;
  int ret;

  rs->req_has_auth = 0;

  if (!rs->nonce)
    return 0;

  if (!rs->password)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Authentication required but no password found for device '%s'\n", rs->devname);

      return -2;
    }

  hash_fmt = "%02x";
  username = ""; /* No username */

  gc_err = gcry_md_open(&hd, GCRY_MD_MD5, 0);
  if (gc_err != GPG_ERR_NO_ERROR)
    {
      gpg_strerror_r(gc_err, ebuf, sizeof(ebuf));
      DPRINTF(E_LOG, L_AIRPLAY, "Could not open MD5: %s\n", ebuf);

      return -1;
    }

  memset(ha1, 0, sizeof(ha1));
  memset(ha2, 0, sizeof(ha2));
  hashlen = gcry_md_get_algo_dlen(GCRY_MD_MD5);

  /* HA 1 */

  gcry_md_write(hd, username, strlen(username));
  gcry_md_write(hd, ":", 1);
  gcry_md_write(hd, rs->realm, strlen(rs->realm));
  gcry_md_write(hd, ":", 1);
  gcry_md_write(hd, rs->password, strlen(rs->password));

  hash_bytes = gcry_md_read(hd, GCRY_MD_MD5);
  if (!hash_bytes)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not read MD5 hash\n");

      return -1;
    }

  for (i = 0; i < hashlen; i++)
    sprintf(ha1 + (2 * i), hash_fmt, hash_bytes[i]);

  /* RESET */
  gcry_md_reset(hd);

  /* HA 2 */
  gcry_md_write(hd, method, strlen(method));
  gcry_md_write(hd, ":", 1);
  gcry_md_write(hd, uri, strlen(uri));

  hash_bytes = gcry_md_read(hd, GCRY_MD_MD5);
  if (!hash_bytes)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not read MD5 hash\n");

      return -1;
    }

  for (i = 0; i < hashlen; i++)
    sprintf(ha2 + (2 * i), hash_fmt, hash_bytes[i]);

  /* RESET */
  gcry_md_reset(hd);

  /* Final value */
  gcry_md_write(hd, ha1, 32);
  gcry_md_write(hd, ":", 1);
  gcry_md_write(hd, rs->nonce, strlen(rs->nonce));
  gcry_md_write(hd, ":", 1);
  gcry_md_write(hd, ha2, 32);

  hash_bytes = gcry_md_read(hd, GCRY_MD_MD5);
  if (!hash_bytes)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not read MD5 hash\n");

      return -1;
    }

  for (i = 0; i < hashlen; i++)
    sprintf(ha1 + (2 * i), hash_fmt, hash_bytes[i]);

  gcry_md_close(hd);

  /* Build header */
  ret = snprintf(auth, sizeof(auth), "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"",
		 username, rs->realm, rs->nonce, uri, ha1);
  if ((ret < 0) || (ret >= sizeof(auth)))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Authorization value header exceeds buffer size\n");

      return -1;
    }

  evrtsp_add_header(req->output_headers, "Authorization", auth);

  DPRINTF(E_DBG, L_AIRPLAY, "Authorization header: %s\n", auth);

  rs->req_has_auth = 1;

  return 0;
}
/*
static int
response_header_auth_parse(struct airplay_session *rs, struct evrtsp_request *req)
{
  const char *param;
  char *auth;
  char *token;
  char *ptr;

  if (rs->realm)
    {
      free(rs->realm);
      rs->realm = NULL;
    }

  if (rs->nonce)
    {
      free(rs->nonce);
      rs->nonce = NULL;
    }

  param = evrtsp_find_header(req->input_headers, "WWW-Authenticate");
  if (!param)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "WWW-Authenticate header not found\n");

      return -1;
    }

  DPRINTF(E_DBG, L_AIRPLAY, "WWW-Authenticate: %s\n", param);

  if (strncmp(param, "Digest ", strlen("Digest ")) != 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Unsupported authentication method: %s\n", param);

      return -1;
    }

  auth = strdup(param);
  if (!auth)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Out of memory for WWW-Authenticate header copy\n");

      return -1;
    }

  token = strchr(auth, ' ');
  token++;

  token = strtok_r(token, " =", &ptr);
  while (token)
    {
      if (strcmp(token, "realm") == 0)
	{
	  token = strtok_r(NULL, "=\"", &ptr);
	  if (!token)
	    break;

	  rs->realm = strdup(token);
	}
      else if (strcmp(token, "nonce") == 0)
	{
	  token = strtok_r(NULL, "=\"", &ptr);
	  if (!token)
	    break;

	  rs->nonce = strdup(token);
	}

      token = strtok_r(NULL, " =", &ptr);
    }

  free(auth);

  if (!rs->realm || !rs->nonce)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not find realm/nonce in WWW-Authenticate header\n");

      if (rs->realm)
	{
	  free(rs->realm);
	  rs->realm = NULL;
	}

      if (rs->nonce)
	{
	  free(rs->nonce);
	  rs->nonce = NULL;
	}

      return -1;
    }

  DPRINTF(E_DBG, L_AIRPLAY, "Found realm: [%s], nonce: [%s]\n", rs->realm, rs->nonce);

  return 0;
}
*/
static int
request_headers_add(struct evrtsp_request *req, struct airplay_session *rs, enum evrtsp_cmd_type req_method)
{
  char buf[64];
  const char *method;
  const char *url;
  const char *user_agent;
  int ret;

  method = evrtsp_method(req_method);

  snprintf(buf, sizeof(buf), "%d", rs->cseq);
  evrtsp_add_header(req->output_headers, "CSeq", buf);

  rs->cseq++;

  user_agent = cfg_getstr(cfg_getsec(cfg, "general"), "user_agent");
  evrtsp_add_header(req->output_headers, "User-Agent", user_agent);

  /* Add Authorization header */
  url = (req_method == EVRTSP_REQ_OPTIONS) ? "*" : rs->session_url;

  ret = request_header_auth_add(req, rs, method, url);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not add Authorization header\n");

      if (ret == -2)
	rs->state = AIRPLAY_STATE_AUTH;

      return -1;
    }

  snprintf(buf, sizeof(buf), "%" PRIX64, libhash);
  evrtsp_add_header(req->output_headers, "Client-Instance", buf);
  evrtsp_add_header(req->output_headers, "DACP-ID", buf);

  // We set Active-Remote as 32 bit unsigned decimal, as at least my device
  // can't handle any larger. Must be aligned with volume_byactiveremote().
  snprintf(buf, sizeof(buf), "%" PRIu32, (uint32_t)rs->device_id);
  evrtsp_add_header(req->output_headers, "Active-Remote", buf);

#if AIRPLAY_USE_STREAMID
  evrtsp_add_header(req->output_headers, "X-Apple-StreamID", "1");
#endif

  /* Content-Length added automatically by evrtsp */

  return 0;
}

static void
metadata_rtptimes_get(uint32_t *start, uint32_t *display, uint32_t *pos, uint32_t *end, struct airplay_master_session *rms, struct output_metadata *metadata)
{
  struct rtp_session *rtp_session = rms->rtp_session;
  // All the calculations with long ints to avoid surprises
  int64_t sample_rate;
  int64_t diff_ms;
  int64_t elapsed_ms;
  int64_t elapsed_samples;
  int64_t len_samples;

  sample_rate = rtp_session->quality.sample_rate;

  // First calculate the rtptime that streaming of this item started:
  // - at time metadata->pts the elapsed time was metadata->pos_ms
  // - the time is now rms->cur_stamp.ts and the position is rms->cur_stamp.pos
  // -> time since item started is elapsed_ms = metadata->pos_ms + (rms->cur_stamp.ts - metadata->pts)
  // -> start must then be start = rms->cur_stamp.pos - elapsed_ms * sample_rate;
  diff_ms         = (rms->cur_stamp.ts.tv_sec - metadata->pts.tv_sec) * 1000L + (rms->cur_stamp.ts.tv_nsec - metadata->pts.tv_nsec) / 1000000L;
  elapsed_ms      = (int64_t)metadata->pos_ms + diff_ms;
  elapsed_samples = elapsed_ms * sample_rate / 1000;
  *start          = rms->cur_stamp.pos - elapsed_samples;

/*  DPRINTF(E_DBG, L_AIRPLAY, "pos_ms=%u, len_ms=%u, startup=%d, metadata.pts=%ld.%09ld, player.ts=%ld.%09ld, diff_ms=%" PRIi64 ", elapsed_ms=%" PRIi64 "\n",
    metadata->pos_ms, metadata->len_ms, metadata->startup, metadata->pts.tv_sec, metadata->pts.tv_nsec, rms->cur_stamp.ts.tv_sec, rms->cur_stamp.ts.tv_nsec, diff_ms, elapsed_ms);
*/
  // Here's the deal with progress values:
  // - display is always start minus a delay
  //    -> delay x1 if streaming is starting for this device (joining or not)
  //    -> delay x2 if stream is switching to a new song
  //    TODO what if we are just sending a keep_alive?
  // - pos is the RTP time of the first sample for this song for this device
  //    -> start of song
  //    -> start of song + offset if device is joining in the middle of a song,
  //       or getting out of a pause or seeking
  // - end is the RTP time of the last sample for this song
  len_samples     = (int64_t)metadata->len_ms * sample_rate / 1000;
  *display        = metadata->startup ? *start - AIRPLAY_MD_DELAY_STARTUP : *start - AIRPLAY_MD_DELAY_SWITCH;
  *pos            = MAX(rms->cur_stamp.pos, *start);
  *end            = len_samples ? *start + len_samples : *pos;

  DPRINTF(E_SPAM, L_AIRPLAY, "start=%u, display=%u, pos=%u, end=%u, rtp_session.pos=%u, cur_stamp.pos=%u\n",
    *start, *display, *pos, *end, rtp_session->pos, rms->cur_stamp.pos);
}

static int
rtpinfo_header_add(struct evrtsp_request *req, struct airplay_session *rs, struct output_metadata *metadata)
{
  uint32_t start;
  uint32_t display;
  uint32_t pos;
  uint32_t end;
  char rtpinfo[32];
  int ret;

  metadata_rtptimes_get(&start, &display, &pos, &end, rs->master_session, metadata);

  ret = snprintf(rtpinfo, sizeof(rtpinfo), "rtptime=%u", start);
  if ((ret < 0) || (ret >= sizeof(rtpinfo)))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "RTP-Info too big for buffer while sending metadata\n");
      return -1;
    }

  evrtsp_add_header(req->output_headers, "RTP-Info", rtpinfo);
  return 0;
}

static int
rtsp_cipher(struct evbuffer *outbuf, struct evbuffer *inbuf, void *arg, int encrypt)
{
  struct airplay_session *rs = arg;
  uint8_t *in;
  size_t in_len;
  uint8_t *out = NULL;
  size_t out_len = 0;
  ssize_t processed;

  in = evbuffer_pullup(inbuf, -1);
  in_len = evbuffer_get_length(inbuf);

  if (encrypt)
    {
#if AIRPLAY_DUMP_TRAFFIC
      if (in_len < 4096)
	DHEXDUMP(E_DBG, L_AIRPLAY, in, in_len, "Encrypting outgoing request\n");
      else
	DPRINTF(E_DBG, L_AIRPLAY, "Encrypting outgoing request (size %zu)\n", in_len);
#endif

      processed = pair_encrypt(&out, &out_len, in, in_len, rs->control_cipher_ctx);
      if (processed < 0)
	goto error;
    }
  else
    {
      processed = pair_decrypt(&out, &out_len, in, in_len, rs->control_cipher_ctx);
      if (processed < 0)
	goto error;

#if AIRPLAY_DUMP_TRAFFIC
      if (out_len < 4096)
	DHEXDUMP(E_DBG, L_AIRPLAY, out, out_len, "Decrypted incoming response\n");
      else
	DPRINTF(E_DBG, L_AIRPLAY, "Decrypted incoming response (size %zu)\n", out_len);
#endif
    }

  evbuffer_drain(inbuf, processed);
  evbuffer_add(outbuf, out, out_len);

  return 0;

 error:
  DPRINTF(E_LOG, L_AIRPLAY, "Error while %s (len=%zu): %s\n", encrypt ? "encrypting" : "decrypting", in_len, pair_cipher_errmsg(rs->control_cipher_ctx));

  return -1;
}


/* ------------------------------ Session handling -------------------------- */

// Maps our internal state to the generic output state and then makes a callback
// to the player to tell that state
static void
session_status(struct airplay_session *rs)
{
  enum output_device_state state;

  switch (rs->state)
    {
      case AIRPLAY_STATE_AUTH:
	state = OUTPUT_STATE_PASSWORD;
	break;
      case AIRPLAY_STATE_FAILED:
	state = OUTPUT_STATE_FAILED;
	break;
      case AIRPLAY_STATE_STOPPED:
	state = OUTPUT_STATE_STOPPED;
	break;
      case AIRPLAY_STATE_INFO ... AIRPLAY_STATE_RECORD:
	state = OUTPUT_STATE_STARTUP;
	break;
      case AIRPLAY_STATE_CONNECTED:
	state = OUTPUT_STATE_CONNECTED;
	break;
      case AIRPLAY_STATE_STREAMING:
	state = OUTPUT_STATE_STREAMING;
	break;
      case AIRPLAY_STATE_TEARDOWN:
	DPRINTF(E_LOG, L_AIRPLAY, "Bug! session_status() called with transitional state (TEARDOWN)\n");
	state = OUTPUT_STATE_STOPPED;
	break;
      default:
	DPRINTF(E_LOG, L_AIRPLAY, "Bug! Unhandled state in session_status(): %d\n", rs->state);
	state = OUTPUT_STATE_FAILED;
    }

  outputs_cb(rs->callback_id, rs->device_id, state);
  rs->callback_id = -1;
}

static void
master_session_free(struct airplay_master_session *rms)
{
  if (!rms)
    return;

  outputs_quality_unsubscribe(&rms->rtp_session->quality);
  rtp_session_free(rms->rtp_session);

  transcode_encode_cleanup(&rms->encode_ctx);

  if (rms->input_buffer)
    evbuffer_free(rms->input_buffer);
  if (rms->encoded_buffer)
    evbuffer_free(rms->encoded_buffer);

  free(rms->rawbuf);
  free(rms);
}

static void
master_session_cleanup(struct airplay_master_session *rms)
{
  struct airplay_master_session *s;
  struct airplay_session *rs;

  // First check if any other session is using the master session
  for (rs = airplay_sessions; rs; rs=rs->next)
    {
      if (rs->master_session == rms)
	return;
    }

  if (rms == airplay_master_sessions)
    airplay_master_sessions = airplay_master_sessions->next;
  else
    {
      for (s = airplay_master_sessions; s && (s->next != rms); s = s->next)
	; /* EMPTY */

      if (!s)
	DPRINTF(E_WARN, L_AIRPLAY, "WARNING: struct airplay_master_session not found in list; BUG!\n");
      else
	s->next = rms->next;
    }

  master_session_free(rms);
}

static struct airplay_master_session *
master_session_make(struct media_quality *quality)
{
  struct airplay_master_session *rms;
  struct decode_ctx *decode_ctx;
  int ret;

  // First check if we already have a suitable session
  for (rms = airplay_master_sessions; rms; rms = rms->next)
    {
      if (quality_is_equal(quality, &rms->rtp_session->quality))
	return rms;
    }

  // Let's create a master session
  ret = outputs_quality_subscribe(quality);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not subscribe to required audio quality (%d/%d/%d)\n", quality->sample_rate, quality->bits_per_sample, quality->channels);
      return NULL;
    }

  CHECK_NULL(L_AIRPLAY, rms = calloc(1, sizeof(struct airplay_master_session)));

  rms->rtp_session = rtp_session_new(quality, AIRPLAY_PACKET_BUFFER_SIZE, 0);
  if (!rms->rtp_session)
    {
      goto error;
    }

  decode_ctx = transcode_decode_setup_raw(XCODE_PCM16, quality);
  if (!decode_ctx)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not create decoding context\n");
      goto error;
    }

  rms->encode_ctx = transcode_encode_setup(XCODE_ALAC, quality, decode_ctx, NULL, 0, 0);
  transcode_decode_cleanup(&decode_ctx);
  if (!rms->encode_ctx)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Will not be able to stream AirPlay 2, ffmpeg has no ALAC encoder\n");
      goto error;
    }

  rms->quality = *quality;
  rms->samples_per_packet = AIRPLAY_SAMPLES_PER_PACKET;
  rms->rawbuf_size = STOB(rms->samples_per_packet, quality->bits_per_sample, quality->channels);
  rms->output_buffer_samples = OUTPUTS_BUFFER_DURATION * quality->sample_rate;

  CHECK_NULL(L_AIRPLAY, rms->rawbuf = malloc(rms->rawbuf_size));
  CHECK_NULL(L_AIRPLAY, rms->input_buffer = evbuffer_new());
  CHECK_NULL(L_AIRPLAY, rms->encoded_buffer = evbuffer_new());

  rms->next = airplay_master_sessions;
  airplay_master_sessions = rms;

  return rms;

 error:
  master_session_free(rms);
  return NULL;
}

static void
session_free(struct airplay_session *rs)
{
  if (!rs)
    return;

  if (rs->master_session)
    master_session_cleanup(rs->master_session);

  if (rs->ctrl)
    {
      evrtsp_connection_set_closecb(rs->ctrl, NULL, NULL);
      evrtsp_connection_free(rs->ctrl);
    }

  if (rs->deferredev)
    event_free(rs->deferredev);

  if (rs->server_fd >= 0)
    close(rs->server_fd);

  chacha_close(rs->packet_cipher_hd);

  pair_setup_free(rs->pair_setup_ctx);
  pair_verify_free(rs->pair_verify_ctx);
  pair_cipher_free(rs->control_cipher_ctx);

  free(rs->local_address);
  free(rs->realm);
  free(rs->nonce);
  free(rs->address);
  free(rs->devname);

  free(rs);
}

static void
session_cleanup(struct airplay_session *rs)
{
  struct airplay_session *s;

  if (rs == airplay_sessions)
    airplay_sessions = airplay_sessions->next;
  else
    {
      for (s = airplay_sessions; s && (s->next != rs); s = s->next)
	; /* EMPTY */

      if (!s)
	DPRINTF(E_WARN, L_AIRPLAY, "WARNING: struct airplay_session not found in list; BUG!\n");
      else
	s->next = rs->next;
    }

  outputs_device_session_remove(rs->device_id);

  session_free(rs);
}

static void
session_failure(struct airplay_session *rs)
{
  /* Session failed, let our user know */
  if (rs->state != AIRPLAY_STATE_AUTH)
    rs->state = AIRPLAY_STATE_FAILED;

  session_status(rs);

  session_cleanup(rs);
}

static void
deferred_session_failure_cb(int fd, short what, void *arg)
{
  struct airplay_session *rs = arg;

  DPRINTF(E_DBG, L_AIRPLAY, "Cleaning up failed session (deferred) on device '%s'\n", rs->devname);
  session_failure(rs);
}

static void
deferred_session_failure(struct airplay_session *rs)
{
  struct timeval tv;

  if (rs->state != AIRPLAY_STATE_AUTH)
    rs->state = AIRPLAY_STATE_FAILED;

  evutil_timerclear(&tv);
  evtimer_add(rs->deferredev, &tv);
}

static void
rtsp_close_cb(struct evrtsp_connection *evcon, void *arg)
{
  struct airplay_session *rs = arg;

  DPRINTF(E_LOG, L_AIRPLAY, "Device '%s' closed RTSP connection\n", rs->devname);

  deferred_session_failure(rs);
}

static void
session_success(struct airplay_session *rs)
{
  session_status(rs);

  session_cleanup(rs);
}

static void
session_connected(struct airplay_session *rs)
{
  rs->state = AIRPLAY_STATE_CONNECTED;

  session_status(rs);
}

static void
session_pair_success(struct airplay_session *rs)
{
  if (rs->next_seq != AIRPLAY_SEQ_CONTINUE)
    {
      sequence_start(rs->next_seq, rs, NULL, "pair_success");
      rs->next_seq = AIRPLAY_SEQ_CONTINUE;
      return;
    }

  session_success(rs);
}

static int
session_connection_setup(struct airplay_session *rs, struct output_device *rd, int family)
{
  char *address;
  char *intf;
  unsigned short port;
  int ret;

  rs->naddr.ss.ss_family = family;

  switch (family)
    {
      case AF_INET:
	if (!rd->v4_address)
	  return -1;

	address = rd->v4_address;
	port = rd->v4_port;


	ret = inet_pton(AF_INET, address, &rs->naddr.sin.sin_addr);
	break;

      case AF_INET6:
	if (!rd->v6_address)
	  return -1;

	address = rd->v6_address;
	port = rd->v6_port;

	intf = strchr(address, '%');
	if (intf)
	  *intf = '\0';

	ret = inet_pton(AF_INET6, address, &rs->naddr.sin6.sin6_addr);

	if (intf)
	  {
	    *intf = '%';

	    intf++;

	    rs->naddr.sin6.sin6_scope_id = if_nametoindex(intf);
	    if (rs->naddr.sin6.sin6_scope_id == 0)
	      {
		DPRINTF(E_LOG, L_AIRPLAY, "Could not find interface %s\n", intf);

		ret = -1;
		break;
	      }
	  }

	break;

      default:
	return -1;
    }

  if (ret <= 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Device '%s' has invalid address (%s) for %s\n", rd->name, address, (family == AF_INET) ? "ipv4" : "ipv6");
      return -1;
    }

  rs->ctrl = evrtsp_connection_new(address, port);
  if (!rs->ctrl)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not create control connection to '%s' (%s)\n", rd->name, address);
      return -1;
    }

  evrtsp_connection_set_base(rs->ctrl, evbase_player);

  rs->address = strdup(address);
  rs->family = family;

  return 0;
}

static int
session_cipher_setup(struct airplay_session *rs, const uint8_t *key, size_t key_len)
{
  struct pair_cipher_context *control_cipher_ctx = NULL;
  gcry_cipher_hd_t packet_cipher_hd = NULL;

  // For transient pairing the key_len will be 64 bytes, and rs->shared_secret is 32 bytes
  if (key_len < AIRPLAY_AUDIO_KEY_LEN || key_len > sizeof(rs->shared_secret))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Ciphering setup error: Unexpected key length (%zu)\n", key_len);
      goto error;
    }

  rs->shared_secret_len = key_len;
  memcpy(rs->shared_secret, key, key_len);

  control_cipher_ctx = pair_cipher_new(rs->pair_type, 0, key, key_len);
  if (!control_cipher_ctx)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not create control ciphering context\n");
      goto error;
    }

  packet_cipher_hd = chacha_open(rs->shared_secret, AIRPLAY_AUDIO_KEY_LEN);
  if (!packet_cipher_hd)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not create packet ciphering handle\n");
      goto error;
    }

  DPRINTF(E_DBG, L_AIRPLAY, "Ciphering setup of '%s' completed succesfully, now using encrypted mode\n", rs->devname);

  rs->state = AIRPLAY_STATE_ENCRYPTED;
  rs->control_cipher_ctx = control_cipher_ctx;
  rs->packet_cipher_hd = packet_cipher_hd;

  evrtsp_connection_set_ciphercb(rs->ctrl, rtsp_cipher, rs);

  return 0;

 error:
  pair_cipher_free(control_cipher_ctx);
  chacha_close(packet_cipher_hd);
  return -1;
}

static int
session_ids_set(struct airplay_session *rs)
{
  char *address = NULL;
  char *intf;
  unsigned short port;
  int family;
  int ret;

  // Determine local address, needed for session URL
  evrtsp_connection_get_local_address(rs->ctrl, &address, &port, &family);
  if (!address || (port == 0))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not determine local address\n");
      goto error;
    }

  intf = strchr(address, '%');
  if (intf)
    {
      *intf = '\0';
      intf++;
    }

  DPRINTF(E_DBG, L_AIRPLAY, "Local address: %s (LL: %s) port %d\n", address, (intf) ? intf : "no", port);

  // Session UUID, ID and session URL
  uuid_make(rs->session_uuid);

  gcry_randomize(&rs->session_id, sizeof(rs->session_id), GCRY_STRONG_RANDOM);

  if (family == AF_INET)
    ret = snprintf(rs->session_url, sizeof(rs->session_url), "rtsp://%s/%u", address, rs->session_id);
  else
    ret = snprintf(rs->session_url, sizeof(rs->session_url), "rtsp://[%s]/%u", address, rs->session_id);
  if ((ret < 0) || (ret >= sizeof(rs->session_url)))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Session URL length exceeds 127 characters\n");
      goto error;
    }

  rs->local_address = address;
  return 0;

 error:
  free(address);
  return -1;
}

static struct airplay_session *
session_find_by_address(union net_sockaddr *peer_addr)
{
  struct airplay_session *rs;
  uint32_t *addr_ptr;
  int family = peer_addr->sa.sa_family;

  for (rs = airplay_sessions; rs; rs = rs->next)
    {
      if (family == rs->family)
	{
	  if (family == AF_INET && peer_addr->sin.sin_addr.s_addr == rs->naddr.sin.sin_addr.s_addr)
	    break;

	  if (family == AF_INET6 && IN6_ARE_ADDR_EQUAL(&peer_addr->sin6.sin6_addr, &rs->naddr.sin6.sin6_addr))
	    break;
	}
      else if (family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&peer_addr->sin6.sin6_addr))
	{
	  // ipv4 mapped to ipv6 consists of 16 bytes/4 words: 0x00000000 0x00000000 0x0000ffff 0x[IPv4]
	  addr_ptr = (uint32_t *)(&peer_addr->sin6.sin6_addr);
	  if (addr_ptr[3] == rs->naddr.sin.sin_addr.s_addr)
	    break;
	}
    }

  return rs;
}

static struct airplay_session *
session_make(struct output_device *rd, int callback_id)
{
  struct airplay_session *rs;
  struct airplay_extra *re;
  int ret;

  re = rd->extra_device_info;


  CHECK_NULL(L_AIRPLAY, rs = calloc(1, sizeof(struct airplay_session)));
  CHECK_NULL(L_AIRPLAY, rs->deferredev = evtimer_new(evbase_player, deferred_session_failure_cb, rs));

  rs->devname = strdup(rd->name);
  rs->volume = rd->volume;

  rs->state = AIRPLAY_STATE_STOPPED;
  rs->reqs_in_flight = 0;
  rs->cseq = 1;

  rs->device_id = rd->id;
  rs->callback_id = callback_id;

  rs->server_fd = -1;

  rs->password = rd->password;

  rs->supports_auth_setup = re->supports_auth_setup;
  rs->wanted_metadata = re->wanted_metadata;

  rs->next_seq = AIRPLAY_SEQ_CONTINUE;

  rs->timing_svc = &airplay_timing_svc;
  rs->control_svc = &airplay_control_svc;

  ret = session_connection_setup(rs, rd, AF_INET6);
  if (ret < 0)
    {
      ret = session_connection_setup(rs, rd, AF_INET);
      if (ret < 0)
	goto error;
    }

  rs->master_session = master_session_make(&rd->quality);
  if (!rs->master_session)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not attach a master session for device '%s'\n", rd->name);
      goto error;
    }

  // Attach to list of sessions
  rs->next = airplay_sessions;
  airplay_sessions = rs;

  // rs is now the official device session
  outputs_device_session_add(rd->id, rs);

  return rs;

 error:
  session_free(rs);

  return NULL;
}


/* ----------------------------- Metadata handling -------------------------- */

static void
airplay_metadata_free(struct airplay_metadata *rmd)
{
  if (!rmd)
    return;

  if (rmd->metadata)
    evbuffer_free(rmd->metadata);
  if (rmd->artwork)
    evbuffer_free(rmd->artwork);

  free(rmd);
}

static void
airplay_metadata_purge(void)
{
  if (!airplay_cur_metadata)
    return;

  airplay_metadata_free(airplay_cur_metadata->priv);
  free(airplay_cur_metadata);
  airplay_cur_metadata = NULL;
}

// *** Thread: worker ***
static void *
airplay_metadata_prepare(struct output_metadata *metadata)
{
  struct db_queue_item *queue_item;
  struct airplay_metadata *rmd;
  struct evbuffer *tmp;
  int ret;

  queue_item = db_queue_fetch_byitemid(metadata->item_id);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not fetch queue item\n");
      return NULL;
    }

  CHECK_NULL(L_AIRPLAY, rmd = calloc(1, sizeof(struct airplay_metadata)));
  CHECK_NULL(L_AIRPLAY, rmd->artwork = evbuffer_new());
  CHECK_NULL(L_AIRPLAY, rmd->metadata = evbuffer_new());
  CHECK_NULL(L_AIRPLAY, tmp = evbuffer_new());

  ret = artwork_get_item(rmd->artwork, queue_item->file_id, ART_DEFAULT_WIDTH, ART_DEFAULT_HEIGHT, 0);
  if (ret < 0)
    {
      DPRINTF(E_INFO, L_AIRPLAY, "Failed to retrieve artwork for file '%s'; no artwork will be sent\n", queue_item->path);
      evbuffer_free(rmd->artwork);
      rmd->artwork = NULL;
    }

  rmd->artwork_fmt = ret;

  ret = dmap_encode_queue_metadata(rmd->metadata, tmp, queue_item);
  evbuffer_free(tmp);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not encode file metadata; metadata will not be sent\n");
      airplay_metadata_free(rmd);
      return NULL;
    }

  return rmd;
}

static int
airplay_metadata_send_generic(struct airplay_session *rs, struct output_metadata *metadata, bool only_progress)
{
  struct airplay_metadata *rmd = metadata->priv;

  if (rs->wanted_metadata & AIRPLAY_MD_WANTS_PROGRESS)
    sequence_start(AIRPLAY_SEQ_SEND_PROGRESS, rs, metadata, "SET_PARAMETER (progress)");

  if (!only_progress && (rs->wanted_metadata & AIRPLAY_MD_WANTS_TEXT))
    sequence_start(AIRPLAY_SEQ_SEND_TEXT, rs, metadata, "SET_PARAMETER (text)");

  if (!only_progress && (rs->wanted_metadata & AIRPLAY_MD_WANTS_ARTWORK) && rmd->artwork)
    sequence_start(AIRPLAY_SEQ_SEND_ARTWORK, rs, metadata, "SET_PARAMETER (artwork)");

  return 0;
}

static int
airplay_metadata_startup_send(struct airplay_session *rs)
{
  if (!rs->wanted_metadata || !airplay_cur_metadata)
    return 0;

  airplay_cur_metadata->startup = true;

  return airplay_metadata_send_generic(rs, airplay_cur_metadata, false);
}

static void
airplay_metadata_keep_alive_send(struct airplay_session *rs)
{
  sequence_start(AIRPLAY_SEQ_FEEDBACK, rs, NULL, "keep_alive");
}

static void
airplay_metadata_send(struct output_metadata *metadata)
{
  struct airplay_session *rs;
  struct airplay_session *next;
  int ret;

  for (rs = airplay_sessions; rs; rs = next)
    {
      next = rs->next;

      if (!(rs->state & AIRPLAY_STATE_F_CONNECTED) || !rs->wanted_metadata)
	continue;

      ret = airplay_metadata_send_generic(rs, metadata, false);
      if (ret < 0)
	{
	  session_failure(rs);
	  continue;
	}
    }

  // Replace current metadata with the new stuff
  airplay_metadata_purge();
  airplay_cur_metadata = metadata;
}


/* ------------------------------ Volume handling --------------------------- */

static int
volume_max_get(const char *name)
{
  int max_volume = AIRPLAY_CONFIG_MAX_VOLUME;
  cfg_t *airplay;

  airplay = cfg_gettsec(cfg, "airplay", name);
  if (airplay)
    max_volume = cfg_getint(airplay, "max_volume");

  if ((max_volume < 1) || (max_volume > AIRPLAY_CONFIG_MAX_VOLUME))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Config has bad max_volume (%d) for device '%s', using default instead\n", max_volume, name);
      return AIRPLAY_CONFIG_MAX_VOLUME;
    }

  return max_volume;
}

static float
airplay_volume_from_pct(int volume, const char *name)
{
  float airplay_volume;
  int max_volume;

  max_volume = volume_max_get(name);

  /* RAOP volume
   *  -144.0 is off (not really used since we have no concept of muted/off)
   *  0 - 100 maps to -30.0 - 0 (if no max_volume set)
   */
  if (volume >= 0 && volume <= 100)
    airplay_volume = -30.0 + ((float)max_volume * (float)volume * 30.0) / (100.0 * AIRPLAY_CONFIG_MAX_VOLUME);
  else
    airplay_volume = -144.0;

  return airplay_volume;
}

static int
airplay_volume_to_pct(struct output_device *rd, const char *volstr)
{
  float airplay_volume;
  float volume;
  int max_volume;

  airplay_volume = atof(volstr);

  if ((airplay_volume == 0.0 && volstr[0] != '0') || airplay_volume > 0.0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "AirPlay device volume is invalid: '%s'\n", volstr);
      return -1;
    }

  if (airplay_volume <= -30.0)
    {
      return 0; // -144.0 is muted
    }

  max_volume = volume_max_get(rd->name);

/*
  This is an attempt at scaling the input volume that didn't really work for all
  speakers (e.g. my Sony), but I'm leaving it here in case it should be a config
  option some time

  // If the input volume is -25 and we are playing at -20, then we only want the
  // resulting volume to be -25 if there is no volume scaling. If the scaling is
  // set to say 20% then we want the resulting volume to be -21, i.e. 20% of the
  // change. Expressed as an equation:
  //   a_r = a_0 + m/M * (a_i - a_0)     - where a_0 is the current airplay volume, a_i is the input and a_r is the scaled result
  //
  // Since current volume (rd->volume) is measured on the 0-100 scale, and the
  // result of this func should also be on that scale, we have the following two
  // relationships (the first is also found in _from_pct() above):
  //   a_0 = -30 + m/M * 30/100 * v_0    - where v_0 is rd->volume
  //   v_r = M/m * 100 * (1 + a_r / 30)  - converts a_r to v_r which is [0-100]
  //
  // Solving these three equations gives this:
  volume_base = 100.0 * (1.0 + airplay_volume / 30.0);
  volume = (float)rd->volume * (1.0 - (float)max_volume/AIRPLAY_CONFIG_MAX_VOLUME) + volume_base;

*/
  // RAOP volume: -144.0 is off, -30.0 - 0 scaled by max_volume maps to 0 - 100
  volume = (100.0 * (airplay_volume / 30.0 + 1.0) * AIRPLAY_CONFIG_MAX_VOLUME / (float)max_volume);
  return MAX(0, MIN(100, (int)volume));
}

/* Volume in [0 - 100] */
static int
airplay_set_volume_one(struct output_device *device, int callback_id)
{
  struct airplay_session *rs = device->session;

  if (!rs || !(rs->state & AIRPLAY_STATE_F_CONNECTED))
    return 0;

  rs->volume = device->volume;
  rs->callback_id = callback_id;

  sequence_start(AIRPLAY_SEQ_SEND_VOLUME, rs, NULL, "set_volume_one");

  return 1;
}

static void
airplay_keep_alive_timer_cb(int fd, short what, void *arg)
{
  struct airplay_session *rs;

  if (!airplay_sessions)
    {
      event_del(keep_alive_timer);
      return;
    }

  for (rs = airplay_sessions; rs; rs = rs->next)
    {
      if (!(rs->state & AIRPLAY_STATE_F_CONNECTED))
	continue;

      airplay_metadata_keep_alive_send(rs);
    }

  evtimer_add(keep_alive_timer, &keep_alive_tv);
}


/* -------------------- Creation and sending of RTP packets  ---------------- */

static int
packet_encrypt(uint8_t **out, size_t *out_len, struct rtp_packet *pkt, struct airplay_session *rs)
{
  uint8_t authtag[16];
  uint8_t nonce[12] = { 0 };
  int nonce_offset = 4;
  uint8_t *write_ptr;
  int ret;

  // Alloc so authtag and nonce can be appended
  *out_len = pkt->data_len + sizeof(authtag) + sizeof(nonce) - nonce_offset;
  *out = malloc(*out_len);
  write_ptr = *out;

  // Using seqnum as nonce not very secure, but means that when we resend
  // packets they will be identical to the original
  memcpy(nonce + nonce_offset, &pkt->seqnum, sizeof(pkt->seqnum));

  // The RTP header is not encrypted
  memcpy(write_ptr, pkt->header, pkt->header_len);
  write_ptr = *out + pkt->header_len;

  // Timestamp and SSRC are used as AAD = pkt->header + 4, len 8
  ret = chacha_encrypt(write_ptr, pkt->payload, pkt->payload_len, pkt->header + 4, 8, authtag, sizeof(authtag), nonce, sizeof(nonce), rs->packet_cipher_hd);
  if (ret < 0)
    {
      free(*out);
      return -1;
    }

  write_ptr += pkt->payload_len;
  memcpy(write_ptr, authtag, sizeof(authtag));
  write_ptr += sizeof(authtag);
  memcpy(write_ptr, nonce + nonce_offset, sizeof(nonce) - nonce_offset);

  return 0;
}

static int
packet_send(struct airplay_session *rs, struct rtp_packet *pkt)
{
  uint8_t *encrypted;
  size_t encrypted_len;
  int ret;

  if (!rs)
    return -1;

  ret = packet_encrypt(&encrypted, &encrypted_len, pkt, rs);
  if (ret < 0)
    return -1;

  ret = send(rs->server_fd, encrypted, encrypted_len, 0);
  free(encrypted);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Send error for '%s': %s\n", rs->devname, strerror(errno));

      // Can't free it right away, it would make the ->next in the calling
      // master_session and session loops invalid
      deferred_session_failure(rs);
      return -1;
    }
  else if (ret != encrypted_len)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Partial send (%d) for '%s'\n", ret, rs->devname);
      return -1;
    }

/*  DPRINTF(E_DBG, L_AIRPLAY, "RTP PACKET seqnum %u, rtptime %u, payload 0x%x, pktbuf_s %zu\n",
    rs->master_session->rtp_session->seqnum,
    rs->master_session->rtp_session->pos,
    pkt->header[1],
    rs->master_session->rtp_session->pktbuf_len
    );
*/
  return 0;
}

static void
control_packet_send(struct airplay_session *rs, struct rtp_packet *pkt)
{
  socklen_t addrlen;
  int ret;

  switch (rs->family)
    {
      case AF_INET:
	rs->naddr.sin.sin_port = htons(rs->control_port);
	addrlen = sizeof(rs->naddr.sin);
	break;

      case AF_INET6:
	rs->naddr.sin6.sin6_port = htons(rs->control_port);
	addrlen = sizeof(rs->naddr.sin6);
	break;

      default:
	DPRINTF(E_WARN, L_AIRPLAY, "Unknown family %d\n", rs->family);
	return;
    }

  ret = sendto(rs->control_svc->fd, pkt->data, pkt->data_len, 0, &rs->naddr.sa, addrlen);
  if (ret < 0)
    DPRINTF(E_LOG, L_AIRPLAY, "Could not send playback sync to device '%s': %s\n", rs->devname, strerror(errno));
}

static void
packets_resend(struct airplay_session *rs, uint16_t seqnum, int len)
{
  struct rtp_session *rtp_session;
  struct rtp_packet *pkt;
  uint16_t s;
  int i;
  bool pkt_missing = false;

  rtp_session = rs->master_session->rtp_session;

  DPRINTF(E_DBG, L_AIRPLAY, "Got retransmit request from '%s': seqnum %" PRIu16 " (len %d), next RTP session seqnum %" PRIu16 " (len %zu)\n",
    rs->devname, seqnum, len, rtp_session->seqnum, rtp_session->pktbuf_len);

  // Note that seqnum may wrap around, so we don't use it for counting
  for (i = 0, s = seqnum; i < len; i++, s++)
    {
      pkt = rtp_packet_get(rtp_session, s);
      if (pkt)
	packet_send(rs, pkt);
      else
	pkt_missing = true;
    }

  if (pkt_missing)
    DPRINTF(E_WARN, L_AIRPLAY, "Device '%s' retransmit request for seqnum %" PRIu16 " (len %d) is outside buffer range (next seqnum %" PRIu16 ", len %zu)\n",
      rs->devname, seqnum, len, rtp_session->seqnum, rtp_session->pktbuf_len);
}

static int
packets_send(struct airplay_master_session *rms)
{
  struct rtp_packet *pkt;
  struct airplay_session *rs;
  int len;

  len = alac_encode(rms->encoded_buffer, rms->encode_ctx, rms->rawbuf, rms->rawbuf_size, rms->samples_per_packet, &rms->quality);
  if (len < 0)
    return -1;

  pkt = rtp_packet_next(rms->rtp_session, len, rms->samples_per_packet, AIRPLAY_RTP_PAYLOADTYPE, 0);

  evbuffer_remove(rms->encoded_buffer, pkt->payload, pkt->payload_len);

  for (rs = airplay_sessions; rs; rs = rs->next)
    {
      if (rs->master_session != rms)
	continue;

      // Device just joined
      if (rs->state == AIRPLAY_STATE_CONNECTED)
	{
	  pkt->header[1] = (1 << 7) | AIRPLAY_RTP_PAYLOADTYPE;
	  packet_send(rs, pkt);
	}
      else if (rs->state == AIRPLAY_STATE_STREAMING)
	{
	  pkt->header[1] = AIRPLAY_RTP_PAYLOADTYPE;
	  packet_send(rs, pkt);
	}
    }

  // Commits packet to retransmit buffer, and prepares the session for the next packet
  rtp_packet_commit(rms->rtp_session, pkt);

  return 0;
}

// Overview of rtptimes as they should be when starting a stream, and assuming
// the first rtptime (pos) is 88200:
//   sync pkt:  cur_pos = 0, rtptime = 88200
//   audio pkt: rtptime = 88200
//   RECORD:    rtptime = 88200
//   SET_PARAMETER text/artwork:
//              rtptime = 88200
//   SET_PARAMETER progress:
//              progress = 72840/~88200/[len]
static inline void
timestamp_set(struct airplay_master_session *rms, struct timespec ts)
{
  // The last write from the player had a timestamp which has been passed to
  // this function as ts. This is the player clock, which is more precise than
  // the actual clock because it gives us a calculated time reference, which is
  // independent of how busy the thread is. We save that here, we need this for
  // reference when sending sync packets and progress.
  rms->cur_stamp.ts = ts;

  // So what rtptime should be playing, i.e. coming out of the speaker, at time
  // ts (which is normally "now")? Let's calculate by example:
  //   - we started playback with a rtptime (pos) of X
  //   - up until time ts we have received a 1000 samples from the player
  //   - rms->output_buffer_samples is configured to 400 samples
  //   -> we should be playing rtptime X + 600
  //
  // So how do we measure samples received from player? We know that from the
  // pos, which says how much has been sent to the device, and from rms->input_buffer,
  // which is the unsent stuff being buffered:
  //   - received = (pos - X) + rms->input_buffer_samples
  //
  // This means the rtptime is computed as:
  //   - rtptime = X + received - rms->output_buffer_samples
  //   -> rtptime = X + (pos - X) + rms->input_buffer_samples - rms->out_buffer_samples
  //   -> rtptime = pos + rms->input_buffer_samples - rms->output_buffer_samples
  rms->cur_stamp.pos = rms->rtp_session->pos + rms->input_buffer_samples - rms->output_buffer_samples;
}

static void
packets_sync_send(struct airplay_master_session *rms)
{
  struct rtp_packet *sync_pkt;
  struct airplay_session *rs;
  struct timespec ts;
  bool is_sync_time;

  // Check if it is time send a sync packet to sessions that are already running
  is_sync_time = rtp_sync_is_time(rms->rtp_session);

  // Just used for logging, the clock shouldn't be too far from rms->cur_stamp.ts
  clock_gettime(CLOCK_MONOTONIC, &ts);

  for (rs = airplay_sessions; rs; rs = rs->next)
    {
      if (rs->master_session != rms)
	continue;

      // A device has joined and should get an init sync packet
      if (rs->state == AIRPLAY_STATE_CONNECTED)
	{
	  sync_pkt = rtp_sync_packet_next(rms->rtp_session, rms->cur_stamp, 0x90);
	  control_packet_send(rs, sync_pkt);

	  DPRINTF(E_DBG, L_AIRPLAY, "Start sync packet sent to '%s': cur_pos=%" PRIu32 ", cur_ts=%ld.%09ld, clock=%ld.%09ld, rtptime=%" PRIu32 "\n",
	    rs->devname, rms->cur_stamp.pos, rms->cur_stamp.ts.tv_sec, rms->cur_stamp.ts.tv_nsec, ts.tv_sec, ts.tv_nsec, rms->rtp_session->pos);
	}
      else if (is_sync_time && rs->state == AIRPLAY_STATE_STREAMING)
	{
	  sync_pkt = rtp_sync_packet_next(rms->rtp_session, rms->cur_stamp, 0x80);
	  control_packet_send(rs, sync_pkt);
	}
    }
}


/* ------------------------- Time and control service ----------------------- */

static void
service_stop(struct airplay_service *svc)
{
  if (svc->ev)
    event_free(svc->ev);

  if (svc->fd >= 0)
    close(svc->fd);

  svc->ev = NULL;
  svc->fd = -1;
  svc->port = 0;
}

static int
service_start(struct airplay_service *svc, event_callback_fn cb, unsigned short port, const char *log_service_name)
{
  memset(svc, 0, sizeof(struct airplay_service));

  svc->fd = net_bind(&port, SOCK_DGRAM, log_service_name);
  if (svc->fd < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not start '%s' service\n", log_service_name);
      goto error;
    }

  svc->ev = event_new(evbase_player, svc->fd, EV_READ | EV_PERSIST, cb, svc);
  if (!svc->ev)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not create event for '%s' service\n", log_service_name);
      goto error;
    }

  event_add(svc->ev, NULL);

  svc->port = port;

  return 0;

 error:
  service_stop(svc);
  return -1;
}

static void
timing_svc_cb(int fd, short what, void *arg)
{
  struct airplay_service *svc = arg;
  union net_sockaddr peer_addr;
  socklen_t peer_addrlen = sizeof(peer_addr);
  char address[INET6_ADDRSTRLEN];
  uint8_t req[32];
  uint8_t res[32];
  struct ntp_stamp recv_stamp;
  struct ntp_stamp xmit_stamp;
  int ret;

  ret = timing_get_clock_ntp(&recv_stamp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Couldn't get receive timestamp\n");
      return;
    }

  peer_addrlen = sizeof(peer_addr);
  ret = recvfrom(svc->fd, req, sizeof(req), 0, &peer_addr.sa, &peer_addrlen);
  if (ret < 0)
    {
      net_address_get(address, sizeof(address), &peer_addr);
      DPRINTF(E_LOG, L_AIRPLAY, "Error reading timing request from %s: %s\n", address, strerror(errno));
      return;
    }

  if (ret != 32)
    {
      net_address_get(address, sizeof(address), &peer_addr);
      DPRINTF(E_WARN, L_AIRPLAY, "Got timing request from %s with size %d\n", address, ret);
      return;
    }

  if ((req[0] != 0x80) || (req[1] != 0xd2))
    {
      net_address_get(address, sizeof(address), &peer_addr);
      DPRINTF(E_WARN, L_AIRPLAY, "Packet header from %s doesn't match timing request (got 0x%02x%02x, expected 0x80d2)\n", address, req[0], req[1]);
      return;
    }

  memset(res, 0, sizeof(res));

  /* Header */
  res[0] = 0x80;
  res[1] = 0xd3;
  res[2] = req[2];

  /* Copy client timestamp */
  memcpy(res + 8, req + 24, 8);

  /* Receive timestamp */
  recv_stamp.sec = htobe32(recv_stamp.sec);
  recv_stamp.frac = htobe32(recv_stamp.frac);
  memcpy(res + 16, &recv_stamp.sec, 4);
  memcpy(res + 20, &recv_stamp.frac, 4);

  /* Transmit timestamp */
  ret = timing_get_clock_ntp(&xmit_stamp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Couldn't get transmit timestamp, falling back to receive timestamp\n");

      /* Still better than failing altogether
       * recv/xmit are close enough that it shouldn't matter much
       */
      memcpy(res + 24, &recv_stamp.sec, 4);
      memcpy(res + 28, &recv_stamp.frac, 4);
    }
  else
    {
      xmit_stamp.sec = htobe32(xmit_stamp.sec);
      xmit_stamp.frac = htobe32(xmit_stamp.frac);
      memcpy(res + 24, &xmit_stamp.sec, 4);
      memcpy(res + 28, &xmit_stamp.frac, 4);
    }

  ret = sendto(svc->fd, res, sizeof(res), 0, &peer_addr.sa, peer_addrlen);
  if (ret < 0)
    {
      net_address_get(address, sizeof(address), &peer_addr);
      DPRINTF(E_LOG, L_AIRPLAY, "Could not send timing reply to %s: %s\n", address, strerror(errno));
      return;
    }
}

static void
control_svc_cb(int fd, short what, void *arg)
{
  struct airplay_service *svc = arg;
  union net_sockaddr peer_addr = { 0 };
  socklen_t peer_addrlen = sizeof(peer_addr);
  char address[INET6_ADDRSTRLEN];
  struct airplay_session *rs;
  uint8_t req[8];
  uint16_t seq_start;
  uint16_t seq_len;
  int ret;

  ret = recvfrom(svc->fd, req, sizeof(req), 0, &peer_addr.sa, &peer_addrlen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Error reading control request: %s\n", strerror(errno));
      return;
    }

  if (ret != 8)
    {
      net_address_get(address, sizeof(address), &peer_addr);
      DPRINTF(E_WARN, L_AIRPLAY, "Got control request from %s with size %d\n", address, ret);
      return;
    }

  if ((req[0] != 0x80) || (req[1] != 0xd5))
    {
      net_address_get(address, sizeof(address), &peer_addr);
      DPRINTF(E_WARN, L_AIRPLAY, "Packet header from %s doesn't match retransmit request (got 0x%02x%02x, expected 0x80d5)\n", address, req[0], req[1]);
      return;
    }

  rs = session_find_by_address(&peer_addr);
  if (!rs)
    {
      net_address_get(address, sizeof(address), &peer_addr);
      DPRINTF(E_WARN, L_AIRPLAY, "Control request from %s; not a AirPlay client\n", address);
      return;
    }

  memcpy(&seq_start, req + 4, 2);
  memcpy(&seq_len, req + 6, 2);

  seq_start = be16toh(seq_start);
  seq_len = be16toh(seq_len);

  packets_resend(rs, seq_start, seq_len);
}


/* -------------------- Handlers for sending RTSP requests ------------------ */

static int
payload_make_flush(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  struct airplay_master_session *rms = rs->master_session;
  char buf[64];
  int ret;

  /* Restart sequence */
  ret = snprintf(buf, sizeof(buf), "seq=%" PRIu16 ";rtptime=%u", rms->rtp_session->seqnum, rms->rtp_session->pos);
  if ((ret < 0) || (ret >= sizeof(buf)))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "RTP-Info too big for buffer in FLUSH request\n");
      return -1;
    }
  evrtsp_add_header(req->output_headers, "RTP-Info", buf);

  return 0;
}

static int
payload_make_teardown(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  // Normally we update status when we get the response, but teardown is an
  // exception because we want to stop writing to the device immediately
  rs->state = AIRPLAY_STATE_TEARDOWN;
  return 0;
}

static int
payload_make_set_volume(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  float raop_volume;
  char volstr[32];
  int ret;

  raop_volume = airplay_volume_from_pct(rs->volume, rs->devname);

  /* Don't let locales get in the way here */
  /* We use -%d and -(int)raop_volume so -0.3 won't become 0.3 */
  snprintf(volstr, sizeof(volstr), "-%d.%06d", -(int)raop_volume, -(int)(1000000.0 * (raop_volume - (int)raop_volume)));

  DPRINTF(E_DBG, L_AIRPLAY, "Sending volume %s to '%s'\n", volstr, rs->devname);

  ret = evbuffer_add_printf(req->output_buffer, "volume: %s\r\n", volstr);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Out of memory for SET_PARAMETER payload (volume)\n");
      return -1;
    }

  return 0;
}

static int
payload_make_send_progress(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  struct output_metadata *metadata = arg;
  uint32_t start;
  uint32_t display;
  uint32_t pos;
  uint32_t end;
  int ret;

  metadata_rtptimes_get(&start, &display, &pos, &end, rs->master_session, metadata);

  ret = evbuffer_add_printf(req->output_buffer, "progress: %u/%u/%u\r\n", display, pos, end);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not build progress string for sending\n");
      return -1;
    }

  ret = rtpinfo_header_add(req, rs, metadata);
  if (ret < 0)
    return -1;

  return 0;
}

static int
payload_make_send_artwork(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  struct output_metadata *metadata = arg;
  struct airplay_metadata *rmd = metadata->priv;
  char *ctype;
  uint8_t *buf;
  size_t len;
  int ret;

  switch (rmd->artwork_fmt)
    {
      case ART_FMT_PNG:
	ctype = "image/png";
	break;

      case ART_FMT_JPEG:
	ctype = "image/jpeg";
	break;

      default:
	DPRINTF(E_LOG, L_AIRPLAY, "Unsupported artwork format %d\n", rmd->artwork_fmt);
	return -1;
    }

  buf = evbuffer_pullup(rmd->artwork, -1);
  len = evbuffer_get_length(rmd->artwork);

  ret = evbuffer_add(req->output_buffer, buf, len);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not copy artwork for sending\n");
      return -1;
    }

  ret = rtpinfo_header_add(req, rs, metadata);
  if (ret < 0)
    return -1;

  evrtsp_add_header(req->output_headers, "Content-Type", ctype);

  return 0;
}

static int
payload_make_send_text(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  struct output_metadata *metadata = arg;
  struct airplay_metadata *rmd = metadata->priv;
  uint8_t *buf;
  size_t len;
  int ret;

  buf = evbuffer_pullup(rmd->metadata, -1);
  len = evbuffer_get_length(rmd->metadata);

  ret = evbuffer_add(req->output_buffer, buf, len);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not copy metadata for sending\n");
      return -1;
    }

  ret = rtpinfo_header_add(req, rs, metadata);
  if (ret < 0)
    return -1;

  return 0;
}


/*
Audio formats

Bit 	Value 	Type
2 	0x4 	PCM/8000/16/1
3 	0x8 	PCM/8000/16/2
4 	0x10 	PCM/16000/16/1
5 	0x20 	PCM/16000/16/2
6 	0x40 	PCM/24000/16/1
7 	0x80 	PCM/24000/16/2
8 	0x100 	PCM/32000/16/1
9 	0x200 	PCM/32000/16/2
10 	0x400 	PCM/44100/16/1
11 	0x800 	PCM/44100/16/2
12 	0x1000 	PCM/44100/24/1
13 	0x2000 	PCM/44100/24/2
14 	0x4000 	PCM/48000/16/1
15 	0x8000 	PCM/48000/16/2
16 	0x10000 	PCM/48000/24/1
17 	0x20000 	PCM/48000/24/2
18 	0x40000 	ALAC/44100/16/2
19 	0x80000 	ALAC/44100/24/2
20 	0x100000 	ALAC/48000/16/2
21 	0x200000 	ALAC/48000/24/2
22 	0x400000 	AAC-LC/44100/2
23 	0x800000 	AAC-LC/48000/2
24 	0x1000000 	AAC-ELD/44100/2
25 	0x2000000 	AAC-ELD/48000/2
26 	0x4000000 	AAC-ELD/16000/1
27 	0x8000000 	AAC-ELD/24000/1
28 	0x10000000 	OPUS/16000/1
29 	0x20000000 	OPUS/24000/1
30 	0x40000000 	OPUS/48000/1
31 	0x80000000 	AAC-ELD/44100/1
32 	0x100000000 	AAC-ELD/48000/1
*/
static int
payload_make_setup_stream(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  plist_t root;
  plist_t streams;
  plist_t stream;
  uint8_t *data;
  size_t len;
  int ret;

  stream = plist_new_dict();
  wplist_dict_add_uint(stream, "audioFormat", 262144); // 0x40000 ALAC/44100/16/2
  wplist_dict_add_string(stream, "audioMode", "default");
  wplist_dict_add_uint(stream, "controlPort", rs->control_svc->port);
  wplist_dict_add_uint(stream, "ct", 2); // Compression type, 1 LPCM, 2 ALAC, 3 AAC, 4 AAC ELD, 32 OPUS
  wplist_dict_add_bool(stream, "isMedia", true); // ?
  wplist_dict_add_uint(stream, "latencyMax", 88200); // TODO how do these latencys work?
  wplist_dict_add_uint(stream, "latencyMin", 11025);
  wplist_dict_add_data(stream, "shk", rs->shared_secret, AIRPLAY_AUDIO_KEY_LEN);
  wplist_dict_add_uint(stream, "spf", AIRPLAY_SAMPLES_PER_PACKET); // frames per packet
  wplist_dict_add_uint(stream, "sr", AIRPLAY_QUALITY_SAMPLE_RATE_DEFAULT); // sample rate
  wplist_dict_add_uint(stream, "type", AIRPLAY_RTP_PAYLOADTYPE); // RTP type, 0x60 = 96 real time, 103 buffered
  wplist_dict_add_bool(stream, "supportsDynamicStreamID", false);
  wplist_dict_add_uint(stream, "streamConnectionID", rs->session_id); // Hopefully fine since we have one stream per session
  streams = plist_new_array();
  plist_array_append_item(streams, stream);

  root = plist_new_dict();
  plist_dict_set_item(root, "streams", streams);
  ret = wplist_to_bin(&data, &len, root);
  plist_free(root);

  if (ret < 0)
    return -1;

  evbuffer_add(req->output_buffer, data, len);

  return 0;
}

static int
payload_make_setpeers(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  plist_t item;
  uint8_t *data;
  size_t len;
  int ret;

  plist_t root;

  // TODO also have ipv6
  root = plist_new_array();
  item = plist_new_string(rs->address);
  plist_array_append_item(root, item);
  item = plist_new_string(rs->local_address);
  plist_array_append_item(root, item);

  ret = wplist_to_bin(&data, &len, root);
  plist_free(root);

  if (ret < 0)
    return -1;

  evbuffer_add(req->output_buffer, data, len);

  return 0;
}

static int
payload_make_record(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  struct airplay_master_session *rms = rs->master_session;
  char buf[64];
  int ret;

  evrtsp_add_header(req->output_headers, "X-Apple-ProtocolVersion", "1");

  evrtsp_add_header(req->output_headers, "Range", "npt=0-");

  // Start sequence: next sequence
  ret = snprintf(buf, sizeof(buf), "seq=%" PRIu16 ";rtptime=%u", rms->rtp_session->seqnum, rms->rtp_session->pos);
  if ((ret < 0) || (ret >= sizeof(buf)))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "RTP-Info too big for buffer in RECORD request\n");
      return -1;
    }
  evrtsp_add_header(req->output_headers, "RTP-Info", buf);

  DPRINTF(E_DBG, L_AIRPLAY, "RTP-Info is %s\n", buf);

  return 0;
}

static int
payload_make_setup_session(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  plist_t root;
  plist_t addresses;
  plist_t address;
  char device_id_colon[24];
  uint8_t *data;
  size_t len;
  int ret;

  device_id_colon_make(device_id_colon, sizeof(device_id_colon), airplay_device_id);

  address = plist_new_string(rs->local_address);
  addresses = plist_new_array();
  plist_array_append_item(addresses, address);

  root = plist_new_dict();
  wplist_dict_add_string(root, "deviceID", device_id_colon);
  wplist_dict_add_string(root, "sessionUUID", rs->session_uuid);
  wplist_dict_add_uint(root, "timingPort", rs->timing_svc->port);
  wplist_dict_add_string(root, "timingProtocol", "NTP"); // If set to "None" then an ATV4 will not respond to stream SETUP request

  ret = wplist_to_bin(&data, &len, root);
  plist_free(root);

  if (ret < 0)
    return -1;

  evbuffer_add(req->output_buffer, data, len);

  return 0;
}

/*
The purpose of auth-setup is to authenticate the device and to exchange keys
for encryption. We don't do that, but some AirPlay 2 speakers (Sonos beam,
Airport Express fw 7.8) require this step anyway, otherwise we get a 403 to
our ANNOUNCE. So we do it with a flag for no encryption, and without actually
authenticating the device.

Good to know (source Apple's MFi Accessory Interface Specification):
- Curve25519 Elliptic-Curve Diffie-Hellman technology for key exchange
- RSA for signing and verifying and AES-128 in counter mode for encryption
- We start by sending a Curve25519 public key + no encryption flag
- The device responds with public key, MFi certificate and a signature, which
  is created by the device signing the two public keys with its RSA private
  key and then encrypting the result with the AES master key derived from the
  Curve25519 shared secret (generated from device private key and our public
  key)
- The AES key derived from the Curve25519 shared secret can then be used to
  encrypt future content
- New keys should be generated for each authentication attempt, but we don't
  do that because we don't really use this + it adds a libsodium dependency

Since we don't do auth or encryption, we currently just ignore the reponse.
*/

#if AIRPLAY_USE_AUTH_SETUP
static int
payload_make_auth_setup(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  if (!rs->supports_auth_setup)
    return 1; // skip this request

  // Flag for no encryption. 0x10 may mean encryption.
  evbuffer_add(req->output_buffer, "\x01", 1);

  evbuffer_add(req->output_buffer, airplay_auth_setup_pubkey, sizeof(airplay_auth_setup_pubkey) - 1);

  return 0;
}
#endif

static int
payload_make_pin_start(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  DPRINTF(E_LOG, L_AIRPLAY, "Starting device pairing for '%s', go to the web interface and enter PIN\n", rs->devname);
  return 0;
}

static int
payload_make_pair_generic(int step, struct evrtsp_request *req, struct airplay_session *rs)
{
  uint8_t *body;
  size_t len;
  const char *errmsg;

  switch (step)
    {
      case 1:
	body    = pair_setup_request1(&len, rs->pair_setup_ctx);
	errmsg  = pair_setup_errmsg(rs->pair_setup_ctx);
	break;
      case 2:
	body    = pair_setup_request2(&len, rs->pair_setup_ctx);
	errmsg  = pair_setup_errmsg(rs->pair_setup_ctx);
	break;
      case 3:
	body    = pair_setup_request3(&len, rs->pair_setup_ctx);
	errmsg  = pair_setup_errmsg(rs->pair_setup_ctx);
	break;
      case 4:
	body    = pair_verify_request1(&len, rs->pair_verify_ctx);
	errmsg  = pair_verify_errmsg(rs->pair_verify_ctx);
	break;
      case 5:
	body    = pair_verify_request2(&len, rs->pair_verify_ctx);
	errmsg  = pair_verify_errmsg(rs->pair_verify_ctx);
	break;
      default:
	body    = NULL;
	errmsg  = "Bug! Bad step number";
    }

  if (!body)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Verification step %d request error: %s\n", step, errmsg);
      return -1;
    }

  evbuffer_add(req->output_buffer, body, len);
  free(body);

  // Required!!
  if (rs->pair_type == PAIR_CLIENT_HOMEKIT_NORMAL)
    evrtsp_add_header(req->output_headers, "X-Apple-HKP", "3");
  else if (rs->pair_type == PAIR_CLIENT_HOMEKIT_TRANSIENT)
    evrtsp_add_header(req->output_headers, "X-Apple-HKP", "4");

  return 0;
}

static int
payload_make_pair_setup1(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  char *pin = arg;
  char device_id_hex[16 + 1];

  if (pin)
    rs->pair_type = PAIR_CLIENT_HOMEKIT_NORMAL;

  snprintf(device_id_hex, sizeof(device_id_hex), "%016" PRIX64, airplay_device_id);

  rs->pair_setup_ctx = pair_setup_new(rs->pair_type, pin, NULL, NULL, device_id_hex);
  if (!rs->pair_setup_ctx)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Out of memory for verification setup context\n");
      return -1;
    }

  rs->state = AIRPLAY_STATE_AUTH;

  return payload_make_pair_generic(1, req, rs);
}

static int
payload_make_pair_setup2(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  return payload_make_pair_generic(2, req, rs);
}

static int
payload_make_pair_setup3(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  return payload_make_pair_generic(3, req, rs);
}

static int
payload_make_pair_verify1(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  struct output_device *device;
  char device_id_hex[16 + 1];

  device = outputs_device_get(rs->device_id);
  if (!device)
    return -1;

  snprintf(device_id_hex, sizeof(device_id_hex), "%016" PRIX64, airplay_device_id);

  rs->pair_verify_ctx = pair_verify_new(rs->pair_type, device->auth_key, NULL, NULL, device_id_hex);
  if (!rs->pair_verify_ctx)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Out of memory for verification verify context\n");
      return -1;
    }

  return payload_make_pair_generic(4, req, rs);
}

static int
payload_make_pair_verify2(struct evrtsp_request *req, struct airplay_session *rs, void *arg)
{
  return payload_make_pair_generic(5, req, rs);
}


/* ------------------------------ Session startup --------------------------- */

static void
start_failure(struct airplay_session *rs)
{
  struct output_device *device;

  device = outputs_device_get(rs->device_id);
  if (!device)
    {
      session_failure(rs);
      return;
    }

  // If our key was incorrect, or the device reset its pairings, then this
  // function was called because the encrypted request (SETUP) timed out
  if (device->auth_key)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Clearing '%s' pairing keys, you need to pair again\n", rs->devname);

      free(device->auth_key);
      device->auth_key = NULL;
      device->requires_auth = 1;
    }

  session_failure(rs);
}

static void
start_retry(struct airplay_session *rs)
{
  struct output_device *device;
  int callback_id = rs->callback_id;

  device = outputs_device_get(rs->device_id);
  if (!device)
    {
      session_failure(rs);
      return;
    }

  // Some devices don't seem to work with ipv6, so if the error wasn't a hard
  // failure (bad password) we fall back to ipv4 and flag device as bad for ipv6
  if (rs->family != AF_INET6 || (rs->state & AIRPLAY_STATE_F_FAILED))
    {
      session_failure(rs);
      return;
    }

  // This flag is permanent and will not be overwritten by mdns advertisements
  device->v6_disabled = 1;

  // Drop session, try again with ipv4
  session_cleanup(rs);
  airplay_device_start(device, callback_id);
}


/* ---------------------------- RTSP response handlers ---------------------- */

static enum airplay_seq_type
response_handler_pin_start(struct evrtsp_request *req, struct airplay_session *rs)
{
  rs->state = AIRPLAY_STATE_AUTH;

  return AIRPLAY_SEQ_CONTINUE; // TODO before we reported failure since device is locked
}

static enum airplay_seq_type
response_handler_record(struct evrtsp_request *req, struct airplay_session *rs)
{
  rs->state = AIRPLAY_STATE_RECORD;

  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_setup_stream(struct evrtsp_request *req, struct airplay_session *rs)
{
  plist_t response;
  plist_t streams;
  plist_t stream;
  plist_t item;
  uint64_t uintval;
  int ret;

  DPRINTF(E_INFO, L_AIRPLAY, "Setting up AirPlay session %u (%s -> %s)\n", rs->session_id, rs->local_address, rs->address);

  ret = wplist_from_evbuf(&response, req->input_buffer);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not parse plist from '%s'\n", rs->devname);
      return AIRPLAY_SEQ_ABORT;
    }

  streams = plist_dict_get_item(response, "streams");
  if (!streams)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not find streams item in response from '%s'\n", rs->devname);
      goto error;
    }

  stream = plist_array_get_item(streams, 0);
  if (!stream)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not find stream item in response from '%s'\n", rs->devname);
      goto error;
    }

  item = plist_dict_get_item(stream, "dataPort");
  if (item)
    {
      plist_get_uint_val(item, &uintval);
      rs->data_port = uintval;
    }

  item = plist_dict_get_item(stream, "controlPort");
  if (item)
    {
      plist_get_uint_val(item, &uintval);
      rs->control_port = uintval;
    }

  if (rs->data_port == 0 || rs->control_port == 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Missing port number in reply from '%s' (d=%u, c=%u)\n", rs->devname, rs->data_port, rs->control_port);
      goto error;
    }

  DPRINTF(E_DBG, L_AIRPLAY, "Negotiated UDP streaming session; ports d=%u c=%u t=%u e=%u\n", rs->data_port, rs->control_port, rs->timing_port, rs->events_port);

  rs->server_fd = net_connect(rs->address, rs->data_port, SOCK_DGRAM, "AirPlay data");
  if (rs->server_fd < 0)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Could not connect to data port\n");
      goto error;
    }

  // Reverse connection, used to receive playback events from device
  ret = airplay_events_listen(rs->devname, rs->address, rs->events_port, rs->shared_secret, rs->shared_secret_len);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Could not connect to '%s' events port %u, proceeding anyway\n", rs->devname, rs->events_port);
    }

  rs->state = AIRPLAY_STATE_SETUP;

  plist_free(response);
  return AIRPLAY_SEQ_CONTINUE;

 error:
  plist_free(response);
  return AIRPLAY_SEQ_ABORT;
}

static enum airplay_seq_type
response_handler_volume_start(struct evrtsp_request *req, struct airplay_session *rs)
{
  int ret;

  ret = airplay_metadata_startup_send(rs);
  if (ret < 0)
    return AIRPLAY_SEQ_ABORT;

  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_setup_session(struct evrtsp_request *req, struct airplay_session *rs)
{
  plist_t response;
  plist_t item;
  uint64_t uintval;
  int ret;

  ret = wplist_from_evbuf(&response, req->input_buffer);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not parse plist from '%s'\n", rs->devname);
      return AIRPLAY_SEQ_ABORT;
    }

  item = plist_dict_get_item(response, "eventPort");
  if (item)
    {
      plist_get_uint_val(item, &uintval);
      rs->events_port = uintval;
    }

  item = plist_dict_get_item(response, "timingPort");
  if (item)
    {
      plist_get_uint_val(item, &uintval);
      rs->timing_port = uintval;
    }

  if (rs->events_port == 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "SETUP reply is missing event port\n");
      goto error;
    }

  plist_free(response);
  return AIRPLAY_SEQ_CONTINUE;

 error:
  plist_free(response);
  return AIRPLAY_SEQ_ABORT;
}

static enum airplay_seq_type
response_handler_flush(struct evrtsp_request *req, struct airplay_session *rs)
{
  rs->state = AIRPLAY_STATE_CONNECTED;
  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_teardown(struct evrtsp_request *req, struct airplay_session *rs)
{
  rs->state = AIRPLAY_STATE_STOPPED;
  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_teardown_failure(struct evrtsp_request *req, struct airplay_session *rs)
{
  if (rs->state != AIRPLAY_STATE_AUTH)
    rs->state = AIRPLAY_STATE_FAILED;
  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_info_generic(struct evrtsp_request *req, struct airplay_session *rs)
{
  struct output_device *device;
  plist_t response;
  plist_t item;
  int ret;

  device = outputs_device_get(rs->device_id);
  if (!device)
    return AIRPLAY_SEQ_ABORT;

  ret = session_ids_set(rs);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not make session url or id for device '%s'\n", rs->devname);
      return AIRPLAY_SEQ_ABORT;
    }

  ret = wplist_from_evbuf(&response, req->input_buffer);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not parse plist from '%s'\n", rs->devname);
      return AIRPLAY_SEQ_ABORT;
    }

  item = plist_dict_get_item(response, "statusFlags");
  if (item)
    plist_get_uint_val(item, &rs->statusflags);

  plist_free(response);

  DPRINTF(E_DBG, L_AIRPLAY, "Status flags from '%s' was %" PRIu64 ": cable attached %d, one time pairing %d, password %d, PIN %d\n",
    rs->devname, rs->statusflags, (bool)(rs->statusflags & AIRPLAY_FLAG_AUDIO_CABLE_ATTACHED), (bool)(rs->statusflags & AIRPLAY_FLAG_ONE_TIME_PAIRING_REQUIRED),
    (bool)(rs->statusflags & AIRPLAY_FLAG_PASSWORD_REQUIRED), (bool)(rs->statusflags & AIRPLAY_FLAG_PIN_REQUIRED));

  // Evaluate what next sequence based on response
  if (rs->statusflags & AIRPLAY_FLAG_ONE_TIME_PAIRING_REQUIRED)
    {
      rs->pair_type = PAIR_CLIENT_HOMEKIT_NORMAL;

      if (!device->auth_key)
	{
	  device->requires_auth = 1;
          rs->state = AIRPLAY_STATE_AUTH;
	  return AIRPLAY_SEQ_PIN_START;
	}

      rs->state = AIRPLAY_STATE_INFO;
      return AIRPLAY_SEQ_PAIR_VERIFY;
    }
  else if (rs->statusflags & AIRPLAY_FLAG_PIN_REQUIRED)
    {
      free(device->auth_key);
      device->auth_key = NULL;
      device->requires_auth = 1;

      rs->pair_type = PAIR_CLIENT_HOMEKIT_NORMAL;
      rs->state = AIRPLAY_STATE_AUTH;
      return AIRPLAY_SEQ_PIN_START;
    }
  else if (rs->statusflags & AIRPLAY_FLAG_PASSWORD_REQUIRED)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "'%s' requires password authentication, but that is currently unsupported for AirPlay 2\n", rs->devname);
      rs->state = AIRPLAY_STATE_AUTH;
      return AIRPLAY_SEQ_ABORT;
    }

  rs->pair_type = PAIR_CLIENT_HOMEKIT_TRANSIENT;
  rs->state = AIRPLAY_STATE_INFO;
  return AIRPLAY_SEQ_PAIR_TRANSIENT;
}

static enum airplay_seq_type
response_handler_info_probe(struct evrtsp_request *req, struct airplay_session *rs)
{
  enum airplay_seq_type seq_type;

  seq_type = response_handler_info_generic(req, rs);
  if (seq_type == AIRPLAY_SEQ_ABORT || seq_type == AIRPLAY_SEQ_PIN_START)
    return seq_type;

  // When probing we don't want to continue with PAIR_VERIFY or PAIR_TRANSIENT
  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_info_start(struct evrtsp_request *req, struct airplay_session *rs)
{
  enum airplay_seq_type seq_type;

  seq_type = response_handler_info_generic(req, rs);
  if (seq_type == AIRPLAY_SEQ_ABORT || seq_type == AIRPLAY_SEQ_PIN_START)
    return seq_type;

  // Pair and then run SEQ_START_PLAYBACK which sets up the playback
  rs->next_seq = AIRPLAY_SEQ_START_PLAYBACK;

  return seq_type;
}

static enum airplay_seq_type
response_handler_pair_generic(int step, struct evrtsp_request *req, struct airplay_session *rs)
{
  uint8_t *response;
  const char *errmsg;
  size_t len;
  int ret;

  response = evbuffer_pullup(req->input_buffer, -1);
  len = evbuffer_get_length(req->input_buffer);

  switch (step)
    {
      case 1:
	ret = pair_setup_response1(rs->pair_setup_ctx, response, len);
	errmsg = pair_setup_errmsg(rs->pair_setup_ctx);
	break;
      case 2:
	ret = pair_setup_response2(rs->pair_setup_ctx, response, len);
	errmsg = pair_setup_errmsg(rs->pair_setup_ctx);
	break;
      case 3:
	ret = pair_setup_response3(rs->pair_setup_ctx, response, len);
	errmsg = pair_setup_errmsg(rs->pair_setup_ctx);
	break;
      case 4:
	ret = pair_verify_response1(rs->pair_verify_ctx, response, len);
	errmsg = pair_verify_errmsg(rs->pair_verify_ctx);
	break;
      case 5:
	ret = pair_verify_response2(rs->pair_verify_ctx, response, len);
	errmsg = pair_verify_errmsg(rs->pair_verify_ctx);
	break;
      default:
	ret = -1;
	errmsg = "Bug! Bad step number";
    }

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Pairing step %d response from '%s' error: %s\n", step, rs->devname, errmsg);
      DHEXDUMP(E_DBG, L_AIRPLAY, response, len, "Raw response");
      return AIRPLAY_SEQ_ABORT;
    }

  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_pair_setup1(struct evrtsp_request *req, struct airplay_session *rs)
{
  struct output_device *device;

  if (rs->pair_type == PAIR_CLIENT_HOMEKIT_TRANSIENT && req->response_code == RTSP_CONNECTION_AUTH_REQUIRED)
    {
      device = outputs_device_get(rs->device_id);
      if (!device)
	return AIRPLAY_SEQ_ABORT;

      device->requires_auth = 1; // FIXME might be reset by mdns announcement
      rs->pair_type = PAIR_CLIENT_HOMEKIT_NORMAL;

      return AIRPLAY_SEQ_PIN_START;
    }

  return response_handler_pair_generic(1, req, rs);
}

static enum airplay_seq_type
response_handler_pair_setup2(struct evrtsp_request *req, struct airplay_session *rs)
{
  enum airplay_seq_type seq_type;
  struct pair_result *result;
  int ret;

  seq_type = response_handler_pair_generic(2, req, rs);
  if (seq_type != AIRPLAY_SEQ_CONTINUE)
    return seq_type;

  if (rs->pair_type != PAIR_CLIENT_HOMEKIT_TRANSIENT)
    return seq_type;

  ret = pair_setup_result(NULL, &result, rs->pair_setup_ctx);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Transient setup result error: %s\n", pair_setup_errmsg(rs->pair_setup_ctx));
      goto error;
    }

  ret = session_cipher_setup(rs, result->shared_secret, result->shared_secret_len);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Pair transient error setting up encryption for '%s'\n", rs->devname);
      goto error;
    }

  return AIRPLAY_SEQ_CONTINUE;

 error:
  rs->state = AIRPLAY_STATE_FAILED;
  return AIRPLAY_SEQ_ABORT;
}

static enum airplay_seq_type
response_handler_pair_setup3(struct evrtsp_request *req, struct airplay_session *rs)
{
  struct output_device *device;
  const char *authorization_key;
  enum airplay_seq_type seq_type;
  int ret;

  seq_type = response_handler_pair_generic(3, req, rs);
  if (seq_type != AIRPLAY_SEQ_CONTINUE)
    return seq_type;

  ret = pair_setup_result(&authorization_key, NULL, rs->pair_setup_ctx);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Pair setup result error: %s\n", pair_setup_errmsg(rs->pair_setup_ctx));
      return AIRPLAY_SEQ_ABORT;
    }

  DPRINTF(E_LOG, L_AIRPLAY, "Pair setup stage complete, saving authorization key\n");

  device = outputs_device_get(rs->device_id);
  if (!device)
    return AIRPLAY_SEQ_ABORT;

  free(device->auth_key);
  device->auth_key = strdup(authorization_key);

  // A blocking db call... :-~
  db_speaker_save(device);

  // No longer AIRPLAY_STATE_AUTH
  rs->state = AIRPLAY_STATE_STOPPED;

  return AIRPLAY_SEQ_CONTINUE;
}

static enum airplay_seq_type
response_handler_pair_verify1(struct evrtsp_request *req, struct airplay_session *rs)
{
  struct output_device *device;
  enum airplay_seq_type seq_type;

  seq_type = response_handler_pair_generic(4, req, rs);
  if (seq_type != AIRPLAY_SEQ_CONTINUE)
    {
      rs->state = AIRPLAY_STATE_AUTH;

      device = outputs_device_get(rs->device_id);
      if (!device)
	return AIRPLAY_SEQ_ABORT;

      // Clear auth_key, the device did not accept it
      free(device->auth_key);
      device->auth_key = NULL;

      return AIRPLAY_SEQ_ABORT;
    }

  return seq_type;
}

static enum airplay_seq_type
response_handler_pair_verify2(struct evrtsp_request *req, struct airplay_session *rs)
{
  struct output_device *device;
  enum airplay_seq_type seq_type;
  struct pair_result *result;
  int ret;

  seq_type = response_handler_pair_generic(5, req, rs);
  if (seq_type != AIRPLAY_SEQ_CONTINUE)
    goto error;

  ret = pair_verify_result(&result, rs->pair_verify_ctx);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Pair verify result error: %s\n", pair_verify_errmsg(rs->pair_verify_ctx));
      goto error;
    }

  ret = session_cipher_setup(rs, result->shared_secret, result->shared_secret_len);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Pair verify error setting up encryption for '%s'\n", rs->devname);
      goto error;
    }

  return AIRPLAY_SEQ_CONTINUE;

 error:
  device = outputs_device_get(rs->device_id);
  if (!device)
    return AIRPLAY_SEQ_ABORT;

  // Clear auth_key, the device did not accept it, or some other unexpected error
  free(device->auth_key);
  device->auth_key = NULL;

  rs->state = AIRPLAY_STATE_AUTH;

  return AIRPLAY_SEQ_ABORT;
}


/* ---------------------- Request/response sequence control ----------------- */

/*
 * Request queueing HOWTO
 *
 * Sending:
 * - increment rs->reqs_in_flight
 * - set evrtsp connection closecb to NULL
 *
 * Request callback:
 * - decrement rs->reqs_in_flight first thing, even if the callback is
 *   called for error handling (req == NULL or HTTP error code)
 * - if rs->reqs_in_flight == 0, setup evrtsp connection closecb
 *
 * When a request fails, the whole AirPlay session is declared failed and
 * torn down by calling session_failure(), even if there are requests
 * queued on the evrtsp connection. There is no reason to think pending
 * requests would work out better than the one that just failed and recovery
 * would be tricky to get right.
 *
 * evrtsp behaviour with queued requests:
 * - request callback is called with req == NULL to indicate a connection
 *   error; if there are several requests queued on the connection, this can
 *   happen for each request if the connection isn't destroyed
 * - the connection is reset, and the closecb is called if the connection was
 *   previously connected. There is no closecb set when there are requests in
 *   flight
 */

static struct airplay_seq_definition airplay_seq_definition[] =
{
  { AIRPLAY_SEQ_START, NULL, start_retry },
  { AIRPLAY_SEQ_START_PLAYBACK, session_connected, start_failure },
  { AIRPLAY_SEQ_PROBE, session_success, session_failure },
  { AIRPLAY_SEQ_FLUSH, session_status, session_failure },
  { AIRPLAY_SEQ_STOP, session_success, session_failure },
  { AIRPLAY_SEQ_FAILURE, session_success, session_failure},
  { AIRPLAY_SEQ_PIN_START, session_success, session_failure },
  { AIRPLAY_SEQ_SEND_VOLUME, session_status, session_failure },
  { AIRPLAY_SEQ_SEND_TEXT, NULL, session_failure },
  { AIRPLAY_SEQ_SEND_PROGRESS, NULL, session_failure },
  { AIRPLAY_SEQ_SEND_ARTWORK, NULL, session_failure },
  { AIRPLAY_SEQ_PAIR_SETUP, session_pair_success, session_failure },
  { AIRPLAY_SEQ_PAIR_VERIFY, session_pair_success, session_failure },
  { AIRPLAY_SEQ_PAIR_TRANSIENT, session_pair_success, session_failure },
  { AIRPLAY_SEQ_FEEDBACK, NULL, session_failure },
};

// The size of the second array dimension MUST at least be the size of largest
// sequence + 1, because then we can count on a zero terminator when iterating
static struct airplay_seq_request airplay_seq_request[][7] =
{
  {
    { AIRPLAY_SEQ_START, "GET /info", EVRTSP_REQ_GET, NULL, response_handler_info_start, NULL, "/info", false },
  },
  {
#if AIRPLAY_USE_AUTH_SETUP
    { AIRPLAY_SEQ_START_PLAYBACK, "auth-setup", EVRTSP_REQ_POST, payload_make_auth_setup, NULL, "application/octet-stream", "/auth-setup", true },
#endif
    { AIRPLAY_SEQ_START_PLAYBACK, "SETUP (session)", EVRTSP_REQ_SETUP, payload_make_setup_session, response_handler_setup_session, "application/x-apple-binary-plist", NULL, false },
    { AIRPLAY_SEQ_START_PLAYBACK, "SETPEERS", EVRTSP_REQ_SETPEERS, payload_make_setpeers, NULL, "/peer-list-changed", NULL, false },
    { AIRPLAY_SEQ_START_PLAYBACK, "SETUP (stream)", EVRTSP_REQ_SETUP, payload_make_setup_stream, response_handler_setup_stream, "application/x-apple-binary-plist", NULL, false },
    { AIRPLAY_SEQ_START_PLAYBACK, "RECORD", EVRTSP_REQ_RECORD, payload_make_record, response_handler_record, NULL, NULL, false },
    // Some devices (e.g. Sonos Symfonisk) don't register the volume if it isn't last
    { AIRPLAY_SEQ_START_PLAYBACK, "SET_PARAMETER (volume)", EVRTSP_REQ_SET_PARAMETER, payload_make_set_volume, response_handler_volume_start, "text/parameters", NULL, true },
  },
  {
    { AIRPLAY_SEQ_PROBE, "GET /info (probe)", EVRTSP_REQ_GET, NULL, response_handler_info_probe, NULL, "/info", false },
  },
  {
    { AIRPLAY_SEQ_FLUSH, "FLUSH", EVRTSP_REQ_FLUSH, payload_make_flush, response_handler_flush, NULL, NULL, false },
  },
  {
    { AIRPLAY_SEQ_STOP, "TEARDOWN", EVRTSP_REQ_TEARDOWN, payload_make_teardown, response_handler_teardown, NULL, NULL, true },
  },
  {
    { AIRPLAY_SEQ_FAILURE, "TEARDOWN (failure)", EVRTSP_REQ_TEARDOWN, payload_make_teardown, response_handler_teardown_failure, NULL, NULL, false },
  },
  {
    { AIRPLAY_SEQ_PIN_START, "PIN start", EVRTSP_REQ_POST, payload_make_pin_start, response_handler_pin_start, NULL, "/pair-pin-start", false },
  },
  {
    { AIRPLAY_SEQ_SEND_VOLUME, "SET_PARAMETER (volume)", EVRTSP_REQ_SET_PARAMETER, payload_make_set_volume, NULL, "text/parameters", NULL, true },
  },
  {
    { AIRPLAY_SEQ_SEND_TEXT, "SET_PARAMETER (text)", EVRTSP_REQ_SET_PARAMETER, payload_make_send_text, NULL, "application/x-dmap-tagged", NULL, true },
  },
  {
    { AIRPLAY_SEQ_SEND_PROGRESS, "SET_PARAMETER (progress)", EVRTSP_REQ_SET_PARAMETER, payload_make_send_progress, NULL, "text/parameters", NULL, true },
  },
  {
    { AIRPLAY_SEQ_SEND_ARTWORK, "SET_PARAMETER (artwork)", EVRTSP_REQ_SET_PARAMETER, payload_make_send_artwork, NULL, NULL, NULL, true },
  },
  {
    { AIRPLAY_SEQ_PAIR_SETUP, "pair setup 1", EVRTSP_REQ_POST, payload_make_pair_setup1, response_handler_pair_setup1, "application/octet-stream", "/pair-setup", false },
    { AIRPLAY_SEQ_PAIR_SETUP, "pair setup 2", EVRTSP_REQ_POST, payload_make_pair_setup2, response_handler_pair_setup2, "application/octet-stream", "/pair-setup", false },
    { AIRPLAY_SEQ_PAIR_SETUP, "pair setup 3", EVRTSP_REQ_POST, payload_make_pair_setup3, response_handler_pair_setup3, "application/octet-stream", "/pair-setup", false },
  },
  {
    // Proceed on error is true because we want to delete the device key in the response handler if the verification fails
    { AIRPLAY_SEQ_PAIR_VERIFY, "pair verify 1", EVRTSP_REQ_POST, payload_make_pair_verify1, response_handler_pair_verify1, "application/octet-stream", "/pair-verify", true },
    { AIRPLAY_SEQ_PAIR_VERIFY, "pair verify 2", EVRTSP_REQ_POST, payload_make_pair_verify2, response_handler_pair_verify2, "application/octet-stream", "/pair-verify", false },
  },
  {
    // Some devices (i.e. my ATV4) gives a 470 when trying transient, so we proceed on that so the handler can trigger PIN setup sequence
    { AIRPLAY_SEQ_PAIR_TRANSIENT, "pair setup 1", EVRTSP_REQ_POST, payload_make_pair_setup1, response_handler_pair_setup1, "application/octet-stream", "/pair-setup", true },
    { AIRPLAY_SEQ_PAIR_TRANSIENT, "pair setup 2", EVRTSP_REQ_POST, payload_make_pair_setup2, response_handler_pair_setup2, "application/octet-stream", "/pair-setup", false },
  },
  {
    { AIRPLAY_SEQ_FEEDBACK, "POST /feedback", EVRTSP_REQ_POST, NULL, NULL, NULL, "/feedback", true },
  },
};


static void
sequence_continue_cb(struct evrtsp_request *req, void *arg)
{
  struct airplay_seq_ctx *seq_ctx = arg;
  struct airplay_seq_request *cur_request = seq_ctx->cur_request;
  struct airplay_session *rs = seq_ctx->session;
  enum airplay_seq_type seq_type;

  rs->reqs_in_flight--;
  if (!rs->reqs_in_flight)
    evrtsp_connection_set_closecb(rs->ctrl, rtsp_close_cb, rs);

  if (!req)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "No response to %s from '%s'\n", cur_request->name, rs->devname);
      goto error;
    }

  if (req->response_code != RTSP_OK)
    {
      if (!cur_request->proceed_on_rtsp_not_ok)
	{
	  DPRINTF(E_LOG, L_AIRPLAY, "Response to %s from '%s' was negative, aborting (%d %s)\n", cur_request->name, rs->devname, req->response_code, req->response_code_line);
	  goto error;
	}

      DPRINTF(E_WARN, L_AIRPLAY, "Response to %s from '%s' was negative, proceeding anyway (%d %s)\n", cur_request->name, rs->devname, req->response_code, req->response_code_line);
    }

  // We don't check that the reply CSeq matches the request CSeq, because some
  // targets like Reflector and AirFoil don't return the CSeq according to the
  // rtsp spec. And the CSeq is not really important anyway.

  if (cur_request->response_handler)
    {
      seq_type = cur_request->response_handler(req, rs);
      if (seq_type != AIRPLAY_SEQ_CONTINUE)
	{
	  if (seq_type == AIRPLAY_SEQ_ABORT)
	    goto error;

	  // Handler wanted to start a new sequence
	  sequence_start(seq_type, seq_ctx->session, seq_ctx->payload_make_arg, seq_ctx->log_caller);
	  free(seq_ctx);
	  return;
	}
    }

  seq_ctx->cur_request++;

  if (seq_ctx->cur_request->name)
    {
      sequence_continue(seq_ctx);
      return;
    }

  if (seq_ctx->on_success)
    seq_ctx->on_success(rs);

  free(seq_ctx);
  return;

 error:
  if (seq_ctx->on_error)
    seq_ctx->on_error(rs);

  free(seq_ctx);
}

static void
sequence_continue(struct airplay_seq_ctx *seq_ctx)
{
  struct airplay_session *rs = seq_ctx->session;
  struct airplay_seq_request *cur_request = seq_ctx->cur_request;
  struct evrtsp_request *req = NULL;
  const char *uri;
  int ret;

  req = evrtsp_request_new(sequence_continue_cb, seq_ctx);
  if (!req)
    goto error;

  ret = request_headers_add(req, rs, cur_request->rtsp_type);
  if (ret < 0)
    goto error;

  if (cur_request->content_type)
    evrtsp_add_header(req->output_headers, "Content-Type", cur_request->content_type);

  if (cur_request->payload_make)
    {
      ret = cur_request->payload_make(req, rs, seq_ctx->payload_make_arg);
      if (ret > 0) // Skip to next request in sequence, if none -> error
        {
	  seq_ctx->cur_request++;
	  if (!seq_ctx->cur_request->name)
	    {
	      DPRINTF(E_LOG, L_AIRPLAY, "Bug! payload_make signaled skip request, but there is nothing to skip to\n");
	      goto error;
	    }

	  evrtsp_request_free(req);
	  sequence_continue(seq_ctx);
	  return;
        }
      else if (ret < 0)
	goto error;
    }

  uri = (cur_request->uri) ? cur_request->uri : rs->session_url;

  DPRINTF(E_DBG, L_AIRPLAY, "%s: Sending %s to '%s'\n", seq_ctx->log_caller, cur_request->name, rs->devname);

  ret = evrtsp_make_request(rs->ctrl, req, cur_request->rtsp_type, uri);
  if (ret < 0)
    goto error;

  evrtsp_connection_set_closecb(rs->ctrl, NULL, NULL);

  rs->reqs_in_flight++;

  return;

 error:
  DPRINTF(E_LOG, L_AIRPLAY, "%s: Error sending %s to '%s'\n", seq_ctx->log_caller, cur_request->name, rs->devname);

  if (req)
    evrtsp_request_free(req);

  // Sets status to FAILED, gives status to player and frees session. Must be
  // deferred, otherwise sequence_start() could invalidate the session, meaning
  // any dereference of the session by the caller after sequence_start() would
  // segfault.
  deferred_session_failure(rs);

  free(seq_ctx);
}

// All errors that may occur during a sequence are called back async
static void
sequence_start(enum airplay_seq_type seq_type, struct airplay_session *rs, void *arg, const char *log_caller)
{
  struct airplay_seq_ctx *seq_ctx;

  CHECK_NULL(L_AIRPLAY, seq_ctx = calloc(1, sizeof(struct airplay_seq_ctx)));

  seq_ctx->session = rs;
  seq_ctx->cur_request = &airplay_seq_request[seq_type][0]; // First step of the sequence
  seq_ctx->on_success = airplay_seq_definition[seq_type].on_success;
  seq_ctx->on_error = airplay_seq_definition[seq_type].on_error;
  seq_ctx->payload_make_arg = arg;
  seq_ctx->log_caller = log_caller;

  sequence_continue(seq_ctx); // Ownership transferred
}


/* ---------------- Airplay devices discovery - mDNS callback --------------- */
/*                              Thread: main (mdns)                           */

static int
features_parse(struct keyval *features_kv, const char *features_txt, const char *name)
{
  uint64_t features = 0;
  const char *delim_ptr;
  int i, j;

  // Even though features_txt may be two commaseparated values we can pass it to
  // safe_hextou32() which will only convert the first value.
  if ( safe_hextou32(features_txt, (uint32_t *)&features) < 0 ||
       ((delim_ptr = strchr(features_txt, ',')) && safe_hextou32(delim_ptr + 1, ((uint32_t *)&features) + 1) < 0) )
    {
      DPRINTF(E_LOG, L_AIRPLAY, "AirPlay '%s': unexpected features field '%s' in TXT record\n", name, features_txt);
      return -1;
    }

  // Walk through the bits
  for (i = 0; i < (sizeof(features) * CHAR_BIT); i++)
    {
      if (((features >> i) & 0x01) == 0)
        continue;

      // Check if we have it in the features map
      for (j = 0; j < ARRAY_SIZE(features_map); j++)
	{
	  if (i == features_map[j].bit)
	    {
	      DPRINTF(E_SPAM, L_AIRPLAY, "Speaker '%s' announced feature %d: '%s'\n", name, i, features_map[j].name);
              keyval_add(features_kv, features_map[j].name, "1");
	      break;
	    }
	}

      if (j == ARRAY_SIZE(features_map))
	DPRINTF(E_SPAM, L_AIRPLAY, "Speaker '%s' announced feature %d: 'Unknown'\n", name, i);
    }

  return 0;
}


/* Examples of txt content:
 * Airport Express 2:
     ["pk=7de...39" "gcgl=0" "gid=0fd...4" "pi=0fd...a4" "srcvers=366.0" "protovers=1.1" "serialNumber=C8...R" "manufacturer=Apple Inc." "model=AirPort10,115" "flags=0x4" "fv=p20.78100.3" "rsf=0x0" "features=0x445D0A00,0x1C340" "deviceid=74:1B:B2:D1:1A:B7" "acl=0"]
 * Apple TV 4:
     ["vv=2" "osvers=14.2" "srcvers=525.38.42" "pk=c4e...c88" "psi=67C...DBC" "pi=b0b...da0" "protovers=1.1" "model=AppleTV5,3" "gcgl=1" "igl=1" "gid=B...73" "flags=0x244" "features=0x5A7FDFD5,0x3C155FDE" "fex=1d9/Wt5fFTw" "deviceid=AA:BB:CC:DD:EE:FF" "btaddr=D0:00:44:66:BB:66" "acl=0"]
  * Roku
     ["pk=xxxxxxxxx” "gcgl=0" "gid=xxxxxxx” "psi=xxxxx” "pi=8A:71:CA:EF:xxxx" "srcvers=377.28.01" "protovers=1.1" "serialNumber=xxxxxxx” "manufacturer=Roku" "model=3810X" "flags=0x644" "at=0x3" "fv=p20.9.40.4190" "rsf=0x3" "features=0x7F8AD0,0x10BCF46" "deviceid=8A:71:CA:xxxxx” "acl=0"]
  * Samsung TV
     ["pk=7xxxxxxxxxx” "gcgl=0" "gid=xxxxxxxxxxx” "psi=xxxxxxx” "pi=4C:6F:64:xxxxxxx” "srcvers=377.17.24.6" "protovers=1.1" "serialNumber=xxxxxxx” "manufacturer=Samsung" "model=UNU7090" "flags=0x244" "fv=p20.0.1" "rsf=0x3" "features=0x7F8AD0,0x38BCB46" "deviceid=64:1C:AE:xxxxx” "acl=0"]
  * HomePod
     ["vv=2" "osvers=14.3" "srcvers=530.6" "pk=..." "psi=31...D3" "pi=fd...87" "protovers=1.1" "model=AudioAccessory1,1" "tsid=4...E" "gpn=name" "gcgl=1" "igl=1" "gid=4...E" "flags=0x1a404" "features=0x4A7FCA00,0x3C356BD0" "fex=AMp/StBrNTw" "deviceid=D4:...:C1" "btaddr=5E:...:F1" "acl=0"]
  * Sonos Symfonisk
     ["pk=e5...1c" "gcgl=0" "gid=[uuid]" "pi=[uuid]" "srcvers=366.0" "protovers=1.1" "serialNumber=xx" "manufacturer=Sonos" "model=Bookshelf" "flags=0x4" "fv=p20.63.2-88230" "rsf=0x0" "features=0x445F8A00,0x1C340" "deviceid=11:22:33:44:55:66" "acl=0"]
 */
static void
airplay_device_cb(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt)
{
  struct output_device *rd;
  struct airplay_extra *re;
  struct keyval features_kv = { 0 };
  cfg_t *devcfg;
  cfg_opt_t *cfgopt;
  const char *p;
  const char *nickname = NULL;
  const char *features;
  uint64_t id;
  int ret;

  if (port > 0)
    {
      p = keyval_get(txt, "deviceid");
      if (!p)
	{
	  DPRINTF(E_LOG, L_AIRPLAY, "AirPlay device '%s' is missing a device ID\n", name);
	  return;
	}

      ret = device_id_colon_parse(&id, p);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_AIRPLAY, "Could not extract AirPlay device ID ('%s'): %s\n", name, p);
	  return;
	}
    }
  else
    {
      ret = device_id_find_byname(&id, name);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_AIRPLAY, "Could not remove, AirPlay device '%s' not in our list\n", name);
	  return;
	}
    }

  DPRINTF(E_DBG, L_AIRPLAY, "Event for AirPlay device '%s' (port %d, id %" PRIx64 ")\n", name, port, id);

  devcfg = cfg_gettsec(cfg, "airplay", name);
  if (devcfg && cfg_getbool(devcfg, "exclude"))
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Excluding AirPlay device '%s' as set in config\n", name);
      return;
    }
  if (devcfg && cfg_getbool(devcfg, "permanent") && (port < 0))
    {
      DPRINTF(E_INFO, L_AIRPLAY, "AirPlay device '%s' disappeared, but set as permanent in config\n", name);
      return;
    }
  if (devcfg && cfg_getstr(devcfg, "nickname"))
    {
      nickname = cfg_getstr(devcfg, "nickname");
    }

  CHECK_NULL(L_AIRPLAY, rd = calloc(1, sizeof(struct output_device)));
  CHECK_NULL(L_AIRPLAY, re = calloc(1, sizeof(struct airplay_extra)));

  rd->id = id;
  rd->name = nickname ? strdup(nickname) : strdup(name);
  re->mdns_name = strdup(name); // Used for identifying device when it disappears
  rd->type = OUTPUT_TYPE_AIRPLAY;
  rd->type_name = outputs_name(rd->type);
  rd->extra_device_info = re;

  if (port < 0)
    {
      // Device stopped advertising
      switch (family)
	{
	  case AF_INET:
	    rd->v4_port = 1;
	    break;

	  case AF_INET6:
	    rd->v6_port = 1;
	    break;
	}

      ret = player_device_remove(rd);
      if (ret < 0)
	goto free_rd;

      return;
    }

  // Features, see features_map[]
  features = keyval_get(txt, "features");
  if (!features)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Not using AirPlay 2 for device '%s' as it does not have required 'features' in TXT field\n", name);
      goto free_rd;
    }

  ret = features_parse(&features_kv, features, name);
  if (ret < 0)
    goto free_rd;

  if (!keyval_get(&features_kv, "SupportsAirPlayAudio"))
    {
      DPRINTF(E_DBG, L_AIRPLAY, "AirPlay device '%s' does not support audio\n", name);
      goto free_rd;
    }

  if (keyval_get(&features_kv, "MetadataFeatures_0"))
    re->wanted_metadata |= AIRPLAY_MD_WANTS_ARTWORK;
  if (keyval_get(&features_kv, "MetadataFeatures_1"))
    re->wanted_metadata |= AIRPLAY_MD_WANTS_PROGRESS;
  if (keyval_get(&features_kv, "MetadataFeatures_2"))
    re->wanted_metadata |= AIRPLAY_MD_WANTS_TEXT;
  if (keyval_get(&features_kv, "Authentication_8"))
    re->supports_auth_setup = 1;

  if (keyval_get(&features_kv, "SupportsSystemPairing") || keyval_get(&features_kv, "SupportsCoreUtilsPairingAndEncryption"))
    re->supports_pairing_transient = 1;
  else if (keyval_get(&features_kv, "SupportsHKPairingAndAccessControl"))
    rd->requires_auth = 1;

  keyval_clear(&features_kv);

  // Only default audio quality supported so far
  rd->quality.sample_rate = AIRPLAY_QUALITY_SAMPLE_RATE_DEFAULT;
  rd->quality.bits_per_sample = AIRPLAY_QUALITY_BITS_PER_SAMPLE_DEFAULT;
  rd->quality.channels = AIRPLAY_QUALITY_CHANNELS_DEFAULT;

  if (!quality_is_equal(&rd->quality, &airplay_quality_default))
    DPRINTF(E_INFO, L_AIRPLAY, "Device '%s' requested non-default audio quality (%d/%d/%d)\n", rd->name, rd->quality.sample_rate, rd->quality.bits_per_sample, rd->quality.channels);

  // Device type
  re->devtype = AIRPLAY_DEV_OTHER;
  p = keyval_get(txt, "model");

  if (!p)
    re->devtype = AIRPLAY_DEV_OTHER;
  else if (strncmp(p, "AirPort4", strlen("AirPort4")) == 0)
    re->devtype = AIRPLAY_DEV_APEX2_80211N; // Second generation
  else if (strncmp(p, "AirPort", strlen("AirPort")) == 0)
    re->devtype = AIRPLAY_DEV_APEX3_80211N; // Third generation and newer
  else if (strncmp(p, "AppleTV5,3", strlen("AppleTV5,3")) == 0)
    re->devtype = AIRPLAY_DEV_APPLETV4; // Stream to ATV with tvOS 10 needs to be kept alive
  else if (strncmp(p, "AppleTV", strlen("AppleTV")) == 0)
    re->devtype = AIRPLAY_DEV_APPLETV;
  else if (strncmp(p, "AudioAccessory", strlen("AudioAccessory")) == 0)
    re->devtype = AIRPLAY_DEV_HOMEPOD;
  else if (*p == '\0')
    DPRINTF(E_WARN, L_AIRPLAY, "AirPlay device '%s': am has no value\n", name);

  // If the user didn't set any reconnect setting we enable for Apple TV and
  // HomePods due to https://github.com/owntone/owntone-server/issues/734
  cfgopt = devcfg ? cfg_getopt(devcfg, "reconnect") : NULL;
  if (cfgopt && cfgopt->nvalues == 1)
    rd->resurrect = cfg_opt_getnbool(cfgopt, 0);
  else
    rd->resurrect = (re->devtype == AIRPLAY_DEV_APPLETV4) || (re->devtype == AIRPLAY_DEV_HOMEPOD);

  switch (family)
    {
      case AF_INET:
	rd->v4_address = strdup(address);
	rd->v4_port = port;
	DPRINTF(E_INFO, L_AIRPLAY, "Adding AirPlay device '%s': features %s, type %s, address %s:%d\n", 
	  name, features, airplay_devtype[re->devtype], address, port);
	break;

      case AF_INET6:
	rd->v6_address = strdup(address);
	rd->v6_port = port;
	DPRINTF(E_INFO, L_AIRPLAY, "Adding AirPlay device '%s': features %s, type %s, address [%s]:%d\n", 
	  name, features, airplay_devtype[re->devtype], address, port);
	break;

      default:
	DPRINTF(E_LOG, L_AIRPLAY, "Error: AirPlay device '%s' has neither ipv4 og ipv6 address\n", name);
	goto free_rd;
    }

  ret = player_device_add(rd);
  if (ret < 0)
    goto free_rd;

  return;

 free_rd:
  outputs_device_free(rd);
  keyval_clear(&features_kv);
}


/* ---------------------------- Module definitions -------------------------- */
/*                                Thread: player                              */

static int
airplay_device_probe(struct output_device *device, int callback_id)
{
  struct airplay_session *rs;

  rs = session_make(device, callback_id);
  if (!rs)
    return -1;

  sequence_start(AIRPLAY_SEQ_PROBE, rs, NULL, "device_probe");

  return 1;
}

static int
airplay_device_start(struct output_device *device, int callback_id)
{
  struct airplay_session *rs;

  rs = session_make(device, callback_id);
  if (!rs)
    return -1;

  sequence_start(AIRPLAY_SEQ_START, rs, NULL, "device_start");

  return 1;
}

static int
airplay_device_stop(struct output_device *device, int callback_id)
{
  struct airplay_session *rs = device->session;

  rs->callback_id = callback_id;

  sequence_start(AIRPLAY_SEQ_STOP, rs, NULL, "device_stop");

  return 1;
}

static int
airplay_device_flush(struct output_device *device, int callback_id)
{
  struct airplay_session *rs = device->session;

  if (rs->state != AIRPLAY_STATE_STREAMING)
    return 0; // No-op, nothing to flush

  rs->callback_id = callback_id;

  sequence_start(AIRPLAY_SEQ_FLUSH, rs, NULL, "flush");

  return 1;
}

static void
airplay_device_cb_set(struct output_device *device, int callback_id)
{
  struct airplay_session *rs = device->session;

  rs->callback_id = callback_id;
}

static void
airplay_device_free_extra(struct output_device *device)
{
  struct airplay_extra *re = device->extra_device_info;

  free(re->mdns_name);
  free(re);
}

static int
airplay_device_authorize(struct output_device *device, const char *pin, int callback_id)
{
  struct airplay_session *rs;

  // Make a session so we can communicate with the device
  rs = session_make(device, callback_id);
  if (!rs)
    return -1;

  sequence_start(AIRPLAY_SEQ_PAIR_SETUP, rs, (void *)pin, "device_authorize");

  return 1;
}

static void
airplay_write(struct output_buffer *obuf)
{
  struct airplay_master_session *rms;
  struct airplay_session *rs;
  int i;

  for (rms = airplay_master_sessions; rms; rms = rms->next)
    {
      for (i = 0; obuf->data[i].buffer; i++)
	{
	  if (!quality_is_equal(&obuf->data[i].quality, &rms->rtp_session->quality))
	    continue;

	  // Set rms->cur_stamp, which involves a calculation of which session
	  // rtptime corresponds to the pts we are given by the player.
	  timestamp_set(rms, obuf->pts);

	  // Sends sync packets to new sessions, and if it is sync time then also to old sessions
	  packets_sync_send(rms);

	  // TODO avoid this copy
	  evbuffer_add(rms->input_buffer, obuf->data[i].buffer, obuf->data[i].bufsize);
	  rms->input_buffer_samples += obuf->data[i].samples;

	  // Send as many packets as we have data for (one packet requires rawbuf_size bytes)
	  while (evbuffer_get_length(rms->input_buffer) >= rms->rawbuf_size)
	    {
	      evbuffer_remove(rms->input_buffer, rms->rawbuf, rms->rawbuf_size);
	      rms->input_buffer_samples -= rms->samples_per_packet;

	      packets_send(rms);
	    }
	}
    }

  // Check for devices that have joined since last write (we have already sent them
  // initialization sync and rtp packets via packets_sync_send and packets_send)
  for (rs = airplay_sessions; rs; rs = rs->next)
    {
      if (rs->state != AIRPLAY_STATE_CONNECTED)
	continue;

      // Start sending progress to keep ATV's alive
      if (!event_pending(keep_alive_timer, EV_TIMEOUT, NULL))
	evtimer_add(keep_alive_timer, &keep_alive_tv);

      rs->state = AIRPLAY_STATE_STREAMING;
      // Make a cb?
    }
}

static int
airplay_init(void)
{
  int ret;
  int i;
  int timing_port;
  int control_port;

  airplay_device_id = libhash;

  // Check alignment of enum seq_type with airplay_seq_definition and
  // airplay_seq_request
  for (i = 0; i < ARRAY_SIZE(airplay_seq_definition); i++)
    {
      if (airplay_seq_definition[i].seq_type != i || airplay_seq_request[i][0].seq_type != i)
        {
	  DPRINTF(E_LOG, L_AIRPLAY, "Bug! Misalignment between sequence enum and structs: %d, %d, %d\n", i, airplay_seq_definition[i].seq_type, airplay_seq_request[i][0].seq_type);
	  return -1;
        }
    }

  CHECK_NULL(L_AIRPLAY, keep_alive_timer = evtimer_new(evbase_player, airplay_keep_alive_timer_cb, NULL));

  timing_port = cfg_getint(cfg_getsec(cfg, "airplay_shared"), "timing_port");
  ret = service_start(&airplay_timing_svc, timing_svc_cb, timing_port, "AirPlay timing");
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "AirPlay time synchronization failed to start\n");
      goto out_free_timer;
    }

  control_port = cfg_getint(cfg_getsec(cfg, "airplay_shared"), "control_port");
  ret = service_start(&airplay_control_svc, control_svc_cb, control_port, "AirPlay control");
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "AirPlay playback control failed to start\n");
      goto out_stop_timing;
    }

  ret = airplay_events_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "AirPlay events failed to start\n");
      goto out_stop_control;
    }

  ret = mdns_browse("_airplay._tcp", airplay_device_cb, MDNS_CONNECTION_TEST);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not add mDNS browser for AirPlay devices\n");
      goto out_stop_events;
    }

  return 0;

 out_stop_events:
  airplay_events_deinit();
 out_stop_control:
  service_stop(&airplay_control_svc);
 out_stop_timing:
  service_stop(&airplay_timing_svc);
 out_free_timer:
  event_free(keep_alive_timer);

  return -1;
}

static void
airplay_deinit(void)
{
  struct airplay_session *rs;

  airplay_events_deinit();
  service_stop(&airplay_control_svc);
  service_stop(&airplay_timing_svc);

  event_free(keep_alive_timer);

  for (rs = airplay_sessions; airplay_sessions; rs = airplay_sessions)
    {
      airplay_sessions = rs->next;

      session_free(rs);
    }
}

struct output_definition output_airplay =
{
  .name = "AirPlay 2",
  .type = OUTPUT_TYPE_AIRPLAY,
#ifdef PREFER_AIRPLAY2
  .priority = 1,
#else
  .priority = 2,
#endif
  .disabled = 0,
  .init = airplay_init,
  .deinit = airplay_deinit,
  .device_start = airplay_device_start,
  .device_stop = airplay_device_stop,
  .device_flush = airplay_device_flush,
  .device_probe = airplay_device_probe,
  .device_cb_set = airplay_device_cb_set,
  .device_free_extra = airplay_device_free_extra,
  .device_volume_set = airplay_set_volume_one,
  .device_volume_to_pct = airplay_volume_to_pct,
  .write = airplay_write,
  .metadata_prepare = airplay_metadata_prepare,
  .metadata_send = airplay_metadata_send,
  .metadata_purge = airplay_metadata_purge,
  .device_authorize = airplay_device_authorize,
};
