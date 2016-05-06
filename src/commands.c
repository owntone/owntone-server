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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event2/event.h>

#include "logger.h"


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
  int command_pipe[2];
  struct event *command_event;
  struct command *current_cmd;
};

static void
command_cb(int fd, short what, void *arg)
{
  struct commands_base *cmdbase;
  struct command *cmd;
  enum command_state cmdstate;
  int ret;

  cmdbase = (struct commands_base *) arg;

  ret = read(cmdbase->command_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      goto readd;
    }

  if (cmd->nonblock)
    {
      // Command is executed asynchronously
      cmdstate = cmd->func(cmd->arg, &cmd->ret);

      if (cmdstate == COMMAND_END && cmd->arg)
	free(cmd->arg);
      free(cmd);
      goto readd;
    }

  // Command is executed synchronously, caller is waiting until signaled that the execution finished
  pthread_mutex_lock(&cmd->lck);

  cmdstate = cmd->func(cmd->arg, &cmd->ret);
  if (cmdstate == COMMAND_END)
    {
      // Command execution finished, execute the bottom half function
      if (cmd->ret == 0 && cmd->func_bh)
	{
	  cmdstate = cmd->func_bh(cmd->arg, &cmd->ret);
	}

      pthread_cond_signal(&cmd->cond);
      pthread_mutex_unlock(&cmd->lck);
    }
  else
    {
      // Command execution waiting for pending events before returning to the caller
      cmdbase->current_cmd = cmd;
      cmd->pending = cmd->ret;

      return;
    }

 readd:
  event_add(cmdbase->command_event, NULL);
}

static int
send_command(struct commands_base *cmdbase, struct command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      return -1;
    }

  ret = write(cmdbase->command_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      return -1;
    }

  return 0;
}

struct commands_base *
commands_base_new(struct event_base *evbase)
{
  struct commands_base *cmdbase;
  int ret;

  cmdbase = (struct commands_base*) calloc(1, sizeof(struct commands_base));
  if (!cmdbase)
    {
      DPRINTF(E_LOG, L_MAIN, "Out of memory for cmdbase\n");
      return NULL;
    }

# if defined(__linux__)
  ret = pipe2(cmdbase->command_pipe, O_CLOEXEC);
# else
  ret = pipe(cmdbase->command_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create command pipe: %s\n", strerror(errno));
      free(cmdbase);
      return NULL;
    }

  cmdbase->command_event = event_new(evbase, cmdbase->command_pipe[0], EV_READ, command_cb, cmdbase);
  if (!cmdbase->command_event)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create cmd event\n");
      close(cmdbase->command_pipe[0]);
      close(cmdbase->command_pipe[1]);
      free(cmdbase);
      return NULL;
    }

  ret = event_add(cmdbase->command_event, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not add cmd event\n");
      close(cmdbase->command_pipe[0]);
      close(cmdbase->command_pipe[1]);
      free(cmdbase);
      return NULL;
    }

  return cmdbase;
}

int
commands_base_free(struct commands_base *cmdbase)
{
  close(cmdbase->command_pipe[0]);
  close(cmdbase->command_pipe[1]);
  free(cmdbase);

  return 0;
}

int
commands_exec_returnvalue(struct commands_base *cmdbase)
{
  if (cmdbase->current_cmd == NULL)
      return 0;

  return cmdbase->current_cmd->ret;
}

void
commands_exec_end(struct commands_base *cmdbase, int retvalue)
{
  if (cmdbase->current_cmd == NULL)
    return;

  // A pending event finished, decrease the number of pending events and update the return value
  cmdbase->current_cmd->pending--;
  cmdbase->current_cmd->ret = retvalue;

  DPRINTF(E_DBG, L_MAIN, "Command has %d pending events\n", cmdbase->current_cmd->pending);

  // If there are still pending events return
  if (cmdbase->current_cmd->pending > 0)
    return;

  // All pending events have finished, execute the bottom half and signal the caller that the command finished execution
  if (cmdbase->current_cmd->func_bh)
    {
      cmdbase->current_cmd->func_bh(cmdbase->current_cmd->arg, &cmdbase->current_cmd->ret);
    }
  pthread_cond_signal(&cmdbase->current_cmd->cond);
  pthread_mutex_unlock(&cmdbase->current_cmd->lck);

  cmdbase->current_cmd = NULL;

  /* Process commands again */
  event_add(cmdbase->command_event, NULL);
}

int
commands_exec_sync(struct commands_base *cmdbase, command_function func, command_function func_bh, void *arg)
{
  struct command *cmd;
  int ret;

  cmd = (struct command*) calloc(1, sizeof(struct command));
  cmd->func = func;
  cmd->func_bh = func_bh;
  cmd->arg = arg;
  cmd->nonblock = 0;

  pthread_mutex_lock(&cmd->lck);

  ret = send_command(cmdbase, cmd);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Error sending command\n");
      pthread_mutex_unlock(&cmd->lck);
      return -1;
    }

  pthread_cond_wait(&cmd->cond, &cmd->lck);
  pthread_mutex_unlock(&cmd->lck);

  ret = cmd->ret;
  free(cmd);

  return ret;
}

int
commands_exec_async(struct commands_base *cmdbase, command_function func, void *arg)
{
  struct command *cmd;
  int ret;

  cmd = (struct command*) calloc(1, sizeof(struct command));
  cmd->func = func;
  cmd->func_bh = NULL;
  cmd->arg = arg;
  cmd->nonblock = 1;

  ret = send_command(cmdbase, cmd);
  if (ret < 0)
    return -1;

  return 0;
}

