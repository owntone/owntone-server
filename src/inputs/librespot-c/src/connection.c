#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <errno.h>

#include <json-c/json.h>

#include "librespot-c-internal.h"
#include "connection.h"
#include "channel.h"

static struct timeval sp_idle_tv = { SP_AP_DISCONNECT_SECS, 0 };

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

/* ---------------------------------- MOCKING ------------------------------- */

#ifdef DEBUG_MOCK
#include "mock.h"

static int debug_mock_pipe[2];
static int debug_mock_chunks_written = 0;

static int
debug_mock_response(struct sp_message *msg, struct sp_connection *conn)
{
  if (debug_mock_chunks_written == 1)
    return 0; // Only write the chunk once

  switch(msg->type)
    {
      case MSG_TYPE_CLIENT_HELLO:
	write(debug_mock_pipe[1], mock_resp_apresponse, sizeof(mock_resp_apresponse));
	break;
      case MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED:
        memcpy(conn->decrypt.key, mock_recv_key, sizeof(conn->decrypt.key));
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
        memset(conn->decrypt.key, 0, sizeof(conn->decrypt.key)); // Tells msg_read_one() to skip decryption
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


/* --------------------------------- Helpers -------------------------------- */

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
      case SP_BITRATE_ANY:
	return (a->format > b->format); // This case shouldn't happen, so this is mostly to avoid compiler warnings
    }

  return false;
}

int
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


/* --------------------------- Connection handling -------------------------- */

static int
ap_resolve(char **address, unsigned short *port)
{
  char *body;
  json_object *jresponse = NULL;
  json_object *ap_list;
  json_object *ap;
  char *ap_address = NULL;
  char *ap_port;
  int ap_num;
  int ret;

  free(*address);
  *address = NULL;

  ret = sp_cb.https_get(&body, SP_AP_RESOLVE_URL);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Could not connect to access point resolver");

  jresponse = json_tokener_parse(body);
  if (!jresponse)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Could not parse reply from access point resolver");

  if (! (json_object_object_get_ex(jresponse, SP_AP_RESOLVE_KEY, &ap_list) || json_object_get_type(ap_list) == json_type_array))
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Unexpected reply from access point resolver");

  ap_num = json_object_array_length(ap_list);
  ap = json_object_array_get_idx(ap_list, rand() % ap_num);
  if (! (ap && json_object_get_type(ap) == json_type_string))
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Unexpected reply from access point resolver");

  ap_address = strdup(json_object_get_string(ap));

  if (! (ap_port = strchr(ap_address, ':')))
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Unexpected reply from access point resolver, missing port");
  *ap_port = '\0';
  ap_port += 1;

  *address = ap_address;
  *port = (unsigned short)atoi(ap_port);

  json_object_put(jresponse);
  free(body);
  return 0;

 error:
  free(ap_address);
  json_object_put(jresponse);
  free(body);
  return ret;
}

static bool
is_handshake(enum sp_msg_type type)
{
  return ( type == MSG_TYPE_CLIENT_HELLO ||
           type == MSG_TYPE_CLIENT_RESPONSE_PLAINTEXT ||
           type == MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED );
}

static void
connection_clear(struct sp_connection *conn)
{
  if (!conn)
    return;

  if (conn->response_ev)
    event_free(conn->response_ev);
  if (conn->idle_ev)
    event_free(conn->idle_ev);
  if (conn->timeout_ev)
    event_free(conn->timeout_ev);

  if (conn->handshake_packets)
    evbuffer_free(conn->handshake_packets);
  if (conn->incoming)
    evbuffer_free(conn->incoming);

  free(conn->ap_address);
  free(conn->keys.shared_secret);

  memset(conn, 0, sizeof(struct sp_connection));
  conn->response_fd = -1;
}

void
ap_disconnect(struct sp_connection *conn)
{
  if (conn->is_connected)
    sp_cb.tcp_disconnect(conn->response_fd);

  connection_clear(conn);
}

static void
connection_idle_cb(int fd, short what, void *arg)
{
  struct sp_connection *conn = arg;

  ap_disconnect(conn);

  sp_cb.logmsg("Connection is idle, auto-disconnected\n");
}

static int
connection_make(struct sp_connection *conn, struct sp_conn_callbacks *cb, struct sp_session *session)
{
  int response_fd;
  int ret;

  if (!conn->ap_address || !conn->ap_port)
    {
      ret = ap_resolve(&conn->ap_address, &conn->ap_port);
      if (ret < 0)
	RETURN_ERROR(ret, sp_errmsg);
    }

#ifndef DEBUG_MOCK
  response_fd = sp_cb.tcp_connect(conn->ap_address, conn->ap_port);
  if (response_fd < 0)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Could not connect to access point");
#else
  pipe(debug_mock_pipe);
  response_fd = debug_mock_pipe[0];
#endif

  conn->response_fd = response_fd;
  conn->response_ev = event_new(cb->evbase, response_fd, EV_READ | EV_PERSIST, cb->response_cb, session);
  conn->timeout_ev = evtimer_new(cb->evbase, cb->timeout_cb, conn);

  conn->idle_ev = evtimer_new(cb->evbase, connection_idle_cb, conn);

  conn->handshake_packets = evbuffer_new();
  conn->incoming = evbuffer_new();

  crypto_keys_set(&conn->keys);
  conn->encrypt.logmsg = sp_cb.logmsg;
  conn->decrypt.logmsg = sp_cb.logmsg;

  event_add(conn->response_ev, NULL);

  conn->is_connected = true;

  return 0;

 error:
  return ret;
}

enum sp_error
ap_connect(enum sp_msg_type type, struct sp_conn_callbacks *cb, struct sp_session *session)
{
  int ret;

  if (!session->conn.is_connected)
    {
      ret = connection_make(&session->conn, cb, session);
      if (ret < 0)
	RETURN_ERROR(ret, sp_errmsg);
    }

  if (is_handshake(type) || session->conn.handshake_completed)
    return SP_OK_DONE; // Proceed right away

  return SP_OK_WAIT; // Caller must login again

 error:
  ap_disconnect(&session->conn);
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
packet_make_plain(uint8_t *out, size_t out_len, uint8_t *protobuf, size_t protobuf_len, bool add_version_header)
{
  const uint8_t version_header[] = { 0x00, 0x04 };
  size_t header_len;
  ssize_t len;
  uint32_t be;

  header_len = add_version_header ? sizeof(be) + sizeof(version_header) : sizeof(be);

  len = header_len + protobuf_len;
  if (len > out_len)
    return -1;

  if (add_version_header)
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


/* --------------------------- Incoming messages ---------------------------- */

static enum sp_error
response_client_hello(uint8_t *msg, size_t msg_len, struct sp_session *session)
{
  struct sp_connection *conn = &session->conn;
  APResponseMessage *apresponse;
  size_t header_len = 4; // TODO make a define
  int ret;

  apresponse = apresponse_message__unpack(NULL, msg_len - header_len, msg + header_len);
  if (!apresponse)
    RETURN_ERROR(SP_ERR_INVALID, "Could not unpack apresponse from access point");

  // TODO check APLoginFailed

  // Not sure if necessary
  if (!apresponse->challenge || !apresponse->challenge->login_crypto_challenge)
    RETURN_ERROR(SP_ERR_INVALID, "Missing challenge in response from access point");

  crypto_shared_secret(
    &conn->keys.shared_secret, &conn->keys.shared_secret_len,
    conn->keys.private_key, sizeof(conn->keys.private_key),
    apresponse->challenge->login_crypto_challenge->diffie_hellman->gs.data, apresponse->challenge->login_crypto_challenge->diffie_hellman->gs.len);

  apresponse_message__free_unpacked(apresponse, NULL);

  conn->handshake_completed = true;

  return SP_OK_DONE;

 error:
  return ret;
}

static enum sp_error
response_apwelcome(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  APWelcome *apwelcome;
  int ret;

  apwelcome = apwelcome__unpack(NULL, payload_len, payload);
  if (!apwelcome)
    RETURN_ERROR(SP_ERR_INVALID, "Could not unpack apwelcome response from access point");

  if (apwelcome->reusable_auth_credentials_type == AUTHENTICATION_TYPE__AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS)
    {
      if (apwelcome->reusable_auth_credentials.len > sizeof(session->credentials.stored_cred))
	RETURN_ERROR(SP_ERR_INVALID, "Credentials from Spotify longer than expected");

      session->credentials.stored_cred_len = apwelcome->reusable_auth_credentials.len;
      memcpy(session->credentials.stored_cred, apwelcome->reusable_auth_credentials.data, session->credentials.stored_cred_len);
    }

  apwelcome__free_unpacked(apwelcome, NULL);

  return SP_OK_DONE;

 error:
  return ret;
}

static enum sp_error
response_aplogin_failed(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  APLoginFailed *aplogin_failed;

  aplogin_failed = aplogin_failed__unpack(NULL, payload_len, payload);
  if (!aplogin_failed)
    {
      sp_errmsg = "Could not unpack login failure from access point";
      return SP_ERR_LOGINFAILED;
    }

  sp_errmsg = "(unknown login error)";
  for (int i = 0; i < sizeof(sp_login_errors); i++)
    {
      if (sp_login_errors[i].errorcode != aplogin_failed->error_code)
	continue;

      sp_errmsg = sp_login_errors[i].errmsg;
      break;
    }

  aplogin_failed__free_unpacked(aplogin_failed, NULL);

  return SP_ERR_LOGINFAILED;
}

static enum sp_error
response_chunk_res(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  struct sp_channel *channel;
  uint16_t channel_id;
  int ret;

  ret = channel_msg_read(&channel_id, payload, payload_len, session);
  if (ret < 0)
    goto error;

  channel = &session->channels[channel_id];

  // Save any audio data to a buffer that will be written to audio_fd[1] when
  // it is writable. Note that request for next chunk will also happen then.
  evbuffer_add(channel->audio_buf, channel->body.data, channel->body.data_len);

//  sp_cb.logmsg("EOC is %d, data is %zu, buflen is %zu\n", channel->file.end_of_chunk, channel->body.data_len, evbuffer_get_length(channel->audio_buf));

  return channel->file.end_of_chunk ? SP_OK_DATA : SP_OK_OTHER;

 error:
  return ret;
}

static enum sp_error
response_aes_key(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  struct sp_channel *channel;
  const char *errmsg;
  uint32_t be32;
  uint32_t channel_id;
  int ret;

  // Payload is expected to consist of seq (uint32 BE), and key (16 bytes)
  if (payload_len != sizeof(be32) + 16)
    RETURN_ERROR(SP_ERR_DECRYPTION, "Unexpected key received");

  memcpy(&be32, payload, sizeof(be32));
  channel_id = be32toh(be32);

  channel = channel_get(channel_id, session);
  if (!channel)
    RETURN_ERROR(SP_ERR_INVALID, "Unexpected channel received");

  memcpy(channel->file.key, payload + 4, 16);

  ret = crypto_aes_new(&channel->file.decrypt, channel->file.key, sizeof(channel->file.key), sp_aes_iv, sizeof(sp_aes_iv), &errmsg);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_DECRYPTION, errmsg);

  return SP_OK_DONE;

 error:
  return ret;
}

static enum sp_error
response_aes_key_error(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  sp_errmsg = "Did not get key for decrypting track";

  return SP_ERR_DECRYPTION;
}

static enum sp_error
response_mercury_req(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  struct sp_mercury mercury = { 0 };
  struct sp_channel *channel;
  uint32_t channel_id;
  int ret;

  ret = mercury_parse(&mercury, payload, payload_len);
  if (ret < 0)
    {
      sp_errmsg = "Could not parse message from Spotify";
      return SP_ERR_INVALID;
    }

  if (mercury.parts_num != 1 || !mercury.parts[0].track)
    RETURN_ERROR(SP_ERR_INVALID, "Unexpected track response from Spotify");

  channel_id = (uint32_t)mercury.seq;

  channel = channel_get(channel_id, session);
  if (!channel)
    RETURN_ERROR(SP_ERR_INVALID, "Unexpected channel received");

  ret = file_select(channel->file.id, sizeof(channel->file.id), mercury.parts[0].track, session->bitrate_preferred);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_INVALID, "Could not find track data");

  mercury_free(&mercury, 1);

  return SP_OK_DONE; // Continue to get AES key

 error:
  mercury_free(&mercury, 1);
  return ret;
}

static enum sp_error
response_ping(uint8_t *payload, size_t payload_len, struct sp_session *session)
{
  msg_pong(session);

  return SP_OK_OTHER;
}

static enum sp_error
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
	ret = response_ping(payload, payload_len, session);
	break;
      case CmdStreamChunkRes:
	ret = response_chunk_res(payload, payload_len, session);
	break;
      case CmdCountryCode:
	memcpy(session->country, payload, sizeof(session->country) - 1);
	ret = SP_OK_OTHER;
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
	ret = SP_OK_OTHER;
    }

  return ret;
}

static enum sp_error
msg_read_one(uint8_t **out, size_t *out_len, uint8_t *in, size_t in_len, struct sp_connection *conn)
{
  uint32_t be32;
  ssize_t msg_len;
  int ret;

#ifdef DEBUG_MOCK
  if (conn->is_encrypted && !conn->decrypt.key[0] && !conn->decrypt.key[1])
    {
      uint16_t be;
      memcpy(&be, in + 1, sizeof(be));
      msg_len = be16toh(be) + 7;
      if (msg_len > in_len)
	return SP_OK_WAIT;

      *out = malloc(msg_len);
      *out_len = msg_len;
      evbuffer_remove(conn->incoming, *out, msg_len);

      return SP_OK_DONE;
    }
#endif

  if (conn->is_encrypted)
    {
      msg_len = crypto_decrypt(in, in_len, &conn->decrypt);
      if (msg_len < 0)
	RETURN_ERROR(SP_ERR_DECRYPTION, "Decryption error");
      if (msg_len == 0)
	return SP_OK_WAIT;
    }
  else
    {
      if (in_len < sizeof(be32))
	return SP_OK_WAIT; // Wait for more data, size header is incomplete

      memcpy(&be32, in, sizeof(be32));
      msg_len = be32toh(be32);
      if (msg_len < 0)
	RETURN_ERROR(SP_ERR_INVALID, "Invalid message length");
      if (msg_len > in_len)
	return SP_OK_WAIT;

      if (!conn->handshake_completed)
	evbuffer_add(conn->handshake_packets, in, msg_len);
    }

  // At this point we have a complete, decrypted message.
  *out = malloc(msg_len);
  *out_len = msg_len;
  evbuffer_remove(conn->incoming, *out, msg_len);

  return SP_OK_DONE;

 error:
  return ret;
}

enum sp_error
response_read(struct sp_session *session)
{
  struct sp_connection *conn = &session->conn;
  uint8_t *in;
  size_t in_len;
  uint8_t *msg;
  size_t msg_len;
  int ret;

  in_len = evbuffer_get_length(conn->incoming);
  in = evbuffer_pullup(conn->incoming, -1);

  ret = msg_read_one(&msg, &msg_len, in, in_len, conn);
  if (ret != SP_OK_DONE)
    goto error;

  if (msg_len < 128)
    sp_cb.hexdump("Received message\n", msg, msg_len);
  else
    sp_cb.hexdump("Received message (truncated)\n", msg, 128);

  if (!session->response_handler)
    RETURN_ERROR(SP_ERR_INVALID, "Unexpected response from Spotify, aborting");

  // Handler must return SP_OK_DONE if the message is a response to a request.
  // It must return SP_OK_OTHER if the message is something else (e.g. a ping),
  // SP_ERR_xxx if the response indicates an error. Finally, SP_OK_DATA is like
  // DONE except it also means that there is new audio data to write.
  ret = session->response_handler(msg, msg_len, session);
  free(msg);

  return ret;

 error:
  return ret;
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

  diffie_hellman.gc.len = sizeof(session->conn.keys.public_key);
  diffie_hellman.gc.data = session->conn.keys.public_key;
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
client_response_crypto(uint8_t **challenge, size_t *challenge_len, struct sp_connection *conn)
{
  uint8_t *packets;
  size_t packets_len;
  int ret;

  packets_len = evbuffer_get_length(conn->handshake_packets);
  packets = malloc(packets_len);
  evbuffer_remove(conn->handshake_packets, packets, packets_len);

  ret = crypto_challenge(challenge, challenge_len,
                         conn->encrypt.key, sizeof(conn->encrypt.key),
                         conn->decrypt.key, sizeof(conn->decrypt.key),
                         packets, packets_len,
                         conn->keys.shared_secret, conn->keys.shared_secret_len);

  free(packets);

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

  ret = client_response_crypto(&challenge, &challenge_len, &session->conn);
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
  char system_information_string[64];
  char version_string[64];
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
  else if (strlen(session->credentials.password))
    {
      login_credentials.typ = AUTHENTICATION_TYPE__AUTHENTICATION_USER_PASS;
      login_credentials.auth_data.len = strlen(session->credentials.password);
      login_credentials.auth_data.data = (unsigned char *)session->credentials.password;
    }
  else
    return -1;

  system_info.cpu_family = CPU_FAMILY__CPU_UNKNOWN;
  system_info.os = OS__OS_UNKNOWN;
  snprintf(system_information_string, sizeof(system_information_string), "%s_%s_%s",
    sp_sysinfo.client_name, sp_sysinfo.client_version, sp_sysinfo.client_build_id);
  system_info.system_information_string = system_information_string;
  system_info.device_id = sp_sysinfo.device_id;

  client_response.login_credentials = &login_credentials;
  client_response.system_info = &system_info;
  snprintf(version_string, sizeof(version_string), "%s-%s", sp_sysinfo.client_name, sp_sysinfo.client_version);
  client_response.version_string = version_string;

  len = client_response_encrypted__get_packed_size(&client_response);
  if (len > out_len)
    return -1;

  client_response_encrypted__pack(&client_response, out);

  return len;
}

// From librespot-golang:
// Mercury is the protocol implementation for Spotify Connect playback control and metadata fetching. It works as a
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

  assert(sizeof(uri) > sizeof(SP_MERCURY_URI_TRACK) + 2 * sizeof(channel->file.media_id));

  ptr = uri;
  ptr += sprintf(ptr, "%s", SP_MERCURY_URI_TRACK);

  for (i = 0; i < sizeof(channel->file.media_id); i++)
    ptr += sprintf(ptr, "%02x", channel->file.media_id[i]);

  mercury.method = "GET";
  mercury.seq    = channel->id;
  mercury.uri    = uri;

  return msg_make_mercury_req(out, out_len, &mercury);
}

static ssize_t
msg_make_mercury_episode_get(uint8_t *out, size_t out_len, struct sp_session *session)
{
  struct sp_mercury mercury = { 0 };
  struct sp_channel *channel = session->now_streaming_channel;
  char uri[256];
  char *ptr;
  int i;

  assert(sizeof(uri) > sizeof(SP_MERCURY_URI_EPISODE) + 2 * sizeof(channel->file.media_id));

  ptr = uri;
  ptr += sprintf(ptr, "%s", SP_MERCURY_URI_EPISODE);

  for (i = 0; i < sizeof(channel->file.media_id); i++)
    ptr += sprintf(ptr, "%02x", channel->file.media_id[i]);

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

  required_len = sizeof(channel->file.id) + sizeof(channel->file.media_id) + sizeof(be32) + sizeof(be);

  if (required_len > out_len)
    return -1;

  ptr = out;

  memcpy(ptr, channel->file.id, sizeof(channel->file.id));
  ptr += sizeof(channel->file.id);

  memcpy(ptr, channel->file.media_id, sizeof(channel->file.media_id));
  ptr += sizeof(channel->file.media_id);

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

  if (!channel)
    return -1;

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

int
msg_make(struct sp_message *msg, enum sp_msg_type type, struct sp_session *session)
{
  memset(msg, 0, sizeof(struct sp_message));
  msg->type = type;

  switch (type)
    {
      case MSG_TYPE_CLIENT_HELLO:
	msg->len = msg_make_client_hello(msg->data, sizeof(msg->data), session);
	msg->type_next = MSG_TYPE_CLIENT_RESPONSE_PLAINTEXT;
	msg->add_version_header = true;
	msg->response_handler = response_client_hello;
	break;
      case MSG_TYPE_CLIENT_RESPONSE_PLAINTEXT:
	msg->len = msg_make_client_response_plaintext(msg->data, sizeof(msg->data), session);
	msg->type_next = MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED;
	msg->response_handler = NULL; // No response expected
	break;
      case MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED:
	msg->len = msg_make_client_response_encrypted(msg->data, sizeof(msg->data), session);
	msg->cmd = CmdLogin;
        msg->encrypt = true;
	msg->response_handler = response_generic;
	break;
      case MSG_TYPE_MERCURY_TRACK_GET:
	msg->len = msg_make_mercury_track_get(msg->data, sizeof(msg->data), session);
	msg->cmd = CmdMercuryReq;
        msg->encrypt = true;
	msg->type_next = MSG_TYPE_AUDIO_KEY_GET;
	msg->response_handler = response_generic;
	break;
      case MSG_TYPE_MERCURY_EPISODE_GET:
	msg->len = msg_make_mercury_episode_get(msg->data, sizeof(msg->data), session);
	msg->cmd = CmdMercuryReq;
        msg->encrypt = true;
	msg->type_next = MSG_TYPE_AUDIO_KEY_GET;
	msg->response_handler = response_generic;
	break;
      case MSG_TYPE_AUDIO_KEY_GET:
	msg->len = msg_make_audio_key_get(msg->data, sizeof(msg->data), session);
	msg->cmd = CmdRequestKey;
        msg->encrypt = true;
	msg->type_next = MSG_TYPE_CHUNK_REQUEST;
	msg->response_handler = response_generic;
	break;
      case MSG_TYPE_CHUNK_REQUEST:
	msg->len = msg_make_chunk_request(msg->data, sizeof(msg->data), session);
	msg->cmd = CmdStreamChunk;
        msg->encrypt = true;
	msg->response_handler = response_generic;
	break;
      case MSG_TYPE_PONG:
	msg->len = 4;
	msg->cmd = CmdPong;
        msg->encrypt = true;
	memset(msg->data, 0, msg->len); // librespot just replies with zeroes
	break;
      default:
	msg->len = -1;
    }

  return (msg->len < 0) ? -1 : 0;
}

int
msg_send(struct sp_message *msg, struct sp_connection *conn)
{
  uint8_t pkt[4096];
  ssize_t pkt_len;
  int ret;

  if (conn->is_encrypted)
    pkt_len = packet_make_encrypted(pkt, sizeof(pkt), msg->cmd, msg->data, msg->len, &conn->encrypt);
  else
    pkt_len = packet_make_plain(pkt, sizeof(pkt), msg->data, msg->len, msg->add_version_header);

  if (pkt_len < 0)
    RETURN_ERROR(SP_ERR_INVALID, "Error constructing packet to Spotify");

#ifndef DEBUG_MOCK
  ret = send(conn->response_fd, pkt, pkt_len, 0);
  if (ret != pkt_len)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Error sending packet to Spotify");

  sp_cb.logmsg("Sent pkt type %d (cmd=0x%02x) with size %zu (fd=%d)\n", msg->type, msg->cmd, pkt_len, conn->response_fd);
#else
  ret = debug_mock_response(msg, conn);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Error mocking send packet to Spotify");

  sp_cb.logmsg("Mocked send/response pkt type %d (cmd=0x%02x) with size %zu\n", msg->type, msg->cmd, pkt_len);
#endif

  // Save sent packet for MAC calculation later
  if (!conn->handshake_completed)
    evbuffer_add(conn->handshake_packets, pkt, pkt_len);

  // Reset the disconnect timer
  event_add(conn->idle_ev, &sp_idle_tv);

  return 0;

 error:
  return ret;
}

int
msg_pong(struct sp_session *session)
{
  struct sp_message msg;
  int ret;

  ret = msg_make(&msg, MSG_TYPE_PONG, session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_INVALID, "Error constructing pong message to Spotify");

  ret = msg_send(&msg, &session->conn);
  if (ret < 0)
    RETURN_ERROR(ret, sp_errmsg);

  return 0;

 error:
  return ret;
}
