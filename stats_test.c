/* stats_test.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#include "stats.h"
#include "error.h"

#define DPRINTF if (DEBUG) printf

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


int writer()
{
    struct stats *stats = NULL;
    int err = S_OK;

    stats = open_stats();
    if (!stats)
    {
        return ERROR_FAIL;
    }

    micro_sleep(2,0);

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

    stats = open_stats();
    if (!stats)
    {
        return ERROR_FAIL;
    }

    micro_sleep(2,0);

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
                return writer();
            else
                return reader(i);
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



