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
    int j, err, n, maxy, col, ret, ch;
    struct timeval tv;
    long long start_time, sample_time, now;
    fd_set fds;

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

    start_time = current_time();

    while (!signal_received)
    {
        err = stats_get_sample(stats,cl,sample);
        if (err != S_OK)
        {
            printf("Error %08x (%s) getting sample\n",err,error_message(err));
        }

        clear();

        sample_time = TIME_DELTA_TO_NANOS(start_time, sample->sample_time);

        mvprintw(0,0,"SAMPLE @ %6lld.%03llds  SEQ:%d\n", sample_time / 1000000000ll, (sample->sample_time % 1000000000ll) / 1000000ll, sample->sample_seq_no);

        n = 1;
        maxy = getmaxy(stdscr);
        col = 0;
        for (j = 0; j < cl->cl_count; j++)
        {
            counter_get_key(cl->cl_ctr[j],counter_name,MAX_COUNTER_KEY_LENGTH+1);
            mvprintw(n,col+0,"%s", counter_name);
            mvprintw(n,col+29,"%15lld", stats_sample_get_value(sample,j));
            mvprintw(n,col+46,"%15lld", stats_sample_get_delta(sample,prev_sample,j));
            if (++n == maxy)
            {
                col += 66;
                n = 1;
            }
        }
        refresh();

        tmp = prev_sample;
        prev_sample = sample;
        sample = tmp;

        FD_ZERO(&fds);
        FD_SET(0,&fds);

        now = current_time();

        tv.tv_sec = 0;
        tv.tv_usec = 1000000 - (now % 1000000000) / 1000;

        ret = select(1, &fds, NULL, NULL, &tv);
        if (ret == 1)
        {
            ch = getch();
            if (ch == 'c' || ch == 'C')
            {
                stats_reset_counters(stats);
            }
        }
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
