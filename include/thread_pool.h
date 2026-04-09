#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>

/* 线程池中的任务回调，参数是客户端连接和上下文数据。 */
typedef void (*task_handler_fn)(int client_fd, void *user_data);

/* 线程池对外同样只暴露为不透明类型。 */
typedef struct thread_pool thread_pool_t;

/* 创建指定数量的工作线程。 */
thread_pool_t *thread_pool_create(size_t thread_count, task_handler_fn handler, void *user_data);
/* 提交一个新的客户端连接任务。 */
int thread_pool_submit(thread_pool_t *pool, int client_fd);
/* 销毁线程池并等待所有工作线程退出。 */
void thread_pool_destroy(thread_pool_t *pool);

#endif
