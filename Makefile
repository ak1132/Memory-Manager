CC = gcc
CFLAGS = -g -c
AR = ar -rc
RANLIB = ranlib


Target: my_pthread.a

my_pthread.a: my_pthread.o
	$(AR) libmy_pthread.a my_pthread.o
	$(RANLIB) libmy_pthread.a

my_pthread.o: my_pthread_t.h my_mem_manager.h
	$(CC) -pthread $(CFLAGS) my_pthread.c my_mem_manager.c

compile:
	$(CC) -pthread -g -O0 -m32 -o my_mem_manager my_pthread.c my_mem_manager.c debug.c
	
clean:
	rm -rf testfile *.o *.a my_mem_manager
