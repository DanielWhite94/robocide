#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include "threads.h"

typedef enum
{
  tstate_setup, // Thread is still in the creation process
  tstate_ready, // Thread is ready for a task
  tstate_busy, // Thread is busy with a task
  tstate_dead // Thread is dead (i.e. ready, or almost ready, to be join'ed)
}tstate_t;

struct lock_t
{
  sem_t Lock;
};

struct thread_t
{
  pthread_t Id;
  tstate_t State;
  lock_t *StateLock, *StateChangeFlag, *DataLock;
  void (*Function)(void *Data);
  void *Data;
};

void *ThreadMain(void *Arg);
void ThreadChangeState(thread_t *Thread, tstate_t State);
void ThreadWaitForState(thread_t *Thread, tstate_t State);
void ThreadWaitForStateChange(thread_t *Thread, tstate_t OldState, tstate_t NewState);
tstate_t ThreadGetState(thread_t *Thread);

thread_t *ThreadCreate()
{
  // Allocate memory
  thread_t *Thread=malloc(sizeof(thread_t));
  if (Thread==NULL)
    return NULL;
  
  // Init data
  Thread->Data=NULL;
  Thread->StateLock=LockInit(1);
  Thread->StateChangeFlag=LockInit(1);
  Thread->DataLock=LockInit(0);
  ThreadChangeState(Thread, tstate_setup);
  
  // Start thread
  if (pthread_create(&Thread->Id, NULL, &ThreadMain, (void *) Thread))
  {
    LockFree(Thread->StateLock);
    LockFree(Thread->StateChangeFlag);
    LockFree(Thread->DataLock);
    free(Thread);
    return NULL;
  }
  
  return Thread;
}

void ThreadFree(thread_t *Thread)
{
  // No thread given?
  if (Thread==NULL)
    return;
  
  // Wait for thread to be in 'ready' state before setting to 'dead' state
  ThreadWaitForStateChange(Thread, tstate_ready, tstate_dead);
  
  // Set away thread (essentially to commit suicide)
  LockPost(Thread->DataLock);
  
  // Wait for thread to finish
  pthread_join(Thread->Id, NULL);
  
  // Free locks and memory
  LockFree(Thread->StateLock);
  LockFree(Thread->StateChangeFlag);
  LockFree(Thread->DataLock);
  free(Thread);
}

void ThreadRun(thread_t *Thread, void (*Function)(void *), void *Data)
{
  // Wait for thread to be in 'ready' state before setting to 'busy' state
  ThreadWaitForStateChange(Thread, tstate_ready, tstate_busy);
  
  // Give data
  Thread->Function=Function;
  Thread->Data=Data;
  
  // Release data lock to start thread
  LockPost(Thread->DataLock);
}

bool ThreadIsReady(thread_t *Thread)
{
  return (ThreadGetState(Thread)==tstate_ready);
}

bool ThreadIsBusy(thread_t *Thread)
{
  return (ThreadGetState(Thread)==tstate_busy);
}

void ThreadWaitReady(thread_t *Thread)
{
  ThreadWaitForState(Thread, tstate_ready);
}

void *ThreadGetData(thread_t *Thread)
{
  return Thread->Data;
}

lock_t *LockInit(unsigned int Value)
{
  // Allocate memory
  lock_t *Lock=malloc(sizeof(lock_t));
  if (Lock==NULL)
    return NULL;
  
  // Init semaphore
  if (sem_init(&Lock->Lock, 0, Value)!=0)
  {
    free(Lock);
    return NULL;
  }
  
  return Lock;
}

void LockFree(lock_t *Lock)
{
  sem_destroy(&Lock->Lock);
}

void LockWait(lock_t *Lock)
{
  sem_wait(&Lock->Lock);
}

void LockPost(lock_t *Lock)
{
  sem_post(&Lock->Lock);
}

void *ThreadMain(void *Arg)
{
  // Get our data
  thread_t *Thread=(thread_t *)Arg;
  
  // Main loop
  while(1)
  {
    // Update our state to 'ready'
    ThreadChangeState(Thread, tstate_ready);
    
    // Wait for instructions
    LockWait(Thread->DataLock);
    
    // What are we to do?
    switch(Thread->State)
    {
      case tstate_setup:
      case tstate_ready:
        // Nothing to do
      break;
      case tstate_busy:
        // Execute task given
        Thread->Function(Thread->Data);
      break;
      case tstate_dead:
        // Exit
        return NULL;
      break;
    }
  }
  
  return NULL;
}

void ThreadChangeState(thread_t *Thread, tstate_t State)
{
  LockWait(Thread->StateLock);
  Thread->State=State;
  LockPost(Thread->StateChangeFlag);
  LockPost(Thread->StateLock);
}

void ThreadWaitForState(thread_t *Thread, tstate_t State)
{
  // Loop until thread is in correct state
  while(ThreadGetState(Thread)!=State)
    // Wait for a state change
    LockWait(Thread->StateChangeFlag);
}

void ThreadWaitForStateChange(thread_t *Thread, tstate_t OldState, tstate_t NewState)
{
  while(1)
  {
    // Check state
    LockWait(Thread->StateLock);
    if (Thread->State==OldState)
    {
      // If match, update the state
      Thread->State=NewState;
      
      // Indicate a change has occured
      LockPost(Thread->StateChangeFlag);
      
      // Release state lock
      LockPost(Thread->StateLock);
      
      // Return
      break;
    }
    
    // Release state lock
    LockPost(Thread->StateLock);
    
    // Wait for a state change
    LockWait(Thread->StateChangeFlag);
  }
}

tstate_t ThreadGetState(thread_t *Thread)
{
  LockWait(Thread->StateLock);
  tstate_t State=Thread->State;
  LockPost(Thread->StateLock);
  return State;
}
