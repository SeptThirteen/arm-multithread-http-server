#include "thread_pool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct task_node {
    int client_fd;
    struct task_node *next;
} task_node_t;

struct thread_pool {
    pthread_t *threads;
    size_t thread_count;

    task_node_t *head;
    task_node_t *tail;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutting_down;

    task_handler_fn handler;
    void *user_data;
};

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
