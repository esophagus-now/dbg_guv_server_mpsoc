#ifdef DEBUG_ON
#include <stdio.h>
#include <ctype.h>
#endif

#include <pthread.h>
#include "queue.h"



//Adds a char to queue q in a thread-safe way. Returns 0 on successful write,
//negative on error. This function can sleep; do not call while holding _any_
//mutexes, not even the one in the struct! 
int enqueue_single(queue *q, char c) {
    //Lock mutex before we try adding c to the queue
    pthread_mutex_lock(&q->mutex);
    //Wait until there is space
    while (q->full) pthread_cond_wait(&q->can_prod, &q->mutex);
    
    //Add c to the buffer, making sure to signal to everyone else that they
    //can read
    q->buf[q->wr_pos++] = c;
    if (q->wr_pos >= BUF_SIZE) q->wr_pos = 0;
    if (q->wr_pos == q->rd_pos) q->full = 1;
    q->empty = 0;
#ifdef DEBUG_ON
    fprintf(stderr, "Enqueued 0x%02x", c);
    if (isprint(c)) fprintf(stderr, " = '%c'", c);
    fprintf(stderr, "\n");
#endif
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_signal(&q->can_cons);
    return 0;
}

//Reads a char from queue q in a thread-safe way. Returns 0 on successful read,
//-1 if no producers. This function can sleep; do not call while holding _any_
//mutexes, not even the one in the struct! 
int dequeue_single(queue *q, char *c) {
    pthread_mutex_lock(&q->mutex);
    while (q->empty && q->num_producers > 0) pthread_cond_wait(&q->can_cons, &q->mutex);
    if (q->num_producers <= 0) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    
    *c = q->buf[q->rd_pos++];
    if (q->rd_pos >= BUF_SIZE) q->rd_pos = 0;
    if (q->rd_pos == q->wr_pos) q->empty = 1;
    q->full = 0;
#ifdef DEBUG_ON
    fprintf(stderr, "Dequeued 0x%02x", *c);
    if (isprint(*c)) fprintf(stderr, " = '%c'", *c);
    fprintf(stderr, "\n");
#endif
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_signal(&q->can_prod);
    return 0;
}

//Waits until len spaces are free in the queue, then writes all at once. I have 
//not written any scheduling code, so there are cases where one producer could
//starve another. Returns 0 on success, negative otherwise
int queue_write(queue *q, char *buf, int len) {
    //Check if len is too big to begin with
    if (len > BUF_SIZE) return -1;
    
    //Lock mutex before we try adding data to the queue
    pthread_mutex_lock(&q->mutex);
    //Wait until there is enough space for entire buffer
    while (PTR_QUEUE_VACANCY(q) < len)
        pthread_cond_wait(&q->can_prod, &q->mutex);
    
    //Copy buf into the queue
#ifdef DEBUG_ON
    fprintf(stderr, "Enqueued [");
#endif
    int i;
    for (i = 0; i < len; i++) {
#ifdef DEBUG_ON
        fprintf(stderr, "%c", *buf);
#endif
        q->buf[q->wr_pos++] = *buf++;
        if (q->wr_pos >= BUF_SIZE) q->wr_pos = 0;
    }
    if (q->wr_pos == q->rd_pos) q->full = 1;
    q->empty = 0;
#ifdef DEBUG_ON
    fprintf(stderr, "]\n");
#endif
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_signal(&q->can_cons);
    return 0;
}

//Tries to read a single byte from the queue. Returns negative if it can't
int nb_dequeue_single(queue *q, char *c) {
    pthread_mutex_lock(&q->mutex);
    if (q->empty) {
        if (q->num_producers > 0) {
            pthread_mutex_unlock(&q->mutex);
            return 1;
        } else {
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
    }
    
    *c = q->buf[q->rd_pos++];
    if (q->rd_pos >= BUF_SIZE) q->rd_pos = 0;
    if (q->rd_pos == q->wr_pos) q->empty = 1;
    q->full = 0;
#ifdef DEBUG_ON
    fprintf(stderr, "Dequeued 0x%02x", *c);
    if (isprint(*c)) fprintf(stderr, " = '%c'", *c);
    fprintf(stderr, "\n");
#endif
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_signal(&q->can_prod);
    return 0;
}

//Reads n bytes from queue q in a thread-safe way. Returns 0 on successful read,
//1 if there was nothing to read, -1 on error (no producers). This function 
//locks (and unlocks) mutexes, so don't call while holding any mutexes
int nb_dequeue_n(queue *q, char *buf, int n) {
    pthread_mutex_lock(&q->mutex);
    if (PTR_QUEUE_OCCUPANCY(q) < n) {
        if (q->num_producers > 0) {
            pthread_mutex_unlock(&q->mutex);
            return 1;
        } else {
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
    }
    
    //Dequeue queue into buf
#ifdef DEBUG_ON
    fprintf(stderr, "Dequeued [");
#endif
    int i;
    for (i = 0; i < len; i++) {
#ifdef DEBUG_ON
        fprintf(stderr, "0x%02x ", q->buf[q->rd_pos]);
#endif
        *buf++ = q->buf[q->rd_pos++];
        if (q->rd_pos >= BUF_SIZE) q->rd_pos = 0;
    }
#ifdef DEBUG_ON
    fprintf(stderr, "]\n");
#endif

    if (q->rd_pos == q->wr_pos) q->empty = 1;
    q->full = 0;

    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_signal(&q->can_prod);
    return 0;
}
