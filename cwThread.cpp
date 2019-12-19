#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"

#include <pthread.h>

namespace cw
{

  enum
  {
   kDoExitThFl  = 0x01,
   kDoPauseThFl = 0x02,
   kDoRunThFl   = 0x04
  };
  
  typedef struct thread_str
  {
    pthread_t        pThreadH;
    kThreadStateId_t stateId;
    threadFunc_t     func;
    void*            funcArg;
    unsigned         doFlags;
    unsigned         stateMicros;
    unsigned         pauseMicros;
    unsigned         sleepMicros = 15000;
  } thread_t;

#define _threadHandleToPtr(h) handleToPtr<threadH_t,thread_t>(h)

  rc_t _waitForState( thread_t* p, unsigned stateId )
  {
    unsigned waitTimeMicroSecs = 0;

    while( p->stateId != stateId && waitTimeMicroSecs < p->stateMicros )
    {
      sleepUs( p->sleepMicros );
      waitTimeMicroSecs += p->sleepMicros;
    }

    return p->stateId==stateId ? kOkRC :  kTimeOutRC;
  }

  void _threadCleanUpCallback(void* p)
  {
    ((thread_t*)p)->stateId = kExitedThId;
  }
  

  void* _threadCallback(void* param)
  {
    thread_t* p = (thread_t*)param;

    // set a clean up handler - this will be called when the 
    // thread terminates unexpectedly or pthread_cleanup_pop() is called.
    pthread_cleanup_push(_threadCleanUpCallback,p);

    while( cwIsFlag(p->doFlags,kDoExitThFl) == false )
    {

      // if we are in the pause state
      if( p->stateId == kPausedThId )
      {
        
        sleepUs( p->pauseMicros );

        // check if we have been requested to leave the pause state
        if( cwIsFlag(p->doFlags,kDoRunThFl) )
        {
          p->doFlags = cwClrFlag(p->doFlags,kDoRunThFl);
          p->stateId   = kRunningThId;
        }
      }
      else
      {
        // call the user-defined function
        if( p->func(p->funcArg)==false )
          break;

        // check if we have been requested to enter the pause state
        if( cwIsFlag(p->doFlags,kDoPauseThFl) )
        {
          p->doFlags = cwClrFlag(p->doFlags,kDoPauseThFl);
          p->stateId   = kPausedThId;        
        }
      }
    }

    pthread_cleanup_pop(1);
	
    pthread_exit(NULL);

    return p;
  }
  
}


cw::rc_t cw::threadCreate( threadH_t& hRef, threadFunc_t func, void* funcArg, int stateMicros, int pauseMicros )
{
  rc_t rc;
  int  sysRC;
  
  if((rc = threadDestroy(hRef)) != kOkRC )
    return rc;

  thread_t* p = memAllocZ<thread_t>();

  p->func        = func;
  p->funcArg     = funcArg;
  p->stateMicros = stateMicros;
  p->pauseMicros = pauseMicros;
  p->stateId     = kPausedThId;
  
  if((sysRC = pthread_create(&p->pThreadH,NULL, _threadCallback, (void*)p )) != 0 )
  {
    p->stateId = kNotInitThId;
    rc = cwLogSysError(kOpFailRC,sysRC,"Thread create failed.");
  }
  
  hRef.set(p);
  return rc;
}
  
cw::rc_t cw::threadDestroy( threadH_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return rc;

  thread_t* p  = _threadHandleToPtr(hRef);

  // tell the thread to exit
  p->doFlags = cwSetFlag(p->doFlags,kDoExitThFl);

  // wait for the thread to exit and then deallocate the thread object
  if((rc = _waitForState(p,kExitedThId)) != kOkRC )
    return  cwLogError(rc,"Thread timed out waiting for destroy.");

  hRef.release();
  
  return rc;
}


cw::rc_t cw::threadPause( threadH_t& h, unsigned cmdFlags )
{
  rc_t      rc         = kOkRC;
  bool      pauseFl    = cwIsFlag(cmdFlags,kThreadPauseFl);
  bool      waitFl     = cwIsFlag(cmdFlags,kThreadWaitFl);
  thread_t* p          = _threadHandleToPtr(h);
  bool      isPausedFl = p->stateId == kPausedThId;
  unsigned  waitId;
 
  if( isPausedFl == pauseFl )
    return kOkRC;

  if( pauseFl )
  {
    p->doFlags = cwSetFlag(p->doFlags,kDoPauseThFl);
    waitId = kPausedThId;
  }
  else
  {
    p->doFlags = cwSetFlag(p->doFlags,kDoRunThFl);
    waitId = kRunningThId;
  }

  if( waitFl )
    rc = _waitForState(p,waitId);

  if( rc != kOkRC )
    cwLogError(rc,"Thread timed out waiting for '%s'.", pauseFl ? "pause" : "un-pause");
  
  return rc;
  
}

cw::kThreadStateId_t cw::threadState( threadH_t h )
{
  thread_t* p = _threadHandleToPtr(h);
  return p->stateId;
}


namespace cw
{
  bool _threadTestCb( void* p )
  {
    unsigned* ip = (unsigned*)p;
    ip[0]++;
    return true;
  }
}

cw::rc_t cw::threadTest()
{
  threadH_t h;
  unsigned  val = 0;
  rc_t      rc;
  char c = 0;
  
  if((rc = threadCreate(h,_threadTestCb,&val)) != kOkRC )
    return rc;
  
  if((rc = threadPause(h,0)) != kOkRC )
    goto errLabel;


  cwLogInfo("o=print p=pause s=state q=quit\n");

  while( c != 'q' )
  {

    c = (char)fgetc(stdin);
    fflush(stdin);

    switch(c)
    {
      case 'o':
        cwLogInfo("val: 0x%x\n",val);
        break;

      case 's':
        cwLogInfo("state=%i\n",threadState(h));
        break;

      case 'p':
        {
          if( threadState(h) == kPausedThId )
            rc = threadPause(h,kThreadWaitFl);
          else
            rc = threadPause(h,kThreadPauseFl|kThreadWaitFl);

          if( rc == kOkRC )
            cwLogInfo("new state:%i\n", threadState(h));
          else
          {
            cwLogError(rc,"threadPause() test failed.");
            goto errLabel;
          }

        }
        break;
        
      case 'q':
        break;

        //default:
        //cwLogInfo("Unknown:%c\n",c);
          
    }
  }

 errLabel:
  rc_t rc0 = rc = threadDestroy(h);
  
  return rc == kOkRC ? rc0 : rc;
}
