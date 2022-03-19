void
ap_disconnect(struct sp_connection *conn);

enum sp_error
ap_connect(struct sp_connection *conn, enum sp_msg_type type, time_t *cooldown_ts, const char *ap_address, struct sp_conn_callbacks *cb, void *cb_arg);

const char *
ap_address_get(struct sp_connection *conn);

enum sp_error
response_read(struct sp_session *session);

bool
msg_is_handshake(enum sp_msg_type type);

int
msg_make(struct sp_message *msg, enum sp_msg_type type, struct sp_session *session);

int
msg_send(struct sp_message *msg, struct sp_connection *conn);

int
msg_pong(struct sp_session *session);
