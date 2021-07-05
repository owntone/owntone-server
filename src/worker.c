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
#include "commands.h"
#include "misc.h"


struct worker_arg
{
  void (*cb)(void *);
  void *cb_arg;
  int delay;
  struct event *timer;
};


/* --- Globals --- */
// worker thread
static pthread_t tid_worker;

// Event base, pipes and events
struct event_base *evbase_worker;
static int g_initialized;
static struct commands_base *cmdbase;


/* ---------------------------- CALLBACK EXECUTION ------------------------- */
/*                                Thread: worker                             */

static void
execute_cb(int fd, short what, void *arg)
{
  struct worker_arg *cmdarg = arg;

  cmdarg->cb(cmdarg->cb_arg);

  event_free(cmdarg->timer);
  free(cmdarg->cb_arg);
  free(cmdarg);
}


static enum command_state
execute(void *arg, int *retval)
{
  struct worker_arg *cmdarg = arg;
  struct timeval tv = { cmdarg->delay, 0 };

  if (cmdarg->delay)
    {
      cmdarg->timer = evtimer_new(evbase_worker, execute_cb, cmdarg);
      evtimer_add(cmdarg->timer, &tv);

      *retval = 0;
      return COMMAND_PENDING; // Not done yet, ask caller not to free cmd
    }

  cmdarg->cb(cmdarg->cb_arg);
  free(cmdarg->cb_arg);

  *retval = 0;
  return COMMAND_END;
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


/* ---------------------------- Our worker API  --------------------------- */

/* Thread: player */
void
worker_execute(void (*cb)(void *), void *cb_arg, size_t arg_size, int delay)
{
  struct worker_arg *cmdarg;
  void *argcpy;

  cmdarg = calloc(1, sizeof(struct worker_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not allocate worker_arg\n");
      return;
    }

  if (arg_size > 0)
    {
      argcpy = malloc(arg_size);
      if (!argcpy)
	{
	  DPRINTF(E_LOG, L_MAIN, "Out of memory\n");
	  free(cmdarg);
	  return;
	}

      memcpy(argcpy, cb_arg, arg_size);
    }
  else
    argcpy = NULL;

  cmdarg->cb = cb;
  cmdarg->cb_arg = argcpy;
  cmdarg->delay = delay;

  commands_exec_async(cmdbase, execute, cmdarg);
}

int
worker_init(void)
{
  int ret;

  evbase_worker = event_base_new();
  if (!evbase_worker)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create an event base\n");
      goto evbase_fail;
    }

  cmdbase = commands_base_new(evbase_worker, NULL);

  ret = pthread_create(&tid_worker, NULL, worker, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not spawn worker thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  thread_setname(tid_worker, "worker");

  return 0;
  
 thread_fail:
  commands_base_free(cmdbase);
  event_base_free(evbase_worker);
  evbase_worker = NULL;

 evbase_fail:
  return -1;
}

void
worker_deinit(void)
{
  int ret;

  g_initialized = 0;
  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_worker, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not join worker thread: %s\n", strerror(errno));
      return;
    }

  // Free event base (should free events too)
  event_base_free(evbase_worker);
}
