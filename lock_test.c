/* sem_test.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "error.h"
#include "lock.h"
#include <errno.h>

#define DPRINTF if (DEBUG) printf
#define NWORKERS 4
#define NTESTS   20

int run_test(int pidx)
{
    struct lock lock;
    int res;
    struct timespec ts;
    int i;
    int pid;

    srandomdev();

    pid = getpid();
    res = lock_init(&lock,"locktest");
    if (res != S_OK)
    {
        printf("Error creating lock: %s\n", error_message(res));
        return 1;
    }

    res = lock_open(&lock);
    if (res != S_OK)
    {
        printf("Error opening lock: %s\n", error_message(res));
        return 1;
    }


    for (i = 0; i < NTESTS; i++)
    {
        ts.tv_sec = 0;
        ts.tv_nsec = 200000000;

        printf("process %d (%d): waiting for lock\n", pidx, pid);
        fflush(stdout);
        lock_acquire(&lock);

        printf("process %d (%d): acquired lock\n", pidx, pid);
        nanosleep(&ts, NULL);

        printf("process %d (%d): releasing lock\n", pidx, pid);
        lock_release(&lock);

        ts.tv_sec = 0;
        ts.tv_nsec = 100000;

        nanosleep(&ts, NULL);
    }

    lock_close(&lock);

    return 0;
}

int main()
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
            return run_test(i);
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
