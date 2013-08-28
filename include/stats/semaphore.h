/* semaphore.h */

#ifndef _SEMAPHORE_H_
#define _SEMAPHORE_H_

#include <sys/ipc.h>
#include <sys/sem.h>

#include "omode.h"

#define SEMAPHORE_MAGIC   'semm'

#define SEMAPHORE_MAX_NAME_LEN 31

struct semaphore {
    int magic;
    unsigned short size;
    char name[SEMAPHORE_MAX_NAME_LEN + 1];
    key_t semkey;
    int semid;
};

#ifdef LINUX
union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux-specific) */
};
#endif

int semaphore_create(const char *name, unsigned short size, struct semaphore **sem_out);
int semaphore_init(struct semaphore *sem, const char *name, unsigned short size);
int semaphore_open(struct semaphore *sem, int flags);
int semaphore_open_and_set(struct semaphore *sem, ... );
int semaphore_set_value(struct semaphore *sem, unsigned short nsem, unsigned short value);
int semaphore_set_values(struct semaphore *sem, unsigned short *values);
int semaphore_P(struct semaphore *sem, unsigned short nsem);
int semaphore_V(struct semaphore *sem, unsigned short nsem);
int semaphore_close(struct semaphore *sem, int remove);
void semaphore_free(struct semaphore *sem);

#define semaphore_signal semaphore_V
#define semaphore_wait semaphore_P

#define semaphore_size(s) ((s)->size)
#define semaphore_name(s) ((s)->name)
#define semaphore_is_open(s) ((s)->semid != -1)
#define SEMAPHORE_DIRECTORY "/tmp"
#define MAX_PATH 255

#endif
