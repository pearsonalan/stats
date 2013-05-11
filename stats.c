/* stats.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "error.h"
#include "stats.h"
#include "hash.h"
#include "debug.h"

static void stats_init_data(struct stats *stats);
static int stats_hash_probe(struct stats_data *data, const char *key, int len);

int stats_create(const char *name, struct stats **stats_out)
{
    struct stats * stats = NULL;
    int err = S_OK;
    char lock_name[SEMAPHORE_MAX_NAME_LEN+1];
    char mem_name[SHARED_MEMORY_MAX_NAME_LEN];

    assert(sizeof(struct stats_header) == 16);
    assert(sizeof(struct stats_counter) == 48);

    if (stats_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    /* check that the length of the name plus the extension we are adding is not too long */
    if (strlen(name) + 4 > SEMAPHORE_MAX_NAME_LEN)
        return ERROR_INVALID_PARAMETERS;
    if (strlen(name) + 4 > SHARED_MEMORY_MAX_NAME_LEN)
        return ERROR_INVALID_PARAMETERS;

    strcpy(lock_name,name);
    strcat(lock_name,".sem");
    strcpy(mem_name,name);
    strcat(mem_name,".mem");

    stats = (struct stats *) malloc(sizeof(struct stats));
    if (stats == NULL)
        goto fail;

    stats->magic = STATS_MAGIC;
    stats->data = NULL;

    err = lock_init(&stats->lock, lock_name);
    if (err != S_OK)
        goto fail;

    err = shared_memory_init(&stats->shmem, mem_name, OMODE_OPEN_OR_CREATE | DESTROY_ON_CLOSE_IF_LAST, sizeof(struct stats_data));
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
    DPRINTF("Intializing stats data\n");

    memset(stats->data,0,sizeof(struct stats_data));
    stats->data->hdr.stats_magic = STATS_MAGIC;
}

int stats_close(struct stats *stats)
{
    int shared_mem_destroyed;
    shared_memory_close(&stats->shmem,&shared_mem_destroyed);
    lock_close(&stats->lock,shared_mem_destroyed);
    return S_OK;
}

int stats_free(struct stats *stats)
{
    free(stats);
    return S_OK;
}

int stats_allocate_counter(struct stats *stats, const char *name, struct stats_counter **ctr_out)
{
    int loc, key_len;
    int err = S_OK;
    struct stats_counter *ctr = NULL;

    if (!stats || stats->magic != STATS_MAGIC || stats->data == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (ctr_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    key_len = strlen(name);
    if (key_len > MAX_COUNTER_KEY_LENGTH)
        return ERROR_STATS_KEY_TOO_LONG;

    lock_acquire(&stats->lock);

    loc = stats_hash_probe(stats->data, name, key_len);
    if (loc == -1)
    {
        err = ERROR_STATS_CANNOT_ALLOCATE_COUNTER;
    }
    else
    {
        ctr = stats->data->ctr + loc;
        if (ctr->ctr_allocation_status != ALLOCATION_STATUS_ALLOCATED)
        {
            ctr->ctr_allocation_status = ALLOCATION_STATUS_ALLOCATED;
            ctr->ctr_key_len = key_len;
            memcpy(ctr->ctr_key, name, key_len);
            stats->data->hdr.stats_sequence_number++;
        }
    }

    lock_release(&stats->lock);

    *ctr_out = ctr;

    assert(*ctr_out != NULL && err == S_OK || *ctr_out == NULL && err != S_OK);

    return err;
}

int stats_get_counters(struct stats *stats, struct stats_counter **counters, int counter_size, int *counter_out, int *sequence_number_out)
{
    int err = S_OK;
    int i, n;
    struct stats_data *data;

    if (!stats || stats->magic != STATS_MAGIC || stats->data == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (!counters)
        return ERROR_INVALID_PARAMETERS;

    data = stats->data;
    memset(counters, 0, sizeof(struct stats_counter *) * counter_size);
    n = 0;
    i = 0;

    lock_acquire(&stats->lock);

    while (i < COUNTER_TABLE_SIZE)
    {
        if (data->ctr[i].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED)
        {
            counters[n] = data->ctr + i;
            n++;
        }
        i++;
    }

    if (counter_out)
        *counter_out = n;

    if (sequence_number_out)
        *sequence_number_out = data->hdr.stats_sequence_number;

    lock_release(&stats->lock);

    return err;
}


static int stats_hash_probe(struct stats_data *data, const char *key, int len)
{
    uint32_t h, k, n;
    int i, probes = 1;

    h = fast_hash(key,len);
    k = h % COUNTER_TABLE_SIZE;

    if (data->ctr[k].ctr_allocation_status == ALLOCATION_STATUS_FREE)
    {
        DPRINTF("emtpy slot %d found after %d probes\n", k, probes);
        return k;
    }
    else if (data->ctr[k].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED &&
             data->ctr[k].ctr_key_len == len &&
             memcmp(data->ctr[k].ctr_key,key,len) == 0)
    {
        DPRINTF("found matching slot %d found after %d probes\n", k, probes);
        return k;
    }

    n = 1;
    for (i =0; i < 32; i++)
    {
        k = (h + n) % COUNTER_TABLE_SIZE;
        probes++;
        if (data->ctr[k].ctr_allocation_status == ALLOCATION_STATUS_FREE)
        {
            DPRINTF("emtpy slot %d found after %d probes\n", k, probes);
            return k;
        }
        else if (data->ctr[k].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED &&
                 data->ctr[k].ctr_key_len == len &&
                 memcmp(data->ctr[k].ctr_key,key,len) == 0)
        {
            DPRINTF("found matching slot %d found after %d probes\n", k, probes);
            return k;
        }
        n = n * 2;
    }

    DPRINTF("no emtpy slot %d found after %d probes\n", k, probes);
    return -1;
}

void counter_get_key(struct stats_counter *ctr, char *buf, int buflen)
{
    if (buflen >= ctr->ctr_key_len+1)
    {
        memcpy(buf,ctr->ctr_key,ctr->ctr_key_len);
        buf[ctr->ctr_key_len] = '\0';
    }
    else
    {
        memcpy(buf,ctr->ctr_key,buflen-1);
        buf[buflen-1] = '\0';
    }
}

void counter_increment(struct stats_counter *ctr)
{
    if (ctr != NULL)
    {
        __sync_fetch_and_add_8(&ctr->ctr_value.val64,1);
    }
}
