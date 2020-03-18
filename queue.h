#ifndef QUEUE_H
#define QUEUE_H 1

#include <pthread.h>

//Quick and dirty; don't bother with dynamic allocation
#define BUF_SIZE 2048 
typedef struct {
    char buf[BUF_SIZE];
    int wr_pos, rd_pos;
    int empty;
    int full;
    pthread_mutex_t mutex;
    pthread_cond_t can_prod;
    pthread_cond_t can_cons;
    int num_producers;
    int num_consumers;
} queue;

#define PTR_QUEUE_OCCUPANCY(q) ((q)->full ? BUF_SIZE : ((BUF_SIZE + (q)->wr_pos - (q)->rd_pos) % BUF_SIZE))
#define PTR_QUEUE_VACANCY(q) (BUF_SIZE - PTR_QUEUE_OCCUPANCY(q))

#define QUEUE_INITIALIZER {\
    .wr_pos = 0,\
    .rd_pos = 0,\
    .empty = 1,\
    .full = 0,\
    .mutex = PTHREAD_MUTEX_INITIALIZER,\
    .can_prod = PTHREAD_COND_INITIALIZER,\
    .can_cons = PTHREAD_COND_INITIALIZER,\
    .num_producers = 0,\
    .num_consumers = 0\
}

//Adds a char to queue q in a thread-safe way. Returns 0 on successful write,
//negative on error. This function can sleep; do not call while holding _any_
//mutexes, not even the one in the struct! 
int enqueue_single(queue *q, char c);

//Waits until len spaces are free in the queue, then writes all at once. I have 
//not written any scheduling code, so there are cases where one producer could
//starve another. Returns 0 on success, negative otherwise
int queue_write(queue *q, char *buf, int len);

//Reads a char from queue q in a thread-safe way. Returns 0 on successful read,
//-1 if no producers. This function can sleep; do not call while holding _any_
//mutexes, not even the one in the struct! 
int dequeue_single(queue *q, char *c);

//Reads n bytes from queue q in a thread-safe way. Returns 0 on successful read,
//and -1 on error (no producers). This function  locks (and unlocks) mutexes, so 
//don't call while holding any mutexes
int dequeue_n(queue *q, char *buf, int n);

//Reads a char from queue q in a thread-safe way. Returns 0 on successful read,
//1 if there was nothing to read, -1 on error (no producers). This function 
//locks (and unlocks) mutexes, so don't call while holding any mutexes
int nb_dequeue_single(queue *q, char *c);

//Reads n bytes from queue q in a thread-safe way. Returns 0 on successful read,
//1 if there was nothing to read, -1 on error (no producers). This function 
//locks (and unlocks) mutexes, so don't call while holding any mutexes
int nb_dequeue_n(queue *q, char *buf, int n);

#endif
