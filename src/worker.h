
#ifndef __WORKER_H__
#define __WORKER_H__

/* The worker thread is made for running asyncronous tasks from a real time
 * thread, mainly the player thread.

 * The worker_execute() function will trigger a callback from the worker thread.
 * Before returning the function will copy the argument given, so the caller
 * does not need to preserve them. However, if the argument contains pointers to
 * data, the caller must either make sure that the data remains valid until the
 * callback (which can free it), or make sure the callback does not refer to it.
 *
 * @param cb the function to call from the worker thread
 * @param cb_arg arguments for callback
 * @param arg_size size of the arguments given
 * @param delay how much in seconds to delay the execution
 */
void
worker_execute(void (*cb)(void *), void *cb_arg, size_t arg_size, int delay);

int
worker_init(void);

void
worker_deinit(void);

#endif /* !__WORKER_H__ */
