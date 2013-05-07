#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "error.h"
#include "stats.h"
#include "shared_mem.h"
#include "semaphore.h"
#include <libkern/OSAtomic.h>

#define DPRINTF if (DEBUG) printf

int basic_test()
{
    int err;
    struct shared_memory * shmem = NULL;
    int nattach;
    time_t t;
    char buf[32];

    printf("stats test\n");

    err = shared_memory_create("test", OMODE_OPEN_OR_CREATE | DESTROY_ON_CLOSE_IF_LAST, 1024*16, &shmem);
    if (err != S_OK)
    {
        printf("Error %s (%08x) creating shared memory\n", error_message(err), err);
        return 1;
    }

    err = shared_memory_open(shmem);
    if (err != S_OK)
    {
        printf("Error %s (%08x) opening shared memory\n", error_message(err), err);
        return 1;
    }

    printf("Shared memory %s %s created\n", shared_memory_name(shmem), shared_memory_was_created(shmem) ? "was" : "was not");

    if (shared_memory_was_created(shmem))
    {
        memset(shared_memory_ptr(shmem), 0, shared_memory_size(shmem));
    }
    else
    {
        printf("Shared memory contents: %s\n", shared_memory_ptr(shmem));
    }

    err = shared_memory_nattach(shmem, &nattach);
    if (err != S_OK)
    {
        printf("Error %s (%08x) getting shared memory stats\n", error_message(err), err);
        return 1;
    }

    printf("Shared memory has %d attaches\n", nattach);

    t = time(0);
    sprintf(shared_memory_ptr(shmem), "This shared memory was created by process %d at %s", getpid(), ctime_r(&t,buf) );
    shared_memory_close(shmem);

    return 0;
}

struct data {
    int valid;
    int index;
    int value;
    char text[20];
};

#define MEMSIZE 2000

int read_test(int dur)
{
    int err;
    struct shared_memory * shmem = NULL;
    int i, n;
    struct data *ptr;
    struct data d;
    time_t start, end;
    char buf[20];

    printf("beginning read test\n");

    srandomdev();
    err = shared_memory_create("test", OMODE_OPEN_OR_CREATE | DESTROY_ON_CLOSE_IF_LAST, MEMSIZE*sizeof(struct data), &shmem);
    if (err != S_OK)
    {
        printf("Error %s (%08x) creating shared memory\n", error_message(err), err);
        return 1;
    }

    err = shared_memory_open(shmem);
    if (err != S_OK)
    {
        printf("Error %s (%08x) opening shared memory\n", error_message(err), err);
        return 1;
    }

    ptr = (struct data *) shared_memory_ptr(shmem);

    time(&start);

    i = 0;
    for (;;)
    {
        for (n = 0; n < MEMSIZE; n++)
        {
            if (ptr[n].valid == 1)
            {
                memcpy(&d,ptr+n,sizeof(struct data));
                if (ptr[n].valid == 1)
                {
                    if (d.index != n)
                    {
                        printf("Invalid index discovered!\n");
                    }
                    else
                    {
                        snprintf(buf, 20, "idx:%d val:%d", d.index, d.value);
                        if (memcmp(buf,d.text,strlen(buf)+1) != 0)
                        {
                            printf("Buffer comparison falied. idx=%d, val=%d buf='%s', data='%s'\n", d.index, d.value, buf, d.text);
                        }
                    }
                }
                else
                {
                    DPRINTF("data is no longer valid!\n");
                }
            }
        }

        if (++i % 1000 == 0)
        {
            time(&end);
            if (end - start >= dur)
                break;
        }
    }

    shared_memory_close(shmem);

    printf("ending read test\n");

    return 0;
}

int write_test(int dur)
{
    int err;
    struct shared_memory * shmem = NULL;
    int i, index, v, success;
    struct data *ptr;
    struct data *dptr;
    time_t start, end;

    printf("beginning write test\n");

    srandomdev();
    err = shared_memory_create("test", OMODE_OPEN_OR_CREATE | DESTROY_ON_CLOSE_IF_LAST, MEMSIZE*sizeof(struct data), &shmem);
    if (err != S_OK)
    {
        printf("Error %s (%08x) creating shared memory\n", error_message(err), err);
        return 1;
    }

    err = shared_memory_open(shmem);
    if (err != S_OK)
    {
        printf("Error %s (%08x) opening shared memory\n", error_message(err), err);
        return 1;
    }

    ptr = (struct data *) shared_memory_ptr(shmem);

    time(&start);

    i = 0;
    for (;;)
    {
        /* loop until we either successfully set or clear an element */
        success = FALSE;
        while (!success)
        {
            index = random() % MEMSIZE;
            dptr = ptr + index;

            v = dptr->valid;
            if (v == 0)
            {
                if (OSAtomicCompareAndSwap32(v,2,&dptr->valid))
                {
                    dptr->index = index;
                    dptr->value = random() % 1000;
                    snprintf(dptr->text, 20, "idx:%d val:%d", index, dptr->value);
                    dptr->valid = 1;
                    success = TRUE;
                }
                else
                {
                    DPRINTF("writer: item is no longer UNSET while attempting to SET\n");
                }
            }

            if (v == 1)
            {
                if (OSAtomicCompareAndSwap32(v,0,&dptr->valid))
                {
                    success = TRUE;
                }
                else
                {
                    DPRINTF("writer: item is no longer SET while attempting to SET\n");
                }
            }
        }


        if (++i % 1000 == 0)
        {
            time(&end);
            if (end - start >= dur)
                break;
        }
    }

    shared_memory_close(shmem);

    printf("ending write test\n");

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1],"basic") == 0)
        return basic_test();

    if (strcmp(argv[1],"read") == 0)
        return read_test(60);

    if (strcmp(argv[1],"write") == 0)
        return write_test(60);

    return basic_test();
}


