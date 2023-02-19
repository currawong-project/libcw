#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwMutex.h"
#include "cwTime.h"

#include <pthread.h>

namespace cw
{
  namespace mutex
  {
    typedef struct mutex_str
    {
      pthread_mutex_t mutex;
      pthread_cond_t  cvar;
    } mutex_t;
  

    mutex_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,mutex_t>(h); }
  
    rc_t _destroy( mutex_t* p )
    {
      rc_t rc = kOkRC;
      int sysRC;
    
      if((sysRC = pthread_cond_destroy(&p->cvar)) != 0)
        rc = cwLogSysError(kObjFreeFailRC,sysRC,"Thread condition var. destroy failed.");
      else
        if((sysRC = pthread_mutex_destroy(&p->mutex)) != 0)
          rc = cwLogSysError(kObjFreeFailRC,sysRC,"Thread mutex destroy failed.");
        else
          mem::release(p);
    
      return rc;    
    }
  }
}

cw::rc_t cw::mutex::create(  handle_t& h )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  mutex_t* p = mem::allocZ<mutex_t>();
  int      sysRC;
  
  if((sysRC = pthread_mutex_init(&p->mutex,NULL)) != 0 )
    cwLogSysError(kObjAllocFailRC,sysRC,"Thread mutex create failed.");
  else
    if((sysRC = pthread_cond_init(&p->cvar,NULL)) != 0 )
      cwLogSysError(kObjAllocFailRC,sysRC,"Thread Condition var. create failed.");

  h.set(p);
  
  return rc;
}

cw::rc_t cw::mutex::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid() )
    return rc;

  mutex_t* p = _handleToPtr(h);
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}

cw::rc_t cw::mutex::tryLock( handle_t h, bool& lockFlRef )
{
  rc_t     rc     = kOkRC;
  mutex_t* p      = _handleToPtr(h);
  int      sysRc  = pthread_mutex_trylock(&p->mutex);

  switch(sysRc)
  {
    case EBUSY:
      lockFlRef = false;
      break;

    case 0:
      lockFlRef = true;
      break;

    default:
      rc = cwLogSysError(kInvalidOpRC,sysRc,"Thread mutex try-lock failed.");
  }
  
  return rc;
}

cw::rc_t cw::mutex::lock( handle_t h, unsigned timeout_milliseconds )
{
  rc_t     rc = kOkRC;
  mutex_t* p  = _handleToPtr(h);
  int      sysRc;
  time::spec_t ts;

  time::get(ts);
  time::advanceMs(ts,timeout_milliseconds);
  
  
  switch(sysRc = pthread_mutex_timedlock(&p->mutex,&ts) )
  {
    case 0:
      rc = kOkRC;
      break;
      
    case ETIMEDOUT:
      rc = kTimeOutRC;
      break;
      
    default:
      rc = cwLogSysError(kInvalidOpRC,sysRc,"Lock failed.");
  }
  
  return rc;  
  
}

cw::rc_t cw::mutex::lock(    handle_t h )
{
  rc_t     rc = kOkRC;
  mutex_t* p  = _handleToPtr(h);
  int      sysRc;
  
  if((sysRc = pthread_mutex_lock(&p->mutex)) != 0)
    rc = cwLogSysError(kInvalidOpRC,sysRc,"Lock failed.");

  return rc;  
}

cw::rc_t cw::mutex::unlock(  handle_t h )
{
  rc_t     rc = kOkRC;
  mutex_t* p  = _handleToPtr(h);
  int      sysRc;
  
  if((sysRc = pthread_mutex_unlock(&p->mutex)) != 0)
    return cwLogSysError(kInvalidOpRC,sysRc,"Unlock failed.");

  return rc;  
}
  
cw::rc_t cw::mutex::waitOnCondVar( handle_t h, bool lockThenWaitFl, unsigned timeOutMs )
{
  rc_t     rc = kOkRC;
  mutex_t* p  = _handleToPtr(h);
  int      sysRC;
  
  if( lockThenWaitFl )
    if((sysRC = pthread_mutex_lock(&p->mutex)) != 0)
      return cwLogSysError(kInvalidOpRC,sysRC,"Lock failed.");

  // if no timeout was given ....
  if( timeOutMs == 0)
  {
    // ... then wait until the cond-var is triggered
    if((sysRC = pthread_cond_wait(&p->cvar,&p->mutex)) != 0 )
      return cwLogSysError(kInvalidOpRC,sysRC,"Thread cond. var. wait failed.");
  }
  else  // ... otherwise use the cond. var. wait with timeout API
  {

    struct timespec ts;
    
    if((rc = time::futureMs(ts,timeOutMs)) == kOkRC )    
      if((sysRC = pthread_cond_timedwait(&p->cvar,&p->mutex,&ts)) != 0 )
      {
        if( sysRC == ETIMEDOUT )
          rc = kTimeOutRC;
        else
          return cwLogSysError(kInvalidOpRC,sysRC,"Thread cond. var. wait failed.");
      }
  }

  return rc;
  
  
}

cw::rc_t cw::mutex::signalCondVar( handle_t h)
{

  rc_t     rc = kOkRC;
  mutex_t* p  = _handleToPtr(h);

  int sysRC;
  if((sysRC = pthread_cond_signal(&p->cvar)) != 0 )
    rc = cwLogSysError(kOpFailRC,sysRC,"Thread cond var. signaling failed.");
  
  return rc;
}

