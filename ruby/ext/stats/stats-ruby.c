#define RSTRING_NOT_MODIFIED
#include <ruby.h>
#include <stdio.h>
#include <assert.h>

#include "stats/error.h"
#include "stats/stats.h"
#include "stats/hash.h"

static VALUE stats_class = Qnil;
static VALUE ctr_class = Qnil;
static VALUE tmr_class = Qnil;

#define STATS_MAGIC  'stat'
#define CTR_MAGIC    'ctr '

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


static VALUE rbctr_alloc(struct stats_counter *counter);
static void rbctr_free(void *p);

static VALUE rbtmr_alloc(struct stats_counter *counter);
static void rbtmr_free(void *p);


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

/* this simple table keeps track of stats objects so that multiple calls to
 *   Stats.new('foo')
 * will return a rbstats object which wraps the same base stats object.
 */

struct open_stats_objects {
    char name[STATS_MAX_NAME_LEN + 1];
    int open_count;
    struct stats *stats;
};

#define OPEN_STATS_OBJECTS 8

struct open_stats_objects open_stats_object_table[OPEN_STATS_OBJECTS];
int open_stats_object_table_initialized = 0;


static struct stats *open_stats(const char * name)
{
    struct stats *stats = NULL;
    int err;
    int i;

    if (strlen(name) > STATS_MAX_NAME_LEN)
    {
        printf("Stats name is too long\n");
        return NULL;
    }

    if (open_stats_object_table_initialized == 0)
    {
        memset(open_stats_object_table, 0, sizeof(open_stats_object_table));
        open_stats_object_table_initialized = 1;
    }

    for (i = 0; i < OPEN_STATS_OBJECTS; i++ )
    {
        if (*open_stats_object_table[i].name == 0)
            break;
        if (strcmp(name,open_stats_object_table[i].name) == 0)
        {
            if (open_stats_object_table[i].stats == NULL)
            {
                /* there is an entry at location i, but it is not open */
                break;
            }
            else
            {
                open_stats_object_table[i].open_count++;
                return open_stats_object_table[i].stats;
            }
        }
    }

    /* see if the table is all full */
    if (i == OPEN_STATS_OBJECTS)
    {
        printf("Failed to create stats: stats table is full\n");
        return NULL;
    }

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

    strcpy(open_stats_object_table[i].name, name);
    open_stats_object_table[i].open_count++;
    open_stats_object_table[i].stats = stats;

    return stats;
}


/******************************************************************
 *
 *  Ruby Stats object
 *
 */


static void rbstats_free(void *p)
{
    struct rbstats *s;
    int i;

    s = (struct rbstats *)p;
    if (s && s->stats)
    {
        /* look for the object in the open table and decrement the open count */
        for (i = 0; i < OPEN_STATS_OBJECTS; i++ )
        {
            if (open_stats_object_table[i].stats == s->stats)
            {
                open_stats_object_table[i].open_count--;
                if (open_stats_object_table[i].open_count == 0)
                {
                    open_stats_object_table[i].stats = NULL;
                    stats_close(s->stats);
                    stats_free(s->stats);
                }
            }
        }
    }

    free(p);
}

VALUE rbstats_new(VALUE class, VALUE name)
{
    struct rbstats *stats;
    VALUE tdata;

    Check_Type(name, T_STRING);

    stats = (struct rbstats *)malloc(sizeof(struct rbstats));
    memset(stats,0,sizeof(struct rbstats));
    stats->magic = STATS_MAGIC;

    tdata = Data_Wrap_Struct(class, 0, rbstats_free, stats);
    rb_obj_call_init(tdata, 1, &name);

    return tdata;
}

static VALUE rbstats_init(VALUE self, VALUE name)
{
    struct rbstats *stats;

    Check_Type(name, T_STRING);
    Data_Get_Struct(self, struct rbstats, stats);

    stats->stats = open_stats(RSTRING_PTR(name));

    return self;
}


static struct rbstats *rbstats_get_wrapped_stats(VALUE self)
{
    struct rbstats *stats;

    Data_Get_Struct(self, struct rbstats, stats);
    assert(stats != NULL);
    assert(stats->magic == STATS_MAGIC);

    return stats;
}

static struct stats_counter *rbstats_get_counter(struct rbstats *stats, VALUE rbkey)
{
    char *key;
    int idx, keylen;
    struct stats_counter *counter = NULL;

    Check_Type(rbkey, T_STRING);

    key = RSTRING_PTR(rbkey);
    keylen = RSTRING_LEN(rbkey);

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
    }
    else
    {
        /* table is full, can't create a new counter */
    }

    return counter;
}

static VALUE rbstats_get(VALUE self, VALUE rbkey)
{
    struct rbstats *stats;
    struct stats_counter *counter;
    VALUE ret = Qnil;

    stats = rbstats_get_wrapped_stats(self);
    if (stats)
    {
        counter = rbstats_get_counter(stats, rbkey);
        if (counter)
        {
            ret = rbctr_alloc(counter);
        }
    }

    return ret;
}


static VALUE rbstats_get_tmr(VALUE self, VALUE rbkey)
{
    struct rbstats *stats;
    struct stats_counter *counter;
    VALUE ret = Qnil;

    stats = rbstats_get_wrapped_stats(self);
    if (stats)
    {
        counter = rbstats_get_counter(stats, rbkey);
        if (counter)
        {
            ret = rbtmr_alloc(counter);
        }
    }

    return ret;
}

static VALUE rbstats_inc(VALUE self, VALUE rbkey)
{
    struct rbstats *stats;
    struct stats_counter *counter;
    VALUE ret = Qnil;

    stats = rbstats_get_wrapped_stats(self);
    if (stats)
    {
        counter = rbstats_get_counter(stats, rbkey);
        if (counter)
        {
            counter_increment(counter);
            ret = Qtrue;
        }
    }

    return ret;
}



static VALUE rbstats_add(VALUE self, VALUE rbkey, VALUE amt)
{
    struct rbstats *stats;
    struct stats_counter *counter;
    VALUE ret = Qnil;

    Check_Type(amt,T_FIXNUM);

    stats = rbstats_get_wrapped_stats(self);
    if (stats)
    {
        counter = rbstats_get_counter(stats, rbkey);
        if (counter)
        {
            counter_increment_by(counter,FIX2LONG(amt));
            ret = Qtrue;
        }
    }

    return ret;
}

static VALUE rbstats_set(VALUE self, VALUE rbkey, VALUE amt)
{
    struct rbstats *stats;
    struct stats_counter *counter;
    VALUE ret = Qnil;

    Check_Type(amt,T_FIXNUM);

    stats = rbstats_get_wrapped_stats(self);
    if (stats)
    {
        counter = rbstats_get_counter(stats, rbkey);
        if (counter)
        {
            counter_set(counter,FIX2LONG(amt));
            ret = Qtrue;
        }
    }

    return ret;
}

static VALUE rbstats_clr(VALUE self, VALUE rbkey)
{
    struct rbstats *stats;
    struct stats_counter *counter;
    VALUE ret = Qnil;

    stats = rbstats_get_wrapped_stats(self);
    if (stats)
    {
        counter = rbstats_get_counter(stats, rbkey);
        if (counter)
        {
            counter_clear(counter);
            ret = Qtrue;
        }
    }

    return ret;
}


/******************************************************************
 *
 *  Ruby Counter
 *
 */

static VALUE rbctr_alloc(struct stats_counter *counter)
{
    VALUE tdata;

    tdata = Data_Wrap_Struct(ctr_class, 0, rbctr_free, counter);

    return tdata;
}

VALUE rbctr_inc(VALUE self)
{
    struct stats_counter *counter = NULL;

    Data_Get_Struct(self, struct stats_counter, counter);
    counter_increment(counter);
    return self;
}

VALUE rbctr_add(VALUE self, VALUE amt)
{
    struct stats_counter *counter = NULL;

    Check_Type(amt,T_FIXNUM);

    Data_Get_Struct(self, struct stats_counter, counter);
    counter_increment_by(counter,FIX2LONG(amt));
    return self;
}

VALUE rbctr_clr(VALUE self)
{
    struct stats_counter *counter = NULL;

    Data_Get_Struct(self, struct stats_counter, counter);
    counter_clear(counter);
    return self;
}

VALUE rbctr_set(VALUE self, VALUE amt)
{
    struct stats_counter *counter = NULL;

    Check_Type(amt,T_FIXNUM);

    Data_Get_Struct(self, struct stats_counter, counter);
    counter_set(counter,FIX2LONG(amt));
    return self;
}

static void rbctr_free(void *p)
{
}




/******************************************************************
 *
 *  Ruby Timer
 *
 */

struct timer_data
{
    struct stats_counter *counter;
    long long start_time;
    int depth;
};

static VALUE rbtmr_alloc(struct stats_counter *counter)
{
    VALUE tdata;
    struct timer_data *td;

    td = (struct timer_data *)malloc(sizeof(struct timer_data));
    td->counter = counter;
    td->start_time = 0;
    td->depth = 0;

    tdata = Data_Wrap_Struct(tmr_class, 0, rbtmr_free, td);

    return tdata;
}

VALUE rbtmr_enter(VALUE self)
{
    struct timer_data *td;

    Data_Get_Struct(self, struct timer_data, td);
    if (td->start_time == 0)
        td->start_time = current_time();
    td->depth++;

    return self;
}

VALUE rbtmr_exit(VALUE self)
{
    struct timer_data *td;

    Data_Get_Struct(self, struct timer_data, td);
    if (td->depth > 0)
        td->depth--;
    if (td->start_time > 0 && td->depth == 0)
    {
        counter_increment_by(td->counter,TIME_DELTA_TO_NANOS(td->start_time,current_time()) / 1000ll);
        td->start_time = 0;
    }
    return self;
}

VALUE rbtmr_time(VALUE self)
{
    struct timer_data *td;
    long long start_time;
    VALUE ret;

    Data_Get_Struct(self, struct timer_data, td);
    start_time = current_time();
    ret = rb_yield(self);
    counter_increment_by(td->counter,TIME_DELTA_TO_NANOS(start_time,current_time()) / 1000ll);
    return ret;
}


static void rbtmr_free(void *p)
{
    free(p);
}

void Init_stats()
{
    stats_class = rb_define_class("Stats", rb_cObject);
    rb_define_singleton_method(stats_class, "new", rbstats_new, 1);
    rb_define_method(stats_class, "initialize", rbstats_init, 1);
    rb_define_method(stats_class, "get", rbstats_get, 1);
    rb_define_method(stats_class, "timer", rbstats_get_tmr, 1);
    rb_define_method(stats_class, "inc", rbstats_inc, 1);
    rb_define_method(stats_class, "add", rbstats_add, 2);
    rb_define_method(stats_class, "set", rbstats_set, 2);
    rb_define_method(stats_class, "clr", rbstats_clr, 1);

    ctr_class = rb_define_class("Counter", rb_cObject);
    rb_define_method(ctr_class, "inc", rbctr_inc, 0);
    rb_define_method(ctr_class, "add", rbctr_add, 1);
    rb_define_method(ctr_class, "set", rbctr_set, 1);
    rb_define_method(ctr_class, "clr", rbctr_clr, 0);

    tmr_class = rb_define_class("Timer", rb_cObject);
    rb_define_method(tmr_class, "enter", rbtmr_enter, 0);
    rb_define_method(tmr_class, "exit", rbtmr_exit, 0);
    rb_define_method(tmr_class, "time", rbtmr_time, 0);
}

