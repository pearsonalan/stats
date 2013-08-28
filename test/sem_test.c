/* sem_test.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#include "stats/error.h"
#include "stats/stats.h"
#include "stats/semaphore.h"

#define DPRINTF  if (DEBUG) printf
#define NWORKERS 2
#define NTESTS   20

int run_test(int pidx)
{
    struct semaphore * sem = NULL;
    int res;
    struct timespec ts;
    int i;
    int pid;

#ifdef DARWIN
    srandomdev();
#else
    srand(time(0));
#endif

    pid = getpid();
    res = semaphore_create("semtest", 1, &sem);
    if (res != S_OK)
    {
        printf("Error creating semaphore: %s\n", error_message(res));
        return 1;
    }

    res = semaphore_open_and_set(sem, 1);
    if (res != S_OK)
    {
        printf("Error opening semaphore: %s\n", error_message(res));
        return 1;
    }

    for (i = 0; i < NTESTS; i++)
    {
        ts.tv_sec = 0;
        ts.tv_nsec = 200000000;

        printf("process %d (%d): waiting for semaphore\n", pidx, pid);
        fflush(stdout);
        semaphore_P(sem,0);

        printf("process %d (%d): acquired semaphore\n", pidx, pid);
        nanosleep(&ts, NULL);

        printf("process %d (%d): releasing semaphore\n", pidx, pid);
        semaphore_V(sem,0);

        ts.tv_sec = 0;
        ts.tv_nsec = 100000;

        nanosleep(&ts, NULL);
    }

    if (sem != NULL)
    {
        semaphore_close(sem,0);
        semaphore_free(sem);
    }

    return 0;
}

int main()
{
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
