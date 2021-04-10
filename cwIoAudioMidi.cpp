#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwFile.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoAudioMidi.h"

namespace cw
{
  namespace io
  {
    namespace audio_midi
    {

      // Application Id's for UI elements
      enum
      {
        // Resource Based elements
        kPanelDivId = 1000,
        kQuitBtnId,
        kIoReportBtnId,
        kReportBtnId,
        kRecordCheckId,
        kStartBtnId,
        kStopBtnId,
        kClearBtnId,
        kMsgCntId,
        kSaveBtnId,
        kOpenBtnId,
        kFnStringId
      };

      enum
      {
        kAmMidiTimerId
      };

    
      // Application Id's for the resource based UI elements.
      ui::appIdMap_t mapA[] =
      {
        { ui::kRootAppId,  kPanelDivId,     "panelDivId" },
        { kPanelDivId,     kQuitBtnId,      "quitBtnId" },
        { kPanelDivId,     kIoReportBtnId,  "ioReportBtnId" },
        { kPanelDivId,     kReportBtnId,    "reportBtnId" },
        
        { kPanelDivId,     kRecordCheckId,  "recordCheckId" },
        { kPanelDivId,     kStartBtnId,     "startBtnId" },
        { kPanelDivId,     kStopBtnId,      "stopBtnId" },
        { kPanelDivId,     kClearBtnId,     "clearBtnId" },
        { kPanelDivId,     kMsgCntId,       "msgCntId" },
        
        { kPanelDivId,     kSaveBtnId,      "saveBtnId" },
        { kPanelDivId,     kOpenBtnId,      "openBtnId" },
        { kPanelDivId,     kFnStringId,     "filenameId" },
        
      };

      unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);

      typedef struct am_audio_str
      {
        time::spec_t timestamp;
        unsigned     chCnt;
        
        
      } am_audio_t;

      typedef struct am_midi_msg_str
      {
        unsigned      devIdx;
        unsigned      portIdx;
        time::spec_t  timestamp;
        uint8_t       ch;
        uint8_t       status;
        uint8_t       d0;
        uint8_t       d1;

        unsigned microsec;
        
      } am_midi_msg_t;

      typedef struct app_str
      {
        const char*    record_dir;
        const char*    record_fn;
        const char*    record_fn_ext;
        am_midi_msg_t* midiMsgArray;
        unsigned       midiMsgArrayN;
        unsigned       midiMsgArrayInIdx;
        unsigned       midiMsgArrayOutIdx;
        unsigned       midi_timer_period_micro_sec;

        const char*    midiOutDevLabel;
        const char*    midiOutPortLabel;
        unsigned       midiOutDevIdx;
        unsigned       midiOutPortIdx;
        
        time::spec_t   play_time;        
        char*          filename;
        
        time::spec_t   start_time;        
        unsigned       midiFilterCnt;
        
        bool           recordFl;
        bool           startedFl;
        
        handle_t       ioH;
      } app_t;
      
      rc_t _parseCfg(app_t* app, const object_t* cfg )
      {
        rc_t rc = kOkRC;
        
        if((rc = cfg->getv(
                        "record_dir",                  app->record_dir,
                        "record_fn",                   app->record_fn,
                        "record_fn_ext",               app->record_fn_ext,
                        "max_midi_msg_count",          app->midiMsgArrayN,
                        "midi_timer_period_micro_sec", app->midi_timer_period_micro_sec,
                        "midi_out_device",             app->midiOutDevLabel,
                        "midi_out_port",               app->midiOutPortLabel)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"Audio MIDI app configuration parse failed.");
        }

        // allocate the MIDI msg buffer
        app->midiMsgArray = mem::allocZ<am_midi_msg_t>( app->midiMsgArrayN );

        // verify that the output directory exists
        filesys::makeDir(app->record_dir);

        return rc;
      }

      void _free( app_t& app )
      {
        mem::release(app.midiMsgArray);
        mem::release(app.filename);
      }

      rc_t  _resolve_midi_device_port_index( app_t* app )
      {
        rc_t rc = kOkRC;
        
        if((app->midiOutDevIdx = io::midiDeviceIndex(app->ioH,app->midiOutDevLabel)) == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The MIDI output device: '%s' was not found.", cwStringNullGuard(app->midiOutDevLabel) );
        }
        
        if((app->midiOutPortIdx = io::midiDevicePortIndex(app->ioH,app->midiOutDevIdx,false,app->midiOutPortLabel)) == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The MIDI output port: '%s' was not found.", cwStringNullGuard(app->midiOutPortLabel) );
        }

        printf("MIDI DEV: %i PORT:%i\n",app->midiOutDevIdx,app->midiOutPortIdx);

        return rc;
      }
            
      void _set_midi_msg_next_index( app_t* app, unsigned next_idx )
      {
        app->midiMsgArrayInIdx = next_idx;
        
        io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kMsgCntId), app->midiMsgArrayInIdx );

      }

      void _set_midi_msg_next_play_index(app_t* app, unsigned next_idx)
      {
        app->midiMsgArrayOutIdx = next_idx;
      }


      void _on_start_btn( app_t* app )
      {
        app->startedFl = true;

        time::get(app->start_time);
        
        if( app->recordFl )
        {
          app->midiFilterCnt = 0;
                                 
          _set_midi_msg_next_index(app, 0 );
        }
        else
        {
          _set_midi_msg_next_play_index(app,0);
          io::timerStart( app->ioH, io::timerIdToIndex(app->ioH, kAmMidiTimerId) );
          time::get(app->play_time);
        }        
      }

      void _on_stop_btn( app_t* app )
      {
        app->startedFl = false;

        time::spec_t t1;
        time::get(t1);

        if( app->recordFl )
        {

          // set the 'microsec' value for each MIDI msg
          for(unsigned i=0; i<app->midiMsgArrayInIdx; ++i)
          {
            app->midiMsgArray[i].microsec = time::elapsedMicros(app->midiMsgArray[0].timestamp,app->midiMsgArray[i].timestamp);
          }
          
          cwLogInfo("MIDI messages recorded: %i filtered: %i\n",app->midiMsgArrayInIdx, app->midiFilterCnt );
          
        }
        else
        {
          io::timerStop( app->ioH, io::timerIdToIndex(app->ioH, kAmMidiTimerId) );
        }

        cwLogInfo("Runtime: %5.2f seconds.", time::elapsedMs(app->start_time,t1)/1000.0 );
      }

      rc_t _read_midi( app_t* app )
      {
        rc_t  rc = kOkRC;
        char* fn = filesys::makeFn(  app->record_dir, app->filename, NULL, NULL );
        file::handle_t fH;
        unsigned n = 0;
        
        if((rc = file::open(fH,fn,file::kReadFl)) != kOkRC )
        {
          rc = cwLogError(kOpenFailRC,"Unable to locate the file: '%s'.", fn );
          goto errLabel;
        }

        if((rc = file::read(fH,n)) != kOkRC )
        {
          rc = cwLogError(kReadFailRC,"Header read failed on Audio-MIDI file: '%s'.", fn );
          goto errLabel;
        }

        if( n > app->midiMsgArrayN )
        {
          cwLogWarning("The count of message in Audio-MIDI file '%s' reduced from %i to %i.", fn, n, app->midiMsgArrayN );
          n = app->midiMsgArrayN;
        }

        if((rc = file::read(fH,app->midiMsgArray,n*sizeof(am_midi_msg_t))) != kOkRC )
        {
          rc = cwLogError(kReadFailRC,"Data read failed on Audio-MIDI file: '%s'.", fn );
          goto errLabel;
        }

        _set_midi_msg_next_index(app, n );

        cwLogInfo("Read %i from '%s'.",n,fn);

      errLabel:
        mem::release(fn);
        
        return rc;        
      }
      
      rc_t _write_midi( app_t* app )
      {
        rc_t           rc = kOkRC;
        char*          fn = nullptr;
        file::handle_t fH;

        if( app->midiMsgArrayInIdx == 0 )
        {
          cwLogWarning("Nothing to write.");
          return rc;
        }

        // form the filename
        if((fn = filesys::makeVersionedFn(  app->record_dir, app->record_fn, app->record_fn_ext, NULL )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"Unable to form versioned filename in '%s' with prefix: '%s' and extension: '%s'.",
                          cwStringNullGuard(app->record_dir),
                          cwStringNullGuard(app->record_fn),
                          cwStringNullGuard(app->record_fn_ext));
        }

        // open the file
        if((rc = file::open(fH,fn,file::kWriteFl)) != kOkRC )
        {
          rc = cwLogError(kOpenFailRC,"Unable to create the file '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }

        // write the file header
        if((rc = write(fH,app->midiMsgArrayInIdx)) != kOkRC )
        {
          rc = cwLogError(kWriteFailRC,"Header write to '%s' failed.",cwStringNullGuard(fn));
          goto errLabel;          
        }

        // write the file data
        if((rc = write(fH,app->midiMsgArray,sizeof(am_midi_msg_t)*app->midiMsgArrayInIdx)) != kOkRC )
        {
          rc = cwLogError(kWriteFailRC,"Data write to '%s' failed.",cwStringNullGuard(fn));
          goto errLabel;                    
        }

        // update UI msg count
        io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kMsgCntId), app->midiMsgArrayInIdx );

        file::close(fH);

        cwLogInfo("Saved %i events to '%s'.", app->midiMsgArrayInIdx, fn );
        
      errLabel:
        mem::release(fn);
        return rc;
      }

      void _print_midi_msg( const am_midi_msg_t* mm )
      {
        printf("%i %i : %10i : %2i 0x%02x 0x%02x 0x%02x\n", mm->devIdx, mm->portIdx, mm->microsec, mm->ch, mm->status, mm->d0, mm->d1 );
      }
      
      void _report_midi( app_t* app )
      {
        for(unsigned i=0; i<app->midiMsgArrayInIdx; ++i)
        {
          am_midi_msg_t* mm = app->midiMsgArray + i;
          _print_midi_msg(mm);
        }
      }

      rc_t _onUiInit(app_t* app, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;
      
        return rc;
      }

      rc_t _onUiValue(app_t* app, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;

        switch( m.appId )
        {
          case kQuitBtnId:
            io::stop( app->ioH );
            break;

          case kIoReportBtnId:
            io::report( app->ioH );
            break;
            
          case kReportBtnId:
            _report_midi(app);
            break;

          case kSaveBtnId:
            _write_midi(app);
            break;

          case kOpenBtnId:
            printf("open btn\n");
            _read_midi(app);
            break;

          case kRecordCheckId:
            cwLogInfo("Record:%i",m.value->u.b);
            app->recordFl = m.value->u.b;
            break;
            
          case kStartBtnId:
            cwLogInfo("Start");
            _on_start_btn(app);
            break;
            
          case kStopBtnId:
            cwLogInfo("Stop");
            _on_stop_btn(app);
            break;

          case kClearBtnId:
            cwLogInfo("Clear");
            _set_midi_msg_next_index(app, 0 );
            break;
            
          case kFnStringId:
            mem::release(app->filename);
            app->filename = mem::duplStr(m.value->u.s);
            printf("filename:%s\n",app->filename);
            break;
        }

        return rc;
      }
    
      rc_t _onUiEcho(app_t* app, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;
        return rc;
      }
    
      rc_t uiCb( app_t* app, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;
      
        switch( m.opId )
        {
          case ui::kConnectOpId:
            cwLogInfo("UI Connected: wsSessId:%i.",m.wsSessId);
            break;
          
          case ui::kDisconnectOpId:
            cwLogInfo("UI Disconnected: wsSessId:%i.",m.wsSessId);          
            break;
          
          case ui::kInitOpId:
            _onUiInit(app,m);
            break;

          case ui::kValueOpId:
            _onUiValue( app, m );
            break;

          case ui::kEchoOpId:
            _onUiEcho( app, m );
            break;

          case ui::kIdleOpId:
            break;
          
          case ui::kInvalidOpId:
            // fall through
          default:
            assert(0);
            break;
        
        }

        return rc;
      }

            
      rc_t timerCb(app_t* app, timer_msg_t& m)
      {
        rc_t rc = kOkRC;

        // if the MIDI player is started and in 'play' mode and msg remain to be played
        if( app->startedFl && (app->recordFl==false) && (app->midiMsgArrayOutIdx < app->midiMsgArrayInIdx))
        {
          time::spec_t t;
          time::get(t);

          unsigned cur_time_us = time::elapsedMicros(app->play_time,t);

          while( app->midiMsgArray[ app->midiMsgArrayOutIdx ].microsec <= cur_time_us )
          {

            am_midi_msg_t* mm = app->midiMsgArray + app->midiMsgArrayOutIdx;

            //_print_midi_msg(mm);

            io::midiDeviceSend( app->ioH, app->midiOutDevIdx, app->midiOutPortIdx, mm->status + mm->ch, mm->d0, mm->d1 );
            
            _set_midi_msg_next_play_index(app, app->midiMsgArrayOutIdx+1 );

            // if all MIDI messages have been played
            if( app->midiMsgArrayOutIdx >= app->midiMsgArrayInIdx )
            {
              _on_stop_btn(app);
              break;
            }
          }
        }
        
        return rc;
      }

      bool _midi_filter( const midi::msg_t* mm )
      {
        //bool drop_fl = (mm->status & 0xf0) == midi::kCtlMdId && (64 <= mm->d0) && (mm->d0 <= 67) && (mm->d1 < 25);
        //return drop_fl;
        return false;
      }

      rc_t midiCb( app_t* app, const midi_msg_t& m )
      {
        rc_t rc = kOkRC;
        const midi::packet_t* pkt = m.pkt;

        // for each midi msg
        for(unsigned j=0; j<pkt->msgCnt; ++j)
        {
          
          // if this is a sys-ex msg
          if( pkt->msgArray == NULL )
          {
            // this is a sys ex msg use: pkt->sysExMsg[j]
          }
          else // this is a triple
          {
            if( app->recordFl && app->startedFl )
            {

              // verify that space exists in the record buffer
              if( app->midiMsgArrayInIdx >= app->midiMsgArrayN )
              {
                _on_stop_btn(app);
                rc = cwLogError(kBufTooSmallRC,"MIDI message record buffer is full. % messages.",app->midiMsgArrayN);
                goto errLabel;
              }
              else
              {
                // copy the msg into the record buffer
                am_midi_msg_t* am = app->midiMsgArray + app->midiMsgArrayInIdx;
                midi::msg_t*   mm = pkt->msgArray + j;

                if( _midi_filter(mm) )
                {
                  app->midiFilterCnt++;
                }
                else
                {
                  am->devIdx    = pkt->devIdx;
                  am->portIdx   = pkt->portIdx;
                  am->timestamp = mm->timeStamp;
                  am->ch        = mm->status & 0x0f;
                  am->status    = mm->status & 0xf0;
                  am->d0        = mm->d0;
                  am->d1        = mm->d1;

                  app->midiMsgArrayInIdx += 1;

                  // send msg count
                  io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kMsgCntId), app->midiMsgArrayInIdx );                  
                }
              }
            }
          }

          /*
          if( pkt->msgArray == NULL )
            printf("io midi cb: 0x%x ",pkt->sysExMsg[j]);
          else
          {
            if( !_midi_filter(pkt->msgArray + j) )
              printf("io midi cb: %ld %ld 0x%x %i %i\n", pkt->msgArray[j].timeStamp.tv_sec, pkt->msgArray[j].timeStamp.tv_nsec, pkt->msgArray[j].status, pkt->msgArray[j].d0, pkt->msgArray[j].d1);                    
          }
          */
        }

      errLabel:
        return rc;
      }
      
      rc_t audioCb( app_t* app, const audio_msg_t& m )
      {
        rc_t rc = kOkRC;

        unsigned chN     = std::min(m.iBufChCnt,m.oBufChCnt);
        unsigned byteCnt = m.dspFrameCnt * sizeof(sample_t);

        // Copy the input to the output
        for(unsigned i=0; i<chN; ++i)
          if( m.oBufArray[i] != NULL )
          {      
            // the input channel is not disabled
            if( m.iBufArray[i] != NULL )
            {
              for(unsigned j=0; j<m.dspFrameCnt; ++j )
                m.oBufArray[i][j] =  m.iBufArray[i][j];
            }
            else
            {
              // the input channel is disabled but the output is not - so fill the output with zeros
              memset(m.oBufArray[i], 0, byteCnt);
            }
          }
      
        return rc;      
      }


      // The main application callback
      rc_t ioCb( void* arg, const msg_t* m )
      {
        rc_t rc = kOkRC;
        app_t* app = reinterpret_cast<app_t*>(arg);

        switch( m->tid )
        {
          case kTimerTId:
            if( m->u.timer != nullptr )
              rc = timerCb(app,*m->u.timer);
            break;
            
          case kSerialTId:
            break;
          
          case kMidiTId:
            if( m->u.midi != nullptr )
              rc = midiCb(app,*m->u.midi);
            break;
          
          case kAudioTId:
            if( m->u.audio != nullptr )
              rc = audioCb(app,*m->u.audio);
            break;

          case kAudioMeterTId:
            break;
          
          case kSockTId:
            break;
          
          case kWebSockTId:
            break;
          
          case kUiTId:
            rc = uiCb(app,m->u.ui);
            break;

          default:
            assert(0);
        
        }

        return rc;
      }
    
      void _report( handle_t h )
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

      
    }
  }
}

cw::rc_t cw::io::audio_midi::main( const object_t* cfg )
{

  rc_t rc;
  app_t app = {};
  
  if((rc = _parseCfg(&app,cfg)) != kOkRC )
    goto errLabel;

  // create the io framework instance
  if((rc = create(app.ioH,cfg,ioCb,&app,mapA,mapN)) != kOkRC )
    return rc;

  // create the MIDI playback timer
  if((rc = timerCreate( app.ioH, "am_timer", kAmMidiTimerId, app.midi_timer_period_micro_sec)) != kOkRC )
  {
    cwLogError(rc,"Audio-MIDI timer create failed.");
    goto errLabel;
  }
  
  //report(app.ioH);

  // start the io framework instance
  if((rc = start(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio-MIDI app start failed.");
    goto errLabel;
    
  }
  else
  {

    // resolve the MIDI out dev/port indexes from the MIDI out dev/port labels
    rc_t rc0 = _resolve_midi_device_port_index(&app);

    
    // resolve the audio group index from the lavel
    rc_t rc1 = kOkRC;
    unsigned devIdx;
    
    if( (devIdx = audioDeviceLabelToIndex(app.ioH, "main")) == kInvalidIdx )
      rc1 = cwLogError(kOpFailRC, "Unable to locate the requested audio device.");

    if(rcSelect(rc0,rc1) != kOkRC )
      goto errLabel;
  }
  
  // execute the io framework
  while( !isShuttingDown(app.ioH))
  {
    exec(app.ioH);
    sleepMs(50);
  }

 errLabel:
  _free(app);
  destroy(app.ioH);
  printf("Audio-MIDI Done.\n");
  return rc;
  
}
