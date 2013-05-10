/* stats.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "error.h"
#include "stats.h"

static void stats_init_data(struct stats *stats);

int stats_create(const char *name, struct stats **stats_out)
{
    struct stats * stats = NULL;
    int err = S_OK;

    if (stats_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    stats = (struct stats *) malloc(sizeof(struct stats));
    if (stats == NULL)
        goto fail;

    stats->magic = STATS_MAGIC;
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

    if (!stats || stats->magic != STATS_MAGIC || stats->data != NULL)
        return ERROR_INVALID_PARAMETERS;

    assert(!lock_is_open(&stats->lock));
    assert(!shared_memory_is_open(&stats->shmem));
    assert(stats->data == NULL);

    /* open the lock */
    err = lock_open(&stats->lock);
    if (err == S_OK)
    {
        /* acquire the lock to make the process of getting and initializing the shared memory atomic */
        lock_acquire(&stats->lock);

        /* open the shared memory */
        err = shared_memory_open(&stats->shmem);
        if (err == S_OK)
        {
            assert(shared_memory_size(&stats->shmem) == sizeof(struct stats_data));
            assert(shared_memory_ptr(&stats->shmem) != NULL);

            /* get the pointer to the shared memory and initialize it if this process created it */
            stats->data = (struct stats_data *) shared_memory_ptr(&stats->shmem);

            if (shared_memory_was_created(&stats->shmem))
            {
                stats_init_data(stats);
            }

            assert(stats->data->hdr.stats_magic == STATS_MAGIC);
        }

        lock_release(&stats->lock);
    }

    assert((err == S_OK && stats->data != NULL) || (err != S_OK && stats->data == NULL));

    return err;
}


static void stats_init_data(struct stats *stats)
{
    printf("Intializing stats data\n");

    memset(stats->data,0,sizeof(struct stats_data));
    stats->data->hdr.stats_magic = STATS_MAGIC;
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
