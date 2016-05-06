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

struct commands_base;


/*
 * Creates a new command base, needs to be freed by commands_base_free.
 */
struct commands_base *
commands_base_new(struct event_base *evbase);

/*
 * Frees the command base and closes the (internally used) pipes
 */
int
commands_base_free(struct commands_base *cmdbase);

/*
 * Gets the current return value for the current pending command.
 *
 * @param cmdbase The command base
 * @return The current return value
 */
int
commands_exec_returnvalue(struct commands_base *cmdbase);

/*
 * If a command function returned COMMAND_PENDING, each event triggered by this command needs to
 * call command_exec_end, passing it the return value of the event execution.
 *
 * If a command function is waiting for multiple events the, each event needs to call command_exec_end.
 * The command base keeps track of the number of still pending events and only returns to the caller
 * if there are no pending events left.
 *
 * @param cmdbase The command base (holds the current pending command)
 * @param retvalue The return value for the calling thread
 */
void
commands_exec_end(struct commands_base *cmdbase, int retvalue);

/*
 * Execute the function 'func' with the given argument 'arg' in the event loop thread.
 * Blocks the caller (thread) until the function returned.
 *
 * If a function 'func_bh' ("bottom half") is given, it is executed after 'func' has successfully
 * finished.
 *
 * @param cmdbase The command base
 * @param func The function to be executed
 * @param func_bh The bottom half function to be executed after all pending events from func are processed
 * @param arg Argument passed to func (and func_bh)
 * @return Return value of func (or func_bh if func_bh is not NULL)
 */
int
commands_exec_sync(struct commands_base *cmdbase, command_function func, command_function func_bh, void *arg);

/*
 * Execute the function 'func' with the given argument 'arg' in the event loop thread.
 * Triggers the function execution and immediately returns (does not wait for func to finish).
 *
 * The pointer passed as argument is freed in the event loop thread after func returned.
 *
 * @param cmdbase The command base
 * @param func The function to be executed
 * @param arg Argument passed to func
 * @return 0 if triggering the function execution succeeded, -1 on failure.
 */
int
commands_exec_async(struct commands_base *cmdbase, command_function func, void *arg);


#endif /* SRC_COMMANDS_H_ */
