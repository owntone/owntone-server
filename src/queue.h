
#ifndef SRC_QUEUE_H_
#define SRC_QUEUE_H_


#include "db.h"

enum repeat_mode {
  REPEAT_OFF  = 0,
  REPEAT_SONG = 1,
  REPEAT_ALL  = 2,
};


/*
 * Internal representation of a queue
 */
struct queue;

/*
 * Internal representation of a list of queue items
 */
struct queue_item;


struct queue *
queue_new();

void
queue_free(struct queue *queue);

unsigned int
queue_count(struct queue *queue);

int
queueitem_pos(struct queue_item *item, uint32_t id);

uint32_t
queueitem_id(struct queue_item *item);

unsigned int
queueitem_item_id(struct queue_item *item);

unsigned int
queueitem_len(struct queue_item *item);

enum data_kind
queueitem_data_kind(struct queue_item *item);

enum media_kind
queueitem_media_kind(struct queue_item *item);

struct queue_item *
queue_get_byitemid(struct queue *queue, unsigned int item_id);

struct queue_item *
queue_get_byindex(struct queue *queue, unsigned int index, char shuffle);

struct queue_item *
queue_get_bypos(struct queue *queue, unsigned int item_id, unsigned int pos, char shuffle);

int
queue_index_byitemid(struct queue *queue, unsigned int item_id, char shuffle);

struct queue_item *
queue_next(struct queue *queue, unsigned int item_id, char shuffle, enum repeat_mode r_mode, int reshuffle);

struct queue_item *
queue_prev(struct queue *queue, unsigned int item_id, char shuffle, enum repeat_mode r_mode);

struct queue *
queue_new_byindex(struct queue *queue, unsigned int index, unsigned int count, char shuffle);

struct queue *
queue_new_bypos(struct queue *queue, unsigned int item_id, unsigned int count, char shuffle);

void
queue_add(struct queue *queue, struct queue_item *item);

void
queue_add_after(struct queue *queue, struct queue_item *item, unsigned int item_id);

void
queue_move_bypos(struct queue *queue, unsigned int item_id, unsigned int from_pos, unsigned int to_offset, char shuffle);

void
queue_move_byindex(struct queue *queue, unsigned int from_pos, unsigned int to_pos, char shuffle);

void
queue_move_byitemid(struct queue *queue, unsigned int item_id, unsigned int to_pos, char shuffle);

void
queue_remove_byitemid(struct queue *queue, unsigned int item_id);

void
queue_remove_byindex(struct queue *queue, unsigned int index, char shuffle);

void
queue_remove_bypos(struct queue *queue, unsigned int item_id, unsigned int pos, char shuffle);

void
queue_clear(struct queue *queue);

void
queue_shuffle(struct queue *queue, unsigned int item_id);

struct queue_item *
queueitem_make_byquery(struct query_params *qp);

struct queue_item *
queueitem_make_byplid(int plid);

struct queue_item *
queueitem_make_byid(uint32_t id);

#endif /* SRC_QUEUE_H_ */
