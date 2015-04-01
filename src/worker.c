/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <event2/event.h>

#include "db.h"
#include "logger.h"
#include "worker.h"


struct worker_command;

typedef int (*cmd_func)(struct worker_command *cmd);

struct worker_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;

  int nonblock;

  struct {
    void (*cb)(void *);
    void *cb_arg;
    int delay;
    struct event *timer;
  } arg;

  int ret;
};


/* --- Globals --- */
// worker thread
static pthread_t tid_worker;

// Event base, pipes and events
struct event_base *evbase_worker;
static int g_initialized;
static int g_exit_pipe[2];
static int g_cmd_pipe[2];
static struct event *g_exitev;
static struct event *g_cmdev;

/* ---------------------------- CALLBACK EXECUTION ------------------------- */
/*                                Thread: worker                             */

static void
execute_cb(int fd, short what, void *arg)
{
  struct worker_command *cmd = arg;

  cmd->arg.cb(cmd->arg.cb_arg);

  event_free(cmd->arg.timer);
  free(cmd->arg.cb_arg);
  free(cmd);
}


static int
execute(struct worker_command *cmd)
{
  struct timeval tv = { cmd->arg.delay, 0 };

  if (cmd->arg.delay)
    {
      cmd->arg.timer = evtimer_new(evbase_worker, execute_cb, cmd);
      evtimer_add(cmd->arg.timer, &tv);

      return 1; // Not done yet, ask caller not to free cmd
    }

  cmd->arg.cb(cmd->arg.cb_arg);
  free(cmd->arg.cb_arg);

  return 0;
}


/* ---------------------------- COMMAND EXECUTION -------------------------- */

static int
send_command(struct worker_command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      DPRINTF(E_LOG, L_MAIN, "BUG: cmd->func is NULL!\n");
      return -1;
    }

  ret = write(g_cmd_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_MAIN, "Could not send command: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

static int
nonblock_command(struct worker_command *cmd)
{
  int ret;

  ret = send_command(cmd);
  if (ret < 0)
    return -1;

  return 0;
}

/* Thread: main */
static void
thread_exit(void)
{
  int dummy = 42;

  DPRINTF(E_DBG, L_MAIN, "Killing worker thread\n");

  if (write(g_exit_pipe[1], &dummy, sizeof(dummy)) != sizeof(dummy))
    DPRINTF(E_LOG, L_MAIN, "Could not write to exit fd: %s\n", strerror(errno));
}



/* --------------------------------- MAIN --------------------------------- */
/*                              Thread: worker                              */

static void *
worker(void *arg)
{
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Error: DB init failed (worker thread)\n");
      pthread_exit(NULL);
    }

  g_initialized = 1;

  event_base_dispatch(evbase_worker);

  if (g_initialized)
    {
      DPRINTF(E_LOG, L_MAIN, "Worker event loop terminated ahead of time!\n");
      g_initialized = 0;
    }

  db_perthread_deinit();

  pthread_exit(NULL);
}

static void
exit_cb(int fd, short what, void *arg)
{
  int dummy;
  int ret;

  ret = read(g_exit_pipe[0], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_MAIN, "Error reading from exit pipe\n");

  event_base_loopbreak(evbase_worker);

  g_initialized = 0;

  event_add(g_exitev, NULL);
}

static void
command_cb(int fd, short what, void *arg)
{
  struct worker_command *cmd;
  int ret;

  ret = read(g_cmd_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_MAIN, "Could not read command! (read %d): %s\n", ret, (ret < 0) ? strerror(errno) : "-no error-");
      goto readd;
    }

  if (cmd->nonblock)
    {
      ret = cmd->func(cmd);

      if (ret == 0)
        free(cmd);
      goto readd;
    }

  pthread_mutex_lock(&cmd->lck);

  ret = cmd->func(cmd);
  cmd->ret = ret;

  pthread_cond_signal(&cmd->cond);
  pthread_mutex_unlock(&cmd->lck);

 readd:
  event_add(g_cmdev, NULL);
}


/* ---------------------------- Our worker API  --------------------------- */

/* Thread: player */
void
worker_execute(void (*cb)(void *), void *cb_arg, size_t arg_size, int delay)
{
  struct worker_command *cmd;
  void *argcpy;

  DPRINTF(E_DBG, L_MAIN, "Got worker execute request\n");

  cmd = (struct worker_command *)malloc(sizeof(struct worker_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not allocate worker_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct worker_command));

  argcpy = malloc(arg_size);
  if (!argcpy)
    {
      DPRINTF(E_LOG, L_MAIN, "Out of memory\n");
      return;
    }

  memcpy(argcpy, cb_arg, arg_size);

  cmd->nonblock = 1;
  cmd->func = execute;
  cmd->arg.cb = cb;
  cmd->arg.cb_arg = argcpy;
  cmd->arg.delay = delay;

  nonblock_command(cmd);

  return;
}

int
worker_init(void)
{
  int ret;

# if defined(__linux__)
  ret = pipe2(g_exit_pipe, O_CLOEXEC);
# else
  ret = pipe(g_exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create pipe: %s\n", strerror(errno));
      goto exit_fail;
    }

# if defined(__linux__)
  ret = pipe2(g_cmd_pipe, O_CLOEXEC);
# else
  ret = pipe(g_cmd_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create command pipe: %s\n", strerror(errno));
      goto cmd_fail;
    }

  evbase_worker = event_base_new();
  if (!evbase_worker)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create an event base\n");
      goto evbase_fail;
    }

  g_exitev = event_new(evbase_worker, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create exit event\n");
      goto evnew_fail;
    }

  g_cmdev = event_new(evbase_worker, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create cmd event\n");
      goto evnew_fail;
    }

  event_add(g_exitev, NULL);
  event_add(g_cmdev, NULL);

  ret = pthread_create(&tid_worker, NULL, worker, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not spawn worker thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;
  
 thread_fail:
 evnew_fail:
  event_base_free(evbase_worker);
  evbase_worker = NULL;

 evbase_fail:
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);

 cmd_fail:
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);

 exit_fail:
  return -1;
}

void
worker_deinit(void)
{
  int ret;

  thread_exit();

  ret = pthread_join(tid_worker, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not join worker thread: %s\n", strerror(errno));
      return;
    }

  // Free event base (should free events too)
  event_base_free(evbase_worker);

  // Close pipes
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);
}
