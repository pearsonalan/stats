/* shared_mem.h */

#ifndef _SHARED_MEM_H_
#define _SHARED_MEM_H_

#include <sys/shm.h>
#include <sys/ipc.h>

#include "omode.h"

#define SHARED_MEMORY_MAGIC   'shmm'

#define DESTROY_ON_CLOSE                    0x0000010
#define DESTROY_ON_CLOSE_IF_LAST            0x0000020
#define DESTROY_MASK                        0x0000030

#define SHARED_MEMORY_MAX_NAME_LEN 31

struct shared_memory {
    int magic;
    int flags;
    int size;
    char name[SHARED_MEMORY_MAX_NAME_LEN + 1];
    key_t shmkey;
    int shmid;
    short int created;
    void * ptr;
};

int shared_memory_create(const char *name, int flags, int size, struct shared_memory **shmem_out);
int shared_memory_open(struct shared_memory *shmem);
int shared_memory_close(struct shared_memory *shmem);
void shared_memory_free(struct shared_memory *shmem);
int shared_memory_nattach(struct shared_memory *shmem, int *attach_count_out);

#define shared_memory_size(s) (s->size)
#define shared_memory_ptr(s) (s->ptr)
#define shared_memory_name(s) (s->name)
#define shared_memory_was_created(s) (s->created)

#define SHARED_MEMORY_DIRECTORY "/tmp"
#define MAX_PATH 255

#endif
