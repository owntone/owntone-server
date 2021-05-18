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

#include "commands.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

struct command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  command_function func;
  command_function func_bh;
  void *arg;
  int nonblock;
  int ret;
  int pending;
};

struct commands_base
{
  struct event_base *evbase;
  command_exit_cb exit_cb;
  int command_pipe[2];
  struct event *command_event;
  struct command *current_cmd;
};

static int
mutex_init(pthread_mutex_t *mutex)
{
  pthread_mutexattr_t mattr;
  int err;

  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
  err = pthread_mutex_init(mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);

  return err;
}

/*
 * Asynchronous execution of the command function
 */
static void
command_cb_async(struct commands_base *cmdbase, struct command *cmd)
{
  enum command_state cmdstate;

  // Command is executed asynchronously
  cmdstate = cmd->func(cmd->arg, &cmd->ret);

  // Only free arg if there are no pending events (used in worker.c)
  if (cmdstate != COMMAND_PENDING && cmd->arg)
    free(cmd->arg);

  free(cmd);

  event_add(cmdbase->command_event, NULL);
}

/*
 * Synchronous execution of the command function
 */
static void
command_cb_sync(struct commands_base *cmdbase, struct command *cmd)
{
  enum command_state cmdstate;

  pthread_mutex_lock(&cmd->lck);

  cmdstate = cmd->func(cmd->arg, &cmd->ret);
  if (cmdstate == COMMAND_PENDING)
    {
      // Command execution is waiting for pending events before returning to the caller
      cmdbase->current_cmd = cmd;
      cmd->pending = cmd->ret;
    }
  else
    {
      // Command execution finished, execute the bottom half function
      if (cmd->ret == 0 && cmd->func_bh)
	cmd->func_bh(cmd->arg, &cmd->ret);

      event_add(cmdbase->command_event, NULL);

      // Signal the calling thread that the command execution finished
      pthread_cond_signal(&cmd->cond);
      pthread_mutex_unlock(&cmd->lck);

      // Note if cmd->func was cmdloop_exit then cmdbase may be invalid now,
      // because commands_base_destroy() may have freed it
    }
}

/*
 * Event callback function
 *
 * Function is triggered by libevent if there is data to read on the command pipe (writing to the command pipe happens through
 * the send_command function).
 */
static void
command_cb(int fd, short what, void *arg)
{
  struct commands_base *cmdbase;
  struct command *cmd;
  int ret;

  cmdbase = arg;

  // Get the command to execute from the pipe
  ret = read(cmdbase->command_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      // Incorrect length, ignore
      event_add(cmdbase->command_event, NULL);
      return;
    }

  // Execute the command function
  if (cmd->nonblock)
    {
      // Command is executed asynchronously
      command_cb_async(cmdbase, cmd);
    }
  else
    {
      // Command is executed synchronously, caller is waiting until signaled that the execution finished
      command_cb_sync(cmdbase, cmd);
    }
}

/*
 * Writes the given command to the command pipe
 */
static int
send_command(struct commands_base *cmdbase, struct command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      errno = EINVAL;
      return -1;
    }

  ret = write(cmdbase->command_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      // errno set by write()
      return -1;
    }

  return 0;
}

/*
 * Frees the command base and closes the (internally used) pipes
 */
int
commands_base_free(struct commands_base *cmdbase)
{
  if (cmdbase->command_event)
    event_free(cmdbase->command_event);

  close(cmdbase->command_pipe[0]);
  close(cmdbase->command_pipe[1]);
  free(cmdbase);

  return 0;
}

/*
 * Creates a new command base, needs to be freed by commands_base_destroy or commands_base_free.
 *
 * @param evbase The libevent base to use for command handling
 * @param exit_cb Optional callback function to be called during commands_base_destroy
 */
struct commands_base *
commands_base_new(struct event_base *evbase, command_exit_cb exit_cb)
{
  struct commands_base *cmdbase;
  int ret;

  cmdbase = calloc(1, sizeof(struct commands_base));

#ifdef HAVE_PIPE2
  ret = pipe2(cmdbase->command_pipe, O_CLOEXEC);
#else
  ret = pipe(cmdbase->command_pipe);
#endif
  if (ret < 0)
    {
      // errno set by pipe
      free(cmdbase);
      return NULL;
    }

  cmdbase->command_event = event_new(evbase, cmdbase->command_pipe[0], EV_READ, command_cb, cmdbase);
  if (!cmdbase->command_event)
    {
      commands_base_free(cmdbase);
      errno = ENOMEM;
      return NULL;
    }

  ret = event_add(cmdbase->command_event, NULL);
  if (ret != 0)
    {
      commands_base_free(cmdbase);
      errno = ENOMEM;
      return NULL;
    }

  cmdbase->evbase = evbase;
  cmdbase->exit_cb = exit_cb;

  return cmdbase;
}

/*
 * Gets the current return value for the current pending command.
 *
 * If a command has more than one pending event, each event can access the previous set return value
 * if it depends on it.
 *
 * @param cmdbase The command base
 * @return The current return value
 */
int
commands_exec_returnvalue(struct commands_base *cmdbase)
{
  if (cmdbase->current_cmd == NULL)
      return 0;

  return cmdbase->current_cmd->ret;
}

/*
 * If a command function returned COMMAND_PENDING, each event triggered by this command needs to
 * call command_exec_end, passing it the return value of the event execution.
 *
 * If a command function is waiting for multiple events, each event needs to call command_exec_end.
 * The command base keeps track of the number of still pending events and only returns to the caller
 * if there are no pending events left.
 *
 * @param cmdbase The command base (holds the current pending command)
 * @param retvalue The return value for the calling thread
 */
void
commands_exec_end(struct commands_base *cmdbase, int retvalue)
{
  struct command *current_cmd = cmdbase->current_cmd;

  if (!current_cmd)
    return;

  // A pending event finished, decrease the number of pending events and update the return value
  current_cmd->pending--;
  current_cmd->ret = retvalue;

  // If there are still pending events return
  if (current_cmd->pending > 0)
    return;

  // All pending events have finished, execute the bottom half and signal the caller that the command execution finished
  if (current_cmd->func_bh)
    current_cmd->func_bh(current_cmd->arg, &current_cmd->ret);

  cmdbase->current_cmd = NULL;

  /* Process commands again */
  event_add(cmdbase->command_event, NULL);

  pthread_cond_signal(&current_cmd->cond);
  pthread_mutex_unlock(&current_cmd->lck);
}

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
commands_exec_sync(struct commands_base *cmdbase, command_function func, command_function func_bh, void *arg)
{
  struct command cmd;
  int errsv = 0;
  int ret;

  memset(&cmd, 0, sizeof(struct command));
  cmd.func = func;
  cmd.func_bh = func_bh;
  cmd.arg = arg;
  cmd.nonblock = 0;

  mutex_init(&cmd.lck);
  pthread_cond_init(&cmd.cond, NULL);

  pthread_mutex_lock(&cmd.lck);

  ret = send_command(cmdbase, &cmd);
  if (ret < 0)
    {
      errsv = errno;
      cmd.ret = -1;
    }
  else
    {
      pthread_cond_wait(&cmd.cond, &cmd.lck);
    }

  // May change errno, but we don't care about that
  pthread_mutex_unlock(&cmd.lck);

  pthread_cond_destroy(&cmd.cond);
  pthread_mutex_destroy(&cmd.lck);

  errno = errsv;
  return cmd.ret;
}

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
commands_exec_async(struct commands_base *cmdbase, command_function func, void *arg)
{
  struct command *cmd;
  int ret;

  cmd = calloc(1, sizeof(struct command));
  cmd->func = func;
  cmd->func_bh = NULL;
  cmd->arg = arg;
  cmd->nonblock = 1;

  ret = send_command(cmdbase, cmd);
  if (ret < 0)
    {
      free(cmd);
      return -1;
    }

  return 0;
}

/*
 * Command to break the libevent loop
 *
 * If the command base was created with an exit_cb function, exit_cb is called before breaking the
 * libevent loop.
 *
 * @param arg The command base
 * @param retval Always set to COMMAND_END
 */
static enum command_state
cmdloop_exit(void *arg, int *retval)
{
  struct commands_base *cmdbase = arg;
  *retval = 0;

  if (cmdbase->exit_cb)
    cmdbase->exit_cb();

  event_base_loopbreak(cmdbase->evbase);

  return COMMAND_END;
}

/*
 * Break the libevent loop for the given command base, closes the internally used pipes
 * and frees the command base.
 *
 * @param cmdbase The command base
 */
void
commands_base_destroy(struct commands_base *cmdbase)
{
  commands_exec_sync(cmdbase, cmdloop_exit, NULL, cmdbase);
  commands_base_free(cmdbase);
}

