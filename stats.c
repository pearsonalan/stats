/* stats.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "stats.h"

int stats_create(const char *name, struct stats **stats_out)
{
    struct stats * stats = NULL;
    int err = S_OK;

    if (stats_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    stats = (struct stats *) malloc(sizeof(struct stats));
    if (stats == NULL)
        goto fail;

    stats->data = NULL;

    err = lock_init(&stats->lock, name);
    if (err != S_OK)
        goto fail;

    err = shared_memory_init(&stats->shmem, name, OMODE_OPEN_OR_CREATE | DESTROY_ON_CLOSE_IF_LAST, sizeof(struct stats_data));
    if (err != S_OK)
        goto fail;

    err = S_OK;
    goto ok;

fail:

    if (stats)
    {
        free(stats);
        stats = NULL;
    }

ok:
    *stats_out = stats;
    return err;
}

int stats_open(struct stats *stats)
{
    int err;

    err = lock_open(&stats->lock);
    if (err == S_OK)
    {
        err = shared_memory_open(&stats->shmem);
    }
    return err;
}

int stats_close(struct stats *stats)
{
    lock_close(&stats->lock);
    shared_memory_close(&stats->shmem);
    return S_OK;
}

int stats_free(struct stats *stats)
{
    free(stats);
    return S_OK;
}

int stats_allocate_counter(struct stats *stats, char *name, int *key_out)
{
    return S_OK;
}
