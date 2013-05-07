/* semaphore.c */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "error.h"
#include "semaphore.h"

int semaphore_create(const char *name, unsigned short size, struct semaphore **sem_out )
{
    struct semaphore * sem;
    int err;

    if (sem_out == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (strlen(name) >= SEMAPHORE_MAX_NAME_LEN)
        return ERROR_SEMAPHORE_NAME_TOO_LONG;

    sem = malloc(sizeof(struct semaphore));
    if (!sem)
        return ERROR_MEMORY;

    err = semaphore_init(sem,name,size);
    if (err == S_OK)
    {
        *sem_out = sem;
    }
    else
    {
        free(sem);
        *sem_out = NULL;
    }

    return err;
}


int semaphore_init(struct semaphore *sem, const char *name, unsigned short size)
{
    if (sem == NULL)
        return ERROR_INVALID_PARAMETERS;

    if (strlen(name) >= SEMAPHORE_MAX_NAME_LEN)
        return ERROR_SEMAPHORE_NAME_TOO_LONG;

    memset(sem, 0, sizeof(struct semaphore));

    sem->magic = SEMAPHORE_MAGIC;
    sem->semid = -1;
    sem->size = size;
    strcpy(sem->name, name);

    return S_OK;
}


int semaphore_open(struct semaphore * sem, int flags)
{
    struct stat s;
    char path[MAX_PATH];
    int mode;
    int omode;

    if (sem == NULL || sem->magic != SEMAPHORE_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    /* extract open mode from flags */
    omode = flags & OMODE_MASK;

    /* check for existance of the directory */
    if (stat(SEMAPHORE_DIRECTORY,&s) != 0)
    {
        /* try to create the directory */
        if (mkdir(SEMAPHORE_DIRECTORY, 0755) != 0)
            return ERROR_SEMAPHORE_CANNOT_CREATE_DIRECTORY;
    }
    else
    {
        if ((s.st_mode & S_IFDIR) != S_IFDIR)
            return ERROR_SEMAPHORE_PATH_NOT_DIRECTORY;
    }

    sprintf(path, "%s/%s", SEMAPHORE_DIRECTORY, sem->name);

    if (access(path, W_OK) == -1)
    {
        int fd = creat(path, 0644 );
        if (fd == -1)
            return ERROR_SEMAPHORE_CANNOT_CREATE_PATH;
        close(fd);
    }

    sem->semkey = ftok(path, 1);
    if (sem->semkey == -1)
    {
        return ERROR_SEMAPHORE_CANNOT_CREATE_IPC_TOKEN;
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

    printf("Opening semaphore key %08x for %s (absolute path: %s), mode 0%o, size %d\n", sem->semkey, sem->name, path, mode, sem->size);

    sem->semid = semget(sem->semkey, sem->size, mode) ;
    if (sem->semid == -1)
    {
        if (errno == EEXIST && omode == OMODE_CREATE)
            return ERROR_SEMAPHORE_ALREADY_EXISTS;
        if (errno == ENOENT && omode == OMODE_OPEN_EXISTING)
            return ERROR_SEMAPHORE_DOES_NOT_EXIST;
        if (errno == EINVAL)
            return ERROR_SEMAPHORE_INVALID_SIZE;
        return ERROR_SEMAPHORE_CANNOT_OPEN;
    }

    printf("Successfylly openend semaphore %d (key 0x%08x) for %s.\n", sem->semid, sem->semkey, sem->name);

    return S_OK;
}

int semaphore_set_value(struct semaphore *sem, unsigned short nsem, unsigned short value)
{
    union semun s;

    if (sem == NULL || sem->magic != SEMAPHORE_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    if (nsem >= sem->size)
        return ERROR_INVALID_PARAMETERS;

    s.val = value;

    semctl(sem->semid, nsem, SETVAL, s);

    return S_OK;
}

int semaphore_set_values(struct semaphore *sem, unsigned short *values)
{
    union semun s;

    if (sem == NULL || sem->magic != SEMAPHORE_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    s.array = values;

    semctl(sem->semid, 0, SETALL, s);

    return S_OK;
}

int semaphore_open_and_set(struct semaphore *sem, ... )
{
    va_list ap;
    int i;
    unsigned short *values;
    int res;

    if (sem == NULL || sem->magic != SEMAPHORE_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    values = (unsigned short *) alloca(sizeof(unsigned short) * sem->size);

    va_start(ap, sem);
    for (i = 0; i < sem->size; i++)
    {
        values[i] = (unsigned short) va_arg(ap, int);
    }
    va_end(ap);


    res = semaphore_open(sem, OMODE_CREATE);
    if (res == S_OK)
    {
        printf("Created semaphore. setting initial value\n");
        res = semaphore_set_values(sem,values);
    }
    else if (res == ERROR_SEMAPHORE_ALREADY_EXISTS)
    {
        res = semaphore_open(sem, OMODE_OPEN_OR_CREATE);
        /*
           Race condition possible here!!! this function could return when after the creating process
           has created the semaphore, but not yet set the initial values, so it is possible that the
           semaphore values are uninitialized.  See the stevens solution with polling on ipc_stat
        */
    }

    return res;
}

int semaphore_P(struct semaphore *sem, unsigned short nsem)
{
    struct sembuf s;

    if (sem == NULL || sem->magic != SEMAPHORE_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    if (nsem >= sem->size)
        return ERROR_INVALID_PARAMETERS;

    s.sem_num = nsem;
    s.sem_op = -1;
    s.sem_flg = SEM_UNDO;

    if (semop(sem->semid,&s,1) == -1)
    {

    }

    return S_OK;
}

int semaphore_V(struct semaphore *sem, unsigned short nsem)
{
    struct sembuf s;

    if (sem == NULL || sem->magic != SEMAPHORE_MAGIC)
        return ERROR_INVALID_PARAMETERS;

    if (nsem >= sem->size)
        return ERROR_INVALID_PARAMETERS;

    s.sem_num = nsem;
    s.sem_op = 1;
    s.sem_flg = SEM_UNDO;

    if (semop(sem->semid,&s,1) == -1)
    {

    }

    return S_OK;
}


int semaphore_close(struct semaphore *sem)
{
    return S_OK;
}

void semaphore_free(struct semaphore *sem)
{
    free(sem);
}

