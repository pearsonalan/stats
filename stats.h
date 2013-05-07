/* stats.h */

#ifndef _STATS_H_INCLUDED_
#define _STATS_H_INCLUDED_

#include "shared_mem.h"
#include "lock.h"

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
 * The stats_counter should be a multiple of 16 bytes to preserve
 * alignment. The current definition is 48 bytes.
 */
struct stats_counter
{
    int ctr_allocation_status;
    int ctr_flags;
    long long ctr_value;
    char ctr_name[32];
};

/* stats_data is the layout of the shared memory data.
 *
 * It contains a header followed by some number of counters.
 * The number of counters should be a prime number for better
 * hashing. Right now, we are using 2003, which is the
 * smallest prime number larger than 2000.
 */
struct stats_data
{
    struct stats_header     hdr;
    struct stats_counter    ctr[2003];
};


/* the in-memory stats object */
struct stats
{
    struct shared_memory shmem;
    struct lock lock;
    struct stats_data *data;
};


int stats_create(struct stats **stats_out);
int stats_open(struct stats *stats);

int stats_allocate_counter(struct stats *stats, char *name, int *key_out);
int stats_remove_counter(struct stats *stats, int key);

#endif
