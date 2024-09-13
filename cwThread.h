#ifndef cwThread_H
#define cwThread_H

namespace cw
{
  namespace thread
  {
    const int kDefaultStateTimeOutMicros=100000;
    const int kDefaultPauseMicros =       10000;
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
    rc_t create( handle_t& hRef,
                 cbFunc_t func,
                 void* funcArg,
                 const char* label,  // Assign a label which will show up via `top -H` or `ps -T`.
                 int stateTimeOutMicros=kDefaultStateTimeOutMicros,
                 int pauseMicros=kDefaultPauseMicros );
    
    rc_t destroy( handle_t& hRef );


    // 'cycleCnt' gives a limit on the count of time the thread function should be called
    // when the thread exits the pause state. It is ignored if kPauseFl is set in 'cmdFlags'
    enum { kPauseFl=0x01, kWaitFl=0x02 };
    rc_t pause( handle_t h, unsigned cmdFlags = kWaitFl, unsigned cycleCnt=0 );
    rc_t unpause( handle_t h, unsigned cycleCnt=0 ); // same as threadPause(h,kWaitFl)    
  
    stateId_t state( handle_t h );

    // Return the thread id of the calling context.
    thread_id_t id();
    const char* label( handle_t h );

    unsigned stateTimeOutMicros( handle_t h);
    unsigned pauseMicros( handle_t h );
  }
  rc_t threadTest();
}

#endif
