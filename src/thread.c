#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>

#include "thread.h"

struct Lock
{
  sem_t lock;
};

struct Thread
{
  Lock *lock, *runFlag;
  pthread_t id;
  void (*function)(void *userData);
  void *userData;
};

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void *threadMain(void *threadData);
void threadExit(void *dummy);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

Thread *threadNew(void)
{
  // Allocate memory
  Thread *thread=malloc(sizeof(Thread));
  Lock *lock=lockNew(1);
  Lock *runFlag=lockNew(0);
  if (thread==NULL || lock==NULL || runFlag==NULL)
  {
    free(thread);
    lockFree(lock);
    lockFree(runFlag);
    return NULL;
  }
  
  // Init data
  thread->lock=lock;
  thread->runFlag=runFlag;
  thread->function=NULL;
  thread->userData=NULL;
  
  // Start thread
  if (pthread_create(&thread->id, NULL, &threadMain, (void *)thread)!=0)
  {
    free(thread);
    lockFree(thread->lock);
    lockFree(thread->runFlag);
    return NULL;
  }
  
  return thread;
}

void threadFree(Thread *thread)
{
  // No thread given?
  if (thread==NULL)
    return;
  
  // Tell thread to run exit routine, which will promptly kill it
  // this will also wait for the current task to finish.
  threadRun(thread, &threadExit, NULL);
  
  // Wait for thread to finish.
  pthread_join(thread->id, NULL);
  
  // Free locks and memory
  lockFree(thread->lock);
  lockFree(thread->runFlag);
  free(thread);
}

void threadRun(Thread *thread, void (*function)(void *userData), void *userData)
{
  // Grab lock (ensures thread is ready).
  // This is released by the thread itself in threadMain().
  lockWait(thread->lock);
    
  // Give data.
  thread->function=function;
  thread->userData=userData;
  
  // Indicate work is now available.
  lockPost(thread->runFlag);
}

bool threadIsReady(Thread *thread)
{
  return (lockGetValue(thread->lock)>0);
}

void threadWaitReady(Thread *thread)
{
  // Simply wait for lock (i.e. thread is free), then restore.
  lockWait(thread->lock);
  lockPost(thread->lock);
}

Lock *lockNew(unsigned int value)
{
  // Allocate memory.
  Lock *lock=malloc(sizeof(Lock));
  if (lock==NULL)
    return NULL;
  
  // Init semaphore.
  if (sem_init(&lock->lock, 0, value)!=0)
  {
    free(lock);
    return NULL;
  }
  
  return lock;
}

void lockFree(Lock *lock)
{
  if (lock==NULL)
    return;
  sem_destroy(&lock->lock);
  free(lock);
}

void lockWait(Lock *lock)
{
  sem_wait(&lock->lock);
}

void lockPost(Lock *lock)
{
  sem_post(&lock->lock);
}

bool lockTryWait(Lock *lock)
{
  return (sem_trywait(&lock->lock)==0);
}

unsigned int lockGetValue(Lock *lock)
{
  int ret;
  if (sem_getvalue(&lock->lock, &ret)==0)
    return (ret>=0 ? ret : 0); // POSIX allows ret to be negative but we don't rely on it.
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void *threadMain(void *threadData)
{
  // Get our data.
  Thread *thread=(Thread *)threadData;
  
  // Main loop.
  while(1)
  {
    // Wait to be given a function.
    lockWait(thread->runFlag);
    
    // Call function.
    thread->function(thread->userData);
    
    // Release lock (which was grabbed in threadRun()).
    lockPost(thread->lock);
  }
  
  return NULL;
}

void threadExit(void *dummy)
{
  pthread_exit(NULL);
}
