#ifndef MY_PTHREAD_T_H
#define MY_PTHREAD_T_H

#define _GNU_SOURCE

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>

#define USE_MY_PTHREAD 1

#ifdef USE_MY_PTHREAD
#define pthread_t my_pthread_t
#define pthread_mutex_t my_pthread_mutex_t
#define pthread_create my_pthread_create
#define pthread_exit my_pthread_exit
#define pthread_join my_pthread_join
#define pthread_yield my_pthread_yield
#define pthread_mutex_init my_pthread_mutex_init
#define pthread_mutex_lock my_pthread_mutex_lock
#define pthread_mutex_unlock my_pthread_mutex_unlock
#define pthread_mutex_destroy my_pthread_mutex_destroy
#endif

#define MEM 4000
#define MAX_THREADS 1024
#define LEVELS 25
#define TIME_QUANTUM 50
#define RUNNING_TIME 500

// Important threads will not be demoted below this level.
//Threads on which other threads are waiting and/or threads holding mutexes are important threads.

typedef uint my_pthread_t;

typedef enum state {
	READY, RUNNING, WAITING, TERMINATED,
} state;

typedef struct threadControlBlock {
	my_pthread_t tid;
	struct threadControlBlock *next;
	ucontext_t ucontext;
	enum state state;
	uint run_count;
	uint current_ts;
	void *return_val;
	struct tcb_queue *tcb_wait_queue;
	uint priority;
} tcb;

/*Pointers to the start and end of the queue*/
typedef struct tcb_queue {
	tcb *start;
	tcb *end;
} tcb_list;

/* mutex struct definition */
typedef struct my_pthread_mutex_t {
	int id;
	int lock;
	my_pthread_t tid;
	struct my_pthread_mutex_t *next;
	struct tcb_queue *m_wait_queue;
	int initialized;
} my_pthread_mutex_t;

typedef struct my_pthread_return_values {
	my_pthread_t tid;
	void *return_val;
	struct my_pthread_return_values *next;
} thread_ret_val;

/*Scheduler defintion*/
typedef struct my_scheduler_t {
	tcb *running_queue;
	tcb *running_thread;
	tcb_list *priority_queue[LEVELS];
	my_pthread_mutex_t *mutex_list;
	thread_ret_val ret_vals;
} my_scheduler;

my_scheduler scheduler;

/* define your data structures here: */
//const int no_of_queues = 5;

/* create a new thread */
int my_pthread_create(my_pthread_t *thread, pthread_attr_t *attr,
		void *(*function)(void *), void *arg);

/* give CPU pocession to other user level threads voluntarily */
int my_pthread_yield();

/* terminate a thread */
void my_pthread_exit(void *value_ptr);

/* wait for thread termination */
int my_pthread_join(my_pthread_t thread, void **value_ptr);

/* initial the mutex lock */
int my_pthread_mutex_init(my_pthread_mutex_t *mutex,
		const pthread_mutexattr_t *mutexattr);

/* aquire the mutex lock */
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex);

/* release the mutex lock */
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex);

/* destroy the mutex */
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex);

#endif
