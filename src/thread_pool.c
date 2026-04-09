#include "thread_pool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct task_node {
    /* 待处理的客户端连接。 */
    int client_fd;
    /* 单向队列中的下一个任务。 */
    struct task_node *next;
} task_node_t;

struct thread_pool {
    /* 工作线程数组。 */
    pthread_t *threads;
    /* 线程数量。 */
    size_t thread_count;

    /* 任务队列头尾。 */
    task_node_t *head;
    task_node_t *tail;

    /* 保护任务队列和关闭标志。 */
    pthread_mutex_t mutex;
    /* 没有任务时阻塞工作线程。 */
    pthread_cond_t cond;
    /* 设置后不再接收新任务。 */
    int shutting_down;

    /* 线程执行的任务函数和共享上下文。 */
    task_handler_fn handler;
    void *user_data;
};

/* 工作线程入口：等待任务、取出任务并执行处理函数。 */
static void *worker_entry(void *arg)
{
    thread_pool_t *pool = (thread_pool_t *)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        while (!pool->shutting_down && pool->head == NULL) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        if (pool->shutting_down && pool->head == NULL) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        task_node_t *task = pool->head;
        pool->head = task->next;
        if (pool->head == NULL) {
            pool->tail = NULL;
        }

        pthread_mutex_unlock(&pool->mutex);

        pool->handler(task->client_fd, pool->user_data);
        free(task);
    }

    return NULL;
}

/* 创建线程池，并启动所有工作线程。 */
thread_pool_t *thread_pool_create(size_t thread_count, task_handler_fn handler, void *user_data)
{
    if (thread_count == 0 || handler == NULL) {
        return NULL;
    }

    thread_pool_t *pool = (thread_pool_t *)calloc(1, sizeof(thread_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->threads = (pthread_t *)calloc(thread_count, sizeof(pthread_t));
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }

    pool->thread_count = thread_count;
    pool->handler = handler;
    pool->user_data = user_data;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (size_t i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_entry, pool) != 0) {
            pool->shutting_down = 1;
            pthread_cond_broadcast(&pool->cond);
            for (size_t j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            pthread_cond_destroy(&pool->cond);
            pthread_mutex_destroy(&pool->mutex);
            free(pool->threads);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

/* 将新连接追加到任务队列，并唤醒一个工作线程。 */
int thread_pool_submit(thread_pool_t *pool, int client_fd)
{
    if (pool == NULL) {
        return -1;
    }

    task_node_t *task = (task_node_t *)calloc(1, sizeof(task_node_t));
    if (task == NULL) {
        return -1;
    }
    task->client_fd = client_fd;

    pthread_mutex_lock(&pool->mutex);

    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->mutex);
        free(task);
        return -1;
    }

    if (pool->tail != NULL) {
        pool->tail->next = task;
        pool->tail = task;
    } else {
        pool->head = task;
        pool->tail = task;
    }

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

/* 进入关闭流程，唤醒所有线程并回收剩余任务。 */
void thread_pool_destroy(thread_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);
    pool->shutting_down = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for (size_t i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    task_node_t *node = pool->head;
    while (node != NULL) {
        task_node_t *next = node->next;
        free(node);
        node = next;
    }

    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->mutex);

    free(pool->threads);
    free(pool);
}
