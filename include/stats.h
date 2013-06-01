/* stats.h */

#ifndef _STATS_H_INCLUDED_
#define _STATS_H_INCLUDED_

#include "shared_mem.h"
#include "lock.h"

#define STATS_MAGIC   'stat'

/* stats_header is the first 16 bytes of the stats shared memory
 *
 * The stats_header should be aligned to a 16-byte boundary to
 * preserve alignment of the stats counter data.
 */
struct stats_header
{
    int stats_magic;
    int stats_sequence_number;
    char reserved[8];
};

/* stats_counter is the data for each counter
 *
 * The stats_counter should be a multiple of 8 bytes to preserve
 * alignment. The current definition is 56 bytes.
 */

#define MAX_COUNTER_KEY_LENGTH 32

#define ALLOCATION_STATUS_FREE        0
#define ALLOCATION_STATUS_CLAIMED    -1   /* not used right now */
#define ALLOCATION_STATUS_ALLOCATED   1

struct stats_counter
{
    int ctr_allocation_status;
    int ctr_allocation_seq;
    union {
        long long val64;
        long      val32;
    } ctr_value;
    int ctr_flags;
    int ctr_key_len;
    char ctr_key[MAX_COUNTER_KEY_LENGTH];
};


/* stats_data is the layout of the shared memory data.
 *
 * It contains a header followed by some number of counters.
 * The number of counters should be a prime number for better
 * hashing. Right now, we are using 2003, which is the
 * smallest prime number larger than 2000.
 */

#define COUNTER_TABLE_SIZE 2003

struct stats_data
{
    struct stats_header     hdr;
    struct stats_counter    ctr[COUNTER_TABLE_SIZE];
};


/* struct stats
 *
 * the in-memory stats object
 */

struct stats
{
    int magic;
    struct shared_memory shmem;
    struct lock lock;
    struct stats_data *data;
};

int stats_create(const char *name, struct stats **stats_out);
int stats_open(struct stats *stats);
int stats_close(struct stats *stats);
int stats_free(struct stats *stats);

int stats_allocate_counter(struct stats *stats, const char *name, struct stats_counter **ctr_out);

/* deprecated. use stats_counter_list instead */
int stats_get_counters(struct stats *stats, struct stats_counter **counters, int counter_size, int *counter_out, int *sequence_number_out);



struct stats_counter_list
{
    int cl_seq_no;
    int cl_count;
    struct stats_counter *cl_ctr[COUNTER_TABLE_SIZE];
};

int stats_get_counter_list(struct stats *stats, struct stats_counter_list *cl);
int stats_cl_is_updated(struct stats *stats, struct stats_counter_list *cl);




void counter_get_key(struct stats_counter *ctr, char *buf, int buflen);
void counter_increment(struct stats_counter *ctr);

#define stats_get_sequence_number(s) ((s)->data->hdr.stats_sequence_number)

#endif
