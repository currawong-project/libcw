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

#define TASK_LOG_FL (0)
#define TASK_LOG_RECD_CNT 100U

namespace cw
{
  namespace thread_tasks
  {
    struct thread_tasks_str;

    typedef struct task_log_str
    {
      unsigned task_cnt; 
    } task_log_t;

    typedef struct task_thread_str
    {
      thread::handle_t         threadH;
      struct thread_tasks_str* owner;
      unsigned                 threadId;
      
      task_log_t logA[ TASK_LOG_RECD_CNT ];
      unsigned   log_idx;
      
    } task_thread_t;
    
    typedef struct thread_tasks_str
    {
      task_thread_t* threadA; // threadA[ threadN ] - arg. records for call to _threadFunc
      unsigned       threadN;
      
      task_t*        taskA;   // taskA[ taskN ] - list of user provided callbacks set by run
      unsigned       taskN;

      mutex::handle_t mutexH;      // mutex associated with the cond. var. used to block the app thread
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
          if( TASK_LOG_FL )
          {
            printf("%i : ",i);
            for(unsigned j=0; j<TASK_LOG_RECD_CNT; ++j)
            {
              printf("%2i ",p->threadA[i].logA[j].task_cnt);
              //if( j>0 && j%100 == 0 )
              //  printf("\n");
            }
            printf("\n");
          }
          
          
          
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

        if( TASK_LOG_FL && (task_thread->log_idx < TASK_LOG_RECD_CNT) )
        {
          task_thread->logA[ task_thread->log_idx ].task_cnt += 1;          
        }

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

          task_thread->log_idx += 1;
          
          // all tasks are done - pause this thread
          thread::pause(task_thread->threadH,thread::kPauseFl);
        }

      }
      else  // ... otherwise pause the thread
      {
        task_thread->log_idx += 1;

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
    p->threadA[i].log_idx = 0;

    for(unsigned j=0; j<TASK_LOG_RECD_CNT; ++j)
      p->threadA[i].logA[j].task_cnt = 0;

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



//---------------------------------------------------------------------------------------------------
// thread_ftasks
//

#include <sys/syscall.h>
#include <linux/futex.h>

namespace cw
{
  namespace thread_ftasks
  {
    enum {
      kRunOpId,
      kExitOpId
    };
    
    struct thread_tasks_str;

    typedef struct task_log_str
    {
      std::atomic<unsigned> task_cnt; 
    } task_log_t;
    
    typedef struct thread_str
    {
      pthread_attr_t           attr;
      pthread_t                pthreadH;
      struct thread_tasks_str* p;
      char*                    label;
      bool                     created_fl;

      task_log_t logA[ TASK_LOG_RECD_CNT ];
      std::atomic<unsigned>   log_idx;
      
    } thread_t;

    
    typedef struct thread_tasks_str
    {
      std::atomic<int> thread_futex_var;
      std::atomic<int> app_futex_var;
      
      thread_t* threadA;
      unsigned  threadN;

      task_t*   taskA;
      unsigned  taskN;

      std::atomic<unsigned>   op_id;
      
      std::atomic<unsigned> next_task_idx;
      std::atomic<unsigned> done_cnt;
      
    } thread_tasks_t;

    thread_tasks_t* _handleToPtr( handle_t h )
    {
      return handleToPtr<handle_t,thread_tasks_t>(h);
    }
    
    int _futex_wait(std::atomic<int> *addr, int val)
    {      
      return syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val, NULL, NULL, 0);
    }

    int _futex_wait_1(std::atomic<int> *addr, int val)
    {
      int rc = 0;
      unsigned try_cnt = 0;
      while(1)
      {
        errno = 0;
        rc = syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val, NULL, NULL, 0);

        if( rc == 0 && addr->load() == val )
        {
          continue;
        }

        if( rc==-1 && errno == EAGAIN )
        {          
          ++try_cnt;
          addr->store(0);
          sleepMs(1);

          if( try_cnt < 10 )
            continue;

          cwLogError(kOpFailRC,"Futex wait retry failed.");
        }
        
        break;
      }

      return rc;
    }
    
    int _futex_wake(std::atomic<int> *addr, int count)
    {
      return syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
    }

    void _run_tasks( thread_t* t )
    {
      thread_tasks_t* p = t->p;
      
      do
      {        
        // get the next available task
        unsigned nti = p->next_task_idx.fetch_add(1, std::memory_order_acq_rel);

        // if nti is a valid task index ...
        if( nti >= p->taskN )
          break;
        
        // ... then execute the task
        task_t* task = p->taskA + nti;
        task->rc     = task->func( task->arg );

        if( TASK_LOG_FL )
          t->logA[ t->log_idx % TASK_LOG_RECD_CNT ].task_cnt++;          

        // 
        unsigned done_cnt = p->done_cnt.fetch_add(1, std::memory_order_acq_rel);

        // if the last task is done
        if( done_cnt + 1 == p->taskN )
        {
          p->app_futex_var.store(1);
          
          // unblock the app thread
          _futex_wake(&p->app_futex_var,1);
        }
        
      }while(1);

      // reset the task thread futex var to zero.
      p->thread_futex_var.store(0);
      
      t->log_idx++;
      
      
    }
    
    void* _thread_func( void* arg )
    {
      thread_t* t = (thread_t*)arg;
      unsigned op_id;

      t->created_fl = true;
      if( t->label != nullptr )
        pthread_setname_np(t->pthreadH, t->label);
      
      do
      {
        // Block here until 'thread_futex_var' is set to non-zero
        // the thread is awakened by the application.
        if( _futex_wait(&t->p->thread_futex_var, 0) == -1 )
        {
          if( errno == EAGAIN )
          {
            continue;
          }
          
          cwLogSysError(kOpFailRC,errno,"Worker thread futex wait failed.");
        }

        // Get the operation id set by the app.
        op_id = t->p->op_id.load();

        switch( op_id )
        {
          case kRunOpId:
            _run_tasks(t); // run as many tasks as possible
            break;
            
          case kExitOpId:
            break;
            
        }

        op_id = t->p->op_id.load();
        
      }while( op_id != kExitOpId );
      
      return nullptr;
    }

      
    rc_t _create_thread( thread_tasks_t* p, thread_t* t, unsigned thread_idx, unsigned cpu_affinity, const char* thread_prefix_label )
    {
      rc_t      rc    = kOkRC;
      int       sysRC = 0;
      cpu_set_t cpu_set;
      
      CPU_ZERO(&cpu_set);

      t->p = p;
      t->log_idx = 0;

      // create the thread label
      if( thread_prefix_label != nullptr )
      {
        t->label = mem::printf(t->label,"%s-%i",thread_prefix_label,thread_idx);
      }

      // initialize the thread attribute argument record
      if((sysRC = pthread_attr_init(&t->attr)) != 0)
      {
        rc = cwLogSysError(kOpFailRC,sysRC,"Thread attribute init failed.");
        goto errLabel;
      }

      if( cpu_affinity != kInvalidIdx )
      {
        CPU_SET( cpu_affinity, &cpu_set);

        // set the thread CPU affinity
        if((sysRC = pthread_attr_setaffinity_np(&t->attr, sizeof(cpu_set), &cpu_set)) != 0 )
        {
          rc = cwLogSysError(kOpFailRC,sysRC,"Thread CPU affinity set failed.");
          goto errLabel;
        }
      }

      // create the thread
      if((sysRC = pthread_create(&t->pthreadH, &t->attr, _thread_func, (void*)t )) != 0 )
      {
        rc = cwLogSysError(kOpFailRC,sysRC,"Thread create failed.");
        goto errLabel;
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"ftask thread create failed.");
      
      return rc;
        
    }

    rc_t _destroy( thread_tasks_t* p )
    {
      rc_t rc = kOkRC;

      // Wake-up the task threads and tell them to exit.
      p->op_id.store(kExitOpId);
      p->thread_futex_var.store(1);
      
      if( _futex_wake(&p->thread_futex_var, p->threadN) == -1 )
      {
        rc = cwLogError(kOpFailRC,"Futex wake failed.");
        goto errLabel;
      }

      // release the resource of each thread
      for(unsigned i=0; i<p->threadN; ++i)
        if( p->threadA[i].created_fl )
        {
          int sysRC;

          if( p->threadA[i].label == nullptr )
            printf("%i : ", i );
          else
            printf("%s : ",p->threadA[i].label);

          if( TASK_LOG_FL )
          {
            for(unsigned j=0; j<TASK_LOG_RECD_CNT; ++j)
              printf("%2i ",p->threadA[i].logA[j].task_cnt.load());
            printf("\n");
          }
          
          if((sysRC = pthread_join(p->threadA[i].pthreadH,NULL)) != 0 )
            rc = cwLogSysError(kOpFailRC,sysRC,"Thread join failed.");

          mem::release(p->threadA[i].label);
        }
      

      mem::release(p->threadA);
      mem::release(p);

    errLabel:
      return rc;
    }    
  }
}

 
cw::rc_t cw::thread_ftasks::create(  handle_t& hRef, unsigned threadN, const unsigned* cpu_affinityA, const char* thread_label_prefix )
{
  rc_t            rc = kOkRC;;
  thread_tasks_t* p  = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  p = mem::allocZ<thread_tasks_t>();

  p->threadA = mem::allocZ<thread_t>(threadN);
  p->threadN = threadN;

  p->thread_futex_var.store(0);
  p->app_futex_var.store(0);
  p->done_cnt.store(0);
  
  for(unsigned i=0; i<p->threadN; ++i)
  {
    unsigned cpu_affinity = kInvalidIdx;
    
    if( cpu_affinityA != nullptr )
      cpu_affinity = cpu_affinityA[i];
    
    if((rc = _create_thread( p, p->threadA + i, i, cpu_affinity, thread_label_prefix )) != kOkRC )
      goto errLabel;
  }

  hRef.set(p);
  
errLabel:
  if(rc != kOkRC )
     _destroy(p);
     
  return rc;
}

cw::rc_t cw::thread_ftasks::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if(!hRef.isValid())
    return rc;

  thread_tasks_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}

cw::rc_t cw::thread_ftasks::run( handle_t h, task_t* taskA, unsigned taskN, unsigned timeOutMs )
{
  rc_t rc = kOkRC;
  
  thread_tasks_t* p = _handleToPtr(h);
  
  p->app_futex_var.store(0);
  p->next_task_idx.store(0);
  p->done_cnt.store(0);
  
  p->taskA = taskA;
  p->taskN = taskN;
  
  p->op_id.store(kRunOpId);      // Tell the threads that they should enter 'run' mode.
  p->thread_futex_var.store(1);  // Change the value of the futex var to unblock the waiting threads

  
  // wake the threads
  if( _futex_wake(&p->thread_futex_var, p->threadN ) == -1 )
  {
    rc = cwLogError(kOpFailRC,"Thread pool wakeup failed.");
    goto errLabel;
  }

  // Put this thread into wait mode.
  // When the last task in p->taskA[] is completed
  // 'app_futex_var' is set to 1 and this thread is a awaken.
  
  // wait for the tasks to run
  if( _futex_wait(&p->app_futex_var, 0) == -1 )
  {
    // Under no-load the tasks will finish before the app thread waits.
    // In this case the p->app_futext_var will be 1 (not 0) and errno will be set to EAGAIN.
    // (See futex(7) FUTEX_WAIT).
    if( errno == EAGAIN && p->done_cnt.load() == p->taskN )
    {
      // All tasks finished before call to _futex_wait()
    }
    else
    {
      rc = cwLogSysError(kOpFailRC,errno,"App thread futex wait failed.");
      goto errLabel;
    }
  }
  
  
errLabel:
  return rc;
}

namespace cw
{
  namespace thread_ftasks
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

cw::rc_t cw::thread_ftasks::test( const test::test_args_t& args )
{
  rc_t           rc      = kOkRC;
  const unsigned threadN = 2;
  const unsigned taskN   = 50;
  const unsigned execN   = 20;
  handle_t       ttH;

  test_task_t* test_taskA = mem::allocZ<test_task_t>(taskN);
  task_t*      taskA      = mem::allocZ<task_t>(taskN);

  for(unsigned i=0; i<taskN; ++i)
  {
    taskA[i].func = testThreadFunc;
    taskA[i].arg  = test_taskA + i;    
  }

  if((rc = create(  ttH, threadN, nullptr, "test_thread" )) != kOkRC )
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
    sleepMs(1);
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
