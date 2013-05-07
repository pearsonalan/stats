#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "error.h"
#include "shared_mem.h"

int shared_memory_create(const char *name, int flags, int size, struct shared_memory **shmem_out )
{
    struct shared_memory * shmem;

    if (shmem_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (strlen(name) >= SHARED_MEMORY_MAX_NAME_LEN)
        return ERROR_SHARED_MEM_NAME_TOO_LONG;

    shmem = malloc(sizeof(struct shared_memory));
    if (!shmem)
        return 0;

    memset(shmem, 0, sizeof(struct shared_memory));

    shmem->magic = SHARED_MEMORY_MAGIC;
    shmem->shmid = -1;
    shmem->flags = flags;
    shmem->size = size;
    strcpy(shmem->name, name);

    *shmem_out = shmem;

    return S_OK;
}


int shared_memory_open(struct shared_memory * shmem)
{
    struct stat s;
    char path[MAX_PATH];
    int mode;
    struct shmid_ds ds;
    int omode;

    if (shmem == NULL || shmem->magic != SHARED_MEMORY_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    /* extract open mode from flags */
    omode = shmem->flags & OMODE_MASK;

    /* check for existance of the directory */
    if (stat(SHARED_MEMORY_DIRECTORY,&s) != 0)
    {
        /* try to create the directory */
        if (mkdir(SHARED_MEMORY_DIRECTORY, 0755) != 0)
            return ERROR_SHARED_MEM_CANNOT_CREATE_DIRECTORY;
    }
    else
    {
        if ((s.st_mode & S_IFDIR) != S_IFDIR)
            return ERROR_SHARED_MEM_PATH_NOT_DIRECTORY;
    }

    sprintf(path, "%s/%s", SHARED_MEMORY_DIRECTORY, shmem->name);

    if (access(path, W_OK) == -1)
    {
        int fd = creat(path, 0644 );
        if (fd == -1)
            return ERROR_SHARED_MEM_CANNOT_CREATE_PATH;
        close(fd);
    }

    shmem->shmkey = ftok(path, 1);
    if (shmem->shmkey == -1)
    {
        return ERROR_SHARED_MEM_CANNOT_CREATE_IPC_TOKEN;
    }

    switch (omode)
    {
    case OMODE_CREATE:
        mode = IPC_CREAT | IPC_EXCL | 0644;
        break;
    case OMODE_OPEN_OR_CREATE:
        mode = IPC_CREAT | 0644;
        break;
    case OMODE_OPEN_EXISTING:
    default:
        mode = 0644;
        break;
    }

    printf("Opening shmkey %08x for %s (absolute path: %s), mode 0%o, size %d\n", shmem->shmkey, shmem->name, path, mode, shmem->size);

    shmem->shmid = shmget(shmem->shmkey, shmem->size, mode) ;
    if (shmem->shmid == -1)
    {
        if (errno == EEXIST && omode == OMODE_CREATE)
            return ERROR_SHARED_MEM_ALREADY_EXISTS;
        if (errno == ENOENT && omode == OMODE_OPEN_EXISTING)
            return ERROR_SHARED_MEM_DOES_NOT_EXIST;
        if (errno == EINVAL)
            return ERROR_SHARED_MEM_INVALID_SIZE;
        return ERROR_SHARED_MEM_CANNOT_OPEN;
    }

    switch (omode)
    {
    case OMODE_CREATE:
        shmem->created = TRUE;
        break;
    case OMODE_OPEN_OR_CREATE:
        if (shmctl(shmem->shmid, IPC_STAT, &ds) != 0)
            return ERROR_SHARED_MEM_CANNOT_STAT;
        if (ds.shm_cpid == getpid())
            shmem->created = TRUE;
        else
            shmem->created = FALSE;
        break;
    case OMODE_OPEN_EXISTING:
        shmem->created = FALSE;
        break;
    }

    shmem->ptr = shmat(shmem->shmid, NULL, 0);
    if ((intptr_t)shmem->ptr == -1)
    {
        shmem->ptr = NULL;
        return ERROR_SHARED_MEM_CANNOT_ATTACH;
    }

    printf("Successfylly openend shm 0x%08x for %s.  Attached at 0x%016lx\n", shmem->shmkey, shmem->name, (intptr_t) shmem->ptr);

    return S_OK;
}


int shared_memory_nattach(struct shared_memory *shmem, int *attach_count_out)
{
    struct shmid_ds ds;

    if (!shmem || shmem->magic != SHARED_MEMORY_MAGIC || shmem->shmid == -1)
        return ERROR_INVALID_PARAMETERS;

    if (shmctl(shmem->shmid, IPC_STAT, &ds) != 0)
        return ERROR_SHARED_MEM_CANNOT_STAT;

    *attach_count_out = ds.shm_nattch;
    return S_OK;
}


int shared_memory_close(struct shared_memory *shmem)
{
    struct shmid_ds ds;
    int destroy_mode;
    int destroy = FALSE;
    char path[MAX_PATH];

    if (shmem == NULL || shmem->magic != SHARED_MEMORY_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    destroy_mode = shmem->flags & DESTROY_MASK;

    if (shmem->ptr)
    {
        shmdt(shmem->ptr);
    }

    if (shmem->shmid != -1)
    {
        if (destroy_mode == DESTROY_ON_CLOSE_IF_LAST)
        {
            /* need to stat to see if there are no more attaches */
            if (shmctl(shmem->shmid, IPC_STAT, &ds) == 0)
            {
                if (ds.shm_nattch == 0)
                    destroy = TRUE;
            }
        }

        if (destroy_mode == DESTROY_ON_CLOSE)
            destroy = TRUE;

        if (destroy)
        {
            shmctl(shmem->shmid, IPC_RMID, NULL);
            sprintf(path, "%s/%s", SHARED_MEMORY_DIRECTORY, shmem->name);
            unlink(path);
        }
    }

    return S_OK;
}

void shared_memory_free(struct shared_memory *shmem)
{
    free(shmem);
}


