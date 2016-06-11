/*
 * Copyright (C) 2016 Christian Meffert <christian.meffert@googlemail.com>
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
#ifndef SRC_COMMANDS_H_
#define SRC_COMMANDS_H_

#include <event2/event.h>
#include <time.h>


enum command_state {
  COMMAND_END = 0,
  COMMAND_PENDING = 1,
};

/*
 * Function that will be executed in the event loop thread.
 *
 * If the function has pending events to complete, it needs to return COMMAND_PENDING with 'ret' set to
 * the number of pending events to wait for.
 *
 * @param arg Opaque pointer passed by command_exec_sync or command_exec_async
 * @param ret Pointer to the return value for the caller of the command
 * @return COMMAND_END if there are no pending events (function execution is complete) or COMMAND_PENDING if there are pending events
 */
typedef enum command_state (*command_function)(void *arg, int *ret);

typedef void (*command_exit_cb)(void);


struct commands_base;


struct commands_base *
commands_base_new(struct event_base *evbase, command_exit_cb exit_cb);

int
commands_base_free(struct commands_base *cmdbase);

int
commands_exec_returnvalue(struct commands_base *cmdbase);

void
commands_exec_end(struct commands_base *cmdbase, int retvalue);

int
commands_exec_sync(struct commands_base *cmdbase, command_function func, command_function func_bh, void *arg);

int
commands_exec_async(struct commands_base *cmdbase, command_function func, void *arg);

void
commands_base_destroy(struct commands_base *cmdbase);

#endif /* SRC_COMMANDS_H_ */
