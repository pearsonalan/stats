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


static struct stats *open_stats(const char * name)
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


/******************************************************************
 *
 *  Ruby Stats object
 *
 */


static void rbstats_free(void *p)
{
    struct rbstats *s;

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
};

static VALUE rbtmr_alloc(struct stats_counter *counter)
{
    VALUE tdata;
    struct timer_data *td;

    td = (struct timer_data *)malloc(sizeof(struct timer_data));
    td->counter = counter;
    td->start_time = current_time();

    tdata = Data_Wrap_Struct(tmr_class, 0, rbtmr_free, td);

    return tdata;
}

VALUE rbtmr_enter(VALUE self)
{
    struct timer_data *td;

    Data_Get_Struct(self, struct timer_data, td);
    td->start_time = current_time();

    return self;
}

VALUE rbtmr_exit(VALUE self)
{
    struct timer_data *td;

    Data_Get_Struct(self, struct timer_data, td);
    counter_increment_by(td->counter,current_time() - td->start_time);
    return self;
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
}
