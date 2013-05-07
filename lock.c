/* lock.c */

#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "lock.h"

int lock_create(const char * name, struct lock **lock_out)
{
    struct lock * lock;
    int err;

    if (lock_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    lock = malloc(sizeof(struct lock));
    if (!lock)
        return ERROR_MEMORY;

    err = lock_init(lock,name);
    if (err == S_OK)
    {
        *lock_out = lock;
    }
    else
    {
        free(lock);
        *lock_out = NULL;
    }

    return err;
}

int lock_init(struct lock *lock, const char *name)
{
    return semaphore_init(&lock->sem, name, 1);
}

int lock_open(struct lock *lock)
{
    return semaphore_open_and_set(&lock->sem, 1);
}

int lock_acquire(struct lock *lock)
{
    return semaphore_P(&lock->sem,0);
}

int lock_release(struct lock *lock)
{
    return semaphore_V(&lock->sem,0);
}

int lock_close(struct lock *lock)
{
    return semaphore_close(&lock->sem);
}

void lock_free(struct lock *lock)
{
    free(lock);
}
