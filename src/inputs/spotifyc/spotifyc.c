#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <fcntl.h>
#include <endian.h>

#include <ctype.h> // for isdigit(), isupper(), islower()
#include <assert.h>

#include <pthread.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <gcrypt.h>
#include <json-c/json.h>

#include "commands.h"

#include "spotifyc.h"
#include "shannon/Shannon.h"
#include "proto/keyexchange.pb-c.h"
#include "proto/authentication.pb-c.h"
#include "proto/mercury.pb-c.h"
#include "proto/metadata.pb-c.h"

/* TODO list

 - session cleanup on failure
 - connect/disconnect/reconnect
 - seek
 - don't memleak e.g. track
 - protect against DOS
 - identify as forked-daapd
*/

#define SP_AP_RESOLVE_URL "https://APResolve.spotify.com/"
#define SP_AP_RESOLVE_KEY "ap_list"

// A "mercury" response may contain multiple parts (e.g. multiple tracks), even
// though this implenentation currently expects just one.
#define SP_MERCURY_MAX_PARTS 32

  // librespot-golang uses /4 ... not sure what that means?
#define SP_MERCURY_ENDPOINT "hm://metadata/3/track/"

// Special Spotify header that comes before the actual Ogg data
#define SP_OGG_HEADER_LEN 167

// For now we just always use channel 0, expand with more if needed
#define SP_DEFAULT_CHANNEL 0

#define SP_CHUNK_LEN_WORDS 1024 * 16

// Shorthand for error handling
#define RETURN_ERROR(r, m) \
  do { ret = (r); sp_errmsg = (m); goto error; } while(0)

enum sp_error {
  SP_OOM        = -1,
  SP_INVALID    = -2,
  SP_DECRYPTION = -3,
  SP_WRITE      = -4,
  SP_NOCONNECTION = -5,
  SP_OCCUPIED     = -6,
  SP_NOSESSION    = -7,
  SP_LOGINFAILED  = -8,
};

enum sp_msg_type
{
  MSG_TYPE_NONE,
  MSG_TYPE_CLIENT_HELLO,
  MSG_TYPE_CLIENT_RESPONSE_PLAINTEXT,
  MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED,
  MSG_TYPE_PONG,
  MSG_TYPE_MERCURY_TRACK_GET,
  MSG_TYPE_AUDIO_KEY_GET,
  MSG_TYPE_CHUNK_REQUEST,
};

// From librespot-golang
enum sp_cmd_type
{
  CmdNone           = 0x00,
  CmdSecretBlock    = 0x02,
  CmdPing           = 0x04,
  CmdStreamChunk    = 0x08,

  CmdStreamChunkRes = 0x09,
  CmdChannelError   = 0x0a,
  CmdChannelAbort   = 0x0b,
  CmdRequestKey     = 0x0c,
  CmdAesKey         = 0x0d,
  CmdAesKeyError    = 0x0e,

  CmdImage          = 0x19,
  CmdCountryCode    = 0x1b,

  CmdPong           = 0x49,
  CmdPongAck        = 0x4a,
  CmdPause          = 0x4b,

  CmdProductInfo    = 0x50,
  CmdLegacyWelcome  = 0x69,

  CmdLicenseVersion = 0x76,
  CmdLogin          = 0xab,
  CmdAPWelcome      = 0xac,
  CmdAuthFailure    = 0xad,

  CmdMercuryReq     = 0xb2,
  CmdMercurySub     = 0xb3,
  CmdMercuryUnsub   = 0xb4,
};

struct sp_cmdargs {
  int (*handler)(struct sp_session *, struct sp_cmdargs *);

  struct sp_session *session;
  char *username;
  char *password;
  uint8_t *stored_cred;
  size_t stored_cred_len;
  uint8_t *token;
  size_t token_len;
  char *path;
  int fd_read;
  int fd_write;
  int seek_ms;
  enum sp_bitrates bitrate;
};

struct crypto_cipher
{
  shn_ctx shannon;
  uint8_t key[32];
  uint32_t nonce;
  uint8_t last_header[3]; // uint8 cmd and uint16 BE size
};

struct crypto_aes_cipher
{
  gcry_cipher_hd_t aes;
  uint8_t key[16];
  uint8_t aes_iv[16];
};

struct crypto_keys
{
  uint8_t private_key[96];
  uint8_t public_key[96];

  uint8_t *shared_secret;
  size_t shared_secret_len;
};

struct sp_mercury
{
  char *uri;
  char *method;
  char *content_type;

  uint64_t seq;

  uint16_t parts_num;
  struct sp_mercury_parts
    {
      uint8_t *data;
      size_t len;

      Track *track;
    } parts[SP_MERCURY_MAX_PARTS];
};

struct sp_file
{
  uint8_t id[20];

  Track *track; // TODO free this
  char *track_path;
  uint8_t track_id[16];
  uint8_t track_key[16];

  uint16_t channel_id;

  // Length and download progress
  size_t len_words; // Length of file in words (32 bit)
  size_t offset_words;
  size_t received_words;
  bool end_of_file;
  bool end_of_chunk;

  struct crypto_aes_cipher decrypt;
};

struct sp_channel
{
  int id;

  bool data_mode;
  bool spotify_header_received;
  bool in_use;
  bool stop_requested;

  // pipe where we write audio data
  int audio_fd[2];

  struct sp_file file;
};

struct sp_channel_header
{
  uint16_t len;
  uint8_t id;
  uint8_t *data;
  size_t data_len;
};

// Linked list of sessions
struct sp_session
{
  struct sp_credentials credentials;

  enum sp_bitrates bitrate_preferred;

  char country[3]; // Incl null term

  // Where we receive data from Spotify
  bool connected;
  int receive_fd;
  struct event *receive_ev;

  struct sp_channel channels[8];

  // Points to the channel that is streaming, and via this information about
  // the current track is also available
  struct sp_channel *now_streaming_channel;

  bool is_encrypted;
  struct crypto_keys keys;
  struct crypto_cipher encrypt;
  struct crypto_cipher decrypt;

  struct evbuffer *incoming;
  struct evbuffer *in_plain;

  // Buffer holding client hello and ap response, since they are needed for
  // MAC calculation
  struct evbuffer *handshake_packets;
  bool handshake_completed;

  enum sp_msg_type msg_type;

  enum sp_msg_type msg_type_next;
  struct event *msg_next_ev;
  int (*msg_handler)(uint8_t *, size_t, struct sp_session *);

  struct sp_session *next;
};

struct sp_err_map
{
  ErrorCode errorcode;
  const char *errmsg;
};


/* -------------------------------- Globals --------------------------------- */


static struct sp_session *sp_sessions;

static struct sp_callbacks sp_cb;
static void *sp_cb_arg;

static bool sp_initialized;

static pthread_t sp_tid;
static struct event_base *sp_evbase;
static struct commands_base *sp_cmdbase;

static const char *sp_errmsg;

static uint8_t sp_aes_iv[] = { 0x72, 0xe0, 0x67, 0xfb, 0xdd, 0xcb, 0xcf, 0x77, 0xeb, 0xe8, 0xbc, 0x64, 0x3f, 0x63, 0x0d, 0x93 };

static struct sp_err_map sp_login_errors[] = {
  { ERROR_CODE__ProtocolError, "Protocol error" },
  { ERROR_CODE__TryAnotherAP, "Try another access point" },
  { ERROR_CODE__BadConnectionId, "Bad connection ID" },
  { ERROR_CODE__TravelRestriction, "Travel restriction" },
  { ERROR_CODE__PremiumAccountRequired, "Premium account required" },
  { ERROR_CODE__BadCredentials, "Bad credentials" },
  { ERROR_CODE__CouldNotValidateCredentials, "Could not validate credentials" },
  { ERROR_CODE__AccountExists, "Account exists" },
  { ERROR_CODE__ExtraVerificationRequired, "Extra verification required" },
  { ERROR_CODE__InvalidAppKey, "Invalid app key" },
  { ERROR_CODE__ApplicationBanned, "Application banned" },
};

// Forwards
static int
msg_send(enum sp_msg_type type, struct sp_session *session);

static void
msg_receive(int fd, short what, void *arg);


/* ---------------------------------- MOCKING ------------------------------- */

#ifdef DEBUG_MOCK
#include "mock.h"

static int debug_mock_pipe[2];
static int debug_mock_chunks_written = 0;

static int
debug_mock_response(struct sp_session *session)
{
  if (debug_mock_chunks_written == 1)
    return 0; // Only write the chunk once

  switch(session->msg_type)
    {
      case MSG_TYPE_CLIENT_HELLO:
	write(debug_mock_pipe[1], mock_resp_apresponse, sizeof(mock_resp_apresponse));
	break;
      case MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED:
        memcpy(session->decrypt.key, mock_recv_key, sizeof(session->decrypt.key));
	// Spotify will send the replies split in these 6 packets
	write(debug_mock_pipe[1], mock_resp_client_encrypted1, sizeof(mock_resp_client_encrypted1));
	write(debug_mock_pipe[1], mock_resp_client_encrypted2, sizeof(mock_resp_client_encrypted2));
	write(debug_mock_pipe[1], mock_resp_client_encrypted3, sizeof(mock_resp_client_encrypted3));
	write(debug_mock_pipe[1], mock_resp_client_encrypted4, sizeof(mock_resp_client_encrypted4));
	write(debug_mock_pipe[1], mock_resp_client_encrypted5, sizeof(mock_resp_client_encrypted5));
	write(debug_mock_pipe[1], mock_resp_client_encrypted6, sizeof(mock_resp_client_encrypted6));
	break;
      case MSG_TYPE_MERCURY_TRACK_GET:
	write(debug_mock_pipe[1], mock_resp_mercury_req1, sizeof(mock_resp_mercury_req1));
	write(debug_mock_pipe[1], mock_resp_mercury_req2, sizeof(mock_resp_mercury_req2));
	break;
      case MSG_TYPE_AUDIO_KEY_GET:
        memset(session->decrypt.key, 0, sizeof(session->decrypt.key)); // Tells msg_read_one() to skip decryption
	write(debug_mock_pipe[1], mock_resp_aeskey, sizeof(mock_resp_aeskey));
	break;
      case MSG_TYPE_CHUNK_REQUEST:
	write(debug_mock_pipe[1], mock_resp_chunk1, sizeof(mock_resp_chunk1));
	write(debug_mock_pipe[1], mock_resp_chunk2, sizeof(mock_resp_chunk2));
	debug_mock_chunks_written++;
	break;
      default:
	break;
    }

  return 0;
}
#endif


/* ------------------------------- MISC HELPERS ----------------------------- */

int
net_connect(const char *addr, unsigned short port, int type, const char *log_service_name)
{
  struct addrinfo hints = { 0 };
  struct addrinfo *servinfo;
  struct addrinfo *ptr;
  char strport[8];
  int fd;
  int ret;

  sp_cb.logmsg("Connecting to '%s' at %s (port %u)\n", log_service_name, addr, port);

  hints.ai_socktype = (type & (SOCK_STREAM | SOCK_DGRAM)); // filter since type can be SOCK_STREAM | SOCK_NONBLOCK
  hints.ai_family = AF_UNSPEC;

  snprintf(strport, sizeof(strport), "%hu", port);
  ret = getaddrinfo(addr, strport, &hints, &servinfo);
  if (ret < 0)
    {
      sp_cb.logmsg("Could not connect to '%s' at %s (port %u): %s\n", log_service_name, addr, port, gai_strerror(ret));
      return -1;
    }

  for (ptr = servinfo; ptr; ptr = ptr->ai_next)
    {
      fd = socket(ptr->ai_family, type | SOCK_CLOEXEC, ptr->ai_protocol);
      if (fd < 0)
	{
	  continue;
	}

      ret = connect(fd, ptr->ai_addr, ptr->ai_addrlen);
      if (ret < 0 && errno != EINPROGRESS) // EINPROGRESS in case of SOCK_NONBLOCK
	{
	  close(fd);
	  continue;
	}

      break;
    }

  freeaddrinfo(servinfo);

  if (!ptr)
    {
      sp_cb.logmsg("Could not connect to '%s' at %s (port %u): %s\n", log_service_name, addr, port, strerror(errno));
      return -1;
    }

  // net_address_get(ipaddr, sizeof(ipaddr), (union net_sockaddr *)ptr->ai-addr);

  return fd;
}

static int
ap_connect(struct sp_session *session)
{
  char *body;
  json_object *jresponse = NULL;
  json_object *ap_list;
  json_object *ap;
  char *ap_address = NULL;
  char *ap_port;
  int ap_num;
  int ret;

  ret = sp_cb.https_get(&body, SP_AP_RESOLVE_URL);
  if (ret < 0)
    RETURN_ERROR(SP_NOCONNECTION, "Could not connect to access point resolver");

  jresponse = json_tokener_parse(body);
  if (!jresponse)
    RETURN_ERROR(SP_NOCONNECTION, "Could not parse reply from access point resolver");

  if (! (json_object_object_get_ex(jresponse, SP_AP_RESOLVE_KEY, &ap_list) || json_object_get_type(ap_list) == json_type_array))
    RETURN_ERROR(SP_NOCONNECTION, "Unexpected reply from access point resolver");

  ap_num = json_object_array_length(ap_list);
  ap = json_object_array_get_idx(ap_list, rand() % ap_num);
  if (! (ap && json_object_get_type(ap) == json_type_string))
    RETURN_ERROR(SP_NOCONNECTION, "Unexpected reply from access point resolver");

  ap_address = strdup(json_object_get_string(ap));

  if (! (ap_port = strchr(ap_address, ':')))
    RETURN_ERROR(SP_NOCONNECTION, "Unexpected reply from access point resolver, missing port");
  *ap_port = '\0';
  ap_port += 1;

#ifndef DEBUG_MOCK
  session->receive_fd = net_connect(ap_address, atoi(ap_port), SOCK_STREAM, "spotifyc");
  if (session->receive_fd < 0)
    RETURN_ERROR(SP_NOCONNECTION, "Could not connect to access point");
#else
  pipe(debug_mock_pipe);
  session->receive_fd = debug_mock_pipe[0];
#endif

  // Reply event
  session->receive_ev = event_new(sp_evbase, session->receive_fd, EV_READ | EV_PERSIST, msg_receive, session);
  if (!session->receive_ev)
    RETURN_ERROR(SP_OOM, "Could not create receive event");

  event_add(session->receive_ev, NULL);

  free(ap_address);
  json_object_put(jresponse);
  free(body);

  return 0;

 error:
  free(ap_address);
  json_object_put(jresponse);
  free(body);
  return ret;
}

static void
password_zerofree(struct sp_credentials *credentials)
{
  if (!credentials->password)
    return;

  memset(credentials->password, 0, strlen(credentials->password));
  free(credentials->password);
  credentials->password = NULL;
}

/* ----------------------------------- Crypto ------------------------------- */

#define SHA512_DIGEST_LENGTH 64
#define bnum_new(bn)                                            \
    do {                                                        \
        if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) { \
            if (!gcry_check_version("1.5.4"))                   \
                abort();                                        \
            gcry_control(GCRYCTL_DISABLE_SECMEM, 0);            \
            gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);   \
        }                                                       \
        bn = gcry_mpi_new(1);                                   \
    } while (0)
#define bnum_free(bn)                 gcry_mpi_release(bn)
#define bnum_num_bytes(bn)            (gcry_mpi_get_nbits(bn) + 7) / 8
#define bnum_is_zero(bn)              (gcry_mpi_cmp_ui(bn, (unsigned long)0) == 0)
#define bnum_bn2bin(bn, buf, len)     gcry_mpi_print(GCRYMPI_FMT_USG, buf, len, NULL, bn)
#define bnum_bin2bn(bn, buf, len)     gcry_mpi_scan(&bn, GCRYMPI_FMT_USG, buf, len, NULL)
#define bnum_hex2bn(bn, buf)          gcry_mpi_scan(&bn, GCRYMPI_FMT_HEX, buf, 0, 0)
#define bnum_random(bn, num_bits)     gcry_mpi_randomize(bn, num_bits, GCRY_WEAK_RANDOM)
#define bnum_add(bn, a, b)            gcry_mpi_add(bn, a, b)
#define bnum_sub(bn, a, b)            gcry_mpi_sub(bn, a, b)
#define bnum_mul(bn, a, b)            gcry_mpi_mul(bn, a, b)
#define bnum_mod(bn, a, b)            gcry_mpi_mod(bn, a, b)
typedef gcry_mpi_t bnum;
__attribute__((unused)) static void bnum_modexp(bnum bn, bnum y, bnum q, bnum p)
{
  gcry_mpi_powm(bn, y, q, p);
}
__attribute__((unused)) static void bnum_modadd(bnum bn, bnum a, bnum b, bnum m)
{
  gcry_mpi_addm(bn, a, b, m);
}

static const uint8_t generator_bytes[] = { 0x2 };
static const uint8_t prime_bytes[] =
{
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc9, 0x0f, 0xda, 0xa2, 0x21, 0x68, 0xc2, 0x34,
  0xc4, 0xc6, 0x62, 0x8b, 0x80, 0xdc, 0x1c, 0xd1, 0x29, 0x02, 0x4e, 0x08, 0x8a, 0x67, 0xcc, 0x74,
  0x02, 0x0b, 0xbe, 0xa6, 0x3b, 0x13, 0x9b, 0x22, 0x51, 0x4a, 0x08, 0x79, 0x8e, 0x34, 0x04, 0xdd,
  0xef, 0x95, 0x19, 0xb3, 0xcd, 0x3a, 0x43, 0x1b, 0x30, 0x2b, 0x0a, 0x6d, 0xf2, 0x5f, 0x14, 0x37,
  0x4f, 0xe1, 0x35, 0x6d, 0x6d, 0x51, 0xc2, 0x45, 0xe4, 0x85, 0xb5, 0x76, 0x62, 0x5e, 0x7e, 0xc6,
  0xf4, 0x4c, 0x42, 0xe9, 0xa6, 0x3a, 0x36, 0x20, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static int
crypto_keys_set(struct crypto_keys *keys)
{
  bnum generator;
  bnum prime;
  bnum private_key;
  bnum public_key;

  bnum_bin2bn(generator, generator_bytes, sizeof(generator_bytes));
  bnum_bin2bn(prime, prime_bytes, sizeof(prime_bytes));
  bnum_new(private_key);
  bnum_new(public_key);

//  bnum_random(private_key, 8 * (sizeof(keys->private_key) - 1)); // Not sure why it is 95 bytes?
  bnum_random(private_key, 8 * sizeof(keys->private_key));

  bnum_modexp(public_key, generator, private_key, prime);

  memset(keys, 0, sizeof(struct crypto_keys));
  bnum_bn2bin(private_key, keys->private_key, sizeof(keys->private_key));
  bnum_bn2bin(public_key, keys->public_key, sizeof(keys->public_key));

  bnum_free(generator);
  bnum_free(prime);
  bnum_free(private_key);
  bnum_free(public_key);

  return 0;
}

static void
crypto_shared_secret(uint8_t **shared_secret_bytes, size_t *shared_secret_bytes_len,
                     uint8_t *private_key_bytes, size_t private_key_bytes_len,
                     uint8_t *server_key_bytes, size_t server_key_bytes_len)
{
  bnum private_key;
  bnum server_key;
  bnum prime;
  bnum shared_secret;

  bnum_bin2bn(private_key, private_key_bytes, private_key_bytes_len);
  bnum_bin2bn(server_key, server_key_bytes, server_key_bytes_len);
  bnum_bin2bn(prime, prime_bytes, sizeof(prime_bytes));
  bnum_new(shared_secret);

  bnum_modexp(shared_secret, server_key, private_key, prime);

  *shared_secret_bytes_len = bnum_num_bytes(shared_secret);
  *shared_secret_bytes = malloc(*shared_secret_bytes_len);
  bnum_bn2bin(shared_secret, *shared_secret_bytes, *shared_secret_bytes_len);

  bnum_free(private_key);
  bnum_free(server_key);
  bnum_free(prime);
  bnum_free(shared_secret);
}

// Calculates challenge and send/receive keys. The challenge is allocated,
// caller must free
static int
crypto_challenge(uint8_t **challenge, size_t *challenge_len,
                 uint8_t *send_key, size_t send_key_len,
                 uint8_t *recv_key, size_t recv_key_len,
                 uint8_t *packets, size_t packets_len,
                 uint8_t *shared_secret, size_t shared_secret_len)
{
  gcry_mac_hd_t hd = NULL;
  uint8_t data[0x64];
  uint8_t i;
  size_t offset;
  size_t len;

  if (gcry_mac_open(&hd, GCRY_MAC_HMAC_SHA1, 0, NULL) != GPG_ERR_NO_ERROR)
    goto error;

  if (gcry_mac_setkey(hd, shared_secret, shared_secret_len) != GPG_ERR_NO_ERROR)
    goto error;

  offset = 0;
  for (i = 1; i <= 6; i++)
    {
      gcry_mac_write(hd, packets, packets_len);
      gcry_mac_write(hd, &i, sizeof(i));
      len = sizeof(data) - offset;
      gcry_mac_read(hd, data + offset, &len);
      offset += len;
      gcry_mac_reset(hd);
    }

  gcry_mac_close(hd);
  hd = NULL;

  assert(send_key_len == 32);
  assert(recv_key_len == 32);

  memcpy(send_key, data + 20, send_key_len);
  memcpy(recv_key, data + 52, recv_key_len);

  // Calculate challenge
  if (gcry_mac_open(&hd, GCRY_MAC_HMAC_SHA1, 0, NULL) != GPG_ERR_NO_ERROR)
    goto error;

  if (gcry_mac_setkey(hd, data, 20) != GPG_ERR_NO_ERROR)
    goto error;

  gcry_mac_write(hd, packets, packets_len);

  *challenge_len = gcry_mac_get_algo_maclen(GCRY_MAC_HMAC_SHA1);
  *challenge = malloc(*challenge_len);
  gcry_mac_read(hd, *challenge, challenge_len);
  gcry_mac_close(hd);

  return 0;

 error:
  if (hd)
    gcry_mac_close(hd);
  return -1;
}

// Inplace encryption, buf_len must be larger than plain_len so that the mac
// can be added
static ssize_t
crypto_encrypt(uint8_t *buf, size_t buf_len, size_t plain_len, struct crypto_cipher *cipher)
{
  uint32_t nonce;
  uint8_t mac[4];
  size_t encrypted_len;

  encrypted_len = plain_len + sizeof(mac);
  if (encrypted_len > buf_len)
    return -1;

  shn_key(&cipher->shannon, cipher->key, sizeof(cipher->key));

  nonce = htobe32(cipher->nonce);
  shn_nonce(&cipher->shannon, (uint8_t *)&nonce, sizeof(nonce));

  shn_encrypt(&cipher->shannon, buf, plain_len);
  shn_finish(&cipher->shannon, mac, sizeof(mac));

  memcpy(buf + plain_len, mac, sizeof(mac));

  cipher->nonce++;

  return encrypted_len;
}

static size_t
payload_len_get(uint8_t *header)
{
  uint16_t be;
  memcpy(&be, header + 1, sizeof(be));
  return (size_t)be16toh(be);
}

// *encrypted will consist of a header (3 bytes, encrypted), payload length (2
// bytes, encrypted, BE), the encrypted payload and then the mac (4 bytes, not
// encrypted). The return will be the number of bytes decrypted (incl mac if a
// whole packet was decrypted). Zero means not enough data for a packet.
static ssize_t
crypto_decrypt(uint8_t *encrypted, size_t encrypted_len, struct crypto_cipher *cipher)
{
  uint32_t nonce;
  uint8_t mac[4];
  size_t header_len = sizeof(cipher->last_header);
  size_t payload_len;

  sp_cb.logmsg("Decrypting %zu bytes with nonce %u\n", encrypted_len, cipher->nonce);
//  sp_cb.hexdump("Key\n", cipher->key, sizeof(cipher->key));
//  sp_cb.hexdump("Encrypted\n", encrypted, encrypted_len);

  // In case we didn't even receive the basics, header and mac, then return.
  if (encrypted_len < header_len + sizeof(mac))
    {
      sp_cb.logmsg("Waiting for %zu header bytes, have %zu\n", header_len + sizeof(mac), encrypted_len);
      return 0;
    }

  // Will be zero if this is the first pass
  payload_len = payload_len_get(cipher->last_header);
  if (!payload_len)
    {
      shn_key(&cipher->shannon, cipher->key, sizeof(cipher->key));

      nonce = htobe32(cipher->nonce);
      shn_nonce(&cipher->shannon, (uint8_t *)&nonce, sizeof(nonce));

      // Decrypt header to get the size, save it in case another pass will be
      // required
      shn_decrypt(&cipher->shannon, encrypted, header_len);
      memcpy(cipher->last_header, encrypted, header_len);

      payload_len = payload_len_get(cipher->last_header);

      sp_cb.logmsg("Payload len is %zu\n", payload_len);
      sp_cb.hexdump("Decrypted header\n", encrypted, header_len);
    }

  // At this point the header is already decrypted, so now decrypt the payload
  encrypted += header_len;
  encrypted_len -= header_len + sizeof(mac);

  // Not enough data for decrypting the entire packet
  if (payload_len > encrypted_len)
    {
      sp_cb.logmsg("Waiting for %zu payload bytes, have %zu\n", payload_len, encrypted_len);
      return 0;
    }

  shn_decrypt(&cipher->shannon, encrypted, payload_len);

//  sp_cb.hexdump("Decrypted payload\n", encrypted, payload_len);

  shn_finish(&cipher->shannon, mac, sizeof(mac));
//  sp_cb.hexdump("mac in\n", encrypted + payload_len, sizeof(mac));
//  sp_cb.hexdump("mac our\n", mac, sizeof(mac));
  if (memcmp(mac, encrypted + payload_len, sizeof(mac)) != 0)
    {
      sp_cb.logmsg("MAC VALIDATION FAILED\n");
      memset(cipher->last_header, 0, header_len);
      return -1;
    }

  cipher->nonce++;
  memset(cipher->last_header, 0, header_len);

  return header_len + payload_len + sizeof(mac);
}

static void
crypto_aes_free(struct crypto_aes_cipher *cipher)
{
  if (!cipher || !cipher->aes)
    return;

  gcry_cipher_close(cipher->aes);
}

static int
crypto_aes_new(struct crypto_aes_cipher *cipher, uint8_t *key, size_t key_len, uint8_t *iv, size_t iv_len, const char **errmsg)
{
  gcry_error_t err;

  err = gcry_cipher_open(&cipher->aes, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_CTR, 0);
  if (err)
    {
      *errmsg = "Error initialising AES 128 CTR decryption";
      goto error;
    }

  err = gcry_cipher_setkey(cipher->aes, key, key_len);
  if (err)
    {
      *errmsg = "Could not set key for AES 128 CTR";
      goto error;
    }

  err = gcry_cipher_setctr(cipher->aes, iv, iv_len);
  if (err)
    {
      *errmsg = "Could not set iv for AES 128 CTR";
      return -1;
    }

  memcpy(cipher->aes_iv, iv, iv_len);

  return 0;

 error:
  crypto_aes_free(cipher);
  return -1;
}

/* For future use
static int
crypto_aes_seek(struct crypto_aes_cipher *cipher, size_t seek, const char **errmsg)
{
  gcry_error_t err;
  uint64_t be64;
  uint64_t ctr;
  uint8_t iv[16];
  size_t iv_len;
  size_t num_blocks;
  size_t offset;

  iv_len = gcry_cipher_get_algo_blklen(GCRY_CIPHER_AES128);

  assert(iv_len == sizeof(iv));

  memcpy(iv, cipher->aes_iv, iv_len);
  num_blocks = seek / iv_len;
  offset = seek % iv_len;

  // Advance the block counter
  memcpy(&be64, iv + iv_len / 2, iv_len / 2);
  ctr = be64toh(be64);
  ctr += num_blocks;
  be64 = htobe64(ctr);
  memcpy(iv + iv_len / 2, &be64, iv_len / 2);

  err = gcry_cipher_setctr(cipher->aes, iv, iv_len);
  if (err)
    {
      *errmsg = "Could not set iv for AES 128 CTR";
      return -1;
    }

  // Advance if the seek is into a block. iv is used because we have it already,
  // it could be any buffer as long as it big enough
  err = gcry_cipher_decrypt(cipher->aes, iv, offset, NULL, 0);
  if (err)
    {
      *errmsg = "Error CTR offset while seeking";
      return -1;
    }

  return 0;
}
*/

static int
crypto_aes_decrypt(uint8_t *encrypted, size_t encrypted_len, struct crypto_aes_cipher *cipher, const char **errmsg)
{
  gcry_error_t err;

  err = gcry_cipher_decrypt(cipher->aes, encrypted, encrypted_len, NULL, 0);
  if (err)
    {
      *errmsg = "Error CTR decrypting";
      return -1;
    }

  return 0;
}


/* -------------------------------- Session --------------------------------- */

static void
session_free(struct sp_session *session)
{
  if (!session)
    return;

  if (session->receive_fd > 0)
    close(session->receive_fd);

  if (session->receive_ev)
    event_free(session->receive_ev);

  if (session->incoming)
    evbuffer_free(session->incoming);

  if (session->handshake_packets)
    evbuffer_free(session->handshake_packets);

  if (session->msg_next_ev)
    event_free(session->msg_next_ev);

  free(session->credentials.username);
  password_zerofree(&session->credentials);

  free(session);
}

static void
session_cleanup(struct sp_session *session)
{
  struct sp_session *s;

  if (session == sp_sessions)
    sp_sessions = session->next;
  else
    {
      for (s = sp_sessions; s && (s->next != session); s = s->next)
	; /* EMPTY */

      if (s)
	s->next = session->next;
    }

  session_free(session);
}

static int
session_check(struct sp_session *session)
{
  struct sp_session *s;

  for (s = sp_sessions; s; s = s->next)
    {
      if (s == session)
	return 0;
    }

  return -1;
}

static struct sp_session *
session_find_by_fd(int fd)
{
  struct sp_session *s;

  for (s = sp_sessions; s; s = s->next)
    {
      if (s->now_streaming_channel && s->now_streaming_channel->audio_fd[0] == fd)
	return s;
    }

  return NULL;
}

/* -------------------------------- Channels -------------------------------- */

/*
Here is my current understanding of the channel concept:

1. A channel is established for retrieving chunks of audio. A channel is not a
   separate connection, all the traffic goes via the same Shannon-encrypted tcp
   connection as the rest.
2. It depends on the cmd whether a channel is used. CmdStreamChunk,
   CmdStreamChunkRes, CmdChannelError, CmdChannelAbort use channels. A channel
   is identified with a uint16_t, which is the first 2 bytes of these packets.
3. A channel is established with CmdStreamChunk where receiver picks channel id.
   Spotify responds with CmdStreamChunkRes that initially has some headers after
   the channel id. The headers are "reverse tlv": uint16_t header length,
   uint8_t header id, uint8_t header_data[]. The length includes the id length.
4. After the headers are sent the channel switches to data mode. This is
   signalled by a header length of 0. In data mode Spotify sends the requested
   chunks of audio (CmdStreamChunkRes) which have the audio right after the
   channel id prefix. The audio is AES encrypted with a per-file key. An empty
   CmdStreamChunkRes indicates the end. The caller can then make a new
   CmdStreamChunk requesting the next data.
5. For Ogg, the first 167 bytes of audio is a special Spotify header.
6. The channel can presumably be reset with CmdChannelAbort (?)
*/

static int
channel_id_check(uint32_t channel_id, struct sp_session *session)
{
  if (channel_id > sizeof(session->channels)/sizeof(session->channels)[0])
    return -1;

  if (!session->channels[channel_id].in_use)
    return -1;

  return 0;
}

static void
channel_reset(struct sp_channel *channel)
{
  if (!channel)
    return;

  if (channel->audio_fd[1] >= 0)
    close(channel->audio_fd[1]);

  memset(channel, 0, sizeof(struct sp_channel));
}

static int
channel_begin(struct sp_channel **channel, struct sp_session *session)
{
  uint16_t i = SP_DEFAULT_CHANNEL;

  channel_reset(&session->channels[i]);
  session->channels[i].id = i;
  session->channels[i].in_use = true;
  session->channels[i].audio_fd[1] = -1;

  *channel = &session->channels[i];

  return 0;
}

// Always returns number of byte read so caller can advance read pointer. If
// header->len == 0 is returned it means that there are no more headers, and
// caller should switch the channel to data mode.
static ssize_t
channel_header_parse(struct sp_channel_header *header, uint8_t *data, size_t data_len)
{
  uint8_t *ptr;
  uint16_t be;

  if (data_len < sizeof(be))
    return -1;

  ptr = data;
  memset(header, 0, sizeof(struct sp_channel_header));

  memcpy(&be, ptr, sizeof(be));
  header->len = be16toh(be);
  ptr += sizeof(be);

  if (header->len == 0)
    goto done; // No more headers
  else if (data_len < header->len + sizeof(be))
    return -1;

  header->id = ptr[0];
  ptr += 1;

  header->data = ptr;
  header->data_len = header->len - 1;
  ptr += header->data_len;

  assert(ptr - data == header->len + sizeof(be));

 done:
  return header->len + sizeof(be);
}

static void
channel_header_handle(struct sp_channel *channel, struct sp_channel_header *header)
{
  uint32_t be32;

  sp_cb.hexdump("Received header\n", header->data, header->data_len);

  // The only header that librespot seems to use is 0x3, which is the audio file
  // size in words
  if (header->id == 0x3)
    {
      if (header->data_len != sizeof(be32))
	{
	  sp_cb.logmsg("Unexpected header length for header id 0x3\n");
	  return;
	}

      memcpy(&be32, header->data, sizeof(be32));
      channel->file.len_words = be32toh(be32);

      sp_cb.logmsg("File size is %zu\n", channel->file.len_words);
    }
}

static ssize_t
channel_header_trailer_read(uint16_t channel_id, uint8_t *msg, size_t msg_len, struct sp_session *session)
{
  struct sp_channel *channel = &session->channels[channel_id];
  struct sp_channel_header header;
  ssize_t parsed_len;
  ssize_t consumed_len;
  int ret;

  if (msg_len == 0)
    {
      channel->file.end_of_chunk = true;
      channel->file.end_of_file = (channel->file.received_words >= channel->file.len_words);
      channel->data_mode = false; // In preparation for next chunk's header

      return 0;
    }
  else if (channel->data_mode)
    {
      return 0;
    }

  for (consumed_len = 0; msg_len > 0; msg += parsed_len, msg_len -= parsed_len)
    {
      parsed_len = channel_header_parse(&header, msg, msg_len);
      if (parsed_len < 0)
	RETURN_ERROR(SP_INVALID, "Invalid channel header");

      consumed_len += parsed_len;

      if (header.len == 0)
	{
	  channel->data_mode = true;
	  break; // All headers read
	}

      channel_header_handle(channel, &header);
    }

  return consumed_len;

 error:
  return ret;
}

static ssize_t
channel_data_read(uint16_t channel_id, uint8_t *msg, size_t msg_len, struct sp_session *session)
{
  struct sp_channel *channel = &session->channels[channel_id];
  const char *errmsg;
  ssize_t wrote;
  int ret;

  assert (msg_len % 4 == 0);

  channel->file.received_words += msg_len / 4;

  ret = crypto_aes_decrypt(msg, msg_len, &channel->file.decrypt, &errmsg);
  if (ret < 0)
    RETURN_ERROR(SP_DECRYPTION, errmsg);

  // Skip Spotify header
  if (!channel->spotify_header_received)
    {
      if (msg_len < SP_OGG_HEADER_LEN)
	RETURN_ERROR(SP_INVALID, "Invalid data received");

      channel->spotify_header_received = true;

      msg += SP_OGG_HEADER_LEN;
      msg_len -= SP_OGG_HEADER_LEN;
    }

  if (channel->stop_requested)
    return 0;

  wrote = write(channel->audio_fd[1], msg, msg_len);

  if (wrote != msg_len)
    RETURN_ERROR(SP_WRITE, "Could not write to output");

  return 0;

 error:
  return ret;
}

static int
channel_msg_read(uint16_t *channel_id, uint8_t *msg, size_t msg_len, struct sp_session *session)
{
  uint16_t be;
  ssize_t consumed_len;
  int ret;

  if (msg_len < sizeof(be))
    RETURN_ERROR(SP_INVALID, "Chunk response is too small");

  memcpy(&be, msg, sizeof(be));
  *channel_id = be16toh(be);

  ret = channel_id_check(*channel_id, session);
  if (ret < 0)
    {
      sp_cb.hexdump("Message with unknown channel\n", msg, msg_len);
      RETURN_ERROR(SP_INVALID, "Could not recognize channel in chunk response");
    }

  msg += sizeof(be);
  msg_len -= sizeof(be);

  // Will set data_mode, end_of_file and end_of_chunk as appropriate
  consumed_len = channel_header_trailer_read(*channel_id, msg, msg_len, session);
  if (consumed_len < 0)
    RETURN_ERROR((int)consumed_len, sp_errmsg);

  msg += consumed_len;
  msg_len -= consumed_len;

  if (!session->channels[*channel_id].data_mode || !(msg_len > 0))
    return 0; // Not in data mode or no data to read

  consumed_len = channel_data_read(*channel_id, msg, msg_len, session);
  if (consumed_len < 0)
    RETURN_ERROR((int)consumed_len, sp_errmsg);

  return 0;

 error:
  return ret;
}


/* ------------------------------ Raw packets ------------------------------- */

static ssize_t
packet_make_encrypted(uint8_t *out, size_t out_len, uint8_t cmd, const uint8_t *payload, size_t payload_len, struct crypto_cipher *cipher)
{
  uint16_t be;
  size_t plain_len;
  ssize_t pkt_len;
  uint8_t *ptr;

  be = htobe16(payload_len);

  plain_len = sizeof(cmd) + sizeof(be) + payload_len;
  if (plain_len > out_len)
    {
      sp_cb.logmsg("Buffer too small\n");
      goto error;
    }

  ptr = out;
  memcpy(ptr, &cmd, sizeof(cmd));
  ptr += sizeof(cmd);
  memcpy(ptr, &be, sizeof(be));
  ptr += sizeof(be);
  memcpy(ptr, payload, payload_len);

//  sp_cb.hexdump("Encrypting packet\n", out, plain_len);

  pkt_len = crypto_encrypt(out, out_len, plain_len, cipher);
  if (pkt_len < 9)
    {
      sp_cb.logmsg("Could not encrypt\n");
      goto error;
    }

  return pkt_len;

 error:
  return -1;
}

static ssize_t
packet_make_plain(uint8_t *out, size_t out_len, uint8_t *protobuf, size_t protobuf_len, bool with_version_header)
{
  const uint8_t version_header[] = { 0x00, 0x04 };
  size_t header_len;
  ssize_t len;
  uint32_t be;

  header_len = with_version_header ? sizeof(be) + sizeof(version_header) : sizeof(be);

  len = header_len + protobuf_len;
  if (len > out_len)
    return -1;

  if (with_version_header)
    memcpy(out, version_header, sizeof(version_header));

  be = htobe32(len);
  memcpy(out + header_len - sizeof(be), &be, sizeof(be)); // Last bytes of the header is the length
  memcpy(out + header_len, protobuf, protobuf_len);

  return len;
}


/* ---------------------------- Mercury messages ---------------------------- */

static void
mercury_free(struct sp_mercury *mercury, int content_only)
{
  int i;

  if (!mercury)
    return;

  free(mercury->uri);
  free(mercury->method);
  free(mercury->content_type);

  for (i = 0; i < mercury->parts_num; i++)
    {
      free(mercury->parts[i].data);

      if (mercury->parts[i].track)
	track__free_unpacked(mercury->parts[i].track, NULL);
    }

  if (content_only)
    memset(mercury, 0, sizeof(struct sp_mercury));
  else
    free(mercury);
}

static int
mercury_parse(struct sp_mercury *mercury, uint8_t *payload, size_t payload_len)
{
  Header *header;
  uint8_t *ptr;
  uint16_t be;
  uint64_t be64;
  uint16_t seq_len;
  uint16_t header_len;
  size_t required_len; // For size checking
  uint8_t flags;
  int i;

  ptr = payload;

  required_len = sizeof(be);
  if (required_len > payload_len)
    goto error_length; // Length check 1

  memcpy(&be, ptr, sizeof(be));
  seq_len = be16toh(be);
  ptr += sizeof(be); // 1: length += sizeof(be)

  required_len += seq_len + sizeof(flags) + sizeof(be) + sizeof(be);
  if (required_len > payload_len || seq_len != sizeof(be64))
    goto error_length; // Length check 2

  memcpy(&be64, ptr, sizeof(be64));
  mercury->seq = be64toh(be64);
  ptr += seq_len; // 2: length += seq_len

  memcpy(&flags, ptr, sizeof(flags));
  ptr += sizeof(flags); // 2: length += sizeof(flags)

  memcpy(&be, ptr, sizeof(be));
  mercury->parts_num = be16toh(be) - 1; // What's the deal with the 1...?
  ptr += sizeof(be); // 2: length += sizeof(be)

  if (mercury->parts_num > SP_MERCURY_MAX_PARTS)
    return -1;

  memcpy(&be, ptr, sizeof(be));
  header_len = be16toh(be);
  ptr += sizeof(be); // 2: length += sizeof(be)

  required_len += header_len;
  if (required_len > payload_len)
    goto error_length; // Length check 3

  header = header__unpack(NULL, header_len, ptr);
  if (!header)
    goto error_length;
  ptr += header_len; // 3: length += header_len

  mercury->uri = header->uri ? strdup(header->uri) : NULL;
  mercury->method = header->method ? strdup(header->method) : NULL;
  mercury->content_type = header->content_type ? strdup(header->content_type) : NULL;

  for (i = 0; i < mercury->parts_num; i++)
    {
      required_len += sizeof(be);
      if (required_len > payload_len)
	goto error_length; // Length check 4

      memcpy(&be, ptr, sizeof(be));
      mercury->parts[i].len = be16toh(be);
      ptr += sizeof(be); // 4: length += sizeof(be)

      required_len += mercury->parts[i].len;
      if (required_len > payload_len)
	goto error_length; // Length check 5

      mercury->parts[i].data = malloc(mercury->parts[i].len);
      memcpy(mercury->parts[i].data, ptr, mercury->parts[i].len);
      ptr += mercury->parts[i].len; // 5: length += mercury->parts[i].len

      mercury->parts[i].track = track__unpack(NULL, mercury->parts[i].len, mercury->parts[i].data);
    }

  header__free_unpacked(header, NULL);

  assert(ptr == payload + required_len);

  return 0;

 error_length:
  mercury_free(mercury, 1);
  return -1;
}

// Returns true if format of a is preferred over b (and is valid). According to
// librespot comment most podcasts are 96 kbit.
static bool
format_is_preferred(AudioFile *a, AudioFile *b, enum sp_bitrates bitrate_preferred)
{
  if (a->format != AUDIO_FILE__FORMAT__OGG_VORBIS_96 &&
      a->format != AUDIO_FILE__FORMAT__OGG_VORBIS_160 &&
      a->format != AUDIO_FILE__FORMAT__OGG_VORBIS_320)
    return false;

  if (!b)
    return true; // Any format is better than no format

  switch (bitrate_preferred)
    {
      case SP_BITRATE_96:
	return (a->format < b->format); // Prefer lowest
      case SP_BITRATE_160:
	if (b->format == AUDIO_FILE__FORMAT__OGG_VORBIS_160)
	  return false;
	else if (a->format == AUDIO_FILE__FORMAT__OGG_VORBIS_160)
	  return true;
	else
	  return (a->format < b->format); // Prefer lowest
      case SP_BITRATE_320:
	return (a->format > b->format); // Prefer highest
    }

  return false;
}

static int
file_select(uint8_t *out, size_t out_len, Track *track, enum sp_bitrates bitrate_preferred)
{
  AudioFile *selected = NULL;
  AudioFile *file;
  int i;

  for (i = 0; i < track->n_file; i++)
    {
      file = track->file[i];

      if (!file->has_file_id || !file->has_format || file->file_id.len != out_len)
	continue;

      if (format_is_preferred(file, selected, bitrate_preferred))
	selected = file;
    }

  if (!selected)
    return -1;

  memcpy(out, selected->file_id.data, selected->file_id.len);

  return 0;
}


/* --------------------------- Incoming messages ---------------------------- */

static int
response_client_hello(uint8_t *msg, size_t msg_len, struct sp_session *session)
{
  APResponseMessage *apresponse;
  size_t header_len = 4; // TODO make a define
  int ret;

  apresponse = apresponse_message__unpack(NULL, msg_len - header_len, msg + header_len);
  if (!apresponse)
    RETURN_ERROR(SP_INVALID, "Could not unpack response from access point");

  // TODO check APLoginFailed

  // Not sure if necessary
  if (!apresponse->challenge || !apresponse->challenge->login_crypto_challenge)
    RETURN_ERROR(SP_INVALID, "Missing challenge in response from access point");

  crypto_shared_secret(
    &session->keys.shared_secret, &session->keys.shared_secret_len,
    session->keys.private_key, sizeof(session->keys.private_key),
    apresponse->challenge->login_crypto_challenge->diffie_hellman->gs.data, apresponse->challenge->login_crypto_challenge->diffie_hellman->gs.len);

  apresponse_message__free_unpacked(apresponse, NULL);

  session->handshake_completed = true;

  return 1; // 1 will make msg_receive continue

 error:
  return ret;
}

static int
response_apwelcome(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  APWelcome *apwelcome;
  int ret;

  apwelcome = apwelcome__unpack(NULL, payload_len, payload);

  if (apwelcome->reusable_auth_credentials_type == AUTHENTICATION_TYPE__AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS)
    {
      if (apwelcome->reusable_auth_credentials.len > sizeof(session->credentials.stored_cred))
	RETURN_ERROR(SP_INVALID, "Credentials from Spotify longer than expected");

      session->credentials.stored_cred_len = apwelcome->reusable_auth_credentials.len;
      memcpy(session->credentials.stored_cred, apwelcome->reusable_auth_credentials.data, session->credentials.stored_cred_len);
    }

  apwelcome__free_unpacked(apwelcome, NULL);

  sp_cb.logged_in(session, sp_cb_arg, &session->credentials);

  return 0;

 error:
  return ret;
}

static int
response_aplogin_failed(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  APLoginFailed *aplogin_failed;

  aplogin_failed = aplogin_failed__unpack(NULL, payload_len, payload);

  sp_errmsg = "(unknown login error)";

  for (int i = 0; i < sizeof(sp_login_errors); i++)
    {
      if (sp_login_errors[i].errorcode != aplogin_failed->error_code)
	continue;

      sp_errmsg = sp_login_errors[i].errmsg;
      break;
    }

  aplogin_failed__free_unpacked(aplogin_failed, NULL);

  return SP_LOGINFAILED;
}

static int
response_chunk_res(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  struct sp_channel *channel;
  uint16_t channel_id;
  int ret;

  ret = channel_msg_read(&channel_id, payload, payload_len, session);
  if (ret < 0)
    return ret;

  channel = &session->channels[channel_id];

  if (channel->file.end_of_file || (channel->stop_requested && channel->file.end_of_chunk))
    {
      sp_cb.track_closed(session, sp_cb_arg, channel->audio_fd[0]);
      channel_reset(channel);
    }
  else if (channel->file.end_of_chunk)
    {
      // Will make msg_receive -> msg_next_cb trigger request for a new chunk
      channel->file.offset_words += SP_CHUNK_LEN_WORDS;
      channel->file.end_of_chunk = false;
      session->msg_type_next = MSG_TYPE_CHUNK_REQUEST;
    }

  return 1;
}

static int
response_aes_key(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  struct sp_channel *channel;
  const char *errmsg;
  uint32_t be32;
  uint32_t channel_id;
  int ret;

  // Payload is expected to consist of seq (uint32 BE), and key (16 bytes)
  if (payload_len != sizeof(be32) + 16)
    RETURN_ERROR(SP_DECRYPTION, "Unexpected key received");

  memcpy(&be32, payload, sizeof(be32));
  channel_id = be32toh(be32);

  ret = channel_id_check(channel_id, session);
  if (ret < 0)
    RETURN_ERROR(SP_INVALID, "Unexpected channel received");

  channel = &session->channels[channel_id];

  memcpy(channel->file.track_key, payload + 4, 16);

  ret = crypto_aes_new(&channel->file.decrypt, channel->file.track_key, sizeof(channel->file.track_key), sp_aes_iv, sizeof(sp_aes_iv), &errmsg);
  if (ret < 0)
    RETURN_ERROR(SP_DECRYPTION, errmsg);

  // Caller reads from audio_fd[0]
  sp_cb.track_opened(session, sp_cb_arg, channel->audio_fd[0]);

  return 0;

 error:
  return ret;
}

static int
response_aes_key_error(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  sp_errmsg = "Did not get key for decrypting track";

  return SP_DECRYPTION;
}

static int
response_mercury_req(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  struct sp_mercury mercury = { 0 };
  struct sp_channel *channel;
  uint32_t channel_id;
  int ret;

  mercury_parse(&mercury, payload, payload_len);
  if (mercury.parts_num != 1 || !mercury.parts[0].track)
    RETURN_ERROR(SP_INVALID, "Unexpected track response from Spotify");

  channel_id = (uint32_t)mercury.seq;

  ret = channel_id_check(channel_id, session);
  if (ret < 0)
    RETURN_ERROR(SP_INVALID, "Unexpected channel received");

  channel = &session->channels[channel_id];

  channel->file.track = mercury.parts[0].track;

  ret = file_select(channel->file.id, sizeof(channel->file.id), channel->file.track, session->bitrate_preferred);
  if (ret < 0)
    RETURN_ERROR(SP_INVALID, "Could not find track data");

  // TODO memleaking, can we free track here?

  return 1; // Continue to get AES key

 error:
  return ret;
}

static int
response_dummy(uint8_t *msg, size_t msg_len, struct sp_session *session)
{
  sp_errmsg = "Unexpected data received, aborting";

  return SP_INVALID;
}

// Returns 1 if the received message is the one expected to continue start-up
// sequence, 0 if we should wait, -1 on error
static int
response_generic(uint8_t *msg, size_t msg_len, struct sp_session *session)
{
  enum sp_cmd_type cmd;
  uint8_t *payload;
  size_t payload_len;
  int ret;

  cmd = msg[0];
  payload = msg + 3;
  payload_len = msg_len - 3 - 4;

  switch (cmd)
    {
      case CmdAPWelcome:
	ret = response_apwelcome(payload, payload_len, session);
	break;
      case CmdAuthFailure:
	ret = response_aplogin_failed(payload, payload_len, session);
	break;
      case CmdPing:
	ret = msg_send(MSG_TYPE_PONG, session);
	break;
      case CmdStreamChunkRes:
	ret = response_chunk_res(payload, payload_len, session); // Will return 1 if 
	break;
      case CmdCountryCode:
	memcpy(session->country, payload, sizeof(session->country) - 1);
	ret = 0;
	break;
      case CmdAesKey:
	ret = response_aes_key(payload, payload_len, session);
	break;
      case CmdAesKeyError:
	ret = response_aes_key_error(payload, payload_len, session);
	break;
      case CmdMercuryReq:
	ret = response_mercury_req(payload, payload_len, session);
	break;
      case CmdLegacyWelcome: // 0 bytes, ignored by librespot
      case CmdSecretBlock: // ignored by librespot
      case 0x50: // XML received after login, ignored by librespot
      case CmdLicenseVersion: // ignored by librespot
      default:
	ret = 0;
    }

  return ret; // If ret is 1 then msg_receive may continue to next request (if there is one)
}

static void
msg_next_cb(int fd, short what, void *arg)
{
  struct sp_session *session = arg;
  enum sp_msg_type type;

  if (session->msg_type_next != MSG_TYPE_NONE)
    {
      sp_cb.logmsg(">>> msg_next >>>\n");

      type = session->msg_type_next;
      session->msg_type_next = MSG_TYPE_NONE;
      msg_send(type, session);
    }
}

static int
msg_read_one(uint8_t **out, size_t *out_len, uint8_t *in, size_t in_len, struct sp_session *session)
{
  uint32_t be32;
  ssize_t msg_len;
  int ret;

#ifdef DEBUG_MOCK
  if (session->is_encrypted && !session->decrypt.key[0] && !session->decrypt.key[1])
    {
      uint16_t be;
      memcpy(&be, in + 1, sizeof(be));
      msg_len = be16toh(be) + 7;
      if (msg_len > in_len)
	return 0;

      *out = malloc(msg_len);
      *out_len = msg_len;
      evbuffer_remove(session->incoming, *out, msg_len);

      return msg_len;
    }
#endif

  if (session->is_encrypted)
    {
      msg_len = crypto_decrypt(in, in_len, &session->decrypt);
      if (msg_len < 0)
	RETURN_ERROR(SP_DECRYPTION, "Decryption error");
      if (msg_len == 0)
	return 0; // Wait for more data
    }
  else
    {
      if (in_len < sizeof(be32))
	return 0; // Wait for more data, size header is incomplete

      memcpy(&be32, in, sizeof(be32));
      msg_len = be32toh(be32);
      if (msg_len < 0)
	RETURN_ERROR(SP_INVALID, "Invalid message length");
      if (msg_len > in_len)
	return 0; // Wait for more data

      if (!session->handshake_completed)
	evbuffer_add(session->handshake_packets, in, msg_len);
    }

  // At this point we have a complete, decrypted message.
  *out = malloc(msg_len);
  *out_len = msg_len;
  evbuffer_remove(session->incoming, *out, msg_len);

  return msg_len;

 error:
  return ret;
}

static void
msg_receive(int fd, short what, void *arg)
{
  struct sp_session *session = arg;
  uint8_t *in;
  size_t in_len;
  uint8_t *msg;
  size_t msg_len;
  bool proceed_next = false;
  int ret;

  ret = evbuffer_read(session->incoming, fd, -1);
  if (ret < 0)
    RETURN_ERROR(SP_NOCONNECTION, "Error reading Spotify data");
  if (ret == 0)
    goto wait;

  sp_cb.logmsg("Read data len %d\n", ret);

  // Incoming may be encrypted and may consist of multiple messages
  while ((in_len = evbuffer_get_length(session->incoming)))
    {
      in = evbuffer_pullup(session->incoming, -1);

      ret = msg_read_one(&msg, &msg_len, in, in_len, session);
      if (ret == 0)
	goto wait;
      else if (ret < 0)
	goto error;

      if (msg_len < 128)
	sp_cb.hexdump("Received message\n", msg, msg_len);
      else
	sp_cb.hexdump("Received message (truncated)\n", msg, 128);

      ret = session->msg_handler(msg, msg_len, session);
      free(msg);
      if (ret < 0)
	goto error;
      else if (ret == 1)
	proceed_next = true;

      sp_cb.logmsg("ret %d, next %d\n", ret, session->msg_type_next);
    }

 wait:
  if (proceed_next)
    msg_next_cb(fd, what, arg);

  return;

 error:
  evbuffer_drain(session->incoming, evbuffer_get_length(session->incoming));

  sp_cb.error(session, sp_cb_arg, ret, sp_errmsg);
  return;
}


/* --------------------------- Outgoing messages ---------------------------- */

// This message is constructed like librespot does it, see handshake.rs
static ssize_t
msg_make_client_hello(uint8_t *out, size_t out_len, struct sp_session *session)
{
  ClientHello client_hello = CLIENT_HELLO__INIT;
  BuildInfo build_info = BUILD_INFO__INIT;
  LoginCryptoHelloUnion login_crypto = LOGIN_CRYPTO_HELLO_UNION__INIT;
  LoginCryptoDiffieHellmanHello diffie_hellman = LOGIN_CRYPTO_DIFFIE_HELLMAN_HELLO__INIT;
  Cryptosuite crypto_suite = CRYPTOSUITE__CRYPTO_SUITE_SHANNON;
  uint8_t padding[1] = { 0x1e };
  uint8_t nonce[16] = { 0 };
  size_t len;

  build_info.product = PRODUCT__PRODUCT_PARTNER;
  build_info.platform = PLATFORM__PLATFORM_LINUX_X86;
  build_info.version = 109800078;

  diffie_hellman.gc.len = sizeof(session->keys.public_key);
  diffie_hellman.gc.data = session->keys.public_key;
  diffie_hellman.server_keys_known = 1;

  login_crypto.diffie_hellman = &diffie_hellman;

  client_hello.build_info = &build_info;
  client_hello.n_cryptosuites_supported = 1;
  client_hello.cryptosuites_supported = &crypto_suite;
  client_hello.login_crypto_hello = &login_crypto;
  client_hello.client_nonce.len = sizeof(nonce);
  client_hello.client_nonce.data = nonce;
  client_hello.has_padding = 1;
  client_hello.padding.len = sizeof(padding);
  client_hello.padding.data = padding;

  len = client_hello__get_packed_size(&client_hello);
  if (len > out_len)
    return -1;

  client_hello__pack(&client_hello, out);

  return len;
}

static int
client_response_crypto(uint8_t **challenge, size_t *challenge_len, struct sp_session *session)
{
  uint8_t *packets;
  size_t packets_len;
  int ret;

  packets = evbuffer_pullup(session->handshake_packets, -1);
  packets_len = evbuffer_get_length(session->handshake_packets);

  ret = crypto_challenge(challenge, challenge_len,
                         session->encrypt.key, sizeof(session->encrypt.key),
                         session->decrypt.key, sizeof(session->decrypt.key),
                         packets, packets_len,
                         session->keys.shared_secret, session->keys.shared_secret_len);

  return ret;
}

static ssize_t
msg_make_client_response_plaintext(uint8_t *out, size_t out_len, struct sp_session *session)
{
  ClientResponsePlaintext client_response = CLIENT_RESPONSE_PLAINTEXT__INIT;
  LoginCryptoResponseUnion login_crypto_response = LOGIN_CRYPTO_RESPONSE_UNION__INIT;
  LoginCryptoDiffieHellmanResponse diffie_hellman = LOGIN_CRYPTO_DIFFIE_HELLMAN_RESPONSE__INIT;
  uint8_t *challenge;
  size_t challenge_len;
  ssize_t len;
  int ret;

  ret = client_response_crypto(&challenge, &challenge_len, session);
  if (ret < 0)
    return -1;

  diffie_hellman.hmac.len = challenge_len;
  diffie_hellman.hmac.data = challenge;

  login_crypto_response.diffie_hellman = &diffie_hellman;

  client_response.login_crypto_response = &login_crypto_response;

  len = client_response_plaintext__get_packed_size(&client_response);
  if (len > out_len)
    {
      free(challenge);
      return -1;
    }

  client_response_plaintext__pack(&client_response, out);

  free(challenge);
  return len;
}

static ssize_t
msg_make_client_response_encrypted(uint8_t *out, size_t out_len, struct sp_session *session)
{
  ClientResponseEncrypted client_response = CLIENT_RESPONSE_ENCRYPTED__INIT;
  LoginCredentials login_credentials = LOGIN_CREDENTIALS__INIT;
  SystemInfo system_info = SYSTEM_INFO__INIT;
  ssize_t len;

  login_credentials.has_auth_data = 1;
  login_credentials.username = session->credentials.username;

  if (session->credentials.stored_cred_len > 0)
    {
      login_credentials.typ = AUTHENTICATION_TYPE__AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS;
      login_credentials.auth_data.len = session->credentials.stored_cred_len;
      login_credentials.auth_data.data = session->credentials.stored_cred;
    }
  else if (session->credentials.token_len > 0)
    {
      login_credentials.typ = AUTHENTICATION_TYPE__AUTHENTICATION_SPOTIFY_TOKEN;
      login_credentials.auth_data.len = session->credentials.token_len;
      login_credentials.auth_data.data = session->credentials.token;
    }
  else if (session->credentials.password)
    {
      login_credentials.typ = AUTHENTICATION_TYPE__AUTHENTICATION_USER_PASS;
      login_credentials.auth_data.len = strlen(session->credentials.password);
      login_credentials.auth_data.data = (unsigned char *)session->credentials.password;
    }
  else
    return -1;

  system_info.cpu_family = CPU_FAMILY__CPU_UNKNOWN;
  system_info.os = OS__OS_UNKNOWN;
  system_info.system_information_string = "librespot_a2f832d_vTEFD7FD";
  system_info.device_id = "e2ae20d9ae7fcacb605c03c198e0a1c51d446f50";

  client_response.login_credentials = &login_credentials;
  client_response.system_info = &system_info;
  client_response.version_string = "librespot-a2f832d";

  len = client_response_encrypted__get_packed_size(&client_response);
  if (len > out_len)
    return -1;

  sp_cb.logmsg("Size of encrypted payload is %ld\n", len);

  client_response_encrypted__pack(&client_response, out);

//  sp_cb.hexdump("our client_response_enc\n", out, len);

  return len;
}

// From librespot-golang:
// Mercury is the protocol implementation for Spotify Connect playback control and metadata fetching.It works as a
// PUB/SUB system, where you, as an audio sink, subscribes to the events of a specified user (playlist changes) but
// also access various metadata normally fetched by external players (tracks metadata, playlists, artists, etc).
static ssize_t
msg_make_mercury_req(uint8_t *out, size_t out_len, struct sp_mercury *mercury)
{
  Header header = HEADER__INIT;
  uint8_t *ptr;
  uint16_t be;
  uint64_t be64;
  uint8_t flags = 1; // Flags "final" according to librespot
  size_t prefix_len;
  size_t header_len;
  size_t body_len;
  int i;

  prefix_len = sizeof(be) + sizeof(be64) + sizeof(flags) + sizeof(be) + sizeof(be);

  if (prefix_len > out_len)
    return -1; // Buffer too small

  ptr = out;

  be = htobe16(sizeof(be64));
  memcpy(ptr, &be, sizeof(be)); // prefix_len += sizeof(be)
  ptr += sizeof(be);

  be64 = htobe64(mercury->seq);
  memcpy(ptr, &be64, sizeof(be64)); // prefix_len += sizeof(be64)
  ptr += sizeof(be64);

  memcpy(ptr, &flags, sizeof(flags)); // prefix_len += sizeof(flags)
  ptr += sizeof(flags);

  be = htobe16(1 + mercury->parts_num); // = payload_len + 1 = "parts count"?
  memcpy(ptr, &be, sizeof(be)); // prefix_len += sizeof(be)
  ptr += sizeof(be);

  header.uri = mercury->uri;
  header.method = mercury->method; // "GET", "SUB" etc
  header.content_type = mercury->content_type;

  header_len = header__get_packed_size(&header);
  if (header_len + prefix_len > out_len)
    return -1; // Buffer too small

  be = htobe16(header_len);
  memcpy(ptr, &be, sizeof(be)); // prefix_len += sizeof(be)
  ptr += sizeof(be);

  assert(ptr - out == prefix_len);

  header__pack(&header, ptr);
  ptr += header_len;

  body_len = 0;
  for (i = 0; i < mercury->parts_num; i++)
    {
      body_len += sizeof(be) + mercury->parts[i].len;
      if (body_len + header_len + prefix_len > out_len)
	return -1; // Buffer too small

      be = htobe16(mercury->parts[i].len);
      memcpy(ptr, &be, sizeof(be));
      ptr += sizeof(be);

      memcpy(ptr, mercury->parts[i].data, mercury->parts[i].len);
      ptr += mercury->parts[i].len;
    }

  assert(ptr - out == header_len + prefix_len + body_len);

  return header_len + prefix_len + body_len;
}

static ssize_t
msg_make_mercury_track_get(uint8_t *out, size_t out_len, struct sp_session *session)
{
  struct sp_mercury mercury = { 0 };
  struct sp_channel *channel = session->now_streaming_channel;
  char uri[256];
  char *ptr;
  int i;

  assert(sizeof(uri) > sizeof(SP_MERCURY_ENDPOINT) + 2 * sizeof(channel->file.track_id));

  ptr = uri;
  ptr += sprintf(ptr, "%s", SP_MERCURY_ENDPOINT);

  for (i = 0; i < sizeof(channel->file.track_id); i++)
    ptr += sprintf(ptr, "%02x", channel->file.track_id[i]);

  mercury.method = "GET";
  mercury.seq    = channel->id;
  mercury.uri    = uri;

  return msg_make_mercury_req(out, out_len, &mercury);
}

static ssize_t
msg_make_audio_key_get(uint8_t *out, size_t out_len, struct sp_session *session)
{
  struct sp_channel *channel = session->now_streaming_channel;
  size_t required_len;
  uint32_t be32;
  uint16_t be;
  uint8_t *ptr;

  required_len = sizeof(channel->file.id) + sizeof(channel->file.track_id) + sizeof(be32) + sizeof(be);

  if (required_len > out_len)
    return -1;

  ptr = out;

  memcpy(ptr, channel->file.id, sizeof(channel->file.id));
  ptr += sizeof(channel->file.id);

  memcpy(ptr, channel->file.track_id, sizeof(channel->file.track_id));
  ptr += sizeof(channel->file.track_id);

  be32 = htobe32(channel->id);
  memcpy(ptr, &be32, sizeof(be32));
  ptr += sizeof(be32);

  be = htobe16(0); // Unknown
  memcpy(ptr, &be, sizeof(be));
  ptr += sizeof(be);

  return required_len;
}

static ssize_t
msg_make_chunk_request(uint8_t *out, size_t out_len, struct sp_session *session)
{
  struct sp_channel *channel = session->now_streaming_channel;
  uint8_t *ptr;
  uint16_t be;
  uint32_t be32;
  size_t required_len;

  ptr = out;

  required_len = 3 * sizeof(be) + sizeof(channel->file.id) + 5 * sizeof(be32);
  if (required_len > out_len)
    return -1;

  be = htobe16(channel->id);
  memcpy(ptr, &be, sizeof(be));
  ptr += sizeof(be); // x1

  be = htobe16(1); // Unknown purpose
  memcpy(ptr, &be, sizeof(be));
  ptr += sizeof(be); // x2

  be = htobe16(0); // Unknown purpose
  memcpy(ptr, &be, sizeof(be));
  ptr += sizeof(be); // x3

  be32 = htobe32(0); // Unknown purpose
  memcpy(ptr, &be32, sizeof(be32));
  ptr += sizeof(be32); // x1

  be32 = htobe32(0x00009C40); // Unknown purpose
  memcpy(ptr, &be32, sizeof(be32));
  ptr += sizeof(be32); // x2

  be32 = htobe32(0x00020000); // Unknown purpose
  memcpy(ptr, &be32, sizeof(be32));
  ptr += sizeof(be32); // x3

  memcpy(ptr, channel->file.id, sizeof(channel->file.id));
  ptr += sizeof(channel->file.id);

  be32 = htobe32(channel->file.offset_words);
  memcpy(ptr, &be32, sizeof(be32));
  ptr += sizeof(be32); // x4

  be32 = htobe32(channel->file.offset_words + SP_CHUNK_LEN_WORDS);
  memcpy(ptr, &be32, sizeof(be32));
  ptr += sizeof(be32); // x5

  assert(required_len == ptr - out);
  assert(required_len == 46);

  return required_len;
}

static int
msg_send(enum sp_msg_type type, struct sp_session *session)
{
  uint8_t pkt[4096];
  ssize_t pkt_len;
  uint8_t msg[4096];
  ssize_t msg_len;
  bool with_version_header = false;
  enum sp_cmd_type cmd = 0;
  int ret;

  session->msg_type = type;

  switch (type)
    {
      case MSG_TYPE_CLIENT_HELLO:
	msg_len = msg_make_client_hello(msg, sizeof(msg), session);
	with_version_header = true;
	session->msg_handler = response_client_hello;
	session->msg_type_next = MSG_TYPE_CLIENT_RESPONSE_PLAINTEXT;
	break;
      case MSG_TYPE_CLIENT_RESPONSE_PLAINTEXT:
	msg_len = msg_make_client_response_plaintext(msg, sizeof(msg), session);
	session->msg_handler = response_dummy;
	session->msg_type_next = MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED;

	// No response expected here, so add event to trigger sending next msg
	event_active(session->msg_next_ev, 0, 0);
	break;
      case MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED:
	msg_len = msg_make_client_response_encrypted(msg, sizeof(msg), session);
	password_zerofree(&session->credentials); // Should be done with it now, so zero it
	cmd = CmdLogin;

	sp_cb.hexdump("Key\n", session->decrypt.key, sizeof(session->decrypt.key));

	session->is_encrypted = true;
	session->msg_handler = response_generic;
	break;
      case MSG_TYPE_MERCURY_TRACK_GET:
	msg_len = msg_make_mercury_track_get(msg, sizeof(msg), session);
	cmd = CmdMercuryReq;
	session->msg_handler = response_generic;
	session->msg_type_next = MSG_TYPE_AUDIO_KEY_GET;
	break;
      case MSG_TYPE_AUDIO_KEY_GET:
	msg_len = msg_make_audio_key_get(msg, sizeof(msg), session);
	cmd = CmdRequestKey;
	session->msg_handler = response_generic;
	break;
      case MSG_TYPE_CHUNK_REQUEST:
	msg_len = msg_make_chunk_request(msg, sizeof(msg), session);
	cmd = CmdStreamChunk;
	session->msg_handler = response_generic;
	break;
      case MSG_TYPE_PONG:
	msg_len = 4;
	memset(msg, 0, msg_len); // librespot just replies with zeroes
	cmd = CmdPong;
	break;
      default:
	msg_len = -1;
    }

  if (msg_len < 0)
    {
      sp_cb.logmsg("Could not construct message\n");
      return -1;
    }

  if (session->is_encrypted)
    pkt_len = packet_make_encrypted(pkt, sizeof(pkt), cmd, msg, msg_len, &session->encrypt);
  else
    pkt_len = packet_make_plain(pkt, sizeof(pkt), msg, msg_len, with_version_header);

  if (pkt_len < 0)
    {
      sp_cb.logmsg("Could not construct packet\n");
      return -1;
    }

#ifndef DEBUG_MOCK
  sp_cb.logmsg("\nSending pkt type %d (cmd=0x%02x) with size %zu\n", type, cmd, pkt_len);

  ret = send(session->receive_fd, pkt, pkt_len, 0);
  if (ret != pkt_len)
    {
      sp_cb.logmsg("Could not send\n");
      return -1;
    }
#else
  sp_cb.logmsg("\nMocking send/response pkt type %d (cmd=0x%02x) with size %zu\n", type, cmd, pkt_len);

  ret = debug_mock_response(session);
  if (ret < 0)
    {
      sp_cb.logmsg("Could not mock send\n");
      return -1;
    }
#endif

  // Save sent packet for MAC calculation later
  if (!session->handshake_completed)
    evbuffer_add(session->handshake_packets, pkt, pkt_len);

  return 0;
}


/* ----------------------------- Implementation ----------------------------- */

static unsigned char
base62_digit(char c)
{
  if (isdigit(c))
    return c - '0';
  else if (islower(c))
    return c - 'a' + 10;
  else if (isupper(c))
    return c - 'A' + 10 + 26;
  else
    return 0xff;
}

// base 62 to bin: 4gtj0ZuMWRw8WioT9SXsC2 -> 8c283882b29346829b8d021f52f5c2ce
//                 00AdHZ94Jb7oVdHVJmJsIU -> 004f421c7e934635aaf778180a8fd068
static int
path_to_track_id(struct sp_file *file)
{
  uint8_t u8;
  bnum n;
  bnum base;
  bnum digit;
  char *ptr;
  int ret;

  u8 = 62;
  bnum_bin2bn(base, &u8, sizeof(u8));
  bnum_new(n);

  ptr = strrchr(file->track_path, ':');
  if (!ptr || strlen(ptr + 1) != 22)
    RETURN_ERROR(SP_INVALID, "Spotify track ID had unexpected length");

  for (ptr += 1; *ptr; ptr++)
    {
      // n = 62 * n + base62_digit(*p);
      bnum_mul(n, n, base);
      u8 = base62_digit(*ptr);

      // Heavy on alloc's, but means we can use bnum compability wrapper
      bnum_bin2bn(digit, &u8, sizeof(u8));
      bnum_add(n, n, digit);
      bnum_free(digit);
    }

  ret = bnum_num_bytes(n);
  if (ret > sizeof(file->track_id))
    RETURN_ERROR(SP_INVALID, "Invalid Spotify track ID");

  memset(file->track_id, 0, sizeof(file->track_id) - ret);
  bnum_bn2bin(n, file->track_id + sizeof(file->track_id) - ret, ret);

  sp_cb.hexdump("file->track_id\n", file->track_id, sizeof(file->track_id));

  bnum_free(n);
  bnum_free(base);

  return 0;

 error:
  return ret;
}

static int
track_play(struct sp_session *session, struct sp_cmdargs *cmdargs)
{
  int ret;

  ret = msg_send(MSG_TYPE_CHUNK_REQUEST, session);
  if (ret < 0)
    RETURN_ERROR(SP_NOCONNECTION, "Could not send request for audio chunk");

  return 0;

 error:
  return ret;
}

static int
track_seek(struct sp_session *session, struct sp_cmdargs *cmdargs)
{
  return SP_INVALID;
}

static int
track_stop(struct sp_session *session, struct sp_cmdargs *cmdargs)
{
  struct sp_channel *channel = session->now_streaming_channel;

  if (!channel || !channel->in_use)
    return 0;

  // Prevents further chunk requests
  channel->stop_requested = true;

  return 0;
}

static int
track_open(struct sp_session *session, struct sp_cmdargs *cmdargs)
{
  struct sp_channel *channel = NULL;
  int ret;

  if (session->now_streaming_channel)
    {
      sp_errmsg = "Already getting a track";
      return SP_OCCUPIED;
    }

  // Reserve a channel for the upcoming communication
  ret = channel_begin(&channel, session);
  if (ret < 0)
    RETURN_ERROR(SP_OOM, "Could not reserve a channel");

  // Must be set before calling msg_send() because this info is needed for
  // making the request
  session->now_streaming_channel = channel;
  channel->file.track_path = cmdargs->path;

  channel->audio_fd[0] = cmdargs->fd_read;
  channel->audio_fd[1] = cmdargs->fd_write;

  // Sets file->track_id from file->path
  path_to_track_id(&channel->file);

  // Kicks of a sequence where we first get file info and then get the AES key
  ret = msg_send(MSG_TYPE_MERCURY_TRACK_GET, session);
  if (ret < 0)
    RETURN_ERROR(SP_NOCONNECTION, "Could not send track request");

  return 0;

 error:
  session->now_streaming_channel = NULL;
  channel_reset(channel);
  return ret;
}

static int
bitrate_set(struct sp_session *session, struct sp_cmdargs *cmdargs)
{
  session->bitrate_preferred = cmdargs->bitrate;
  return 0;
}

static int
login(struct sp_session *session, struct sp_cmdargs *cmdargs)
{
  int ret;

  session->credentials.username = cmdargs->username;

  if (cmdargs->stored_cred)
    {
      if (cmdargs->stored_cred_len > sizeof(session->credentials.stored_cred))
	RETURN_ERROR(SP_INVALID, "Invalid stored credential");

      session->credentials.stored_cred_len = cmdargs->stored_cred_len;
      memcpy(session->credentials.stored_cred, cmdargs->stored_cred, session->credentials.stored_cred_len);
    }
  else if (cmdargs->token)
    {
      if (cmdargs->token_len > sizeof(session->credentials.token))
	RETURN_ERROR(SP_INVALID, "Invalid token");

      session->credentials.token_len = cmdargs->token_len;
      memcpy(session->credentials.token, cmdargs->token, session->credentials.token_len);
    }
  else
    session->credentials.password = cmdargs->password;

  session->bitrate_preferred = SP_BITRATE_320;

  session->incoming = evbuffer_new();
  session->handshake_packets = evbuffer_new();
  session->msg_next_ev = event_new(sp_evbase, -1, 0, msg_next_cb, session);

  crypto_keys_set(&session->keys);

  ret = ap_connect(session);
  if (ret < 0)
    RETURN_ERROR(ret, sp_errmsg);

  // Send login request
  ret = msg_send(MSG_TYPE_CLIENT_HELLO, session);
  if (ret < 0)
    RETURN_ERROR(SP_NOCONNECTION, "Could not send request");

  // Add to linked list
  session->next = sp_sessions;
  sp_sessions = session;

  free(cmdargs->stored_cred);
  free(cmdargs->token);
  return 0;

 error:
  free(cmdargs->stored_cred);
  free(cmdargs->token);
  session_free(session);
  return ret;
}

// Covers all the boiler-plate
static enum command_state
command_receive(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session = cmdargs->session;
  int ret;

  if (session && cmdargs->handler != login)
    {
      // Since we're async the session may have become invalid
      ret = session_check(session);
      if (ret < 0)
	RETURN_ERROR(SP_NOSESSION, "Session has disappeared");
    }
  else if (!session)
    {
      session = session_find_by_fd(cmdargs->fd_read);
      if (!session)
	RETURN_ERROR(SP_NOSESSION, "Invalid file descriptor");
    }

  ret = cmdargs->handler(session, cmdargs);
  if (ret < 0)
    RETURN_ERROR(ret, sp_errmsg);

  *retval = 0;
  return COMMAND_END;

 error:
  sp_cb.error(session, sp_cb_arg, ret, sp_errmsg);

  *retval = ret;
  return COMMAND_END;
}

/* ------------------------------ Event loop -------------------------------- */

static void *
spotifyc(void *arg)
{
  event_base_dispatch(sp_evbase);

  pthread_exit(NULL);
}


/* ---------------------------------- API ----------------------------------- */

int
spotifyc_open(const char *path, struct sp_session *session)
{
  struct sp_cmdargs *cmdargs;
  int fd[2];
  int ret;

  // Open the fd's right away so we can return the fd to caller. Caller will own
  // the reading end, audio_fd[0]
  ret = pipe(fd);
  if (ret < 0)
    return SP_WRITE;

  cmdargs = calloc(1, sizeof(struct sp_cmdargs));
  cmdargs->session  = session;
  cmdargs->path     = strdup(path);
  cmdargs->fd_read  = fd[0];
  cmdargs->fd_write = fd[1];
  cmdargs->handler  = track_open;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);

  return fd[0];
}

void
spotifyc_bitrate_set(enum sp_bitrates bitrate, struct sp_session *session)
{
  struct sp_cmdargs *cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->session  = session;
  cmdargs->bitrate  = bitrate;
  cmdargs->handler  = bitrate_set;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);
}

// Starts writing audio for the caller to read from the file descriptor
int
spotifyc_play(int fd)
{
  struct sp_cmdargs *cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->fd_read  = fd;
  cmdargs->handler  = track_play;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);

  return 0;
}

int
spotifyc_seek(int seek_ms, int fd)
{
  struct sp_cmdargs *cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->fd_read  = fd;
  cmdargs->seek_ms  = seek_ms;
  cmdargs->handler  = track_seek;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);

  return 0;
}

int
spotifyc_stop(int fd)
{
  struct sp_cmdargs *cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->fd_read  = fd;
  cmdargs->handler  = track_stop;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);

  return 0;
}

struct sp_session *
spotifyc_login_password(const char *username, const char *password)
{
  struct sp_cmdargs *cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->session  = calloc(1, sizeof(struct sp_session));
  cmdargs->username = strdup(username);
  cmdargs->password = strdup(password);
  cmdargs->handler  = login;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);

  return cmdargs->session;
}

struct sp_session *
spotifyc_login_stored_cred(const char *username, uint8_t *stored_cred, size_t stored_cred_len)
{
  struct sp_cmdargs *cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->session         = calloc(1, sizeof(struct sp_session));
  cmdargs->username        = strdup(username);
  cmdargs->stored_cred     = malloc(stored_cred_len);
  memcpy(cmdargs->stored_cred, stored_cred, stored_cred_len);
  cmdargs->stored_cred_len = stored_cred_len;
  cmdargs->handler         = login;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);

  return cmdargs->session;
}

struct sp_session *
spotifyc_login_token(const char *username, uint8_t *token, size_t token_len)
{
  struct sp_cmdargs *cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->session         = calloc(1, sizeof(struct sp_session));
  cmdargs->username        = strdup(username);
  cmdargs->token           = malloc(token_len);
  memcpy(cmdargs->token, token, token_len);
  cmdargs->token_len       = token_len;
  cmdargs->handler         = login;

  commands_exec_async(sp_cmdbase, command_receive, cmdargs);

  return cmdargs->session;
}

void
spotifyc_logout(struct sp_session *session)
{
  // TODO
  return;
}


/* ---------------------------- Sync interface ------------------------------ */

const char *
spotifyc_last_errmsg(void)
{
  return sp_errmsg ? sp_errmsg : "(no error)";
}

int
spotifyc_init(struct sp_callbacks *callbacks, void *callback_arg)
{
  int ret;

  if (sp_initialized)
    RETURN_ERROR(SP_INVALID, "spotifyc already initialized");

  sp_cb     = *callbacks;
  sp_cb_arg = callback_arg;
  sp_initialized = true;

  sp_evbase = event_base_new();
  if (!sp_evbase)
    RETURN_ERROR(SP_OOM, "event_base_new() failed");

  sp_cmdbase = commands_base_new(sp_evbase, NULL);
  if (!sp_cmdbase)
    RETURN_ERROR(SP_OOM, "commands_base_new() failed");

  ret = pthread_create(&sp_tid, NULL, spotifyc, NULL);
  if (ret < 0)
    RETURN_ERROR(SP_OOM, "Could not start thread");

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(sp_tid, "spotifyc");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(sp_tid, "spotifyc");
#endif

  return 0;

 error:
  spotifyc_deinit();
  return ret;
}

void
spotifyc_deinit()
{
  commands_base_destroy(sp_cmdbase);
  sp_cmdbase = NULL;

  event_base_free(sp_evbase);
  sp_evbase = NULL;

  pthread_join(sp_tid, NULL);

  sp_initialized = false;
  memset(&sp_cb, 0, sizeof(struct sp_callbacks));

  return;
}
