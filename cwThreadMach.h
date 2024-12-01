//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwThreadMach_H
#define cwThreadMach_H

namespace cw
{
  namespace thread_mach
  {
    typedef handle<struct thread_mach_str> handle_t;
    typedef thread::cbFunc_t               threadFunc_t;

    // Create a thread machine instance.
    // contextArray[ threadN ][ contextRecdByteN ] is an optional  blob consisting of 'threadN' records each of size 'contextRecdByteN'.
    // Each of the records then becomes the entity which is used as the 'arg' value in the callback for the first 'threadN' threads.
    rc_t create(  handle_t& hRef, threadFunc_t threadFunc=nullptr, void* contextArray=nullptr, unsigned contexRecdByteN=0, unsigned threadN=0 );
    rc_t destroy( handle_t& hRef );

    // Create an additional thread. Note that the additional thread will be started by the next
    // call to 'start()'.
    rc_t add(   handle_t h, threadFunc_t threadFunc, void* arg, const char* label );

    // Start all threads    
    rc_t start( handle_t h );
    
    // Stop all threads.
    rc_t stop(  handle_t h );

    // Check if all threads are shutdown.
    bool is_shutdown( handle_t h );
  }

  namespace thread_tasks
  {

    typedef handle<struct thread_tasks_str> handle_t;
    typedef thread::cbFunc_t                threadFunc_t;

    // Create a thread tasks machine with threadN records
    rc_t create(  handle_t& hRef, unsigned threadN );
    rc_t destroy( handle_t& hRef );

    typedef struct task_str
    {
      rc_t        (*func)(void* arg);
      void*        arg;
      rc_t         rc;
    } task_t;

    // timeOutMs is the count of milliseconds run will block while waiting
    // for all the tasks to complete.
    rc_t run( handle_t h, task_t* taskA, unsigned taskN, unsigned timeOutMs=100 );

    rc_t test( const test::test_args_t& args );
    
  }
}

#endif
