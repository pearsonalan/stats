#include <stdio.h>
#include "stats.h"
#include "error.h"

#define DPRINTF if (DEBUG) printf

int main(int argc, char **argv)
{
    struct stats *stats = NULL;
    int err;

    err = stats_create("stattest",&stats);
    if (err != S_OK)
    {
        printf("Failed to create stats: %s\n", error_message(err));
        goto exit;
    }

    err = stats_open(stats);
    if (err != S_OK)
    {
        printf("Failed to open stats: %s\n", error_message(err));
        goto exit;
    }


exit:

    if (stats)
    {
        stats_close(stats);
        stats_free(stats);
    }

    return err;
}


