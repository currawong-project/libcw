#ifndef cwThreadMach_H
#define cwThreadMach_H

namespace cw
{
  namespace thread_mach
  {
    typedef handle<struct thread_mach_str> handle_t;
    typedef thread::cbFunc_t               threadFunc_t;

    rc_t create(  handle_t& hRef, threadFunc_t threadFunc=nullptr, void* contextArray=nullptr, unsigned contexRecdByteN=0, unsigned threadN=0 );
    rc_t destroy( handle_t& hRef );
    rc_t add(   handle_t h, threadFunc_t threadFunc, void* arg );
    rc_t start( handle_t h );
    rc_t stop(  handle_t h );
    bool is_shutdown( handle_t h );
  }
}

#endif
