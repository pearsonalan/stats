/* stats.h */

#ifndef _STATS_H_INCLUDED_
#define _STATS_H_INCLUDED_

#include "shared_mem.h"
#include "lock.h"

#ifdef LINUX
size_t strlcat(char *dst, const char *src, size_t siz);
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

long long current_time();


#define STATS_MAGIC   'stat'


typedef union {
    long long val64;
    struct {
        int    hi;
        int    lo;
    } val32;
} STATS_VALUE;


/* stats_header is the first 16 bytes of the stats shared memory
 *
 * The stats_header should be aligned to a 16-byte boundary to
 * preserve alignment of the stats counter data.
 *
 * stats_magic is the magic number STATS_MAGIC from above.
 * stats_sequence_number is a value which starts at 0 and is incremented
 *      each time a new counter is allocated.
 * reserved is there to align the structure to a multipe of 16 bytes.
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
 *
 * ctr_allocation_status is a flag indicating if the counter is
 *      in use or not.
 * ctr_allocation_seq is an ordinal indicationg the order which
 *      the counter was allocated.  When you get a counter list
 *      the pointers will be sorted according to this ordinal so
 *      every list is in the same order, with new counters sorted
 *      to the end.
 * ctr_value is the value of the counter (either 64 or 32 bits)
 * ctr_flags indicates the type of counter (timer, gauge) and the
 *      size of the counter (64 or 32 bits)
 * ctr_key_len indicates the number of characters in ctr_key which
 *      make up the counter name
 * ctr_key contains the name of the counter. Note: this string may
 *      not be NUL terminated.  If ctr_key_len == MAX_COUNTER_KEY_LENGTH
 *      then there will NOT be a NUL char at the end of the string.
 *      The safest thing to do is to use counter_get_key() to get
 *      the name, which will return a NUL terminated string.
 */

#define MAX_COUNTER_KEY_LENGTH 32

/* flags for the ctr_allocation_status field */
#define ALLOCATION_STATUS_FREE        0
#define ALLOCATION_STATUS_CLAIMED    -1   /* not used right now */
#define ALLOCATION_STATUS_ALLOCATED   1


/* flags for the ctr_flags field */
#define CTR_FLAG_32BIT          0x00000000
#define CTR_FLAG_64BIT          0x00000001

#define CTR_FLAG_TIMER          0x00000010
#define CTR_FLAG_GAUGE          0x00000020

struct stats_counter
{
    int ctr_allocation_status;
    int ctr_allocation_seq;
    STATS_VALUE ctr_value;
    int ctr_flags;
    int ctr_key_len;
    char ctr_key[MAX_COUNTER_KEY_LENGTH];
};


/* stats_data is the layout of the shared memory data.
 *
 * It contains a header followed by a fixed size hash table
 * containing the counters.
 *
 * The size of the hash table should be a prime number for better
 * hashing. Right now, we are using 2003, which is the smallest
 * prime number larger than 2000.  This is a compile-time setting
 * and can be changed to any other prime number.
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


/**
 * stats_counter_list
 *
 * stats_counter_list contains a snapshot of the set of counters in the shared
 * memory at a point in time.
 *
 * cl_seq_no - represents the point in time which this counter list represents
 *      this value is copied from the stats.stats_sequence_number at the time
 *      the counter list is captured.
 * cl_count - the number of counters in cl_ctr
 * cl_ctr - a contiguous array of pointers to stats_counter objects
 *     from [0,cl_count-1]
 */

struct stats_counter_list
{
    int cl_seq_no;
    int cl_count;
    struct stats_counter *cl_ctr[COUNTER_TABLE_SIZE];
};

int stats_get_counter_list(struct stats *stats, struct stats_counter_list *cl);
int stats_cl_create(struct stats_counter_list **cl_out);
void stats_cl_init(struct stats_counter_list *cl);
void stats_cl_free(struct stats_counter_list *cl);
int stats_cl_is_updated(struct stats *stats, struct stats_counter_list *cl);


/**
 * stats_sample
 */

struct stats_sample
{
    int sample_seq_no;
    int sample_count;
    long long sample_time;
    STATS_VALUE sample_value[COUNTER_TABLE_SIZE];
};

int stats_sample_create(struct stats_sample **sample_out);
void stats_sample_init(struct stats_sample *sample);
void stats_sample_free(struct stats_sample *sample);
long long stats_sample_get_value(struct stats_sample *sample, int index);
long long stats_sample_get_delta(struct stats_sample *sample, struct stats_sample *prev_sample, int index);

int stats_get_sample(struct stats *stats, struct stats_counter_list *cl, struct stats_sample *sample);



void counter_get_key(struct stats_counter *ctr, char *buf, int buflen);
void counter_increment(struct stats_counter *ctr);
void counter_increment_by(struct stats_counter *ctr, long long val);

#define stats_get_sequence_number(s) ((s)->data->hdr.stats_sequence_number)

#endif
