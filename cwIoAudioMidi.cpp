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
#include "cwAudioFile.h"

/*

TODO:
0. Check for leaks with valgrind 
1. Add audio recording
2. Add audio metering panel
3. Turn into a reusable component.


 */


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
        
        kAudioRecordCheckId,
        kAudioSecsId,
                
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
        
        { kPanelDivId,     kRecordCheckId,      "recordCheckId" },
        { kPanelDivId,     kAudioRecordCheckId, "audioRecordCheckId" },
        { kPanelDivId,     kStartBtnId,         "startBtnId" },
        { kPanelDivId,     kStopBtnId,          "stopBtnId" },
        { kPanelDivId,     kClearBtnId,         "clearBtnId" },
        { kPanelDivId,     kMsgCntId,           "msgCntId" },

        { kPanelDivId,     kAudioRecordCheckId, "audioRecordCheckId" },
        { kPanelDivId,     kAudioSecsId,        "audioSecsId" },
        
        { kPanelDivId,     kSaveBtnId,      "saveBtnId" },
        { kPanelDivId,     kOpenBtnId,      "openBtnId" },
        { kPanelDivId,     kFnStringId,     "filenameId" },
        
      };

      unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);

      typedef struct am_audio_str
      {
        time::spec_t         timestamp;
        unsigned             chCnt;
        unsigned             dspFrameCnt;
        struct am_audio_str* link;
        sample_t             audioBuf[]; // [[ch0:dspFramCnt][ch1:dspFrmCnt]] total: chCnt*dspFrameCnt samples
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

        am_audio_t*    audioBeg;       // first in a chain of am_audio_t audio buffers
        am_audio_t*    audioEnd;       // last in a chain of am_audio_t audio buffers
        
        am_audio_t*    audioFile;      // one large audio buffer holding the last loaded audio file

        double         audioSrate;
        bool           audioRecordFl;
        unsigned       audioSmpIdx;
        
        bool           recordFl;
        bool           startedFl;
        
        io::handle_t       ioH;
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


        return rc;
      }
            
      void _set_midi_msg_next_index( app_t* app, unsigned next_idx )
      {
        app->midiMsgArrayInIdx = next_idx;
        
        io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kMsgCntId), app->midiMsgArrayInIdx );

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

        if( app->audioRecordFl )
        {
        }
        else
        {
          app->audioSmpIdx = 0;
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

      rc_t _midi_read( app_t* app )
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
      
      rc_t _midi_write( app_t* app )
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
        io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kMsgCntId), app->midiMsgArrayInIdx );

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

      am_audio_t* _am_audio_alloc( unsigned dspFrameCnt, unsigned chCnt )
      {
        unsigned    sample_byte_cnt = chCnt * dspFrameCnt * sizeof(sample_t);        
        void*       vp              = mem::alloc<uint8_t>( sizeof(am_audio_t) + sample_byte_cnt );
        am_audio_t* a               = (am_audio_t*)vp;
        
        a->chCnt       = chCnt;
        a->dspFrameCnt = dspFrameCnt;
        
        return a;
      }

      am_audio_t* _am_audio_from_sample_index( app_t* app, unsigned sample_idx, unsigned& sample_offs_ref )
      {
        unsigned    n = 0;
        am_audio_t* a = app->audioBeg;

        if( app->audioBeg == nullptr )
          return nullptr;
        
        for(; a!=nullptr; a=a->link)
        {
          if( n < sample_idx )
          {
            sample_offs_ref = sample_idx - n;
            return a;
          }

          n += a->dspFrameCnt;
        }
          
        return nullptr;
      }

      void _audio_file_buffer( app_t* app, io::audio_msg_t& adst )
      {
        unsigned    sample_offs = 0;
        am_audio_t* a           = _am_audio_from_sample_index(app, app->audioSmpIdx, sample_offs );
        unsigned    copy_n_0      = std::min(a->dspFrameCnt - sample_offs, adst.dspFrameCnt );
        unsigned    chN         = std::min(a->chCnt, adst.oBufChCnt );

        for(unsigned i=0; i<chN; ++i)
          memcpy( adst.oBufArray[i], a->audioBuf + sample_offs, copy_n_0 * sizeof(sample_t));

        app->audioSmpIdx += copy_n_0;

        if( copy_n_0 < adst.dspFrameCnt )
        {
          a  = _am_audio_from_sample_index(app, app->audioSmpIdx, sample_offs );
        
          unsigned copy_n_1 = std::min(a->dspFrameCnt - sample_offs, adst.dspFrameCnt - copy_n_0 );
        
          for(unsigned i=0; i<chN; ++i)
            memcpy( adst.oBufArray[i] + copy_n_0, a->audioBuf + sample_offs, copy_n_1 * sizeof(sample_t));
        }
      }

      void _audio_record( app_t* app, const io::audio_msg_t& asrc )
      {
        am_audio_t* a  = _am_audio_alloc(asrc.dspFrameCnt,asrc.iBufChCnt);
        
        for(unsigned chIdx=0; chIdx<asrc.iBufChCnt; ++chIdx)
          memcpy(a->audioBuf + chIdx*asrc.dspFrameCnt, asrc.iBufArray[chIdx], asrc.dspFrameCnt * sizeof(sample_t));

        app->audioEnd->link = a;   // link the new audio record to the end of the audio sample buffer chain
        app->audioEnd       = a;   // make the new audio record the last ele. of the chain

        // if this is the first ele of the chain
        if( app->audioBeg == nullptr )
        {
          app->audioBeg   = a;
          app->audioSrate = asrc.srate; 
        }
      }

      void _audio_play( app_t* app, io::audio_msg_t& adst )
      {
        if( app->audioFile == nullptr )
          return;

        if( app->audioSmpIdx >= app->audioFile->dspFrameCnt )
          return;

        unsigned chCnt   = std::min( adst.oBufChCnt,   app->audioFile->chCnt );
        unsigned copy_n  = std::min( adst.dspFrameCnt, app->audioFile->dspFrameCnt - app->audioSmpIdx);
        unsigned extra_n = adst.dspFrameCnt - copy_n;
        unsigned i;
        
        for(i=0; i<chCnt; ++i)
        {
          memcpy(adst.oBufArray + i*adst.dspFrameCnt,          app->audioFile->audioBuf + i*app->audioFile->dspFrameCnt, copy_n  * sizeof(sample_t));
          memset(adst.oBufArray + i*adst.dspFrameCnt + copy_n, 0,                                                        extra_n * sizeof(sample_t));
        }
      }

      void _audio_through( app_t* app, io::audio_msg_t& m )
      {
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
      }
      
      rc_t _audio_write_as_wav( app_t* app )
      {
        rc_t                rc = kOkRC;
        audiofile::handle_t afH;
        char*               fn = nullptr;

        // if there is no audio to write
        if( app->audioBeg == nullptr )
          return rc;

        // form the filename
        if((fn = filesys::makeVersionedFn(  app->record_dir, app->record_fn, "wav", NULL )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"Unable to form versioned filename in '%s' with prefix: '%s' and extension: '%s'.",
                          cwStringNullGuard(app->record_dir),
                          cwStringNullGuard(app->record_fn),
                          cwStringNullGuard("wav"));
        }

        // create an audio file
        if((rc = audiofile::create( afH, fn, app->audioSrate, 16, app->audioBeg->chCnt )) != kOkRC )
        {
          cwLogError(rc,"Audio file create failed.");
          goto errLabel;
        }

        // write each buffer 
        for(am_audio_t* a=app->audioBeg; a!=nullptr; a=a->link)
        {
          float* chBufArray[ a->chCnt ];
          for(unsigned i=0; i<a->chCnt; ++i)
            chBufArray[i] = a->audioBuf + (i*a->dspFrameCnt);
            
          if((rc = writeFloat( afH, a->dspFrameCnt, a->chCnt, (const float**)chBufArray )) != kOkRC )
          {
            cwLogError(rc,"An error occurred while writing and audio buffer.");
            goto errLabel;
          }
        }
        
      errLabel:

        // close the audio file
        if((rc == audiofile::close(afH)) != kOkRC )
        {
          cwLogError(rc,"Audio file close failed.");
          goto errLabel;
        }
        
        mem::free(fn);
        return rc;
      }

      rc_t _audio_write_buffer_times( app_t* app )
      {
        rc_t           rc = kOkRC;
        char*          fn = nullptr;
        am_audio_t*    a0 = app->audioBeg;
        file::handle_t fH;
        
        // if there is no audio to write
        if( app->audioBeg == nullptr )
          return rc;

        // form the filename
        if((fn = filesys::makeVersionedFn(  app->record_dir, app->record_fn, "txt", NULL )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"Unable to form versioned filename in '%s' with prefix: '%s' and extension: '%s'.",
                          cwStringNullGuard(app->record_dir),
                          cwStringNullGuard(app->record_fn),
                          cwStringNullGuard("wav"));
        }

        // create the file
        if((rc = file::open(fH,fn,file::kWriteFl)) != kOkRC )
        {
          cwLogError(rc,"Create audio buffer time file failed.");
          goto errLabel;
        }

        file::print(fH,"{ [\n");
        
        // write each buffer
        for(am_audio_t* a=app->audioBeg; a!=nullptr; a=a->link)
        {
          unsigned elapsed_us = time::elapsedMicros( a0->timestamp, a->timestamp );
          file::printf(fH,"{ elapsed_us:%i chCnt:%i frameCnt:%i }\n", elapsed_us, a->chCnt, a->dspFrameCnt );
          a0 = a;
        }

        file::print(fH,"] }\n");

        // close the file
        if((rc = file::close(fH)) != kOkRC )
        {
          cwLogError(rc,"Close the audio buffer time file.");
          goto errLabel;
        }

      errLabel:
        mem::release(fn);
        
        return rc;        
      }

      rc_t _audio_read( app_t* app )
      {
        rc_t                 rc = kOkRC;
        char*                fn = nullptr;
        filesys::pathPart_t* pp = nullptr;
        audiofile::handle_t  afH;
        audiofile::info_t    af_info;
        
        
        if((fn = filesys::makeFn(  app->record_dir, app->filename, NULL, NULL )) != nullptr )
        {
          rc = cwLogError(kOpFailRC,"Unable to form the audio file:%s",fn);
          goto errLabel;
        }

        if((pp = filesys::pathParts(fn)) != nullptr )
        {
          rc = cwLogError(kOpFailRC,"Unable to parse audio file name:%s",fn);
          goto errLabel;
        }
        
        mem::release(fn);
        
        if((fn = filesys::makeFn( app->record_dir, pp->fnStr, "wav", NULL )) != nullptr )
        {
          rc = cwLogError(kOpFailRC,"Unable form audio file wav name:%s",fn);
          goto errLabel;          
        }
        
        if((rc = audiofile::open(afH, fn, &af_info)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"Audio file '%s' open failed.",fn);
          goto errLabel;         
        }
        
        if((app->audioFile = _am_audio_alloc(af_info.frameCnt,af_info.chCnt)) != nullptr )
        {
          rc = cwLogError(kOpFailRC,"Allocate audio buffer (%i samples) failed.",af_info.frameCnt*af_info.chCnt);
          goto errLabel;
        }
        else
        {
          unsigned audioFrameCnt = 0;
          float* chArray[ af_info.chCnt ];
          
          for(unsigned i=0; i<af_info.chCnt; ++i)
            chArray[i] = app->audioFile->audioBuf + (i*af_info.frameCnt);
          
          if((rc = audiofile::readFloat(afH, af_info.frameCnt, 0, af_info.chCnt, chArray, &audioFrameCnt)) != kOkRC )
          {
            rc = cwLogError(kOpFailRC,"Audio file read failed.");
            goto errLabel;
          }

          double audioSecs = (double)af_info.frameCnt / af_info.srate;
          io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kAudioSecsId), audioSecs );

        }
        
      errLabel:
        if((rc = audiofile::close(afH)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"Audio file close failed.");
          goto errLabel;
        }
        
        mem::release(pp);
        mem::release(fn);
        return rc;
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
            _midi_write(app);
            _audio_write_as_wav(app);
            _audio_write_buffer_times(app);
            break;

          case kOpenBtnId:
            printf("open btn\n");
            _midi_read(app);
            _audio_read(app);
            break;

          case kRecordCheckId:
            cwLogInfo("Record:%i",m.value->u.b);
            app->recordFl = m.value->u.b;
            break;

          case kAudioRecordCheckId:
            cwLogInfo("Audio Record:%i",m.value->u.b);
            app->audioRecordFl = m.value->u.b;
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
                  am->ch        = mm->ch;
                  am->status    = mm->status;
                  am->d0        = mm->d0;
                  am->d1        = mm->d1;

                  app->midiMsgArrayInIdx += 1;

                  // send msg count
                  io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kMsgCntId), app->midiMsgArrayInIdx );                  
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
      
      rc_t audioCb( app_t* app, audio_msg_t& m )
      {
        rc_t rc = kOkRC;

        if( app->startedFl )
        {
          if( app->audioRecordFl )
          {
            if( m.iBufChCnt > 0 )
              _audio_record(app,m);
            
            if( m.oBufChCnt > 0 )
              _audio_play(app,m);
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
    }
  }
}


cw::rc_t cw::io::audio_midi::main( const object_t* cfg )
{

  rc_t rc;
  app_t app = {};
  bool asyncFl = true;
  
  if((rc = _parseCfg(&app,cfg)) != kOkRC )
    goto errLabel;

  // create the io framework instance
  if((rc = create(app.ioH,cfg,ioCb,&app,mapA,mapN)) != kOkRC )
    return rc;

  // create the MIDI playback timer
  if((rc = timerCreate( app.ioH, "am_timer", kAmMidiTimerId, app.midi_timer_period_micro_sec, asyncFl)) != kOkRC )
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
    const unsigned wsTimeOutMs = 50;
    time::spec_t t0 = time::current_time();
    
    exec(app.ioH,wsTimeOutMs);
    
    time::spec_t t1  = time::current_time();
    unsigned     dMs = time::elapsedMs(t0,t1);
    
    if( dMs < wsTimeOutMs ) 
      sleepMs(wsTimeOutMs-dMs);
  }

 errLabel:
  _free(app);
  destroy(app.ioH);
  printf("Audio-MIDI Done.\n");
  return rc;
  
}
