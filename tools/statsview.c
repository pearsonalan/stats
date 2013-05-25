/* statsview.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <signal.h>
#include <time.h>

#include "stats.h"
#include "error.h"
#include "screenutil.h"

int signal_received = 0;

static unsigned long long msleep(unsigned long long millisec)
{
    struct timespec t = {0};
    t.tv_sec= millisec/1000;
    t.tv_nsec= (millisec%1000) * 1000000;
    if (nanosleep(&t,&t) != -1)
        return millisec;
    return millisec - ((unsigned long long)t.tv_sec*1000 + t.tv_nsec/1000000);
}

static void sigfunc(int sig_no)
{
    signal_received = 1;
}

static struct stats *open_stats(const char *name)
{
    struct stats *stats = NULL;
    int err;

    err = stats_create(name,&stats);
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

int main(int argc, char **argv)
{
    struct stats *stats = NULL;
    struct sigaction sa;
    struct stats_counter **counters;
    int seq = 0, nseq;
    int ncounters = 0;
    char counter_name[MAX_COUNTER_KEY_LENGTH+1];
    int j;
    int err;

    if (argc != 2)
    {
        printf("usage: statsview STATS\n");
        return -1;
    }

    /* allocate an array of pointers to counter objects */
    counters = (struct stats_counter **) malloc(sizeof(struct stats_counter *) * COUNTER_TABLE_SIZE);
    if (!counters)
    {
        printf("failed to allocate memory\n");
        return ERROR_FAIL;
    }
    memset(counters, 0, sizeof(struct stats_counter *) * COUNTER_TABLE_SIZE);

    stats = open_stats(argv[1]);
    if (!stats)
    {
        return ERROR_FAIL;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &sigfunc;
    sigaction(SIGINT, &sa, NULL);

    // init_screen();

    while (!signal_received)
    {

        nseq = stats_get_sequence_number(stats);
        if (nseq != seq)
        {
            /* sequence number has changed. this means there might be new counter definitions.
               reload the counter list */
            err = stats_get_counters(stats, counters, COUNTER_TABLE_SIZE, &ncounters, &nseq);
            if (err != S_OK)
            {
                printf("Error %08x getting counters %s\n",err,error_message(err));
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

        msleep(1000);
    }

    // close_screen();

    if (stats)
    {
        stats_close(stats);
        stats_free(stats);
    }

    if (signal_received)
        printf("Exiting on signal.\n");

    return signal_received;
}
