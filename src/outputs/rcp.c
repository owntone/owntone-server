/*
 * Copyright (C) 2022 Ray <whatdoineed2do@gmail.com>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"
#include "mdns.h"


/* RCP is the Roku Soundbridge control protocol; we can request a Soundbridge to
 * play data from our internal .mp3 stream and the Soundbridge will act as an
 * output
 *
 * References to the RCP spec are related to the Roku Functional Specification
 * dated 09-Aug-2007, document version 2.4 and software versions SoundBridge 3.0.44
 *
 * RCP spec page 7 - Overview, What is RCP?
 *   [...] the Roku Control Protocol (RCP). RCP is a control
 *   protocol implemented by the Roku SoundBridge line of digital audio players with
 *   software version 2.3 or later, and the Roku Wi-Fi Media Module (WMM), a drop-
 *   in hardware solution for implementing digital audio functionality targeted for
 *   OEMs. Remote applications can use RCP to access the digital-media
 *   functionality of those device to automate repetitive tasks, initiate and control
 *   media playback, or extend the user interface to other network-connected
 *   devices.
 *
 * RCP spec page 10 - Protocol Summary
 *   RCP was designed with simplicity and completeness as primary requirements.
 *   Commands and results are exchanged as short transmissions across a high-
 *   speed interface like a serial port, telnet connection, or parallel interface. Each
 *   command is composed of a short ASCII command id string, generally just zero or
 *   one parameters, and the two-byte terminator CRLF. All command results from
 *   the RCP host are composed of the command-id of the client command that
 *   caused this result followed by a result string and the two-byte CRLF terminator
 *
 *   ...
 *
 *   RCP commands can be loosely categorized by the way in which they execute:
 *   synchronous commands, transacted commands, and subscription commands.
 *   Synchronous commands return their results immediately, and do not block the
 *   host device during execution. Transacted commands are commands that might
 *   require a long extent of time to complete, some as long as ten seconds or more!
 *   These commands generally depend on sending and receiving data from the
 *   network, which is why their completion time is non-deterministic. Transacted
 *   commands run asynchronously, or “in the background,” in an RCP session, and
 *   allow the client to issue other commands or cancel the command while it is in
 *   process.
 *
 * Communcations within this module only use the sync subset of commands from
 * the RCP spec
 *
 * RCP/Roku devices only support ipv4
 */
enum rcp_state
{
  RCP_STATE_SETUP,					// 0
  RCP_STATE_SETUP_WAKEUP,

  RCP_STATE_SETUP_GET_CONNECTED_SERVER,
  RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_INIT,
  RCP_STATE_SETUP_SERVER_DISCONNECT_DISCONNECTED,
  RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_END,		// 5
  RCP_STATE_SETUP_SERVER_DISCONNECT,
  RCP_STATE_SETUP_SET_SERVER_FILTER,
  RCP_STATE_SETUP_LIST_SERVERS_RESULT_SIZE,
  RCP_STATE_SETUP_LIST_SERVERS_INTERNET_RADIO,
  RCP_STATE_SETUP_LIST_SERVERS_RESULTS_END,		// 10
  RCP_STATE_SETUP_LIST_SERVERS,
  RCP_STATE_SETUP_SERVER_CONNECT_TRANS_INIT,
  RCP_STATE_SETUP_SERVER_CONNECT_CONNECTED,
  RCP_STATE_SETUP_SERVER_CONNECT_TRANS_END,
  RCP_STATE_SETUP_SERVER_CONNECT,			// 15

  RCP_STATE_SETUP_VOL_GET,

  RCP_STATE_QUEUING_CLEAR,
  RCP_STATE_QUEUING_SET_TITLE,
  RCP_STATE_QUEUING_SET_PLAYLIST_URL,
  RCP_STATE_QUEUING_SET_REMOTE_STREAM,			// 20
  RCP_STATE_QUEUING_PLAY,

  RCP_STATE_STREAMING,

  RCP_STATE_VOL_GET,
  RCP_STATE_VOL_SET,

  RCP_STATE_STOPPING,					// 25

  RCP_STATE_SHUTDOWN_STOPPED,
  RCP_STATE_SHUTDOWN_GET_CONNECTED_SERVER,
  RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_INIT,
  RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_DISCONNECTED,
  RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_END,	// 30
  RCP_STATE_SHUTDOWN_SERVER_DISCONNECT,

  // grouped order
  RCP_STATE_STANDBY,
  RCP_STATE_DISCONNECTED,
  RCP_STATE_FAILED,					// 35

  RCP_STATE_MAX
};

struct rcp_state_map
{
  enum rcp_state state;
  char *cmd;
  bool has_arg;
};

// direct mapping to cmds against state, if applicable
static const struct rcp_state_map  rcp_state_send_map[] =
{
  { RCP_STATE_SETUP, NULL },
  { RCP_STATE_SETUP_WAKEUP, "SetPowerState on no" },

  { RCP_STATE_SETUP_GET_CONNECTED_SERVER, "GetConnectedServer" },
  { RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_INIT, "ServerDisconnect" },
  { RCP_STATE_SETUP_SERVER_DISCONNECT_DISCONNECTED, NULL },
  { RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_END, NULL },
  { RCP_STATE_SETUP_SERVER_DISCONNECT, NULL },
  { RCP_STATE_SETUP_SET_SERVER_FILTER, "SetServerFilter radio" },
  { RCP_STATE_SETUP_LIST_SERVERS_RESULT_SIZE, "ListServers" },
  { RCP_STATE_SETUP_LIST_SERVERS_INTERNET_RADIO, NULL },
  { RCP_STATE_SETUP_LIST_SERVERS_RESULTS_END, NULL },
  { RCP_STATE_SETUP_LIST_SERVERS, NULL },
  { RCP_STATE_SETUP_SERVER_CONNECT_TRANS_INIT, "ServerConnect 0" },
  { RCP_STATE_SETUP_SERVER_CONNECT_CONNECTED, NULL },
  { RCP_STATE_SETUP_SERVER_CONNECT_TRANS_END, NULL },
  { RCP_STATE_SETUP_SERVER_CONNECT, NULL },

  { RCP_STATE_SETUP_VOL_GET, "GetVolume" },

  { RCP_STATE_QUEUING_CLEAR, "ClearWorkingSong" },
  { RCP_STATE_QUEUING_SET_TITLE, "SetWorkingSongInfo title", true },
  { RCP_STATE_QUEUING_SET_PLAYLIST_URL, "SetWorkingSongInfo playlistURL", true },  // set from session's own url
  { RCP_STATE_QUEUING_SET_REMOTE_STREAM, "SetWorkingSongInfo remoteStream 1" },
  { RCP_STATE_QUEUING_PLAY, "QueueAndPlayOne working" },

  { RCP_STATE_STREAMING, NULL },

  { RCP_STATE_VOL_GET, "GetVolume" },
  { RCP_STATE_VOL_SET, "SetVolume", true },

  { RCP_STATE_STOPPING, NULL },

  { RCP_STATE_SHUTDOWN_STOPPED, "Stop" },
  { RCP_STATE_SHUTDOWN_GET_CONNECTED_SERVER, "GetConnectedServer" },
  { RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_INIT, "ServerDisconnect" },
  { RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_DISCONNECTED, NULL },
  { RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_END, NULL },
  { RCP_STATE_SHUTDOWN_SERVER_DISCONNECT, NULL },

  { RCP_STATE_STANDBY, "SetPowerState standby" },
  { RCP_STATE_DISCONNECTED, NULL },
  { RCP_STATE_FAILED, "Reboot" },

  { RCP_STATE_MAX, NULL },
};

struct rcp_session
{
  // enum output_device_state state;
  enum rcp_state state;

  int callback_id;

  char *devname;
  char *address;
  unsigned short port;
  int sock;
  char *stream_url;  // usues ip4 addr that the Roku believes we're on

  bool clear_on_close;
  unsigned close_timeout;

  // the rcp cmds are limited length - used to build response
#define RCP_RESP_BUF_SIZE 256
  char respbuf[RCP_RESP_BUF_SIZE+1];
  // pointer into our buffer, next available location for data resp
  char *respptr;

  // 0..100 incl
  unsigned short volume;
  struct output_device *device;

  struct event *ev;
  struct event *reply_timeout;

  struct rcp_session *next;
};

static struct rcp_session *rcp_sessions;

/* From player.c */
extern struct event_base *evbase_player;

// fwd
static int rcp_send(struct rcp_session* s, enum rcp_state next_state, const char *cmd);

/* ---------------------------- STATE MACHINE ------------------------------- */

/* Uses current state to determine valid response; returns
 *   -1 invalid req-resp reply
 *    0   valid req-resp reply
 *    1   valid req-resp reply but failed request
 */
static int
rcp_state_verify(struct rcp_session *s, const char *resp)
{
  int ret;

  switch (s->state)
  {
    case RCP_STATE_SETUP:
      if (strcmp(resp, "roku: ready\r\n") == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_SHUTDOWN_GET_CONNECTED_SERVER:
    case RCP_STATE_SETUP_GET_CONNECTED_SERVER:
      if (strcmp(resp, "GetConnectedServer: OK\r\n") == 0 || strcmp(resp, "GetConnectedServer: GenericError\r\n") == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_INIT:
    case RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_INIT:
      if (strcmp(resp, "ServerDisconnect: TransactionInitiated\r\n") == 0)
	{
	  if (s->state == RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_INIT)
	    s->state = RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_DISCONNECTED;
	  else
	    s->state = RCP_STATE_SETUP_SERVER_DISCONNECT_DISCONNECTED;
	  return 0;
	}
      // roku doesnt think its connected, no other resp for this state
      // seen that it goes directly into ErrorDisconnected without the xact init
      if (strcmp(resp, "ServerDisconnect: ErrorDisconnected\r\n") == 0)
	{
	  if (s->state == RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_INIT)
	    s->state = RCP_STATE_SHUTDOWN_SERVER_DISCONNECT;
	  else
	    s->state = RCP_STATE_SETUP_SERVER_DISCONNECT;
	  return 0;
	}
      if (strcmp(resp, "ServerDisconnect: ResourceAllocationError\r\n") == 0)
	{
	  // this state seems like a lockup on the roku, only clearable rebooting
	  rcp_send(s, RCP_STATE_FAILED, NULL);
	}
      if (strcmp(resp, "ServerDisconnect: GenericError\r\n") == 0)
	{
	  s->state = RCP_STATE_FAILED;
	  return -1;
	}
      goto resp_err;

    case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_DISCONNECTED:
    case RCP_STATE_SETUP_SERVER_DISCONNECT_DISCONNECTED:
      if (strcmp(resp, "ServerDisconnect: Disconnected\r\n") == 0 ||
          strcmp(resp, "ServerDisconnect: ErrorDisconnected\r\n") == 0)
	{
	  if (s->state == RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_DISCONNECTED)
	    s->state = RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_END;
	  else
	    s->state = RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_END;
	  return 0;
	}
      // drop through .. reported directly xact complete after init

    case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_END:
    case RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_END:
      if (strcmp(resp, "ServerDisconnect: TransactionComplete\r\n") == 0)
        {
          if (s->state == RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_END)
	    s->state = RCP_STATE_SHUTDOWN_SERVER_DISCONNECT;
	  else
	    s->state = RCP_STATE_SETUP_SERVER_DISCONNECT;
	  return 0;
	}
      goto resp_err;

    case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT:
    case RCP_STATE_SETUP_SERVER_DISCONNECT:
      break;

    case RCP_STATE_SETUP_SET_SERVER_FILTER:
      if (strcmp(resp, "SetServerFilter: OK\r\n") == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_SETUP_LIST_SERVERS_RESULT_SIZE:
      if (strcmp(resp, "ListServers: ListResultSize 1\r\n") == 0)
	{
	  s->state = RCP_STATE_SETUP_LIST_SERVERS_INTERNET_RADIO;
	  return 0;
	}
      goto resp_err;

    case RCP_STATE_SETUP_LIST_SERVERS_INTERNET_RADIO:
      if (strcmp(resp, "ListServers: Internet Radio\r\n") == 0)
	{
	  s->state = RCP_STATE_SETUP_LIST_SERVERS_RESULTS_END;
	  return 0;
	}
      goto resp_err;

    case RCP_STATE_SETUP_LIST_SERVERS_RESULTS_END:
      if (strcmp(resp, "ListServers: ListResultEnd\r\n") == 0)
	{
	  s->state = RCP_STATE_SETUP_LIST_SERVERS;
	  return 0;
	}
      goto resp_err;

    case RCP_STATE_SETUP_LIST_SERVERS:
      break;

    case RCP_STATE_SETUP_SERVER_CONNECT_TRANS_INIT:
      if (strcmp(resp, "ServerConnect: TransactionInitiated\r\n") == 0)
	{
	  s->state = RCP_STATE_SETUP_SERVER_CONNECT_CONNECTED;
	  return 0;
	}
      goto resp_err;

    case RCP_STATE_SETUP_SERVER_CONNECT_CONNECTED:
      if (strcmp(resp, "ServerConnect: Connected\r\n") == 0)
	{
	  s->state = RCP_STATE_SETUP_SERVER_CONNECT_TRANS_END;
	  return 0;
	}
      // drop through incase theres no response on this

    case RCP_STATE_SETUP_SERVER_CONNECT_TRANS_END:
      if (strcmp(resp, "ServerConnect: TransactionComplete\r\n") == 0)
	{
	  s->state = RCP_STATE_SETUP_SERVER_CONNECT;
	  return 0;
	}
      goto resp_err;

    case RCP_STATE_SETUP_SERVER_CONNECT:
      break;

    case RCP_STATE_SETUP_WAKEUP:
    case RCP_STATE_STANDBY:
      if (strcmp(resp, "SetPowerState: OK\r\n") == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_QUEUING_CLEAR:
      if (strcmp(resp, "ClearWorkingSong: OK\r\n") == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_QUEUING_SET_TITLE:
    case RCP_STATE_QUEUING_SET_PLAYLIST_URL:
    case RCP_STATE_QUEUING_SET_REMOTE_STREAM:
      if (strcmp(resp, "SetWorkingSongInfo: OK\r\n") == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_QUEUING_PLAY:
      if (strcmp(resp, "QueueAndPlayOne: OK\r\n") == 0)
	return 0;
 
      /* this means the address we used in request to play in
       * RCP_STATE_QUEUING_SET_PLAYLIST_URL is invalid
       */
      if (strcmp(resp, "QueueAndPlayOne: ParameterError\r\n") == 0)
	{
	  DPRINTF(E_LOG, L_RCP, "Failed to start stream, remote unable to reach '%s' from '%s' at %s\n", s->stream_url, s->devname, s->address);
	  return 1;
	}

      goto resp_err;

    case RCP_STATE_SHUTDOWN_STOPPED:
      if (strcmp(resp, "Stop: OK\r\n") == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_SETUP_VOL_GET:
    case RCP_STATE_VOL_GET:
      if (strncmp(resp, "GetVolume: ", strlen("GetVolume: ")) == 0)
	{
	  ret = sscanf(resp, "GetVolume: %hd", &s->volume);
	  if (ret < 0)
	    goto resp_err;

	  s->device->volume = s->volume;
	  return 0;
	}
      goto resp_err;

    case RCP_STATE_VOL_SET:
      if (strcmp(resp, "SetVolume: OK\r\n") == 0 ||
          strcmp(resp, "SetVolume: ParameterError\r\n")  == 0)
	return 0;
      goto resp_err;

    case RCP_STATE_STREAMING:
    case RCP_STATE_DISCONNECTED:
    case RCP_STATE_FAILED:
      // no resp in this state
      break;

    default:
      goto resp_err;
  }

  return 0;

resp_err:
  return -1;
}

void
rcp_session_shutdown(struct rcp_session* s, enum rcp_state state);

/* Handle current state, action and move to next state
 * returns -1 when machine is done
 */
static int
rcp_state_transition(struct rcp_session *s)
{
  switch (s->state)
    {
      case RCP_STATE_SETUP:
	rcp_send(s, RCP_STATE_SETUP_WAKEUP, NULL);
	break;

	  /* RCP spec - "Usage Scenario: Testing an Internet Radio URL", page 176:
	   *   To play back an arbitrary Internet Radio URL from RCP, you must set the
	   *   “working” song to identify the URL you want to play, make sure you are
	   *   connected to an appropriate music server, and then execute the
	   *   QueueAndPlayOne command
	   *
	   *   First, we ensure that we’re connected to the Internet Radio music server. Note
	   *   that we set the server filter to “radio” before invoking the ListServers command,
	   *   ensuring that the only list result is the built-in Internet Radio server [...]
	   *
	   * ->  GetConnectedServer
	   * << "GetConnectedServer: OK"
	   * ->  ServerDisconnect
	   * << "ServerDisconnect: TransactionInitiated"
	   * << "ServerDisconnect: Disconnected"
	   * << "ServerDisconnect: TransactionComplete"
	   * ->  SetServerFilter radio
	   * << "SetServerFilter: OK"
	   * ->  ListServers
	   * << "ListServers: ListResultSize 1"
	   * << "ListServers: Internet Radio"
	   * << "ListServers: ListResultEnd"
	   * ->  ServerConnect 0
	   * << "ServerConnect: TransactionInitiated"
	   * << "ServerConnect: Connected"
	   * << "ServerConnect: TransactionComplete"
	   * 
	   * ->  ClearWorkingSong
	   * << "ClearWorkingSong: OK"
	   * ->  SetWorkingSongInfo playlistURL http://ownetone.local:3689/stream.mp3
	   * << "SetWorkingSongInfo: OK"
	   * ->  SetWorkingSongInfo remoteStream 1
	   * << "SetWorkingSongInfo: OK"
	   * ->  QueueAndPlayOne working
	   * << "QueueAndPlayOne: OK"
	   *
	   * alternative but ICY meta not displayed
	   *
	   * RCP spec - "Usage Scenario: Playing a music File on the local network", page 177:
	   *   The RCP client may wish to play back a music files stored on the
	   *   local network that is not servedby amusic server. This is possible
	   *   using the RCP session’s working song variable. The RCP client must
	   *   setup the working song’s url and format fields and then call
	   *   QueueAndPlayOne. It is recommended to not set the remoteStream
	   *   field, as this will cause the file to be played back over 
	   *   automatically once it reaches the end of the file
	   *
	   *
	   * Note that the RCP spec has a copy/paste error; it refers to
	   * 'ClearWorkingSongInfo' which is an invalid command
	   *
	   * ->  ClearWorkingSong
	   * << "ClearWorkingSong: OK"
	   * ->  SetWorkingSongInfo title xxx
	   * << "SetWorkingSongInfo: OK"
	   * ->  SetWorkingSongInfo url http://ownetone.local:3689/stream.mp3
	   * << "SetWorkingSongInfo: OK"
	   * ->  SetWorkingSongInfo format MP3
	   * << "SetWorkingSongInfo: OK"
	   * ->  QueueAndPlayOne working
	   * << "QueueAndPlayOne: OK"
	   */

      case RCP_STATE_SETUP_WAKEUP:
	rcp_send(s, RCP_STATE_SETUP_GET_CONNECTED_SERVER, NULL);
	break;

      case RCP_STATE_SHUTDOWN_STOPPED:
	rcp_send(s, RCP_STATE_SHUTDOWN_GET_CONNECTED_SERVER, NULL);
	break;

      case RCP_STATE_SHUTDOWN_GET_CONNECTED_SERVER:
	rcp_send(s, RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_INIT, NULL);
	break;

      case RCP_STATE_SETUP_GET_CONNECTED_SERVER:
	rcp_send(s, RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_INIT, NULL);
	break;

      case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_INIT:
      case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_DISCONNECTED:
      case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT_TRANS_END:
      case RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_INIT:
      case RCP_STATE_SETUP_SERVER_DISCONNECT_DISCONNECTED:
      case RCP_STATE_SETUP_SERVER_DISCONNECT_TRANS_END:
	// no response, multistage response
	break;

      case RCP_STATE_SETUP_SERVER_DISCONNECT:
	rcp_send(s, RCP_STATE_SETUP_SET_SERVER_FILTER, NULL);
	break;

      case RCP_STATE_SETUP_SET_SERVER_FILTER:
	rcp_send(s, RCP_STATE_SETUP_LIST_SERVERS_RESULT_SIZE, NULL);
	break;

      case RCP_STATE_SETUP_LIST_SERVERS_RESULT_SIZE:
      case RCP_STATE_SETUP_LIST_SERVERS_INTERNET_RADIO:
      case RCP_STATE_SETUP_LIST_SERVERS_RESULTS_END:
	// no response, multistage response
	break;

      case RCP_STATE_SETUP_LIST_SERVERS:
	rcp_send(s, RCP_STATE_SETUP_SERVER_CONNECT_TRANS_INIT, NULL);
	break;

      case RCP_STATE_SETUP_SERVER_CONNECT_TRANS_INIT:
      case RCP_STATE_SETUP_SERVER_CONNECT_CONNECTED:
      case RCP_STATE_SETUP_SERVER_CONNECT_TRANS_END:
	break;

      case RCP_STATE_SETUP_SERVER_CONNECT:
	rcp_send(s, RCP_STATE_SETUP_VOL_GET, NULL);
	break;

      case RCP_STATE_SETUP_VOL_GET:
	rcp_send(s, RCP_STATE_QUEUING_CLEAR, NULL);
	break;

      case RCP_STATE_QUEUING_CLEAR:
	rcp_send(s, RCP_STATE_QUEUING_SET_TITLE, cfg_getstr(cfg_getsec(cfg, "library"), "name"));
	break;

      case RCP_STATE_QUEUING_SET_TITLE:
	rcp_send(s, RCP_STATE_QUEUING_SET_PLAYLIST_URL, s->stream_url);
	break;

      case RCP_STATE_QUEUING_SET_PLAYLIST_URL:
	rcp_send(s, RCP_STATE_QUEUING_SET_REMOTE_STREAM, NULL);
	break;

      case RCP_STATE_QUEUING_SET_REMOTE_STREAM:
	rcp_send(s, RCP_STATE_QUEUING_PLAY, NULL);
	break;

      case RCP_STATE_QUEUING_PLAY:
        DPRINTF(E_INFO, L_RCP, "Ready '%s' volume at %d\n", s->devname, s->volume);
	event_del(s->reply_timeout);
	// fall through

      case RCP_STATE_VOL_GET:
      case RCP_STATE_VOL_SET:
	s->state = RCP_STATE_STREAMING;
	break;

      case RCP_STATE_STOPPING:
	s->state = RCP_STATE_SHUTDOWN_STOPPED;
	break;

      case RCP_STATE_STREAMING:
	break;

      case RCP_STATE_SHUTDOWN_SERVER_DISCONNECT:
	rcp_send(s, RCP_STATE_STANDBY, NULL);
	break;

      case RCP_STATE_STANDBY:
	rcp_session_shutdown(s, RCP_STATE_DISCONNECTED);
	goto done;
	break;

      case RCP_STATE_DISCONNECTED:
	goto done;
	break;

      default:
	DPRINTF(E_WARN, L_RCP, "Unhandled state transition %d '%s'\n", s->state, s->devname);
    }
  return 0;

done:
  return -1;
}

static void
rcp_status(struct rcp_session *s);

// send to remote and transition to next state
static int
rcp_send(struct rcp_session* s, enum rcp_state next_state, const char *arg)
{
    struct iovec  iov[] = { 
      { NULL, 0 },  // cmd
      { "", 0 },  // arg spacer
      { "", 0 },  // arg
      { (void*)"\r\n", 2 }
    };
    const struct rcp_state_map *map = NULL;
    int ret;

    // ensure the state has a mapping
    for (int i=0; i<RCP_STATE_MAX; ++i)
      {
	if (rcp_state_send_map[i].state == next_state &&
	    rcp_state_send_map[i].cmd) 
	  {
	    map = &rcp_state_send_map[i];
	    break;
	  }
      }
    if (!map || (map && map->cmd == NULL))
      {
	DPRINTF(E_WARN, L_RCP, "BUG - state machine has no cmd for state %d on '%s'\n", s->state, s->devname);
	return -1;
      }

    iov[0].iov_base = (void*)map->cmd;
    iov[0].iov_len  = strlen(map->cmd);
    if (map->has_arg)
      {
	iov[1].iov_base = (void*)" ";
	iov[1].iov_len  = 1;
	iov[2].iov_base = (void*)(arg ? arg : "");
	iov[2].iov_len  = arg ? strlen(arg) : 0;
      }

//    DPRINTF(E_DBG, L_RCP, "Device %" PRIu64 " state %d send '%s%s%s'\n", s->device->id, s->state, (char*)(iov[0].iov_base), (char*)(iov[1].iov_base), (char*)(iov[2].iov_base));

    if (s->sock <= 0) {
	DPRINTF(E_LOG, L_RCP, "Ignoring send request on %s, state = %d\n", s->address, s->state);
	return -1;
    }

    ret = writev(s->sock, iov, 4);
    if (ret < 0)
      {
	s->state = RCP_STATE_FAILED;
	return -1;
      }
    if (ret == 0)
      {
	s->state = RCP_STATE_DISCONNECTED;
	return -1;
      }

    s->state = next_state;

    return 0;
}

/* Returns:
 *   -1  - failure of some kind on link
 *    0  - recv'd data
 *
 * reads data
 */
static int
rcp_recv(struct rcp_session *s)
{
  /* The RCP responses are of finite size so we can
   * limit the input buf
   */
  ssize_t recvd;
  const size_t  avail = RCP_RESP_BUF_SIZE - (s->respptr - s->respbuf);

  if (avail == 0)
    {
      DPRINTF(E_WARN, L_RCP, "Protocol BUG, cmd buf (%d) exhausted %" PRIu64 " state %d\n", RCP_RESP_BUF_SIZE, s->device->id, s->state);

      s->state = RCP_STATE_FAILED;
      return -1;
    }

  recvd = read(s->sock, s->respptr, avail);

//  DPRINTF(E_DBG,  L_RCP, "Device %" PRIu64 " state %d recv'd %zd bytes '%s'\n", s->device->id, s->state, recvd, s->respptr);
  if (recvd <= 0)
    {
      DPRINTF(E_LOG,  L_RCP, "Failed to read response from '%s' - %s\n", s->devname, strerror(recvd == 0 ? ECONNRESET : errno));
      s->state = RCP_STATE_DISCONNECTED;
      return -1;
    }

  s->respptr += recvd;
  return 0;
}

/* Returns non-null ptr to a single response
 * Roku can send multiple responses in packet
 */
static const char *
rcp_state_1resp(char *resp, struct rcp_session *s)
{
  char *p, *q;
  int len;
  char *ret = NULL;

  // find termination sequence of '\r\n' and adjust the incoming write position
  // s->respptr, accordingly

  // verify response termination sequence of '\r\n' but expected at least cmd + ':'
  // ie min response is 'A: OK\r\n'
  if (s->respptr == s->respbuf || s->respptr - s->respbuf < 6)
    return NULL;

  p = s->respbuf;
  q = p+1;
  while (q < s->respptr)
    {
      if (*p == '\r' && *q == '\n')
      {
	  len = q+1 - s->respbuf;
	  memcpy(resp, s->respbuf, len);
	  resp[len] = '\0';

	  // now slide the rest of the s->respbuf to begining
	  memmove(s->respbuf, s->respbuf+len, s->respptr - s->respbuf - len);
	  s->respptr -= len;
	  memset(s->respptr, 0, len);

	  ret = &resp[0];
      }
      ++p;
      ++q;

      if (ret)
	break;
   }
  return ret;
}

/* ---------------------------- SESSION HANDLING ---------------------------- */

void
rcp_disconnect(int fd)
{
  /* no more receptions */
  shutdown(fd, SHUT_RDWR);
  close(fd);
}

void
rcp_session_shutdown(struct rcp_session* s, enum rcp_state state)
{
  event_del(s->ev);
  event_del(s->reply_timeout);

  s->device->prevent_playback = 1;

  rcp_disconnect(s->sock);
  s->sock = -1;

  DPRINTF(E_INFO, L_RCP, "Disconnected '%s'\n", s->devname);

  // we've shutdown, ensure state is valid
  switch (state)
  {
    case RCP_STATE_STANDBY ... RCP_STATE_FAILED:
      break;

    default:
      state = RCP_STATE_FAILED;
  }
  s->state = state;
 
  rcp_status(s);
}


static void
rcp_reply_shutdown_timeout_cb(int fd, short what, void *arg)
{
  struct rcp_session *s;
  s = (struct rcp_session *)arg;

  if (what != EV_TIMEOUT)
    {
      DPRINTF(E_INFO, L_RCP, "Unexpected non timeout event (%d) %s at %s\n", what, s->devname, s->address);
      return;
    }

  DPRINTF(E_LOG, L_RCP, "No response from '%s' (state %d), forcing shutting down\n", s->devname, s->state);
  rcp_session_shutdown(s, RCP_STATE_DISCONNECTED);
}

static void
rcp_session_shutdown_init(struct rcp_session* s)
{
  struct timeval clear_timeout = { 15, 0 };

  if (s->reply_timeout)
    event_free(s->reply_timeout);

  s->reply_timeout = evtimer_new(evbase_player, rcp_reply_shutdown_timeout_cb, s);
  if (!s->reply_timeout)
    {
      DPRINTF(E_WARN, L_RCP, "Out of memory for shutdown reply_timeout on session\n");
      rcp_session_shutdown(s, RCP_STATE_DISCONNECTED);
    }
  else
    {
      DPRINTF(E_DBG, L_RCP, "Limiting shutdown timeout %ld sec '%s' at %s\n", clear_timeout.tv_sec, s->devname, s->address);

      // ensure we're not blocked forever on responses
      event_add(s->reply_timeout, &clear_timeout);

      /* force the Roku into a non-library connected state, otherwise re-power
       * will put Roku into pre-powerdown state which will cause it to reconnect
       * to this server
       *
       * some users prefer non Roku connected state
       */
      rcp_send(s,
	       s->clear_on_close ?
		  RCP_STATE_SHUTDOWN_GET_CONNECTED_SERVER :
		  RCP_STATE_STANDBY,
	      NULL);
    }
}

/* The core of this module. Libevent makes a callback to this function whenever
 * there is new data to be read on the fd from the Soundbridge.
 * Process data based on state machine
 */
static void
rcp_listen_cb(int fd, short what, void *arg)
{
  struct rcp_session *s;
  const char *p;
  char cmd[RCP_RESP_BUF_SIZE+1] = { 0 };
  int ret;

  for (s = rcp_sessions; s; s = s->next)
    {
      if (s == (struct rcp_session *)arg)
	break;
    }

  if (!s)
    {
      DPRINTF(E_INFO, L_RCP, "Callback on dead session, ignoring\n");
      return;
    }

  if (what == EV_TIMEOUT)
    {
      DPRINTF(E_LOG, L_RCP, "Unexpected timeout event on '%s', shutting down\n", s->devname);
      goto fail;
    }

  /* response from Soundbridge can be chunked: even simple initial response msg
   * can arrive a 'r' 'oku: read' 'y\r\n'
   *
   * concat the response into the cmd buffer
   */
  ret = rcp_recv(s);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RCP, "Failed to recv/construct response from '%s'\n", s->devname);
      goto fail;
    }

  // process all full responses in s->respbuf
  while ( (p = rcp_state_1resp(cmd, s)) )
    {
      // ensure respose matches state otherwise state machine is out of whack
      ret = rcp_state_verify(s, cmd);
      if (ret < 0)
	goto resp_fail;
      if (ret > 0)
	goto fail;
      memset(cmd, 0, sizeof(cmd));

      ret = rcp_state_transition(s);
      if (ret < 0)
	// all done
	break;
      rcp_status(s);
    }

  return;

 resp_fail:
  DPRINTF(E_WARN, L_RCP, "Unexpected response (parsed cmd '%s' remaining buf '%s') in state %d '%s' at %s\n", cmd, s->respbuf, s->state, s->devname, s->address);

 fail:
  // Downgrade state to make rcp_session_shutdown perform an exit which is
  // quick and won't require a reponse from remote
  s->state = RCP_STATE_FAILED;
  s->device->prevent_playback = 1;
  rcp_session_shutdown(s, RCP_STATE_FAILED);
}

static void
rcp_reply_timeout_cb(int fd, short what, void *arg)
{
  struct rcp_session *s;
  s = (struct rcp_session *)arg;

  if (what == EV_TIMEOUT && s->state != RCP_STATE_STREAMING)
    {
      DPRINTF(E_LOG, L_RCP, "Slow response from '%s' (state %d), shutting down\n", s->devname, s->state);

      s->state = RCP_STATE_FAILED;
      s->device->prevent_playback = 1;
      rcp_session_shutdown(s, RCP_STATE_FAILED);

      event_del(s->reply_timeout);
    }
}


/* RCP spec - "RCP Sessions" #2, page 8:
 *   Telnet (TCP port 5555) – SoundBridge and WMM devices listen on TCP port 
 *   5555 at their configured IPaddress for incoming connections, and expose the
 *   RCP shell directly on this connection. Once connected, the device will 
 *   answer with the RCP initiation sequence, “roku:ready”, indicating that the
 *   connection is ready for commands.
 */
static struct rcp_session *
rcp_session_make(struct output_device *device, int callback_id)
{
  struct rcp_session *s;
  cfg_t *cfgrcp;
  struct timeval rcp_resp_timeout = { 20, 0 };
  int flags;

  struct sockaddr_storage ss = { 0 };
  ev_socklen_t socklen = sizeof(ss);
  char addrbuf[128] = { 0 };
  void *inaddr;
  const char *addr;
  int httpd_port;
  int ret;

  CHECK_NULL(L_RCP, s = calloc(1, sizeof(struct rcp_session)));

  s->state = RCP_STATE_SETUP;
  s->callback_id = callback_id;
  s->device = device;
  s->respptr = s->respbuf;

  cfgrcp = cfg_gettsec(cfg, "rcp", device->name);
  s->clear_on_close = cfgrcp ? cfg_getbool(cfgrcp, "clear_on_close") : false;

  s->sock = net_connect(device->v4_address, device->v4_port, SOCK_STREAM, "RCP control");
  if (s->sock < 0)
    {
      DPRINTF(E_LOG, L_RCP, "Could not connect to %s\n", device->name);
      goto out_free_session;
    }

  ret = getsockname(s->sock, (struct sockaddr*)&ss, &socklen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RCP, "Could not determine client's connected address %s\n", device->name);
      goto out_close_connection;
    }

  inaddr = &((struct sockaddr_in*)&ss)->sin_addr;
  addr = evutil_inet_ntop(ss.ss_family, inaddr, addrbuf, sizeof(addrbuf));
  if (!addr)
    {
      DPRINTF(E_LOG, L_RCP, "Could not determine client's connected address %s\n", device->name);
      goto out_close_connection;
    }

  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");
  s->stream_url = safe_asprintf("http://%s:%d/stream.mp3", addr, httpd_port);

  s->ev = event_new(evbase_player, s->sock, EV_READ | EV_PERSIST, rcp_listen_cb, s);
  if (!s->ev)
    {
      DPRINTF(E_LOG, L_RCP, "Out of memory for listener event\n");
      goto out_close_connection;
    }

  s->reply_timeout = evtimer_new(evbase_player, rcp_reply_timeout_cb, s);
  if (!s->reply_timeout)
    {
      DPRINTF(E_LOG, L_RCP, "Out of memory for reply_timeout\n");
      goto out_free_ev;
    }

  flags = fcntl(s->sock, F_GETFL, 0);
  fcntl(s->sock, F_SETFL, flags | O_NONBLOCK);

  event_add(s->ev, NULL);
  event_add(s->reply_timeout, &rcp_resp_timeout);

  s->devname = strdup(device->name);
  s->address = strdup(device->v4_address);
  s->volume = 0;

  s->next = rcp_sessions;
  rcp_sessions = s;

  DPRINTF(E_DBG, L_RCP, "Make session device %" PRIu64 " %s at %s stream url '%s'\n", s->device->id, s->devname, s->address, s->stream_url);

  // s is now the official device session
  outputs_device_session_add(device->id, s);

  DPRINTF(E_INFO, L_RCP, "Connection to '%s' established\n", s->devname);

  rcp_status(s);

  return s;

 out_free_ev:
  event_free(s->reply_timeout);
  event_free(s->ev);
 out_close_connection:
  rcp_disconnect(s->sock);
 out_free_session:
  free(s);

  return NULL;
}


static void
rcp_session_free(struct rcp_session *s)
{
  if (!s)
    return;

  if (s->sock >= 0)
    rcp_disconnect(s->sock);
  if (s->ev)
    event_free(s->ev);
  if (s->reply_timeout)
    event_free(s->reply_timeout);

  free(s->stream_url);
  free(s->address);
  free(s->devname);

  free(s);
}

static void
rcp_session_cleanup(struct rcp_session *rs)
{
  struct rcp_session *s;

  if (rs == rcp_sessions)
    rcp_sessions = rcp_sessions->next;
  else
    {
      for (s = rcp_sessions; s && (s->next != rs); s = s->next)
	; /* EMPTY */

      if (!s)
	DPRINTF(E_WARN, L_RCP, "WARNING: struct rcp_session not found in list; BUG!\n");
      else
	s->next = rs->next;
    }

  outputs_device_session_remove(rs->device->id);

  rcp_session_free(rs);
}


/* ---------------------------- STATUS HANDLERS ----------------------------- */

static void
rcp_status(struct rcp_session *s)
{
  enum output_device_state state;

  switch (s->state)
    {
      case RCP_STATE_SETUP:
      case RCP_STATE_SETUP_WAKEUP:
	state = OUTPUT_STATE_STARTUP;
	break;

      case RCP_STATE_SETUP_GET_CONNECTED_SERVER ... RCP_STATE_SETUP_VOL_GET:
      case RCP_STATE_QUEUING_CLEAR ... RCP_STATE_QUEUING_PLAY:
      case RCP_STATE_VOL_GET:
      case RCP_STATE_VOL_SET:
      case RCP_STATE_STOPPING ... RCP_STATE_STANDBY:
	state = OUTPUT_STATE_CONNECTED;
	break;

      case RCP_STATE_STREAMING:
	state = OUTPUT_STATE_STREAMING;
	break;

      case RCP_STATE_DISCONNECTED:
	state = OUTPUT_STATE_STOPPED;
	break;

      case RCP_STATE_FAILED:
      default:
	state = OUTPUT_STATE_FAILED;
    }

  DPRINTF(E_DBG, L_RCP, "Mapping state from (internal) %d -> (output) %d\n", s->state, state);
  outputs_cb(s->callback_id, s->device->id, state);
  s->callback_id = -1;

  if (state == OUTPUT_STATE_STOPPED || state == OUTPUT_STATE_FAILED)
    rcp_session_cleanup(s);
}


/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
rcp_device_start(struct output_device *device, int callback_id)
{
  struct rcp_session *s;

  s = rcp_session_make(device, callback_id);
  if (!s)
    return -1;

  return 1;
}

static int
rcp_device_stop(struct output_device *device, int callback_id)
{
  struct rcp_session *s = device->session;

  /* force these devices as deselected (auto state saves in db later) since 
   * these need use to select (and cause the device probe to start connection to
   * remote side
   */
  device->prevent_playback = 0;

  s->callback_id = callback_id;
  // tear this session down, incl free'ing it
  rcp_session_shutdown_init(s);

  return 1;
}

static int
rcp_device_flush(struct output_device *device, int callback_id)
{
  struct rcp_session *s = device->session;

  s->callback_id = callback_id;
  s->state = OUTPUT_STATE_STOPPED;

  rcp_status(s);

  return 1;
}

static int
rcp_device_probe(struct output_device *device, int callback_id)
{
  struct rcp_session *s;

  s = rcp_session_make(device, callback_id);
  if (!s)
    return -1;


  return 1;
}


static int
rcp_device_volume_set(struct output_device *device, int callback_id)
{
  struct rcp_session *s = device->session;
  char cmd[4];
  int ret;

  if (s->state != RCP_STATE_STREAMING)
    return 0;

  s->callback_id = callback_id;


  ret = snprintf(cmd, sizeof(cmd), "%d", device->volume);
  if (ret < 0) {
    return 0;
  }

  rcp_send(s, RCP_STATE_VOL_SET, cmd);

  return 1;
}

static void
rcp_device_cb_set(struct output_device *device, int callback_id)
{
  struct rcp_session *s = device->session;

  s->callback_id = callback_id;
}

static void
rcp_mdns_device_cb(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt)
{
  struct output_device *device;
  bool exclude;
  int ret;

  /* $ avahi-browse -vrt  _roku-rcp._tcp
	Server version: avahi 0.7; Host name: foo.local
	E Ifce Prot Name                       Type                 Domain
	+  eth0 IPv4 SoundBridge              _roku-rcp._tcp       local
	=  eth0 IPv4 SoundBridge              _roku-rcp._tcp       local
	   hostname = [SoundBridge.local]
	   address = [192.168.0.3]
	   port = [5555]
	   txt = []
	: Cache exhausted
	: All for now
   */

  exclude = cfg_getbool(cfg_gettsec(cfg, "rcp", name), "exclude");
  DPRINTF(E_DBG, L_RCP, "Event for %sRCP/SoundBridge device '%s' (address %s, port %d)\n", exclude ? "excluded " : "", name, address, port);
  
  if (exclude)
    {
      DPRINTF(E_INFO, L_RCP, "Excluding discovered RCP/SoundBridge device '%s' at %s\n", name, address);
      return;
    }

  CHECK_NULL(L_RCP, device = calloc(1, sizeof(struct output_device)));

  device->id = djb_hash(name, strlen(name));
  device->name = strdup(name);
  device->type = OUTPUT_TYPE_RCP;
  device->type_name = outputs_name(device->type);

  if (port < 0 || !address)
    {
      ret = player_device_remove(device);
    }
  else
    {
      // RCP/Roku Soundbridges only support ipv4
      device->v4_address = strdup(address);
      device->v4_port = port;

      DPRINTF(E_INFO, L_RCP, "Adding RCP output device '%s' at '%s'\n", name, address);

      ret = player_device_add(device);
    }

  if (ret < 0)
    outputs_device_free(device);
}


static int
rcp_init(void)
{
  cfg_t *cfg_rcp;
  int i;
  int ret;

  // validate best we can rcp_state_send_map has all the rcp_states
  assert(ARRAY_SIZE(rcp_state_send_map) == RCP_STATE_MAX+1);
  //DPRINTF(E_FATAL, L_RCP, "BUG: rcp_state_send_map[] (%d) out of sync with rcp_states (%d)\n", ARRAY_SIZE(rcp_state_send_map), RCP_STATE_MAX);

  for (i=0; i<RCP_STATE_MAX; i++)
    {
      assert(rcp_state_send_map[i].state == (enum rcp_state)i);
      //DPRINTF(E_FATAL, L_RCP, "BUG: rcp_state_send_map[%d] out of sync with enum rcp_states\n", i);
    }

  cfg_rcp = cfg_gettsec(cfg, "rcp", "*");
  if (cfg_rcp && cfg_getbool(cfg_rcp, "exclude"))
    {
      DPRINTF(E_LOG, L_RCP, "Excluding all RCP/SoundBridges\n");
      return 0;
    }

  ret = mdns_browse("_roku-rcp._tcp", rcp_mdns_device_cb, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_RCP, "Could not add mDNS browser for RCP/SoundBridge devices\n");
      return -1;
    }

  return 0;
}

static void
rcp_deinit(void)
{
  struct rcp_session *s;

  for (s = rcp_sessions; rcp_sessions; s = rcp_sessions)
    {
      rcp_sessions = s->next;
      rcp_session_cleanup(s);
    }
}

struct output_definition output_rcp =
{
  .name = "RCP/SoundBridge",
  .type = OUTPUT_TYPE_RCP,
  .priority = 99,
  .disabled = 0,
  .init = rcp_init,
  .deinit = rcp_deinit,
  .device_start = rcp_device_start,
  .device_stop = rcp_device_stop,
  .device_flush = rcp_device_flush,
  .device_probe = rcp_device_probe,
  .device_volume_set = rcp_device_volume_set,
  .device_cb_set = rcp_device_cb_set,
};
