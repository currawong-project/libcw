#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwFile.h"
#include "cwThread.h"
#include "cwMutex.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwAudioFile.h"
#include "cwAudioDeviceDecls.h"
#include "cwAudioDevice.h"
#include "cwAudioDeviceFile.h"
#include "cwVectOps.h"

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
          
          std::atomic_uint    readyCnt;
          
          unsigned            framesPerCycle;
          double              srate;

          cbFunc_t            cbFunc;
          void*               cbArg;
          unsigned            cbDevIdx;
          
          bool                isStartedFl;

          char*               iFname;
          audiofile::handle_t iFileH;
          unsigned            iFlags;
          unsigned            iChCnt;
          bool                iEnableFl;
          audioPacket_t       iPkt;
          float*              iPktAudioBuf;
          unsigned            iCbCnt;
          float**             iChArray;  // iChArray[2]
          float*              iChSmpBuf; //
          unsigned            iErrCnt;   // count of errors
          unsigned            iFrmCnt;   // count of frames read
          
          char*               oFname;
          audiofile::handle_t oFileH;
          unsigned            oFlags;
          unsigned            oChCnt;
          bool                oEnableFl;
          unsigned            oBitsPerSample;
          audioPacket_t       oPkt;
          float*              oPktAudioBuf;
          unsigned            oCbCnt;
          float**             oChArray;  // oChArray[2]
          float*              oChSmpBuf; //
          unsigned            oErrCnt;   // count of errors
          unsigned            oFrmCnt;   // count of frames written
                    
          struct dev_str*     link;
        } dev_t;
        
        typedef struct dev_mgr_str
        {
          driver_t         driver;
          dev_t*           list;
          unsigned         threadTimeOutMs;
          unsigned         threadCbCnt;
          thread::handle_t threadH;
          mutex::handle_t  mutexH;
          
        } dev_mgr_t;

        dev_mgr_t* _handleToPtr( handle_t h )
        {  return handleToPtr<handle_t,dev_mgr_t>(h); }

        dev_mgr_t* _driverToPtr( driver_t* drvr )
        { return (dev_mgr_t*)drvr->drvArg; }

        dev_t* _labelToDev( dev_mgr_t* p, const char* label )
        {
          dev_t* d = p->list;
          for(; d!=nullptr; d=d->link)
            if( textCompare(d->label,label) == 0 )
              return d;

          return nullptr;          
        }

        dev_t* _indexToDev( dev_mgr_t* p, unsigned devIdx )
        {
          unsigned i = 0;
          dev_t*   d = p->list;
          
          for(; d!=nullptr; d=d->link)
          {
            if( i == devIdx )
              return d;
            ++i;
          }

          return nullptr;
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

          if(p->list == nullptr)
            p->list = d;
          else
          {
            // set d0 to the last dev on the list
            dev_t* d0 = p->list;
            while( d0->link != nullptr )
              d0 = d0->link;
            
            d0->link = d;
          }
          
          return d;          
        }

        void _devReport( dev_t* d )
        {
          cwLogInfo("%s : FpC:%i sr:%f",cwStringNullGuard(d->label),d->framesPerCycle,d->srate);
          
          if( d->iFileH.isValid() )
            cwLogInfo(" in:  flags:0x%x ch:%i sr:%f : %s", d->iFlags,channelCount(d->iFileH),sampleRate(d->iFileH),cwStringNullGuard(d->iFname));
          
          if( d->oFileH.isValid() )
            cwLogInfo(" out: flags:0x%x ch:%i sr:%f : %s", d->oFlags,d->oChCnt,d->srate,cwStringNullGuard(d->oFname));
        }

        void _close_input( dev_t* d )
        {
          if( d->iFileH.isValid() )
          {
            close(d->iFileH);
            d->iCbCnt = 0;
            mem::release(d->iFname);
            mem::release(d->iPktAudioBuf);
            mem::release(d->iChArray);
            mem::release(d->iChSmpBuf);
          }
        }

        rc_t _open_input( dev_t* d, unsigned devIdx )
        {
          rc_t rc = kOkRC;
          audiofile::handle_t iFileH;
          audiofile::info_t info;

          if( d->iFname == nullptr )
            return rc;
                      
          // open the requested audio flie
          if((rc = open( iFileH, d->iFname, &info )) != kOkRC )
          {
            rc = cwLogError(rc,"Audio file device open failed on '%s'.",cwStringNullGuard(d->iFname));
            goto errLabel;
          }

          // if the device input file is already open - then close it
          if( d->iFileH.isValid() )
          {
            cwLogWarning("The audio file device '%s' input file '%s' has been closed prior to opening '%s'.",name(d->iFileH),cwStringNullGuard(d->iFname));
            _close_input(d);
          }

          if( info.srate != d->srate )
            cwLogWarning("The audio file sample rate (%f) does not match device sample rate (%f).",info.srate,d->srate);

          d->iFileH              = iFileH;
          d->iPktAudioBuf        = mem::resize<sample_t>(d->iPktAudioBuf,d->framesPerCycle*info.chCnt);
          d->iChArray            = mem::resize<sample_t*>(d->iChArray,info.chCnt);
          d->iChSmpBuf           = mem::resize<sample_t>(d->iChSmpBuf, d->framesPerCycle*info.chCnt );
          d->iChCnt              = info.chCnt;
          d->iEnableFl           = true;
          d->iErrCnt             = 0;
          d->iFrmCnt             = 0;
          d->iPkt.devIdx         = d->cbDevIdx;
          d->iPkt.begChIdx       = 0;
          d->iPkt.chCnt          = info.chCnt;
          d->iPkt.audioFramesCnt = d->framesPerCycle;
          d->iPkt.bitsPerSample  = sizeof(sample_t);
          d->iPkt.flags          = kInterleavedApFl | kFloatApFl;
          d->iPkt.audioBytesPtr  = d->iPktAudioBuf;
          d->iPkt.cbArg          = d->cbArg;
          
          for(unsigned i=0; i<d->iChCnt; ++i)
            d->iChArray[i] = d->iChSmpBuf + i*d->framesPerCycle;
          
        errLabel:
          return rc;
        }

        rc_t _read_input( dev_t* d )
        {
          rc_t rc = kOkRC;

          unsigned actualFrameCnt = 0;

          if( !d->iEnableFl )
          {
            memset(d->iPkt.audioBytesPtr,0,d->iChCnt*d->framesPerCycle*sizeof(sample_t));
          }
          else
          {          
            // read the file
            if((rc = readFloat(d->iFileH, d->framesPerCycle, 0, d->iChCnt, d->iChArray, &actualFrameCnt)) == kOkRC )
              d->iFrmCnt += actualFrameCnt;
            else
            {
              rc = cwLogError(rc,"File read failed on audio device file: %s.",cwStringNullGuard(d->label));
              d->iErrCnt += 1;
              goto errLabel;
            }

            // interleave into the iPkt audio buffer
            vop::interleave( d->iPktAudioBuf, d->iChSmpBuf, actualFrameCnt, d->iChCnt );

            if( actualFrameCnt < d->framesPerCycle )
            {
              for(unsigned i=0; i<d->iChCnt; ++i)
                vop::fill( d->iPktAudioBuf + (actualFrameCnt * d->iChCnt) + i, 0, d->framesPerCycle-actualFrameCnt, d->iChCnt );            
            }
          }
        errLabel:
          return rc;
        }

        void _close_output( dev_t* d )
        {
          if( d->oFileH.isValid() )
          {
            close(d->oFileH);
            d->oCbCnt = 0;
            mem::release(d->oFname);
            mem::release(d->oPktAudioBuf);
            mem::release(d->oChArray);
            mem::release(d->oChSmpBuf);
          }
        }

        rc_t _open_output( dev_t* d, unsigned devIdx )
        {
          rc_t rc = kOkRC;
          audiofile::handle_t oFileH;

          if( d->oFname == nullptr )
            return rc;
          
          // open the requested audio output flie
          if((rc = create( oFileH, d->oFname, d->srate, d->oBitsPerSample, d->oChCnt )) != kOkRC )
          {
            rc = cwLogError(rc,"Audio file device open failed on '%s'.",cwStringNullGuard(d->oFname));
            goto errLabel;
          }

          d->oFileH              = oFileH;
          d->oPktAudioBuf        = mem::resize<sample_t>(d->oPktAudioBuf,d->framesPerCycle*d->oChCnt);
          d->oChArray            = mem::resize<sample_t*>(d->oChArray,d->oChCnt);
          d->oChSmpBuf           = mem::resize<sample_t>(d->oChSmpBuf, d->framesPerCycle*d->oChCnt );
          d->oEnableFl           = true;
          d->oFrmCnt             = 0;
          d->oErrCnt             = 0;
          d->oPkt.devIdx         = d->cbDevIdx;
          d->oPkt.begChIdx       = 0;
          d->oPkt.chCnt          = d->oChCnt;
          d->oPkt.audioFramesCnt = d->framesPerCycle;
          d->oPkt.bitsPerSample  = 0;
          d->oPkt.flags          = kInterleavedApFl | kFloatApFl;
          d->oPkt.audioBytesPtr  = d->oPktAudioBuf;
          d->oPkt.cbArg          = d->cbArg;

          for(unsigned i=0; i<d->oChCnt; ++i)
            d->oChArray[i] = d->oChSmpBuf + i*d->framesPerCycle;

        errLabel:
          return rc;
        }

        rc_t _write_output( dev_t* d )
        {
          rc_t rc = kOkRC;

          if( d->oFileH.isValid() && d->oEnableFl )
          {
            /*
            // deinterleave the audio into d->oChArray[]
            vop::deinterleave( d->oChSmpBuf, d->oPktAudioBuf, d->framesPerCycle, d->oChCnt );

            // write the audio
            if((rc = writeFloat( d->oFileH, d->framesPerCycle, d->oChCnt, d->oChArray )) == kOkRC )
            */
            
            if((rc = writeFloatInterleaved( d->oFileH, d->framesPerCycle, d->oChCnt, d->oPktAudioBuf )) == kOkRC )            
              d->oFrmCnt += d->framesPerCycle;
            else
            {
              rc = cwLogError(rc,"Audio device file write failed on device '%s'.",d->label);
              d->oErrCnt += 1;
              goto errLabel;
            }
          }

        errLabel:
          return rc;
        }

        rc_t _update_device( dev_t* d )
        {
          rc_t           rc0   = kOkRC;
          rc_t           rc1   = kOkRC;
          
          audioPacket_t* iPkt  = nullptr;
          unsigned       iPktN = 0;
          audioPacket_t* oPkt  = nullptr;
          unsigned       oPktN = 0;
          
          if( d->iFileH.isValid() )
          {
            iPkt       = &d->iPkt;
            iPktN      = 1;
            d->iCbCnt += 1;
            rc0        = _read_input( d );    
          }

          if( d->oFileH.isValid() )
          {
            oPkt       = &d->oPkt;
            oPktN      = 1;
            d->oCbCnt += 1;
            vop::zero( d->oPktAudioBuf, d->oChCnt * d->framesPerCycle );
          }
  
          d->cbFunc( d->cbArg, iPkt, iPktN, oPkt, oPktN );
  
          if( d->oFileH.isValid() )
            rc1 = _write_output( d );

          return rcSelect(rc0,rc1);          
        }

        bool _threadCbFunc( void* arg )
        {
          rc_t       rc = kOkRC;
          dev_mgr_t* p  = (dev_mgr_t*)arg;
          dev_t*     d  = nullptr;
          
          // block on the cond. var - unlock the mutex
          if((rc = mutex::waitOnCondVar(p->mutexH,false,p->threadTimeOutMs)) != kOkRC )
          {
            if( rc != kTimeOutRC )
            {
              cwLogError(rc,"Audio device file thread Wait-on-condition-var failed.");
              return false;
            }
          }

          p->threadCbCnt += 1;

          // if the cond. var was signaled and p->mutexH is locked
          if( rc == kOkRC )
          {
            // check for ready devices
            for(d=p->list; d!=nullptr; d=d->link)
            {
              // if this device is ready
              if( std::atomic_load_explicit(&d->readyCnt, std::memory_order_acquire) > 0  ) // ACQUIRE
              {
                // atomic incr  - note that the ordering doesn't matter because the update does not control access to any other variables from another thread
                std::atomic_store_explicit(&d->readyCnt, d->readyCnt-1,  std::memory_order_relaxed); // decrement

                if((rc = _update_device(d)) != kOkRC )
                  cwLogError(rc,"The update of audio device file %s failed.",cwStringNullGuard(d->label));
              
              }
            }
          }
          return true;
        }

                
        rc_t _destroy( dev_mgr_t* p )
        {
          rc_t rc = kOkRC;

          // destroy the thread
          if((rc = thread::destroy(p->threadH)) != kOkRC )
          {
            rc = cwLogError(rc,"Audio file device thread destroy failed.");
            goto errLabel;
          }
          
          // destroy the mutex
          if((rc = mutex::destroy( p->mutexH )) != kOkRC )
          {
            rc = cwLogError(rc,"Audio file device thread mutex destroy failed.");
            goto errLabel;
          }
          
          // destroy each device          
          for(dev_t* d = p->list; d != nullptr; )
          {
            dev_t* d0 = d->link;
            _close_input(d);
            _close_output(d);
            mem::release(d->label);
            mem::release(d);
            
            d = d0;            
          }

          mem::release(p);

        errLabel:          
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
  p->driver.deviceExecute        = deviceExecute;
  p->driver.deviceEnable         = deviceEnable;
  p->driver.deviceSeek           = deviceSeek;
  p->driver.deviceRealTimeReport = deviceRealTimeReport;

  if((rc = create( p->threadH, _threadCbFunc, p )) != kOkRC )
  {
    rc = cwLogError(rc,"Audio device file thread create failed.");
    goto errLabel;
  }

  // create the audio group thread mutex/cond var
  if((rc = mutex::create(p->mutexH)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio device file mutex create failed.");
    goto errLabel;
  }

  
  drvRef = &p->driver;

  hRef.set(p);

 errLabel:
  if( rc != kOkRC )
  {
    _destroy(p);
    rc = cwLogError(rc,"Audio device file create failed.");
  }
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

  hRef.clear();

  return rc;  
}

cw::rc_t   cw::audio::device::file::start( handle_t h )
{
  rc_t       rc = kOkRC;
  dev_mgr_t* p  = _handleToPtr(h);

  // if there are any audio device files
  if( p->list != nullptr )
  {
    if((rc = thread::unpause(p->threadH)) != kOkRC )
    {
      rc = cwLogError(rc,"Audio file device thread start failed.");
      goto errLabel;
    }
  }
 errLabel:
  return rc;
}

cw::rc_t   cw::audio::device::file::stop( handle_t h )
{
  rc_t       rc = kOkRC;
  dev_mgr_t* p  = _handleToPtr(h);

  if((rc = thread::pause(p->threadH)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio file device thread stop failed.");
    goto errLabel;
  }

 errLabel:
  return rc;
}


unsigned    cw::audio::device::file::deviceCount(          struct driver_str* drv)
{
  dev_mgr_t* p = _driverToPtr(drv);
  dev_t* d = p->list;
  unsigned n = 0;
  for(; d!=nullptr; d=d->link)
    ++n;
  return n;
}

const char* cw::audio::device::file::deviceLabel(          struct driver_str* drv, unsigned devIdx )
{
  rc_t       rc= kOkRC;
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return nullptr;

  return d->label;
}

unsigned    cw::audio::device::file::deviceChannelCount(   struct driver_str* drv, unsigned devIdx, bool inputFl )
{
  rc_t       rc = kOkRC;
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return 0;

  audiofile::handle_t fH = inputFl ? d->iFileH : d->oFileH;
  
  return fH.isValid() ? channelCount( fH ) : 0;
}

double      cw::audio::device::file::deviceSampleRate(     struct driver_str* drv, unsigned devIdx )
{
  rc_t       rc= kOkRC;
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return 0;

  return d->srate;
}

unsigned    cw::audio::device::file::deviceFramesPerCycle( struct driver_str* drv, unsigned devIdx, bool inputFl )
{
  rc_t       rc= kOkRC;
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return 0;

  return d->framesPerCycle;
  
}

cw::rc_t    cw::audio::device::file::deviceSetup( struct driver_str* drv, unsigned devIdx, double srate, unsigned frmPerCycle, cbFunc_t cbFunc, void* cbArg, unsigned cbDevIdx )
{
  rc_t       rc0 = kOkRC;
  rc_t       rc1 = kOkRC;
  dev_t*     d   = nullptr;
  dev_mgr_t* p   = _driverToPtr(drv);
  
  if((rc0 = _indexToDev( p, devIdx, d )) != kOkRC )
    return rc0;

  _close_input(d);
  _close_output(d);

  d->framesPerCycle = frmPerCycle;
  d->srate          = srate;
  d->cbFunc         = cbFunc;
  d->cbArg          = cbArg;
  d->cbDevIdx       = cbDevIdx;

  unsigned ms = (frmPerCycle * 1000) / srate;
  if( p->threadTimeOutMs==0 || ms < p->threadTimeOutMs )
  {
    p->threadTimeOutMs = ms;
    cwLogInfo("Audio device file time out %i ms.",p->threadTimeOutMs);
  }
  
  rc0 = _open_input(d, devIdx);
  rc1 = _open_output(d, devIdx);
  
  rc0 = rcSelect(rc0,rc1);

  if(rc0 != kOkRC )
    rc0 = cwLogError(rc0,"Audio device file '%s' setup failed.",cwStringNullGuard(d->label));

  return rc0;
}

cw::rc_t    cw::audio::device::file::deviceStart(          struct driver_str* drv, unsigned devIdx )
{
  rc_t rc0 = kOkRC;
  rc_t rc1 = kOkRC;
  dev_t*     d = nullptr;
  dev_mgr_t* p = _driverToPtr(drv);
  
  if((rc0 = _indexToDev( p, devIdx, d )) == kOkRC )
  {
    if( d->iFileH.isValid() && cwIsFlag(d->iFlags,kRewindOnStartFl) )
      if((rc0 = seek(d->iFileH,0)) != kOkRC )
        rc0 = cwLogError(rc0,"Rewind on start failed on the audio device file input file.");

    if( d->oFileH.isValid() && cwIsFlag(d->oFlags,kRewindOnStartFl) )    
      if((rc1 = seek(d->oFileH,0)) != kOkRC )
        rc1 = cwLogError(rc1,"Rewind on start failed on the audio device file output file.");
  }
  
  if((rc0 = rcSelect(rc0,rc1)) == kOkRC )
    d->isStartedFl = true;
  
  return rc0;
}

cw::rc_t    cw::audio::device::file::deviceStop(           struct driver_str* drv, unsigned devIdx )
{
  rc_t       rc = kOkRC;
  dev_t*     d  = nullptr;
  dev_mgr_t* p  = _driverToPtr(drv);

  if((rc = _indexToDev( p, devIdx, d )) == kOkRC )
    if( d->isStartedFl && d->oFileH.isValid() )
    {
      // TODO: implement audiofile::flush()
      // if((rc = flush(d->ofileH)) != kOkRC )
      //   rc = cwLogError(rc,"Flush on stop failed on the audio device file output file.");
      
      d->isStartedFl = false;
    }  

  return rc;
}

bool        cw::audio::device::file::deviceIsStarted(      struct driver_str* drv, unsigned devIdx )
{
  rc_t       rc = kOkRC;
  dev_t*     d  = nullptr;
  dev_mgr_t* p  = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) == kOkRC )
    return d->isStartedFl;
  return false;
}

cw::rc_t    cw::audio::device::file::deviceExecute(        struct driver_str* drv, unsigned devIdx )
{
  rc_t           rc    = kOkRC;
  dev_mgr_t*     p     = _driverToPtr(drv);
  dev_t*         d     = nullptr;
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    goto errLabel;
  
  std::atomic_store_explicit(&d->readyCnt, d->readyCnt+1, std::memory_order_relaxed); // atomic incr

  mutex::signalCondVar(p->mutexH);

 errLabel:
  return rc; 
}

cw::rc_t  cw::audio::device::file::deviceEnable( struct driver_str* drv, unsigned devIdx, bool inputFl, bool enableFl )
{
  rc_t           rc    = kOkRC;
  dev_mgr_t*     p     = _driverToPtr(drv);
  dev_t*         d     = nullptr;
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    goto errLabel;

  if( inputFl )
    d->iEnableFl = enableFl;
  else
    d->oEnableFl = enableFl;

  printf("Enable i:%i o:%i\n",d->iEnableFl,d->oEnableFl);
  
 errLabel:
  return rc; 
}

cw::rc_t  cw::audio::device::file::deviceSeek(   struct driver_str* drv, unsigned devIdx, bool inputFl, unsigned frameOffset )
{
  rc_t           rc    = kOkRC;
  dev_mgr_t*     p     = _driverToPtr(drv);
  dev_t*         d     = nullptr;
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    goto errLabel;

  if( inputFl )
  {
    if( d->iFileH.isValid() )
      if((rc = seek(d->iFileH,frameOffset)) != kOkRC )
        rc = cwLogError(rc,"Seek failed on the audio device file input file.");    
  }
  else
  {
    if( d->oFileH.isValid())
      if((rc = seek(d->oFileH,frameOffset)) != kOkRC )
        rc = cwLogError(rc,"Seek failed on the audio device file output file.");
  }

 errLabel:
  return rc;
}

void        cw::audio::device::file::deviceRealTimeReport( struct driver_str* drv, unsigned devIdx )
{
  rc_t       rc = kOkRC;
  dev_t*     d  = nullptr;
  dev_mgr_t* p  = _driverToPtr(drv);
  
  if((rc = _indexToDev( p, devIdx, d )) != kOkRC )
    return;
    
  cwLogInfo("file cb i:%i o:%i err i:%i  o:%i frames: i:%i o:%i : th:%i :  %s",d->iCbCnt,d->oCbCnt,d->iErrCnt,d->oErrCnt,d->iFrmCnt,d->oFrmCnt,p->threadCbCnt,cwStringNullGuard(d->label));

}

cw::rc_t    cw::audio::device::file::createInDevice(  handle_t& h, const char* label, const char* audioInFName, unsigned flags )
{
  rc_t       rc = kOkRC;
  handle_t   iFileH;
  dev_t*     d;  
  dev_mgr_t* p = _handleToPtr(h);
  
  // create or find a device
  if((d = _findOrCreateDev(p, label )) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Audio file device create failed.");
    goto errLabel;
  }

  d->iFname  = mem::duplStr(audioInFName);
  d->iFlags  = flags;
  
 errLabel:
      
  return rc;
}

cw::rc_t    cw::audio::device::file::createOutDevice( handle_t& h, const char* label, const char* audioOutFName, unsigned flags, unsigned chCnt, unsigned bitsPerSample )
{
  rc_t       rc = kOkRC;
  dev_t*     d;  
  dev_mgr_t* p = _handleToPtr(h);
  
  // create or find a device
  if((d = _findOrCreateDev(p, label )) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Audio file device create failed.");
    goto errLabel;
  }

  d->oFname         = mem::duplStr(audioOutFName);
  d->oFlags         = flags;
  d->oChCnt         = chCnt;
  d->oBitsPerSample = bitsPerSample;
  
 errLabel:
  
  return rc;  
}


cw::rc_t    cw::audio::device::file::report(handle_t h )
{
  rc_t rc = kOkRC;
  dev_mgr_t* p  = _handleToPtr(h);
  dev_t* d = p->list;

  for(; d!=nullptr; d=d->link)
    _devReport(d);
  
  return rc;
}

cw::rc_t    cw::audio::device::file::report()
{
  rc_t rc = kOkRC;
  return rc;
}


namespace cw {
  namespace audio {
    namespace device {
      namespace file {

        typedef struct cb_object_str
        {
          uint8_t*         buf;
          unsigned         byteCnt;
          std::atomic<int> readyCnt;
        } cb_object_t;
        
        void driverCallback( void* cbArg, audioPacket_t* inPktArray, unsigned inPktCnt, audioPacket_t* outPktArray, unsigned outPktCnt )
        {
          cb_object_t* p = (cb_object_t*)cbArg;

          if( inPktCnt )
          {
            audioPacket_t* pkt      = inPktArray;
            unsigned       pktByteN = pkt->audioFramesCnt * pkt->chCnt * sizeof(sample_t);
            unsigned       byteN    = std::min(p->byteCnt,pktByteN);
            vop::copy(p->buf,(const uint8_t*)pkt->audioBytesPtr,byteN);
            p->readyCnt++;
          }

          if( outPktCnt && p->readyCnt.load() > 0)
          {
            audioPacket_t* pkt      = outPktArray;
            unsigned       pktByteN = pkt->audioFramesCnt * pkt->chCnt * sizeof(sample_t);
            unsigned       byteN    = std::min(p->byteCnt,pktByteN);
            vop::copy((uint8_t*)pkt->audioBytesPtr,p->buf,byteN);
            p->readyCnt--;
          }
        }
      }
    }
  }
}

cw::rc_t cw::audio::device::file::test( const object_t* cfg)
{
  rc_t               rc             = kOkRC;
  rc_t               rc1            = kOkRC;
  rc_t               rc2            = kOkRC;
  const char*        ifname         = nullptr;
  const char*        ofname         = nullptr;
  struct driver_str  driver         = {0};
  struct driver_str* driver_ptr     = &driver;
  unsigned           bitsPerSample  = 0; // zero indicates floating point sample format for output audio file
  unsigned           sleepMicrosec  = 0;
  const char*        devLabel       = "dev_file";
  unsigned           devIdx         = 0;
  unsigned           framesPerCycle = 0;
  cb_object_t        obj            = { 0 };
  void*              cbArg          = &obj;
  audiofile::info_t  info;
  handle_t           h;

  // parse the test args
  if((rc = cfg->getv("inAudioFname",ifname,
                     "outAudioFname",ofname,
                     "framesPerCycle",framesPerCycle)) != kOkRC || ifname==nullptr || ofname==nullptr )
  {
    rc = cwLogError(rc,"Parsing audiio device file test cfg. failed.");
    goto errLabel;
  }

  // create the audioDeviceFile mgr
  if((rc = create(h,driver_ptr)) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating audio device file mgr.");
    goto errLabel;
  }

  // get input file srate and channel count
  if((rc = getInfo( ifname, &info )) != kOkRC )
  {
    rc = cwLogError(rc,"Error parsing input audio file '%s' header.",cwStringNullGuard(ifname));
    goto errLabel;
  }

  // create an input audio file device
  if((rc = createInDevice( h, devLabel, ifname,  kRewindOnStartFl )) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating the audio device file input device from the file:%s.",cwStringNullGuard(ifname));
    goto errLabel;    
  }

  // create an output audio file device
  if((rc = createOutDevice( h, devLabel, ofname, kRewindOnStartFl, info.chCnt, bitsPerSample )) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating the audio device file output device from the file:%s.",cwStringNullGuard(ofname));
    goto errLabel;        
  }

  // setup the audio device file device
  if((rc =  deviceSetup( driver_ptr, devIdx, info.srate, framesPerCycle, driverCallback, cbArg, devIdx )) != kOkRC )
  {
    rc = cwLogError(rc,"Error setting up the audio device file.");
    goto errLabel;
  }

  sleepMicrosec = (framesPerCycle * 1e6) / info.srate;
  
  report(h);

  obj.byteCnt = framesPerCycle * info.chCnt * sizeof(sample_t);
  obj.buf = mem::allocZ<uint8_t>( obj.byteCnt );

  // start the audio device file
  if((rc = start(h)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio device file start failed.");
    goto errLabel;
  }

  // run the audio device file
  for(unsigned i=0; i<10; ++i)
  {
    deviceExecute( driver_ptr, devIdx );
    deviceRealTimeReport( driver_ptr, devIdx );
    sleepUs( sleepMicrosec );
  }
    
 errLabel:
  mem::release(obj.buf);

  // stop the audio device file mgr.
  if((rc2 = stop(h)) != kOkRC )
    rc2 = cwLogError(rc2,"Audio device file mgr. stop failed.");

  // destroy the audio device file mgr
  if((rc1 = destroy(h)) != kOkRC )
    rc1 = cwLogError(rc1,"Audio device file mgr. destroy failed.");
  
  return rcSelect(rc,rc1,rc2);  
}
