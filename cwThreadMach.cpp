#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
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
