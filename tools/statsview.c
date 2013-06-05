/* statsview.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

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
    struct stats_counter_list *cl = NULL;
    struct stats_sample *sample = NULL;
    struct sigaction sa;
    char counter_name[MAX_COUNTER_KEY_LENGTH+1];
    int j, err, n, maxy, col;
    struct timeval tv;

    if (argc != 2)
    {
        printf("usage: statsview STATS\n");
        return -1;
    }

    if (stats_cl_create(&cl) != S_OK)
    {
        printf("Failed to allocate stats counter list\n");
        return ERROR_FAIL;
    }

    stats = open_stats(argv[1]);
    if (!stats)
    {
        printf("Failed to open stats %s\n", argv[1]);
        return ERROR_FAIL;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &sigfunc;
    sigaction(SIGINT, &sa, NULL);

    init_screen();

    while (!signal_received)
    {
        gettimeofday(&tv,NULL);

        if (stats_cl_is_updated(stats,cl))
        {
            /* sequence number has changed. this means there might be new counter definitions.
               reload the counter list */
            err = stats_get_counter_list(stats, cl);
            if (err != S_OK)
            {
                printf("Error %08x getting counters %s\n",err,error_message(err));
            }
        }

        clear();

        mvprintw(0,0,"SAMPLE @ %d.%2d  SEQ:%d\n", tv.tv_sec, tv.tv_usec / 1000, cl->cl_seq_no);
        n = 2;
        maxy = getmaxy(stdscr);
        col = 0;
        for (j = 0; j < cl->cl_count; j++)
        {
            counter_get_key(cl->cl_ctr[j],counter_name,MAX_COUNTER_KEY_LENGTH+1);
            printf("%s: %lld\n", counter_name, cl->cl_ctr[j]->ctr_value.val64);

            mvprintw(n,col+0,"%s", counter_name);
            mvprintw(n,col+33,"%-7lld", cl->cl_ctr[j]->ctr_value.val64);
            if (++n == maxy)
            {
                col += 45;
                n = 2;
            }
        }
        refresh();

        msleep(1000);
    }

    close_screen();

    if (stats)
    {
        stats_close(stats);
        stats_free(stats);
    }

    if (cl)
        stats_cl_free(cl);

    if (sample)
        stats_sample_free(sample);
    
    if (signal_received)
        printf("Exiting on signal.\n");

    return signal_received;
}
