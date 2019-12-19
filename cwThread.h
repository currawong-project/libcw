#ifndef cwThread_H
#define cwThread_H

namespace cw
{
  typedef enum
  {
   kNotInitThId,
   kPausedThId,
   kRunningThId,
   kExitedThId
  } kThreadStateId_t;
  
  typedef handle<struct thread_str> threadH_t;

  typedef bool (*threadFunc_t)( void* arg );

  // stateMicros = time out duration for switching in/out of pause or in to exit
  // pauseMicros = duration of thread sleep interval when in paused state.
  rc_t threadCreate( threadH_t& hRef, threadFunc_t func, void* funcArg, int stateTimeOutMicros=100000, int pauseMicros=10000 );
  rc_t threadDestroy( threadH_t& hRef );

  enum { kThreadPauseFl=0x01, kThreadWaitFl=0x02 };
  rc_t threadPause( threadH_t& h, unsigned cmdFlags );
  kThreadStateId_t threadState( threadH_t h );

  rc_t threadTest();
}

#endif
