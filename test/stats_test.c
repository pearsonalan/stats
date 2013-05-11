/* stats_test.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <assert.h>

#include "stats.h"
#include "error.h"
#include "debug.h"

const char * counter_names[] = {
    "backed",
    "cabal",
    "important",
    "bucket",
    "typical",
    "partial",
    "cabbage",
    "namely",
    "silliness",
    "gargoyle",
    "umpteenth",
    "abundant",
    "representative",
    "majestic",
    "pasta",
    "fill",
    "withstand",
    "basketball",
    "globe",
    "filet"
};

#define NCOUNTERNAMES (sizeof(counter_names) / sizeof(*counter_names))

void micro_sleep(int sec, int microsec)
{
    struct timespec ts;

    ts.tv_sec = sec;
    ts.tv_nsec = microsec * 1000;

    nanosleep(&ts, NULL);
}

struct stats *open_stats()
{
    struct stats *stats = NULL;
    int err;

    err = stats_create("stattest",&stats);
    if (err != S_OK)
    {
        printf("Failed to create stats: %s\n", error_message(err));
        return NULL;
    }

    err = stats_open(stats);
    if (err != S_OK)
    {
        printf("Failed to open stats: %s\n", error_message(err));
        stats_free(stats);
        return NULL;
    }

    return stats;
}


int writer(int n)
{
    struct stats *stats = NULL;
    int err = S_OK;
    struct stats_counter *ctrs[20];
    struct stats_counter *ctr = NULL;
    int i, j, nctr = 0;

    assert(NCOUNTERNAMES == 20);

    stats = open_stats();
    if (!stats)
    {
        return ERROR_FAIL;
    }

    for (i = 0; i < NCOUNTERNAMES * 10; i++)
    {
        micro_sleep(0,100000);

        if ((i % 10) == 0)
        {
            printf("writer %d: allocating counter %s\n", n, counter_names[nctr]);
            stats_allocate_counter(stats,counter_names[nctr],&ctr);
            ctrs[nctr++] = ctr;
        }

        for (j = 0; j < nctr; j++)
        {
            counter_increment(ctrs[j]);
        }
    }

    if (stats)
    {
        stats_close(stats);
        stats_free(stats);
    }

    return err;
}


int reader(int n)
{
    struct stats *stats = NULL;
    int err = S_OK;
    int i, j;
    int seq = 0, nseq;
    struct stats_counter **counters;
    int ncounters = 0;
    char counter_name[MAX_COUNTER_KEY_LENGTH+1];

    /* allocate an array of pointers to counter objects */
    counters = (struct stats_counter **) malloc(sizeof(struct stats_counter *) * COUNTER_TABLE_SIZE);
    if (!counters)
    {
        printf("failed to allocate memory\n");
        return ERROR_FAIL;
    }
    memset(counters, 0, sizeof(struct stats_counter *) * COUNTER_TABLE_SIZE);

    stats = open_stats();
    if (!stats)
    {
        return ERROR_FAIL;
    }

    for (i = 0; i < 25 && err == S_OK; i++)
    {
        micro_sleep(1,0);

        nseq = stats_get_sequence_number(stats);
        if (nseq != seq)
        {
            /* sequence number has changed. this means there might be new counter definitions.
               reload the counter list */
            err = stats_get_counters(stats, counters, COUNTER_TABLE_SIZE, &ncounters, &nseq);
            if (err != S_OK)
            {
                printf("reader %d: Error %08x getting counters %s\n",n,err,error_message(err));
            }
            seq = nseq;
        }

        printf("\n\n[[===============\n");
        for (j = 0; j <  ncounters; j++)
        {
            counter_get_key(counters[j],counter_name,MAX_COUNTER_KEY_LENGTH+1);
            printf("%s: %lld\n", counter_name, counters[j]->ctr_value.val64);
        }
        printf("===============]]\n\n");
    }

    if (stats)
    {
        stats_close(stats);
        stats_free(stats);
    }

    return err;
}


#define NWORKERS 3

int main(int argc, char **argv)
{
    int pids[NWORKERS];
    int i, n;

    for (i = 0; i < NWORKERS; i++)
    {
        printf("calling fork from %d\n", getpid());
        fflush(stdout);
        fflush(stderr);
        n = fork();
        if (n == -1)
        {
            printf("error: could not fork\n");
            return -1;
        }
        else if (n == 0)
        {
            printf("in child\n");
            if (i == 0)
                return reader(i);
            else
                return writer(i);
        }
        else
        {
            printf("Launched pid %d\n", n);
            pids[i] = n;
        }
    }

    printf("Waiting for child processes\n");

    while ((i = wait(&n)) != -1)
    {
        printf("wait returned %d, stat=%d, errno=%d\n", i, n, errno);
    }

    printf("wait returned %d, stat=%d, errno=%d\n", i, n, errno);

    return 0;
}



