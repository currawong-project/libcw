#ifndef cwMutex_H
#define cwMutex_H

namespace cw
{
  namespace mutex
  {
    typedef handle<struct mutex_str> handle_t;

    rc_t create(  handle_t& h );
    rc_t destroy( handle_t& h );

    rc_t tryLock( handle_t h, bool& lockFlRef );
    rc_t lock(    handle_t h );
    rc_t unlock(  handle_t h );

    rc_t waitOnCondVar( handle_t h, bool lockThenWaitFl, unsigned timeOutMs );

  }
}
  


#endif
