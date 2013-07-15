/* histd.h */

#ifndef _HISTD_H_INCLUDED_
#define _HISTD_H_INCLUDED_

#include "stats/stats.h"

#define MAX_METRICS 3001
#define MAX_PATH_LEN 120

struct sample
{
    int32_t     sample_time;
    int64_t     value;
};


#define HIST_FILE_MAGIC         'hist'
#define CURRENT_FILE_VERSION    0x00000001

#define NSERIES 5

/* a sample every second for the past 15 minutes (900 samples) */
#define SERIES_0_SIZE (15 * 60)

/* a sample every 10 seconds for the past 120 minutes (720 samples) */
#define SERIES_1_SIZE (120 * 60 / 10)

/* a sample every minute for the past 24 hours (1440 samples) */
#define SERIES_2_SIZE (24 * 60)

/* a sample every 10 minutes for the past 7 days (1008 samples) */
#define SERIES_3_SIZE (7 * 24 * 60 / 10)

/* a sample every hour for 30 days (720 samples) */
#define SERIES_4_SIZE (30 * 24)

struct hist_file_header
{
    int magic;
    int file_version;
    int nseries;
    int reserved;
};

/*
 * the layout of the history file is:
 *
 * struct hist_file_header      hdr
 * int                          series_length[N]
 * int                          series_head[N]
 * struct sample                series0[series_length[0]]
 * struct sample                series1[series_length[1]]
 * ...
 * struct sample                series(N-1)[series_length[N-1]]
 *
 * where N is the value in hdr.nseries
 */

struct hist_file
{
    struct hist_file_header     hdr;
};

int hist_file_validate(struct hist_file *h);
int *hist_series_lengths(struct hist_file *h);
int *hist_series_heads(struct hist_file *h);
int hist_file_size(struct hist_file *h);
int hist_file_get_series(struct hist_file *h, int index, struct sample **series_out, int *series_len_out, int *series_head_out);


struct metric_history
{
    char name[MAX_COUNTER_KEY_LENGTH];

    int fd;

    struct hist_file *h;
};

int metric_history_create(const char *name, struct metric_history **mh_out);
int metric_history_open(struct metric_history *mh, char *metric_dir);
int metric_history_close(struct metric_history *mh);
int metric_history_free(struct metric_history *mh);
int metric_history_add_sample(struct metric_history *mh, uint32_t timestamp, uint64_t metric_value);



struct metrics
{
    struct metric_history *metrics[MAX_METRICS];
};

int metrics_alloc(struct metrics **m_out);
int metrics_free(struct metrics *m);
int metrics_process_update_message(struct metrics *m, char *buffer, int msglen);
struct metric_history * metrics_find_metric(struct metrics *m, const char * metric_name);

#endif

