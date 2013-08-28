/* histd_client.c */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <sys/socket.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>

#ifdef LINUX
size_t strlcpy(dst, src, siz)
    char *dst;
    const char *src;
    size_t siz;
{
    register char *d = dst;
    register const char *s = src;
    register size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
        do {
            if ((*d++ = *s++) == 0)
                break;
        } while (--n != 0);
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';      /* NUL-terminate dst */
        while (*s++)
            ;
    }

    return(s - src - 1);    /* count does not include NUL */
}
#endif

#include "histd/protocol.h"

uint64_t htonll(uint64_t ll)
{
    union {
        uint64_t ll;
        struct {
            uint32_t h;
            uint32_t l;
        } s;
    } u;

    u.ll = ll;
    u.s.h = htonl(u.s.h);
    u.s.l = htonl(u.s.l);
    return u.ll;
}

char * prepare_message(int *len_out, char *metric_name, int metric_value)
{
    char *buffer;
    int len;
    struct histd_proto_message_header *hdr;
    struct histd_proto_update_message *msg;
    struct histd_proto_metric_update *upd;

    printf("sizeof(histd_proto_message_header)=%ld\nsizeof(histd_proto_update_message)=%ld\nsizeof(histd_proto_metric_update)=%ld\n",
        sizeof(struct histd_proto_message_header), sizeof(struct histd_proto_update_message), sizeof(struct histd_proto_metric_update));

    len = sizeof(struct histd_proto_message_header) +  sizeof(struct histd_proto_update_message) + sizeof(struct histd_proto_metric_update);
    buffer = malloc(len);

    hdr = (struct histd_proto_message_header *)buffer;
    msg = (struct histd_proto_update_message *)(buffer + sizeof(struct histd_proto_message_header));
    upd = (struct histd_proto_metric_update *)(buffer + sizeof(struct histd_proto_message_header) + sizeof(struct histd_proto_update_message));

    hdr->message_type = htonl(HISTD_PROTO_UPDATE_MESSAGE_TYPE);
    hdr->message_length = htonl(sizeof(struct histd_proto_message_header) + sizeof(struct histd_proto_update_message));

    msg->timestamp = htonl(time(0));
    msg->metric_count = htonl(1);

    strlcpy(upd->metric_name, metric_name, HISTD_PROTO_MAX_METRIC_NAME_LEN);
    upd->metric_value = htonll(metric_value);

    *len_out = len;
    return buffer;
}

int send_message(struct in_addr inaddr, int port, char *metric_name, int metric_value)
{
    struct sockaddr_in addr;
    char *buffer;
    int len = 0, s;

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_addr = inaddr;
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    s = socket(PF_INET,SOCK_DGRAM,0);
    if (s == -1)
    {
        return 1;
    }

    buffer = prepare_message(&len, metric_name, metric_value);
    if (sendto(s, buffer, len, 0, (const struct sockaddr *)&addr, sizeof(struct sockaddr_in)) == 0)
    {
        printf("sendto failed\n");
        return 2;
    }

    free(buffer);

    return 0;
}


int main(int argc, char **argv)
{
    struct hostent * h;
    unsigned int i;
    int port = 7010;

    if (argc != 4)
    {
        printf("usage: histd_client addr metric_name metric_value\n");
        return 1;
    }

    h = gethostbyname(argv[1]);
    if (h == NULL)
    {
        printf("gethostbyname() failed\n");
    }
    else
    {
        printf("sending to %s\n", h->h_name);
        for (i = 0; h->h_addr_list[i] != NULL; i++)
        {
            printf( "trying %s\n", inet_ntoa(*(struct in_addr *)(h->h_addr_list[i])));
            if (send_message(*(struct in_addr *)(h->h_addr_list[i]),port,argv[2],atoi(argv[3])) == 0)
            {
                printf("ok\n");
                break;
            }
        }
    }

    return 0;
}
