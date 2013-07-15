#define RSTRING_NOT_MODIFIED
#include <ruby.h>
#include <stdio.h>
#include <assert.h>

#include "stats/error.h"
#include "stats/stats.h"
#include "stats/hash.h"

static VALUE stats_class;

#define STATS_MAGIC  'stat'

struct rbstats_counter_entry
{
    char key[MAX_COUNTER_KEY_LENGTH+1];
    struct stats_counter * ctr;
};

#define TABLE_SIZE 2003

struct rbstats
{
    int magic;

    struct stats * stats;
    struct rbstats_counter_entry tbl[TABLE_SIZE];
};



struct stats *open_stats(const char * name)
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

static void rbstats_free(void *p)
{
    struct rbstats *s;

    printf("In rbstats_free\n");

    s = (struct rbstats *)p;
    if (s && s->stats)
    {
        stats_close(s->stats);
        stats_free(s->stats);
    }

    free(p);
}

VALUE rbstats_new(VALUE class, VALUE name)
{
    struct rbstats *stats;
    VALUE tdata;

    printf("in rbstats_new\n");

    Check_Type(name, T_STRING);

    stats = (struct rbstats *)malloc(sizeof(struct rbstats));
    memset(stats,0,sizeof(struct rbstats));
    stats->magic = STATS_MAGIC;

    printf("rbstats = %016lx, magic = %08x\n", (intptr_t)stats, stats->magic);
    tdata = Data_Wrap_Struct(class, 0, rbstats_free, stats);
    rb_obj_call_init(tdata, 1, &name);

    fflush(stdout);
    return tdata;
}

static VALUE rbstats_init(VALUE self, VALUE name)
{
    struct rbstats *stats;
    VALUE ret = Qnil;

    printf("in stats_init\n");

    Check_Type(name, T_STRING);
    Data_Get_Struct(self, struct rbstats, stats);

    stats->stats = open_stats(RSTRING_PTR(name));
    if (stats->stats != NULL)
    {
        ret = self;
    }

    return self;
}


static int hash_probe(struct rbstats *stats, const char *key, long len)
{
    uint32_t h, k, n;
    int i;

    h = fast_hash(key,(int)len);
    k = h % TABLE_SIZE;

    if (stats->tbl[k].key[0] == '\0')
    {
        strcpy(stats->tbl[k].key,key);
        return k;
    }
    else if (strcmp(stats->tbl[k].key,key) == 0)
    {
        return k;
    }

    n = 1;
    for (i =0; i < 32; i++)
    {
        k = (h + n) % TABLE_SIZE;
        if (stats->tbl[k].key[0] == '\0')
        {
            strcpy(stats->tbl[k].key,key);
            return k;
        }
        else if (strcmp(stats->tbl[k].key,key) == 0)
        {
            return k;
        }
        n = n * 2;
    }

    return -1;
}


static VALUE rbstats_inc(VALUE self, VALUE rbkey)
{
    struct rbstats *stats;
    char *key;
    int idx, keylen;
    struct stats_counter *counter = NULL;
    VALUE ret = Qnil;

    Check_Type(rbkey, T_STRING);

    key = RSTRING_PTR(rbkey);
    keylen = RSTRING_LEN(rbkey);

    Data_Get_Struct(self, struct rbstats, stats);
    assert(stats->magic == STATS_MAGIC);

    idx = hash_probe(stats,key,keylen);
    if (idx != -1)
    {
        counter = stats->tbl[idx].ctr;
        if (counter == NULL)
        {
            if (stats_allocate_counter(stats->stats,key,&counter) == S_OK)
            {
                stats->tbl[idx].ctr = counter;
            }
            else
            {
                /* failed to allocate counter */
            }
        }

        if (counter != NULL)
        {
            counter_increment(counter);
            ret = Qtrue;
        }
    }
    else
    {
        /* table is full, can't create a new counter */
    }

    return ret;
}


void Init_stats()
{
    printf("In Init_stats\n");
    stats_class = rb_define_class("Stats", rb_cObject);
    rb_define_singleton_method(stats_class, "new", rbstats_new, 1);
    rb_define_method(stats_class, "initialize", rbstats_init, 1);
    rb_define_method(stats_class, "inc", rbstats_inc, 1);
}