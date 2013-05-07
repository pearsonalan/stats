OSTYPE = $(shell uname -s)
DEBUG = 1

$(info OSTYPE = $(OSTYPE))

all: obj bin bin/shmem_test bin/sem_test bin/lock_test bin/find_prime bin/stats_test

clean:
	-rm obj/*.o bin/shmem_test bin/sem_test bin/lock_test bin/find_prime bin/stats_test
	-rmdir obj
	-rmdir bin

ifeq ($(DEBUG),1)
CFLAGS = -DDEBUG=1 -Wall -g
LINKFLAGS = -g
else
CFLAGS = -DDEBUG=0 -Wall -O2
LINKFLAGS = -O2
endif

ifeq ($(OSTYPE),Darwin)
  CC = clang
  CFLAGS += -DDARWIN
endif

ifeq ($(OSTYPE),Linux)
  CC = gcc
  CFLAGS += -DLINUX -Wno-multichar
endif

STATS_TEST_OBJS =   obj/stats.o obj/stats_test.o obj/shared_mem.o obj/semaphore.o obj/lock.o obj/error.o
SHMEM_TEST_OBJS =   obj/shmem_test.o obj/shared_mem.o obj/semaphore.o obj/error.o
SEM_TEST_OBJS =     obj/sem_test.o obj/semaphore.o obj/error.o
LOCK_TEST_OBJS =    obj/lock_test.o obj/semaphore.o obj/lock.o obj/error.o

bin/shmem_test: $(SHMEM_TEST_OBJS)
	$(CC) $(LINKFLAGS) -o $@ $(SHMEM_TEST_OBJS)

bin/stats_test: $(STATS_TEST_OBJS)
	$(CC) $(LINKFLAGS) -o $@ $(STATS_TEST_OBJS)

bin/sem_test: $(SEM_TEST_OBJS)
	$(CC) $(LINKFLAGS) -o $@ $(SEM_TEST_OBJS)

bin/lock_test: $(LOCK_TEST_OBJS)
	$(CC) $(LINKFLAGS) -o $@ $(LOCK_TEST_OBJS)

bin/find_prime: obj/find_prime.o
	$(CC) $(LINKFLAGS) -o $@ obj/find_prime.o

obj:
	mkdir obj

bin:
	mkdir bin

obj/%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

obj/lock.o: error.h semaphore.h omode.h lock.h
obj/stats.o: error.h stats.h shared_mem.h semaphore.h lock.h omode.h
obj/shared_mem.o: error.h shared_mem.h omode.h
obj/semaphore.o: error.h semaphore.h omode.h
obj/shmem_test.o: error.h shared_mem.h omode.h
obj/stats_test.o: error.h shared_mem.h omode.h semaphore.h lock.h
obj/sem_test.o: error.h semaphore.h omode.h
obj/lock_test.o: error.h semaphore.h lock.h omode.h

