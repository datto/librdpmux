/** @file */
#include "queue.h"

/**
 * @brief Checks if the queue is empty.
 *
 * @returns Whether the queue is empty.
 * @param q The queue to check.
 */
static bool shim_queue_check_is_empty(ShimMsgQueue *q)
{
    return SIMPLEQ_EMPTY(&q->updates);
}

/**
 * @brief Dequeues an update.
 *
 * @returns Pointer to the update.
 * @param q The queue to dequeue from.
 */
void *shim_queue_dequeue(ShimMsgQueue *q)
{
    //printf("LIBSHIM: Waiting on update now!\n");
    void *ret;
    // take lock on the mutex
    pthread_mutex_lock(&q->lock);
    // while the queue is empty, wait
    while (shim_queue_check_is_empty(q)) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    // remove the head of the queue
    //printf("LIBSHIM: Dequeueing update now!\n");
    ret = (void *) SIMPLEQ_FIRST(&q->updates);
    SIMPLEQ_REMOVE_HEAD(&q->updates, next);
    // unlock
    pthread_mutex_unlock(&q->lock);
    // return value
    return ret;
}

/**
 * @brief Enqueues an update.
 *
 * @param q The queue to stick the update on.
 * @param update The update to stick on the queue.
 */
void shim_queue_enqueue(ShimMsgQueue *q, ShimUpdate *update)
{
    //printf("LIBSHIM: Enqueueing update now!\n");
    pthread_mutex_lock(&q->lock);
    SIMPLEQ_INSERT_TAIL(&q->updates, update, next);
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

/**
 * @brief Clears a queue of all updates.
 *
 * @param q The queue to clear.
 */
void shim_queue_clear(ShimMsgQueue *q)
{
    //printf("LIBSHIM: Clearing queue now\n");
    ShimUpdate *update;
    pthread_mutex_lock(&q->lock);
    while ((update = SIMPLEQ_FIRST(&q->updates)) != NULL) {
        SIMPLEQ_REMOVE(&q->updates, update, ShimUpdate, next);
        g_free(update);
    }
    pthread_mutex_unlock(&q->lock);
}
