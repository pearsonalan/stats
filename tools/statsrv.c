/* statsrv.c */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include "stats/stats.h"
#include "stats/error.h"


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


static void sigint_cb(evutil_socket_t fd, short event, void *arg)
{
    struct event *signal = arg;
    printf("Terminating on SIGINT\n");
    event_base_loopbreak(event_get_base(signal));
}

static void http_request_cb(struct evhttp_request *req, void *arg)
{
    const char *uri = evhttp_request_get_uri(req);
    struct evbuffer *evb = NULL;
    const char *type;

    if (evhttp_request_get_command(req) != EVHTTP_REQ_GET)
    {
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    printf("GET: %s\n",  uri);

    if (strcmp(uri,"/") == 0)
    {
        type = "text/html";
        evb = evbuffer_new();
        evbuffer_add_printf(evb, "<html><body><h1>Hello World!</h1></body></html>");
        evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", type);
        evhttp_send_reply(req, 200, "OK", evb);
        printf(" - 200 - ok\n");
    }
    else
    {
        type = "text/plain";
        evb = evbuffer_new();
        evbuffer_add_printf(evb, "Not Found");
        evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", type);
        evhttp_send_reply(req, 404, "Not Found", evb);
        printf(" - 404 - not found\n");
    }

    if (evb)
        evbuffer_free(evb);
}

static int http_get_address(struct evhttp_bound_socket *handle, char *buffer, int buflen)
{
    /* Extract and display the address we're listening on. */
    struct sockaddr_storage ss;
    evutil_socket_t fd;
    ev_socklen_t socklen = sizeof(ss);
    char addrbuf[128];
    void *inaddr;
    const char *addr;
    int got_port = -1;

    memset(&ss, 0, sizeof(ss));

    fd = evhttp_bound_socket_get_fd(handle);
    if (getsockname(fd, (struct sockaddr *)&ss, &socklen))
    {
        return ERROR_FAIL;
    }

    if (ss.ss_family == AF_INET)
    {
        got_port = ntohs(((struct sockaddr_in*)&ss)->sin_port);
        inaddr = &((struct sockaddr_in*)&ss)->sin_addr;
    }
    else if (ss.ss_family == AF_INET6)
    {
        got_port = ntohs(((struct sockaddr_in6*)&ss)->sin6_port);
        inaddr = &((struct sockaddr_in6*)&ss)->sin6_addr;
    }
    else
    {
        fprintf(stderr, "Unexpected address family %d\n", ss.ss_family);
        return ERROR_FAIL;
    }

    addr = evutil_inet_ntop(ss.ss_family, inaddr, addrbuf, sizeof(addrbuf));
    if (addr)
    {
        evutil_snprintf(buffer, buflen, "http://%s:%d", addr,got_port);
    }
    else
    {
        fprintf(stderr, "evutil_inet_ntop failed\n");
        return ERROR_FAIL;
    }

    return S_OK;
}

int main(int argc, char **argv)
{
    struct event_base *base = NULL;
    struct stats *stats = NULL;
    struct stats_counter_list *cl = NULL;
    struct stats_sample *sample = NULL, *prev_sample = NULL;
    struct evhttp *http = NULL;
    struct event *signal_int;
    struct evhttp_bound_socket *handle;
    char listen_addr[256];

    if (argc != 2)
    {
        printf("usage: statsrv STATS\n");
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

    base = event_base_new();
    if (!base)
    {
        printf("Could not allocate event base\n");
        return 1;
    }

    /* add a handler for SIGINT */
    signal_int = evsignal_new(base, SIGINT, sigint_cb, event_self_cbarg());
    evsignal_add(signal_int,0);

    /* Create a new evhttp object to handle requests. */
    http = evhttp_new(base);
    if (!http)
    {
        printf("could not create evhttp.\n");
        return ERROR_FAIL;
    }

    evhttp_set_gencb(http, http_request_cb, http);

    /* Now we tell the evhttp what port to listen on */
    handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", 8080);
    if (!handle)
    {
        printf("couldn't bind to http port %d.\n", (int)8080);
        return ERROR_FAIL;
    }

    if (http_get_address(handle, listen_addr, sizeof(listen_addr)) == S_OK)
        printf("http: listening at %s\n", listen_addr);

    event_base_dispatch(base);

    event_free(signal_int);

#if 0
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
#endif

    if (base)
        event_base_free(base);

    if (http)
        evhttp_free(http);

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

    return 0;
}

