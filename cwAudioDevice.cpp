//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwAudioDevice.h"
#include "cwText.h"

namespace cw
{
  namespace audio
  {
    namespace device
    {
      typedef struct drv_str
      {
        unsigned        begIdx;
        unsigned        endIdx;
        driver_t*       drv;
        struct drv_str* link;
      } drv_t;
      
      typedef struct device_str
      {
        drv_t*   drvList    = nullptr;
        unsigned nextDrvIdx = 0;
      } device_t;

      inline device_t* _handleToPtr( handle_t h ){ return handleToPtr<handle_t,device_t>(h); }

      cw::rc_t _destroy( device_t* p )
      {
        drv_t* d = p->drvList;
        while( d != nullptr )
        {
          drv_t* d0 = d->link;
          mem::release(d);
          d = d0;
        }

        mem::release(p);
        
        return kOkRC;
      }

      drv_t* _indexToDriver( device_t* p, unsigned idx )
      {
        drv_t* l = p->drvList;
        for(; l != nullptr; l=l->link )
          if( l->begIdx <= idx && idx <= l->endIdx )
            return l;

        cwLogError(kInvalidArgRC,"An invalid audio device index (%) was requested.",idx);
        return nullptr;
      }

      drv_t* _indexToDriver( handle_t h, unsigned idx )
      {
        device_t* p = _handleToPtr(h);
        return _indexToDriver(p,idx);
      }
      
    }
  }  
}

cw::rc_t cw::audio::device::create(  handle_t& hRef )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC)
    return rc;

  device_t* p = mem::allocZ<device_t>();
  hRef.set(p);

  return rc;  
}

cw::rc_t cw::audio::device::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  device_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::rc_t cw::audio::device::registerDriver( handle_t h, driver_t* drv )
{
  device_t* p = _handleToPtr(h);

  unsigned n = drv->deviceCount( drv );
  if( n > 0 )
  {
    drv_t* d = mem::allocZ<drv_t>();
  
    d->begIdx      = p->nextDrvIdx;
    p->nextDrvIdx += n;
    d->endIdx      = p->nextDrvIdx-1;
    d->drv         = drv;
    d->link        = p->drvList;
    p->drvList     = d;    
  }
  
  return kOkRC;
}

unsigned cw::audio::device::count( handle_t h )
{
  device_t* p = _handleToPtr(h);
  return p->nextDrvIdx;  
}

unsigned      cw::audio::device::labelToIndex( handle_t h, const char* label )
{
  unsigned n = count(h);
  unsigned i;
  for(i=0; i<n; ++i)
  {
    const char* s = device::label(h,i);
    if( textCompare(s,label)==0)
      return i;
  }
  return kInvalidIdx;
}


const char* cw::audio::device::label( handle_t h, unsigned devIdx )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceLabel( d->drv, devIdx - d->begIdx );
  return nullptr;
}

unsigned cw::audio::device::channelCount( handle_t h, unsigned devIdx, bool inputFl )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceChannelCount( d->drv, devIdx - d->begIdx, inputFl );
  return 0;
}

double cw::audio::device::sampleRate( handle_t h, unsigned devIdx )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceSampleRate( d->drv, devIdx - d->begIdx );
  return 0;
}

unsigned cw::audio::device::framesPerCycle( handle_t h, unsigned devIdx, bool inputFl )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceFramesPerCycle( d->drv, devIdx - d->begIdx, inputFl );
  return 0;
}

cw::rc_t  cw::audio::device::setup( handle_t h, unsigned devIdx, double sr, unsigned frmPerCycle, cbFunc_t cb, void* cbData )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceSetup( d->drv, devIdx - d->begIdx, sr, frmPerCycle, cb, cbData, devIdx );
  return kInvalidArgRC;
}

cw::rc_t  cw::audio::device::start( handle_t h, unsigned devIdx )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceStart( d->drv, devIdx - d->begIdx );
  return kInvalidArgRC;
}

cw::rc_t  cw::audio::device::stop( handle_t h, unsigned devIdx )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceStop( d->drv, devIdx - d->begIdx );
  return kInvalidArgRC;
}

bool  cw::audio::device::isStarted(handle_t h, unsigned devIdx )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    return d->drv->deviceIsStarted( d->drv, devIdx - d->begIdx );
  return false;  
}


cw::rc_t cw::audio::device::execute(        handle_t h, unsigned devIdx )
{
  rc_t rc = kOkRC;
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) == nullptr )
    rc = kInvalidArgRC;
  else
    rc = d->drv->deviceExecute( d->drv, devIdx - d->begIdx );

  return rc;
}

cw::rc_t  cw::audio::device::enable(         handle_t h, unsigned devIdx, bool inputFl, bool enableFl )
{
  rc_t rc = kOkRC;
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) == nullptr )
    rc = kInvalidArgRC;
  else
    rc = d->drv->deviceEnable( d->drv, devIdx - d->begIdx, inputFl, enableFl );

  return rc;
}

cw::rc_t  cw::audio::device::seek(           handle_t h, unsigned devIdx, bool inputFl, unsigned frameOffset )
{
  rc_t rc = kOkRC;
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) == nullptr )
    rc = kInvalidArgRC;
  else
    rc = d->drv->deviceSeek( d->drv, devIdx - d->begIdx, inputFl, frameOffset );
  
  return rc;  
}

void cw::audio::device::realTimeReport( handle_t h, unsigned devIdx )
{
  drv_t* d;
  if((d = _indexToDriver(h,devIdx)) != nullptr )
    d->drv->deviceRealTimeReport( d->drv, devIdx - d->begIdx );
}

void cw::audio::device::report( handle_t h )
{
  for(unsigned i=0; i<count(h); ++i)
  {
    cwLogInfo( "%8.1f in:%i (%i) out:%i (%i) %s",
      sampleRate(h,i),
      channelCount(h,i,true), framesPerCycle(h,i,true),
      channelCount(h,i,false), framesPerCycle(h,i,false),
      label(h,i));
  }
  
}

void cw::audio::device::realTimeReport( handle_t h )
{
  for(unsigned i=0; i<count(h); ++i)
  {
    realTimeReport(h,i);
  }
  
}

