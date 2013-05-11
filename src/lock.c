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


void lock_free(struct lock *lock)
{
    free(lock);
}
