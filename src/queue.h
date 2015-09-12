
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

/*
 * External representation of an item in a queue
 */
struct queue_item_info
{
  /* Item-Id is a unique id for this queue item. If the same item appears multiple
     times in the queue each corresponding queue item has its own id. */
  unsigned int item_id;

  /* Id of the file/item in the files database */
  unsigned int dbmfi_id;

  /* Length of the item in ms */
  unsigned int len_ms;

  /* Data type of the item */
  enum data_kind data_kind;
  /* Media type of the item */
  enum media_kind media_kind;
};

/*
 * External representation of a queue
 */
struct queue_info
{
  // The number of items in the queue
  unsigned int length;

  // The position (0-based) in the queue for the first item in the queue array
  unsigned int start_pos;
  // The number of items in the queue array
  unsigned int count;
  // The queue array (array of items infos)
  struct queue_item_info *queue;
};

struct queue *
queue_new();

void
queue_free(struct queue *queue);

unsigned int
queue_count(struct queue *queue);

int
queueitem_pos(struct queue_item *item, uint32_t dbmfi_id);

struct queue_item_info *
queue_get_byitemid(struct queue *queue, unsigned int item_id);

struct queue_item_info *
queue_get_byindex(struct queue *queue, unsigned int index, char shuffle);

struct queue_item_info *
queue_get_bypos(struct queue *queue, unsigned int item_id, unsigned int pos, char shuffle);

int
queue_index_byitemid(struct queue *queue, unsigned int item_id, char shuffle);

struct queue_item_info *
queue_next(struct queue *queue, unsigned int item_id, char shuffle, enum repeat_mode r_mode);

struct queue_item_info *
queue_prev(struct queue *queue, unsigned int item_id, char shuffle, enum repeat_mode r_mode);

struct queue_info *
queue_info_new_byindex(struct queue *queue, unsigned int index, unsigned int count, char shuffle);

struct queue_info *
queue_info_new_bypos(struct queue *queue, unsigned int item_id, unsigned int count, char shuffle);

void
queue_info_free(struct queue_info *qi);

void
queue_add(struct queue *queue, struct queue_item *item);

void
queue_add_after(struct queue *queue, struct queue_item *item, unsigned int item_id);

void
queue_move_bypos(struct queue *queue, unsigned int item_id, unsigned int from_pos, unsigned int to_offset, char shuffle);

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
queue_make(struct query_params *qp);

struct queue_item *
queue_make_pl(int plid);

struct queue_item *
queue_make_item(uint32_t dbmfi_id);

#endif /* SRC_QUEUE_H_ */
