/*
 * Copyright (C) 2015 Christian Meffert <christian.meffert@googlemail.com>
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


#include "queue.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "misc.h"
#include "rng.h"


/*
 * Internal representation of an item in a queue. It links to the previous and the next item
 * in the queue for shuffle on/off. Only the queue_item_info can be exposed.
 */
struct queue_item
{
  /* Identifies the item in the db and the queue */
  struct queue_item_info qii;

  /* Link to the previous/next item in the queue */
  struct queue_item *next;
  struct queue_item *prev;

  /* Link to the previous/next item in the shuffle queue */
  struct queue_item *shuffle_next;
  struct queue_item *shuffle_prev;
};

/*
 * The queue struct references two (double) linked lists of queue_item. One for the play-queue
 * and one for the shuffle-queue.
 *
 * Both linked lists start with the "head" item. The head item is not a media item, instead it is
 * an internal item created during initialization of a new queue struct (see queue_new() function).
 * The head item is always the first item in the queue and will only be removed when queue is
 * destructed.
 *
 * The linked lists are circular, therefor the last item in a list has the first item (the head item)
 * as "next" and the first item in the queue (the head item) has the last item as "prev" linked.
 *
 * An empty queue (with no media items) will only consist of the head item pointing to itself.
 */
struct queue
{
  /* The queue item id of the last inserted item */
  unsigned int last_inserted_item_id;

  /* The version number of the queue */
  unsigned int version;

  /* Shuffle RNG state */
  struct rng_ctx shuffle_rng;

  /*
   * The head item in the queue is not an actual media item, instead it is the
   * starting point for the play-queue and the shuffle-queue. It always has the
   * item-id 0. The queue is circular, the last item of the queue has the head
   * item as "next" and the head item has the last item as "prev".
   */
  struct queue_item *head;
};


/*
 * Creates and initializes a new queue
 */
struct queue *
queue_new()
{
  struct queue *queue;

  queue = (struct queue *)calloc(1, sizeof(struct queue));
  queue->head = (struct queue_item *)calloc(1, sizeof(struct queue_item));

  // Create the head item and make the queue circular (head points to itself)
  queue->head->next = queue->head;
  queue->head->prev = queue->head;
  queue->head->shuffle_next = queue->head;
  queue->head->shuffle_prev = queue->head;

  rng_init(&queue->shuffle_rng);

  return queue;
}

/*
 * Frees the given item and all linked items
 */
static void
queue_items_free(struct queue_item *item)
{
  struct queue_item *temp;
  struct queue_item *next;

  item->prev->next = NULL;

  next = item;
  while (next)
    {
      temp = next->next;
      free(next);
      next = temp;
    }
}

/*
 * Frees the given queue and all the items in it
 */
void
queue_free(struct queue *queue)
{
  queue_items_free(queue->head);
  free(queue);
}

/*
 * Returns the number of media items in the queue
 *
 * @param queue The queue
 * @return      The number of items in the queue
 */
unsigned int
queue_count(struct queue *queue)
{
  struct queue_item *item;
  int count;

  count = 0;

  for (item = queue->head->next; item != queue->head; item = item->next)
    {
      count++;
    }

  return count;
}

/*
 * Returns the next item in the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 */
static struct queue_item *
item_next(struct queue_item *item, char shuffle)
{
  if (shuffle)
    return item->shuffle_next;
  return item->next;
}

/*
 * Returns the previous item in the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 */
static struct queue_item *
item_prev(struct queue_item *item, char shuffle)
{
  if (shuffle)
    return item->shuffle_prev;
  return item->prev;
}

/*
 * Returns the (0-based) position of the first item with the given dbmfi-id.
 * If no item is found for the given id, it returns -1.
 */
int
queueitem_pos(struct queue_item *item, uint32_t dbmfi_id)
{
  struct queue_item *temp;
  int pos;

  if (dbmfi_id == 0 || item->qii.dbmfi_id == dbmfi_id)
    return 0;

  pos = 1;
  for (temp = item->next; (temp != item) && temp->qii.dbmfi_id != dbmfi_id; temp = temp->next)
    {
      pos++;
    }

  if (temp == item)
    {
      // Item with given dbmfi_id does not exists
      return -1;
    }

  return pos;
}

/*
 * Returns the item with the given item_id in the queue
 *
 * @param queue   The queue
 * @param item_id The unique id of the item in the queue
 * @return        Item with the given item_id or NULL if not found
 */
static struct queue_item *
queueitem_get_byitemid(struct queue *queue, int item_id)
{
  struct queue_item *item;

  for (item = queue->head->next; item != queue->head && item->qii.item_id != item_id; item = item->next)
    {
      // Iterate through the queue until the item with item_id is found
    }

  if (item == queue->head && item_id != 0)
    return NULL;

  return item;
}

/*
 * Returns the item at the given index (0-based) in the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 *
 * @param queue   The queue
 * @param index   Index of item in the queue (0-based)
 * @param shuffle Play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 * @return        Item at position in the queue or NULL if not found
 */
static struct queue_item *
queueitem_get_byindex(struct queue *queue, unsigned int index, char shuffle)
{
  struct queue_item *item;
  int i;

  i = 0;
  for (item = item_next(queue->head, shuffle); item != queue->head && i < index; item = item_next(item, shuffle))
    {
      i++;
    }

  if (item == queue->head)
    return NULL;

  return item;
}

/*
 * Returns the item at the given position relative to the item with the given item_id in the
 * play queue (shuffle = 0) or shuffle queue (shuffle = 1).
 *
 * The item with item_id is at pos == 0.
 *
 * @param queue   The queue
 * @param pos     The position relative to the item with given queue-item-id
 * @param shuffle If 0 the position in the play-queue, 1 the position in the shuffle-queue
 * @return        Item at position in the queue or NULL if not found
 */
static struct queue_item *
queueitem_get_bypos(struct queue *queue, unsigned int item_id, unsigned int pos, char shuffle)
{
  struct queue_item *item_base;
  struct queue_item *item;
  int i;

  item_base = queueitem_get_byitemid(queue, item_id);

  if (!item_base)
    return NULL;

  i = 0;
  for (item = item_base; item != queue->head && i < pos; item = item_next(item, shuffle))
    {
      i++;
    }

  if (item == queue->head)
    return NULL;

  return item;
}

/*
 * Returns the item with the given item_id in the queue
 *
 * @param queue   The queue
 * @param item_id The unique id of the item in the queue
 * @return        Item with the given item_id or NULL if not found
 */
struct queue_item_info *
queue_get_byitemid(struct queue *queue, unsigned int item_id)
{
  struct queue_item *item;

  item = queueitem_get_byitemid(queue, item_id);

  if (!item)
    return NULL;

  return &item->qii;
}

/*
 * Returns the item at the given index (0-based) in the play queue (shuffle = 0) or shuffle queue (shuffle = 1)
 *
 * @param queue   The queue
 * @param index   Position of item in the queue (zero-based)
 * @param shuffle Play queue (shuffle = 0) or shuffle queue (shuffle = 1)
 * @return        Item at index in the queue or NULL if not found
 */
struct queue_item_info *
queue_get_byindex(struct queue *queue, unsigned int index, char shuffle)
{
  struct queue_item *item;

  item = queueitem_get_byindex(queue, index, shuffle);

  if (!item)
    return NULL;

  return &item->qii;
}

/*
 * Returns the item at the given position relative to the item with the given item_id in the
 * play queue (shuffle = 0) or shuffle queue (shuffle = 1).
 *
 * The item with item_id is at pos == 0.
 *
 * @param queue   The queue
 * @param item_id The unique id of the item in the queue
 * @param pos     The position relative to the item with given queue-item-id
 * @param shuffle If 0 the position in the play-queue, 1 the position in the shuffle-queue
 * @return        Item at position in the queue or NULL if not found
 */
struct queue_item_info *
queue_get_bypos(struct queue *queue, unsigned int item_id, unsigned int pos, char shuffle)
{
  struct queue_item *item;

  item = queueitem_get_bypos(queue, item_id, pos, shuffle);

  if (!item)
    return NULL;

  return &item->qii;
}

/*
 * Returns the index of the item with the given item-id (unique id in the queue)
 * or -1 if the item does not exist. Depending on the given shuffle value, the position
 * is either the on in the play-queue (shuffle = 0) or the shuffle-queue (shuffle = 1).
 *
 * @param queue   The queue to search the item
 * @param item_id The id of the item in the queue
 * @param shuffle If 0 the position in the play-queue, 1 the position in the shuffle-queue
 * @return        Index (0-based) of the item in the given queue or -1 if it does not exist
 */
int
queue_index_byitemid(struct queue *queue, unsigned int item_id, char shuffle)
{
  struct queue_item *item;
  int pos;

  pos = 0;
  for (item = item_next(queue->head, shuffle); item != queue->head && item->qii.item_id != item_id; item = item_next(item, shuffle))
    {
      pos++;
    }

  if (item == queue->head)
    // Item not found
    return -1;

  return pos;
}

/*
 * Return the next item in the queue for the item with the given item-id.
 *
 * @param queue   The queue
 * @param item_id The id of the item in the queue
 * @param shuffle If 0 return the next item in the play-queue, if 1 the next item in the shuffle-queue
 * @param r_mode  Repeat mode
 * @return The next item
 */
struct queue_item_info *
queue_next(struct queue *queue, unsigned int item_id, char shuffle, enum repeat_mode r_mode)
{
  struct queue_item *item;

  item = queueitem_get_byitemid(queue, item_id);

  if (!item)
    // Item not found
    return NULL;

  if (r_mode == REPEAT_SONG && item != queue->head)
    return &item->qii;

  item = item_next(item, shuffle);

  if (item == queue->head && r_mode == REPEAT_ALL)
    {
      // Repeat all and end of queue reached, return first item in the queue
      item = item_next(queue->head, shuffle);
    }

  if (item == queue->head)
    return NULL;

  return &item->qii;
}

/*
 * Return the previous item in the queue for the item with the given item-id.
 *
 * @param queue   The queue
 * @param item_id The id of the item in the queue
 * @param shuffle If 0 return the next item in the play-queue, if 1 the next item in the shuffle-queue
 * @param r_mode  Repeat mode
 * @return The previous item
 */
struct queue_item_info *
queue_prev(struct queue *queue, unsigned int item_id, char shuffle, enum repeat_mode r_mode)
{
  struct queue_item *item;

  item = queueitem_get_byitemid(queue, item_id);

  if (!item)
    // Item not found
    return NULL;

  if (r_mode == REPEAT_SONG && item != queue->head)
    return &item->qii;

  item = item_prev(item, shuffle);

  if (item == queue->head && r_mode == REPEAT_ALL)
    {
      // Repeat all and start of queue reached, return last item in the queue
      item = item_prev(queue->head, shuffle);
    }

  if (item == queue->head)
    return NULL;

  return &item->qii;
}

/*
 * Creates a new queue-info for the given queue.
 *
 * The given number of items (count) are copied from the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 * starting with the item at the given index (0-based).
 *
 * If count == 0, all items from the given index up to the end of the queue will be returned.
 *
 * @param queue   The queue
 * @param index   Index of the first item in the queue
 * @param count   Maximum number of items to copy (if 0 all remaining items after index)
 * @param shuffle If 0 the play-queue, if 1 the shuffle queue
 * @return A new queue-info with the specified items
 */
struct queue_info *
queue_info_new_byindex(struct queue *queue, unsigned int index, unsigned int count, char shuffle)
{
  struct queue_info *qi;
  struct queue_item_info *qii;
  struct queue_item *item_base;
  struct queue_item *item;
  unsigned int i;
  unsigned int qlength;
  int qii_size;

  qlength = queue_count(queue);

  qii_size = qlength - index;
  if (count > 0 && count < qii_size)
    qii_size = count;

  if (qii_size <= 0)
    {
      return NULL;
    }

  item_base = queueitem_get_byindex(queue, index, shuffle);

  if (!item_base)
    return NULL;

  qi = malloc(sizeof(struct queue_info));
  qii = malloc(qii_size * sizeof(struct queue_item_info));

  i = 0;
  for (item = item_base; item != queue->head && i < qii_size; item = item_next(item, shuffle))
    {
      qii[i].dbmfi_id = item->qii.dbmfi_id;
      qii[i].item_id = item->qii.item_id;
      qii[i].len_ms = item->qii.len_ms;
      qii[i].data_kind = item->qii.data_kind;
      qii[i].media_kind = item->qii.media_kind;

      i++;
    }

  qi->count = i;
  qi->length = qlength;
  qi->start_pos = index;
  qi->queue = qii;

  return qi;
}

/*
 * Creates a new queue-info for the given queue.
 *
 * The given number of items (count) are copied from the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 * starting after the item with the given item_id. The item with item_id is excluded, therefor the first item
 * is the one after the item with item_id.
 *
 * If count == 0, all items from the given index up to the end of the queue will be returned.
 *
 * @param queue   The queue
 * @param item_id The unique id of the item in the queue
 * @param count   Maximum number of items to copy (if 0 all remaining items after index)
 * @param shuffle If 0 the play-queue, if 1 the shuffle queue
 * @return A new queue-info with the specified items
 */
struct queue_info *
queue_info_new_bypos(struct queue *queue, unsigned int item_id, unsigned int count, char shuffle)
{
  int pos;
  struct queue_info *qi;

  pos = queue_index_byitemid(queue, item_id, shuffle);

  if (pos < 0)
    pos = 0;
  else
    pos = pos + 1; // exclude the item with the given item-id

  qi = queue_info_new_byindex(queue, pos, count, shuffle);

  return qi;
}

/*
 * Frees the queue info
 */
void
queue_info_free(struct queue_info *qi)
{
  free(qi->queue);
  free(qi);
}

/*
 * Adds items to the queue after the given item
 *
 * @param queue      The queue to add the new items
 * @param item_new   The item(s) to add
 * @param item_prev  The item to append the new items
 */
static void
queue_add_afteritem(struct queue *queue, struct queue_item *item_new, struct queue_item *item_prev)
{
  struct queue_item *item;
  struct queue_item *item_tail;

  if (!item_new)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid new item given to add items\n");
      return;
    }

  // Check the item after which the new items will be added
  if (!item_prev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid previous item given to add items\n");
      queue_items_free(item_new);
      return;
    }

  // Set item-id for all new items
  queue->last_inserted_item_id++;
  item_new->qii.item_id = queue->last_inserted_item_id;
  for (item = item_new->next; item != item_new; item = item->next)
    {
      queue->last_inserted_item_id++;
      item->qii.item_id = queue->last_inserted_item_id;
    }

  // Add items into the queue
  item_tail = item_new->prev;

  item_tail->next = item_prev->next;
  item_tail->shuffle_next = item_prev->shuffle_next;
  item_prev->next->prev = item_tail;
  item_prev->shuffle_next->shuffle_prev = item_tail;

  item_prev->next = item_new;
  item_prev->shuffle_next = item_new;
  item_new->prev = item_prev;
  item_new->shuffle_prev = item_prev;
}

/*
 * Adds items to the end of the queue
 *
 * @param queue   The queue to add the new items
 * @param item    The item(s) to add
 */
void
queue_add(struct queue *queue, struct queue_item *item)
{
  queue_add_afteritem(queue, item, queue->head->prev);
}

/*
 * Adds items to the queue after the item with the given item id (id of the item in the queue)
 *
 * @param queue   The queue to add the new items
 * @param item    The item(s) to add
 * @param item_id The item id after which the new items will be inserted
 */
void
queue_add_after(struct queue *queue, struct queue_item *item, unsigned int item_id)
{
  struct queue_item *item_prev;

  // Get the item after which the new items will be added
  item_prev = queueitem_get_byitemid(queue, item_id);
  queue_add_afteritem(queue, item, item_prev);
}

/*
 * Moves the item at from_pos to to_pos in the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 *
 * The position arguments are relativ to the item with the given id. At position = 1 is the first item
 * after the item with the given id (either in the play-queue or shuffle-queue, depending on the shuffle
 * argument).
 *
 * @param queue     The queue to move items
 * @param from_pos  The position of the first item to be moved
 * @param to_pos    The position to move the items
 * @param shuffle   If 0 the position in the play-queue, 1 the position in the shuffle-queue
 */
void
queue_move_bypos(struct queue *queue, unsigned int item_id, unsigned int from_pos, unsigned int to_offset, char shuffle)
{
  struct queue_item *item;
  struct queue_item *item_next;

  // Get the item to be moved
  item = queueitem_get_bypos(queue, item_id, from_pos, shuffle);
  if (!item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid position given to move items\n");
      return;
    }

  // Get the item at the target position
  item_next = queueitem_get_bypos(queue, item_id, to_offset, shuffle);
  if (!item_next)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid position given to move items\n");
      return;
    }

  // Remove item from the queue
  if (shuffle)
    {
      item->shuffle_prev->shuffle_next = item->shuffle_next;
      item->shuffle_next->shuffle_prev = item->shuffle_prev;
    }
  else
    {
      item->prev->next = item->next;
      item->next->prev = item->prev;
    }

  // Insert item into the queue befor the item at the target postion
  if (shuffle)
    {
      item_next->shuffle_prev->shuffle_next = item;
      item->shuffle_prev = item_next->shuffle_prev;

      item_next->shuffle_prev = item;
      item->shuffle_next = item_next;
    }
  else
    {
      item_next->next->prev = item;
      item->next = item_next->next;

      item_next->next = item;
      item->prev = item_next;
    }
}

/*
 * Removes the item from the queue and frees it
 */
static void
queue_remove_item(struct queue_item *item)
{
  struct queue_item *item_next;
  struct queue_item *item_prev;

  item_next = item->next;
  item_prev = item->prev;

  item_prev->next = item_next;
  item_next->prev = item_prev;

  item_next = item->shuffle_next;
  item_prev = item->shuffle_prev;

  item_prev->shuffle_next = item_next;
  item_next->shuffle_prev = item_prev;

  item->next = NULL;
  item->prev = NULL;
  item->shuffle_next = NULL;
  item->shuffle_prev = NULL;

  free(item);
}

/*
 * Removes the item with the given item-id from the queue
 */
void
queue_remove_byitemid(struct queue *queue, unsigned int item_id)
{
  struct queue_item *item;

  // Do not remove the head item
  if (item_id <= 0)
    return;

  // Get the item after which the items will be removed from the queue
  item = queueitem_get_byitemid(queue, item_id);
  if (!item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid item-id given to remove items\n");
      return;
    }

  queue_remove_item(item);
}

/*
 * Remove item at index from the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 *
 * @param queue   The queue
 * @param index   The index of the item to be removed (0-based)
 * @param shuffle If 0 the position in the play-queue, 1 the position in the shuffle-queue
 */
void
queue_remove_byindex(struct queue *queue, unsigned int index, char shuffle)
{
  struct queue_item *item;

  // Get the item after which the items will be removed from the queue
  item = queueitem_get_byindex(queue, index, shuffle);
  if (!item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid position given to remove items\n");
      return;
    }

  queue_remove_item(item);
}

/*
 * Removes the item at pos from the play-queue (shuffle = 0) or shuffle-queue (shuffle = 1)
 *
 * The position argument is relativ to the item with the given id. At position = 1 is the first item
 * after the item with the given id (either in the play-queue or shuffle-queue, depending on the shuffle
 * argument).
 *
 * @param queue   The queue to add the new items
 * @param item_id The unique id of the item in the queue
 * @param pos     The position of the first item to be removed
 * @param shuffle If 0 the position in the play-queue, 1 the position in the shuffle-queue
 */
void
queue_remove_bypos(struct queue *queue, unsigned int item_id, unsigned int pos, char shuffle)
{
  struct queue_item *item;

  // Get the item after which the items will be removed from the queue
  item = queueitem_get_bypos(queue, item_id, pos, shuffle);
  if (!item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid position given to remove items\n");
      return;
    }

  queue_remove_item(item);
}

/*
 * Removes all items from the queue
 *
 * @param queue The queue to clear
 */
void
queue_clear(struct queue *queue)
{
  struct queue_item *item;

  // Check if the queue is already empty
  if (queue->head->next == queue->head)
    return;

  // Remove the head item from the shuffle-queue
  item = queue->head->shuffle_next;
  item->shuffle_prev = queue->head->shuffle_prev;
  queue->head->shuffle_prev->shuffle_next = item;

  // Remove the head item from the play-queue
  item = queue->head->next;
  item->prev = queue->head->prev;
  queue->head->prev->next = item;

  // item now points to the first item in the play-queue (excluding the head item)
  queue_items_free(item);

  // Make the queue circular again
  queue->head->next = queue->head;
  queue->head->prev = queue->head;
  queue->head->shuffle_next = queue->head;
  queue->head->shuffle_prev = queue->head;
}

/*
 * Resets the shuffle-queue to be identical to the play-queue and returns the item
 * with the given item_id.
 *
 * If no item was found with the given item_id, it returns the head item.
 */
static struct queue_item *
queue_reset_and_find(struct queue *queue, unsigned int item_id)
{
  struct queue_item *item;
  struct queue_item *temp;

  item = queue->head;

  item->shuffle_next = item->next;
  item->shuffle_prev = item->prev;

  for (temp = item->next; temp != queue->head; temp = temp->next)
    {
      temp->shuffle_next = temp->next;
      temp->shuffle_prev = temp->prev;

      if (temp->qii.item_id == item_id)
	item = temp;
    }

  return item;
}

/*
 * Shuffles the queue
 *
 * If the item_id > 0, only the items in the queue after the item (excluding it)
 * with the given id are shuffled.
 *
 * @param queue   The queue to shuffle
 * @param item_id 0 to shuffle the whole queue or the item-id after which the queue gets shuffled
 */
void
queue_shuffle(struct queue *queue, unsigned int item_id)
{
  struct queue_item *temp;
  struct queue_item *item;
  struct queue_item **item_array;
  int nitems;
  int i;

  item = queue_reset_and_find(queue, item_id);

  // Count items to reshuffle
  nitems = 0;
  for (temp = item->next; temp != queue->head; temp = temp->next)
    {
      nitems++;
    }

  // Do not reshuffle queue with one item
  if (nitems < 2)
    return;

  // Construct array for number of items in queue
  item_array = (struct queue_item **)malloc(nitems * sizeof(struct queue_item *));
  if (!item_array)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate memory for shuffle array\n");
      return;
    }

  // Fill array with items in queue
  i = 0;
  for (temp = item->next; temp != queue->head; temp = temp->next)
    {
      item_array[i] = temp;
      i++;
    }

  // Shuffle item array
  shuffle_ptr(&queue->shuffle_rng, (void **)item_array, nitems);

  // Update shuffle-next/-prev for shuffled items
  for (i = 0; i < nitems; i++)
    {
      temp = item_array[i];

      if (i > 0)
	temp->shuffle_prev = item_array[i - 1];
      else
	temp->shuffle_prev = NULL;

      if (i < (nitems - 1))
	temp->shuffle_next = item_array[i + 1];
      else
	temp->shuffle_next = NULL;
    }

  // Insert shuffled items after item with given item_id
  item->shuffle_next = item_array[0];
  item_array[0]->shuffle_prev = item;

  queue->head->shuffle_prev = item_array[nitems - 1];
  item_array[nitems - 1]->shuffle_next = queue->head;

  free(item_array);
}

/*
 * Creates a new queue item for the given media file
 *
 * @param dbmfi media file info
 * @return The new queue item or NULL if an error occured
 */
static struct queue_item *
queue_item_new(struct db_media_file_info *dbmfi)
{
  struct queue_item *item;
  uint32_t id;
  uint32_t len_ms;
  uint32_t data_kind;
  uint32_t media_kind;
  int ret;

  ret = safe_atou32(dbmfi->id, &id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid song id in query result!\n");
      return NULL;
    }

  ret = safe_atou32(dbmfi->song_length, &len_ms);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid song length in query result!\n");
      return NULL;
    }

  ret = safe_atou32(dbmfi->data_kind, &data_kind);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid data kind in query result!\n");
      return NULL;
    }

  ret = safe_atou32(dbmfi->media_kind, &media_kind);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid media kind in query result!\n");
      return NULL;
    }

  item = (struct queue_item *) calloc(1, sizeof(struct queue_item));
  if (!item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Out of memory for struct queue_item\n");
      return NULL;
    }

  item->qii.dbmfi_id = id;
  item->qii.len_ms = len_ms;
  item->qii.data_kind = data_kind;
  item->qii.media_kind = media_kind;

  return item;
}

struct queue_item *
queue_make(struct query_params *qp)
{
  struct db_media_file_info dbmfi;
  struct queue_item *item_head;
  struct queue_item *item_tail;
  struct queue_item *item_temp;
  int ret;

  ret = db_query_start(qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not start query\n");
      return NULL;
    }

  DPRINTF(E_DBG, L_PLAYER, "Player queue query returned %d items\n", qp->results);

  item_head = NULL;
  item_tail = NULL;
  while (((ret = db_query_fetch_file(qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      item_temp = queue_item_new(&dbmfi);
      if (!item_temp)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error creating new queue_item for id '%s'\n", dbmfi.id);
	  continue;
	}

      if (!item_head)
	item_head = item_temp;

      if (item_tail)
	{
	  item_tail->next = item_temp;
	  item_temp->prev = item_tail;
	  item_tail->shuffle_next = item_temp;
	  item_temp->shuffle_prev = item_tail;
	}

      item_tail = item_temp;

      DPRINTF(E_DBG, L_PLAYER, "Added song id %s (%s)\n", dbmfi.id, dbmfi.title);
    }

  db_query_end(qp);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error fetching results\n");
      return NULL;
    }

  item_head->prev = item_tail;
  item_tail->next = item_head;
  item_head->shuffle_prev = item_tail;
  item_tail->shuffle_next = item_head;

  return item_head;
}

/*
 * Makes a list of queue-items for the given playlist id (plid)
 *
 * @param plid Id of the playlist
 * @return List of items for all playlist items
 */
struct queue_item *
queue_make_pl(int plid)
{
  struct query_params qp;
  struct queue_item *item;

  memset(&qp, 0, sizeof(struct query_params));

  qp.id = plid;
  qp.type = Q_PLITEMS;
  qp.offset = 0;
  qp.limit = 0;
  qp.sort = S_NONE;
  qp.idx_type = I_NONE;

  item = queue_make(&qp);

  return item;
}

/*
 * Makes a queue-item for the item/file with the given id (dbmfi_id)
 *
 * @param dbmfi_id Id of the item/file
 * @return List of items containing only the item with the given id
 */
struct queue_item *
queue_make_item(uint32_t dbmfi_id)
{
  struct query_params qp;
  struct queue_item *item;
  char buf[124];

  memset(&qp, 0, sizeof(struct query_params));

  qp.id = 0;
  qp.type = Q_ITEMS;
  qp.offset = 0;
  qp.limit = 0;
  qp.sort = S_NONE;
  snprintf(buf, sizeof(buf), "f.id = %" PRIu32, dbmfi_id);
  qp.filter = strdup(buf);

  item = queue_make(&qp);

  return item;
}
