#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/http.h>

#include "histd.h"
#include "http.h"
#include "histd/protocol.h"
#include "stats/hash.h"
#include "stats/error.h"

#define METRICS_DIR "/mnt/tmp"

uint64_t ntohll(uint64_t ll)
{
    union {
        uint64_t ll;
        struct {
            uint32_t h;
            uint32_t l;
        } s;
    } u;

    u.ll = ll;
    u.s.h = ntohl(u.s.h);
    u.s.l = ntohl(u.s.l);
    return u.ll;
}

struct udp_listener
{
    /** event handle */
    struct event *event;

    /** udp socket */
    int sock;

    /** server endpoint */
    struct sockaddr_in addr;

    /** udp port */
    unsigned short port;

    /** metrics object */
    struct metrics *metrics;
};


int udp_listener_init(struct udp_listener* l, struct event_base *base, struct metrics *metrics);
int udp_listener_cleanup(struct udp_listener* l);
void udp_read_callback(int fd, short flags, void *arg);
int udp_set_socket_options(int sock);


/***
 * hist_file
 */

int *hist_series_lengths(struct hist_file *h)
{
    return (int *)((char *)h + sizeof(struct hist_file_header));
}

int *hist_series_heads(struct hist_file *h)
{
    return (int *)((char *)h + sizeof(struct hist_file_header) + h->hdr.nseries * sizeof(int));
}

int hist_file_size(struct hist_file *h)
{
    return sizeof(struct hist_file_header);
}

int hist_file_new_size()
{
    return sizeof(struct hist_file_header) + NSERIES*sizeof(int)*2 + (SERIES_0_SIZE + SERIES_1_SIZE + SERIES_2_SIZE + SERIES_3_SIZE + SERIES_4_SIZE)*sizeof(struct sample);
}

int hist_file_get_series(struct hist_file *h, int index, struct sample **series_out, int *series_len_out, int *series_head_out)
{
    char *p = (char *) h;
    int *lengths = hist_series_lengths(h);
    int *heads = hist_series_heads(h);
    int i;

    assert(h != NULL);
    assert(index <= h->hdr.nseries);

    p += sizeof(struct hist_file_header);
    p += h->hdr.nseries * 2 * sizeof(int);

    for (i = 0; i < index; i++)
    {
        p += lengths[i] * sizeof(struct sample);
    }

    *series_out = (struct sample *)p;
    *series_len_out = lengths[index];
    *series_head_out = heads[index];

    return S_OK;
}

void hist_file_advance_head(struct hist_file *h, int index)
{
    int *lengths = hist_series_lengths(h);
    int *heads = hist_series_heads(h);

    heads[index] = (heads[index] + 1) % lengths[index];
}


int hist_file_validate(struct hist_file *h)
{
    /* TODO: implement file validation */
    return S_OK;
}


/***
 * metrics_history
 */

int metric_history_create(const char *name, struct metric_history **mh_out)
{
    struct metric_history * mh = NULL;
    int err = S_OK;

    if (mh_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (strlen(name) >= MAX_COUNTER_KEY_LENGTH)
        return ERROR_INVALID_PARAMETERS;

    mh = (struct metric_history *) malloc(sizeof(struct metric_history));
    if (mh == NULL)
    {
        err = ERROR_MEMORY;
        goto exit;
    }

    strcpy(mh->name, name);
    mh->fd = -1;
    mh->h = NULL;

    *mh_out = mh;

exit:
    return err;
}

int metric_history_init_file(int fd)
{
    struct hist_file_header hdr;
    int lengths[NSERIES] = {
        SERIES_0_SIZE,
        SERIES_1_SIZE,
        SERIES_2_SIZE,
        SERIES_3_SIZE,
        SERIES_4_SIZE
    };
    int heads[NSERIES] = {0};
    struct sample s = {0,0};
    int i;
    int n = SERIES_0_SIZE + SERIES_1_SIZE + SERIES_2_SIZE + SERIES_3_SIZE + SERIES_4_SIZE;

    hdr.magic = HIST_FILE_MAGIC;
    hdr.file_version = CURRENT_FILE_VERSION;
    hdr.nseries = NSERIES;
    hdr.reserved = 0;

    if (write(fd,&hdr,sizeof(struct hist_file_header)) != sizeof(struct hist_file_header))
    {
        printf("Failed to write header to history file\n");
        return ERROR_FAIL;
    }

    if (write(fd,lengths,NSERIES*sizeof(int)) != NSERIES*sizeof(int))
    {
        printf("Failed to write sizes to history file\n");
        return ERROR_FAIL;
    }

    if (write(fd,heads,NSERIES*sizeof(int)) != NSERIES*sizeof(int))
    {
        printf("Failed to write heads to history file\n");
        return ERROR_FAIL;
    }

    for (i = 0; i < n; i++)
    {
        if (write(fd,&s,sizeof(struct sample)) != sizeof(struct sample))
        {
            printf("Failed to write sample to history file\n");
            return ERROR_FAIL;
        }
    }

    return S_OK;
}

int metric_history_open(struct metric_history *mh, char * path)
{
    char filename[MAX_PATH_LEN + MAX_COUNTER_KEY_LENGTH + 4];
    struct stat s;
    int fd = -1;
    void *pv = NULL;
    int err = ERROR_FAIL;
    int map_size, new_file;

    if (mh == NULL)
        return ERROR_INVALID_PARAMETERS;
    if (path == NULL)
        return ERROR_INVALID_PARAMETERS;
    if (strlen(path) >= MAX_PATH_LEN)
        return ERROR_INVALID_PARAMETERS;

    sprintf(filename, "%s/%s.mhf", path, mh->name);
    printf("opening metric history file %s\n", filename);
    fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1)
    {
        printf("cannot open mapping file\n");
        goto exit;
    }
    printf("ok\n");

    fstat(fd,&s);

    if (s.st_size == 0)
    {
        /* need to initialize the file */
        printf("Initializing new history file\n");

        /* TODO: possibly use flock to guarantee that two processes are not initializing the file at the same time */
        metric_history_init_file(fd);

        map_size = hist_file_new_size();
        new_file = 1;
    }
    else
    {
        /* TODO: possibly use a shared lock here to make sure we don't use the file while it is being initialized by another process */
        map_size = s.st_size;
        new_file = 0;
    }

    printf("mapping metric history file (%d bytes) ...\n", map_size);
    pv = mmap(NULL,map_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if (pv == NULL)
    {
        printf("cannot map\n");
        goto exit;
    }
    printf("ok. mapped at %016lx\n",(intptr_t)pv);

    /* check magic, validate sizes, etc */
    if (hist_file_validate((struct hist_file *) pv) != S_OK)
    {
        printf("failed to validate history file\n");
        goto exit;
    }

    mh->h = (struct hist_file *) pv;
    mh->fd = fd;

    err = S_OK;

exit:

    if (err != S_OK)
    {
        if (pv != NULL)
        {
            munmap(pv, sizeof(struct hist_file));
        }

        if (fd != -1)
        {
            close(fd);
        }
    }

    return err;
}

int metric_history_close(struct metric_history *mh)
{
    if (!mh)
        return ERROR_INVALID_PARAMETERS;

    if (mh->h)
    {
        munmap((void *)mh->h, sizeof(struct hist_file));
    }

    if (mh->fd)
    {
        close(mh->fd);
        mh->fd = -1;
    }

    return S_OK;
}

int metric_history_free(struct metric_history *mh)
{
    if (!mh)
        return ERROR_INVALID_PARAMETERS;

    assert(mh->fd == -1);
    assert(mh->h == NULL);

    free(mh);

    return S_OK;
}

int metric_history_add_sample(struct metric_history *mh, uint32_t timestamp, uint64_t metric_value)
{
    struct hist_file *h;
    struct sample *series;
    int i, n, series_len, series_head, last_sample_index, last_sample_time;

    if (!mh)
        return ERROR_INVALID_PARAMETERS;

    if (!mh->h)
        return ERROR_INVALID_PARAMETERS;

    h = mh->h;

    hist_file_get_series(h, 0, &series, &series_len, &series_head);

    printf("Adding sample at position %d. Series @0x%016lx, len %d\n", series_head, (intptr_t)series, series_len);

    last_sample_index = (series_head + series_len - 1) % series_len;
    last_sample_time = series[last_sample_index].sample_time;
    printf("last_sample_index = %d, timestamp of last sample is %d\n", last_sample_index, last_sample_time);

    if (timestamp > last_sample_time)
    {
        printf("timestamp of new sample is greater than any existing timestamp in the file\n");
        if (last_sample_time == 0)
        {
            printf("Adding first sample to the file\n");

            printf("*** WRITING TIMESTAMP %d at position %d\n", timestamp, series_head);
            series[series_head].sample_time = timestamp;
            series[series_head].value = metric_value;
            hist_file_advance_head(h,0);
        }
        else if (last_sample_time + 1 == timestamp)
        {
            printf("Inserting sample immediately after last sample\n");
            printf("*** WRITING TIMESTAMP %d at position %d\n", timestamp, series_head);
            series[series_head].sample_time = timestamp;
            series[series_head].value = metric_value;
            hist_file_advance_head(h,0);
        }
        else
        {
            n = timestamp - last_sample_time - 1;

            printf("Missing %d samples between %d and %d.\n", n, last_sample_time, timestamp);

            for (i = 0; i < n; i++)
            {
                assert(hist_series_heads(h)[0] == (series_head + i) % series_len);
                printf("*** WRITING TIMESTAMP %d at position %d\n", last_sample_time + i + 1, (series_head + i) % series_len);
                series[(series_head + i) % series_len].sample_time = last_sample_time + i + 1;
                series[(series_head + i) % series_len].value = 0;
                hist_file_advance_head(h,0);
            }

            assert(hist_series_heads(h)[0] == (series_head + n) % series_len);
            printf("*** WRITING TIMESTAMP %d at position %d\n", timestamp, (series_head + n) % series_len);
            series[(series_head + n) % series_len].sample_time = timestamp;
            series[(series_head + n) % series_len].value = metric_value;
            hist_file_advance_head(h,0);
        }
    }
    else
    {
        printf("Updating old data\n");
    }

    return S_OK;
}



/***
 * metrics
 */

int metrics_alloc(struct metrics **m_out)
{
    struct metrics *m;

    m = (struct metrics *) malloc(sizeof(struct metrics));
    if (!m)
    {
        return ERROR_MEMORY;
    }

    memset(m, 0, sizeof(struct metrics));
    *m_out = m;

    return S_OK;
}

int metrics_free(struct metrics *m)
{
    if (m)
    {
        free(m);
    }

    return S_OK;
}

static int metrics_hash_probe(struct metrics *m, const char *key, long len)
{
    uint32_t h, k, n;
    int i;

    h = fast_hash(key,(int)len);
    k = h % MAX_METRICS;

    if (m->metrics[k] == NULL)
    {
        return k;
    }
    else if (strcmp(m->metrics[k]->name,key) == 0)
    {
        return k;
    }

    n = 1;
    for (i =0; i < 32; i++)
    {
        k = (h + n) % MAX_METRICS;
        if (m->metrics[k] == NULL)
        {
            return k;
        }
        else if (strcmp(m->metrics[k]->name,key) == 0)
        {
            return k;
        }
        n = n * 2;
    }

    return -1;
}

struct metric_history * metrics_find_metric(struct metrics *m, const char * metric_name)
{
    int k;
    struct metric_history *mh;

    k = metrics_hash_probe(m,metric_name,strlen(metric_name));
    mh = m->metrics[k];

    return mh;
}

static int metrics_store_metric(struct metrics *m, uint32_t timestamp, char *metric_name, uint64_t metric_value)
{
    int k;
    struct metric_history *mh;

    printf("  %s: %llu\n", metric_name, metric_value);

    k = metrics_hash_probe(m,metric_name,strlen(metric_name));
    printf("    key=%d\n",k);

    if ((mh = m->metrics[k]) == NULL)
    {
        printf("    allocating new metric\n");
        if (metric_history_create(metric_name, &mh) != S_OK)
        {
            printf("    ERROR allocating metric_history\n");
        }
        else
        {
            if (metric_history_open(mh, METRICS_DIR) != S_OK)
            {
                printf("    ERROR opening metric_history\n");
                metric_history_free(mh);
                mh = NULL;
            }
            else
            {
                m->metrics[k] = mh;
            }
        }
    }
    else
    {
        printf("    metric already exists\n");
    }

    assert(mh != NULL);

    metric_history_add_sample(mh, timestamp, metric_value);
    return S_OK;
}

int metrics_process_update_message(struct metrics *m, char *buffer, int msglen)
{
    struct histd_proto_message_header *hdr;
    struct histd_proto_update_message *msg;
    int i;

    hdr = (struct histd_proto_message_header *)buffer;
    msg = (struct histd_proto_update_message *)(buffer + sizeof(struct histd_proto_message_header));

    msg->timestamp = ntohl(msg->timestamp);
    msg->metric_count = ntohl(msg->metric_count);

    printf("ts=%u, mcount=%u\n", msg->timestamp, msg->metric_count);

    for (i = 0; i < msg->metric_count; i++)
    {
        metrics_store_metric(m, msg->timestamp, msg->metrics[i].metric_name, ntohll(msg->metrics[i].metric_value));
    }

    return S_OK;
}

int metrics_process_message(struct metrics *m, char *buffer, int msglen)
{
    struct histd_proto_message_header *hdr;

    hdr = (struct histd_proto_message_header *)buffer;

    hdr->message_type = ntohl(hdr->message_type);
    hdr->message_length = ntohl(hdr->message_length);

    printf("metrics: msg type %d, len %d\n", hdr->message_type, hdr->message_length);

    switch (hdr->message_type)
    {
    case HISTD_PROTO_UPDATE_MESSAGE_TYPE:
        printf("processing message type HISTD_PROTO_UPDATE_MESSAGE_TYPE\n");
        metrics_process_update_message(m,buffer,msglen);
        break;
    default:
        printf("unhandled message type %d", hdr->message_type);
        break;
    }
    return S_OK;
}



/**
 * udp listener
 */

int udp_listener_init(struct udp_listener* udp, struct event_base *base, struct metrics *metrics)
{
    memset(udp, 0, sizeof(struct udp_listener));

    udp->port = 7010;
    udp->metrics = metrics;

    if ((udp->sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        printf("ERROR - unable to create socket\n");
        return -1;
    }

    if (udp_set_socket_options(udp->sock) != 0)
    {
        return -1;
    }

    memset(&udp->addr, 0, sizeof(struct sockaddr_in));
    udp->addr.sin_addr.s_addr = INADDR_ANY;
    udp->addr.sin_port = htons(udp->port);
    udp->addr.sin_family = AF_INET;

    if (bind(udp->sock, (struct sockaddr *)&udp->addr, sizeof(struct sockaddr_in)) != 0)
    {
        printf("Error: Unable to bind the default IP \n");
        return -1;
    }

    udp->event = event_new(base, udp->sock, EV_READ | EV_PERSIST, udp_read_callback, udp);
    if (!udp->event)
    {
        printf("Could not allocate event\n");
        return 1;
    }

    printf("udp: listening at 0.0.0.0:%d\n", udp->port);

    return S_OK;
}

int udp_listener_cleanup(struct udp_listener* udp)
{
    if (udp->event)
    {
        /* TODO: free event */
    }

    if (udp->sock != -1)
    {
        close(udp->sock);
    }

    return S_OK;
}

void udp_read_callback(int fd, short flags, void *arg)
{
    unsigned int addr_len;
    struct sockaddr_in addr;
    int n = 0;
    char buffer[2048];
    char time_buf[32];
    char addr_buf[32];
    struct tm tm;
    struct timeval tv;
    struct udp_listener* udp;

    udp = (struct udp_listener*) arg;

    addr_len = sizeof(addr);

    n = recvfrom(fd, buffer, 2048, 0, (struct sockaddr *)&addr, &addr_len);
    if (n == -1)
    {
        printf("recvfrom: error %d.\n", errno);
    }
    else if (n == 0)
    {
        printf("recvfrom: shutdown signalled. error %d.\n", errno);
    }
    else if (n > 0)
    {
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec,&tm);

        sprintf(time_buf, "%02d/%02d %02d:%02d:%02d.%03d :", tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));
        inet_ntop(AF_INET, &(addr.sin_addr), addr_buf,sizeof(addr_buf));

        printf("%s: %-15s: %d bytes\n", time_buf, addr_buf, n);

        metrics_process_message(udp->metrics, buffer, n);
    }
}

int udp_set_socket_options(int sock)
{
    int opt = 1;
    int flags;

    flags = fcntl(sock, F_GETFL, 0);
    if (flags< 0)
    {
        printf("Cannot get socket flags. error %d\n", errno);
        return -1;
    }

    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        printf("cannot set socket flags. error %d\n", errno);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt)))
    {
        printf("cannot set socket options. error %d\n", errno);
        return -1;
    }

    return 0;
}



int main(int argc, char **argv)
{
    struct event_base *base;
    struct udp_listener udp;
    struct http http;
    struct metrics *metrics = NULL;

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return (1);

    base = event_base_new();
    if (!base)
    {
        printf("Could not allocate event base\n");
        return 1;
    }

    if (metrics_alloc(&metrics) != S_OK)
    {
        return 2;
    }

    if (udp_listener_init(&udp, base, metrics) != S_OK)
    {
        return 2;
    }

    if (http_init(&http, base, metrics) != S_OK)
    {
        return 2;
    }

    event_add(udp.event, NULL);
    event_base_dispatch(base);

    udp_listener_cleanup(&udp);
    metrics_free(metrics);

    return 0;
}
