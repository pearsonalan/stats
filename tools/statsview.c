/* statsview.c */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <curses.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#include "stats/stats.h"
#include "stats/error.h"
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
    struct stats_sample *sample = NULL, *prev_sample = NULL, *tmp = NULL;
    struct sigaction sa;
    char counter_name[MAX_COUNTER_KEY_LENGTH+1];
    int j, err, n, maxy, col;

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

    if (stats_sample_create(&sample) != S_OK)
    {
        printf("Failed to allocate stats sample\n");
        return ERROR_FAIL;
    }

    if (stats_sample_create(&prev_sample) != S_OK)
    {
        printf("Failed to allocate stats sample\n");
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
        err = stats_get_sample(stats,cl,sample);
        if (err != S_OK)
        {
            printf("Error %08x (%s) getting sample\n",err,error_message(err));
        }

        clear();

        mvprintw(0,0,"SAMPLE @ %d.%2d  SEQ:%d\n", sample->sample_time / 1000, sample->sample_time % 1000, sample->sample_seq_no);
        mvprintw(1,0,"sample = %016x, prev = %016x, scount = %d, pcount = %d",
                 (intptr_t)sample,(intptr_t)prev_sample, sample->sample_count, prev_sample->sample_count);

        n = 2;
        maxy = getmaxy(stdscr);
        col = 0;
        for (j = 0; j < cl->cl_count; j++)
        {
            counter_get_key(cl->cl_ctr[j],counter_name,MAX_COUNTER_KEY_LENGTH+1);
            mvprintw(n,col+0,"%s", counter_name);
            mvprintw(n,col+33,"%-7lld", stats_sample_get_value(sample,j));
            mvprintw(n,col+41,"%-7lld", stats_sample_get_delta(sample,prev_sample,j));
            if (++n == maxy)
            {
                col += 52;
                n = 2;
            }
        }
        refresh();

        tmp = prev_sample;
        prev_sample = sample;
        sample = tmp;

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

    if (prev_sample)
        stats_sample_free(prev_sample);

    if (signal_received)
        printf("Exiting on signal.\n");

    return signal_received;
}
