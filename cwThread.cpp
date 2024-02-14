#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"

#include <pthread.h>

namespace cw
{
  
  namespace thread
  {
    enum
    {
     kDoExitThFl  = 0x01,
     kDoPauseThFl = 0x02,
     kDoRunThFl   = 0x04
    };
  
    typedef struct thread_str
    {
      pthread_t pThreadH;
      
      std::atomic<stateId_t> stateId;
      std::atomic<unsigned>  doFlags;
      
      cbFunc_t  func;
      void*     funcArg;
      unsigned  stateMicros;
      unsigned  pauseMicros;
      unsigned  sleepMicros;
      pthread_attr_t attr;
      
    } thread_t;

    inline thread_t* _handleToPtr(handle_t h) { return handleToPtr<handle_t,thread_t>(h); }

    // Called from client thread to wait for the internal thread to transition to a specified state.
    rc_t _waitForState( thread_t* p, stateId_t stateId )
    {
      unsigned waitTimeMicroSecs = 0;
      stateId_t curStateId;
      
      do
      {
        curStateId = p->stateId.load(std::memory_order_acquire);

        if(curStateId == stateId )
          break;
        
        sleepUs( p->sleepMicros );
        
        waitTimeMicroSecs += p->sleepMicros;

      }while( waitTimeMicroSecs < p->stateMicros );

      return curStateId==stateId ? kOkRC :  kTimeOutRC;
    }

    void _threadCleanUpCallback(void* p)
    {
      ((thread_t*)p)->stateId.store(kExitedThId,std::memory_order_release);
    }
  

    void* _threadCallback(void* param)
    {
      thread_t* p = (thread_t*)param;

      // set a clean up handler - this will be called when the 
      // thread terminates unexpectedly or pthread_cleanup_pop() is called.
      pthread_cleanup_push(_threadCleanUpCallback,p);

      unsigned curDoFlags = 0;
      
      do
      {
        // get the current thread state (running or paused)
        stateId_t curStateId = p->stateId.load(std::memory_order_relaxed);

        // if we are in the pause state
        if( curStateId == kPausedThId )
        {
        
          sleepUs( p->pauseMicros );

          curDoFlags = p->doFlags.load(std::memory_order_acquire);

          // check if we have been requested to leave the pause state
          if( cwIsFlag(curDoFlags,kDoRunThFl) )
          {
            p->stateId.store(kRunningThId,std::memory_order_release);
          }
        }
        else // ... we are in running state
        {
          // call the user-defined function
          if( p->func(p->funcArg)==false )
            break;

          curDoFlags = p->doFlags.load(std::memory_order_acquire);
          
          // check if we have been requested to enter the pause state
          if( cwIsFlag(curDoFlags,kDoPauseThFl) )
          {
            p->stateId.store(kPausedThId,std::memory_order_release);
          }
        }
        
      }while( cwIsFlag(curDoFlags,kDoExitThFl) == false );

      pthread_cleanup_pop(1);
	
      pthread_exit(NULL);

      return p;
    }
  }  
}


cw::rc_t cw::thread::create( handle_t& hRef, cbFunc_t func, void* funcArg, int stateMicros, int pauseMicros )
{
  rc_t rc;
  int  sysRC;

  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  thread_t* p = mem::allocZ<thread_t>();

  p->func        = func;
  p->funcArg     = funcArg;
  p->stateMicros = stateMicros;
  p->pauseMicros = pauseMicros;
  p->stateId     = kPausedThId;
  p->sleepMicros = 15000;

  if((sysRC = pthread_attr_init(&p->attr)) != 0)
  {
    p->stateId = kNotInitThId;
    rc = cwLogSysError(kOpFailRC,sysRC,"Thread attribute init failed.");
  }
  else
  {
    /* 

    // Creating the thread in a detached state should prevent it from leaking memory when 
    // the thread is closed and pthread_join() is not called but it doesn't seem to work anymore ????

    if ((sysRC = pthread_attr_setdetachstate(&p->attr, PTHREAD_CREATE_DETACHED)) != 0)
    {
      p->stateId = kNotInitThId;
      rc = cwLogSysError(kOpFailRC,sysRC,"Thread set detach attribute failed.");
    }  
    else      
    */
      if((sysRC = pthread_create(&p->pThreadH, &p->attr, _threadCallback, (void*)p )) != 0 )
      {
        p->stateId = kNotInitThId;
        rc = cwLogSysError(kOpFailRC,sysRC,"Thread create failed.");
      }
  }
  
  hRef.set(p);
  return rc;
}
  
cw::rc_t cw::thread::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  int sysRC;
  
  if( !hRef.isValid() )
    return rc;

  thread_t* p  = _handleToPtr(hRef);

  // tell the thread to exit
  p->doFlags.store(kDoExitThFl,std::memory_order_release);

  // wait for the thread to exit and then deallocate the thread object
  if((rc = _waitForState(p,kExitedThId)) != kOkRC )
    return  cwLogError(rc,"Thread timed out waiting for destroy.");

  // Block until the thread is actually fully cleaned up
  if((sysRC = pthread_join(p->pThreadH,NULL)) != 0)
    rc = cwLogSysError(kOpFailRC,sysRC,"Thread join failed.");
      
  //if( pthread_attr_destroy(&p->attr) != 0 )
  //  rc = cwLogError(kOpFailRC,"Thread attribute destroy failed.");
    
  
  mem::release(p);
  hRef.clear();
  
  return rc;
}


cw::rc_t cw::thread::pause( handle_t h, unsigned cmdFlags )
{
  rc_t      rc         = kOkRC;
  bool      pauseFl    = cwIsFlag(cmdFlags,kPauseFl);
  bool      waitFl     = cwIsFlag(cmdFlags,kWaitFl);
  thread_t* p          = _handleToPtr(h);
  stateId_t curStateId = p->stateId.load(std::memory_order_acquire);
  bool      isPausedFl = curStateId == kPausedThId;
  stateId_t waitId;
 
  if( isPausedFl == pauseFl )
    return kOkRC;

  if( pauseFl )
  {
    p->doFlags.store(kDoPauseThFl,std::memory_order_release);
    waitId = kPausedThId;
  }
  else
  {
    p->doFlags.store(kDoRunThFl,std::memory_order_release);
    waitId = kRunningThId;
  }

  if( waitFl )
    rc = _waitForState(p,waitId);

  if( rc != kOkRC )
    cwLogError(rc,"Thread timed out waiting for '%s'. pauseMicros:%i stateMicros:%i sleepMicros:%i", pauseFl ? "pause" : "un-pause",p->pauseMicros,p->stateMicros,p->sleepMicros);
  
  return rc;
  
}

cw::rc_t cw::thread::unpause( handle_t h )
{  return pause( h, kWaitFl);  }

cw::thread::stateId_t cw::thread::state( handle_t h )
{
  thread_t* p = _handleToPtr(h);
  return p->stateId.load(std::memory_order_acquire);
}

cw::thread::thread_id_t cw::thread::id()
{
  typedef struct
  {
    union
    {
      thread_id_t id;
      pthread_t   pthread_id;
    } u;
  } id_t;
  
  id_t id;
  id.u.pthread_id = pthread_self();
  return id.u.id;
}

unsigned cw::thread::stateTimeOutMicros( handle_t h)
{
  thread_t* p = _handleToPtr(h);
  return p->stateMicros;
}

unsigned cw::thread::pauseMicros( handle_t h )
{
  thread_t* p = _handleToPtr(h);
  return p->pauseMicros;
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
  thread::handle_t h;
  unsigned val = 0;
  rc_t     rc;
  char     c   = 0;
  
  if((rc = thread::create(h,_threadTestCb,&val)) != kOkRC )
    return rc;
  
  if((rc = thread::pause(h,0)) != kOkRC )
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
        cwLogInfo("state=%i\n",thread::state(h));
        break;

      case 'p':
        {
          if( thread::state(h) == thread::kPausedThId )
            rc = thread::pause(h,thread::kWaitFl);
          else
            rc = thread::pause(h,thread::kPauseFl|thread::kWaitFl);

          if( rc == kOkRC )
            cwLogInfo("new state:%i\n", thread::state(h));
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
  rc_t rc0 = rc = thread::destroy(h);
  
  return rc == kOkRC ? rc0 : rc;
}
