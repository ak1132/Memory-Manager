/*
 * debug.c
 *
 *  Created on: Oct 27, 2018
 *      Author: sg1425
 */

#include "my_pthread_t.h"
#include "my_mem_manager.h"

/*
int main(int argc, char **argv) {
	int *i = malloc(500 * sizeof(int));
	*i = malloc(100 * sizeof(int));
}
*/

void * dummyFunction(tcb *thread) {
	my_pthread_t curr_threadID = thread->tid;
	printf("Entered Thread %i\n", curr_threadID);

	int i = 0, tot_mem = 0;
	for (i = 0; i < 5; i++) {

		tot_mem += 4000;
		printf("Thread : %d  total: %d\n", curr_threadID, tot_mem);
		int *ptr = (int*)malloc(4000);
		*ptr = curr_threadID * 100 + i;
		printf("Store : %d\n", *ptr);
		pthread_yield();
		printf("Thread : %d   Retrieve : %d\n", curr_threadID, *ptr);

	}
	printf("Exited Thread: %i\n", curr_threadID);
	return &(thread->tid);
}

int main(int argc, char **argv) {
	pthread_t t1, t2, t3, t4;
	pthread_create(&t1, NULL, (void *) dummyFunction, &t1);
	pthread_create(&t2, NULL, (void *) dummyFunction, &t2);

	int curr_threadID = 1;
	int i = 0, tot_mem = 0;
	for (i = 0; i < 5; i++) {
		tot_mem += 4000;
		printf("Thread : %d  total: %d\n", curr_threadID, tot_mem);
		int *ptr = (int*)malloc(3000);
		*ptr = curr_threadID * 100 + i;
		printf("Store : %d\n", *ptr);
		pthread_yield();
		printf("Thread : %d   Retrieve : %d\n", curr_threadID, *ptr);
	}

	printf("Done\n");
	void *value_ptr = NULL;
	pthread_exit(value_ptr);
	return 0;
}
