#ifndef THREAD_H
#define THREAD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Thread Thread;
typedef struct Lock Lock; // A semaphore.
typedef struct { volatile int value; } SpinLock;

typedef volatile bool AtomicBool;
typedef volatile uint64_t AtomicUInt64;

Thread *threadNew(void);
void threadFree(Thread *thread); // Waits until thread is ready.

void threadRun(Thread *thread, void (*function)(void *userData), void *userData); // Waits until thread is ready.

void threadWaitReady(Thread *thread); // Returns only when thread is ready.

Lock *lockNew(unsigned int value);
void lockFree(Lock *lock);

void lockWait(Lock *lock);
void lockPost(Lock *lock);
bool lockTryWait(Lock *lock);

void spinLockInit(SpinLock *spinLock, bool locked);
void spinLockWait(SpinLock *spinLock);
void spinLockPost(SpinLock *spinLock);

bool atomicBoolGet(AtomicBool *abool);
void atomicBoolSet(AtomicBool *abool, bool value);

uint64_t atomicUInt64Get(AtomicUInt64 *auint64);
void atomicUInt64Set(AtomicUInt64 *auint64, uint64_t value);

#endif
