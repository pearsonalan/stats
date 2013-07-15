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

#include "histd.h"
#include "http.h"
#include "stats/error.h"


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

static void http_data_request_cb(struct evhttp_request *req, void *arg)
{
    // struct http *http = (struct http *)arg;
    struct evbuffer *evb = NULL;
    const char *uri = NULL;
    struct evhttp_uri *decoded = NULL;
    const char *query;

    uri = evhttp_request_get_uri(req);

    /* Decode the URI */
    decoded = evhttp_uri_parse(uri);
    if (!decoded)
    {
        printf("It's not a good URI. Sending BADREQUEST\n");
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    query = evhttp_uri_get_query(decoded);

    printf("Process %d: data request %s\n", getpid(), query ? query : "null");

    /* This holds the content we're sending. */
    evb = evbuffer_new();

    /* generate the response */
    evbuffer_add_printf(evb, "{\"results\":[");
    evbuffer_add_printf(evb, "]}\n");

    /* add headers */
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "application/json");

    /* send the response */
    evhttp_send_reply(req, 200, "OK", evb);

    if (decoded)
        evhttp_uri_free(decoded);

    if (evb)
        evbuffer_free(evb);
}

static void send_document_cb(struct evhttp_request *req, void *arg)
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

    printf("Got a GET request for <%s>\n",  uri);

    /* Decode the URI */
    decoded = evhttp_uri_parse(uri);
    if (!decoded)
    {
        printf("It's not a good URI. Sending BADREQUEST\n");
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
        perror("malloc");
        goto err;
    }

    evutil_snprintf(whole_path, len, "%s/%s", docroot, decoded_path);

    if (stat(whole_path, &st) < 0)
    {
        goto err;
    }

    /* This holds the content we're sending. */
    evb = evbuffer_new();

    if (S_ISDIR(st.st_mode))
    {
        goto err;
    }
    else
    {
        /* Otherwise it's a file; add it to the buffer to get
         * sent via sendfile */
        const char *type = guess_content_type(decoded_path);
        if ((fd = open(whole_path, O_RDONLY)) < 0)
        {
            perror("open");
            goto err;
        }

        if (fstat(fd, &st) < 0)
        {
            /* Make sure the length still matches, now that we
             * opened the file :/ */
            perror("fstat");
            goto err;
        }

        evhttp_add_header(evhttp_request_get_output_headers(req),
            "Content-Type", type);

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

    evhttp_set_cb(http->http, "/data", http_data_request_cb, http);

    evhttp_set_gencb(http->http, send_document_cb, http);

    /* Now we tell the evhttp what port to listen on */
    http->handle = evhttp_bind_socket_with_handle(http->http, "0.0.0.0", http->port);
    if (!http->handle)
    {
        printf("couldn't bind to http port %d.\n", (int)http->port);
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
