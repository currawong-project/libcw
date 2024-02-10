#ifndef cwThread_H
#define cwThread_H

namespace cw
{
  namespace thread
  {
    typedef enum
    {
     kNotInitThId,
     kPausedThId,
     kRunningThId,
     kExitedThId
    } stateId_t;
  
    typedef handle<struct thread_str> handle_t;

    // Return false to indicate that the thread should terminate.
    typedef bool (*cbFunc_t)( void* arg );

    typedef  unsigned long long thread_id_t;

    // The thread is in the 'paused' state after it is created.
    // stateMicros = total time out duration for switching to the  exit state or for switching in/out of pause state. 
    // pauseMicros = duration of thread sleep interval when in paused state.
    rc_t create( handle_t& hRef, cbFunc_t func, void* funcArg, int stateTimeOutMicros=100000, int pauseMicros=10000 );
    rc_t destroy( handle_t& hRef );

  
    enum { kPauseFl=0x01, kWaitFl=0x02 };
    rc_t pause( handle_t h, unsigned cmdFlags = kWaitFl );
    rc_t unpause( handle_t h ); // same as threadPause(h,kWaitFl)
  
    stateId_t state( handle_t h );

    // Return the thread id of the calling context.
    thread_id_t id();
  }
  rc_t threadTest();
}

#endif
