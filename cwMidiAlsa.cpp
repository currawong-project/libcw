#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwMidi.h"
#include "cwTextBuf.h"
#include "cwMidiPort.h"
#include "cwThread.h"

#include <alsa/asoundlib.h>

namespace cw
{
  namespace midi
  {
    namespace device
    {
      typedef struct
      {
        bool             inputFl;   // true if this an input port
        char*            nameStr;   // string label of this device
        unsigned         alsa_type; // ALSA type flags from snd_seq_port_info_get_type()
        unsigned         alsa_cap;  // ALSA capability flags from snd_seq_port_info_get_capability()
        snd_seq_addr_t   alsa_addr; // ALSA client/port address for this port
        parser::handle_t parserH;   // interface to the client callback function for this port
      } port_t;

      // MIDI devices 
      typedef struct
      {
        char*         nameStr;    // string label for this device
        unsigned      iPortCnt;   // input ports on this device
        port_t*       iPortArray; //
        unsigned      oPortCnt;   // output ports on this device
        port_t*       oPortArray; //
        unsigned char clientId;   // ALSA client id (all ports on this device use use this client id in their address)

      } dev_t;

      typedef struct device_str
      {
        unsigned         devCnt;           // MIDI devices attached to this computer
        dev_t*           devArray;
        cbFunc_t         cbFunc;           // MIDI input application callback 
        void*            cbDataPtr;
        snd_seq_t*       h;                // ALSA system sequencer handle
        snd_seq_addr_t   alsa_addr;        // ALSA client/port address representing the application
        int              alsa_queue;       // ALSA device queue
        thread::handle_t thH;              // MIDI input listening thread
        int              alsa_fdCnt;       // MIDI input driver file descriptor array
        struct pollfd*   alsa_fd;
        dev_t*           prvRcvDev;        // the last device and port to rcv MIDI 
        port_t*          prvRcvPort;
        unsigned         prvTimeMicroSecs; // time of last recognized event in microseconds
        unsigned         eventCnt;         // count of recognized events
        time::spec_t     baseTimeStamp;
      } device_t;

      device_t* _handleToPtr( handle_t h ){ return handleToPtr<handle_t,device_t>(h); }

      rc_t _cmMpErrMsgV(rc_t rc, int alsaRc, const char* fmt, va_list vl )
      {
        if( alsaRc < 0 )
          cwLogError(kOpFailRC,"ALSA Error:%i %s",alsaRc,snd_strerror(alsaRc));

        return cwLogVError(rc,fmt,vl);  
      }

      rc_t _cmMpErrMsg(rc_t rc, int alsaRc, const char* fmt, ... )
      {
        va_list vl;
        va_start(vl,fmt);
        rc = _cmMpErrMsgV(rc,alsaRc,fmt,vl);
        va_end(vl);
        return rc;
      }

      unsigned _cmMpGetPortCnt( snd_seq_t* h, snd_seq_port_info_t* pip, bool inputFl )
      {
        unsigned i = 0;

        snd_seq_port_info_set_port(pip,-1);

        while( snd_seq_query_next_port(h,pip) == 0)
          if( cwIsFlag(snd_seq_port_info_get_capability(pip),inputFl?SND_SEQ_PORT_CAP_READ:SND_SEQ_PORT_CAP_WRITE) ) 
            ++i;
 
        return i;
      }

      dev_t* _cmMpClientIdToDev( device_t* p, int clientId )
      {
        unsigned    i;
        for(i=0; i<p->devCnt; ++i)
          if( p->devArray[i].clientId == clientId )
            return p->devArray + i;

        return NULL;
      }

      port_t* _cmMpInPortIdToPort( dev_t* dev, int portId )
      {
        unsigned i;

        for(i=0; i<dev->iPortCnt; ++i)
          if( dev->iPortArray[i].alsa_addr.port == portId )
            return dev->iPortArray + i;

        return NULL;
      }


      void _cmMpSplit14Bits( unsigned v, uint8_t* d0, uint8_t* d1 )
      {
        *d0 = (v & 0x3f80) >> 7;
        *d1 = v & 0x7f;
      }

      rc_t _cmMpPoll(device_t* p)
      {
        rc_t rc        = kOkRC;
        int  timeOutMs = 50;

        snd_seq_event_t *ev;

        if (poll(p->alsa_fd, p->alsa_fdCnt, timeOutMs) > 0) 
        {
          int rc = 1;

          do
          {
            rc = snd_seq_event_input(p->h,&ev);

            // if no input
            if( rc == -EAGAIN )
            {
              // TODO: report or at least count error
              break;
            }
            
            // if input buffer overrun
            if( rc == -ENOSPC )
            {
              // TODO: report or at least count error
              break;
            }
            
            // get the device this event arrived from
            if( p->prvRcvDev==NULL || p->prvRcvDev->clientId != ev->source.client )
              p->prvRcvDev = _cmMpClientIdToDev(p,ev->source.client);
      
            // get the port this event arrived from
            if( p->prvRcvDev != NULL && (p->prvRcvPort==NULL || p->prvRcvPort->alsa_addr.port != ev->source.port) )
              p->prvRcvPort = _cmMpInPortIdToPort(p->prvRcvDev,ev->source.port);

            if( p->prvRcvDev == NULL || p->prvRcvPort == NULL )
              continue;
            
            //printf("%i %x\n",ev->type,ev->type);
            //printf("dev:%i port:%i ch:%i %i\n",ev->source.client,ev->source.port,ev->data.note.channel,ev->data.note.note);

            unsigned     microSecs1     = (ev->time.time.tv_sec * 1000000) + (ev->time.time.tv_nsec/1000);
            //unsigned     deltaMicroSecs = p->prvTimeMicroSecs==0 ? 0 : microSecs1 - p->prvTimeMicroSecs;
            uint8_t d0             = 0xff;
            uint8_t d1             = 0xff;
            uint8_t status         = 0;

            switch(ev->type)
            {
              //
              // MIDI Channel Messages
              //

              case SND_SEQ_EVENT_NOTEON:
                status = kNoteOnMdId;
                d0     = ev->data.note.note;
                d1     = ev->data.note.velocity;
                //printf("%s (%i : %i) (%i)\n", snd_seq_ev_is_abstime(ev)?"abs":"rel",ev->time.time.tv_sec,ev->time.time.tv_nsec, deltaMicroSecs/1000);
                break;

              case SND_SEQ_EVENT_NOTEOFF:
                status = kNoteOffMdId;
                d0     = ev->data.note.note;
                d1     = ev->data.note.velocity;
                break;

              case SND_SEQ_EVENT_KEYPRESS:
                status = kPolyPresMdId;
                d0     = ev->data.note.note;
                d1     = ev->data.note.velocity;
                break;

              case SND_SEQ_EVENT_PGMCHANGE:
                status = kPgmMdId;
                d0     = ev->data.control.param;
                d1     = 0xff;
                break;

              case SND_SEQ_EVENT_CHANPRESS:
                status = kChPresMdId;
                d0     = ev->data.control.param;
                d1     = 0xff;
                break;

              case SND_SEQ_EVENT_CONTROLLER:
                status = kCtlMdId;
                d0     = ev->data.control.param;
                d1     = ev->data.control.value;
                break;

              case SND_SEQ_EVENT_PITCHBEND:
                _cmMpSplit14Bits(ev->data.control.value + 8192, &d0, &d1 );
                status = kPbendMdId;
                break;

                //
                // MIDI System Common Messages
                //
              case SND_SEQ_EVENT_QFRAME:       
                status = kSysComMtcMdId;  
                d0     = ev->data.control.value;
                break;

              case SND_SEQ_EVENT_SONGPOS:      
                _cmMpSplit14Bits(ev->data.control.value, &d0, &d1 );
                status = kSysComSppMdId;            
                break;

              case SND_SEQ_EVENT_SONGSEL:      
                status = kSysComSelMdId;  
                d0     = ev->data.control.value;
                break;

              case SND_SEQ_EVENT_TUNE_REQUEST: 
                status = kSysComTuneMdId; 
                break;

                //
                // MIDI System Real-time Messages
                //
              case SND_SEQ_EVENT_CLOCK:     status = kSysRtClockMdId; break;
              case SND_SEQ_EVENT_START:     status = kSysRtStartMdId; break;
              case SND_SEQ_EVENT_CONTINUE:  status = kSysRtContMdId;  break;          
              case SND_SEQ_EVENT_STOP:      status = kSysRtStopMdId;  break;
              case SND_SEQ_EVENT_SENSING:   status = kSysRtSenseMdId; break;
              case SND_SEQ_EVENT_RESET:     status = kSysRtResetMdId; break;

              case SND_SEQ_EVENT_SYSEX: 
                //printf("Sysex: %i\n",ev->data.ext.len);
                break;
                

            }

            if( status != 0 )
            {
              uint8_t ch = ev->data.note.channel;
              time::spec_t ts;
              ts.tv_sec  = p->baseTimeStamp.tv_sec  + ev->time.time.tv_sec;
              ts.tv_nsec = p->baseTimeStamp.tv_nsec + ev->time.time.tv_nsec;
              while( ts.tv_nsec > 1000000000 )
              {
                ts.tv_nsec -= 1000000000;
                ts.tv_sec  += 1;
              }

              //printf("MIDI: %ld %ld : 0x%x %i %i\n",ts.tv_sec,ts.tv_nsec,status,d0,d1);

              parser::midiTriple(p->prvRcvPort->parserH, &ts, status | ch, d0, d1 );

              p->prvTimeMicroSecs  = microSecs1;
              p->eventCnt         += 1;
            }

          }while( snd_seq_event_input_pending(p->h,0));

          parser::transmit(p->prvRcvPort->parserH);
        }  

        return rc;
      }


      bool _threadCbFunc(void* arg)
      {
        device_t* p = static_cast<device_t*>(arg);
        _cmMpPoll(p);
        return true;
      }

      rc_t _cmMpAllocStruct( device_t* p, const char* appNameStr, cbFunc_t cbFunc, void* cbDataPtr, unsigned parserBufByteCnt )
      {
        rc_t                      rc   = kOkRC;
        snd_seq_client_info_t*    cip  = NULL;
        snd_seq_port_info_t*      pip  = NULL;
        snd_seq_port_subscribe_t *subs = NULL;
        unsigned                  i,j,k,arc;

        // alloc the subscription recd on the stack
        snd_seq_port_subscribe_alloca(&subs);

        // alloc the client recd
        if((arc = snd_seq_client_info_malloc(&cip)) < 0 )
        {
          rc = _cmMpErrMsg(kOpFailRC,arc,"ALSA seq client info allocation failed.");
          goto errLabel;
        }

        // alloc the port recd
        if((arc = snd_seq_port_info_malloc(&pip)) < 0 )
        {
          rc = _cmMpErrMsg(kOpFailRC,arc,"ALSA seq port info allocation failed.");
          goto errLabel;
        }

        if((p->alsa_queue = snd_seq_alloc_queue(p->h)) < 0 )
        {
          rc = _cmMpErrMsg(kOpFailRC,p->alsa_queue,"ALSA queue allocation failed.");
          goto errLabel;
        }

        // Set arbitrary tempo (mm=100) and resolution (240) (FROM RtMidi.cpp)
        /*
          snd_seq_queue_tempo_t *qtempo;
          snd_seq_queue_tempo_alloca(&qtempo);
          snd_seq_queue_tempo_set_tempo(qtempo, 600000);
          snd_seq_queue_tempo_set_ppq(qtempo, 240);
          snd_seq_set_queue_tempo(p->h, p->alsa_queue, qtempo);
          snd_seq_drain_output(p->h);
        */

        // setup the client port
        snd_seq_set_client_name(p->h,appNameStr);
        snd_seq_port_info_set_client(pip, p->alsa_addr.client = snd_seq_client_id(p->h) );
        snd_seq_port_info_set_name(pip,cwStringNullGuard(appNameStr));
        snd_seq_port_info_set_capability(pip,SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_DUPLEX | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE );
        snd_seq_port_info_set_type(pip, SND_SEQ_PORT_TYPE_SOFTWARE | SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC );
 
        snd_seq_port_info_set_midi_channels(pip, 16);

        // cfg for real-time time stamping
        snd_seq_port_info_set_timestamping(pip, 1);
        snd_seq_port_info_set_timestamp_real(pip, 1);    
        snd_seq_port_info_set_timestamp_queue(pip, p->alsa_queue);

        // create the client port
        if((p->alsa_addr.port = snd_seq_create_port(p->h,pip)) < 0 )
        {
          rc = _cmMpErrMsg(kOpFailRC,p->alsa_addr.port,"ALSA client port creation failed.");
          goto errLabel;
        }


        p->devCnt = 0;

        // determine the count of devices
        snd_seq_client_info_set_client(cip, -1);
        while( snd_seq_query_next_client(p->h,cip) == 0)
          p->devCnt += 1;

        // allocate the device array
        p->devArray = mem::allocZ<dev_t>(p->devCnt);

        // fill in each device record
        snd_seq_client_info_set_client(cip, -1);
        for(i=0; snd_seq_query_next_client(p->h,cip)==0; ++i)
        {
          assert(i<p->devCnt);

          int         client = snd_seq_client_info_get_client(cip);
          const char* name   = snd_seq_client_info_get_name(cip);
    
          // initalize the device record
          p->devArray[i].nameStr    = mem::duplStr(cwStringNullGuard(name));
          p->devArray[i].iPortCnt   = 0;
          p->devArray[i].oPortCnt   = 0;
          p->devArray[i].iPortArray = NULL;
          p->devArray[i].oPortArray = NULL;
          p->devArray[i].clientId   = client;


          snd_seq_port_info_set_client(pip,client);
          snd_seq_port_info_set_port(pip,-1);

          // determine the count of in/out ports on this device
          while( snd_seq_query_next_port(p->h,pip) == 0 )
          {
            unsigned    caps = snd_seq_port_info_get_capability(pip);

            if( cwIsFlag(caps,SND_SEQ_PORT_CAP_READ) )
              p->devArray[i].iPortCnt += 1;

            if( cwIsFlag(caps,SND_SEQ_PORT_CAP_WRITE) )
              p->devArray[i].oPortCnt += 1;
      
          }

          // allocate the device port arrays
          if( p->devArray[i].iPortCnt > 0 )
            p->devArray[i].iPortArray = mem::allocZ<port_t>(p->devArray[i].iPortCnt);

          if( p->devArray[i].oPortCnt > 0 )
            p->devArray[i].oPortArray = mem::allocZ<port_t>(p->devArray[i].oPortCnt);
    

          snd_seq_port_info_set_client(pip,client);    // set the ports client id
          snd_seq_port_info_set_port(pip,-1);

          // fill in the port information 
          for(j=0,k=0; snd_seq_query_next_port(p->h,pip) == 0; )
          {
            const char*    port = snd_seq_port_info_get_name(pip);
            unsigned       type = snd_seq_port_info_get_type(pip);
            unsigned       caps = snd_seq_port_info_get_capability(pip);
            snd_seq_addr_t addr = *snd_seq_port_info_get_addr(pip);

            if( cwIsFlag(caps,SND_SEQ_PORT_CAP_READ) )
            {
              assert(j<p->devArray[i].iPortCnt);
              p->devArray[i].iPortArray[j].inputFl   = true;
              p->devArray[i].iPortArray[j].nameStr   = mem::duplStr(cwStringNullGuard(port));
              p->devArray[i].iPortArray[j].alsa_type = type;
              p->devArray[i].iPortArray[j].alsa_cap  = caps;
              p->devArray[i].iPortArray[j].alsa_addr = addr;
              parser::create(p->devArray[i].iPortArray[j].parserH, i, j, cbFunc, cbDataPtr, parserBufByteCnt);

              // port->app
              snd_seq_port_subscribe_set_sender(subs, &addr);
              snd_seq_port_subscribe_set_dest(subs, &p->alsa_addr);
              snd_seq_port_subscribe_set_queue(subs, 1);
              snd_seq_port_subscribe_set_time_update(subs, 1);
              snd_seq_port_subscribe_set_time_real(subs, 1);
              if((arc = snd_seq_subscribe_port(p->h, subs)) < 0)
                rc = _cmMpErrMsg(kOpFailRC,arc,"Input port to app. subscription failed on port '%s'.",cwStringNullGuard(port));

              ++j;
            }

            if( cwIsFlag(caps,SND_SEQ_PORT_CAP_WRITE) )
            {
              assert(k<p->devArray[i].oPortCnt);
              p->devArray[i].oPortArray[k].inputFl   = false;
              p->devArray[i].oPortArray[k].nameStr   = mem::duplStr(cwStringNullGuard(port));
              p->devArray[i].oPortArray[k].alsa_type = type;
              p->devArray[i].oPortArray[k].alsa_cap  = caps;
              p->devArray[i].oPortArray[k].alsa_addr = addr;

              // app->port connection
              snd_seq_port_subscribe_set_sender(subs, &p->alsa_addr);
              snd_seq_port_subscribe_set_dest(  subs, &addr);
              if((arc = snd_seq_subscribe_port(p->h, subs)) < 0 )
                rc = _cmMpErrMsg(kOpFailRC,arc,"App to output port subscription failed on port '%s'.",cwStringNullGuard(port));
       
              ++k;
            }
          }
        }

      errLabel:
        if( pip != NULL)
          snd_seq_port_info_free(pip);

        if( cip != NULL )
          snd_seq_client_info_free(cip);

        return rc;
  
      }

      void _cmMpReportPort( textBuf::handle_t tbH, const port_t* port )
      {
        textBuf::print( tbH,"    client:%i port:%i    '%s' caps:(",port->alsa_addr.client,port->alsa_addr.port,port->nameStr);
        if( port->alsa_cap & SND_SEQ_PORT_CAP_READ       ) textBuf::print( tbH,"Read " );
        if( port->alsa_cap & SND_SEQ_PORT_CAP_WRITE      ) textBuf::print( tbH,"Writ " );
        if( port->alsa_cap & SND_SEQ_PORT_CAP_SYNC_READ  ) textBuf::print( tbH,"Syrd " );
        if( port->alsa_cap & SND_SEQ_PORT_CAP_SYNC_WRITE ) textBuf::print( tbH,"Sywr " );
        if( port->alsa_cap & SND_SEQ_PORT_CAP_DUPLEX     ) textBuf::print( tbH,"Dupl " );
        if( port->alsa_cap & SND_SEQ_PORT_CAP_SUBS_READ  ) textBuf::print( tbH,"Subr " );
        if( port->alsa_cap & SND_SEQ_PORT_CAP_SUBS_WRITE ) textBuf::print( tbH,"Subw " );
        if( port->alsa_cap & SND_SEQ_PORT_CAP_NO_EXPORT  ) textBuf::print( tbH,"Nexp " );

        textBuf::print( tbH,") type:(");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_SPECIFIC   )    textBuf::print( tbH,"Spec ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_MIDI_GENERIC)   textBuf::print( tbH,"Gnrc ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_MIDI_GM  )      textBuf::print( tbH,"GM ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_MIDI_GS  )      textBuf::print( tbH,"GS ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_MIDI_XG  )      textBuf::print( tbH,"XG ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_MIDI_MT32 )     textBuf::print( tbH,"MT32 ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_MIDI_GM2  )     textBuf::print( tbH,"GM2 ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_SYNTH   )       textBuf::print( tbH,"Syn ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_DIRECT_SAMPLE)  textBuf::print( tbH,"Dsmp ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_SAMPLE   )      textBuf::print( tbH,"Samp ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_HARDWARE   )    textBuf::print( tbH,"Hwar ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_SOFTWARE   )    textBuf::print( tbH,"Soft ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_SYNTHESIZER   ) textBuf::print( tbH,"Sizr ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_PORT   )        textBuf::print( tbH,"Port ");
        if( port->alsa_type & SND_SEQ_PORT_TYPE_APPLICATION   ) textBuf::print( tbH,"Appl ");

        textBuf::print( tbH,")\n");

      }

      rc_t _destroy( device_t* p )
      {
        rc_t rc = kOkRC;
        
        if( p != NULL )
        {
          int arc;

          // stop the thread first
          if((rc = thread::destroy(p->thH)) != kOkRC )
          {
            rc = _cmMpErrMsg(rc,0,"Thread destroy failed.");
            goto errLabel;
          }

          // stop the queue
          if( p->h != NULL )
            if((arc = snd_seq_stop_queue(p->h,p->alsa_queue, NULL)) < 0 )
            {
              rc = _cmMpErrMsg(kOpFailRC,arc,"ALSA queue stop failed.");
              goto errLabel;
            }

          // release the alsa queue
          if( p->alsa_queue != -1 )
          {
            if((arc = snd_seq_free_queue(p->h,p->alsa_queue)) < 0 )
              rc = _cmMpErrMsg(kOpFailRC,arc,"ALSA queue release failed.");
            else
              p->alsa_queue = -1;
          }

          // release the alsa system handle
          if( p->h != NULL )
          {
            if( (arc = snd_seq_close(p->h)) < 0 )
              rc = _cmMpErrMsg(kOpFailRC,arc,"ALSA sequencer close failed.");
            else
              p->h = NULL;
          }

          unsigned i,j;
          for(i=0; i<p->devCnt; ++i)
          {
            for(j=0; j<p->devArray[i].iPortCnt; ++j)
            {
              parser::destroy(p->devArray[i].iPortArray[j].parserH);
              mem::release( p->devArray[i].iPortArray[j].nameStr );
            }

            for(j=0; j<p->devArray[i].oPortCnt; ++j)
            {
              mem::release( p->devArray[i].oPortArray[j].nameStr );
            }
      
            mem::release(p->devArray[i].iPortArray);
            mem::release(p->devArray[i].oPortArray);
            mem::release(p->devArray[i].nameStr);
   
          }

          mem::release(p->devArray);
    
          mem::free(p->alsa_fd);

          mem::release(p);
    
        }
      errLabel:
        return rc;
      }
      
      
    } // device
  } // midi    
} // cw


cw::rc_t cw::midi::device::create(  handle_t& h, cbFunc_t cbFunc, void* cbArg, unsigned parserBufByteCnt, const char* appNameStr )
{
  rc_t rc  = kOkRC;
  int  arc = 0;

  if((rc = destroy(h)) != kOkRC )
    return rc;

  device_t* p   = mem::allocZ<device_t>(1);
  p->h          = NULL;
  p->alsa_queue = -1;


  // create the listening thread
  if((rc = thread::create( p->thH, _threadCbFunc, p)) != kOkRC )
  {
    rc = _cmMpErrMsg(rc,0,"Thread initialization failed.");
    goto errLabel;
  }

  // initialize the ALSA sequencer
  if((arc = snd_seq_open(&p->h, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK )) < 0 )
  {
    rc = _cmMpErrMsg(kOpFailRC,arc,"ALSA Sequencer open failed.");
    goto errLabel;
  }
  
  // prevent valgrind from report memory leaks in libasound (https://stackoverflow.com/questions/13478861/alsa-mem-leak)
  snd_config_update_free_global();

  // setup the device and port structures
  if((rc = _cmMpAllocStruct(p,appNameStr,cbFunc,cbArg,parserBufByteCnt)) != kOkRC )
    goto errLabel;

  // allocate the file descriptors used for polling
  p->alsa_fdCnt = snd_seq_poll_descriptors_count(p->h, POLLIN);
  p->alsa_fd = mem::allocZ<struct pollfd>(p->alsa_fdCnt);
  snd_seq_poll_descriptors(p->h, p->alsa_fd, p->alsa_fdCnt, POLLIN);

  p->cbFunc    = cbFunc;
  p->cbDataPtr = cbArg;

  // start the sequencer queue
  if((arc = snd_seq_start_queue(p->h, p->alsa_queue, NULL)) < 0 )
  {
    rc = _cmMpErrMsg(kOpFailRC,arc,"ALSA queue start failed.");
    goto errLabel;
  }
  
  // send any pending commands to the driver
  snd_seq_drain_output(p->h);
  
  // all time stamps will be an offset from this time stamp
  clock_gettime(CLOCK_MONOTONIC,&p->baseTimeStamp);

  if((rc = thread::unpause(p->thH)) != kOkRC )
    rc = _cmMpErrMsg(rc,0,"Thread start failed.");

  h.set(p);
  
 errLabel:

  if( rc != kOkRC )
    _destroy(p);

  return rc;
  
}


cw::rc_t cw::midi::device::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  
  if( !h.isValid() )
    return rc;

  device_t* p  = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
    
  return rc;
}

bool cw::midi::device::isInitialized(handle_t h)
{ return h.isValid(); }

unsigned      cw::midi::device::count(handle_t h)
{
  device_t* p = _handleToPtr(h);
  return p->devCnt;
}

const char* cw::midi::device::name( handle_t h, unsigned devIdx )
{ 
  device_t* p = _handleToPtr(h);
  
  if( p==NULL || devIdx>=p->devCnt)
    return NULL;

  return p->devArray[devIdx].nameStr;
}

unsigned    cw::midi::device::portCount(  handle_t h, unsigned devIdx, unsigned flags )
{
  device_t* p = _handleToPtr(h);
  
  if( p==NULL || devIdx>=p->devCnt)
    return 0;

  if( cwIsFlag(flags,kInMpFl) )
    return p->devArray[devIdx].iPortCnt;

  return p->devArray[devIdx].oPortCnt;  
}

const char*    cw::midi::device::portName(   handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx )
{
  device_t* p = _handleToPtr(h);
  
  if( p==NULL || devIdx>=p->devCnt)
    return 0;

  if( cwIsFlag(flags,kInMpFl) )
  {
    if( portIdx >= p->devArray[devIdx].iPortCnt )
      return 0;

    return p->devArray[devIdx].iPortArray[portIdx].nameStr;
  }

  if( portIdx >= p->devArray[devIdx].oPortCnt )
    return 0;

  return p->devArray[devIdx].oPortArray[portIdx].nameStr;  
}


cw::rc_t  cw::midi::device::send( handle_t h, unsigned devIdx, unsigned portIdx, uint8_t status, uint8_t d0, uint8_t d1 )
{
  rc_t            rc = kOkRC;
  snd_seq_event_t ev;
  int             arc;
  device_t*       p  = _handleToPtr(h);

  assert( p!=NULL && devIdx < p->devCnt && portIdx < p->devArray[devIdx].oPortCnt );

  port_t* port = p->devArray[devIdx].oPortArray + portIdx;

  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, p->alsa_addr.port);
  //snd_seq_ev_set_subs(&ev);

  snd_seq_ev_set_dest(&ev, port->alsa_addr.client, port->alsa_addr.port);
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_fixed(&ev);

  
  switch( status & 0xf0 )
  {
    case kNoteOffMdId:  
      ev.type = SND_SEQ_EVENT_NOTEOFF;    
      ev.data.note.note     = d0;
      ev.data.note.velocity = d1;
      break;

    case kNoteOnMdId:   
      ev.type               = SND_SEQ_EVENT_NOTEON;     
      ev.data.note.note     = d0;
      ev.data.note.velocity = d1;
      break;

    case kPolyPresMdId: 
      ev.type = SND_SEQ_EVENT_KEYPRESS ;  
      ev.data.note.note     = d0;
      ev.data.note.velocity = d1;
      break;

    case kCtlMdId:      
      ev.type = SND_SEQ_EVENT_CONTROLLER; 
      ev.data.control.param  = d0;
      ev.data.control.value  = d1;
      break;

    case kPgmMdId:      
      ev.type = SND_SEQ_EVENT_PGMCHANGE;  
      ev.data.control.param  = d0;
      ev.data.control.value  = d1;
      break; 

    case kChPresMdId:   
      ev.type = SND_SEQ_EVENT_CHANPRESS;  
      ev.data.control.param  = d0;
      ev.data.control.value  = d1;
      break;

    case kPbendMdId:    
      {
        int val = d0;
        val <<= 7;
        val += d1;
        val -= 8192;

        ev.type = SND_SEQ_EVENT_PITCHBEND;  
        ev.data.control.param  = 0;
        ev.data.control.value  = val;
      }
      break;

    default:
      rc = _cmMpErrMsg(kInvalidArgRC,0,"Cannot send an invalid MIDI status byte:0x%x.",status & 0xf0);
      goto errLabel;
  }

  ev.data.note.channel  = status & 0x0f;

  if((arc = snd_seq_event_output(p->h, &ev)) < 0 )
    rc = _cmMpErrMsg(kOpFailRC,arc,"MIDI event output failed.");

  if((arc = snd_seq_drain_output(p->h)) < 0 )
    rc = _cmMpErrMsg(kOpFailRC,arc,"MIDI event output drain failed.");

 errLabel:
  return rc;
}

cw::rc_t      cw::midi::device::sendData( handle_t h, unsigned devIdx, unsigned portIdx, const uint8_t* dataPtr, unsigned byteCnt )
{
  return cwLogError(kInvalidOpRC,"cmMpDeviceSendData() has not yet been implemented for ALSA.");
}

cw::rc_t    cw::midi::device::installCallback( handle_t h, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbDataPtr )
{
  rc_t      rc = kOkRC;
  unsigned  di;
  unsigned  dn = count(h);
  device_t* p  = _handleToPtr(h);

  for(di=0; di<dn; ++di)
    if( di==devIdx || devIdx == kInvalidIdx )
    {
      unsigned pi;
      unsigned pn = portCount(h,di,kInMpFl);

      for(pi=0; pi<pn; ++pi)
        if( pi==portIdx || portIdx == kInvalidIdx )
          if( parser::installCallback( p->devArray[di].iPortArray[pi].parserH, cbFunc, cbDataPtr ) != kOkRC )
            goto errLabel;
    }

 errLabel:
  return rc;
}

cw::rc_t    cw::midi::device::removeCallback(  handle_t h, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbDataPtr )
{
  rc_t      rc     = kOkRC;
  unsigned  di;
  unsigned  dn     = count(h);
  unsigned  remCnt = 0;
  device_t* p      = _handleToPtr(h);

  for(di=0; di<dn; ++di)
    if( di==devIdx || devIdx == kInvalidIdx )
    {
      unsigned pi;
      unsigned pn = portCount(h,di,kInMpFl);

      for(pi=0; pi<pn; ++pi)
        if( pi==portIdx || portIdx == kInvalidIdx )
          if( parser::hasCallback(p->devArray[di].iPortArray[pi].parserH, cbFunc, cbDataPtr ) )
          {
            if( parser::removeCallback( p->devArray[di].iPortArray[pi].parserH, cbFunc, cbDataPtr ) != kOkRC )
              goto errLabel;
            else
              ++remCnt;
          }
    }

  if( remCnt == 0 && dn > 0 )
    rc =  _cmMpErrMsg(kInvalidArgRC,0,"The callback was not found on any of the specified devices or ports.");

 errLabel:
  return rc;
}

bool        cw::midi::device::usesCallback(    handle_t h, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbDataPtr )
{
  unsigned  di;
  unsigned  dn = count(h);
  device_t* p  = _handleToPtr(h);

  for(di=0; di<dn; ++di)
    if( di==devIdx || devIdx == kInvalidIdx )
    {
      unsigned pi;
      unsigned pn = portCount(h,di,kInMpFl);

      for(pi=0; pi<pn; ++pi)
        if( pi==portIdx || portIdx == kInvalidIdx )
          if( parser::hasCallback(  p->devArray[di].iPortArray[pi].parserH, cbFunc, cbDataPtr ) )
            return true;
    }

  return false;
}



void cw::midi::device::report( handle_t h, textBuf::handle_t tbH )
{
  device_t* p = _handleToPtr(h);
  unsigned i,j;

  textBuf::print( tbH,"Buffer size bytes in:%i out:%i\n",snd_seq_get_input_buffer_size(p->h),snd_seq_get_output_buffer_size(p->h));

  for(i=0; i<p->devCnt; ++i)
  {
    const dev_t* d = p->devArray + i;

    textBuf::print( tbH,"%i : Device: '%s' \n",i,cwStringNullGuard(d->nameStr));

    if(d->iPortCnt > 0 )
      textBuf::print( tbH,"  Input:\n");

    for(j=0; j<d->iPortCnt; ++j)
      _cmMpReportPort(tbH,d->iPortArray+j);
    
    if(d->oPortCnt > 0 )
      textBuf::print( tbH,"  Output:\n");

    for(j=0; j<d->oPortCnt; ++j)
      _cmMpReportPort(tbH,d->oPortArray+j);
  }
}


