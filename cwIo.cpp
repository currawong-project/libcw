#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwText.h"
#include "cwTextBuf.h"

#include "cwIo.h"

#include "cwMidi.h"
#include "cwMidiPort.h"


#include "cwObject.h"

#include "cwThread.h"
#include "cwThreadMach.h"
#include "cwMutex.h"

#include "cwSerialPort.h"
#include "cwSerialPortSrv.h"

#include "cwAudioDevice.h"
#include "cwAudioBuf.h"
#include "cwAudioDeviceAlsa.h"

#include "cwSocket.h"

#include "cwWebSock.h"
#include "cwUi.h"


namespace cw
{
  namespace io
  {

    struct io_str;

    typedef struct thread_str
    {
      unsigned           id;
      void*              arg;
      bool               asyncFl;
      struct io_str*     p;
      struct thread_str* link;
    } thread_t;
    
    typedef struct timer_str
    {
      struct io_str*    io;
      bool              deletedFl;
      bool              startedFl;
      char*             label;
      unsigned          id;
      unsigned          periodMicroSec;
      bool              asyncFl;
    } timer_t;
    
    typedef struct serialPort_str
    {
      char*                   label;    
      unsigned                userId;
      bool                    asyncFl;
      char*                   device;
      unsigned                baudRate;
      unsigned                flags;
      serialPortSrv::handle_t serialH;
    } serialPort_t;

    typedef struct audioGroup_str
    {
      bool                   enableFl;
      bool                   asyncFl;
      audio_msg_t            msg;
      mutex::handle_t        mutexH;
      unsigned               threadTimeOutMs;
      struct io_str*         p;
    } audioGroup_t;

    typedef struct audioDev_str
    {
      bool               enableFl; // True if this device was enabled by the user
      const char*        label;    // User label
      unsigned           userId;   // User id
      char*              devName;  // System device name
      unsigned           devIdx;   // AudioDevice interface device index
      audioGroup_t*      iGroup;   // Audio group pointers for this device
      audioGroup_t*      oGroup;   //
      audio_group_dev_t* iagd;     // Audio group device record assoc'd with this device
      audio_group_dev_t* oagd;     //
    } audioDev_t;

    typedef struct socket_str
    {
      bool     enableFl;
      bool     asyncFl;
      char*    label;
      unsigned sockA_index;
      unsigned userId;      
    } socket_t;

    typedef struct io_str
    {
      std::atomic<bool>             quitFl;

      time::spec_t                  t0;
      
      cbFunc_t                      cbFunc;
      void*                         cbArg;

      mutex::handle_t               cbMutexH;
      unsigned                      cbMutexTimeOutMs;
      
      thread_mach::handle_t         threadMachH;
      
      object_t*                     cfg;

      thread_t*                     threadL;
      
      timer_t*                      timerA;
      unsigned                      timerN;
      
      serialPort_t*                 serialA;
      unsigned                      serialN;
      serialPortSrv::handle_t       serialPortSrvH;
      
      midi::device::handle_t        midiH;
      bool                          midiAsyncFl;

      socket_t*                     sockA;
      unsigned                      sockN;
      sock::handle_t                sockH;
      unsigned                      sockThreadTimeOutMs;
      
      
      audio::device::handle_t       audioH;
      audio::device::alsa::handle_t alsaH;
      audio::buf::handle_t          audioBufH;
      unsigned                      audioThreadTimeOutMs;
      unsigned                      audioMeterDevEnabledN;
      unsigned                      audioMeterCbPeriodMs;
      time::spec_t                  audioMeterNextTime;
      
      audioDev_t*                   audioDevA;
      unsigned                      audioDevN;
      
      audioGroup_t*                 audioGroupA;
      unsigned                      audioGroupN;
      
      ui::ws::handle_t              wsUiH;       // ui::ws handle (invalid if no UI was specified)
      ui::appIdMap_t*               uiMapA;      // Application supplied id's for the UI resource supplied with the cfg script via 'uiCfgFn'.
      unsigned                      uiMapN;      //
      bool                          uiAsyncFl;
      
    } io_t;
  

    //----------------------------------------------------------------------------------------------------------
    //
    // io
    //
    
    io_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,io_t>(h); }


    // All callbacks to the application occur through this function
    rc_t _ioCallback( io_t* p, bool asyncFl, const msg_t* m, rc_t* app_rc_ref=nullptr )
    {
      rc_t rc       = kOkRC;
      bool unlockFl = false;
      bool isSynchronousFl = !asyncFl;

      // if this is a synchronous callback then lock the mutex
      if( isSynchronousFl )
      {
        switch(rc = mutex::lock(p->cbMutexH,p->cbMutexTimeOutMs))
        {
          case kOkRC:
            unlockFl = true;
            break;
            
          case kTimeOutRC:
            rc = cwLogError(rc,"io mutex callback mutex lock timed out.");
            break;
            
          default:
            rc = cwLogError(rc,"io mutex callback mutex lock failed.");
        }
           
      }

      // make the callback to the client
      if( rc == kOkRC )
      {
        rc_t app_rc = p->cbFunc( p->cbArg, m );
        if( app_rc_ref != nullptr )
          *app_rc_ref = app_rc;
      }

      // if the mutex is locked
      if( unlockFl )
      {
        if((rc = mutex::unlock(p->cbMutexH)) != kOkRC )
        {
          rc = cwLogError(rc,"io mutex callback mutex unlock failed.");          
        }
      }

      return rc;
    }

    rc_t _ioParse( io_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;
      const object_t* ioCfg;
      if((ioCfg = cfg->find("io")) == nullptr )
      {
        cwLogError(kInvalidArgRC,"The 'io' configuration block could not be found.");
        goto errLabel;
      }

      if((rc = ioCfg->getv("callbackMutexTimeOutMs",p->cbMutexTimeOutMs)) != kOkRC )
      {
        cwLogError(rc,"Parsing of 'io' block configuration failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }
    
    //----------------------------------------------------------------------------------------------------------
    //
    // Thread
    //
    bool _threadFunc( void* arg )
    {
      rc_t        rc  = kOkRC;
      thread_t*    t  = (thread_t*)arg;
      thread_msg_t tm = { .id=t->id, .arg=t->arg };
      msg_t        m;

      m.tid       = kThreadTId;
      m.u.thread = &tm;
    
      if((rc = _ioCallback( t->p, t->asyncFl, &m )) != kOkRC )
        cwLogError(rc,"Thread app callback failed.");

      return true;
    }

    void _threadRelease( io_t* p )
    {
      thread_t* t0 = p->threadL;
      for(; t0!=nullptr; t0=t0->link)
      {
        thread_t* t1 = t0->link;
        mem::release(t0);
        t0 = t1;
      }
    }
    
    //----------------------------------------------------------------------------------------------------------
    //
    // Timer
    //
    bool _timerThreadCb( void* arg )
    {
      timer_t* t = (timer_t*)arg;

      sleepUs( t->periodMicroSec );

      if( t->startedFl && !t->deletedFl )
      {
        rc_t        rc = kOkRC;
        msg_t       m;        
        timer_msg_t tm;
        
        tm.id     = t->id;
        m.tid     = kTimerTId;
        m.u.timer = &tm;
        
        if((rc = _ioCallback( t->io, t->asyncFl, &m )) != kOkRC )
          cwLogError(rc,"Timer app callback failed.");
      }

      return !t->deletedFl;
    }

    rc_t _timerCreate( io_t* p, const char* label, unsigned id, unsigned periodMicroSec, bool asyncFl )
    {
      rc_t     rc = kOkRC;
      timer_t* t  = nullptr;

      // look for a deleted timer
      for(unsigned i=0; i<p->timerN; ++i)
        if( p->timerA[i].deletedFl )
        {
          t = p->timerA + i;
          break;
        }

      // if no deleted timer was found
      if( t == nullptr )
      {
        // reallocate the timer array with an additional slot
        timer_t* tA = mem::allocZ< timer_t >( p->timerN + 1 );
        for(unsigned i=0; i<p->timerN; ++i)
          tA[i] = p->timerA[i];

        // keep a pointer to the empty slot
        t = tA + p->timerN;

        // update the timer array
        mem::release( p->timerA );
        p->timerA = tA;
        p->timerN = p->timerN + 1;
      }
        
      assert( t != nullptr );

      t->io             = p;
      t->label          = mem::duplStr(label);
      t->id             = id;
      t->asyncFl        = asyncFl;
      t->periodMicroSec = periodMicroSec;

      if((rc = thread_mach::add(p->threadMachH,_timerThreadCb,t)) != kOkRC )
      {
        rc = cwLogError(rc,"Timer thread assignment failed.");        
      }

      return rc;
    }

    timer_t* _timerIndexToPtr( io_t* p, unsigned timerIdx )
    {
      if( timerIdx >= p->timerN || p->timerA[ timerIdx ].deletedFl == true )
      {
        cwLogError(kInvalidIdRC,"The timer index '%i' is invalid.", timerIdx );
        return nullptr;
      }

      return p->timerA + timerIdx;
    }

    rc_t    _timerStart( io_t* p, unsigned timerIdx, bool startFl )
    {
      rc_t     rc = kOkRC;
      timer_t* t  = _timerIndexToPtr(p,timerIdx);

      if( t == nullptr )
        rc = kInvalidIdRC;
      else
        p->timerA[ timerIdx ].startedFl = startFl;
    
      return rc;
    }
    
    
    //----------------------------------------------------------------------------------------------------------
    //
    // Serial
    //    
  
    void _serialPortCb( void* arg, unsigned serialCfgIdx, const void* byteA, unsigned byteN )
    {
      io_t* p = (io_t*)arg;

      if( serialCfgIdx > p->serialN )
        cwLogError(kAssertFailRC,"The serial cfg index %i is out of range %i in serial port callback.", serialCfgIdx, p->serialN );
      else
      {
        rc_t rc = kOkRC;
        const serialPort_t* sp = p->serialA + serialCfgIdx;
        msg_t m;
        serial_msg_t sm;
        sm.label  = sp->label;
        sm.userId = sp->userId;
        sm.dataA  = byteA;
        sm.byteN  = byteN;
        
        m.tid      = kSerialTId;
        m.u.serial = &sm;

        if((rc = _ioCallback( p, sp->asyncFl,  &m )) != kOkRC )
          cwLogError(rc,"Serial port app callback failed.");
      }
      
    }


    rc_t _serialPortParseCfg( const object_t& e, serialPort_t* port, bool& enableFlRef )
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
                      "enableFl",     enableFlRef,
                      "asyncFl",      port->asyncFl,
                      "label",        port->label,
                      "device",       port->device,
                      "baud",         port->baudRate,
                      "bits",         bits,
                      "stop",         stop,
                      "parity",       parityLabel )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Serial configuration parse failed.");
      }

      switch( bits )
      {
        case 5:  port->flags |= serialPort::kDataBits5Fl; break;
        case 6:  port->flags |= serialPort::kDataBits6Fl; break;
        case 7:  port->flags |= serialPort::kDataBits7Fl; break;
        case 8:  port->flags |= serialPort::kDataBits8Fl; break;
        default:
          rc = cwLogError(kSyntaxErrorRC,"Invalid serial data bits cfg:%i.",bits);
      }

      switch( stop )
      {
        case 1: port->flags |= serialPort::k1StopBitFl; break;
        case 2: port->flags |= serialPort::k2StopBitFl; break;
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
      rc_t            rc           = kOkRC;
      const object_t* cfg          = nullptr;
      const object_t* port_array   = nullptr;
      unsigned        pollPeriodMs = 50;
      unsigned        recvBufByteN = 512;

      // get the serial port list node
      if((cfg = c->find("serial")) == nullptr)
      {
        cwLogWarning("No 'serial' configuration.");
        return kOkRC;
      }

      // the serial header values
      if((rc = cfg->getv("pollPeriodMs", pollPeriodMs,
                         "recvBufByteN", recvBufByteN,
                         "array",        port_array)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Serial cfg header parse failed.");
        goto errLabel;
      }

      p->serialN = port_array->child_count();
      p->serialA = mem::allocZ<serialPort_t>(p->serialN);

      
      // create the serial server
      if((rc = serialPortSrv::create(p->serialPortSrvH,pollPeriodMs,recvBufByteN)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Serial port server failed.");
        goto errLabel;
      }
      
      // for each serial port cfg
      for(unsigned i=0; i<p->serialN; ++i)
      {
        const object_t* e = port_array->child_ele(i);
        serialPort_t*   r = p->serialA + i;
        
        if( e == nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unable to access a 'serial' port configuration record at index:%i.",i);
          break;
        }
        else
        {
          bool enableFl = false;
          
          // parse the cfg record
          if((rc = _serialPortParseCfg(*e,r,enableFl)) != kOkRC )
          {
            rc = cwLogError(rc,"Serial configuration parse failed on record index:%i.", i );
            break;
          }

          if( enableFl )
          {
            r->userId = i; // default the serial port userId to the index into serialA[]

            serialPort::handle_t spH = serialPortSrv::serialHandle(p->serialPortSrvH);

          
            // create the serial port object
            if((rc = serialPort::createPort( spH, i, r->device, r->baudRate, r->flags, _serialPortCb, p )) != kOkRC )
            {
              rc = cwLogError(rc,"Serial port create failed on record index:%i.", i );
              break;
            }
          }
        }
      }

    errLabel:
      return rc;
    }

    void _serialPortDestroy( io_t* p )
    {
      serialPortSrv::destroy(p->serialPortSrvH);
                            
      mem::release(p->serialA);
      p->serialN = 0;
      
    }

    rc_t _serialPortStart( io_t* p )
    {
      rc_t rc = kOkRC;

      if( p->serialPortSrvH.isValid() ) 
        // the service is only started if at least one serial port is enabled
        if( serialPort::portCount( serialPortSrv::serialHandle(p->serialPortSrvH) ) > 0 )      
          if((rc =serialPortSrv::start( p->serialPortSrvH )) != kOkRC )
            rc = cwLogError(rc,"The serial port server start failed.");
      
      return rc;
    }


    //----------------------------------------------------------------------------------------------------------
    //
    // MIDI
    //
    void _midiCallback( const midi::packet_t* pktArray, unsigned pktCnt )
    {
      unsigned i;
      for(i=0; i<pktCnt; ++i)
      {
        msg_t                 m;
        midi_msg_t            mm;
        const midi::packet_t* pkt = pktArray + i;        
        io_t*                 p   = reinterpret_cast<io_t*>(pkt->cbDataPtr);
        rc_t                  rc  = kOkRC;

        
        mm.pkt   = pkt;
        m.tid    = kMidiTId;
        m.u.midi = &mm;

        if((rc = _ioCallback( p, p->midiAsyncFl, &m )) !=kOkRC )
          cwLogError(rc,"MIDI app callback failed.");

        /*
        for(unsigned j=0; j<pkt->msgCnt; ++j)
          if( pkt->msgArray != NULL )
            printf("io midi cb: %ld %ld 0x%x %i %i\n", pkt->msgArray[j].timeStamp.tv_sec, pkt->msgArray[j].timeStamp.tv_nsec, pkt->msgArray[j].status,pkt->msgArray[j].d0, pkt->msgArray[j].d1);
          else
            printf("io midi cb: 0x%x ",pkt->sysExMsg[j]);
        */
      }
    }
    
    rc_t _midiPortCreate( io_t* p, const object_t* c )
    {
      rc_t     rc             = kOkRC;
      unsigned parserBufByteN = 1024;
      const object_t* cfg = nullptr;

      // get the MIDI port cfg
      if((cfg = c->find("midi")) == nullptr)
      {
        cwLogWarning("No 'MIDI' configuration.");
        return kOkRC;
      }
      
      if((rc = cfg->getv("parserBufByteN", parserBufByteN,
                         "asyncFl", p->midiAsyncFl )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"MIDI configuration parse failed.");
      }
          
      // initialie the MIDI system
      if((rc = create(p->midiH, _midiCallback, p, parserBufByteN, "app")) != kOkRC )
        return rc;

      
      return rc;
    }

    //------------------------------------------------------------------------------------------------
    //
    // Socket
    //

    socket_t* _socketIndexToRecd( io_t* p, unsigned sockIdx )
    {
      if( sockIdx >= p->sockN )
        cwLogError(kInvalidArgRC,"Invalid socket index (%i >= %i)", sockIdx, p->sockN);
      return p->sockA + sockIdx;
    }
    
    bool _socketThreadFunc( void* arg )
    {
      rc_t     rc        = kOkRC;
      io_t*    p         = reinterpret_cast<io_t*>(arg);
      unsigned readByteN = 0;
      
      if((rc = receive_all(p->sockH, p->sockThreadTimeOutMs, readByteN)) != kOkRC )
      {
        if( rc != kTimeOutRC )
          cwLogWarning("Socket receive_all() failed.");
      }
      

      return true;
    }
    
    void _socketCallback( void* cbArg, sock::cbOpId_t cbId, unsigned sockArray_index, unsigned connId, const void* byteA, unsigned byteN, const struct sockaddr_in* srcAddr )
    {
      io_t* p = reinterpret_cast<io_t*>(cbArg);

      if( sockArray_index >= p->sockN )
        cwLogError(kInvalidArgRC,"The socket index '%i' outside range (0-%i).", sockArray_index, p->sockN );
      else
      {
        socket_msg_t sm;
        sm.cbId    = cbId;
        sm.sockIdx = sockArray_index,
        sm.userId  = p->sockA[ sockArray_index ].userId;
        sm.connId  = connId;
        sm.byteA   = byteA;
        sm.byteN   = byteN;
        sm.srcAddr = srcAddr;

        msg_t m;
        m.tid    = kSockTId;
        m.u.sock = &sm;

        rc_t rc;
        
        if((rc = _ioCallback( p, p->sockA[ sockArray_index ].asyncFl, &m )) != kOkRC )
          cwLogError(rc,"Socket app callback failed.");
      }
    }
    
    
    rc_t _socketParseAttrs( const object_t* attrL, unsigned& flagsRef )
    {
      rc_t            rc    = kOkRC;
      const object_t* node  = nullptr;

      idLabelPair_t attrA[] =
        {
          { .id=sock::kNonBlockingFl,   .label="non_blocking"   }, // Create a non-blocking socket.
          { .id=sock::kBlockingFl,      .label="blocking"       }, // Create a blocking socket.
          { .id=sock::kTcpFl,           .label="tcp"            }, // Create a TCP socket rather than a UDP socket.
          { .id=0,                      .label="udp"            }, //
          { .id=sock::kBroadcastFl,     .label="broadcast"      }, //
          { .id=sock::kReuseAddrFl,     .label="reuse_addr"     }, //
          { .id=sock::kReusePortFl,     .label="reuse_port"     }, //
          { .id=sock::kMultiCastTtlFl,  .label="multicast_ttl"  }, //
          { .id=sock::kMultiCastLoopFl, .label="multicast_loop" }, //
          { .id=sock::kListenFl,        .label="listen"         }, // Use this socket to listen for incoming connections
          { .id=sock::kStreamFl,        .label="stream"         }, // Connected stream (vs. Datagram)
          { .id=0,                      .label=nullptr,         }
        };

      flagsRef = 0;
      
      for(unsigned j=0; j<attrL->child_count(); ++j)
        if((node = attrL->child_ele(j)) != nullptr && node->is_type( kStringTId ) )
        {
          unsigned k;
          for(k=0; attrA[k].label != nullptr; ++k)
            if( textCompare(attrA[k].label, node->u.str) == 0 )
            {
              flagsRef += attrA[k].id;
              break;
            }

          if( attrA[k].label == nullptr )
          {
            rc = cwLogError(kInvalidArgRC,"The attribute label '%s'.",cwStringNullGuard(node->u.str));
          }
        }

      return rc;
    }

    rc_t _socketParseConfig( io_t* p, const object_t* cfg )
    {
      const object_t* node           = nullptr;
      rc_t            rc             = kOkRC;
      unsigned        maxSocketCnt   = 10;
      unsigned        recvBufByteCnt = 4096;
      const object_t* socketL        = nullptr;
      
      // get the socket configuration node
      if((node = cfg->find("socket")) == nullptr )
      {
        cwLogWarning("No 'socket' configuration node.");
        return kOkRC;
      }

      // get the required socket arguments
      if(( rc = node->getv(
            "maxSocketCnt",       maxSocketCnt,
            "recvBufByteCnt",     recvBufByteCnt,
            "threadTimeOutMs",    p->sockThreadTimeOutMs,
            "socketL",            socketL )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Unable to parse the 'socket' configuration node.");
        goto errLabel;
      }

      // THe max socket count must be at least as large as the number of defined sockets
      maxSocketCnt = std::max(p->sockN,maxSocketCnt);

      // create the socket control array
      p->sockN = socketL->child_count();
      p->sockA = mem::allocZ<socket_t>(p->sockN);

      // create the socket manager
      if((rc = sock::createMgr( p->sockH, recvBufByteCnt, maxSocketCnt )) != kOkRC )
      {
        rc = cwLogError(rc,"Socket manager creation failed.");
        goto errLabel;
      }

      // parse each socket configuration
      for(unsigned i=0; i<p->sockN; ++i)
      {
        if((node = socketL->child_ele(i)) != nullptr )
        {
          unsigned        port       = sock::kInvalidPortNumber;
          unsigned        timeOutMs  = 50; 
          const object_t* attrL      = nullptr;
          char*           remoteAddr = nullptr;
          unsigned        remotePort = sock::kInvalidPortNumber;
          char*           localAddr  = nullptr;
          unsigned        flags      = 0;

          // parse the required arguments
          if(( rc = node->getv(
                "enableFl",  p->sockA[i].enableFl,
                "asyncFl",   p->sockA[i].asyncFl,
                "label",     p->sockA[i].label,
                "port",      port,
                "timeOutMs", timeOutMs,
                "attrL",     attrL )) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing required socket cfg record at index:%i",i);
            goto errLabel;
          }

          // parse the optional arguments
          if((rc = node->getv_opt(
                "userId",     p->sockA[i].userId,
                "remoteAddr", remoteAddr,
                "remotePort", remotePort,
                "localAddr",  localAddr)) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing optional socket cfg record at index:%i",i);
            goto errLabel;
          }

          // parse the socket attribute list
          if((rc = _socketParseAttrs( attrL, flags )) != kOkRC )
            goto errLabel;

          // create the socket object
          if((rc = create( p->sockH, i, port, flags, timeOutMs, _socketCallback, p, remoteAddr, remotePort, localAddr )) != kOkRC )
          {
            rc = cwLogError(rc,"Socket create failed.");
            goto errLabel;
          }
          
        }
      }

      // create the socket thread
      if( p->sockN > 0 )
        if((rc = thread_mach::add(p->threadMachH,_socketThreadFunc,p)) != kOkRC )
        {
          rc = cwLogError(rc,"Error creating socket thread.");
          goto errLabel;
        }
      

    errLabel:
      return rc;
    }
    

    
    //----------------------------------------------------------------------------------------------------------
    //
    // Audio
    //

    // Start or stop all the audio devices in p->audioDevA[]
    rc_t _audioDeviceStartStop( io_t* p, bool startFl )
    {
      rc_t rc = kOkRC;
      
      for(unsigned i=0; i<p->audioDevN; ++i)
        if( p->audioDevA[i].enableFl )
        {
          rc_t rc0 = kOkRC;
          if( startFl )
            rc0 = audio::device::start( p->audioH, p->audioDevA[i].devIdx );
          else
            rc0 = audio::device::stop( p->audioH, p->audioDevA[i].devIdx );

          if(rc0 != kOkRC )
            rc = cwLogError(rc0,"The audio device: %s failed to %s.", cwStringNullGuard(p->audioDevA[i].devName), startFl ? "start" : "stop");

        }      

      return rc;
    }


    // Release all resource associated with a group-device record.
    void _audioGroupDestroyDevs( audio_group_dev_t* agd )
    {
      while( agd != nullptr )
      {
        audio_group_dev_t* agd0 = agd->link;
        mem::release(agd->meterA);
        mem::release(agd);
        agd = agd0;
      }
    }

    // Release all resource associated with all audio group records
    rc_t _audioGroupDestroyAll( io_t* p )
    {
      rc_t rc = kOkRC;

      for(unsigned i=0; i<p->audioGroupN; ++i)
      {
        audioGroup_t* ag = p->audioGroupA + i;
        _audioGroupDestroyDevs( ag->msg.iDevL );
        _audioGroupDestroyDevs( ag->msg.oDevL );
        mem::release(ag->msg.iBufArray);
        mem::release(ag->msg.oBufArray);

        mutex::unlock( ag->mutexH );  // the mutex is expected to be locked at this point
        mutex::destroy( ag->mutexH );
      }

      mem::release(p->audioGroupA);
      p->audioGroupN = 0;
      return rc;
    }

    rc_t _audioDestroy( io_t* p )
    {
      rc_t rc = kOkRC;

      // stop each device - this will stop the callbacks to _audioDeviceCallback()
      if((rc = _audioDeviceStartStop(p,false)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio device stop failed.");
        goto errLabel;
      }


      if((rc = audio::device::alsa::destroy(p->alsaH)) != kOkRC )
      {
        rc = cwLogError(rc,"ALSA sub-system shutdown failed.");
        goto errLabel;
      }
      
            
      if((rc = audio::device::destroy(p->audioH)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio device sub-system shutdown failed.");
        goto errLabel;
      }

      
      if((rc = audio::buf::destroy(p->audioBufH)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio buffer release failed.");
        goto errLabel;
      }

      if((rc = _audioGroupDestroyAll(p)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio group release failed.");        
        goto errLabel;
      }

      for(unsigned i=0; i<p->audioDevN; ++i)
        mem::release( p->audioDevA[i].devName );
      
      mem::free(p->audioDevA);
      p->audioDevN = 0;

    errLabel:

      if(rc != kOkRC )
        rc = cwLogError(rc,"Audio sub-system shutdown failed.");
      
      return rc;
      
    }

    
    // Are either input or output meter enabled on this audio devices.
    // Set flags to kInFl or kOutput to select inut or output meters.
    // Set both flags to check if either input or output meters are enabled.
    bool _audioDeviceIsMeterEnabled( audioDev_t* ad, unsigned flags )
    {
      return (cwIsFlag(flags,kInFl ) && ad->iagd!=nullptr && cwIsFlag(ad->iagd->flags,kMeterFl))
        ||   (cwIsFlag(flags,kOutFl) && ad->oagd!=nullptr && cwIsFlag(ad->oagd->flags,kMeterFl));
    }

    
    rc_t _audioGroupDeviceProcessMeter( io_t* p, audio_group_dev_t* agd, audioGroup_t* ag, unsigned audioBufFlags )
    {
      rc_t rc = kOkRC;
      if( agd != nullptr && ag != nullptr && cwIsFlag(agd->flags,kMeterFl))
      {
        rc_t app_rc = kOkRC;
        msg_t m;
        m.tid             = kAudioMeterTId;
        m.u.audioGroupDev = agd;

        // get the current meter values from the audioBuf
        audio::buf::meter(p->audioBufH, agd->devIdx, audioBufFlags, agd->meterA, agd->chCnt );

        // callback the application with the current meter values
        if((rc = _ioCallback( p, ag->asyncFl, &m, &app_rc )) != kOkRC )
          cwLogError(rc,"Audio meter app callback failed.");
        
        rc = app_rc;
      }

      return rc;
    }
    
    rc_t _audioDeviceProcessMeters( io_t* p )
    {
      rc_t rc = kOkRC;

      // if it is time to execute the next meter update
      if( time::isGTE(p->t0,p->audioMeterNextTime) )
      {
        for(unsigned i=0; i<p->audioDevN; ++i)
          if( p->audioDevA[i].enableFl )
          {
            audioDev_t* ad = p->audioDevA + i;
            rc_t rc0;

            // update the input meters
            if((rc0 = _audioGroupDeviceProcessMeter( p, ad->iagd,  ad->iGroup, audio::buf::kInFl )) != kOkRC )
              rc = rc0;

            // update the output meters
            if((rc0 = _audioGroupDeviceProcessMeter( p, ad->oagd,  ad->oGroup, audio::buf::kOutFl )) != kOkRC )
              rc = rc0;          
          }


        // schedule the next meter update
        p->audioMeterNextTime = p->t0;
        time::advanceMs(p->audioMeterNextTime,p->audioMeterCbPeriodMs);
      }
      
      return rc;
    }
    

    bool _audioGroupBufIsReady( io_t* p, audioGroup_t* ag, bool inputFl )
    {
      audio_group_dev_t* agd =  inputFl ? ag->msg.iDevL : ag->msg.oDevL;      
      for(; agd!=nullptr; agd=agd->link)
        if( !audio::buf::isDeviceReady( p->audioBufH, agd->devIdx, inputFl ? audio::buf::kInFl : audio::buf::kOutFl ) )
          return false;

      return true;          
    }

    // Get sample ponters or advance the pointers on the buffer associates with each device.
    enum { kAudioGroupGetBuf, kAudioGroupAdvBuf };
    void _audioGroupProcSampleBufs( io_t* p, audioGroup_t* ag, unsigned processTypeId, unsigned inputFl  )
    {
      sample_t** bufArray      = inputFl ? ag->msg.iBufArray   : ag->msg.oBufArray;
      unsigned   bufArrayChCnt = inputFl ? ag->msg.iBufChCnt   : ag->msg.oBufChCnt;
      audio_group_dev_t* agd   = inputFl ? ag->msg.iDevL       : ag->msg.oDevL;
      unsigned   audioBufFlags = inputFl ? audio::buf::kInFl : audio::buf::kOutFl;
      
      unsigned chIdx = 0;
      for(; agd!=nullptr && chIdx < bufArrayChCnt; agd=agd->link)
      {
        switch( processTypeId )
        {
          case kAudioGroupGetBuf:
            audio::buf::get( p->audioBufH, agd->devIdx, audioBufFlags, bufArray+chIdx, agd->chCnt );
            break;
            
          case kAudioGroupAdvBuf:
            audio::buf::advance( p->audioBufH, agd->devIdx, audioBufFlags );
            break;

          default:
            assert(0);
        }
        
        chIdx += agd->chCnt;
      }
    }

    // This is the audio processing thread function. Block on the audio group condition var
    // which is triggered when all the devices in the group are ready by _audioGroupNotifyIfReady().    
    bool _audioGroupThreadFunc( void* arg )
    {
      rc_t rc = kOkRC;

      audioGroup_t* ag = reinterpret_cast<audioGroup_t*>(arg);

      // block on the cond. var
      if((rc = mutex::waitOnCondVar(ag->mutexH,false,ag->threadTimeOutMs)) != kOkRC )
      {
        if( rc != kTimeOutRC )
        {
          cwLogError(rc,"Audio thread Wait-on-condition-var failed.");
          return false;
        }
      }

      // if the cond. var was signaled and ag->mutexH is locked
      if( rc == kOkRC )
      {
        msg_t msg;
        msg.tid     = kAudioTId;
        msg.u.audio = &ag->msg;

        // While the all audio devices for this group are ready 
        while( _audioGroupBufIsReady( ag->p, ag, true ) && _audioGroupBufIsReady( ag->p, ag, false) )
        {
          _audioGroupProcSampleBufs( ag->p, ag, kAudioGroupGetBuf, true );
          _audioGroupProcSampleBufs( ag->p, ag, kAudioGroupGetBuf, false );

          if((rc = _ioCallback( ag->p, ag->asyncFl, &msg)) != kOkRC )
            cwLogError(rc,"Audio app callback failed %i.",ag->asyncFl);

          _audioGroupProcSampleBufs( ag->p, ag, kAudioGroupAdvBuf, true );
          _audioGroupProcSampleBufs( ag->p, ag, kAudioGroupAdvBuf, false );
        }
      }
     
      return true;
    }
    
    // Given a device index return the associated audioDev_t record.
    audioDev_t* _audioDeviceIndexToRecd( io_t* p, unsigned devIdx, bool reportMissingFl=true )
    {
      for(unsigned i=0; i<p->audioDevN; ++i)
        if( p->audioDevA[i].devIdx == devIdx )
          return p->audioDevA + i;

      if( reportMissingFl)
        cwLogError(kInvalidArgRC,"A device with index %i could not be found.",devIdx);
      
      return nullptr;
    }

    // Given a device name return the associated audioDev_t record.
    audioDev_t* _audioDeviceNameToRecd( io_t* p, const char* devName, bool reportMissingFl=true)
    {
      for(unsigned i=0; i<p->audioDevN; ++i)
        if( textCompare(p->audioDevA[i].devName, devName ) == 0 )
          return p->audioDevA + i;

      if( reportMissingFl )
        cwLogError(kInvalidArgRC,"A device named '%s' was not found.", cwStringNullGuard(devName) );
      
      return nullptr;
    }

    // Given a user label return the associated audioDev_t record.
    audioDev_t* _audioDeviceLabelToRecd( io_t* p, const char* label, bool reportMissingFl=true )
    {
      for(unsigned i=0; i<p->audioDevN; ++i)
        if( textCompare(label,p->audioDevA[i].label) == 0 )
          return p->audioDevA + i;

      if( reportMissingFl )
        cwLogError(kInvalidArgRC,"An audio device with label '%s' could not be found.",cwStringNullGuard(label));

      return nullptr;
    }
    
    // Add an audioGroup pointer to groupA[] and return the new count of elements in the array.
    unsigned _audioDeviceUpdateGroupArray( audioGroup_t** groupA, unsigned groupN, unsigned curGroupN, audioGroup_t* ag )
    {
      if( ag != nullptr )
      {
        for(unsigned i=0; i<curGroupN; ++i)
          if( groupA[i] == ag )
            return curGroupN;

        if( curGroupN >= groupN )
        {
          cwLogError(kAssertFailRC,"The group array was found to be too small during an audio device callback.");
          goto errLabel;
        }
        
        groupA[curGroupN++] = ag;
      }

    errLabel:
      return curGroupN;
    }

    // If audioDev (devIdx) is ready then update audio_group_dev.readyCnt and store a pointer to it's associated group in groupA[].
    // Return the count of pointers stored in groupA[].
    unsigned _audioDeviceUpdateReadiness( io_t* p, unsigned devIdx, bool inputFl, audioGroup_t** groupA, unsigned groupN, unsigned curGroupN )
    {
      audioDev_t* ad;
      
      // get the device record assoc'ed with this device
      if((ad = _audioDeviceIndexToRecd(p, devIdx )) == nullptr )
      {
        cwLogError(kAssertFailRC,"An unexpected audio device index was encountered in an audio device callback.");
        goto errLabel;
      }

      // if an input packet was received on this device
      if( inputFl )
      {
        if( audio::buf::isDeviceReady( p->audioBufH, devIdx, audio::buf::kInFl) )
        {
          // atomic incr  - note that the ordering doesn't matter because the update does not control access to any other variables from another thread
          std::atomic_store_explicit(&ad->iagd->readyCnt, ad->iagd->readyCnt+1,  std::memory_order_relaxed); 
          curGroupN = _audioDeviceUpdateGroupArray( groupA, groupN, curGroupN, ad->iGroup ); 
          ad->iagd->cbCnt += 1; // update the callback count for this device
        }
      }
      else // if an output packet was received on this device
      {    
        if( audio::buf::isDeviceReady( p->audioBufH, devIdx, audio::buf::kOutFl ) )
        {
          std::atomic_store_explicit(&ad->oagd->readyCnt, ad->oagd->readyCnt+1, std::memory_order_relaxed); // atomic incr  
          curGroupN = _audioDeviceUpdateGroupArray( groupA, groupN, curGroupN, ad->oGroup );
          ad->oagd->cbCnt += 1;
        }          
      }

    errLabel:
      // return the count of dev indexes in groupA[]
      return curGroupN;
    }


    // Return true if all the devices in the linked list 'agd' are ready to source/sink data.
    bool  _audioGroupIsReady( audio_group_dev_t* agd )
    {
      // are all devices in this group ready to  provide/accept new audio data
      for(; agd!=nullptr; agd=agd->link)
        if( std::atomic_load_explicit(&agd->readyCnt, std::memory_order_acquire) == 0  ) // ACQUIRE
          return false;

      return true;
    }

    // Decrement the ready count on all devices in the linked list pointed to by 'agd'.
    void _audioGroupDecrReadyCount( audio_group_dev_t* agd )
    {
      for(; agd !=nullptr; agd=agd->link)
        std::atomic_store_explicit(&agd->readyCnt,agd->readyCnt--, std::memory_order_release); // REALEASE
    }

    // This function is called by the audio device driver callback _audioDeviceCallback().
    // If all devices in any of the groups contained in groupA[] are ready to source/sink
    // audio data then this function triggers the condition var on the associated
    // group thread to trigger audio processing on those devices.  See _audioGroupThreadFunc().
    void _audioGroupNotifyIfReady( io_t* p, audioGroup_t** groupA, unsigned groupN )
    {      
      // for each device whose audio buffer state changed
      for(unsigned i=0; i<groupN; ++i)
      {
        audioGroup_t* ag = groupA[i];

        if( _audioGroupIsReady( ag->msg.iDevL ) && _audioGroupIsReady( ag->msg.oDevL ) )
        {
          // we now know the group is ready and so the ready count maybe decremented on each  device 
          _audioGroupDecrReadyCount( ag->msg.iDevL);
          _audioGroupDecrReadyCount( ag->msg.oDevL);

          // notify the audio group thread that all devices are ready by signaling the condition var that it is blocked on
          mutex::signalCondVar(ag->mutexH);

        }
      }
        
    }

    // This function is called by the audio device drivers when incoming audio arrives
    // or when there is available space to write outgoing audio.
    // If all in/out devices in a group are ready to be source/sink audio data then this function
    // triggers the group thread condition var thereby causing an application callback
    // to process the audio data.
    void _audioDeviceCallback( void* cbArg, audio::device::audioPacket_t* inPktArray, unsigned inPktCnt, audio::device::audioPacket_t* outPktArray, unsigned outPktCnt )
    {
      io_t* p = reinterpret_cast<io_t*>(cbArg);

      unsigned      groupN = 2 * inPktCnt + outPktCnt;
      audioGroup_t* groupA[ groupN ];
      unsigned curGroupN = 0;
      

      // update the audio buffer
      audio::buf::update( p->audioBufH, inPktArray, inPktCnt, outPktArray, outPktCnt );

      // update the readiness of the input devices
      for(unsigned i=0; i<inPktCnt; ++i)
        curGroupN = _audioDeviceUpdateReadiness( p, inPktArray[i].devIdx, true, groupA, groupN, curGroupN );

      // update the readiness of the output devices
      for(unsigned i=0; i<outPktCnt; ++i)
        curGroupN = _audioDeviceUpdateReadiness( p, outPktArray[i].devIdx, false, groupA, groupN, curGroupN );

      // groupA[] contains the set of groups which may have been made ready during this callback
      _audioGroupNotifyIfReady( p, groupA, curGroupN );

    }



    // Allocate a group-device record and link it to the associated group record.
    rc_t _audioGroupAddDevice( audioGroup_t* ag, bool inputFl, audioDev_t* ad, unsigned chCnt )
    {
      rc_t               rc      = kOkRC;
      audio_group_dev_t* new_agd = mem::allocZ<audio_group_dev_t>();
      
      new_agd->label  = ad->label;
      new_agd->userId = ad->userId;
      new_agd->devName= ad->devName;
      new_agd->devIdx = ad->devIdx;
      new_agd->flags  = inputFl ? kInFl : kOutFl;
      new_agd->chCnt  = chCnt;
      new_agd->chIdx  = inputFl ? ag->msg.iBufChCnt : ag->msg.oBufChCnt;
      new_agd->meterA = mem::allocZ<sample_t>(chCnt);

      audio_group_dev_t*& agd = inputFl ? ag->msg.iDevL : ag->msg.oDevL;
      
      if( agd == nullptr )
        agd = new_agd;
      else
      {      
        for(; agd!=nullptr; agd=agd->link)
          if( agd->link == nullptr )
          {            
            agd->link = new_agd;            
            break;              
          }   
      }

      // update the audio group channel count and set the pointers from
      // the private audioDevice_t to the public audio_group_dev_t.
      if( inputFl )
      {
        ag->msg.iBufChCnt += chCnt;
        ad->iagd = new_agd;
      }
      else
      {
        ag->msg.oBufChCnt += chCnt;
        ad->oagd = new_agd;
      }
      
      return rc;
    }

    // Given an audio group id return the associated audio group.
    audioGroup_t* _audioGroupFromId( io_t* p, unsigned userId, bool reportMissingFl=true )
    {
      for(unsigned i=0; i<p->audioGroupN; ++i)
        if( p->audioGroupA[i].msg.userId == userId )
          return p->audioGroupA + i;

      if( reportMissingFl )
        cwLogError(kInvalidArgRC,"An audio group with user id %i could not be found.", userId );
      
      return nullptr;
    }

    audioGroup_t* _audioGroupFromIndex( io_t* p, unsigned groupIdx, bool reportMissingFl=true )
    {
      if( groupIdx < p->audioGroupN )
        return p->audioGroupA + groupIdx;

      if( reportMissingFl )
        cwLogError(kInvalidArgRC,"'%i' is not a valid audio group index.",groupIdx);
      
      return nullptr;
    }

    // Given an audio group id return the associated audio group.
    audioGroup_t* _audioGroupFromLabel( io_t* p, const char* label, bool reportMissingFl=true )
    {
      for(unsigned i=0; i<p->audioGroupN; ++i)
        if( textCompare(p->audioGroupA[i].msg.label, label) == 0 )
          return p->audioGroupA + i;

      if( reportMissingFl )
        cwLogError(kInvalidArgRC,"An audio group with labe '%s' could not be found.", label );
      
      return nullptr;
    }
    
    
    // Create the audio group records by parsing the cfg. audio.groupL[] list.
    rc_t _audioDeviceParseAudioGroupList( io_t* p, const object_t* c )
    {
      rc_t            rc     = kOkRC;
      const object_t* groupL = nullptr;

      if((groupL = c->find("groupL")) == nullptr )
        return cwLogError(kSyntaxErrorRC,"Audio Group list 'groupL' not found.");

      p->audioGroupN = groupL->child_count();
      p->audioGroupA = mem::allocZ<audioGroup_t>(p->audioGroupN);

      for(unsigned i=0; i<p->audioGroupN; ++i)
      {
        const object_t* node;
        
        if((node = groupL->child_ele(i)) != nullptr )
        {
          if(( rc = node->getv(
                "enableFl",    p->audioGroupA[i].enableFl,
                "asyncFl",     p->audioGroupA[i].asyncFl,
                "label",       p->audioGroupA[i].msg.label,
                "id",          p->audioGroupA[i].msg.userId,
                "srate",       p->audioGroupA[i].msg.srate,
                "dspFrameCnt", p->audioGroupA[i].msg.dspFrameCnt )) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing audio group cfg record at index:%i",i);
            goto errLabel;
          }          

          // create the audio group thread mutex/cond var
          if((rc = mutex::create(p->audioGroupA[i].mutexH)) != kOkRC )
          {
            rc = cwLogError(rc,"Error creating audio group mutex.");
            goto errLabel;
          }

          // Lock the mutex so that it is already locked when it is used to block the audio thread
          // This avoids having to use logic in the thread callback to lock it on the first entry
          // while not locking it on all following entries.
          if((rc = mutex::lock(p->audioGroupA[i].mutexH)) != kOkRC )
          {
            rc = cwLogError(rc,"Error locking audio group mutex.");
            goto errLabel;
          }

          // create the audio group thread
          if((rc = thread_mach::add(p->threadMachH,_audioGroupThreadFunc,p->audioGroupA+i)) != kOkRC )
          {
            rc = cwLogError(rc,"Error creating audio group thread.");
            goto errLabel;
          }

          p->audioGroupA[i].p                = p;
          p->audioGroupA[i].threadTimeOutMs  = p->audioThreadTimeOutMs;
          p->audioGroupA[i].msg.groupIndex   = i;
          
        }
      }

    errLabel:
      return rc;
    }
      
    // Create the audio device records by parsing the cfg audio.deviceL[] list.
    rc_t _audioDeviceParseAudioDeviceList( io_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;
      const object_t* deviceL_Node;
      
      // get the audio device list
      if((deviceL_Node = cfg->find("deviceL")) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"Audio 'deviceL' failed.");
        goto errLabel;
      }

      // create an audio device cfg list
      p->audioDevN = audio::device::count(p->audioH);
      p->audioDevA = mem::allocZ<audioDev_t>(p->audioDevN);

      // Initial audioDev record setup
      for(unsigned i=0; i<p->audioDevN; ++i)
      {
        p->audioDevA[i].devName = mem::duplStr(audio::device::label(p->audioH,i));
        p->audioDevA[i].devIdx  = i;
        p->audioDevA[i].userId  = kInvalidId;
      }

      // fill in the audio device cfg list
      for(unsigned i=0; i<deviceL_Node->child_count(); ++i)
      {
        audioDev_t*   ad             = nullptr; //p->audioDevA + i;
        bool          enableFl       = false;
        char*         userLabel      = nullptr;
        unsigned      userId         = kInvalidId;
        char*         devName        = nullptr;
        unsigned      framesPerCycle = 0;
        unsigned      cycleCnt       = 0;
        
        audioGroup_t* iag            = nullptr;
        audioGroup_t* oag            = nullptr;
        
        double        israte         = 0;
        double        osrate         = 0;
        double        srate          = 0;
        
        unsigned      iDspFrameCnt   = 0;
        unsigned      oDspFrameCnt   = 0;
        unsigned      dspFrameCnt    = 0;
        
        char*         inGroupLabel   = nullptr;
        char*         outGroupLabel  = nullptr;
        
        
        const object_t* node;
        
        if((node = deviceL_Node->child_ele(i)) != nullptr )
        {
          if(( rc = node->getv(
                "enableFl",       enableFl,
                "label",          userLabel,
                "device",         devName,
                "framesPerCycle", framesPerCycle,
                "cycleCnt",       cycleCnt )) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing required audio cfg record at index:%i",i);
            goto errLabel;
          }


          if((rc = node->getv_opt(
                "userId",     userId,
                "inGroup",    inGroupLabel,
                "outGroup",   outGroupLabel )) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing optional audio cfg record at index:%i",i);
            goto errLabel;
          }
          
        }
        
        // if the configuration is enabled
        if( enableFl )
        {
          // locate the record assoc'd with devName
          if((ad = _audioDeviceNameToRecd(p,devName)) == nullptr )
          {
            rc = kInvalidArgRC;
            goto errLabel;
          }
          
          // get the hardware device index
          if((ad->devIdx = audio::device::labelToIndex( p->audioH, ad->devName)) == kInvalidIdx )
          {
            rc = cwLogError(rc,"Unable to locate the audio hardware device:'%s'.", cwStringNullGuard(ad->devName));
            goto errLabel;
          }

          // get the device channel counts
          unsigned iChCnt  = audio::device::channelCount(p->audioH,ad->devIdx,true);
          unsigned oChCnt  = audio::device::channelCount(p->audioH,ad->devIdx,false);
          

          // get the ingroup
          if( inGroupLabel != nullptr )
            if((iag = _audioGroupFromLabel(p, inGroupLabel )) != nullptr )
            {            
              israte       = iag->msg.srate;
              iDspFrameCnt = iag->msg.dspFrameCnt;
            }

          // get the outgroup
          if( outGroupLabel != nullptr )
            if((oag = _audioGroupFromLabel(p, outGroupLabel)) != nullptr )
            {
              osrate       = oag->msg.srate;
              oDspFrameCnt = oag->msg.dspFrameCnt;
            }
          
          // in-srate an out-srate must be equal or one must be 0
          if( osrate==0 || israte==0 || osrate==israte )
          {
            // the true sample rate is the non-zero sample rate
            srate = std::max(israte,osrate);
          }
          else
          {
            rc = cwLogError(kInvalidArgRC,"The device '%s' belongs to two groups (%s and %s) at different sample rates (%f != %f).", cwStringNullGuard(ad->devName), cwStringNullGuard(inGroupLabel), cwStringNullGuard(outGroupLabel), israte, osrate );
            goto errLabel;
          }

          // in-dspFrameCnt an out-dspFrameCnt must be equal or one must be 0
          if( oDspFrameCnt==0 || iDspFrameCnt==0 || oDspFrameCnt==iDspFrameCnt)
          {
            // the true sample rate is the non-zero sample rate
            dspFrameCnt = std::max(iDspFrameCnt,oDspFrameCnt);
          }
          else
          {
            rc = cwLogError(kInvalidArgRC,"The device '%s' belongs to two groups (%s and %s) width different dspFrameCnt values (%i != %i).", cwStringNullGuard(ad->devName), cwStringNullGuard(inGroupLabel), cwStringNullGuard(outGroupLabel), iDspFrameCnt, oDspFrameCnt );
            goto errLabel;
          }
          
          // setup the device based on the configuration
          if((rc = audio::device::setup(p->audioH, ad->devIdx, srate, framesPerCycle, _audioDeviceCallback, p)) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to setup the audio hardware device:'%s'.", ad->devName);
            goto errLabel;
          }
          
          // initialize the audio bufer for this device
          if((rc = audio::buf::setup( p->audioBufH, ad->devIdx, srate, dspFrameCnt, cycleCnt, iChCnt, framesPerCycle, oChCnt, framesPerCycle )) != kOkRC )
          {
            rc = cwLogError(rc,"Audio device buffer channel setup failed.");
            goto errLabel;
          }

          // if an input group was assigned to this device then create a assoc'd audio_group_dev_t
          if( iag != nullptr )
          {            
            if((rc = _audioGroupAddDevice( iag, true, ad, iChCnt )) != kOkRC )
              goto errLabel;
          }
          
          // if an output group was assigned to this device then create a assoc'd audio_group_dev_t
          if( oag != nullptr )
          {
            if((rc = _audioGroupAddDevice( oag, false, ad, oChCnt )) != kOkRC )
              goto errLabel;            
          }
          
          // set the device group pointers
          ad->enableFl = enableFl;
          ad->label    = userLabel;
          ad->userId   = userId;
          ad->iGroup   = iag;
          ad->oGroup   = oag;
            
        }
      }

    errLabel:
      return rc;
    }

    // Allocate the sample ptr buffers for each audio group.
    rc_t _audioGroupAllocBuffer( io_t* p )
    {
      rc_t rc = kOkRC;
      
      for(unsigned i=0; i<p->audioGroupN; ++i)        
      {
        audioGroup_t* ag = p->audioGroupA + i;

        if( ag->msg.iBufChCnt )
          ag->msg.iBufArray   = mem::allocZ<sample_t*>( ag->msg.iBufChCnt );
        
        if( ag->msg.oBufChCnt )
          ag->msg.oBufArray   = mem::allocZ<sample_t*>( ag->msg.oBufChCnt );
      }
      
      return rc;
    }

    rc_t _audioParseConfig( io_t* p, const object_t* cfg )
    {
      rc_t            rc          = kOkRC;
      const object_t* node        = nullptr;
      
      // get the audio port node
      if((node = cfg->find("audio")) == nullptr )
      {
        cwLogWarning("No 'audio' configuration node.");
        return kOkRC;
      }
      
      // get the meterMs value
      if((rc = node->getv("meterMs", p->audioMeterCbPeriodMs, "threadTimeOutMs", p->audioThreadTimeOutMs )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Audio 'meterMs' or 'dspFrameCnt' parse failed.");
        goto errLabel;
      }

      // initialize the audio buffer
      if((rc = audio::buf::create( p->audioBufH, audio::device::count(p->audioH), p->audioMeterCbPeriodMs )) != kOkRC )
      {
        rc = cwLogError(rc,"Audio device buffer failed.");
        goto errLabel;
      }

      // parse the audio group list
      if((rc = _audioDeviceParseAudioGroupList( p, node )) != kOkRC )
      {
        rc = cwLogError(rc,"Parse audio group list.");
        goto errLabel;
      }
      
      // parse the audio device list
      if((rc = _audioDeviceParseAudioDeviceList( p, node )) != kOkRC )
      {
        rc = cwLogError(rc,"Parse audio device list.");
        goto errLabel;
      }

      // create the audio buffer pointer arrays
      if((rc = _audioGroupAllocBuffer(p)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio group buffer allocation failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }


    rc_t _audioCreate( io_t* p, const object_t* c )
    {      
      rc_t                     rc       = kOkRC;
      audio::device::driver_t* audioDrv = nullptr;

      // initialize the audio device interface  
      if((rc = audio::device::create(p->audioH)) != kOkRC )
      {
        rc = cwLogError(rc,"Initialize failed.");
        goto errLabel;
      }

      // initialize the ALSA device driver interface
      if((rc = audio::device::alsa::create(p->alsaH, audioDrv )) != kOkRC )
      {
        rc = cwLogError(rc,"ALSA initialize failed.");
        goto errLabel;
      }

      // register the ALSA device driver with the audio interface
      if((rc = audio::device::registerDriver( p->audioH, audioDrv )) != kOkRC )
      {
        rc = cwLogError(rc,"ALSA driver registration failed.");
        goto errLabel;
      }

      // read the configuration information and setup the audio hardware
      if((rc = _audioParseConfig( p, c )) != kOkRC )
      {
        rc = cwLogError(rc,"Audio device configuration failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    //----------------------------------------------------------------------------------------------------------
    //
    // UI
    //

    // This function is called by the websocket with messages coming from a remote UI.
    rc_t _uiCallback( void* cbArg, unsigned wsSessId, ui::opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, unsigned chanId, const ui::value_t* v )
    {
      io_t* p = (io_t*)cbArg;
      msg_t r;
      rc_t rc = kOkRC;
      rc_t app_rc = kOkRC;
      
      r.tid  = kUiTId;
      r.u.ui = { .opId=opId, .wsSessId=wsSessId, .parentAppId=parentAppId, .uuId=uuId, .appId=appId, .chanId=chanId, .value=v };

      if((rc = _ioCallback( p, p->uiAsyncFl, &r, &app_rc )) != kOkRC )
        cwLogError(rc,"UI app callback failed.");

      return app_rc;
    }

    rc_t _uiConfig( io_t* p, const object_t* c, const ui::appIdMap_t* mapA, unsigned mapN )
    {
      rc_t            rc         = kOkRC;
      const char*     uiCfgLabel = "ui";
      ui::ws::args_t  args       = {};
      const object_t* ui_cfg     = nullptr;

      // Duplicate the application id map
      if( mapN > 0 )
      {
        p->uiMapA   = mem::allocZ<ui::appIdMap_t>( mapN );
        p->uiMapN   = mapN;
        for(unsigned i=0; i<mapN; ++i)
        {
          p->uiMapA[i] = mapA[i];
          p->uiMapA[i].eleName = mem::duplStr(mapA[i].eleName);
        }
      }

     
      // if a UI cfg record was given
      if((ui_cfg = c->find(uiCfgLabel)) != nullptr )
      {

        if((rc = ui_cfg->getv("asyncFl",p->uiAsyncFl)) != kOkRC )
        {
          rc = cwLogError(rc,"UI configuration parse failed.");
          goto errLabel;
        }

        // parse the ui 
        if((rc = ui::ws::parseArgs( *c, args, uiCfgLabel )) == kOkRC )
        {  
          rc = ui::ws::create(p->wsUiH, args, p, _uiCallback, args.uiRsrc, p->uiMapA, p->uiMapN);
          
          ui::ws::releaseArgs(args);
        
        }
      }

    errLabel:
      return rc;

    }

    rc_t _handleToWsUiHandle( handle_t h, ui::ws::handle_t& uiH_Ref )
    {
      rc_t rc = kOkRC;
      io_t* p = _handleToPtr(h);
      if( p != nullptr && p->wsUiH.isValid())
        uiH_Ref = p->wsUiH;
      else
        rc = cwLogError(kInvalidStateRC,"Invalid ui::ws handle in io request.");

      return rc;
    }

    rc_t _handleToUiHandle( handle_t h, ui::handle_t& uiHRef )
    {
      rc_t rc;
      ui::ws::handle_t wsUiH;
      uiHRef.clear();
      if((rc = _handleToWsUiHandle(h, wsUiH)) != kOkRC )
        return rc;

      uiHRef = ui::ws::uiHandle(wsUiH);

      return uiHRef.isValid() ? rc : cwLogError(rc,"Invalid ui handle in io request.");
    }
    

    //----------------------------------------------------------------------------------------------------------
    //
    // IO
    //
    
    rc_t _destroy( io_t* p )
    {
      rc_t rc = kOkRC;

      // stop thread callbacks
      if((rc = thread_mach::destroy(p->threadMachH)) != kOkRC )
        return rc;

      for(unsigned i=0; i<p->timerN; ++i)
        mem::release(p->timerA[i].label);
      
      mem::release(p->timerA);
      p->timerN = 0;

      _serialPortDestroy(p);

      _audioDestroy(p);

      midi::device::destroy(p->midiH);

      sock::destroyMgr( p->sockH );

      mem::release(p->sockA);
      
      for(unsigned i=0; i<p->uiMapN; ++i)
        mem::free(const_cast<char*>(p->uiMapA[i].eleName));
      mem::release(p->uiMapA);
      p->uiMapN = 0;

      ui::ws::destroy(p->wsUiH);


      // free the cfg object 
      if( p->cfg != nullptr )
        p->cfg->free();

      
      
      mem::release(p);

      return rc;
    }

    rc_t _audioDeviceParams( handle_t h, unsigned devIdx, unsigned flags, io_t*& pRef, audioDev_t*& adRef, unsigned& audioBufFlagsRef )
    {
      rc_t rc = kOkRC;
  
      pRef  = _handleToPtr(h);
  
      if((adRef = _audioDeviceIndexToRecd(pRef,devIdx)) == nullptr )
        rc = kInvalidArgRC;
      else
      {
        audioBufFlagsRef = 0;
        if( cwIsFlag(flags,kInFl) )
          audioBufFlagsRef += audio::buf::kInFl;
    
        if( cwIsFlag(flags,kOutFl) )
          audioBufFlagsRef += audio::buf::kOutFl;

        if( cwIsFlag(flags,kEnableFl) )
          audioBufFlagsRef += audio::buf::kEnableFl;
    
      }

      return rc;
    }

    
    
  }
}


//----------------------------------------------------------------------------------------------------------
//
// IO
//

cw::rc_t cw::io::create(
  handle_t&             h,
  const object_t*       o,
  cbFunc_t              cbFunc,
  void*                 cbArg,
  const ui::appIdMap_t* mapA,
  unsigned              mapN,
  const char*           cfgLabel )
{
  rc_t            rc;
  
  if((rc = destroy(h)) != kOkRC )
    return rc;

  // create the io_t object
  io_t* p = mem::allocZ<io_t>();

  // duplicate the cfg object so that we can maintain pointers into its elements without
  // any chance that they will be delted before the application completes
  p->cfg    = o->duplicate();
  p->cbFunc = cbFunc;
  p->cbArg  = cbArg;

  // parse the 'io' configuration block
  if((rc = _ioParse(p,o)) != kOkRC )
    goto errLabel;

  // create the callback mutex
  if((rc = mutex::create( p->cbMutexH )) != kOkRC )
    goto errLabel;

  // create the the thread machine
  if((rc = thread_mach::create( p->threadMachH )) != kOkRC )
    goto errLabel;
  
  // create the serial port device
  if((rc = _serialPortCreate(p,p->cfg)) != kOkRC )
    goto errLabel;

  // create the MIDI port device
  if((rc = _midiPortCreate(p,p->cfg)) != kOkRC )
    goto errLabel;

  // create the Audio device interface
  if((rc = _audioCreate(p,p->cfg)) != kOkRC )
    goto errLabel;

  // create the Socket manager
  if((rc= _socketParseConfig(p, p->cfg )) != kOkRC )
    goto errLabel;
  
  // create the UI interface
  if((rc = _uiConfig(p,p->cfg, mapA, mapN)) != kOkRC )
    goto errLabel;
  

  p->quitFl.store(false);
  time::get(p->t0);  
  
  h.set(p);
  
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

  _audioDeviceStartStop(p,true);

  _serialPortStart(p);
  
  return thread_mach::start( p->threadMachH );
}

cw::rc_t cw::io::pause( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return thread_mach::stop( p->threadMachH );
}

cw::rc_t cw::io::stop( handle_t h )
{
  io_t* p = _handleToPtr(h);
  p->quitFl.store(true);

  // stop the audio devices
  _audioDeviceStartStop(p,false);

  // clear the UI
  if( p->wsUiH.isValid() )
    uiDestroyElement(h,ui::kRootUuId);
  
  return kOkRC;
}

cw::rc_t cw::io::exec( handle_t h, void* execCbArg )
{
  rc_t rc = kOkRC;
  io_t* p = _handleToPtr(h);
  
  if( p->wsUiH.isValid() )    
    rc = ui::ws::exec( p->wsUiH );

  time::get(p->t0);

  if( p->audioMeterDevEnabledN ) 
    _audioDeviceProcessMeters(p);

  msg_t m;
  m.tid = kExecTId;
  m.u.exec.execArg = execCbArg;
  _ioCallback(p,false,&m);
  
  return rc;
}

bool cw::io::isShuttingDown( handle_t h )
{
  io_t* p = _handleToPtr(h);

  return p->quitFl.load();

  
  //return thread_mach::is_shutdown(p->threadMachH);
}

void cw::io::report( handle_t h )
{
  for(unsigned i=0; i<serialDeviceCount(h); ++i)
    printf("serial: %s\n", serialDeviceLabel(h,i));

  for(unsigned i=0; i<midiDeviceCount(h); ++i)
    for(unsigned j=0; j<2; ++j)
    {
      bool     inputFl = j==0;
      unsigned m       = midiDevicePortCount(h,i,inputFl);
      for(unsigned k=0; k<m; ++k)
        printf("midi: %s: %s : %s\n", inputFl ? "in ":"out", midiDeviceName(h,i), midiDevicePortName(h,i,inputFl,k));
        
    }

  for(unsigned i=0; i<audioDeviceCount(h); ++i)
    printf("audio: %s\n", audioDeviceName(h,i));
  
}

//----------------------------------------------------------------------------------------------------------
//
// Thread
//

cw::rc_t cw::io::threadCreate( handle_t h, unsigned id, bool asyncFl, void* arg )
{
  rc_t      rc = kOkRC;
  io_t*     p  = _handleToPtr(h);
  thread_t* t  = mem::allocZ<thread_t>(1);
  
  t->id      = id;
  t->asyncFl = asyncFl;
  t->arg     = arg;
  t->p       = p;
  t->link    = p->threadL;
  p->threadL = t;

  if((rc = thread_mach::add( p->threadMachH, _threadFunc, t )) != kOkRC )
    rc = cwLogError(rc,"Thread create failed.");
 
  return rc;
}

//----------------------------------------------------------------------------------------------------------
//
// Timer
//

cw::rc_t    cw::io::timerCreate( handle_t h, const char* label, unsigned id, unsigned periodMicroSec, bool asyncFl )
{
  io_t* p = _handleToPtr(h);
  return  _timerCreate(p, label, id, periodMicroSec, asyncFl );

}

cw::rc_t    cw::io::timerDestroy( handle_t h, unsigned timerIdx )
{
  io_t*    p = _handleToPtr(h);
  timer_t* t = _timerIndexToPtr( p, timerIdx );

  if( t != nullptr )
  {
    t->startedFl = false;
    t->deletedFl = true;
  }
  
  return t==nullptr ? kInvalidIdRC : kOkRC;
}

unsigned    cw::io::timerCount( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return p->timerN;
}

unsigned    cw::io::timerLabelToIndex( handle_t h, const char* label )
{
  io_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->timerN; ++i)
    if( !p->timerA[i].deletedFl && strcmp(label,p->timerA[i].label) == 0 )
      return i;
  
  return kInvalidIdx;
}

unsigned    cw::io::timerIdToIndex(         handle_t h, unsigned timerId )
{
  io_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->timerN; ++i)
  {
    timer_t* t = p->timerA + i;
    if( !t->deletedFl && t->id == timerId )
      return i;    
  }

  return kInvalidIdx;
}


const char* cw::io::timerLabel(             handle_t h, unsigned timerIdx )
{
  io_t*    p = _handleToPtr(h);
  timer_t* t = _timerIndexToPtr( p, timerIdx );
  
  return t==nullptr ? nullptr : t->label;    
}

unsigned    cw::io::timerId(                handle_t h, unsigned timerIdx )
{
  io_t*    p = _handleToPtr(h);
  timer_t* t = _timerIndexToPtr( p, timerIdx );
  
  return t==nullptr ? kInvalidId : t->id;    
}

unsigned    cw::io::timerPeriodMicroSec(    handle_t h, unsigned timerIdx )
{
  io_t*    p = _handleToPtr(h);
  timer_t* t = _timerIndexToPtr( p, timerIdx );
  
  return t==nullptr ? 0 : t->periodMicroSec;    
}

cw::rc_t    cw::io::timerSetPeriodMicroSec( handle_t h, unsigned timerIdx, unsigned periodMicroSec )
{
  rc_t     rc = kOkRC;
  io_t*    p  = _handleToPtr(h);
  timer_t* t  = _timerIndexToPtr(p, timerIdx);

  if( t == nullptr )
    rc = kInvalidIdRC;
  else
    p->timerA[ timerIdx ].periodMicroSec = periodMicroSec;
    
  return rc;
}

cw::rc_t    cw::io::timerStart( handle_t h, unsigned timerIdx )
{
  io_t*    p  = _handleToPtr(h);
  return _timerStart( p, timerIdx, true );
}

cw::rc_t    cw::io::timerStop( handle_t h, unsigned timerIdx )
{
  io_t*    p  = _handleToPtr(h);
  return _timerStart( p, timerIdx, false );
}



//----------------------------------------------------------------------------------------------------------
//
// Serial
//

unsigned cw::io::serialDeviceCount( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return p->serialN;
}

unsigned cw::io::serialDeviceIndex( handle_t h, const char* label )
{
  io_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->serialN; ++i)
    if( textCompare(label,p->serialA[i].label) == 0 )
      return i;

  return kInvalidIdx;
}

const char* cw::io::serialDeviceLabel(  handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  return p->serialA[devIdx].label;
}


unsigned    cw::io::serialDeviceId(    handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  return p->serialA[devIdx].userId;
}

void        cw::io::serialDeviceSetId( handle_t h, unsigned devIdx, unsigned id )
{
  io_t* p = _handleToPtr(h);
  p->serialA[devIdx].userId = id;
}


cw::rc_t cw::io::serialDeviceSend(  handle_t h, unsigned devIdx, const void* byteA, unsigned byteN )
{
  rc_t  rc = kOkRC;
  //io_t* p  = _handleToPtr(h);
  return rc;
}


//----------------------------------------------------------------------------------------------------------
//
// MIDI
//

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
      
cw::rc_t cw::io::midiDeviceSend( handle_t h, unsigned devIdx, unsigned portIdx, uint8_t status, uint8_t d0, uint8_t d1 )
{
  io_t* p = _handleToPtr(h);
  return midi::device::send( p->midiH, devIdx, portIdx, status, d0, d1 );
}

//----------------------------------------------------------------------------------------------------------
//
// Audio
//

unsigned    cw::io::audioDeviceCount(          handle_t h )
{
  io_t* p = _handleToPtr(h);
  return p->audioDevN;
}

unsigned    cw::io::audioDeviceLabelToIndex(   handle_t h, const char* label )
{
  io_t*       p = _handleToPtr(h);
  audioDev_t* ad;
  
  if((ad = _audioDeviceLabelToRecd(p,label)) != nullptr )
    return ad->devIdx;

  return kInvalidIdx;
}

const char* cw::io::audioDeviceLabel( handle_t h, unsigned devIdx )
{
  io_t*       p = _handleToPtr(h);
  audioDev_t* ad;
  
  if((ad = _audioDeviceIndexToRecd(p,devIdx)) != nullptr )
    return ad->label;

  return nullptr;
}

cw::rc_t  cw::io::audioDeviceSetUserId( handle_t h, unsigned devIdx, unsigned userId )
{
  io_t*       p = _handleToPtr(h);
  audioDev_t* ad;
  
  if((ad = _audioDeviceIndexToRecd(p,devIdx)) != nullptr )
  {
    ad->userId = userId;
    
    if( ad->iagd != nullptr )
      ad->iagd->userId = userId;
    
    if( ad->oagd != nullptr )
      ad->oagd->userId = userId;
  }

  return kInvalidArgRC;  
}

bool  cw::io::audioDeviceIsEnabled( handle_t h, unsigned devIdx )
{
  io_t*       p = _handleToPtr(h);
  audioDev_t* ad;
  if((ad = _audioDeviceIndexToRecd(p,devIdx)) != nullptr )
    return ad->enableFl;
  
  return false;
}


const char* cw::io::audioDeviceName( handle_t h, unsigned devIdx )
{
  io_t*       p = _handleToPtr(h);
  audioDev_t* ad;
  
  if((ad = _audioDeviceIndexToRecd(p, devIdx )) == nullptr )
    return nullptr;

  return ad->devName;  
}

unsigned   cw::io::audioDeviceUserId( handle_t h, unsigned devIdx )
{
  io_t*       p = _handleToPtr(h);
  audioDev_t* ad;
  
  if((ad = _audioDeviceIndexToRecd(p, devIdx )) == nullptr )
    return kInvalidId;

  return ad->userId;    
}


double cw::io::audioDeviceSampleRate( handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  return audio::device::sampleRate(p->audioH, devIdx );
}

unsigned cw::io::audioDeviceFramesPerCycle( handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  audioDev_t* ad;
   
  if((ad = _audioDeviceIndexToRecd(p, devIdx )) == nullptr )
    return audio::device::framesPerCycle(p->audioH, devIdx, ad->iGroup != nullptr );

  return 0;
}

unsigned cw::io::audioDeviceChannelCount(   handle_t h, unsigned devIdx, unsigned inOrOutFlag )
{
  io_t* p = _handleToPtr(h);
  
  return audio::device::channelCount(p->audioH, devIdx, inOrOutFlag & kInFl );
}

cw::rc_t cw::io::audioDeviceEnableMeters( handle_t h, unsigned devIdx, unsigned inOutEnaFlags )
{
  rc_t        rc            = kOkRC;
  io_t*       p             = nullptr;
  audioDev_t* ad            = nullptr;
  unsigned    audioBufFlags = 0;
  
  if((rc = _audioDeviceParams( h, devIdx, inOutEnaFlags, p, ad, audioBufFlags )) != kOkRC )
    rc = cwLogError(rc,"Enable tone failed.");
  else    
  {
    bool     enaFl       = inOutEnaFlags & kEnableFl;
    bool     enaState0Fl = _audioDeviceIsMeterEnabled(ad, kInFl | kOutFl);
    
    audioBufFlags += audio::buf::kMeterFl;


    if( inOutEnaFlags & kInFl )
      ad->iagd->flags = cwEnaFlag(ad->iagd->flags,kMeterFl,enaFl);

    if( inOutEnaFlags & kOutFl )
      ad->oagd->flags = cwEnaFlag(ad->oagd->flags,kMeterFl,enaFl);
    
    audio::buf::setFlag( p->audioBufH, devIdx, kInvalidIdx, audioBufFlags );
    
    bool enaState1Fl= _audioDeviceIsMeterEnabled(ad, kInFl | kOutFl);

    if( enaState1Fl and !enaState0Fl )
      p->audioMeterDevEnabledN += 1;
    else
      if( p->audioMeterDevEnabledN > 0 && !enaState1Fl && enaState0Fl )
        p->audioMeterDevEnabledN -= 1;
   
  }

  if( rc != kOkRC )
     rc = cwLogError(rc,"Enable meters failed.");
   
  return rc;
}


const cw::io::sample_t* cw::io::audioDeviceMeters( handle_t h, unsigned devIdx, unsigned& chCntRef, unsigned inOrOutFlag )
{
  rc_t rc = kOkRC;
  io_t* p = _handleToPtr(h);
  sample_t* meterA = nullptr;
  
  audioDev_t* ad;
  if((ad = _audioDeviceIndexToRecd(p,devIdx)) == nullptr )
    rc = kInvalidArgRC;
  else
  {
    bool               inputFl = inOrOutFlag & kInFl;
    audio_group_dev_t* agd     = inputFl ? ad->iagd     : ad->oagd;
    
    if( !cwIsFlag(agd->flags,kMeterFl) )
      rc = cwLogError(kInvalidArgRC,"The %s meters on device %s are not enabled.", inputFl ? "input" : "output", cwStringNullGuard(ad->label));
    else
    {
      meterA   = agd->meterA;
      chCntRef = agd->chCnt;
    }
    
  }

  if( rc != kOkRC )
    rc = cwLogError(rc,"Get meters failed.");
  
  return meterA;
}


cw::rc_t cw::io::audioDeviceEnableTone( handle_t h, unsigned devIdx, unsigned inOutEnaFlags )
{
  rc_t        rc            = kOkRC;
  io_t*       p             = nullptr;
  audioDev_t* ad            = nullptr;
  unsigned    audioBufFlags = 0;
  
  if((rc = _audioDeviceParams( h, devIdx, inOutEnaFlags, p, ad, audioBufFlags )) != kOkRC )
    rc = cwLogError(rc,"Enable tone failed.");
  else    
  {
    audioBufFlags += audio::buf::kToneFl;
    audio::buf::setFlag( p->audioBufH, devIdx, kInvalidIdx, audioBufFlags );        
  }
  
  return rc;
}

cw::rc_t cw::io::audioDeviceToneFlags( handle_t h, unsigned devIdx, unsigned inOrOutFlag,  bool* toneFlA, unsigned chCnt )
{

  rc_t        rc            = kOkRC;
  io_t*       p             = nullptr;
  audioDev_t* ad            = nullptr;
  unsigned    audioBufFlags = 0;
  
  if((rc = _audioDeviceParams( h, devIdx, inOrOutFlag, p, ad, audioBufFlags )) != kOkRC )
    rc = cwLogError(rc,"Get tone flags failed.");
  else    
  {
    audioBufFlags += audio::buf::kToneFl;
    audio::buf::toneFlags( p->audioBufH, devIdx, audioBufFlags, toneFlA, chCnt );
  }
  
  return rc;
}

cw::rc_t cw::io::audioDeviceEnableMute( handle_t h, unsigned devIdx, unsigned inOutEnaFlags )
{
  rc_t        rc            = kOkRC;
  io_t*       p             = nullptr;
  audioDev_t* ad            = nullptr;
  unsigned    audioBufFlags = 0;
  
  if((rc = _audioDeviceParams( h, devIdx, inOutEnaFlags, p, ad, audioBufFlags )) != kOkRC )
    rc = cwLogError(rc,"Enable mute failed.");
  else    
  {
    audioBufFlags += audio::buf::kMuteFl;
    audio::buf::setFlag( p->audioBufH, devIdx, kInvalidIdx, audioBufFlags );        
  }
  
  return rc;
}

cw::rc_t cw::io::audioDeviceMuteFlags( handle_t h, unsigned devIdx, unsigned inOrOutFlag,  bool* muteFlA, unsigned chCnt )
{
  rc_t        rc            = kOkRC;
  io_t*       p             = nullptr;
  audioDev_t* ad            = nullptr;
  unsigned    audioBufFlags = 0;
  
  if((rc = _audioDeviceParams( h, devIdx, inOrOutFlag, p, ad, audioBufFlags )) != kOkRC )
    rc = cwLogError(rc,"Get mute flags failed.");
  else    
  {
    audioBufFlags += audio::buf::kMuteFl;
    audio::buf::muteFlags( p->audioBufH, devIdx, audioBufFlags, muteFlA, chCnt );
  }
  
  return rc;
}


cw::rc_t cw::io::audioDeviceSetGain( handle_t h, unsigned devIdx, unsigned inOrOutFlag, double gain )
{
  rc_t        rc            = kOkRC;
  io_t*       p             = nullptr;
  audioDev_t* ad            = nullptr;
  unsigned    audioBufFlags = 0;
  
  if((rc = _audioDeviceParams( h, devIdx, inOrOutFlag, p, ad, audioBufFlags )) != kOkRC )
    rc = cwLogError(rc,"Set gain failed.");
  else    
  {
    audio::buf::setGain( p->audioBufH, devIdx, kInvalidIdx, audioBufFlags, gain );
  }
  
  return rc;
}

cw::rc_t cw::io::audioDeviceGain( handle_t h, unsigned devIdx, unsigned inOrOutFlag, double* gainA, unsigned chCnt )
{
  rc_t        rc            = kOkRC;
  io_t*       p             = nullptr;
  audioDev_t* ad            = nullptr;
  unsigned    audioBufFlags = 0;
  
  if((rc = _audioDeviceParams( h, devIdx, inOrOutFlag, p, ad, audioBufFlags )) != kOkRC )
    rc = cwLogError(rc,"Get gain failed.");
  else    
  {
    audioBufFlags += audio::buf::kMuteFl;
    audio::buf::gain( p->audioBufH, devIdx, audioBufFlags, gainA, chCnt );
  }
  
  return rc;
}

unsigned cw::io::audioGroupCount( handle_t h )
{
  io_t* p = _handleToPtr(h);
  return p->audioGroupN;
}

unsigned cw::io::audioGroupLabelToIndex(  handle_t h, const char* label )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);

  if((ag = _audioGroupFromLabel(p,label)) == nullptr )
    return kInvalidIdx;

  return ag->msg.groupIndex; 
}
  
const char* cw::io::audioGroupLabel( handle_t h, unsigned groupIdx )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
    return ag->msg.label;
  return nullptr;
}

bool cw::io::audioGroupIsEnabled( handle_t h, unsigned groupIdx )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
    return ag->enableFl;
  return false;
}

unsigned cw::io::audioGroupUserId( handle_t h, unsigned groupIdx )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
    return ag->msg.userId;
  return kInvalidIdx;
}

cw::rc_t cw::io::audioGroupSetUserId( handle_t h, unsigned groupIdx, unsigned userId )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
    ag->msg.userId = userId;
  
  return kInvalidArgRC;
}

double cw::io::audioGroupSampleRate(    handle_t h, unsigned groupIdx )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
    return ag->msg.srate;
  return 0;
}

unsigned cw::io::audioGroupDspFrameCount( handle_t h, unsigned groupIdx )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
    return ag->msg.dspFrameCnt;
  return 0;
}

unsigned cw::io::audioGroupDeviceCount( handle_t h, unsigned groupIdx, unsigned inOrOutFl )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  unsigned      n = 0;
  
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
  {
    audio_group_dev_t* agd = cwIsFlag(inOrOutFl,kInFl) ? ag->msg.iDevL : ag->msg.oDevL;
    for(; agd!=nullptr; agd=agd->link)
      ++n;
  }
  
  return n;  
}

unsigned cw::io::audioGroupDeviceIndex( handle_t h, unsigned groupIdx, unsigned inOrOutFl, unsigned groupDevIdx )
{
  audioGroup_t* ag;
  io_t*         p = _handleToPtr(h);
  unsigned      n = 0;
  
  if((ag = _audioGroupFromIndex( p, groupIdx )) != nullptr )
  {
    audio_group_dev_t* agd = cwIsFlag(inOrOutFl,kInFl) ? ag->msg.iDevL : ag->msg.oDevL;
    
    for(; agd!=nullptr; agd=agd->link)
    {
      if( n == groupDevIdx )
        return agd->devIdx;
      ++n;
    }
  }

  cwLogError(kInvalidIdRC,"The audio group device index '%i' could found on group index: '%i' .",groupDevIdx,groupIdx);

              
  return kInvalidIdx;    
}


//----------------------------------------------------------------------------------------------------------
//
// Socket
//


unsigned cw::io::socketCount(        handle_t h )
{
  io_t* p = _handleToPtr(h);
  return p->sockN;
}

unsigned cw::io::socketLabelToIndex( handle_t h, const char* label )
{
  io_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->sockN; ++i)
    if( textCompare(p->sockA[i].label,label) == 0 )
      return i;

  cwLogError(kInvalidArgRC,"'%s' is not a valid socket label.", cwStringNullGuard(label));
  return kInvalidIdx;
}

unsigned cw::io::socketUserId( handle_t h, unsigned sockIdx )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return kInvalidId;

  return s->userId;
}

cw::rc_t cw::io::socketSetUserId( handle_t h, unsigned sockIdx, unsigned userId )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return kInvalidArgRC;

  s->userId = userId;
  return kOkRC;  
}

const char*  cw::io::socketLabel( handle_t h, unsigned sockIdx )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return nullptr;

  return s->label;
}

const char*  cw::io::socketHostName(     handle_t h, unsigned sockIdx )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return nullptr;

  return sock::hostName( p->sockH, s->userId );
}

const char*  cw::io::socketIpAddress(    handle_t h, unsigned sockIdx )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return nullptr;

  return sock::ipAddress( p->sockH, s->userId );
}

unsigned  cw::io::socketInetAddress(  handle_t h, unsigned sockIdx )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return 0;

  return sock::inetAddress( p->sockH, s->userId );
}

cw::sock::portNumber_t cw::io::socketPort( handle_t h, unsigned sockIdx )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return sock::kInvalidPortNumber;

  return sock::port( p->sockH, s->userId );  
}

cw::rc_t cw::io::socketPeername( handle_t h, unsigned sockIdx, struct sockaddr_in* addr )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return kInvalidArgRC;

  return sock::peername( p->sockH, s->userId, addr );
}

bool     cw::io::socketIsConnected(  handle_t h, unsigned sockIdx )
{ 
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return sock::isConnected( p->sockH, s->userId );

  return false;
}

cw::rc_t cw::io::socketSend(    handle_t h, unsigned sockIdx, unsigned connId, const void* data, unsigned dataByteCnt )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return kInvalidArgRC;
  return sock::send(p->sockH, s->userId, connId, data, dataByteCnt );
}

cw::rc_t cw::io::socketSend(    handle_t h, unsigned sockIdx, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return kInvalidArgRC;
  return sock::send(p->sockH, s->userId, data, dataByteCnt, remoteAddr );
}

cw::rc_t cw::io::socketSend(    handle_t h, unsigned sockIdx, const void* data, unsigned dataByteCnt, const char* remoteAddr, sock::portNumber_t remotePort )
{
  io_t* p = _handleToPtr(h);
  socket_t* s;
  if((s = _socketIndexToRecd(p,sockIdx)) == nullptr )
    return kInvalidArgRC;
  return sock::send(p->sockH, s->userId, data, dataByteCnt, remoteAddr, remotePort );
}

//----------------------------------------------------------------------------------------------------------
//
// WebSocket
//



//----------------------------------------------------------------------------------------------------------
//
// UI
//
unsigned    cw::io::parentAndNameToAppId( handle_t h, unsigned parentAppId, const char* eleName )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::parentAndNameToAppId(uiH,parentAppId,eleName);
  return kInvalidId;
}

unsigned    cw::io::parentAndNameToUuId(  handle_t h, unsigned parentAppId, const char* eleName )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::parentAndNameToUuId(uiH,parentAppId,eleName);
  return kInvalidId;
}

unsigned    cw::io::parentAndAppIdToUuId( handle_t h, unsigned parentAppId, unsigned appId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::parentAndAppIdToUuId(uiH,parentAppId,appId);
  return kInvalidId;
}

unsigned    cw::io::uiFindElementAppId(  handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementAppId(uiH, uuId );
  return kInvalidId;  
}

unsigned    cw::io::uiFindElementUuId( handle_t h, const char* eleName, unsigned chanId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementUuId(uiH, eleName, chanId );
  return kInvalidId;  
}

unsigned    cw::io::uiFindElementUuId( handle_t h, unsigned appId, unsigned chanId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementUuId(uiH, kInvalidId, appId, chanId );
  return kInvalidId;  
}

unsigned    cw::io::uiFindElementUuId( handle_t h, unsigned parentUuId, const char* eleName, unsigned chanId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementUuId(uiH, parentUuId, eleName, chanId );
  return kInvalidId;  
}

unsigned    cw::io::uiFindElementUuId( handle_t h, unsigned parentUuId, unsigned appId, unsigned chanId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementUuId(uiH, parentUuId, appId, chanId );
  return kInvalidId;  
}


cw::rc_t cw::io::uiCreateFromObject( handle_t h, const object_t* o, unsigned parentUuId, unsigned chanId, const char* eleName)
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::createFromObject(uiH,o,parentUuId,chanId,eleName);
  return rc;
}

cw::rc_t cw::io::uiCreateFromFile(   handle_t h, const char* fn,    unsigned parentUuId, unsigned chanId)
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::createFromFile(uiH,fn,parentUuId,chanId);
  return rc;
}

cw::rc_t cw::io::uiCreateFromText(   handle_t h, const char* text,  unsigned parentUuId, unsigned chanId)
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createFromText(uiH,text,parentUuId, chanId);
  return rc;
}

cw::rc_t cw::io::uiCreateFromRsrc(   handle_t h, const char* label,  unsigned parentUuId, unsigned chanId)
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createFromRsrc(uiH,label,parentUuId, chanId);
  return rc;
}


cw::rc_t cw::io::uiCreateDiv(        handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createDiv(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateLabel(      handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createLabel(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateButton(     handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createButton(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateCheck(      handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createCheck(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateCheck(      handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, bool value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createCheck(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateSelect(     handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createSelect(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateOption(     handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createOption(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateStrDisplay( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createStrDisplay(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateStrDisplay( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, const char* value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createStrDisplay(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateStr(        handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createStr(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateStr(        handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, const char* value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createStr(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, unsigned decPl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumbDisplay(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,decPl);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, unsigned decPl, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumbDisplay(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumb(       handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumb(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,minValue,maxValue,stepValue,decPl);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumb(       handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumb(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,minValue,maxValue,stepValue,decPl,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateProg(       handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createProg(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,minValue,maxValue);
  return rc;  
}
  
cw::rc_t cw::io::uiCreateProg(       handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createProg(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title,minValue,maxValue,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateLog(       handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createLog(uiH,uuIdRef,parentUuId,eleName,appId,chanId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiSetNumbRange( handle_t h, unsigned uuId, double minValue, double maxValue, double stepValue, unsigned decPl, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setNumbRange(uiH,uuId,minValue,maxValue,stepValue,decPl,value);
  return rc;
}

cw::rc_t cw::io::uiSetProgRange( handle_t h, unsigned uuId, double minValue, double maxValue, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setProgRange(uiH,uuId,minValue,maxValue,value);
  return rc;
}

cw::rc_t cw::io::uiSetLogLine(     handle_t h, unsigned uuId, const char* text )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setLogLine(uiH,uuId,text);
  return rc;
}
    
cw::rc_t cw::io::uiSetClickable(   handle_t h, unsigned uuId, bool clickableFl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setClickable(uiH,uuId,clickableFl);
  return rc;
}

cw::rc_t cw::io::uiClearClickable( handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::clearClickable(uiH,uuId);
  return rc;
}

bool cw::io::uiIsClickable(    handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::isClickable(uiH,uuId);
  return false;
}
    
cw::rc_t cw::io::uiSetSelect(      handle_t h, unsigned uuId, bool enableFl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setSelect(uiH,uuId,enableFl);
  return rc;
}

cw::rc_t cw::io::uiClearSelect(    handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::clearSelect(uiH,uuId);
  return rc;
}

bool cw::io::uiIsSelected(     handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::isSelected(uiH,uuId);
  return false;
}

cw::rc_t cw::io::uiSetVisible(     handle_t h, unsigned uuId, bool enableFl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setVisible(uiH,uuId,enableFl);
  return rc;
}

cw::rc_t cw::io::uiClearVisible(   handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::clearVisible(uiH,uuId);
  return rc;

}

bool cw::io::uiIsVisible(      handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::isVisible(uiH,uuId);
  return false;
}
    
cw::rc_t cw::io::uiSetEnable(      handle_t h, unsigned uuId, bool enableFl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setEnable(uiH,uuId,enableFl);
  return rc;
}

cw::rc_t cw::io::uiClearEnable(    handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::clearEnable(uiH,uuId);
  return rc;

}

bool cw::io::uiIsEnabled(      handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::isEnabled(uiH,uuId);
  return false;
}

cw::rc_t cw::io::uiSetOrderKey(    handle_t h, unsigned uuId, int orderKey )
{
  rc_t rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setOrderKey(uiH,uuId,orderKey);
  return rc;
}

int  cw::io::uiGetOrderKey(    handle_t h, unsigned uuId )
{
  rc_t rc;
  ui::handle_t uiH;
  int orderKey = 0;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    orderKey = ui::getOrderKey(uiH,uuId);
  return orderKey;
}

cw::rc_t    cw::io::uiSetBlob(   handle_t h, unsigned uuId, const void* blob, unsigned blobByteN )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::setBlob( uiH, uuId, blob, blobByteN );
  return rc;
}

const void* cw::io::uiGetBlob(   handle_t h, unsigned uuId, unsigned& blobByteN_Ref )
{
  ui::handle_t uiH;
  if( _handleToUiHandle(h,uiH) == kOkRC )
    return ui::getBlob( uiH, uuId, blobByteN_Ref );
  
  blobByteN_Ref = 0;
  return nullptr;
}

cw::rc_t cw::io::uiClearBlob( handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::clearBlob( uiH, uuId );
  return rc;
}


cw::rc_t cw::io::uiRegisterAppIdMap(  handle_t h, const ui::appIdMap_t* map, unsigned mapN )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::registerAppIdMap(uiH,map,mapN);
  return rc;  
}

cw::rc_t cw::io::uiSendValue(   handle_t h, unsigned uuId, bool value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::sendValueBool(uiH,  uuId, value );
  return rc;
}

cw::rc_t cw::io::uiDestroyElement( handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::destroyElement(uiH,  uuId );
  return rc;
}


cw::rc_t cw::io::uiSendValue(    handle_t h, unsigned uuId, int value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::sendValueInt(uiH,  uuId, value );
  return rc;
}

cw::rc_t cw::io::uiSendValue(   handle_t h, unsigned uuId, unsigned value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::sendValueUInt(uiH,  uuId, value );
  return rc;
}

cw::rc_t cw::io::uiSendValue(  handle_t h, unsigned uuId, float value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::sendValueFloat(uiH,  uuId, value );
  return rc;
}

cw::rc_t cw::io::uiSendValue( handle_t h, unsigned uuId, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::sendValueDouble(uiH,  uuId, value );
  return rc;
}

cw::rc_t cw::io::uiSendValue( handle_t h, unsigned uuId, const char* value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::sendValueString(uiH,  uuId, value );
  return rc;
}

void cw::io::uiReport( handle_t h )
{
  ui::handle_t uiH;
  if(_handleToUiHandle(h,uiH) == kOkRC )
    ui::report(uiH);
}
