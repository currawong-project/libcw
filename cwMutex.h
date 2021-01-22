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

    // Set 'lockThenWaitFl' if the function should lock the mutex prior to waiting.
    // If 'lockThenWaitFl' is false then the function assumes the mutex is already locked
    // and directly waits. If 'lockThenWaitFl' is set and the mutex is not already locked
    // then the result is undefined.
    //
    // Note that this function does NOT check for 'spurious wakeups' - when
    // the condition var. returns (w/ the mutex locked) even though it was
    // not acutally signals. If it matters to the application then it must
    // provide this logic.
    rc_t waitOnCondVar( handle_t h, bool lockThenWaitFl, unsigned timeOutMs );

    rc_t signalCondVar( handle_t h);
  }
}
  


#endif
