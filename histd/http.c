#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include "histd.h"
#include "http.h"
#include "stats/error.h"

static int http_get_address(struct http* http, char *buffer, int buflen);

static const struct table_entry {
    const char *extension;
    const char *content_type;
} content_type_table[] = {
    { "txt", "text/plain" },
    { "html", "text/html" },
    { "htm", "text/htm" },
    { "css", "text/css" },
    { "gif", "image/gif" },
    { "jpg", "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "js", "application/javascript" },
    { "png", "image/png" },
    { "pdf", "application/pdf" },
    { "ps", "application/postsript" },
    { NULL, NULL },
};

/* Try to guess a good content-type for 'path' */
static const char *guess_content_type(const char *path)
{
    const char *last_period, *extension;
    const struct table_entry *ent;
    last_period = strrchr(path, '.');
    if (!last_period || strchr(last_period, '/'))
        goto not_found; /* no exension */
    extension = last_period + 1;
    for (ent = &content_type_table[0]; ent->extension; ++ent) {
        if (!evutil_ascii_strcasecmp(ent->extension, extension))
            return ent->content_type;
    }

not_found:
    return "application/octet-stream";
}

static void http_list_request_cb(struct evhttp_request *req, void *arg)
{
    // struct http *http = (struct http *)arg;
    // struct evbuffer *evb = NULL;

    printf("list request\n");


}

static struct metric_history * http_find_metrics_from_query(struct metrics *m, const char *query)
{
    struct metric_history *mh = NULL;
    struct evkeyvalq params;
    const char *metric_name;

    if (query)
    {
        memset(&params, 0, sizeof(params));

        if (evhttp_parse_query_str(query, &params) == 0)
        {
            metric_name = evhttp_find_header(&params,"series");
            if (metric_name != NULL)
            {
                printf("query is for %s\n", metric_name);
                mh = metrics_find_metric(m,metric_name);
            }
        }

        evhttp_clear_headers(&params);
    }

    return mh;
}


static const char * json_escape(const char *str)
{
    return str;
}

static int http_format_metric_error_response(struct evbuffer *evb, const char *error_message)
{
    evbuffer_add_printf(evb, "{\"error\":\"%s\"}",error_message);
    return S_OK;
}


#define SERIES_WRAP(p,len) (((p)+(len))%(len))

static int http_format_metric_response(struct evbuffer *evb, struct metric_history *mh)
{
    struct hist_file *h;
    struct sample *series = NULL;
    int series_index, series_len, series_head, start, i;

    h = mh->h;

    /* TODO: select which series to get based on the request time frame */
    series_index = 0;
    if (hist_file_get_series(h, series_index, &series, &series_len, &series_head) != S_OK)
    {
        http_format_metric_error_response(evb,"cannot load metric data");
    }
    else
    {
        evbuffer_add_printf(evb, "{\"metric\":\"%s\",", json_escape(mh->name));
        evbuffer_add_printf(evb, "\"results\":[");
        for (i = start = SERIES_WRAP(series_head-1,series_len); i != series_head; i = SERIES_WRAP(i-1,series_len))
        {
            evbuffer_add_printf(evb,"%s[%d,%lld]", ((i == start) ? "" : ","), series[i].sample_time, series[i].value);
        }
        evbuffer_add_printf(evb, "]}\n");
    }

    return S_OK;
}

static void http_metrics_request_cb(struct evhttp_request *req, void *arg)
{
    struct http *http = (struct http *)arg;
    struct evbuffer *evb = NULL;
    const char *uri = NULL;
    struct evhttp_uri *decoded = NULL;
    const char *query;
    struct metric_history *mh;

    uri = evhttp_request_get_uri(req);

    /* Decode the URI */
    decoded = evhttp_uri_parse(uri);
    if (!decoded)
    {
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        goto exit;
    }

    query = evhttp_uri_get_query(decoded);

    printf("metrics request %s\n", query ? query : "null");

    /* This holds the content we're sending. */
    evb = evbuffer_new();

    mh = http_find_metrics_from_query(http->metrics, query);
    if (!mh)
    {
        printf("Series not found in query: %s\n", query);
        http_format_metric_error_response(evb, "metric not found");
    }
    else
    {
        http_format_metric_response(evb,mh);
    }

    /* add headers */
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "application/json");

    /* send the response */
    evhttp_send_reply(req, 200, "OK", evb);

exit:
    if (decoded)
        evhttp_uri_free(decoded);

    if (evb)
        evbuffer_free(evb);
}

static void http_document_request_cb(struct evhttp_request *req, void *arg)
{
    struct evbuffer *evb = NULL;
    struct http *http = (struct http *) arg;
    const char *docroot = http->docroot;
    const char *uri = evhttp_request_get_uri(req);
    struct evhttp_uri *decoded = NULL;
    const char *path;
    char *decoded_path;
    char *whole_path = NULL;
    size_t len;
    int fd = -1;
    struct stat st;

    if (evhttp_request_get_command(req) != EVHTTP_REQ_GET)
    {
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    printf("GET: %s\n",  uri);

    /* Decode the URI */
    decoded = evhttp_uri_parse(uri);
    if (!decoded)
    {
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    /* Let's see what path the user asked for. */
    path = evhttp_uri_get_path(decoded);
    if (!path) path = "/";

    /* We need to decode it, to see what path the user really wanted. */
    decoded_path = evhttp_uridecode(path, 0, NULL);
    if (decoded_path == NULL)
        goto err;

    /* Don't allow any ".."s in the path, to avoid exposing stuff outside
     * of the docroot.  This test is both overzealous and underzealous:
     * it forbids aceptable paths like "/this/one..here", but it doesn't
     * do anything to prevent symlink following." */
    if (strstr(decoded_path, ".."))
        goto err;

    len = strlen(decoded_path) + strlen(docroot) + 2;
    if (!(whole_path = malloc(len)))
    {
        goto err;
    }

    snprintf(whole_path, len, "%s/%s", docroot, decoded_path);

    if (stat(whole_path, &st) < 0)
    {
        goto err;
    }

    /* This holds the content we're sending. */
    evb = evbuffer_new();

    if (S_ISDIR(st.st_mode))
    {
        /* TODO: send index.html if the request is for a directory */
        goto err;
    }
    else
    {
        /* Otherwise it's a file; add it to the buffer to get
         * sent via sendfile */
        const char *type = guess_content_type(decoded_path);
        if ((fd = open(whole_path, O_RDONLY)) < 0)
        {
            goto err;
        }

        if (fstat(fd, &st) < 0)
        {
            /* Make sure the length still matches, now that we
             * opened the file :/ */
            goto err;
        }

        evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", type);

        /* TODO: does this read the whole thing into memory??  well, we are only going to be
         * serving small files out of the static content directory, so its probably OK. */
        evbuffer_add_file(evb, fd, 0, st.st_size);
    }

    evhttp_send_reply(req, 200, "OK", evb);
    goto done;

err:
    evhttp_send_error(req, 404, "Document was not found");
    if (fd >= 0)
        close(fd);

done:
    if (decoded)
        evhttp_uri_free(decoded);

    if (decoded_path)
        free(decoded_path);

    if (whole_path)
        free(whole_path);

    if (evb)
        evbuffer_free(evb);
}

int http_init(struct http* http, struct event_base *base, struct metrics *metrics)
{
    char workdir[256];
    char docroot[256];
    char listen_addr[256];

    memset(http, 0, sizeof(struct http));

    http->port = 4000;
    http->metrics = metrics;

    getcwd(workdir,sizeof(workdir));
    snprintf(docroot, sizeof(docroot), "%s/docroot", workdir);

    http->docroot = strdup(docroot);

    /* Create a new evhttp object to handle requests. */
    http->http = evhttp_new(base);
    if (!http->http)
    {
        printf("could not create evhttp.\n");
        return ERROR_FAIL;
    }

    evhttp_set_cb(http->http, "/metrics", http_metrics_request_cb, http);
    evhttp_set_cb(http->http, "/list", http_list_request_cb, http);

    evhttp_set_gencb(http->http, http_document_request_cb, http);

    /* Now we tell the evhttp what port to listen on */
    http->handle = evhttp_bind_socket_with_handle(http->http, "0.0.0.0", http->port);
    if (!http->handle)
    {
        printf("couldn't bind to http port %d.\n", (int)http->port);
        return ERROR_FAIL;
    }

    if (http_get_address(http, listen_addr, sizeof(listen_addr)) == S_OK)
        printf("http: listening at %s\n", listen_addr);

    return S_OK;
}


static int http_get_address(struct http* http, char *buffer, int buflen)
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

    fd = evhttp_bound_socket_get_fd(http->handle);
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

int http_cleanup(struct http* http)
{
    if (!http)
        return ERROR_INVALID_PARAMETERS;

    if (http->docroot)
        free(http->docroot);

    if (http->http)
        evhttp_free(http->http);

    return S_OK;
}

int http_free(struct http* http)
{
    if (!http)
        return ERROR_INVALID_PARAMETERS;

    http_cleanup(http);
    free(http);

    return S_OK;
}
