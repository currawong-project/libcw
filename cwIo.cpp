#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwTime.h"
#include "cwTextBuf.h"
#include "cwMidi.h"
#include "cwMidiPort.h"
#include "cwIo.h"

#include "cwObject.h"

#include "cwThread.h"
#include "cwSerialPort.h"
#include "cwSerialPortSrv.h"

#include "cwAudioDevice.h"
#include "cwAudioBuf.h"
#include "cwAudioDeviceAlsa.h"


namespace cw
{
  namespace io
  {

    typedef struct serialPort_str
    {
      char*                   name;
      char*                   device;
      unsigned                baudRate;
      unsigned                flags;
      unsigned                pollPeriodMs;      
      serialPortSrv::handle_t serialH;
    } serialPort_t;

    typedef struct audioCfg_str
    {
      unsigned enableFl;
      char*    name;
      char*    device;
      double   srate;
      unsigned dspFrameCnt;
      unsigned cycleCnt;
      unsigned devIdx;
    } audioCfg_t;
    
    typedef struct io_str
    {
      cbFunc_t               cbFunc;
      void*                  cbArg;
      
      thread::handle_t       threadH;
      
      serialPort_t*          serialA;
      unsigned               serialN;
      
      midi::device::handle_t midiH;

      audio::device::handle_t       audioH;
      audio::device::alsa::handle_t alsaH;
      
      unsigned    audioCfgN;
      audioCfg_t* audioCfgA;      
      
    } io_t;
  

    io_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,io_t>(h); }

    rc_t _destroy( io_t* p )
    {
      rc_t rc = kOkRC;

      if((rc = thread::destroy(p->threadH)) != kOkRC )
        return rc;

      mem::release(p);

      return rc;
    }
  
    bool _mainThreadFunc( void* arg )
    {
      //io_t* p = static_cast<io_t*>(arg);


      return true;
    }

    void _serialPortCb( void* arg, const void* byteA, unsigned byteN )
    {
      //io_t* p = static_cast<io_t*>(arg);
      
    }

    rc_t _serialPortParseCfg( const object_t& e, serialPort_t* port )
    {
      rc_t        rc          = kOkRC;
      char*       parityLabel = nullptr;
      unsigned    bits        = 8;
      unsigned    stop        = 1;
      
      idLabelPair_t parityA[] =
        {
         { serialPort::kEvenParityFl, "even" },
         { serialPort::kOddParityFl,  "odd"  },
         { serialPort::kNoParityFl,   "no"   },
         { 0, nullptr }
        };
             
      if((rc = e.getv(
            "name",         port->name,
            "device",       port->device,
            "baud",         port->baudRate,
            "bits",         bits,
            "stop",         stop,
            "parity",       parityLabel,
            "pollPeriodMs", port->pollPeriodMs )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Serial configuration parse failed.");
      }

      switch( bits )
      {
        case 5:  port->flags |= serialPort::kDataBits5Fl;
        case 6:  port->flags |= serialPort::kDataBits6Fl;
        case 7:  port->flags |= serialPort::kDataBits7Fl;
        case 8:  port->flags |= serialPort::kDataBits8Fl;
        default:
          rc = cwLogError(kSyntaxErrorRC,"Invalid serial data bits cfg:%i.",bits);
      }

      switch( stop )
      {
        case 1: port->flags |= serialPort::k1StopBitFl;
        case 2: port->flags |= serialPort::k2StopBitFl;
        default:
          rc = cwLogError(kSyntaxErrorRC,"Invalid serial stop bits cfg:%i.",stop);
      }

      unsigned i;
      for(i=0; parityA[i].label != nullptr; ++i)
        if( textCompare(parityLabel,parityA[i].label) == 0 )
        {
          port->flags |= parityA[i].id;
          break;
        }

      if( parityA[i].label == nullptr )
        rc = cwLogError(kSyntaxErrorRC,"Invalid parity cfg:'%s'.",cwStringNullGuard(parityLabel));
      

      return rc;
    }
        
    rc_t _serialPortCreate( io_t* p, const object_t* c )
    {
      rc_t            rc   = kOkRC;
      const object_t* cfgL = nullptr;

      // get the serial port list node
      if((cfgL = c->find("serial")) == nullptr || !cfgL->is_list())
        return cwLogError(kSyntaxErrorRC,"Unable to locate the 'serial' configuration list.");

      p->serialN = cfgL->child_count();
      p->serialA = mem::allocZ<serialPort_t>(p->serialN);
      
      // for each serial port cfg
      for(unsigned i=0; i<p->serialN; ++i)
      {
        const object_t* e = cfgL->list_ele(i);
        serialPort_t*   r = p->serialA + i;
        
        if( e == nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unable to access a 'serial' port configuration record at index:%i.",i);
          break;
        }
        else
        {
          // parse the cfg record
          if((rc = _serialPortParseCfg(*e,r)) != kOkRC )
          {
            rc = cwLogError(rc,"Serial configuration parse failed on record index:%i.", i );
            break;
          }

          /*
          // create the serial port object
          if((rc = serialPortSrv::create( r->serialH, r->device, r->baudRate, r->flags, _serialPortCb, p, r->pollPeriodMs )) != kOkRC )
          {
            rc = cwLogError(rc,"Serial port create failed on record index:%i.", i );
            break;
          }
          */
        }
      }

      return rc;
    }


    void _midiCallback( const midi::packet_t* pktArray, unsigned pktCnt )
    {
      unsigned i,j;
      for(i=0; i<pktCnt; ++i)
      {
        const midi::packet_t* pkt = pktArray + i;
        
        //io_t* p = static_cast<io_t*>(pkt->cbDataPtr);

        for(j=0; j<pkt->msgCnt; ++j)
          if( pkt->msgArray != NULL )
            printf("%ld %ld 0x%x %i %i\n", pkt->msgArray[j].timeStamp.tv_sec, pkt->msgArray[j].timeStamp.tv_nsec, pkt->msgArray[j].status,pkt->msgArray[j].d0, pkt->msgArray[j].d1);
          else
            printf("0x%x ",pkt->sysExMsg[j]);

      }
    }
    
    rc_t _midiPortCreate( io_t* p, const object_t*& c )
    {
      rc_t     rc             = kOkRC;
      unsigned parserBufByteN = 1024;
      
      if((rc = c->getv(
            "parserBufByteN", parserBufByteN )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"MIDI configuration parse failed.");
      }
          
      // initialie the MIDI system
      if((rc = create(p->midiH, _midiCallback, p, parserBufByteN, "app")) != kOkRC )
        return rc;

      
      return rc;
    }

    void _audioDeviceCallback( void* cbArg, audio::device::audioPacket_t* inPktArray, unsigned inPktCnt, audio::device::audioPacket_t* outPktArray, unsigned outPktCnt )
    {
      //io_t* p = (io_t*)cbArg;
      
    }

    rc_t _audioDeviceConfig( io_t* p, const object_t* c )
    {
      rc_t            rc      = kOkRC;
      unsigned        meterMs = 50;
      const object_t* node    = nullptr;
      const object_t* deviceL = nullptr;
      
      // get the audio port node
      if((node = c->find("audio")) == nullptr )
        return cwLogError(kSyntaxErrorRC,"Unable to locate the 'audio' configuration node.");

      // get the meterMs value
      if((rc = node->get("meterMs", meterMs )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Audio 'meterMs' parse failed.");
        goto errLabel;
      }

      // get the audio device list
      if((deviceL = node->find("deviceL")) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"Audio 'deviceL' failed.");
        goto errLabel;
      }

      // create an audio device cfg list
      p->audioCfgN = deviceL->child_count();
      p->audioCfgA = mem::allocZ<audioCfg_t>(p->audioCfgN);

      // fill in the audio device cfg list
      for(unsigned i=0; i<p->audioCfgN; ++i)
      {
        audioCfg_t* r = p->audioCfgA + i;
        if((node = deviceL->list_ele(i)) == nullptr )
        {
          if(( rc = node->getv(
                "enableFl",    r->enableFl,
                "name",        r->name,
                "device",      r->device,
                "srate",       r->srate,
                "dspFrameCnt", r->dspFrameCnt,
                "cycleCnt",    r->cycleCnt )) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing audio cfg record at index:%i",i);
            goto errLabel;
          }          
        }

        // if the configuration is enabled
        if( r->enableFl )
        {
          // get the hardware device index
          if((r->devIdx = audio::device::deviceLabelToIndex( p->audioH, r->device)) == kInvalidIdx )
          {
            rc = cwLogError(rc,"Unable to locate the audio hardware device:'%s'.", r->device);
            goto errLabel;
          }

          // setup the device based on the configuration
          if((rc = audio::device::deviceSetup(p->audioH,r->devIdx,r->srate,r->dspFrameCnt,_audioDeviceCallback,p)) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to setup the audio hardware device:'%s'.", r->device);
            goto errLabel;
          }          
        }
      }

    errLabel:
      return rc;
    }


    rc_t _audioDeviceCreate( io_t* p, const object_t*& c )
    {      
      rc_t                     rc       = kOkRC;
      audio::device::driver_t* audioDrv = nullptr;

      // initialize the audio device interface  
      if((rc = audio::device::create(p->audioH)) != kOkRC )
      {
        cwLogInfo("Initialize failed.");
        goto errLabel;
      }

      // initialize the ALSA device driver interface
      if((rc = audio::device::alsa::create(p->alsaH, audioDrv )) != kOkRC )
      {
        cwLogInfo("ALSA initialize failed.");
        goto errLabel;
      }

      // register the ALSA device driver with the audio interface
      if((rc = audio::device::registerDriver( p->audioH, audioDrv )) != kOkRC )
      {
        cwLogInfo("ALSA driver registration failed.");
        goto errLabel;
      }

      // read the configuration information and setup the audio hardware
      if((rc = _audioDeviceConfig( p, c )) != kOkRC )
      {
        cwLogInfo("Audio device configuration failed.");
        goto errLabel;
      }


    errLabel:
      return rc;
    }

  }
}



cw::rc_t cw::io::create( handle_t& h, const char* cfgStr, cbFunc_t cbFunc, void* cbArg, const char* cfgLabel )
{
  rc_t            rc;
  object_t*       obj_base = nullptr;
  const object_t* o        = nullptr;
  
  if((rc = destroy(h)) != kOkRC )
    return rc;

  // create the io_t object
  io_t* p = mem::allocZ<io_t>();

  // parse the configuration string
  if((rc = objectFromString( cfgStr, obj_base)) != kOkRC )
  {
    rc = cwLogError(rc,"Configuration parse failed.");
    goto errLabel;
  }

  // get the main io cfg object.
  if((o = obj_base->find(cfgLabel)) == nullptr )
  {
    rc = cwLogError(kSyntaxErrorRC,"Unable to locate the I/O cfg. label:%s.",cfgLabel);
    goto errLabel;
  }

  // create the serial port device
  if((rc = _serialPortCreate(p,o)) != kOkRC )
    goto errLabel;

  // create the MIDI port device
  if((rc = _midiPortCreate(p,o)) != kOkRC )
    goto errLabel;

  // create the Audio device interface
  if((rc = _audioDeviceCreate(p,o)) != kOkRC )
    goto errLabel;
  
  // create the the thread
  if((rc = thread::create( p->threadH, _mainThreadFunc, p)) != kOkRC )
    goto errLabel;

  p->cbFunc = cbFunc;
  p->cbArg  = cbArg;
  
 errLabel:
  if(rc != kOkRC )
    _destroy(p);
  
  return rc;
}


cw::rc_t cw::io::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  
  if( !h.isValid() )
    return rc;

  io_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}

cw::rc_t cw::io::start( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return thread::pause( p->threadH, thread::kWaitFl );
}

cw::rc_t cw::io::pause( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return thread::pause( p->threadH, thread::kPauseFl | thread::kWaitFl );
}




unsigned cw::io::serialDeviceCount( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return p->serialN;
}

const char* cw::io::serialDeviceName(  handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  return p->serialA[devIdx].name;
}

unsigned cw::io::serialDeviceIndex( handle_t h, const char* name )
{
  io_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->serialN; ++i)
    if( textCompare(name,p->serialA[i].name) == 0 )
      return i;

  return kInvalidIdx;
}

cw::rc_t cw::io::serialDeviceSend(  handle_t h, unsigned devIdx, const void* byteA, unsigned byteN )
{
  rc_t  rc = kOkRC;
  //io_t* p  = _handleToPtr(h);
  return rc;
}



unsigned    cw::io::midiDeviceCount( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return midi::device::count(p->midiH);
}

const char* cw::io::midiDeviceName( handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  return midi::device::name(p->midiH,devIdx);
}

unsigned cw::io::midiDeviceIndex( handle_t h, const char* devName )
{
  io_t* p = _handleToPtr(h);
  return midi::device::nameToIndex(p->midiH, devName);
}

unsigned cw::io::midiDevicePortCount( handle_t h, unsigned devIdx, bool inputFl )
{
  io_t* p = _handleToPtr(h);
  return midi::device::portCount(p->midiH, devIdx, inputFl ? midi::kInMpFl : midi::kOutMpFl );
}

const char* cw::io::midiDevicePortName( handle_t h, unsigned devIdx, bool inputFl, unsigned portIdx )
{
  io_t* p = _handleToPtr(h);
  return midi::device::portName( p->midiH, devIdx, inputFl ? midi::kInMpFl : midi::kOutMpFl, portIdx ); 
}

unsigned cw::io::midiDevicePortIndex( handle_t h, unsigned devIdx, bool inputFl, const char* portName )
{
  io_t* p = _handleToPtr(h);
  return midi::device::portNameToIndex( p->midiH, devIdx, inputFl ? midi::kInMpFl : midi::kOutMpFl, portName );
}
      
cw::rc_t cw::io::midiDeviceSend( handle_t h, unsigned devIdx, unsigned portIdx, midi::byte_t status, midi::byte_t d0, midi::byte_t d1 )
{
  rc_t rc = kOkRC;
  //io_t* p = _handleToPtr(h);
  //return midi::device::send( p->midiH, devIdx, portIdx, status, d0, d1 );
  return rc;
}


unsigned    cw::io::audioDeviceCount(          handle_t h )
{
  io_t* p = _handleToPtr(h);
  return p->audioCfgN;
}

unsigned    cw::io::audioDeviceLabelToIndex(   handle_t h, const char* label )
{
  io_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->audioCfgN; ++i)
    if( strcmp(p->audioCfgA[i].name,label) == 0 )
      return i;
  
  return kInvalidIdx;
}

const char* cw::io::audioDeviceLabel(          handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  assert( devIdx < p->audioCfgN );
  return p->audioCfgA[ devIdx ].name;
}

cw::rc_t    cw::io::audioDeviceStart(          handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  assert( devIdx < p->audioCfgN );
  return audio::device::deviceStart( p->audioH, p->audioCfgA[ devIdx ].devIdx ); 
}

cw::rc_t    cw::io::audioDeviceStop(           handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  assert( devIdx < p->audioCfgN );
  return audio::device::deviceStop( p->audioH, p->audioCfgA[ devIdx ].devIdx );
}

bool        cw::io::audioDeviceIsStarted(      handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  assert( devIdx < p->audioCfgN );
  return audio::device::deviceIsStarted( p->audioH, p->audioCfgA[ devIdx ].devIdx );
}
