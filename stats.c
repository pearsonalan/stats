/* stats.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "stats.h"

int stats_create(struct stats **stats_out)
{
    struct stats * stats = NULL;
    struct semaphore * sem = NULL;
    struct shared_memory * shmem = NULL;
    int err = S_OK;

    if (stats_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    stats = (struct stats *) malloc(sizeof(struct stats));
    if (stats == NULL)
        goto fail;

    err = semaphore_create("stats", OMODE_OPEN_OR_CREATE, 1, &sem);
    if (err != S_OK)
        goto fail;

    err = shared_memory_create("stats", OMODE_OPEN_OR_CREATE, sizeof(struct stats_data), &shmem);
    if (err != S_OK)
        goto fail;

    stats->sem = sem;
    stats->shmem = shmem;
    stats->data = (struct stats_data *) shared_memory_ptr(shmem);

    err = S_OK;
    goto ok;

fail:

    if (stats)
    {
        free(stats);
        stats = NULL;
    }

    if (sem)
    {
        semaphore_close(sem);
        semaphore_free(sem);
        sem = NULL;
    }

    if (shmem)
    {
        shared_memory_close(shmem);
        shared_memory_free(shmem);
        shmem = NULL;
    }

ok:
    *stats_out = stats;
    return err;
}

int stats_allocate_counter(struct stats *stats, char *name, int *key_out)
{
    return S_OK;
}
