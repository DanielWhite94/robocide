#ifndef THREAD_H
#define THREAD_H

#include <stdbool.h>

typedef struct Thread Thread;
typedef struct Lock Lock; // A semaphore.

Thread *threadNew(void);
void threadFree(Thread *thread); // Waits until thread is ready.

void threadRun(Thread *thread, void (*function)(void *userData), void *userData); // Waits until thread is ready.

void threadWaitReady(Thread *thread); // Returns only when thread is ready.

Lock *lockNew(unsigned int value);
void lockFree(Lock *lock);

void lockWait(Lock *lock);
bool lockTryWait(Lock *lock);
void lockPost(Lock *lock);

#endif
