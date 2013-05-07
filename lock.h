/* lock.h */

#ifndef _LOCK_H_INCLUDED_
#define _LOCK_H_INCLUDED_

#include "semaphore.h"

struct lock {
    struct semaphore sem;
};

int lock_create(const char * name, struct lock **lock_out);
int lock_init(struct lock *lock, const char * name);
int lock_open(struct lock *lock);

int lock_acquire(struct lock *lock);
int lock_release(struct lock *lock);

int lock_close(struct lock *lock);
void lock_free(struct lock *lock);

#endif
