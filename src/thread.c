#include <errno.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>

#include "thread.h"

struct Lock {
	sem_t lock;
};

struct Thread {
	Lock *lock, *runFlag;
	pthread_t id;
	void (*function)(void *userData);
	void *userData;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

void *threadMain(void *threadData);
void threadExit(void *dummy);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

Thread *threadNew(void)
{
	// Allocate memory.
	Thread *thread=malloc(sizeof(Thread));
	Lock *lock=lockNew(1);
	Lock *runFlag=lockNew(0);
	if (thread==NULL || lock==NULL || runFlag==NULL) {
		lockFree(lock);
		lockFree(runFlag);
		free(thread);
		return NULL;
	}

	// Init data.
	thread->lock=lock;
	thread->runFlag=runFlag;
	thread->function=NULL;
	thread->userData=NULL;

	// Start thread.
	if (pthread_create(&thread->id, NULL, &threadMain, (void *)thread)!=0) {
		lockFree(thread->lock);
		lockFree(thread->runFlag);
		free(thread);
		return NULL;
	}

	return thread;
}

void threadFree(Thread *thread) {
	// No thread given?
	if (thread==NULL)
		return;

	// Tell thread to run exit routine, which will promptly kill it
	// this will also wait for the current task to finish.
	threadRun(thread, &threadExit, NULL);

	// Wait for thread to finish.
	pthread_join(thread->id, NULL);

	// Free locks and memory.
	lockFree(thread->lock);
	lockFree(thread->runFlag);
	free(thread);
}

void threadRun(Thread *thread, void (*function)(void *userData), void *userData) {
	// Grab lock (ensures thread is ready).
	// This is released by the thread itself in threadMain().
	lockWait(thread->lock);

	// Give data.
	thread->function=function;
	thread->userData=userData;

	// Indicate work is now available.
	lockPost(thread->runFlag);
}

void threadWaitReady(Thread *thread) {
	// Simply wait for lock (i.e. thread is free), then restore.
	lockWait(thread->lock);
	lockPost(thread->lock);
}

Lock *lockNew(unsigned int value) {
	// Allocate memory.
	Lock *lock=malloc(sizeof(Lock));
	if (lock==NULL)
		return NULL;

	// Init semaphore.
	if (sem_init(&lock->lock, 0, value)!=0) {
		free(lock);
		return NULL;
	}

	return lock;
}

void lockFree(Lock *lock) {
	if (lock==NULL)
		return;
	sem_destroy(&lock->lock);
	free(lock);
}

void lockWait(Lock *lock) {
	do {
		if (sem_wait(&lock->lock)==0)
			return;
	} while(errno==EINTR);
}

void lockPost(Lock *lock) {
	sem_post(&lock->lock);
}

bool lockTryWait(Lock *lock) {
	return (sem_trywait(&lock->lock)==0);
}

bool atomicBoolGet(AtomicBool *abool) {
	return __sync_xor_and_fetch(abool, 0);
}

void atomicBoolSet(AtomicBool *abool, bool value) {
	bool oldValue=*abool;
	while(1) {
		bool newOldValue=__sync_val_compare_and_swap(abool, oldValue, value);
		if (newOldValue==oldValue)
			break;
		oldValue=newOldValue;
	}
}

////////////////////////////////////////////////////////////////////////////////
// Private functions.
////////////////////////////////////////////////////////////////////////////////

void *threadMain(void *threadData) {
	// Get our data.
	Thread *thread=(Thread *)threadData;

	// Main loop.
	while(1) {
		// Wait to be given a function.
		lockWait(thread->runFlag);

		// Call function.
		thread->function(thread->userData);

		// Release lock (which was grabbed in threadRun()).
		lockPost(thread->lock);
	}

	return NULL;
}

void threadExit(void *dummy) {
	pthread_exit(NULL);
}
