/*
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */


/*
Illustration of the general flow, where receive and writing the result are async
operations. For some commands, e.g. open and seek, the entire sequence is
encapsulated in a sync command, which doesn't return until final "done, error or
timeout". The command play is async, so all "done/error/timeout" is returned via
callbacks. Also, play will loop the flow, i.e. after writing a chunk of data it
will go back and ask for the next chunk of data from Spotify.

In some cases there is no result to write, or no reponse expected, but then the
events for proceeding are activated directly.

    |---next----*------------next-------------*----------next----------*
    v           |                             |                        |
----------> start/send  ------------------> recv ----------------> write result
^               |            ^                |       ^                |
|---reconnect---*            |------wait------*       |------wait------*
                |                             |                        |
                v                             v                        v
           done/error                done/error/timeout           done/error

"next": on success, continue with next command
"wait": waiting for more data or for write to become possible
"timeout": receive or write took too long to complete
*/

#include <pthread.h>

#include "spotifyc-internal.h"
#include "commands.h"
#include "connection.h"
#include "channel.h"

/* TODO list

 - protect against DOS
 - use correct user-agent etc
 - web api token (scope streaming)
*/


/* -------------------------------- Globals --------------------------------- */

// Shared
struct sp_callbacks sp_cb;
struct sp_sysinfo sp_sysinfo;
const char *sp_errmsg;

static struct sp_session *sp_sessions;

static bool sp_initialized;

static pthread_t sp_tid;
static struct event_base *sp_evbase;
static struct commands_base *sp_cmdbase;

static struct timeval sp_response_timeout_tv = { SP_AP_TIMEOUT_SECS, 0 };


// Forwards
static int
request_make(enum sp_msg_type type, struct sp_session *session);


/* -------------------------------- Session --------------------------------- */

static void
session_free(struct sp_session *session)
{
  if (!session)
    return;

  channel_free_all(session);

  ap_disconnect(&session->conn);

  event_free(session->continue_ev);
  free(session);
}

static void
session_cleanup(struct sp_session *session)
{
  struct sp_session *s;

  if (!session)
    return;

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
session_new(struct sp_session **out, struct sp_cmdargs *cmdargs, event_callback_fn cb)
{
  struct sp_session *session;
  int ret;

  session = calloc(1, sizeof(struct sp_session));
  if (!session)
    RETURN_ERROR(SP_ERR_OOM, "Out of memory creating session");

  session->continue_ev = evtimer_new(sp_evbase, cb, session);
  if (!session->continue_ev)
    RETURN_ERROR(SP_ERR_OOM, "Out of memory creating session event");

  snprintf(session->credentials.username, sizeof(session->credentials.username), "%s", cmdargs->username);

  if (cmdargs->stored_cred)
    {
      if (cmdargs->stored_cred_len > sizeof(session->credentials.stored_cred))
	RETURN_ERROR(SP_ERR_INVALID, "Invalid stored credential");

      session->credentials.stored_cred_len = cmdargs->stored_cred_len;
      memcpy(session->credentials.stored_cred, cmdargs->stored_cred, session->credentials.stored_cred_len);
    }
  else if (cmdargs->token)
    {
      if (cmdargs->token_len > sizeof(session->credentials.token))
	RETURN_ERROR(SP_ERR_INVALID, "Invalid token");

      session->credentials.token_len = cmdargs->token_len;
      memcpy(session->credentials.token, cmdargs->token, session->credentials.token_len);
    }
  else
    {
      snprintf(session->credentials.password, sizeof(session->credentials.password), "%s", cmdargs->password);
    }

  session->bitrate_preferred = SP_BITRATE_DEFAULT;

  // Add to linked list
  session->next = sp_sessions;
  sp_sessions = session;

  *out = session;

  return 0;

 error:
  session_free(session);
  return ret;
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

static void
session_return(struct sp_session *session, enum sp_error err)
{
  struct sp_channel *channel = session->now_streaming_channel;
  int ret;

  ret = commands_exec_returnvalue(sp_cmdbase);
  if (ret == 0) // Here we are async, i.e. no pending command
    {
      // track_write() completed, close the write end which means reader will
      // get an EOF
      if (channel && channel->is_writing && err == SP_OK_DONE)
	channel_stop(channel);
      return;
    }

  commands_exec_end(sp_cmdbase, err);
}

// Rolls back from an error situation. If it is a failed login then the session
// will be closed, but if it just a connection timeout we keep the session, but
// drop the ongoing download.
static void
session_error(struct sp_session *session, enum sp_error err)
{
  struct sp_channel *channel = session->now_streaming_channel;

  sp_cb.logmsg("Session error: %d\n", err);

  session_return(session, err);

  if (!session->is_logged_in)
    {
      session_cleanup(session);
      return;
    }

  channel_free(channel);
  session->now_streaming_channel = NULL;
}


/* ------------------------ Main sequence control --------------------------- */

// This callback must determine if a new request should be made, or if we are
// done and should return to caller
static void
continue_cb(int fd, short what, void *arg)
{
  struct sp_session *session = arg;
  enum sp_msg_type type = MSG_TYPE_NONE;
  int ret;

  // type_next has priority, since this is what we use to chain a sequence, e.g.
  // the handshake sequence. type_queued is what comes after, e.g. first a
  // handshake (type_next) and then a chunk request (type_queued)
  if (session->msg_type_next != MSG_TYPE_NONE)
    {
      sp_cb.logmsg(">>> msg_next >>>\n");

      type = session->msg_type_next;
      session->msg_type_next = MSG_TYPE_NONE;
    }
  else if (session->msg_type_queued != MSG_TYPE_NONE)
    {
      sp_cb.logmsg(">>> msg_queued >>>\n");

      type = session->msg_type_queued;
      session->msg_type_queued = MSG_TYPE_NONE;
    }

  if (type != MSG_TYPE_NONE)
    {
      ret = request_make(type, session);
      if (ret < 0)
	session_error(session, ret);
    }
  else
    session_return(session, SP_OK_DONE); // All done, yay!
}

// This callback is triggered by response_cb when the message response handler
// said that there was data to write. If not all data can be written in one pass
// it will re-add the event.
static void
audio_write_cb(int fd, short what, void *arg)
{
  struct sp_session *session = arg;
  struct sp_channel *channel = session->now_streaming_channel;
  int ret;

  if (!channel)
    RETURN_ERROR(SP_ERR_INVALID, "Write result request, but not streaming right now");

  ret = channel_data_write(channel);
  switch (ret)
    {
      case SP_OK_WAIT:
	event_add(channel->audio_write_ev, NULL);
	break;
      case SP_OK_DONE:
	event_active(session->continue_ev, 0, 0);
	break;
      default:
	goto error;
    }

  return;

 error:
  session_error(session, ret);
}

static void
timeout_cb(int fd, short what, void *arg)
{
  struct sp_session *session = arg;

  sp_errmsg = "Timeout waiting for Spotify response";

  session_error(session, SP_ERR_TIMEOUT);
}

static void
response_cb(int fd, short what, void *arg)
{
  struct sp_session *session = arg;
  struct sp_connection *conn = &session->conn;
  struct sp_channel *channel = session->now_streaming_channel;
  int ret;

  if (what == EV_READ)
    {
      ret = evbuffer_read(conn->incoming, fd, -1);
      if (ret == 0)
	RETURN_ERROR(SP_ERR_NOCONNECTION, "The access point disconnected");
      else if (ret < 0)
	RETURN_ERROR(SP_ERR_NOCONNECTION, "Connection to Spotify returned an error");

//      sp_cb.logmsg("Received data len %d\n", ret);
    }

  ret = response_read(session);
  switch (ret)
    {
      case SP_OK_WAIT: // Incomplete, wait for more data
	break;
      case SP_OK_DATA:
        if (channel->is_writing && !channel->file.end_of_file)
	  session->msg_type_next = MSG_TYPE_CHUNK_REQUEST;
	if (channel->progress_cb)
	  channel->progress_cb(channel->audio_fd[0], channel->cb_arg, 4 * channel->file.received_words - SP_OGG_HEADER_LEN, 4 * channel->file.len_words - SP_OGG_HEADER_LEN);

	event_del(conn->timeout_ev);
	event_add(channel->audio_write_ev, NULL);
	break;
      case SP_OK_DONE: // Got the response we expected, but possibly more to process
	if (evbuffer_get_length(conn->incoming) > 0)
	  event_active(conn->response_ev, 0, 0);

	event_del(conn->timeout_ev);
	event_active(session->continue_ev, 0, 0);
	break;
      case SP_OK_OTHER: // Not the response we were waiting for, check for other
	if (evbuffer_get_length(conn->incoming) > 0)
	  event_active(conn->response_ev, 0, 0);
	break;
      default:
	event_del(conn->timeout_ev);
	goto error;
    }

  return;

 error:
  session_error(session, ret);
}

static int
relogin(enum sp_msg_type type, struct sp_session *session)
{
  int ret;

  if (session->msg_type_queued != MSG_TYPE_NONE)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Cannot send message, another request is waiting for handshake");

  ret = request_make(MSG_TYPE_CLIENT_HELLO, session);
  if (ret < 0)
    RETURN_ERROR(ret, sp_errmsg);

  // In case we lost connection to the AP we have to make a new handshake for
  // the non-handshake message types. So queue the message until the handshake
  // is complete.
  session->msg_type_queued = type;
  return 0;

 error:
  return ret;
}

static int
request_make(enum sp_msg_type type, struct sp_session *session)
{
  struct sp_message msg;
  struct sp_connection *conn = &session->conn;
  struct sp_conn_callbacks cb = { sp_evbase, response_cb, timeout_cb };
  int ret;

  // Make sure the connection is in a state suitable for sending this message
  ret = ap_connect(type, &cb, session);
  if (ret == SP_OK_WAIT)
    return relogin(type, session); // Can't proceed right now, the handshake needs to complete first
  else if (ret < 0)
    RETURN_ERROR(ret, sp_errmsg);

  ret = msg_make(&msg, type, session);
  if (type == MSG_TYPE_CLIENT_RESPONSE_ENCRYPTED)
    memset(session->credentials.password, 0, sizeof(session->credentials.password));
  if (ret < 0)
    RETURN_ERROR(SP_ERR_INVALID, "Error constructing message to Spotify");

  if (msg.encrypt)
    conn->is_encrypted = true;

  ret = msg_send(&msg, conn);
  if (ret < 0)
    RETURN_ERROR(ret, sp_errmsg);

  // Only start timeout timer if a response is expected, otherwise go straight
  // to next message
  if (msg.response_handler)
    event_add(conn->timeout_ev, &sp_response_timeout_tv);
  else
    event_active(session->continue_ev, 0, 0);

  session->msg_type_next = msg.type_next;
  session->response_handler = msg.response_handler;

  return 0;

 error:
  return ret;
}


/* ----------------------------- Implementation ----------------------------- */

// This command is async
static enum command_state
track_write(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session;
  struct sp_channel *channel;
  int ret;

  *retval = 0;

  session = session_find_by_fd(cmdargs->fd_read);
  if (!session)
    RETURN_ERROR(SP_ERR_NOSESSION, "Cannot play track, no valid session found");

  channel = session->now_streaming_channel;
  if (!channel || !channel->is_allocated)
    RETURN_ERROR(SP_ERR_INVALID, "No active channel to play, has track been opened?");

  channel_play(channel);

  ret = request_make(MSG_TYPE_CHUNK_REQUEST, session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Could not send request for audio chunk");

  channel->progress_cb = cmdargs->progress_cb;
  channel->cb_arg = cmdargs->cb_arg;

  return COMMAND_END;

 error:
  sp_cb.logmsg("Error %d: %s", ret, sp_errmsg);

  return COMMAND_END;
}

static enum command_state
track_pause(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session;
  struct sp_channel *channel;
  int ret;

  session = session_find_by_fd(cmdargs->fd_read);
  if (!session)
    RETURN_ERROR(SP_ERR_NOSESSION, "Cannot pause track, no valid session found");

  channel = session->now_streaming_channel;
  if (!channel || !channel->is_allocated)
    RETURN_ERROR(SP_ERR_INVALID, "No active channel to pause, has track been opened?");

  // If we are playing we are in the process of downloading a chunk, and in that
  // case we need that to complete before doing anything else with the channel,
  // e.g. reset it as track_close() does.
  if (!channel->is_writing)
    {
      *retval = 0;
      return COMMAND_END;
    }

  channel_pause(channel);

  *retval = 1;
  return COMMAND_PENDING;

 error:
  *retval = ret;
  return COMMAND_END;
}

static enum command_state
track_seek(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session;
  struct sp_channel *channel;
  int ret;

  session = session_find_by_fd(cmdargs->fd_read);
  if (!session)
    RETURN_ERROR(SP_ERR_NOSESSION, "Cannot seek, no valid session found");

  channel = session->now_streaming_channel;
  if (!channel || !channel->is_allocated)
    RETURN_ERROR(SP_ERR_INVALID, "No active channel to seek, has track been opened?");
  else if (channel->is_writing)
    RETURN_ERROR(SP_ERR_INVALID, "Seeking during playback not currently supported");

  // This operation is not safe during chunk downloading because it changes the
  // AES decryptor to match the new position. It also flushes the pipe.
  channel_seek(channel, cmdargs->seek_pos);

  ret = request_make(MSG_TYPE_CHUNK_REQUEST, session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Could not send track seek request");

  *retval = 1;
  return COMMAND_PENDING;

 error:
  *retval = ret;
  return COMMAND_END;
}

static enum command_state
track_close(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session;
  int ret;

  session = session_find_by_fd(cmdargs->fd_read);
  if (!session)
    RETURN_ERROR(SP_ERR_NOSESSION, "Cannot close track, no valid session found");

  channel_free(session->now_streaming_channel);
  session->now_streaming_channel = NULL;

  *retval = 0;
  return COMMAND_END;

 error:
  *retval = ret;
  return COMMAND_END;
}

static enum command_state
track_open(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session = cmdargs->session;
  struct sp_channel *channel = NULL;
  int ret;

  ret = session_check(session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOSESSION, "Cannot open track, session is invalid");

  if (session->now_streaming_channel)
    RETURN_ERROR(SP_ERR_OCCUPIED, "Already getting a track");

  ret = channel_new(&channel, session, cmdargs->path, sp_evbase, audio_write_cb);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_OOM, "Could not setup a channel");

  cmdargs->fd_read = channel->audio_fd[0];

  // Must be set before calling request_make() because this info is needed for
  // making the request
  session->now_streaming_channel = channel;

  // Kicks of a sequence where we first get file info, then get the AES key and
  // then the first chunk (incl. headers)
  ret = request_make(MSG_TYPE_MERCURY_TRACK_GET, session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOCONNECTION, "Could not send track request");

  *retval = 1;
  return COMMAND_PENDING;

 error:
  if (channel)
    {
      session->now_streaming_channel = NULL;
      channel_free(channel);
    }

  *retval = ret;
  return COMMAND_END;
}

static enum command_state
track_open_bh(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;

  if (*retval == SP_OK_DONE)
    *retval = cmdargs->fd_read;

  return COMMAND_END;
}

static enum command_state
login(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session = NULL;
  int ret;

  ret = session_new(&session, cmdargs, continue_cb);
  if (ret < 0)
    goto error;

  ret = request_make(MSG_TYPE_CLIENT_HELLO, session);
  if (ret < 0)
    goto error;

  cmdargs->session = session;

  *retval = 1; // Pending command_exec_sync, i.e. response from Spotify
  return COMMAND_PENDING;

 error:
  session_cleanup(session);

  *retval = ret;
  return COMMAND_END;
}

static enum command_state
login_bh(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;

  if (*retval == SP_OK_DONE)
    cmdargs->session->is_logged_in = true;
  else
    cmdargs->session = NULL;

  return COMMAND_END;
}

static enum command_state
logout(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session = cmdargs->session;
  int ret;

  ret = session_check(session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOSESSION, "Session has disappeared, cannot logout");

  session_cleanup(session);

 error:
  *retval = ret;
  return COMMAND_END;
}

static enum command_state
metadata_get(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session;
  struct sp_metadata *metadata = cmdargs->metadata;
  int ret = 0;

  session = session_find_by_fd(cmdargs->fd_read);
  if (!session || !session->now_streaming_channel)
    RETURN_ERROR(SP_ERR_NOSESSION, "Session has disappeared, cannot get metadata");

  memset(metadata, 0, sizeof(struct sp_metadata));
  metadata->file_len = 4 * session->now_streaming_channel->file.len_words - SP_OGG_HEADER_LEN;;

 error:
  *retval = ret;
  return COMMAND_END;
}

static enum command_state
bitrate_set(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session = cmdargs->session;
  int ret;

  if (cmdargs->bitrate == SP_BITRATE_ANY)
    cmdargs->bitrate = SP_BITRATE_DEFAULT;

  ret = session_check(session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOSESSION, "Session has disappeared, cannot set bitrate");

  session->bitrate_preferred = cmdargs->bitrate;

 error:
  *retval = ret;
  return COMMAND_END;
}

static enum command_state
credentials_get(void *arg, int *retval)
{
  struct sp_cmdargs *cmdargs = arg;
  struct sp_session *session = cmdargs->session;
  struct sp_credentials *credentials = cmdargs->credentials;
  int ret;

  ret = session_check(session);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_NOSESSION, "Session has disappeared, cannot get credentials");

  memcpy(credentials, &session->credentials, sizeof(struct sp_credentials));

 error:
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
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.session  = session;
  cmdargs.path     = path;

  return commands_exec_sync(sp_cmdbase, track_open, track_open_bh, &cmdargs);
}

int
spotifyc_seek(int fd, size_t pos)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.fd_read  = fd;
  cmdargs.seek_pos = pos;

  return commands_exec_sync(sp_cmdbase, track_seek, NULL, &cmdargs);
}

// Starts writing audio for the caller to read from the file descriptor
void
spotifyc_write(int fd, sp_progress_cb progress_cb, void *cb_arg)
{
  struct sp_cmdargs *cmdargs;

  cmdargs = calloc(1, sizeof(struct sp_cmdargs));

  cmdargs->fd_read     = fd;
  cmdargs->progress_cb = progress_cb;
  cmdargs->cb_arg      = cb_arg;

  commands_exec_async(sp_cmdbase, track_write, cmdargs);
}

int
spotifyc_close(int fd)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.fd_read  = fd;

  return commands_exec_sync(sp_cmdbase, track_pause, track_close, &cmdargs);
}

struct sp_session *
spotifyc_login_password(const char *username, const char *password)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.username = username;
  cmdargs.password = password;

  commands_exec_sync(sp_cmdbase, login, login_bh, &cmdargs);

  return cmdargs.session;
}

struct sp_session *
spotifyc_login_stored_cred(const char *username, uint8_t *stored_cred, size_t stored_cred_len)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.username        = username;
  cmdargs.stored_cred     = stored_cred;
  cmdargs.stored_cred_len = stored_cred_len;

  commands_exec_sync(sp_cmdbase, login, login_bh, &cmdargs);

  return cmdargs.session;
}

struct sp_session *
spotifyc_login_token(const char *username, uint8_t *token, size_t token_len)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.username        = username;
  cmdargs.token           = token;
  cmdargs.token_len       = token_len;

  commands_exec_sync(sp_cmdbase, login, login_bh, &cmdargs);

  return cmdargs.session;
}

int
spotifyc_logout(struct sp_session *session)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.session         = session;

  return commands_exec_sync(sp_cmdbase, logout, NULL, &cmdargs);
}

int
spotifyc_metadata_get(struct sp_metadata *metadata, int fd)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.metadata = metadata;
  cmdargs.fd_read  = fd;

  return commands_exec_sync(sp_cmdbase, metadata_get, NULL, &cmdargs);
}

int
spotifyc_bitrate_set(struct sp_session *session, enum sp_bitrates bitrate)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.session  = session;
  cmdargs.bitrate  = bitrate;

  return commands_exec_sync(sp_cmdbase, bitrate_set, NULL, &cmdargs);
}

int
spotifyc_credentials_get(struct sp_credentials *credentials, struct sp_session *session)
{
  struct sp_cmdargs cmdargs = { 0 };

  cmdargs.credentials = credentials;
  cmdargs.session = session;

  return commands_exec_sync(sp_cmdbase, credentials_get, NULL, &cmdargs);
}

const char *
spotifyc_last_errmsg(void)
{
  return sp_errmsg ? sp_errmsg : "(no error)";
}

int
spotifyc_init(struct sp_sysinfo *sysinfo, struct sp_callbacks *callbacks)
{
  int ret;

  if (sp_initialized)
    RETURN_ERROR(SP_ERR_INVALID, "spotifyc already initialized");

  sp_cb     = *callbacks;
  sp_initialized = true;

  memcpy(&sp_sysinfo, sysinfo, sizeof(struct sp_sysinfo));

  sp_evbase = event_base_new();
  if (!sp_evbase)
    RETURN_ERROR(SP_ERR_OOM, "event_base_new() failed");

  sp_cmdbase = commands_base_new(sp_evbase, NULL);
  if (!sp_cmdbase)
    RETURN_ERROR(SP_ERR_OOM, "commands_base_new() failed");

  ret = pthread_create(&sp_tid, NULL, spotifyc, NULL);
  if (ret < 0)
    RETURN_ERROR(SP_ERR_OOM, "Could not start thread");

  if (sp_cb.thread_name_set)
    sp_cb.thread_name_set(sp_tid);

  return 0;

 error:
  spotifyc_deinit();
  return ret;
}

void
spotifyc_deinit()
{
  struct sp_session *session;

  commands_base_destroy(sp_cmdbase);
  sp_cmdbase = NULL;

  for (session = sp_sessions; sp_sessions; session = sp_sessions)
    {
      sp_sessions = session->next;
      session_free(session);
    }

  pthread_join(sp_tid, NULL);

  event_base_free(sp_evbase);
  sp_evbase = NULL;

  sp_initialized = false;
  memset(&sp_cb, 0, sizeof(struct sp_callbacks));

  return;
}
