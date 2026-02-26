#ifndef __AIRPTP_INTERNAL_H__
#define __AIRPTP_INTERNAL_H__

#include <event2/event.h>
#include <pthread.h>
#include <inttypes.h>

#include "airptp.h"
#include "utils.h"

#define AIRPTP_SHM_NAME "/airptp_shm"

#define AIRPTP_SHM_STRUCTS_VERSION_MAJOR 0
#define AIRPTP_SHM_STRUCTS_VERSION_MINOR 1

// If the ts is older than this we consider the daemon or peer gone
#define AIRPTP_STALE_SECS 15

#define AIRPTP_DOMAIN 0
#define AIRPTP_MAX_PEERS 32

#define RETURN_ERROR(r, m) \
  do { ret = (r); airptp_errmsg = (m); goto error; } while(0)

extern const char __thread *airptp_errmsg;

// The log2 of the announce message interval in seconds. The ATV uses -2, which
// would be 0.25 sec, my amp uses 0, so 1 sec, as does nqptp.
// See nqptp-ptp-definitions.h.
#define AIRPTP_LOGMESSAGEINT_ANNOUNCE 0
#define AIRPTP_INTERVAL_MS_ANNOUNCE 1000
// Both iOS, ATV, amp and nqptp use -3, so 0.125 sec.
#define AIRPTP_LOGMESSAGEINT_SYNC -3
#define AIRPTP_INTERVAL_MS_SYNC 125
// Used by iOS
#define AIRPTP_LOGMESSAGEINT_SIGNALING -128
#define AIRPTP_INTERVAL_MS_SIGNALING 1000
#define AIRPTP_LOGMESSAGEINT_DELAY_RESP -3

enum airptp_error
{
  AIRPTP_OK           = 0,
  AIRPTP_ERR_INVALID  = -1,
  AIRPTP_ERR_NOCONNECTION = -2,
  AIRPTP_ERR_NOTFOUND = -3,
  AIRPTP_ERR_OOM = -4,
  AIRPTP_ERR_INTERNAL = -5,
};

// TODO maybe not needed
enum airptp_state
{
  AIRPTP_STATE_NONE = 0,
  AIRPTP_STATE_PORTS_BOUND,
  AIRPTP_STATE_RUNNING,
};

struct airptp_shm_struct
{
  uint16_t version_major;
  uint16_t version_minor;
  uint64_t clock_id;
  time_t ts;
};

struct airptp_service
{
  int fd;
  unsigned short port;
  struct event *ev;
};

struct airptp_peer
{
  uint32_t id;
  union net_sockaddr naddr;
  socklen_t naddr_len;
  bool is_active;
  uint64_t last_seen;
};

struct airptp_daemon
{
  bool is_shared;
  struct airptp_shm_struct *info; // mmap

  uint64_t clock_id;

  bool is_running;
  pthread_t tid;
  struct event_base *evbase;

  pthread_mutex_t lock;
  pthread_cond_t cond;

  int exit_pipe[2];
  struct event *start_stop_ev;

  struct airptp_service event_svc;
  struct airptp_service general_svc;

  struct event *shm_update_timer;

  struct event *send_announce_timer;
  struct event *send_signaling_timer;
  struct event *send_sync_timer;

  uint16_t announce_seq;
  uint16_t signaling_seq;
  uint16_t sync_seq;

  struct airptp_callbacks cb;

  struct airptp_peer peers[AIRPTP_MAX_PEERS];
  int num_peers;
};

struct airptp_handle
{
  bool is_daemon;
  enum airptp_state state;

  struct airptp_daemon daemon;

  uint64_t clock_id;
};

void
hexdump(const char *msg, void *data, size_t data_len);

void
logmsg(const char *fmt, ...);

void
thread_name_set(const char *name);

#endif // __AIRPTP_INTERNAL_H__
