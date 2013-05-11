OSTYPE = $(shell uname -s)
DEBUG = 1

# $(info OSTYPE = $(OSTYPE))

OBJDIR =          obj
BINDIR =          bin

STATSLIB =        $(OBJDIR)/libstats.a

LIB_OBJS =        $(OBJDIR)/stats.o $(OBJDIR)/shared_mem.o $(OBJDIR)/semaphore.o $(OBJDIR)/lock.o $(OBJDIR)/error.o $(OBJDIR)/hash.o
STATS_TEST_OBJS = $(OBJDIR)/stats_test.o
SHMEM_TEST_OBJS = $(OBJDIR)/shmem_test.o
SEM_TEST_OBJS =   $(OBJDIR)/sem_test.o
LOCK_TEST_OBJS =  $(OBJDIR)/lock_test.o

TESTS =           $(BINDIR)/shmem_test $(BINDIR)/sem_test $(BINDIR)/lock_test $(BINDIR)/find_prime $(BINDIR)/stats_test

all: $(OBJDIR) $(BINDIR) $(STATSLIB) $(TESTS)

clean:
	-rm $(OBJDIR)/*.o $(STATSLIB) $(TESTS)
	-rmdir $(OBJDIR)
	-rmdir $(BINDIR)

INCLUDES=include ext
INCLUDEFLAGS=$(foreach dir,$(INCLUDES),-I $(dir))

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

LIBFLAGS =        -Lobj -lstats

$(STATSLIB): $(LIB_OBJS)
	ar -r $@ $(LIB_OBJS)

$(BINDIR)/shmem_test: $(SHMEM_TEST_OBJS) $(STATSLIB)
	$(CC) $(LINKFLAGS) -o $@ $(SHMEM_TEST_OBJS) $(LIBFLAGS)

$(BINDIR)/stats_test: $(STATS_TEST_OBJS) $(STATSLIB)
	$(CC) $(LINKFLAGS) -o $@ $(STATS_TEST_OBJS) $(LIBFLAGS)

$(BINDIR)/sem_test: $(SEM_TEST_OBJS) $(STATSLIB)
	$(CC) $(LINKFLAGS) -o $@ $(SEM_TEST_OBJS) $(LIBFLAGS)

$(BINDIR)/lock_test: $(LOCK_TEST_OBJS) $(STATSLIB)
	$(CC) $(LINKFLAGS) -o $@ $(LOCK_TEST_OBJS) $(LIBFLAGS)

$(BINDIR)/find_prime: $(OBJDIR)/find_prime.o
	$(CC) $(LINKFLAGS) -o $@ $(OBJDIR)/find_prime.o $(LIBFLAGS)

$(OBJDIR):
	mkdir $(OBJDIR)

$(BINDIR):
	mkdir $(BINDIR)

$(OBJDIR)/%.o: src/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: ext/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: tools/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: test/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<


$(OBJDIR)/lock.o: include/error.h include/semaphore.h include/omode.h include/lock.h
$(OBJDIR)/stats.o: include/error.h include/stats.h include/shared_mem.h include/semaphore.h include/lock.h include/omode.h
$(OBJDIR)/shared_mem.o: include/error.h include/shared_mem.h include/omode.h
$(OBJDIR)/semaphore.o: include/error.h include/semaphore.h include/omode.h

$(OBJDIR)/shmem_test.o: include/error.h include/shared_mem.h include/omode.h
$(OBJDIR)/stats_test.o: include/error.h include/shared_mem.h include/omode.h include/semaphore.h include/lock.h include/stats.h
$(OBJDIR)/sem_test.o: include/error.h include/semaphore.h include/omode.h
$(OBJDIR)/lock_test.o: include/error.h include/semaphore.h include/lock.h include/omode.h

