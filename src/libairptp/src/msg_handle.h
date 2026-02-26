#ifndef __AIRPTP_MSG_HANDLE_H__
#define __AIRPTP_MSG_HANDLE_H__

void
msg_announce_send(struct airptp_daemon *daemon);

void
msg_signaling_send(struct airptp_daemon *daemon);

void
msg_sync_send(struct airptp_daemon *daemon);

int
msg_peer_add_send(struct airptp_peer *peer, struct airptp_handle *hdl, unsigned short port);

int
msg_peer_del_send(struct airptp_peer *peer, struct airptp_handle *hdl, unsigned short port);

void
msg_handle(struct airptp_daemon *daemon, uint8_t *msg, size_t msg_len, union net_sockaddr *peer_addr, socklen_t peer_addrlen);

int
msg_handle_init(void);

#endif // __AIRPTP_MSG_HANDLE_H__
