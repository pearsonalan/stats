/* lock.h */

#ifndef _LOCK_H_INCLUDED_
#define _LOCK_H_INCLUDED_

#include "semaphore.h"

struct lock {
    struct semaphore sem;
};

int lock_create(const char * name, struct lock **lock_out);

int lock_acquire(struct lock *lock);
int lock_release(struct lock *lock);

int lock_close(struct lock *lock);
void lock_free(struct lock *lock);

#define lock_init(lock,name) semaphore_init(&(lock)->sem,name,1)
#define lock_open(lock) semaphore_open_and_set(&(lock)->sem,1)
#define lock_acquire(lock) semaphore_P(&(lock)->sem,0)
#define lock_release(lock) semaphore_V(&(lock)->sem,0)
#define lock_close(lock,remove) semaphore_close(&(lock)->sem,(remove));
#define lock_is_open(lock) semaphore_is_open(&((lock)->sem))

#endif
