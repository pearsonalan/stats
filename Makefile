OSTYPE = $(shell uname -s)
DEBUG = 0

# $(info OSTYPE = $(OSTYPE))

OBJDIR =		obj
BINDIR =		bin

STATSLIB =		$(OBJDIR)/libstats.a

LIB_OBJS =		$(OBJDIR)/stats.o $(OBJDIR)/shared_mem.o $(OBJDIR)/semaphore.o \
			$(OBJDIR)/lock.o $(OBJDIR)/error.o $(OBJDIR)/hash.o

ifeq ($(OSTYPE),LINUX)
  LIB_OBJS += $(OBJDIR)/strlcpy.o $(OBJDIR)/strlcat.o
endif

STATS_TEST_OBJS =	$(OBJDIR)/stats_test.o
SHMEM_TEST_OBJS =	$(OBJDIR)/shmem_test.o
SEM_TEST_OBJS =		$(OBJDIR)/sem_test.o
LOCK_TEST_OBJS =	$(OBJDIR)/lock_test.o
KEYSTATS_OBJS = 	$(OBJDIR)/keystats.o $(OBJDIR)/screenutil.o
STATSVIEW_OBJS = 	$(OBJDIR)/statsview.o $(OBJDIR)/screenutil.o
STATSRV_OBJS = 		$(OBJDIR)/statsrv.o
HISTD_OBJS =		$(OBJDIR)/histd.o $(OBJDIR)/http.o
HISTD_CLIENT_OBJS =	$(OBJDIR)/histd_client.o

TESTS = 		$(BINDIR)/shmem_test $(BINDIR)/sem_test $(BINDIR)/lock_test $(BINDIR)/stats_test
TOOLS =			$(BINDIR)/find_prime $(BINDIR)/statsview $(BINDIR)/statsrv $(BINDIR)/keystats $(BINDIR)/histd_client
DAEMONS =		$(BINDIR)/histd

ifeq ($(PREFIX),)
  PREFIX = 		/usr/local
endif

ifeq ($(INSTALLDIR),)
  INSTALLDIR = 		$(DESTDIR)$(PREFIX)
endif

.PHONY: rubyext all build install

all: build

clean:
	-rm $(OBJDIR)/*.o $(STATSLIB) $(TESTS) $(TOOLS) $(DAEMONS)
	-rmdir $(OBJDIR)
	-rmdir $(BINDIR)

build: $(OBJDIR) $(BINDIR) $(STATSLIB) $(TESTS) $(TOOLS) $(DAEMONS) # rubyext

install: build
	mkdir -p $(INSTALLDIR)/include/stats
	/bin/cp include/stats/*.h $(INSTALLDIR)/include/stats/
	/bin/cp ext/*.h $(INSTALLDIR)/include/
	mkdir -p $(INSTALLDIR)/lib
	/usr/bin/install $(STATSLIB) $(INSTALLDIR)/lib

rubyext:
	cd ruby && make

INCLUDES=include ext util $(INSTALLDIR)/include
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
  CFLAGS += -DLINUX -Wno-multichar -fPIC
  LINKFLAGS += -fPIC
endif

LIBFLAGS =        -Lobj -L$(INSTALLDIR)/lib -lstats

ifeq ($(OSTYPE),Linux)
  LIBFLAGS += -lrt
endif

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

$(BINDIR)/statsview: $(STATSVIEW_OBJS) $(STATSLIB)
	$(CC) $(LINKFLAGS) -o $@ $(STATSVIEW_OBJS) $(LIBFLAGS) -lcurses

$(BINDIR)/keystats: $(KEYSTATS_OBJS) $(STATSLIB)
	$(CC) $(LINKFLAGS) -o $@ $(KEYSTATS_OBJS) $(LIBFLAGS) -lcurses

$(BINDIR)/statsrv: $(STATSRV_OBJS) $(STATSLIB)
	$(CC) $(LINKFLAGS) -o $@ $(STATSRV_OBJS) $(LIBFLAGS) -levent

$(BINDIR)/histd: $(HISTD_OBJS)
	$(CC) $(LINKFLAGS) -o $@ $(HISTD_OBJS) $(LIBFLAGS) -levent

$(BINDIR)/histd_client: $(HISTD_CLIENT_OBJS)
	$(CC) $(LINKFLAGS) -o $@ $(HISTD_CLIENT_OBJS)

$(OBJDIR):
	mkdir $(OBJDIR)

$(BINDIR):
	mkdir $(BINDIR)

$(OBJDIR)/%.o: src/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: ext/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: util/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: tools/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: test/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: histd/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: histd_client/%.c
	$(CC) -c $(INCLUDEFLAGS) $(CFLAGS) -o $@ $<

$(OBJDIR)/lock.o: include/stats/error.h include/stats/semaphore.h include/stats/omode.h include/stats/lock.h
$(OBJDIR)/stats.o: include/stats/error.h include/stats/stats.h include/stats/shared_mem.h include/stats/semaphore.h include/stats/lock.h include/stats/omode.h
$(OBJDIR)/shared_mem.o: include/stats/error.h include/stats/shared_mem.h include/stats/omode.h
$(OBJDIR)/semaphore.o: include/stats/error.h include/stats/semaphore.h include/stats/omode.h

$(OBJDIR)/shmem_test.o: include/stats/error.h include/stats/shared_mem.h include/stats/omode.h
$(OBJDIR)/stats_test.o: include/stats/error.h include/stats/shared_mem.h include/stats/omode.h include/stats/semaphore.h include/stats/lock.h include/stats/stats.h
$(OBJDIR)/sem_test.o: include/stats/error.h include/stats/semaphore.h include/stats/omode.h
$(OBJDIR)/lock_test.o: include/stats/error.h include/stats/semaphore.h include/stats/lock.h include/stats/omode.h

$(OBJDIR)/histd.o: histd/histd.h include/histd/protocol.h
$(OBJDIR)/histd_client.o: include/histd/protocol.h
