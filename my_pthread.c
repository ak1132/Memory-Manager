// File:	my_pthread.c
// Date:	09/23/2017

// name: Amogh Kulkarni, Sukumar Gaonkar, Vatsal Parikh
// username of iLab:ark159
// iLab Server:h206-2.cs.rutgers.edu

#include "my_pthread_t.h"
#include "my_mem_manager.h"

#undef malloc(x)
#undef free(x)

#define malloc(x) myallocate(x, __FILE__, __LINE__, 0)
#define free(x) mydeallocate(x, __FILE__, __LINE__, 0)
#define USE_MY_PTHREAD 1

tcb *schd_t, *main_t;
ucontext_t curr_context; /*exit_thread_context ;*/
static my_pthread_t threadNo = 1;
static int mutex_id = 0;
static int SYS_MODE = 0;
static int init = 0, timer_hit = 0;
static int NO_OF_MUTEX = 0;

struct itimerval timeslice;
struct sigaction new_action;
static int maintainence_count;

/**
 * Implementing queue functions
 **/

void init_queue(tcb_list **queue) {
	*queue = (tcb_list *) malloc(sizeof(tcb_list));
	return;
}

void enqueue(tcb_list *queue, tcb *new_thread) {

	if (queue->start == NULL) {
		queue->start = new_thread;
		queue->end = new_thread;

	} else {
		queue->end->next = new_thread;
		queue->end = new_thread;
	}

	new_thread->next = NULL;
}

void enqueue_running(tcb_list *queue, tcb *new_thread) {

	if (queue->start == NULL) {
		queue->start = new_thread;
		queue->end = new_thread;

	} else {
		queue->end->next = new_thread;
		queue->end = new_thread;
	}
}

tcb* dequeue(tcb_list *queue) {

	tcb *curr_tcb;

	if (queue->start == NULL) {
		return NULL;
	}

	curr_tcb = queue->start;
	if (curr_tcb->next == NULL) {
		queue->start = NULL;
		queue->end = NULL;
	} else {
		queue->start = queue->start->next;
		if (queue->start == NULL) {
			queue->end = NULL;
		}
	}

	return curr_tcb;
}

tcb* dequeue_running(tcb *queue) {

	tcb *curr_tcb;

	if (queue == NULL) {
		return NULL;
	}

	curr_tcb = queue;
	if (curr_tcb->next == NULL) {
		queue = NULL;
	} else {
		queue = queue->next;
		curr_tcb->next = NULL;
	}

	return curr_tcb;
}

void delete_from_queue(tcb_list *queue, tcb *todel_tcb) {
	if (todel_tcb == NULL) {
		return;
	} else if (queue->start == NULL) {
		return;
	} else if (queue->start == todel_tcb) {
		queue->start = queue->start->next;
		if (queue->start == NULL)
			queue->end = NULL;
		todel_tcb->next = NULL;
	}

	tcb *curr_tcb = queue->start;
	tcb *trail_pointer = queue->start;

	while (curr_tcb != todel_tcb && curr_tcb != NULL) {
		if (curr_tcb != queue->start) {
			trail_pointer = trail_pointer->next;
		}
		curr_tcb = curr_tcb->next;
	}

	if (curr_tcb != NULL) {
		trail_pointer->next = curr_tcb->next;
		curr_tcb->next = NULL;
	}

}

/*
 * Start of the scheduler code block
 */

void signal_handler(int signal) {
	if (SYS_MODE == 1) {
		timer_hit = 1;
		return;
	} else {
		pthread_yield();
	}
}

my_pthread_t tid_generator() {
	return ++threadNo;
}

int mutex_id_generator() {
	return ++mutex_id;
}

void init_priority_queue(tcb_list *q[]) {
	int i;
	for (i = 0; i < LEVELS; i++) {
		init_queue(&(q[i]));
	}
}

void reset_timer() {
	timeslice.it_value.tv_usec = TIME_QUANTUM;
	timeslice.it_value.tv_sec = 0;
	timeslice.it_interval.tv_usec = 0;
	timeslice.it_interval.tv_sec = 0;

	if (setitimer(ITIMER_VIRTUAL, &timeslice, NULL)) {
		printf("Couldn't start the timer\n");
	}

}

void signalTemp() {
	pthread_yield();
}

void make_scheduler() {
//Create context for the scheduler thread
	if (init == 0) {

		init_mem_manager();

		main_t = malloc(sizeof(tcb));

		if (getcontext(&(main_t->ucontext)) == -1) {
			printf("Error getting context!!!\n");
			return;
		}
		main_t->state = RUNNING;
		main_t->priority = 0;
		main_t->tid = 1;
		main_t->run_count = 0;
		main_t->tcb_wait_queue = malloc(sizeof(tcb_list));

		main_t->ucontext.uc_link = 0; //change this to maintenance cycle
		main_t->ucontext.uc_stack.ss_sp = malloc(MEM);
		if (main_t->ucontext.uc_stack.ss_sp == NULL) {
			printf("Memory Allocation Error!!!\n");
			return;
		}
		main_t->ucontext.uc_stack.ss_size = MEM;
		main_t->ucontext.uc_stack.ss_flags = 0;
		makecontext(&(main_t->ucontext), &signalTemp, 0);

		schd_t = malloc(sizeof(tcb));
		if (getcontext(&schd_t->ucontext) == -1) {
			printf("Error getting context!!!\n");
			return;
		}
		schd_t->state = WAITING; // Permanently WAITING. Ensures that the scheduler doesnt schedule itself.
		schd_t->priority = 0;
		schd_t->tid = 1;
		schd_t->run_count = 0;
		schd_t->tcb_wait_queue = malloc(sizeof(tcb_list));

		schd_t->ucontext.uc_link = 0;
		schd_t->ucontext.uc_stack.ss_sp = malloc(MEM);
		if (schd_t->ucontext.uc_stack.ss_sp == NULL) {
			printf("Memory Allocation Error!!!\n");
			return;
		}
		schd_t->ucontext.uc_stack.ss_size = MEM;
		schd_t->ucontext.uc_stack.ss_flags = 0;

		scheduler.running_thread = NULL;

		scheduler.mutex_list = malloc(sizeof(my_pthread_mutex_t));

		init_priority_queue(scheduler.priority_queue);

		scheduler.running_thread = main_t;
		scheduler.running_queue = main_t;

		//Initialize the timer and sig alarm
		new_action.sa_handler = signal_handler;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;
		sigaction(SIGVTALRM, &new_action, NULL);
		reset_timer();

		init = 1;
	}

}

void wrapper_function(void *(*thread_function)(void *), void *arg) {
	void *ret_value = thread_function(arg);
	pthread_exit(ret_value);
}

/* create a new thread */
int my_pthread_create(my_pthread_t *thread, pthread_attr_t *attr,
		void *(*function)(void *), void *arg) {

	assert(thread != NULL);

	*thread = tid_generator();
	
	if(*thread > MAX_THREADS){
		printf("Thread limit exceeded\n");
		return -1;
	}
	
	SYS_MODE = 1;

	make_scheduler();

	ucontext_t curr_context;
	getcontext(&curr_context);
	curr_context.uc_link = 0;
	curr_context.uc_stack.ss_sp = malloc(MEM);
	if (curr_context.uc_stack.ss_sp == NULL) {
		printf("Memory Allocation Error!!!\n");
		return 1;
	}
	curr_context.uc_stack.ss_size = MEM;
	curr_context.uc_stack.ss_flags = 0;
	makecontext(&(curr_context), &wrapper_function, 2, function, arg);

// malloc ensure the tcb is created in heap and is not deallocated once the function returns.
	tcb *new_thread = (tcb *) malloc(sizeof(tcb));
	new_thread->tid = *thread;
	new_thread->ucontext = curr_context;		//test this out
	new_thread->next = NULL;
	new_thread->priority = 0;
	new_thread->state = READY;
	new_thread->tcb_wait_queue = (tcb_list *) malloc(sizeof(tcb_list));
	new_thread->current_ts = TIME_QUANTUM;

	enqueue(scheduler.priority_queue[0], new_thread);

	SYS_MODE = 0;

//if timer is called midway yield the thread
	if (timer_hit == 1) {
		timer_hit = 0;
		pthread_yield();
	}

	return 0;
}

int holds_mutex(tcb *t) {
	my_pthread_mutex_t *curr_mutex = scheduler.mutex_list;

	tcb*curr_tcb;
	while (curr_mutex != NULL) {

		tcb_list * wait_list = curr_mutex->m_wait_queue;

		if (wait_list != NULL) {

			curr_tcb = wait_list->start;

			while (curr_tcb != NULL) {
				if (curr_tcb->tid == t->tid) {
					return 1;
				}
				curr_tcb = curr_tcb->next;
			}
		}

		curr_mutex = curr_mutex->next;
	}

	return 0;
}

void schd_maintenence() {

	int i = 0;		//levels

	//First order of business, promote all threads by 1
	for (i = 1; i < LEVELS; i++) {
		if (scheduler.priority_queue[i]->start != NULL) {
			tcb *temp = NULL;
			while ((temp = dequeue(scheduler.priority_queue[i])) != NULL) {
				temp->priority = temp->priority - 1;
				enqueue(scheduler.priority_queue[i - 1], temp);
			}
		}
	}

	//Now demote processes in running queue

	tcb* temp = scheduler.running_queue;

	while (temp != NULL) {

		if (temp->state != TERMINATED && temp->state != WAITING) {

			tcb *wait_Q = temp->tcb_wait_queue->start;

			if (wait_Q != NULL && temp->priority > LEVELS / 5
					&& (holds_mutex(temp) == 1)) {

				temp->priority = LEVELS / 5;

			} else {
				//If no one is waiting increase priority aka demote
				uint priority_t = temp->priority + 1;
				if (priority_t + 1 >= LEVELS) {
					priority_t = LEVELS - 1;
				}
				temp->priority = priority_t;
			}
			tcb *next_tcb = temp->next;
			enqueue(scheduler.priority_queue[temp->priority], temp);

			temp = next_tcb;

		} else {
			//cannot delete the tcb as we need return values
			temp = temp->next;
		}

	}

	scheduler.running_queue = NULL;

	//Rebuild the running queue for the scheduler from every level, assigning new quantas

	for (i = 0; i < LEVELS; i++) {
		if (scheduler.priority_queue[i]->start != NULL) {
			tcb *temp = NULL;
			while ((temp = dequeue(scheduler.priority_queue[i])) != NULL) {
				temp->current_ts = (i + 1) * TIME_QUANTUM;
				temp->next = NULL;
				if (temp->state == READY) {
					if (scheduler.running_queue == NULL) {
						scheduler.running_queue = temp;
					} else {
						tcb *tex = scheduler.running_queue;
						while (tex->next != NULL) {
							tex = tex->next;
						}
						tex->next = temp;
					}
				} else {
					enqueue(scheduler.priority_queue[i], temp);
				}
			}
		}
	}

	scheduler.running_thread = scheduler.running_queue;
	maintainence_count++;

}

/* give CPU pocession to other user level threads voluntarily */
int my_pthread_yield() {

	SYS_MODE = 1;
	int m_Called = 0;
	make_scheduler();

	tcb *prev_thread = scheduler.running_thread;

	//change statuses of all running threads
	if (prev_thread != NULL) {
		if (prev_thread->state != TERMINATED && prev_thread->state != WAITING) {
			prev_thread->state = READY;
		}
		prev_thread->run_count += prev_thread->current_ts / TIME_QUANTUM;//update runs count
	}

	//end of one cycle
	if (scheduler.running_thread->next == NULL) {
		m_Called = 1;
		if (maintainence_count > 0
				|| scheduler.running_thread->state == WAITING) {
			scheduler.running_thread = NULL;
			schd_maintenence();
		} else {
			enqueue(scheduler.priority_queue[0], scheduler.running_thread);
			scheduler.running_queue = NULL;
			scheduler.running_thread = NULL;
			schd_maintenence();
		}

	}

	struct itimerval t_slice;
	tcb *to_thread = NULL;

	if (scheduler.running_thread != NULL) {

		if (scheduler.running_thread != NULL && m_Called == 1) {
			to_thread = scheduler.running_thread;
			to_thread->state = RUNNING;
			t_slice.it_value.tv_usec = scheduler.running_thread->current_ts;
		}

		if (scheduler.running_thread->next != NULL && m_Called == 0) {
			to_thread = scheduler.running_thread->next;
			to_thread->state = RUNNING;
			t_slice.it_value.tv_usec = scheduler.running_thread->current_ts;
			scheduler.running_thread = scheduler.running_thread->next;
		}

	} else {
		t_slice.it_value.tv_usec = 0;
	}

	t_slice.it_value.tv_sec = 0;
	t_slice.it_interval.tv_sec = 0;
	t_slice.it_interval.tv_usec = 0;
	setitimer(ITIMER_VIRTUAL, &t_slice, NULL);

	SYS_MODE = 0;

	switch_thread(prev_thread->tid, to_thread->tid);

	if (swapcontext(&prev_thread->ucontext, &to_thread->ucontext) == -1) {
		printf("Swapcontext Failed %d %s\n", errno, strerror(errno));
		return -1;
	}

	return 0;
}

void free_queue(tcb_list *list) {

	if (list->start == NULL)
		return;
	tcb *curr_tcb = list->start;
	tcb *forward_pointer = curr_tcb->next;

	while (curr_tcb != NULL) {
		if (curr_tcb->tid != 0) {
			free(curr_tcb);
			curr_tcb = forward_pointer;
			forward_pointer = forward_pointer->next;
		}
	}
}

void delete_tcb(tcb* head) {
	/* deref head_ref to get the real head */
	tcb *current = head;
	tcb* next;

	while (current != NULL) {
		next = current->next;
		free(current);
		current = next;
	}

	head = NULL;
}

/* terminate a thread */
void my_pthread_exit(void *value_ptr) {
	/*
	 * Store return value in value_ptr
	 * deallocate wait queue
	 * call scheduler for the next process
	 */
	make_scheduler();
	SYS_MODE = 1;

	if (scheduler.running_thread->state == TERMINATED) {
		printf("Thread %d already terminated", scheduler.running_thread->tid);
	}

	tcb_list *wait_queue = scheduler.running_thread->tcb_wait_queue;

	while (wait_queue->start != NULL) {
		tcb* tcb_holder = dequeue(wait_queue);
		tcb_holder->priority = 0;
		tcb_holder->state = READY;

		enqueue(scheduler.priority_queue[tcb_holder->priority], tcb_holder);
	}

	int i = 0;
	if (scheduler.running_thread->tid == 1) {
		// Main thread exited, thus kill all children
		printf("Main Thread exiting\n");

		delete_tcb(scheduler.running_queue);

		for (i = 0; i < LEVELS; i++) {
			if (scheduler.priority_queue[i]->start != NULL) {
				free_queue(scheduler.priority_queue[i]);
			}
		}
		return;
	}

	scheduler.running_thread->state = TERMINATED;
	scheduler.running_thread->return_val = value_ptr;

	my_pthread_yield();
}

//check in the running queue too
tcb* get_tcb(my_pthread_t thread) {

	int i = 0;
	for (i = 0; i < LEVELS; i++) {

		tcb *prev_ptr = NULL;
		tcb *curr_tcb = scheduler.priority_queue[i]->start;

		while (curr_tcb != NULL) {

			if (curr_tcb->tid == thread) {
				if (prev_ptr != NULL) {
					prev_ptr->next = curr_tcb->next;
				} else {
					if (curr_tcb->next != NULL) {
						scheduler.priority_queue[i]->start =
								scheduler.priority_queue[i]->start->next;
					} else {
						scheduler.priority_queue[i]->start = NULL;
					}
				}

				return curr_tcb;
			}
			prev_ptr = curr_tcb;
			curr_tcb = curr_tcb->next;
		}
	}

	return NULL;
}

tcb* find_in_running(my_pthread_t thread) {
	tcb *running = scheduler.running_queue;
	tcb *prev = NULL;
	while (running != NULL) {
		if (running->tid == thread) {
			if (prev != NULL) {
				prev->next = running->next;
			} else {
				scheduler.running_queue = scheduler.running_queue->next;
			}
			running->next = NULL;
			return running;
		}
		prev = running;
		running = running->next;
	}
	return NULL;

}

/* wait for thread termination */
int my_pthread_join(my_pthread_t thread, void **value_ptr) {

	SYS_MODE = 1;
	make_scheduler();
	printf("Joining\n");
	if (thread == -1) {
		printf("The thread has already joined and has been terminated\n");
		return -1;
	}

	if (scheduler.running_thread->state != RUNNING) {
		printf("The thread %d is not running\n", scheduler.running_thread->tid);
		return -1;
	}

	if (scheduler.running_thread->tid == thread) {
		printf("Thread %d cannot join itself\n", thread);
		return -1;
	}

	printf("Thread %d joining thread %d\n", scheduler.running_thread->tid,
			thread);

	tcb* t;
	if ((t = get_tcb(thread)) == NULL) {
		printf("Given thread does not exist in queue\n");
	}

	if (t == NULL) {
		if ((t = find_in_running(thread)) == NULL) {
			printf("Given thread does not exist in running Q\n");
			return -1;
		}
	}

	if (t->state == TERMINATED) {
		*value_ptr = t->return_val;
		return 0;
	} else {
		scheduler.running_thread->state = WAITING;
		enqueue(t->tcb_wait_queue, scheduler.running_thread);
		enqueue(scheduler.priority_queue[t->priority], t);
		my_pthread_yield();
	}

	if (value_ptr != NULL) {
		value_ptr = t->return_val;
	}

	return 0;
}

/*Keep track of Mutexes in the system*/
int mutex_exists(my_pthread_mutex_t *mutex) {
	my_pthread_mutex_t *temp = scheduler.mutex_list;
	while (temp != NULL) {
		if (temp->id == mutex->id) {
			return 0;
		}
		temp = temp->next;
	}
	return -1;
}

void enqueue_mutex(my_pthread_mutex_t *queue, my_pthread_mutex_t *new_mutex) {

	assert(queue!=NULL);

	my_pthread_mutex_t *temp = queue;

	while (temp->next != NULL) {
		temp = temp->next;
	}

	temp->next = new_mutex;
}

void dequeue_mutex(my_pthread_mutex_t *queue) {

	assert(queue!=NULL);
	my_pthread_mutex_t *temp = queue;
	my_pthread_mutex_t *prev = NULL;

	if (temp->next != NULL) {
		prev = temp;
		temp = temp->next;
	}

	free(prev);
	queue = temp;
}

/* initial the mutex lock */
int my_pthread_mutex_init(my_pthread_mutex_t *mutex,
		const pthread_mutexattr_t *mutexattr) {

	SYS_MODE = 1;

	make_scheduler();

	if (mutex == NULL) {
		printf("Mutex initialization failed\n");
		return -1;
	}
	mutex->initialized = 1;
	mutex->lock = 0;
	NO_OF_MUTEX++;
	mutex->tid = 0;
	mutex->m_wait_queue = (tcb_list *) malloc(sizeof(tcb_list));
	init_queue(&(mutex->m_wait_queue));
	enqueue_mutex(scheduler.mutex_list, mutex);
	SYS_MODE = 0;
	return 0;
}

/* aquire the mutex lock */
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex) {

	assert(mutex != NULL);

	if (mutex->initialized == 0) {
		printf("Mutex not initialized, Cannot lock it.");
		return -1;
	}

	tcb_list *wait_queue = mutex->m_wait_queue;
	SYS_MODE = 1;

	if (mutex->lock == 1) {

		if (scheduler.running_thread->tid == mutex->tid) {
			printf("Lock is already held by the current thread %d", mutex->tid);
			return -1;
		}

		if (wait_queue != NULL) {

			scheduler.running_thread->state = WAITING;

			find_in_running(scheduler.running_thread->tid);

			enqueue(wait_queue, scheduler.running_thread);

			pthread_yield();

			mutex->tid = scheduler.running_thread->tid;
			scheduler.running_thread->state = RUNNING;
			SYS_MODE = 0;
			return 0;
		} else {
			printf("Wait queue itself is null\n");
		}

	}

	if (mutex->lock == 0) {
		if (mutex->m_wait_queue->start == NULL) {
			mutex->lock = 1;
			mutex->tid = scheduler.running_thread->tid;
			SYS_MODE = 0;
			reset_timer();
			return 0;
		} else {
			enqueue(wait_queue, scheduler.running_thread);
			scheduler.running_thread->state = WAITING;

			pthread_yield();

			mutex->tid = scheduler.running_thread->tid;
			scheduler.running_thread->state = RUNNING;
			SYS_MODE = 0;
			return 0;
		}

	}
	SYS_MODE = 0;
	return -1;
}

/* release the mutex lock */
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex) {
	assert(mutex != NULL);

	if (mutex->initialized == 0) {
		printf("Mutex not initialized, Cannot unlock it.");
		return -1;
	}

	if (mutex->lock == 0) {
		printf("Mutex not locked, Cannot unlock it.");
		return -1;
	}

	tcb_list *wait_queue = mutex->m_wait_queue;
	SYS_MODE = 1;

	if (mutex->lock == 1) {

		if (scheduler.running_thread->tid != mutex->tid) {
			printf("Lock owner is thread %d, cannot unlock it", mutex->tid);
			return -1;
		}

		if (scheduler.running_thread->tid == mutex->tid) {
			if (wait_queue->start == NULL) {	//Changed recently
				mutex->lock = 0;
				mutex->tid = -1;
				my_pthread_yield();
				return 0;
			} else {

				if (mutex->m_wait_queue != NULL
						&& mutex->m_wait_queue->start != NULL) {

					tcb *new_mutex_owner = mutex->m_wait_queue->start;
					new_mutex_owner->state = READY;
					enqueue(scheduler.priority_queue[new_mutex_owner->priority],
							new_mutex_owner);

					mutex->tid = new_mutex_owner->tid;
					mutex->m_wait_queue->start =
							mutex->m_wait_queue->start->next;
					if (mutex->m_wait_queue->start == NULL)
						mutex->m_wait_queue->end = NULL;
				}

				my_pthread_yield();
				return 0;
			}
		}

	}

	return -1;
}

/* destroy the mutex */
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex) {
	assert(mutex != NULL);

	SYS_MODE = 1;

	if (mutex->initialized == 0) {
		printf("Mutex is not initialized, cannot destroy");
		//call the next thread
		my_pthread_yield();
		return -1;
	}

	if (mutex->lock == 1&& mutex->tid == scheduler.running_thread->tid
	&& mutex->m_wait_queue == NULL) {
		SYS_MODE = 1;
		printf("Mutex is held by the owner %d, can destroy", mutex->tid);
		mutex->initialized = 0;
		reset_timer();
		SYS_MODE = 0;
		return 0;
	}

	if (mutex->lock == 0 && mutex->tid == -1) {
		SYS_MODE = 1;
		printf("No one has the lock and no one is waiting for it, destroy...");
		mutex->initialized = 0;
		reset_timer();
		SYS_MODE = 0;
		return 0;
	}

	return 0;
}

