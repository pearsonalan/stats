/* stats.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef DARWIN
#include <mach/mach_time.h>
#endif

#include "stats/error.h"
#include "stats/stats.h"
#include "stats/hash.h"
#include "stats/debug.h"


static void stats_init_data(struct stats *stats);
static int stats_hash_probe(struct stats_data *data, const char *key, int len);


#ifdef DARWIN
static mach_timebase_info_data_t  timebase_info = {0,0};
#endif

#ifdef LINUX
long long current_time()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC,&ts);

    return (long long)ts.tv_sec * 1000000000ll + (long long)ts.tv_nsec;
}

long long time_delta_to_nanos(long long start, long long end)
{
    return start - end;
}
#endif

#ifdef DARWIN
long long current_time()
{
    return mach_absolute_time();
}

long long time_delta_to_nanos(long long start, long long end)
{
    long long elapsed, elapsed_nano;

    elapsed = end - start;

    if (timebase_info.denom == 0)
    {
        mach_timebase_info(&timebase_info);
    }

    // Do the maths. We hope that the multiplication doesn't
    // overflow; the price you pay for working in fixed point.

    elapsed_nano = elapsed * timebase_info.numer / timebase_info.denom;

    return elapsed_nano;
}
#endif

/*
 * stats_create
 *
 * Create and initialize a stats object.  The resources needed for the stats
 * (files, shared memory, semaphore, etc) are not opened yet. The stats_open
 * call must be invoked.
 *
 * If stats_create returns a stats object, you must call stats_free on it
 * to releaseall memory used by the stats object.
 *
 * Returns:
 *    S_OK                              - success
 *    ERROR_INVALID_PARAMETERS          - the name was too long
 *    ERROR_MEMORY                      - out of memory / memory allocation error
 */
int stats_create(const char *name, struct stats **stats_out)
{
    struct stats * stats = NULL;
    int err;
    char lock_name[SEMAPHORE_MAX_NAME_LEN+1];
    char mem_name[SHARED_MEMORY_MAX_NAME_LEN];

    /* printf("Sizeof stats counter is %ld\n",sizeof(struct stats_counter)); */
    assert(sizeof(struct stats_header) == 16);
    assert(sizeof(struct stats_counter) == 56);

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
    {
        err = ERROR_MEMORY;
        goto fail;
    }

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


/*
 * stats_open
 *
 * Opens the resources needed for a stats object. After this call returns
 * successfully, the stats object is ready for use.
 *
 * The resources allocated by stats_open must be freed by a corresponding
 * call to stats_close().
 *
 * Returns:
 *    S_OK                              - success
 *    ERROR_INVALID_PARAMETERS          - the stats object passed was not valid
 */
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
            ctr->ctr_allocation_seq = stats->data->hdr.stats_sequence_number++;
            ctr->ctr_key_len = key_len;
            memcpy(ctr->ctr_key, name, key_len);
        }
    }

    lock_release(&stats->lock);

    *ctr_out = ctr;

    assert((*ctr_out != NULL && err == S_OK) || (*ctr_out == NULL && err != S_OK));

    return err;
}

static int ctr_compare(const void * a, const void * b)
{
    const struct stats_counter *actr = *(struct stats_counter **)a;
    const struct stats_counter *bctr = *(struct stats_counter **)b;
    return actr->ctr_allocation_seq - bctr->ctr_allocation_seq;
}

int stats_get_counters(struct stats *stats, struct stats_counter **counters, int counter_size, int *counter_out, int *sequence_number_out)
{
    int err = S_OK;
    int i, n;
    struct stats_data *data;
    int seq_no;

    if (!stats || stats->magic != STATS_MAGIC || stats->data == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (!counters || counter_size <= 0)
        return ERROR_INVALID_PARAMETERS;

    data = stats->data;
    memset(counters, 0, sizeof(struct stats_counter *) * counter_size);
    n = 0;
    i = 0;

    lock_acquire(&stats->lock);

    while (i < COUNTER_TABLE_SIZE && n < counter_size)
    {
        if (data->ctr[i].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED)
        {
            counters[n] = data->ctr + i;
            n++;
        }
        i++;
    }

    seq_no = data->hdr.stats_sequence_number;

    lock_release(&stats->lock);

    qsort(counters,n,sizeof(struct stats_counter *),ctr_compare);

    if (counter_out)
        *counter_out = n;

    if (sequence_number_out)
        *sequence_number_out = seq_no;

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
        return k;
    }
    else if (data->ctr[k].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED &&
             data->ctr[k].ctr_key_len == len &&
             memcmp(data->ctr[k].ctr_key,key,len) == 0)
    {
        return k;
    }

    n = 1;
    for (i =0; i < 32; i++)
    {
        k = (h + n) % COUNTER_TABLE_SIZE;
        probes++;
        if (data->ctr[k].ctr_allocation_status == ALLOCATION_STATUS_FREE)
        {
            return k;
        }
        else if (data->ctr[k].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED &&
                 data->ctr[k].ctr_key_len == len &&
                 memcmp(data->ctr[k].ctr_key,key,len) == 0)
        {
            return k;
        }
        n = n * 2;
    }

    return -1;
}

int stats_reset_counters(struct stats *stats)
{
    int i;
    struct stats_data *data;

    if (!stats || stats->magic != STATS_MAGIC || stats->data == NULL)
        return ERROR_INVALID_PARAMETERS;

    data = stats->data;

    lock_acquire(&stats->lock);

    for (i = 0; i < COUNTER_TABLE_SIZE; i++)
    {
        if (data->ctr[i].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED)
        {
            __sync_lock_test_and_set(&data->ctr[i].ctr_value.val64,0ll);
        }
    }

    lock_release(&stats->lock);

    return S_OK;
}

int stats_get_counter_list(struct stats *stats, struct stats_counter_list *cl)
{
    int err = S_OK;
    int i, n;
    struct stats_data *data;

    if (!stats || stats->magic != STATS_MAGIC || stats->data == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (!cl)
        return ERROR_INVALID_PARAMETERS;

    data = stats->data;
    memset(cl, 0, sizeof(struct stats_counter_list));
    n = 0;
    i = 0;

    lock_acquire(&stats->lock);

    while (i < COUNTER_TABLE_SIZE)
    {
        if (data->ctr[i].ctr_allocation_status == ALLOCATION_STATUS_ALLOCATED)
        {
            cl->cl_ctr[n] = data->ctr + i;
            n++;
        }
        i++;
    }

    cl->cl_seq_no = data->hdr.stats_sequence_number;

    lock_release(&stats->lock);

    cl->cl_count = n;
    qsort(cl->cl_ctr,n,sizeof(struct stats_counter *),ctr_compare);

    return err;
}


/**
 * stats_counter_list functions
 */

int stats_cl_create(struct stats_counter_list **cl_out)
{
    struct stats_counter_list *cl;

    if (cl_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    cl = (struct stats_counter_list *)malloc(sizeof(struct stats_counter_list));
    if (!cl)
        return ERROR_FAIL;

    stats_cl_init(cl);
    *cl_out = cl;

    return S_OK;
}

void stats_cl_init(struct stats_counter_list *cl)
{
    memset(cl,0,sizeof(struct stats_counter_list));
}

void stats_cl_free(struct stats_counter_list *cl)
{
    free(cl);
}

int stats_cl_is_updated(struct stats *stats, struct stats_counter_list *cl)
{
    return cl->cl_seq_no != stats->data->hdr.stats_sequence_number;
}



/**
 * stats_sample functions
 *
 */

int stats_sample_create(struct stats_sample **sample_out)
{
    struct stats_sample *sample;

    if (sample_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    sample = (struct stats_sample *)malloc(sizeof(struct stats_sample));
    if (!sample)
        return ERROR_FAIL;

    stats_sample_init(sample);
    *sample_out = sample;

    return S_OK;
}

void stats_sample_init(struct stats_sample *sample)
{
    memset(sample,0,sizeof(struct stats_sample));
}

void stats_sample_free(struct stats_sample *sample)
{
    free(sample);
}

int stats_get_sample(struct stats *stats, struct stats_counter_list *cl, struct stats_sample *sample)
{
    long long sample_time;
    int i, err;

    if (stats == NULL || cl == NULL || sample == NULL)
        return ERROR_INVALID_PARAMETERS;

    /* get the sample time */
    sample_time = current_time();

    /* if the counter list has been updated, update the passed in counter list */
    if (stats_cl_is_updated(stats,cl))
    {
        /* sequence number has changed. this means there might be new counter definitions.
           reload the counter list */
        err = stats_get_counter_list(stats, cl);
        if (err != S_OK)
            return err;
    }

    /* save the sequence number */
    sample->sample_seq_no = cl->cl_seq_no;

    /* save the sample time */
    sample->sample_time = sample_time;

    /* save the sample data */
    for (i = 0; i < cl->cl_count; i++)
    {
        sample->sample_value[i] = cl->cl_ctr[i]->ctr_value;
    }

    sample->sample_count = cl->cl_count;

    return S_OK;
}

long long stats_sample_get_value(struct stats_sample *sample, int index)
{
    if (sample == NULL || index > sample->sample_count)
        return 0;
    return sample->sample_value[index].val64;
}


long long stats_sample_get_delta(struct stats_sample *sample, struct stats_sample *prev_sample, int index)
{
    if (sample == NULL || index > sample->sample_count || prev_sample == NULL || index > prev_sample->sample_count)
        return 0;
    return sample->sample_value[index].val64 - prev_sample->sample_value[index].val64;
}

/**
 * counter functions
 */

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
        __sync_fetch_and_add(&ctr->ctr_value.val64,1ll);
    }
}

long long counter_get_value(struct stats_counter *ctr)
{
    if (ctr != NULL)
    {
        return __sync_fetch_and_add(&ctr->ctr_value.val64,0);
    }
    else
    {
        return 0;
    }
}

void counter_increment_by(struct stats_counter *ctr, long long val)
{
    if (ctr != NULL)
    {
        __sync_fetch_and_add(&ctr->ctr_value.val64,val);
    }
}

void counter_clear(struct stats_counter *ctr)
{
    if (ctr != NULL)
    {
        __sync_lock_test_and_set(&ctr->ctr_value.val64,0ll);
    }
}

void counter_set(struct stats_counter *ctr, long long val)
{
    if (ctr != NULL)
    {
        __sync_lock_test_and_set(&ctr->ctr_value.val64,val);
    }
}
