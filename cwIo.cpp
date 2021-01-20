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

#include "cwWebSock.h"
#include "cwUi.h"


namespace cw
{
  namespace io
  {

    struct io_str;
    
    typedef struct serialPort_str
    {
      char*                   name;
      char*                   device;
      unsigned                baudRate;
      unsigned                flags;
      unsigned                pollPeriodMs;      
      serialPortSrv::handle_t serialH;
    } serialPort_t;

    typedef struct audioGroup_str
    {
      bool                   enableFl;
      audio_msg_t            msg;
      mutex::handle_t        mutexH;
      unsigned               threadTimeOutMs;
      struct io_str*         p;
    } audioGroup_t;

    typedef struct audioDev_str
    {
      bool               enableFl; // 
      char*              devName;  //
      unsigned           devIdx;   //
      unsigned           iCbCnt;   // Count the audio driver in/out callbacks
      unsigned           oCbCnt;   //
      audioGroup_t*      iGroup;   // Audio group points for this device
      audioGroup_t*      oGroup;   //
      audio_group_dev_t* iagd;     // audio group device record assoc'd with this
      audio_group_dev_t* oagd;
    } audioDev_t;

    typedef struct io_str
    {
      std::atomic<bool>             quitFl;
      
      cbFunc_t                      cbFunc;
      void*                         cbArg;
      
      thread_mach::handle_t         threadMachH;
      
      object_t*                     cfg;
      
      serialPort_t*                 serialA;
      unsigned                      serialN;
      
      midi::device::handle_t        midiH;
      
      audio::device::handle_t       audioH;
      audio::device::alsa::handle_t alsaH;
      audio::buf::handle_t          audioBufH;
      unsigned                      audioThreadTimeOutMs;
      
      audioDev_t*                   audioDevA;
      unsigned                      audioDevN;
      
      audioGroup_t*                 audioGroupA;
      unsigned                      audioGroupN;
      
      ui::ws::handle_t              wsUiH;       // ui::ws handle (invalid if no UI was specified)
      ui::appIdMap_t*               uiMapA;      // Application supplied id's for the UI resource supplied with the cfg script via 'uiCfgFn'.
      unsigned                      uiMapN;      //
      
    } io_t;
  

    io_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,io_t>(h); }

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

        mutex::unlock( ag->mutexH );
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
      
      mem::free(p->audioDevA);
      p->audioDevN = 0;

    errLabel:

      if(rc != kOkRC )
        rc = cwLogError(rc,"Audio sub-system shutdown failed.");
      
      return rc;
      
    }

    
    rc_t _destroy( io_t* p )
    {
      rc_t rc = kOkRC;

      // stop thread callbacks
      if((rc = thread_mach::destroy(p->threadMachH)) != kOkRC )
        return rc;

      for(unsigned i=0; i<p->serialN; ++i)
        serialPortSrv::destroy( p->serialA[i].serialH );

      mem::free(p->serialA);
      p->serialN = 0;

      // TODO: clean up the audio system more systematically
      // by first stopping all the devices and then
      // reversing the creating process.
      
      _audioDestroy(p);

      midi::device::destroy(p->midiH);

      
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
  
    void _serialPortCb( void* arg, const void* byteA, unsigned byteN )
    {
      //io_t* p = reinterpret_cast<io_t*>(arg);
      
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
        const object_t* e = cfgL->child_ele(i);
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
        
        //io_t* p = reinterpret_cast<io_t*>(pkt->cbDataPtr);

        for(j=0; j<pkt->msgCnt; ++j)
          if( pkt->msgArray != NULL )
            printf("io midi cb: %ld %ld 0x%x %i %i\n", pkt->msgArray[j].timeStamp.tv_sec, pkt->msgArray[j].timeStamp.tv_nsec, pkt->msgArray[j].status,pkt->msgArray[j].d0, pkt->msgArray[j].d1);
          else
            printf("io midi cb: 0x%x ",pkt->sysExMsg[j]);

      }
    }
    
    rc_t _midiPortCreate( io_t* p, const object_t* c )
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
      sample_t** bufArray      = inputFl ? ag->msg.iBufArray : ag->msg.oBufArray;
      unsigned   bufArrayChCnt = inputFl ? ag->msg.iBufChCnt : ag->msg.oBufChCnt;
      audio_group_dev_t* agd   = inputFl ? ag->msg.iDevL     : ag->msg.oDevL;
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
        
          ag->p->cbFunc(ag->p->cbArg,&msg);

          _audioGroupProcSampleBufs( ag->p, ag, kAudioGroupAdvBuf, true );
          _audioGroupProcSampleBufs( ag->p, ag, kAudioGroupAdvBuf, false );
        }
      }
      return true;
    }
    
    // Given a device index return the associated audioDev_t record.
    audioDev_t* _audioDeviceIndexToRecd( io_t* p, unsigned devIdx )
    {
      for(unsigned i=0; i<p->audioDevN; ++i)
        if( p->audioDevA[i].devIdx == devIdx )
          return p->audioDevA + i;
      
      return nullptr;
    }

    
    // Add a audioGroup pointer to groupA[] and return the new count of elements in the array.
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
          std::atomic_store_explicit(&ad->iagd->readyCnt, ad->iagd->readyCnt++,  std::memory_order_relaxed); 
          curGroupN = _audioDeviceUpdateGroupArray( groupA, groupN, curGroupN, ad->iGroup ); 
          ad->iagd->cbCnt += 1; // update the callback count for this device
        }
      }
      else // if an output packet was received on this device
      {    
        if( audio::buf::isDeviceReady( p->audioBufH, devIdx, audio::buf::kOutFl ) )
        {
          std::atomic_store_explicit(&ad->oagd->readyCnt, ad->oagd->readyCnt++, std::memory_order_relaxed); // atomic incr  
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
        if( std::atomic_load_explicit(&agd->readyCnt, std::memory_order_acquire) > 0  ) // ACQUIRE
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

    // This function is called by the audio device drivers to when incoming audio arrives
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
      
      new_agd->name   = ad->devName;
      new_agd->devIdx = ad->devIdx;
      new_agd->chCnt  = chCnt;

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
    audioGroup_t* _audioGroupFromId( io_t* p, unsigned groupId )
    {
      for(unsigned i=0; i<p->audioGroupN; ++i)
        if( p->audioGroupA[i].msg.groupId == groupId )
          return p->audioGroupA + i;

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
                "id",          p->audioGroupA[i].msg.groupId,
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
      p->audioDevN = deviceL_Node->child_count();
      p->audioDevA = mem::allocZ<audioDev_t>(p->audioDevN);

      // fill in the audio device cfg list
      for(unsigned i=0; i<p->audioDevN; ++i)
      {
        audioDev_t*   ad          = p->audioDevA + i;
        unsigned      framesPerCycle = 0;
        unsigned      cycleCnt   = 0;
        
        audioGroup_t* iag        = nullptr;
        audioGroup_t* oag        = nullptr;
        
        double        israte     = 0;
        double        osrate     = 0;
        double        srate      = 0;

        unsigned      iDspFrameCnt = 0;
        unsigned      oDspFrameCnt = 0;
        unsigned      dspFrameCnt  = 0;
        
        unsigned      inGroupId  = kInvalidId;
        unsigned      outGroupId = kInvalidId;
        
        const object_t* node;
        
        if((node = deviceL_Node->child_ele(i)) != nullptr )
        {
          if(( rc = node->getv(
                "enableFl",       ad->enableFl,
                "inGroupId",      inGroupId,
                "outGroupId",     outGroupId,
                "device",         ad->devName,
                "framesPerCycle", framesPerCycle,
                "cycleCnt",       cycleCnt )) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing audio cfg record at index:%i",i);
            goto errLabel;
          }          
        }

        // if the configuration is enabled
        if( ad->enableFl )
        {
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
          if((iag = _audioGroupFromId(p, inGroupId )) != nullptr )
          {
            israte       = iag->msg.srate;
            iDspFrameCnt = iag->msg.dspFrameCnt;
          }

          // get the outgroup 
          if((oag = _audioGroupFromId(p, outGroupId)) != nullptr )
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
            rc = cwLogError(kInvalidArgRC,"The device '%s' belongs to two groups (id:%i and id:%i) at different sample rates (%f != %f).", cwStringNullGuard(ad->devName), inGroupId, outGroupId, israte, osrate );
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
            rc = cwLogError(kInvalidArgRC,"The device '%s' belongs to two groups (id:%i and id:%i) width different dspFrameCnt values (%i != %i).", cwStringNullGuard(ad->devName), inGroupId, outGroupId, iDspFrameCnt, oDspFrameCnt );
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

          if( iag != nullptr )
            if((rc = _audioGroupAddDevice( iag, true, ad, iChCnt )) != kOkRC )
              goto errLabel;

          if( oag != nullptr )
            if((rc = _audioGroupAddDevice( oag, false, ad, oChCnt )) != kOkRC )
              goto errLabel;

          // set the device group pointers
          ad->iGroup = iag;
          ad->oGroup = oag;
            
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
          ag->msg.iBufArray = mem::allocZ<sample_t*>( ag->msg.iBufChCnt );
        
        if( ag->msg.oBufChCnt )
          ag->msg.oBufArray = mem::allocZ<sample_t*>( ag->msg.oBufChCnt );          
      }
      
      return rc;
    }

    rc_t _audioDeviceParseConfig( io_t* p, const object_t* cfg )
    {
      rc_t            rc          = kOkRC;
      unsigned        meterMs     = 50;
      const object_t* node        = nullptr;
      
      // get the audio port node
      if((node = cfg->find("audio")) == nullptr )
        return cwLogError(kSyntaxErrorRC,"Unable to locate the 'audio' configuration node.");

      // get the meterMs value
      if((rc = node->getv("meterMs", meterMs, "threadTimeOutMs", p->audioThreadTimeOutMs )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Audio 'meterMs' or 'dspFrameCnt' parse failed.");
        goto errLabel;
      }

      // initialize the audio buffer
      if((rc = audio::buf::create( p->audioBufH, audio::device::count(p->audioH), meterMs )) != kOkRC )
      {
        rc = cwLogError(rc,"Audio device buffer failed.");
        goto errLabel;
      }

      // parse the audio group list
      if((rc = _audioDeviceParseAudioGroupList( p, cfg )) != kOkRC )
      {
        rc = cwLogError(rc,"Parse audio group list.");
        goto errLabel;
      }
      
      // parse the audio device list
      if((rc = _audioDeviceParseAudioDeviceList( p, cfg )) != kOkRC )
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


    rc_t _audioDeviceCreate( io_t* p, const object_t* c )
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
      if((rc = _audioDeviceParseConfig( p, c )) != kOkRC )
      {
        rc = cwLogError(rc,"Audio device configuration failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }


    // This function is called by the websocket with messages comring from a remote UI.
    rc_t _uiCallback( void* cbArg, unsigned wsSessId, ui::opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, const ui::value_t* v )
    {
      io_t* p = (io_t*)cbArg;
      msg_t r;
      
      r.tid  = kUiTId;
      r.u.ui = { .opId=opId, .wsSessId=wsSessId, .parentAppId=parentAppId, .uuId=uuId, .appId=appId, .value=v };

      return p->cbFunc(p->cbArg,&r);
    }

    rc_t _uiConfig( io_t* p, const object_t* c, const ui::appIdMap_t* mapA, unsigned mapN )
    {
      rc_t           rc         = kOkRC;
      const char*    uiCfgLabel = "ui";
      ui::ws::args_t args;


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
      if( c->find(uiCfgLabel) != nullptr )
      {

        // parse the ui 
        if((rc = ui::ws::parseArgs( *c, args, uiCfgLabel )) == kOkRC )
        {  
          rc = ui::ws::create(p->wsUiH, args, p, _uiCallback, args.uiRsrc, p->uiMapA, p->uiMapN);
          
          ui::ws::releaseArgs(args);
        
        }
      }
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
    

  }
}



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
  p->cfg = o->duplicate();

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
  if((rc = _audioDeviceCreate(p,p->cfg)) != kOkRC )
    goto errLabel;
  
  // create the UI interface
  if((rc = _uiConfig(p,p->cfg, mapA, mapN)) != kOkRC )
    goto errLabel;
  

  p->cbFunc = cbFunc;
  p->cbArg  = cbArg;
  p->quitFl.store(false);
    
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
  return kOkRC;
}

cw::rc_t cw::io::exec( handle_t h )
{
  rc_t rc = kOkRC;
  io_t* p = _handleToPtr(h);
  if( p->wsUiH.isValid() )    
    rc = ui::ws::exec(p->wsUiH );

  // if the application quit flag is set then signal that the main thread needs to exit
  
  
  return rc;
}

bool cw::io::isShuttingDown( handle_t h )
{
  io_t* p = _handleToPtr(h);

  return p->quitFl.load();

  
  //return thread_mach::is_shutdown(p->threadMachH);
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
  rc_t rc = kOkRC;
  //io_t* p = _handleToPtr(h);
  //return midi::device::send( p->midiH, devIdx, portIdx, status, d0, d1 );
  return rc;
}

//----------------------------------------------------------------------------------------------------------
//
// Audio
//

unsigned    cw::io::audioDeviceCount(          handle_t h )
{
  io_t* p = _handleToPtr(h);
  return audio::device::count(p->audioH);
}

unsigned    cw::io::audioDeviceLabelToIndex(   handle_t h, const char* label )
{
  io_t* p = _handleToPtr(h);
  return audio::device::labelToIndex(p->audioH,label);
}

const char* cw::io::audioDeviceLabel(          handle_t h, unsigned devIdx )
{
  io_t* p = _handleToPtr(h);
  assert( devIdx < audioDeviceCount(h) );
  return audio::device::label(p->audioH,devIdx);
}



//----------------------------------------------------------------------------------------------------------
//
// Socket
//


//----------------------------------------------------------------------------------------------------------
//
// WebSocket
//



//----------------------------------------------------------------------------------------------------------
//
// UI
//

unsigned    cw::io::uiFindElementAppId(  handle_t h, unsigned parentUuId, const char* eleName )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementAppId(uiH, parentUuId, eleName );
  return kInvalidId;
}

unsigned    cw::io::uiFindElementUuId(   handle_t h, unsigned parentUuId, const char* eleName )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementUuId(uiH, parentUuId, eleName );
  return kInvalidId;
}

const char* cw::io::uiFindElementName(   handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementName(uiH, uuId );
  return nullptr;
}

unsigned    cw::io::uiFindElementAppId(  handle_t h, unsigned uuId )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementAppId(uiH, uuId );
  return kInvalidId;  
}

unsigned    cw::io::uiFindElementUuId( handle_t h, const char* eleName )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    return ui::findElementUuId(uiH, eleName );
  return kInvalidId;  
}

cw::rc_t cw::io::uiCreateFromObject( handle_t h, const object_t* o, unsigned wsSessId, unsigned parentUuId, const char* eleName)
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::createFromObject(uiH,o,wsSessId,parentUuId,eleName);
  return rc;
}

cw::rc_t cw::io::uiCreateFromFile(   handle_t h, const char* fn,    unsigned wsSessId, unsigned parentUuId)
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc = ui::createFromFile(uiH,fn,wsSessId,parentUuId);
  return rc;
}

cw::rc_t cw::io::uiCreateFromText(   handle_t h, const char* text,  unsigned wsSessId, unsigned parentUuId)
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createFromText(uiH,text,wsSessId,parentUuId);
  return rc;
}

cw::rc_t cw::io::uiCreateDiv(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createDiv(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateTitle(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createTitle(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateButton(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createButton(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateCheck(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createCheck(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateCheck(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, bool value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createCheck(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateSelect(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createSelect(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateOption(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createOption(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateStr(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createStr(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
  return rc;  
}

cw::rc_t cw::io::uiCreateStr(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, const char* value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createStr(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, unsigned decPl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumbDisplay(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,decPl);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, unsigned decPl, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumbDisplay(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumb(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumb(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,minValue,maxValue,stepValue,decPl);
  return rc;  
}

cw::rc_t cw::io::uiCreateNumb(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createNumb(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,minValue,maxValue,stepValue,decPl,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateProg(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createProg(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,minValue,maxValue);
  return rc;  
}
  
cw::rc_t cw::io::uiCreateProg(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double value )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createProg(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title,minValue,maxValue,value);
  return rc;  
}

cw::rc_t cw::io::uiCreateText(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t         rc;
  ui::handle_t uiH;
  if((rc = _handleToUiHandle(h,uiH)) == kOkRC )
    rc  = ui::createText(uiH,uuIdRef,wsSessId,parentUuId,eleName,appId,clas,title);
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

