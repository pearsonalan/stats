/* keystats.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#include "stats/stats.h"
#include "stats/error.h"
#include "screenutil.h"

#define MIN(a,b) ((a)<(b)?(a):(b))

int signal_received = 0;

static void sigfunc(int sig_no)
{
    signal_received = 1;
}

struct stats *open_stats()
{
    struct stats *stats = NULL;
    int err;

    err = stats_create("keystats",&stats);
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
    struct stats_counter *vowel_counter, *char_counter;
    struct stats_counter *key_counters[26], *key_counter;
    char counter_name[12];
    int rows, cols, c, n=0;
    struct sigaction sa;

    stats = open_stats();
    if (!stats)
    {
        return ERROR_FAIL;
    }

    stats_allocate_counter(stats,"vowel.char",&vowel_counter);
    stats_allocate_counter(stats,"total.char",&char_counter);

    memset(key_counters, 0, sizeof(key_counters));
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &sigfunc;
    sigaction(SIGINT, &sa, NULL);

    init_screen();
    getmaxyx(stdscr,rows,cols);
    (void) cols;

    while (!signal_received && (c = wgetch(stdscr)) != ERR)
    {
        /* exit on DEL or ESC */
        if (c == 0x7F || c == 0x1B)
            break;

        key_counter = NULL;

        if (c >= 'a' && c <= 'z')
        {
            key_counter = key_counters[c-'a'];
            if (!key_counter)
            {
                sprintf(counter_name,"%c.char",c);
                stats_allocate_counter(stats,counter_name,&key_counter);
                key_counters[c-'a'] = key_counter;
            }
        }

        if (c >= 'A' && c <= 'Z')
        {
            key_counter = key_counters[c-'A'];
            if (!key_counter)
            {
                sprintf(counter_name,"%c.char",c-'A'+'a');
                stats_allocate_counter(stats,counter_name,&key_counter);
                key_counters[c-'A'+'a'] = key_counter;
            }
        }

        if (key_counter)
            counter_increment(key_counter);

        if (c == 'a' || c == 'A' || c == 'e' || c == 'E' || c == 'i' || c == 'I' || c == 'o' || c == 'O' || c == 'u' || c == 'U')
            counter_increment(vowel_counter);


        counter_increment(char_counter);

#if 0
        metrics::ScopeTimer timer(printTimeCounter);
#endif
        mvprintw(MIN(n,rows-1),0,"0x%x (%c)", c, isprint(c) ? c : ' ');
        n++;
        if (n >= rows)
        {
            scroll(stdscr);
        }
        mvprintw(MIN(n,rows-1),0,"");
        refresh();
    }

    close_screen();

    if (stats)
    {
        stats_close(stats);
        stats_free(stats);
    }

    return 0;
}
