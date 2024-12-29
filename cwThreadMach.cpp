//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwMutex.h"
#include "cwThread.h"
#include "cwTest.h"
#include "cwThreadMach.h"


namespace cw
{
  namespace thread_mach
  {
    typedef struct thread_str
    {
      thread::handle_t thH;
      threadFunc_t     func;
      void*            arg;
      struct thread_str* link;
    } thread_t;
    
    typedef struct thread_mach_str
    {
      thread_t* threadL;
    } thread_mach_t;

    thread_mach_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,thread_mach_t>(h); }

    rc_t _add( thread_mach_t* p, threadFunc_t func, void* arg, const char* label )
    {
      rc_t rc = kOkRC;

      thread_t* t = mem::allocZ<thread_t>();

      if((rc = thread::create(t->thH, func, arg, label==nullptr ? "thread_mach" : label )) != kOkRC )
      {
        rc = cwLogError(rc,"Thread create failed.");
        goto errLabel;
      }
      
      t->func   = func;
      t->arg    = arg;
      t->link   = p->threadL;
      p->threadL = t;
      
    errLabel:
      if( rc != kOkRC )
        mem::release(t);
      
      return rc;
    }

    rc_t _destroy( thread_mach_t* p )
    {
      rc_t rc = kOkRC;
      thread_t* t=p->threadL;
      while( t != nullptr )
      {
        thread_t* t0 = t->link;
        if((rc = destroy(t->thH)) != kOkRC )
        {
          rc = cwLogError(rc,"Thread destroy failed.");
          break;
        }

        mem::release(t);
        t = t0;
      }

      mem::release(p);

      return rc;
      
    }
    
  }  
}

cw::rc_t cw::thread_mach::create( handle_t& hRef, threadFunc_t threadFunc, void* contextArray, unsigned contexRecdByteN, unsigned threadN )
{
  rc_t rc;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  thread_mach_t* p  = mem::allocZ<thread_mach_t>();

  uint8_t* ctxA = reinterpret_cast<uint8_t*>(contextArray);
  
  for(unsigned i=0; i<threadN;  ++i)
  {
    void* arg = ctxA + (i*contexRecdByteN);

    if((rc = _add(p, threadFunc, arg, nullptr)) != kOkRC )
      goto errLabel;
  }

  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::thread_mach::add( handle_t h, threadFunc_t threadFunc, void* arg, const char* label )
{
  thread_mach_t* p = _handleToPtr(h);
  return _add(p,threadFunc,arg, label);
}

cw::rc_t cw::thread_mach::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  thread_mach_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;
}

cw::rc_t cw::thread_mach::start( handle_t h )
{
  rc_t rc = kOkRC;
  rc_t rc0;
  
  thread_mach_t* p = _handleToPtr(h);
  for(thread_t* t=p->threadL; t!=nullptr; t=t->link)
    if((rc0 = thread::unpause( t->thH )) != kOkRC )
      rc = cwLogError(rc0,"Thread start failed.");
  
  return rc;
}

cw::rc_t cw::thread_mach::stop( handle_t h )
{
  rc_t rc = kOkRC;
  rc_t rc0;
  
  thread_mach_t* p = _handleToPtr(h);
  for(thread_t* t=p->threadL; t!=nullptr; t=t->link)
    if((rc0 = thread::pause( t->thH, thread::kPauseFl | thread::kWaitFl )) != kOkRC )
      rc = cwLogError(rc0,"Thread stop failed.");
  
  return rc;
}

bool cw::thread_mach::is_shutdown( handle_t h )
{
  thread_mach_t* p = _handleToPtr(h);
  for(thread_t* t=p->threadL; t!=nullptr; t=t->link)
    if( thread::state(t->thH) != thread::kExitedThId )
      return false;

  return true;
}

//---------------------------------------------------------------------------------------------------
// thread_tasks
//


namespace cw
{
  namespace thread_tasks
  {
    struct thread_tasks_str;
    
    typedef struct task_thread_str
    {
      thread::handle_t         threadH;
      struct thread_tasks_str* owner;
      unsigned                 threadId;
    } task_thread_t;
    
    typedef struct thread_tasks_str
    {
      task_thread_t* threadA; // threadA[ threadN ] - arg. records for call to _threadFunc
      unsigned       threadN;
      
      task_t*        taskA;   // taskA[ taskN ] - list of user provided callbacks set by run
      unsigned       taskN;

      mutex::handle_t mutexH;
      bool            mutexLockFl;
      
      std::atomic<unsigned> next_task_idx;
      std::atomic<unsigned> done_cnt;
      
    } thread_tasks_t;

    thread_tasks_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,thread_tasks_t>(h); }
  
    rc_t _destroy( thread_tasks_t* p )
    {
      rc_t rc = kOkRC;
      if( p->threadN > 0 && p->threadA != nullptr )
      {
        for(unsigned i=0; i<p->threadN; ++i)
        {
          rc_t rc0;
          if((rc0 = thread::destroy(p->threadA[i].threadH)) != kOkRC )
          {
            cwLogError(rc0,"Task thread %i destroy failed.",i);
          }
        }

        if( p->mutexLockFl )
        {
          if((rc = mutex::unlock(p->mutexH)) != kOkRC )
            rc = cwLogError(rc,"Mutex unlock on thread tasks destroy failed.");
          else
            p->mutexLockFl = false;
        }
        
        if((rc = mutex::destroy(p->mutexH)) != kOkRC )
          rc = cwLogError(rc,"Thread tasks mutex destroy failed.");
      }
    
      mem::release(p->threadA);
      mem::release(p);
      return rc;
    }

    bool _threadFunc( void* arg )
    {
      rc_t            rc          = kOkRC;
      task_thread_t*  task_thread = (task_thread_t*)arg;
      thread_tasks_t* p           = task_thread->owner;

      // get the next available task
      unsigned nti = p->next_task_idx.fetch_add(1, std::memory_order_acq_rel);

      // if nti is a valid task index ...
      if( nti < p->taskN )
      {        
        // ... then execute the task
        task_t* task = p->taskA + nti;
        task->rc     = task->func( task->arg );

        // 
        unsigned done_cnt = p->done_cnt.fetch_add(1, std::memory_order_acq_rel);

        // if the last task is done
        if( done_cnt + 1 == p->taskN )
        {
          // By taking the lock here we guarantee that the the main thread is
          // waiting on the cond. var.. Without doing this we might get here
          // before the cond. var. is setup and the main thread will miss the signal.
          if((rc = mutex::lock(p->mutexH)) != kOkRC )
            cwLogError(rc,"Last task mutex lock failed.");
          else
          {
            mutex::unlock(p->mutexH);
          
            // signal the main thread that all tasks are done
            if((rc = signalCondVar( p->mutexH )) != kOkRC )
              rc = cwLogError(rc,"Thread tasks signal cond var failed.");
          }
          
          // all tasks are done - pause this thread
          thread::pause(task_thread->threadH,thread::kPauseFl);
        }

      }
      else  // ... otherwise pause the thread
      {
        // you are in the thread callback and so you can't wait - just signal the thread to pause
        thread::pause(task_thread->threadH,thread::kPauseFl);
        
      }

      return true;
    }
  }
}



cw::rc_t cw::thread_tasks::create(  handle_t& hRef, unsigned threadN )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  thread_tasks_t* p = mem::allocZ<thread_tasks_t>();
  const unsigned labelCharN = 255;
  char label[ labelCharN + 1 ];
  
  p->threadN = threadN;
  p->threadA = mem::allocZ<task_thread_t>(threadN);

  // Create a mutex for the run() blocking cond. var
  if((rc = mutex::create( p->mutexH )) != kOkRC )
  {
    rc = cwLogError(rc,"Thread tasks mutex failed.");
    goto errLabel;
  }

  // Lock the mutex so that it is locked on the first call to waitOnCondVar()
  if((rc = mutex::lock(p->mutexH)) != kOkRC )
  {
    rc = cwLogError(rc,"Thread tasks initial mutex lock failed.");
    goto errLabel;
  }

  p->mutexLockFl = true;
  
  for(unsigned i=0; i<threadN; ++i)
  {
    snprintf(label,labelCharN,"cw_task-%i",i);
    p->threadA[i].owner = p;
    p->threadA[i].threadId = i;

    // Threads are created in 'paused' mode
    if((rc = thread::create( p->threadA[i].threadH, _threadFunc, p->threadA + i, label )) != kOkRC )
    {
      rc = cwLogError(rc,"Task thread create %i failed.",i);
      goto errLabel;
    }
  }
  
  hRef.set(p);

errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::thread_tasks::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  thread_tasks_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;  
}

cw::rc_t cw::thread_tasks::run( handle_t h, task_t* taskA, unsigned taskN, unsigned timeOutMs )
{
  rc_t            rc            = kOkRC;
  thread_tasks_t* p             = _handleToPtr(h);
  unsigned        activeThreadN = std::min(p->threadN,taskN);
  p->taskA                      = taskA;
  p->taskN                      = taskN;
  
  p->done_cnt.store(0,std::memory_order_release);
  p->next_task_idx.store(0,std::memory_order_release);

  for(unsigned i=0; i<activeThreadN; ++i)
  {
    if((rc = thread::pause(p->threadA[i].threadH,0)) != kOkRC )
    {
      rc = cwLogError(rc,"Task thread %i start failed.",i);
      goto errLabel;
    }
  }

  /*
    This spinlock works and is very simple - but it uses up a core which
    may be assigned to one of the worker threads.
    If threads were given core affinities to avoid this scenario then
    the spin lock might be a better solution then the cond. var signaling.

    // block waiting for tasks to complete
  while(1)
  {
    // spin on done_cnt
    unsigned done_cnt = p->done_cnt.load(std::memory_order_acquire);
    if( done_cnt >= taskN )
    {
      //printf("DONE\n");
      break;
    }
  }
  */

  // block waiting for the the tasks to complete
  rc = waitOnCondVar(p->mutexH, false, timeOutMs );
  
  switch(rc)
  {
    case kOkRC: 
      // mutex is locked
      p->mutexLockFl = true;
      break;
        
    case kTimeOutRC:
      // mutex is unlocked
      p->mutexLockFl = false;
      cwLogWarning("Thread tasks timed out after %i ms.",timeOutMs);
      break;
        
    default:
      // mutex is unlocked
      p->mutexLockFl = false;
      rc = cwLogError(rc,"Thread tasks run error.");
  }

  /*
    // pause all the threads
    for(unsigned i=0; i<activeThreadN; ++i)
    {
      if((rc = thread::pause(p->threadA[i].threadH,thread::kPauseFl | thread::kWaitFl)) != kOkRC )
      {
        rc = cwLogError(rc,"Task thread %i post run pause failed.",i);
        goto errLabel;
      }
    }
  */
  
  // if run failed then pause to threads 
  if( rc != kOkRC )
  {
    // lock the mutex (if it isn't already)
    if( !p->mutexLockFl )
    {
      if((rc = mutex::lock(p->mutexH)) != kOkRC )
      {
        rc = cwLogError(rc,"Thread task lock mutex on error cleanup failed.");
        goto errLabel;
      }
      p->mutexLockFl = true;
    }
  }

errLabel:
  return rc;
}


namespace cw
{
  namespace thread_tasks
  {
    typedef struct test_task_str
    {
      std::atomic<unsigned> cnt;
    } test_task_t;
    
    rc_t testThreadFunc( void* arg )
    {
      test_task_t* t = (test_task_t*)arg;

      t->cnt.fetch_add(1,std::memory_order_relaxed);
      return kOkRC;
    }
    
  }
}

cw::rc_t cw::thread_tasks::test( const test::test_args_t& args )
{
  rc_t           rc      = kOkRC;
  const unsigned threadN = 15;
  const unsigned taskN   = 10;
  const unsigned execN   = 10;
  handle_t       ttH;

  test_task_t* test_taskA = mem::allocZ<test_task_t>(taskN);
  task_t*      taskA      = mem::allocZ<task_t>(taskN);

  for(unsigned i=0; i<taskN; ++i)
  {
    taskA[i].func = testThreadFunc;
    taskA[i].arg  = test_taskA + i;    
  }

  if((rc = create(  ttH, threadN )) != kOkRC )
  {
    rc = cwLogError(rc,"Thread tasks object create failed.");
    goto errLabel;
  }

  sleepMs(500);
  
  for(unsigned i=0; i<execN; ++i)
  {
    if((rc = run(ttH, taskA, taskN, 10000 )) != kOkRC )
    {
      rc = cwLogError(rc,"Thread tasks exec failed on iteration %i.",i);
      goto errLabel;      
    }
  }

  for(unsigned i=0; i<taskN; ++i)
    cwLogPrint("task:%i = %i\n",i,test_taskA[i].cnt.load());

errLabel:
  if((rc = destroy(ttH)) != kOkRC )
  {
    rc = cwLogError(rc,"Thread tasks object destroy failed.");
    goto errLabel;
  }

  mem::release(test_taskA);
  
  return rc;
}
