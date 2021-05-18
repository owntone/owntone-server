void
ap_disconnect(struct sp_connection *conn);

enum sp_error
ap_connect(enum sp_msg_type type, struct sp_conn_callbacks *cb, struct sp_session *session);

enum sp_error
response_read(struct sp_session *session);

int
msg_make(struct sp_message *msg, enum sp_msg_type type, struct sp_session *session);

int
msg_send(struct sp_message *msg, struct sp_connection *conn);

int
msg_pong(struct sp_session *session);
