#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>

typedef void (*task_handler_fn)(int client_fd, void *user_data);

typedef struct thread_pool thread_pool_t;

thread_pool_t *thread_pool_create(size_t thread_count, task_handler_fn handler, void *user_data);
int thread_pool_submit(thread_pool_t *pool, int client_fd);
void thread_pool_destroy(thread_pool_t *pool);

#endif
