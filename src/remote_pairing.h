
#ifndef __REMOTE_PAIRING_H__
#define __REMOTE_PAIRING_H__

#define REMOTE_ERROR -1
#define REMOTE_INVALID_PIN -2

void
remote_pairing_kickoff(char **arglist);

int
remote_pairing_pair(const char *pin);

char *
remote_pairing_get_name(void);

int
remote_pairing_init(void);

void
remote_pairing_deinit(void);

#endif /* !__REMOTE_PAIRING_H__ */
