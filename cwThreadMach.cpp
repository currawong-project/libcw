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
      void*            arg;
    } thread_t;
    
    typedef struct thread_mach_str
    {
      thread_t* threadA;
      unsigned  threadN;
    } thread_mach_t;

    thread_mach_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,thread_mach_t>(h); }

    rc_t _destroy( thread_mach_t* p )
    {
      rc_t rc = kOkRC;
      
      for(unsigned i=0; i<p->threadN; ++i)
      {
        if((rc = destroy(p->threadA[i].thH)) != kOkRC )
        {
          rc = cwLogError(rc,"Thread at index %i destroy failed.",i);
          break;
        }
      }

      mem::release(p->threadA);
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
  p->threadA        = mem::allocZ<thread_t>(threadN);
  p->threadN        = threadN;

  uint8_t* ctxA = static_cast<uint8_t*>(contextArray);
  
  for(unsigned i=0; i<threadN;  ++i)
  {
    p->threadA[i].arg = ctxA + (i*contexRecdByteN);

    if((rc = thread::create(p->threadA[i].thH, threadFunc, p->threadA[i].arg )) != kOkRC )
    {
      rc = cwLogError(rc,"Thread at index %i create failed.",i);
      goto errLabel;
    }
  }

  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
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
  for(unsigned i=0; i<p->threadN; ++i)
    if((rc0 = thread::unpause( p->threadA[i].thH )) != kOkRC )
      rc = cwLogError(rc0,"Thread at index %i start failed.", i );
  
  return rc;
}

cw::rc_t cw::thread_mach::stop( handle_t h )
{
  rc_t rc = kOkRC;
  rc_t rc0;
  
  thread_mach_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->threadN; ++i)
    if((rc0 = thread::pause( p->threadA[i].thH, thread::kPauseFl | thread::kWaitFl )) != kOkRC )
      rc = cwLogError(rc0,"Thread at index %i stop failed.", i );
  
  return rc;
}
