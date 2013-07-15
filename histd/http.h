/* http.h */

#ifndef _HISTD_HTTP_H_INCLUDED_
#define _HISTD_HTTP_H_INCLUDED_

struct http {
    struct evhttp *http;
    struct evhttp_bound_socket *handle;

    /** http port */
    unsigned short port;

    /** metrics object */
    struct metrics *metrics;

    /** document root for static files */
    char *docroot;
};

int http_init(struct http* http, struct event_base *base, struct metrics *metrics);
int http_cleanup(struct http* http);
int http_free(struct http* http);

#endif
