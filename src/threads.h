#ifndef THREADS_H
#define THREADS_H

#include <stdbool.h>

typedef struct thread_t thread_t;
typedef struct lock_t lock_t;

thread_t *ThreadCreate();
void ThreadFree(thread_t *Thread);
void ThreadRun(thread_t *Thread, void (*Function)(void *Data), void *Data);
bool ThreadIsReady(thread_t *Thread); // Is thread ready for a new task?
bool ThreadIsBusy(thread_t *Thread); // Is thread busy with a task?
void ThreadWaitReady(thread_t *Thread); // Returns only when thread is ready
void *ThreadGetData(thread_t *Thread);
lock_t *LockInit(unsigned int Value);
void LockFree(lock_t *Lock);
void LockWait(lock_t *Lock);
void LockPost(lock_t *Lock);

#endif
