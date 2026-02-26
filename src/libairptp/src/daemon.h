#ifndef __AIRPTP_DAEMON_H__
#define __AIRPTP_DAEMON_H__

int
daemon_peer_add(struct airptp_daemon *daemon, struct airptp_peer *peer);

int
daemon_peer_del(struct airptp_daemon *daemon, struct airptp_peer *peer);

enum airptp_error
daemon_start(struct airptp_daemon *daemon, bool is_shared, uint64_t clock_id, struct airptp_callbacks cb);

enum airptp_error
daemon_stop(struct airptp_daemon *daemon);

#endif // __AIRPTP_DAEMON_H__
