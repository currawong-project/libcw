#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwAudioDeviceDecls.h"
#include "cwAudioDevice.h"

namespace cw
{
  namespace audio
  {
    namespace device
    {
      namespace file      
      {
        enum {
          kInFl  = 0x01,
          kOutFl = 0x02
        };
        
        typedef struct dev_str
        {
          char*               label;
          audioPacket_t       pkt;
          float*              pktAudioBuf;
          
          unsigned            framesPerCycle;
          double              srate;

          char*               iInFname;
          audiofile::handle_t iFileH;
          unsigned            iFlags;

          char*               oInFname;
          audiofile::handle_t oFileH;
          unsigned            oFlags;
          unsigned            oChCnt;
          unsigned            oBitsPerSample;
          
          struct dev_str*     link;
        } dev_t;
        
        typedef struct dev_mgr_str
        {
          driver_t* driver;
          dev_t*    list;
        } dev_mgr_t;

        dev_mgr_t* _handleToPtr( handle_t h )
        {  return handleToPtr<handle_t,dev_mgr_t>(h); }

        dev_mgr_t* _driverToPtr( driver_t* drvr )
        { return (dev_mgr_t*)drvr->drvArg; }

        dev_t* _labelToDev( dev_mgr_t* p, const char* label )
        {
          dev_t* d = p->label;
          for(; d!=nullptr; d=d->link)
            if( textCompare(d->label,label) == 0 )
              return d;

          return nullptr;          
        }

        dev_t* _indexToDev( dev_mgr_t* p, unsigned devIdx )
        {
          unsigned i = 0;
          dev_t*   d = p->dev;
          
          for(; d!=nullptr; d=d->link)
          {
            if( i == devIdx )
              return d;
            ++i;
          }
        }

        rc_t _indexToDev( dev_mgr_t* p, unsigned devIdx, dev_t*& devPtrRef )
        {
          rc_t rc = kOkRC;
          
          if((devPtrRef = _indexToDev(p,devIdx)) == nullptr )
            rc = cwLogError(kInvalidArgRC,"The audio file device index %i is invalid.",devIdx);
          
          return rc;
        }

        dev_t* _findOrCreateDev( dev_mgr_t* p, const char* label )
        {
          dev_t* d;
          if((d = _labelToDev(p,label)) != nullptr )
            return d;
          
          d = mem::allocZ<dev_t>();
          d->label = mem::duplStr(label);

          if(d0 == nullptr)
            p->list = d;
          else
            d0->link = d;
          
          return d;          
        }

        void _close_input( dev_t* d )
        {
          if( d->iFileH.isValid() )
          {
            close(d->iFileH);
            d->israte = 0;
            d->iFramesPerCycle = 0;
          }
        }

        rc_t _open_input( dev_t* d )
        {
          rc_t rc;
          audiofile::handle_t iFileH;
          
          // open the requested audio flie
          if((rc = open( iFileH, audioInFile, &info )) != kOkRC )
          {
            rc = cwLogError(rc,"Audio file device open failed on '%s'.",cwStringNullGuard(audioInFile));
            goto errLabel;
          }

          // if the device input file is already open - then close it
          if( d->iFileH.isValid() )
          {
            cwLogWarning("The audio file device '%s' input file '%s' has been closed prior to opening '%s'.",name(d->iFileH),cwStringNullGuard(audioInFile));
            _close_input(d);
          }
          
        }
        

        void _close_output( dev_t* d )
        {
          if( d->oFileH.isValid() )
          {
            close(d->oFileH);
            d->osrate = 0;
            d->oFramesPerCycle = 0;
          }
        }

        rc_t _open_output( dev_t* d )
        {
          audiofile::handle_t oFileH;
          
          // open the requested audio flie
          if((rc = create( oFileH, audioOutFile, srate, bitsPerSample, chN )) != kOkRC )
          {
            rc = cwLogError(rc,"Audio file device open failed on '%s'.",cwStringNullGuard(audioInFile));
            goto errLabel;
          }
          
        }
        
        rc_t _destroy( dev_mgr_t* p )
        {
          rc_t rc = kOkRC;
          dev_t* d = p->list;
          while( d != nullptr )
          {
            dev_t* d0 = d->link;
            _close_input(d);
            _close_output(d);
            mem::release(d->label);
            mem::release(d);
            d = d0;
            
          }          
          return rc;
        }

        
        
      }
    }
  }
}


cw::rc_t    cw::audio::device::file::create( handle_t& hRef, struct driver_str*& drvRef )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  dev_mgr_t* p = mem::allocZ<dev_mgr_t>();

  p->driver.drvArg = p;
  p->driver.deviceCount          = deviceCount;
  p->driver.deviceLabel          = deviceLabel;
  p->driver.deviceChannelCount   = deviceChannelCount;
  p->driver.deviceSampleRate     = deviceSampleRate;
  p->driver.deviceFramesPerCycle = deviceFramesPerCycle;
  p->driver.deviceSetup          = deviceSetup;
  p->driver.deviceStart          = deviceStart;
  p->driver.deviceStop           = deviceStop;
  p->driver.deviceIsStarted      = deviceIsStarted;
  p->driver.deviceRealTimeReport = deviceRealTimeReport;
  
  drvRef = &p->driver;

  hRef.set(p);

  return rc;
}
  
cw::rc_t    cw::audio::device::file::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  dev_mgr_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();

  return rc;  
}

unsigned    cw::audio::device::file::deviceCount(          struct driver_str* drv)
{
  dev_mgr_t* p = _driverToPtr(drv);
  dev_t* d = p->list;
  for(; d!=nullptr; d=d->link)
    ++n;
  return n;
}

const char* cw::audio::device::file::deviceLabel(          struct driver_str* drv, unsigned devIdx )
{
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return nullptr;

  return d->label;
}

unsigned    cw::audio::device::file::deviceChannelCount(   struct driver_str* drv, unsigned devIdx, bool inputFl )
{
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return 0;

  return channelCount( inputFl ? d->iFileH : d->oFileH );
}

double      cw::audio::device::file::deviceSampleRate(     struct driver_str* drv, unsigned devIdx )
{
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return 0;

  return inputFl ? d->israte : d->osrate;
}

unsigned    cw::audio::device::file::deviceFramesPerCycle( struct driver_str* drv, unsigned devIdx, bool inputFl )
{
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return nullptr;

  return inputFl ? d->iFramesPerCycle : d->oFramesPerCycle;
  
}

cw::rc_t    cw::audio::device::file::deviceSetup(          struct driver_str* drv, unsigned devIdx, double sr, unsigned frmPerCycle, cbFunc_t cbFunc, void* cbArg )
{
  assert(0); // not yet implemented
}

cw::rc_t    cw::audio::device::file::deviceStart(          struct driver_str* drv, unsigned devIdx )
{
  rc_t rc0 = kOkRC;
  rc_t rc1 = kOkRC;
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc0 = _indexToDev( p, devIdx, d )) == kOkRC )
  {
    if( d->iFileH.isValid() && cwIsSet(d->iFlags,kRewindOnStartFl) )
      if((rc0 = seek(d->iFileH,0)) != kOkRC )
        rc0 = cwLogError(rc0,"Rewind on start failed on the audio device file input file.");

    if( d->oFileH.isValid() && cwIsSet(d->oFlags,kRewindOnStartFl) )    
      if((rc1 = seek(d->oFileH,0)) != kOkRC )
        rc1 = cwLogError(rc1,"Rewind on start failed on the audio device file output file.");
  }
  
  return rcSelect(rc0,rc1);  
}

cw::rc_t    cw::audio::device::file::deviceStop(           struct driver_str* drv, unsigned devIdx )
{
  rc_t       rc = kOkRC;
  dev_t*     d  = nullptr;
  dev_mgr_t* p  = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) == kOkRC )
    if( d->oFileH.isValid() )
      if((rc = flush(d->ofileH)) != kOkRC )
        rc = cwLogError(rc,"Flush on stop failed on the audio device file output file.");

  return rc;
}

bool        cw::audio::device::file::deviceIsStarted(      struct driver_str* drv, unsigned devIdx )
{
}

void        cw::audio::device::file::deviceRealTimeReport( struct driver_str* drv, unsigned devIdx )
{
}

cw::rc_t    cw::audio::device::file::createInDevice(  handle_t& h, const char* label, const char* audioInFile,  unsigned framesPerCycle, unsigned flags )
{
  rc_t       rc;
  handle_t   iFileH;
  info_t     info;
  dev_t*     d;  
  dev_mgr_t* p = _handleToPtr(hRef);
  
  // create or find a device
  if((d = _findOrCreateDev(p, label )) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Audio file device create failed.");
    goto errLabel;
  }

  d->iFname  = mem::duplStr(audioInFile);
  d->iFlags  = flags;
  
 errLabel:
      
  return rc;
}

cw::rc_t    cw::audio::device::file::createOutDevice( handle_t& h, const char* label, const char* audioOutFile, unsigned flags, unsigned chCnt, unsigned bitsPerSample )
{
  rc_t       rc;
  dev_t*     d;  
  dev_mgr_t* p = _handleToPtr(hRef);
  
  // create or find a device
  if((d = _findOrCreateDev(p, label )) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Audio file device create failed.");
    goto errLabel;
  }

  d->oFname         = mem::duplStr(audioOutFile);
  d->oFlags         = flags;
  d->oChCnt         = chCnt;
  d->oBitsPerSample = bitsPerSample;
  
 errLabel:
  
  return rc;  
}

cw::rc_t    cw::audio::device::file::deviceExec( handle_t& h, unsigned devIdx )
{
}

cw::rc_t    cw::audio::device::file::report(handle_t h )
{
}

cw::rc_t    cw::audio::device::file::report()
{
}
