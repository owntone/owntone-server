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

#ifndef SRC_LIBRARY_H_
#define SRC_LIBRARY_H_

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "commands.h"
#include "db.h"

#define LIBRARY_OK 0
#define LIBRARY_ERROR -1
#define LIBRARY_PATH_INVALID -2

/*
 * Definition of a library source
 *
 * A library source is responsible for scanning items into the library db.
 */
struct library_source
{
  char *name;
  int disabled;

  /*
   * Initialize library source (called from the main thread)
   */
  int (*init)(void);

  /*
   * Shutdown library source (called from the main thread after
   * terminating the library thread)
   */
  void (*deinit)(void);

  /*
   * Run initial scan after startup (called from the library thread)
   */
  int (*initscan)(void);

  /*
   * Run rescan (called from the library thread)
   */
  int (*rescan)(void);

  /*
   * Run a full rescan (purge library entries and rescan) (called from the library thread)
   */
  int (*fullrescan)(void);

  /*
   * Save queue as a new playlist under the given virtual path
   */
  int (*playlist_add)(const char *vp_playlist, const char *vp_item);

  /*
   * Removes the playlist under the given virtual path
   */
  int (*playlist_remove)(const char *virtual_path);

  /*
   * Save queue as a new playlist under the given virtual path
   */
  int (*queue_save)(const char *virtual_path);

  /*
   * Add item for the given path to the current queue
   */
  int (*queue_add)(const char *path, int position, char reshuffle, uint32_t item_id, int *count, int *new_item_id);
};

void
library_add_media(struct media_file_info *mfi);

int
library_add_playlist_info(const char *path, const char *title, const char *virtual_path, enum pl_type type, int parent_pl_id, int dir_id);

int
library_queue_add(const char *path, int position, int *count, int *new_item_id);

void
library_rescan();

void
library_fullrescan();

bool
library_is_scanning();

void
library_set_scanning(bool is_scanning);

bool
library_is_exiting();

void
library_update_trigger(short update_events);

int
library_playlist_add(const char *vp_playlist, const char *vp_item);

int
library_playlist_remove(char *virtual_path);

int
library_queue_save(char *path);

int
library_exec_async(command_function func, void *arg);

int
library_init();

void
library_deinit();


#endif /* SRC_LIBRARY_H_ */
