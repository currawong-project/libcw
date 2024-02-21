#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwObject.h"
#include "cwText.h"
#include "cwTextBuf.h"
#include "cwThread.h"

#include "cwMidi.h"
#include "cwMidiDecls.h"
#include "cwMidiFile.h"
#include "cwMidiDevice.h"
#include <poll.h>

#include "cwMidiAlsa.h"
#include "cwMidiFileDev.h"


namespace cw
{
  namespace midi
  {
    namespace device
    {
      typedef enum {
        kStoppedStateId,
        kPausedStateId,
        kPlayingStateId
      } transportStateId_t;
      
      typedef struct device_str
      {
        cbFunc_t cbFunc;
        void*    cbArg;

        alsa::handle_t alsaDevH;
        unsigned       alsaPollfdN;
        struct pollfd* alsaPollfdA;
        unsigned       alsa_dev_cnt;

        file_dev::handle_t fileDevH;
        unsigned file_dev_cnt;
        
        unsigned total_dev_cnt;

        
        unsigned thread_timeout_microsecs;
        thread::handle_t threadH;

        transportStateId_t fileDevStateId;
        
        unsigned long long offset_micros;
        unsigned long long last_posn_micros;
        time::spec_t       start_time;
        
      } device_t;

      device_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,device_t>(h); }

      rc_t _validate_dev_index( device_t* p, unsigned devIdx )
      {
        rc_t rc = kOkRC;
        if( devIdx >= p->total_dev_cnt )
          rc = cwLogError(kInvalidArgRC,"Invalid MIDI device index (%i >= %i).",devIdx,p->total_dev_cnt);
          
        return rc;
      }
      
      unsigned _devIdxToAlsaDevIdx( device_t* p, unsigned devIdx )
      {
        return devIdx >= p->alsa_dev_cnt ? kInvalidIdx : devIdx;
      }

      unsigned _devIdxToFileDevIdx( device_t* p, unsigned devIdx )
      {
        return devIdx==kInvalidIdx || devIdx < p->alsa_dev_cnt ? kInvalidIdx : devIdx - p->alsa_dev_cnt;
      }

      unsigned _alsaDevIdxToDevIdx( device_t* p, unsigned alsaDevIdx )
      { return alsaDevIdx; }

      unsigned _fileDevIdxToDevIdx( device_t* p, unsigned fileDevIdx )
      { return fileDevIdx == kInvalidIdx ? kInvalidIdx : p->alsa_dev_cnt + fileDevIdx; }

      bool _isAlsaDevIdx( device_t* p, unsigned devIdx )
      { return devIdx==kInvalidIdx ? false : devIdx < p->alsa_dev_cnt; }

      bool _isFileDevIdx( device_t* p, unsigned devIdx )
      { return devIdx==kInvalidIdx ? false : (p->alsa_dev_cnt <= devIdx && devIdx < p->total_dev_cnt); }
      

      rc_t _destroy( device_t* p )
      {
        rc_t rc = kOkRC;
        
        if((rc = destroy(p->threadH)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI port thread destroy failed.");
          goto errLabel;
        }
        
        destroy(p->alsaDevH);
        destroy(p->fileDevH);
        mem::release(p);

      errLabel:
        return rc;
      }

      bool _thread_func( void* arg )
      {
        device_t* p = (device_t*)arg;

        unsigned max_sleep_micros = p->thread_timeout_microsecs/2;
        unsigned sleep_millis = max_sleep_micros/1000;
        
        if( p->fileDevStateId == kPlayingStateId )
        {
          time::spec_t       cur_time         = time::current_time();
          unsigned           elapsed_micros   = time::elapsedMicros(p->start_time,cur_time);
          
          unsigned long long file_posn_micros = p->offset_micros + elapsed_micros;

          // Send any messages whose time has expired and get the
          // wait time for the next message.
          file_dev::exec_result_t r = exec(p->fileDevH,file_posn_micros);

          // If the file dev has no more messages to play then sleep for the maximum time.
          unsigned file_dev_sleep_micros = r.eof_fl ? max_sleep_micros : r.next_msg_wait_micros;

          // Prevent the wait time from being longer than the thread state change timeout.
          unsigned sleep_micros = std::min( max_sleep_micros, file_dev_sleep_micros );

          p->last_posn_micros = file_posn_micros + sleep_micros;

          // If the wait time is less than one millisecond then make it one millisecond.
          // (remember that we allowed the file device to go 3 milliseconds ahead and
          //  and so it is safe, and better for preventing many very short timeout's,
          // to wait at least 1 millisecond)
          sleep_millis = std::max(1U, sleep_micros/1000 );
        }
        

        // Block here waiting for ALSA events or timeout when the next file msg should be sent
        int sysRC = poll( p->alsaPollfdA, p->alsaPollfdN, sleep_millis );
        
        if(sysRC == 0 )
        {
          // time-out
        }
        else
        {
          if( sysRC > 0 )
          {
            rc_t rc;
            if((rc = handleInputMsg(p->alsaDevH)) != kOkRC )
            {
              cwLogError(rc,"ALSA MIDI dev. input failed");
            }
          }
          else
          {
            cwLogSysError(kOpFailRC,sysRC,"MIDI device poll failed.");
          }
        }
        
        return true;
      }

      
    } // device
  } // midi
} // cw



cw::rc_t cw::midi::device::create( handle_t&   hRef,
                                   cbFunc_t    cbFunc,
                                   void*       cbArg,
                                   const char* filePortLabelA[],
                                   unsigned    max_file_cnt,
                                   const char* appNameStr,
                                   const char* fileDevName,
                                   unsigned    fileDevReadAheadMicros,
                                   unsigned    parserBufByteCnt )
{
  rc_t rc  = kOkRC;
  rc_t rc1 = kOkRC;  
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  device_t* p = mem::allocZ<device_t>();
  
  if((rc = create( p->alsaDevH, cbFunc, cbArg, parserBufByteCnt, appNameStr )) != kOkRC )
  {
    rc = cwLogError(rc,"ALSA MIDI device create failed.");
    goto errLabel;
  }

  p->alsa_dev_cnt = count(p->alsaDevH);

  if((rc = create( p->fileDevH, cbFunc, cbArg, p->alsa_dev_cnt, filePortLabelA, max_file_cnt, fileDevName, fileDevReadAheadMicros )) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI file device create failed.");
    goto errLabel;
  }

  p->file_dev_cnt  = count(p->fileDevH);    
  p->total_dev_cnt = p->alsa_dev_cnt + p->file_dev_cnt;
  p->alsaPollfdA   = pollFdArray(p->alsaDevH,p->alsaPollfdN);
  p->fileDevStateId = kStoppedStateId;
  
  if((rc = thread::create(p->threadH,
                          _thread_func,
                          p,
                          "midi_dev")) != kOkRC )
  {
    rc = cwLogError(rc,"The MIDI file device thread create failed.");
    goto errLabel;
  }

  p->thread_timeout_microsecs = stateTimeOutMicros(p->threadH); 
  
  hRef.set(p);

  if((rc = unpause(p->threadH)) != kOkRC )
  {
    rc = cwLogError(rc,"Initial thread un-pause failed.");
    goto errLabel;
  }

  
errLabel:
  if(rc != kOkRC )
     rc1 = _destroy(p);

  if((rc = rcSelect(rc,rc1)) != kOkRC )
    rc = cwLogError(rc,"MIDI device mgr. create failed.");
    
  return rc;
}

cw::rc_t cw::midi::device::create( handle_t&       h,
                                   cbFunc_t        cbFunc,
                                   void*           cbArg,
                                   const object_t* args )
{
  rc_t            rc                     = kOkRC;
  const char*     appNameStr             = nullptr;
  const char*     fileDevName            = "file_dev";
  unsigned        fileDevReadAheadMicros = 3000;
  unsigned        parseBufByteCnt        = 1024;
  const object_t* file_ports             = nullptr;
  const object_t* port                   = nullptr;

  if((rc = args->getv("appNameStr",appNameStr,
                      "fileDevName",fileDevName,
                      "fileDevReadAheadMicros",fileDevReadAheadMicros,
                      "parseBufByteCnt",parseBufByteCnt,
                      "file_ports",file_ports)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI port parse args. failed.");
  }
  else
  {
    unsigned fpi            = 0;
    unsigned filePortArgCnt = file_ports->child_count();
    const char* labelArray[ filePortArgCnt ];
    memset(labelArray,0,sizeof(labelArray));
    
    for(unsigned i=0; i<filePortArgCnt; ++i)
    {
      if((port = file_ports->child_ele(i)) != nullptr )
      {
        if((rc = port->getv("label",labelArray[fpi])) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI file dev. port arg parse failed.");
          goto errLabel;
        }
        
        fpi += 1;
        
      }
    }
    
    rc = create(h,cbFunc,cbArg,labelArray,fpi,appNameStr,fileDevName,fileDevReadAheadMicros,parseBufByteCnt);
    
  }
  
  
errLabel:
  return rc;  
}

      
cw::rc_t cw::midi::device::destroy( handle_t& hRef)
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  device_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI device mgr. destroy failed.");
    goto errLabel;
  }

  hRef.clear();
errLabel:
  return rc;
}
      
bool cw::midi::device::isInitialized( handle_t h )
{ return h.isValid(); }

unsigned    cw::midi::device::count( handle_t h )
{
  device_t* p = _handleToPtr(h);
  return p->total_dev_cnt;
}
      
const char* cw::midi::device::name( handle_t h, unsigned devIdx )
{
  device_t*   p          = _handleToPtr(h);
  const char* ret_name   = nullptr;
  unsigned    alsaDevIdx = kInvalidIdx;
  unsigned    fileDevIdx = kInvalidIdx;
  
  if((alsaDevIdx = _devIdxToAlsaDevIdx(p,devIdx)) != kInvalidIdx )
    ret_name                                       = name(p->alsaDevH,alsaDevIdx);
  else
  {
    if((fileDevIdx = _devIdxToFileDevIdx(p,devIdx)) != kInvalidIdx)
      ret_name = name(p->fileDevH,fileDevIdx);
    else
      cwLogError(kInvalidArgRC,"%i is an invalid device index.",devIdx);    
  }

  if( ret_name == nullptr )
    cwLogError(kOpFailRC,"The name of device index %i could not be found.",devIdx);

  return ret_name;
}
      
unsigned    cw::midi::device::nameToIndex(handle_t h, const char* deviceName)
{
  device_t* p = _handleToPtr(h);
  unsigned  devIdx = kInvalidIdx;

  if((devIdx = nameToIndex(p->alsaDevH,deviceName)) != kInvalidIdx )
    devIdx   = _alsaDevIdxToDevIdx(p,devIdx);
  else
  {
    if((devIdx = nameToIndex(p->fileDevH,deviceName)) != kInvalidIdx )
      devIdx   = _fileDevIdxToDevIdx(p,devIdx);
  }

  if( devIdx == kInvalidIdx )
    cwLogError(kOpFailRC,"MIDI device name to index failed on '%s'.",cwStringNullGuard(deviceName));

  return devIdx;
}

unsigned cw::midi::device::portNameToIndex( handle_t h, unsigned devIdx, unsigned flags, const char* portNameStr )
{
  device_t* p          = _handleToPtr(h);
  unsigned  alsaDevIdx = kInvalidIdx;
  unsigned  fileDevIdx = kInvalidIdx;
  unsigned  portIdx    = kInvalidIdx;
  
  if((alsaDevIdx   = _devIdxToAlsaDevIdx(p,devIdx)) != kInvalidIdx )
    portIdx = portNameToIndex(p->alsaDevH,alsaDevIdx,flags,portNameStr);
  else
    if((fileDevIdx = _devIdxToFileDevIdx(p,devIdx)) != kInvalidIdx )
      portIdx = portNameToIndex(p->fileDevH,fileDevIdx,flags,portNameStr);
  
  if( portIdx == kInvalidIdx )
    cwLogError(kInvalidArgRC,"The MIDI port name '%s' could not be found.",cwStringNullGuard(portNameStr));
               
  return portIdx;
}

cw::rc_t cw::midi::device::portEnable( handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx, bool enableFl )
{
  rc_t      rc         = kOkRC;
  device_t* p          = _handleToPtr(h);
  unsigned  alsaDevIdx = kInvalidIdx;
  unsigned  fileDevIdx = kInvalidIdx;
  
  if((alsaDevIdx   = _devIdxToAlsaDevIdx(p,devIdx)) != kInvalidIdx )
    rc = portEnable(p->alsaDevH,alsaDevIdx,flags,portIdx,enableFl);
  else
    if((fileDevIdx = _devIdxToFileDevIdx(p,devIdx)) != kInvalidIdx )
      rc = portEnable(p->fileDevH,fileDevIdx,flags,portIdx,enableFl);
  
  if( rc != kOkRC )
    rc = cwLogError(rc,"The MIDI port %s failed on dev '%s'  port '%s'.",enableFl ? "enable" : "disable", cwStringNullGuard(name(h,devIdx)), cwStringNullGuard(portName(h,devIdx,flags,portIdx)));
               
  return rc;
  
}
      
unsigned cw::midi::device::portCount(  handle_t h, unsigned devIdx, unsigned flags )
{
  device_t* p          = _handleToPtr(h);
  unsigned  alsaDevIdx = kInvalidIdx;
  unsigned  fileDevIdx = kInvalidIdx;
  unsigned  portCnt = 0;
  
  if((alsaDevIdx = _devIdxToAlsaDevIdx(p,devIdx)) != kInvalidIdx )
    portCnt      = portCount(p->alsaDevH,alsaDevIdx,flags);
  else
  {
    if((fileDevIdx = _devIdxToFileDevIdx(p,devIdx)) != kInvalidIdx )
      portCnt      = portCount(p->fileDevH,fileDevIdx,flags);
    else
      cwLogError(kInvalidArgRC,"The device index %i is not valid. Port count access failed.",devIdx);
  }
  
  return portCnt;   
}
      
const char* cw::midi::device::portName(   handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx )
{
  device_t*   p          = _handleToPtr(h);
  unsigned    alsaDevIdx = kInvalidIdx;
  unsigned    fileDevIdx = kInvalidIdx;
  const char* name       = nullptr;

  if((alsaDevIdx   = _devIdxToAlsaDevIdx(p,devIdx)) != kInvalidIdx )
    name           = portName(p->alsaDevH,alsaDevIdx,flags,portIdx);
  else
    if((fileDevIdx = _devIdxToFileDevIdx(p,devIdx)) != kInvalidIdx )
      name         = portName(p->fileDevH,fileDevIdx,flags,portIdx);
    else
      cwLogError(kInvalidArgRC,"The device index %i is not valid.");

  if( name == nullptr )
    cwLogError(kOpFailRC,"The access to port name on device index %i port index %i failed.",devIdx,portIdx);

  return name;  
}
      
      
cw::rc_t cw::midi::device::send(       handle_t h, unsigned devIdx, unsigned portIdx, uint8_t st, uint8_t d0, uint8_t d1 )
{
  rc_t      rc         = kOkRC;
  device_t* p          = _handleToPtr(h);
  unsigned  alsaDevIdx = kInvalidIdx;
  unsigned  fileDevIdx = kInvalidIdx;

  if((alsaDevIdx = _devIdxToAlsaDevIdx(p,devIdx)) != kInvalidIdx )
    rc = send(p->alsaDevH,alsaDevIdx,portIdx,st,d0,d1);
  else
  {
    if((fileDevIdx = _devIdxToFileDevIdx(p,devIdx)) != kInvalidIdx )
      rc                                             = send(p->fileDevH,fileDevIdx,portIdx,st,d0,d1);
    else
      rc = cwLogError(kInvalidArgRC,"The device %i is not valid.",devIdx);
  }

  if( rc != kOkRC )
    rc = cwLogError(rc,"The MIDI msg (0x%x %i %i) transmit failed.",st,d0,d1);

  return rc;
}
      
cw::rc_t cw::midi::device::sendData(   handle_t h, unsigned devIdx, unsigned portIdx, const uint8_t* dataPtr, unsigned byteCnt )
{
  rc_t      rc         = kOkRC;
  device_t* p          = _handleToPtr(h);
  unsigned  alsaDevIdx = kInvalidIdx;
  unsigned  fileDevIdx = kInvalidIdx;

  if((alsaDevIdx = _devIdxToAlsaDevIdx(p,devIdx)) != kInvalidIdx )
    rc = sendData(p->alsaDevH,alsaDevIdx,portIdx,dataPtr,byteCnt);
  else
  {
    if((fileDevIdx = _devIdxToFileDevIdx(p,devIdx)) != kInvalidIdx )
      rc = sendData(p->fileDevH,fileDevIdx,portIdx,dataPtr,byteCnt);
    else
      rc = cwLogError(kInvalidArgRC,"The device %i is not valid.",devIdx);
  }

  if( rc != kOkRC )
    rc = cwLogError(rc,"The MIDI msg transmit data failed.");

  return rc;
}

cw::rc_t cw::midi::device::openMidiFile( handle_t h, unsigned devIdx, unsigned portIdx, const char* fname )
{
  rc_t      rc = kOkRC;
  device_t* p          = _handleToPtr(h);

  if( _devIdxToFileDevIdx(p,devIdx) == kInvalidIdx )
  {
    cwLogError(kInvalidArgRC,"The device index %i does not identify a valid file device.",devIdx);
    goto errLabel;
  }

  if((rc = open_midi_file( p->fileDevH, portIdx, fname)) != kOkRC )
    goto errLabel;
  

errLabel:
  return rc;
}

cw::rc_t cw::midi::device::loadMsgPacket( handle_t h, const packet_t& pkt )
{
  rc_t rc = kOkRC;
  device_t* p  = _handleToPtr(h);

  if( _devIdxToFileDevIdx(p,pkt.devIdx) == kInvalidIdx )
  {
    cwLogError(kInvalidArgRC,"The device index %i does not identify a valid file device.",pkt.devIdx);
    goto errLabel;
  }

  if((rc = load_messages( p->fileDevH, pkt.portIdx, pkt.msgArray, pkt.msgCnt)) != kOkRC )
    goto errLabel;

errLabel:
  return rc;
}

unsigned cw::midi::device::msgCount( handle_t h, unsigned devIdx, unsigned portIdx )
{
  device_t* p  = _handleToPtr(h);
  
  if(_devIdxToFileDevIdx(p,devIdx) == kInvalidIdx )
  {
    cwLogError(kInvalidArgRC,"The device index %i does not identify a valid file device.",devIdx);
    goto errLabel;
  }

  return msg_count( p->fileDevH, portIdx);
  
errLabel:
  return 0;
}

cw::rc_t cw::midi::device::seekToMsg( handle_t h, unsigned devIdx, unsigned portIdx, unsigned msgIdx )
{
  rc_t      rc = kOkRC;
  device_t* p  = _handleToPtr(h);
  
  if(_devIdxToFileDevIdx(p,devIdx) == kInvalidIdx )
  {
    cwLogError(kInvalidArgRC,"The device index %i does not identify a valid file device.",devIdx);
    goto errLabel;
  }

  if((rc = seek_to_msg_index( p->fileDevH, portIdx, msgIdx)) != kOkRC )
    goto errLabel;
  
  
errLabel:
  return rc;

}

cw::rc_t cw::midi::device::setEndMsg( handle_t h, unsigned devIdx, unsigned portIdx, unsigned msgIdx )
{
  rc_t      rc = kOkRC;
  device_t* p          = _handleToPtr(h);
  
  if(_devIdxToFileDevIdx(p,devIdx) == kInvalidIdx )
  {
    cwLogError(kInvalidArgRC,"The device index %i does not identify a valid file device.",devIdx);
    goto errLabel;
  }

  if((rc = set_end_msg_index( p->fileDevH, portIdx, msgIdx)) != kOkRC )
    goto errLabel;
  
  
errLabel:
  return rc;

}


cw::rc_t cw::midi::device::start( handle_t h )
{
  rc_t rc = kOkRC;
  device_t* p = _handleToPtr(h);

  if( p->fileDevStateId != kPlayingStateId )
  {

    if((rc = rewind(p->fileDevH)) != kOkRC )
    {
      rc = cwLogError(rc,"Rewind failed on MIDI file device.");
      goto errLabel;
    }

    p->start_time       = time::current_time();
    p->offset_micros    = 0;
    p->last_posn_micros = 0; 
    p->fileDevStateId   = kPlayingStateId;
  }
errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"MIDI port start failed.");
  
  return rc;
}
  
cw::rc_t cw::midi::device::stop( handle_t h )
{
  device_t* p = _handleToPtr(h);
  
  p->fileDevStateId = kStoppedStateId;
  
  return kOkRC;
}

cw::rc_t cw::midi::device::pause( handle_t h, bool pause_fl )
{
  rc_t rc = kOkRC;
  device_t* p = _handleToPtr(h);
  
  switch( p->fileDevStateId )
  {
    case kStoppedStateId:
      // unpausing does nothing from a 'stopped' state
      break;
      
    case kPausedStateId:
      if( !pause_fl )
      {
        p->start_time = time::current_time();
        p->fileDevStateId = kPlayingStateId;    
      }
      break;
      
    case kPlayingStateId:
      if( pause_fl )
      {
        p->offset_micros  = p->last_posn_micros;
        p->fileDevStateId = kPausedStateId;
      }
      break;
  }

  return rc;
}


cw::rc_t cw::midi::device::report( handle_t h )
{
  rc_t rc = kOkRC;
  textBuf::handle_t tbH;
  
  if((rc = textBuf::create(tbH)) != kOkRC )
    goto errLabel;

  report(h,tbH);
  
errLabel:
  destroy(tbH);
  return rc;
}

void cw::midi::device::report( handle_t h, textBuf::handle_t tbH)
{
  device_t* p = _handleToPtr(h);
  report(p->alsaDevH,tbH);
  report(p->fileDevH,tbH);  
}


void cw::midi::device::latency_measure_reset(handle_t h)
{
  device_t* p = _handleToPtr(h);
  latency_measure_reset(p->alsaDevH);
  latency_measure_reset(p->fileDevH);
}

cw::midi::device::latency_meas_combined_result_t cw::midi::device::latency_measure_result(handle_t h)
{
  device_t* p = _handleToPtr(h);
  latency_meas_combined_result_t r;
  r.alsa_dev = latency_measure_result(p->alsaDevH);
  r.file_dev = latency_measure_result(p->fileDevH);
  return r;
}



