
#ifndef __CAST_H__
#define __CAST_H__

#include "raop.h"

int
cast_device_start(struct raop_device *rd, raop_status_cb cb);

/*void
raop_metadata_purge(void);

void
raop_metadata_prune(uint64_t rtptime);

struct raop_metadata *
raop_metadata_prepare(int id);

void
raop_metadata_send(struct raop_metadata *rmd, uint64_t rtptime, uint64_t offset, int startup);

int
raop_device_probe(struct raop_device *rd, raop_status_cb cb);

int
raop_device_start(struct raop_device *rd, raop_status_cb cb, uint64_t rtptime);

void
raop_device_stop(struct raop_session *rs);

void
raop_playback_start(uint64_t next_pkt, struct timespec *ts);

void
raop_playback_stop(void);

int
raop_set_volume_one(struct raop_session *rs, int volume, raop_status_cb cb);

int
raop_flush(raop_status_cb cb, uint64_t rtptime);


void
raop_set_status_cb(struct raop_session *rs, raop_status_cb cb);


void
raop_v2_write(uint8_t *buf, uint64_t rtptime);
*/

int
cast_init(void);

void
cast_deinit(void);

#endif /* !__CAST_H__ */
