/* histd/protocol.h */

#ifndef _HISTD_PROTOCOL_H_INCLUDED_
#define _HISTD_PROTOCOL_H_INCLUDED_

#include <stdint.h>

#define HISTD_PROTO_MAX_METRIC_NAME_LEN         32


/* message types */
#define HISTD_PROTO_UPDATE_MESSAGE_TYPE         1


/* all integers are in network byte order */

struct histd_proto_message_header
{
    uint32_t message_type;
    uint32_t message_length;
};

struct histd_proto_metric_update
{
    char metric_name[HISTD_PROTO_MAX_METRIC_NAME_LEN];
    uint64_t metric_value;
};

struct histd_proto_update_message
{
    uint32_t timestamp;
    uint32_t metric_count;
    struct histd_proto_metric_update metrics[];
};

#endif
