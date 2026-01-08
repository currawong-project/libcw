//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
#include "cwMutex.h"
#include "cwTest.h"
#include "cwTime.h"

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
      
      cbFunc_t       func;
      void*          funcArg;
      unsigned       stateMicros;
      unsigned       pauseMicros;
      unsigned       waitMicros;
      pthread_attr_t attr;
      char*          label;

      mutex::handle_t mutexH;
      unsigned        cycleIdx;  // current cycle phase
      unsigned        cycleCnt;  // cycle phase limit
      unsigned        execCnt;
      
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

        if(curStateId == kExitedThId)
        {
          return cwLogError(kInvalidStateRC,"Cannot wait on an thread that has already exited.");          
        }
        
        sleepUs( p->waitMicros );
        
        waitTimeMicroSecs += p->waitMicros;

      }while( waitTimeMicroSecs < p->stateMicros );

      return curStateId==stateId ? kOkRC :  kTimeOutRC;
    }

    void _threadCleanUpCallback(void* p)
    {
      thread_t* thread = (thread_t*)p;
      bool lock_fl = false;

      // Attempt to lock the mutex - this is safe
      // because the only thread that could have
      // previously locked the mutex is this thread
      // and try-lock() will simply return EBUSY if
      // the mutex is already locked.
      mutex::tryLock(thread->mutexH,lock_fl);

      // At this point the mutex is locked.

      // Unlock it.
      mutex::unlock(thread->mutexH);
        
        
      thread->stateId.store(kExitedThId,std::memory_order_release);
    }
  

    void* _threadCallback(void* param)
    {
      thread_t* p = (thread_t*)param;
      unsigned curDoFlags = 0;

      // set a clean up handler - this will be called when the 
      // thread terminates unexpectedly or pthread_cleanup_pop() is called.
      pthread_cleanup_push(_threadCleanUpCallback,p);


      // Lock the mutex so that it is already locked prior to the first call to waitOnCondVar()
      rc_t rc;
      if((rc = mutex::lock(p->mutexH)) != kOkRC )
      {
        cwLogError(rc,"Thread signal condition mutex lock failed.");
        goto errLabel;      
      }
      
      
      do
      {
        // get the current thread state (running or paused)
        stateId_t curStateId = p->stateId.load(std::memory_order_relaxed);

        // if we are in the pause state
        if( curStateId == kPausedThId )
        {
          // unlock mutex and block on cond. var. for pauseMicros or until signaled
          rc = waitOnCondVar(p->mutexH, false, p->pauseMicros/1000 ); 

          switch(rc)
          {
            case kTimeOutRC:
              // the mutex is not locked
              break;
              
            case kOkRC:
              // the cond. var. was signaled and the mutex is locked
              break;
              
            default:
              // mutex is not locked
              rc = cwLogError(rc,"Condition variable wait failed.");
          }


          curDoFlags = p->doFlags.load(std::memory_order_acquire);

          // if exit was requested - and the mutex is unlocked
          if( cwIsFlag(curDoFlags,kDoExitThFl) && rc != kOkRC )
          {
            // this will cause the waitOnCondVar() to
            // immediately return at the top of the loop
            signalCondVar(p->mutexH);
            //mutex::lock(p->mutexH);
          }
          else
          {
            // check if we have been requested to leave the pause state
            if( cwIsFlag(curDoFlags,kDoRunThFl) )
            {
              p->cycleIdx = 0;
              p->stateId.store(kRunningThId,std::memory_order_release);
            }
          }
        }
        else // ... we are in running state
        {
          // call the user-defined function
          if( p->func(p->funcArg)==false )
            break;

          curDoFlags = p->doFlags.load(std::memory_order_acquire);

          if( cwIsNotFlag(curDoFlags,kDoExitThFl) )
          {
            p->cycleIdx += 1;

            // if a cycle limit was set then check if the limit was reached
            bool cycles_done_fl =  p->cycleCnt > 0 && p->cycleIdx >= p->cycleCnt;

            // check if we have been requested to enter the pause state
            if(  (cwIsFlag(curDoFlags,kDoPauseThFl) || cycles_done_fl)  )
            {
              p->stateId.store(kPausedThId,std::memory_order_release);
              p->doFlags.store(0,std::memory_order_release);
            }
          }
          
        }
        
      }while( cwIsFlag(curDoFlags,kDoExitThFl) == false );

    errLabel:
      pthread_cleanup_pop(1);
	
      pthread_exit(NULL);

      return p;
    }
  }  
}


cw::rc_t cw::thread::create( handle_t& hRef, cbFunc_t func, void* funcArg, const char* label, int stateMicros, int pauseMicros )
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
  p->waitMicros = 15000;
  p->label       = mem::duplStr(label);

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

    // Create the cond. var mutex
    if((rc = mutex::create(p->mutexH )) != kOkRC )
    {
      rc = cwLogError(rc,"Thread signal condition mutex create failed.");
      goto errLabel;
    }
    
    // create the thread - in paused state
    if((sysRC = pthread_create(&p->pThreadH, &p->attr, _threadCallback, (void*)p )) != 0 )
    {
      p->stateId = kNotInitThId;
      rc = cwLogSysError(kOpFailRC,sysRC,"Thread create failed.");
    }
  }

  if( label != nullptr )
    pthread_setname_np(p->pThreadH, label);

  
  hRef.set(p);

  
  cwLogInfo("Thread %s id:%p created.",cwStringNullGuard(label), p->pThreadH);
  
errLabel:

  if( rc != kOkRC && p->mutexH.isValid() )
  {    
    mutex::destroy(p->mutexH);
  }
  
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
    return  cwLogError(rc,"Thread '%s' timed out waiting for destroy.",p->label);

  // Block until the thread is actually fully cleaned up
  if((sysRC = pthread_join(p->pThreadH,NULL)) != 0)
    rc = cwLogSysError(kOpFailRC,sysRC,"Thread '%s' join failed.",p->label);
      
  //if( pthread_attr_destroy(&p->attr) != 0 )
  //  rc = cwLogError(kOpFailRC,"Thread attribute destroy failed.");

  if( p->mutexH.isValid() )
  {
    mutex::destroy(p->mutexH);
  }
  mem::release(p->label);
  mem::release(p);
  hRef.clear();
  
  return rc;
}


cw::rc_t cw::thread::pause( handle_t h, unsigned cmdFlags, unsigned cycleCnt )
{
  rc_t      rc         = kOkRC;
  bool      pauseFl    = cwIsFlag(cmdFlags,kPauseFl);
  bool      waitFl     = cwIsFlag(cmdFlags,kWaitFl);
  thread_t* p          = _handleToPtr(h);
  stateId_t curStateId = p->stateId.load(std::memory_order_acquire);
  bool      isPausedFl = curStateId == kPausedThId;
  stateId_t waitId;

  p->cycleCnt = cycleCnt;
  
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
    if((rc = signalCondVar(p->mutexH)) != kOkRC )
    {
      cwLogError(rc,"Cond. var. signalling failed.");
      goto errLabel;
    }
  }

  if( waitFl )
    rc = _waitForState(p,waitId);

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Thread '%s' timed out waiting for '%s'. pauseMicros:%i stateMicros:%i waitMicros:%i", p->label, pauseFl ? "pause" : "un-pause",p->pauseMicros,p->stateMicros,p->waitMicros);
  
  return rc;
  
}

cw::rc_t cw::thread::unpause( handle_t h, unsigned cycleCnt )
{  return pause( h, kWaitFl, cycleCnt);  }

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

const char* cw::thread::label( handle_t h )
{
  thread_t* p = _handleToPtr(h);
  return p->label==nullptr ? "<no_thread_label>" : p->label;
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
  time::spec_t g_t0 = {0,0};
  time_t   g_micros = 0;
  unsigned g_n      = 0;
  
  bool _threadTestCb( void* p )
  {
    if( g_t0.tv_nsec != 0 )
    {
      time::spec_t t1;
      time::get(t1);
      g_micros += time::elapsedMicros(g_t0,t1);
      g_n += 1;
      g_t0.tv_nsec = 0;
    }
    
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
  unsigned cycleCnt = 0;
  
  // create the thread
  if((rc = thread::create(h,_threadTestCb,&val,"thread_test")) != kOkRC )
    return rc;

  // start the thread
  if((rc = thread::pause(h,0,cycleCnt)) != kOkRC )
    goto errLabel;


  cwLogInfo("o=print p=pause s=state q=quit\n");

  while( c != 'q' )
  {

    c = (char)fgetc(stdin);
    fflush(stdin);

    switch(c)
    {
      case 'o':
        cwLogInfo("val: 0x%x %i\n",val,val);
        break;

      case 's':
        cwLogInfo("state=%i\n",thread::state(h));
        break;

      case 'p':
        {
          if( thread::state(h) == thread::kPausedThId )
          {
            time::get(g_t0);
            // We don't set kWaitFl w/ cycleCnt>0 because we are running very
            // few cycles - the cycles will run and the
            // state of the thread will return to 'paused'
            // before _waitForState() can notice the 'running' state.
            rc = thread::pause(h, cycleCnt==0 ? thread::kWaitFl : 0,cycleCnt);
          }
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
        printf("wakeup micros:%li cnt:%i avg:%li\n",g_micros,g_n,g_n>0 ? g_micros/g_n : 0);
        break;

        //default:
        //cwLogInfo("Unknown:%c\n",c);
          
    }
  }

 errLabel:
  rc_t rc0 = rc = thread::destroy(h);
  
  return rc == kOkRC ? rc0 : rc;
}
