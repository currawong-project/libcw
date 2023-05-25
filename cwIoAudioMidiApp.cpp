#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwText.h"
#include "cwFileSys.h"
#include "cwFile.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwScoreFollowerPerf.h"
#include "cwIoAudioMidiApp.h"
#include "cwIoMidiRecordPlay.h"
#include "cwIoAudioRecordPlay.h"

namespace cw
{
  namespace audio_midi_app
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
      
      kMidiThruCheckId,
      kCurMidiEvtCntId,
      kTotalMidiEvtCntId,
      kMidiMuteCheckId,
      
      kCurAudioSecsId,
      kTotalAudioSecsId,
      kAudioMuteCheckId,
                
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
        
      { kPanelDivId,     kRecordCheckId,     "recordCheckId" },
      { kPanelDivId,     kStartBtnId,        "startBtnId" },
      { kPanelDivId,     kStopBtnId,         "stopBtnId" },
      { kPanelDivId,     kClearBtnId,        "clearBtnId" },
      
      { kPanelDivId,     kMidiThruCheckId,   "midiThruCheckId" },
      { kPanelDivId,     kCurMidiEvtCntId,   "curMidiEvtCntId" },      
      { kPanelDivId,     kTotalMidiEvtCntId, "totalMidiEvtCntId" },
      { kPanelDivId,     kMidiMuteCheckId,   "midiMuteCheckId" },
      
      { kPanelDivId,     kCurAudioSecsId,    "curAudioSecsId" },
      { kPanelDivId,     kTotalAudioSecsId,  "totalAudioSecsId" },
      { kPanelDivId,     kAudioMuteCheckId,  "audioMuteCheckId" },
        
      { kPanelDivId,     kSaveBtnId,      "saveBtnId" },
      { kPanelDivId,     kOpenBtnId,      "openBtnId" },
      { kPanelDivId,     kFnStringId,     "filenameId" },
        
    };

    unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);
    
    typedef struct app_str
    {
      io::handle_t                ioH;
      
      const char*                 record_dir;
      const char*                 record_folder;
      const char*                 record_fn_ext;
      char*                       directory;
      const char*                 velTableFname;
      
      midi_record_play::handle_t  mrpH;
      audio_record_play::handle_t arpH;
      
      const object_t* midi_play_record_cfg;

    } app_t;

    rc_t _parseCfg(app_t* app, const object_t* cfg )
    {
      rc_t rc = kOkRC;
        
      if((rc = cfg->getv(
                         "record_dir",                  app->record_dir,
                         "record_folder",               app->record_folder,
                         "record_fn_ext",               app->record_fn_ext,
                         "midi_play_record",            app->midi_play_record_cfg)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Audio MIDI app configuration parse failed.");
        goto errLabel;
      }


      if((rc = cfg->getv_opt( "vel_table_fname", app->velTableFname)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Audio MIDI app optional configuration parse failed.");
        goto errLabel;
      }

      // verify that the output directory exists
      if((rc = filesys::makeDir(app->record_dir)) != kOkRC )
        rc = cwLogError(rc,"Unable to create the base output directory:%s.",cwStringNullGuard(app->record_dir));
      
      
      
         errLabel:
      return rc;
    }

    rc_t _free( app_t& app )      
    {
      mem::release(app.directory);
      return kOkRC;
    }

    char* _form_versioned_directory(app_t* app)
    {
      char* dir = nullptr;
      
      for(unsigned version_numb=0; true; ++version_numb)
      {
        unsigned n = textLength(app->record_folder) + 32;
        char     folder[n+1];

        snprintf(folder,n,"%s_%i",app->record_folder,version_numb);
        
        if((dir = filesys::makeFn(app->record_dir,folder, NULL, NULL)) == nullptr )
        {
          cwLogError(kOpFailRC,"Unable to form a versioned directory from:'%s'",cwStringNullGuard(app->record_dir));
          return nullptr;
        }

        if( !filesys::isDir(dir) )
          break;

        mem::release(dir);
      }
      
      return dir;
    }

    
    
    rc_t _on_ui_save( app_t* app )
    {
      rc_t  rc0 = kOkRC;
      rc_t  rc1 = kOkRC;
      char* dir = nullptr;
      char* fn  = nullptr;
      
      if((dir = _form_versioned_directory(app)) == nullptr )
        return cwLogError(kOpFailRC,"Unable to form the versioned directory string.");

      if( !filesys::isDir(dir) )
        if((rc0 = filesys::makeDir(dir)) != kOkRC )
        {
          rc0 = cwLogError(rc0,"Attempt to create directory: '%s' failed.", cwStringNullGuard(dir));
          goto errLabel;
        }

      if((fn = filesys::makeFn(dir,"midi","am",nullptr)) != nullptr )
      {        
        if((rc0 = midi_record_play::save( app->mrpH, fn )) != kOkRC )
          rc0 = cwLogError(rc0,"MIDI file '%s' save failed.",fn);

        mem::release(fn);
      }

      if((fn = filesys::makeFn(dir,"midi","csv",nullptr)) != nullptr )
      {        
        if((rc0 = midi_record_play::save_csv( app->mrpH, fn )) != kOkRC )
          rc0 = cwLogError(rc0,"MIDI CSV file '%s' save failed.",fn);

        mem::release(fn);
      }

      
      if((fn = filesys::makeFn(dir,"audio","wav",nullptr)) != nullptr )
      {        
        if((rc1 = audio_record_play::save( app->arpH, fn )) != kOkRC )
          rc1 = cwLogError(rc1,"Audio file '%s' save failed.",fn);

        mem::release(fn);
      }

    errLabel:
      mem::release(dir);
      
      return rcSelect(rc0,rc1);
    }

    rc_t _on_ui_open( app_t* app )
    {
      rc_t  rc = kOkRC;
      char* fn = nullptr;
      
      if((fn = filesys::makeFn(app->record_dir,"midi","am",app->directory,NULL)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Unable to form the MIDI file name.");
        goto errLabel;
      }

      if((rc = midi_record_play::open(app->mrpH,fn)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file '%s' open failed.",cwStringNullGuard(fn));
        goto errLabel;
      }

      mem::release(fn);

      if((fn = filesys::makeFn(app->record_dir,"audio","wav",app->directory,NULL)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Unable to form the Audio file name.");
        goto errLabel;
      }

      if((rc = audio_record_play::open(app->arpH,fn)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio file '%s' open failed.",cwStringNullGuard(fn));
        goto errLabel;
      }

    errLabel:
      mem::release(fn);

      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), midi_record_play::event_count(app->mrpH) );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurAudioSecsId),    audio_record_play::current_loc_seconds(app->arpH) );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalAudioSecsId),  audio_record_play::duration_seconds(app->arpH) );
      

      return rc;
    }

    rc_t _on_ui_start( app_t* app )
    {
      rc_t rc;
      if((rc = midi_record_play::start(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI start failed.");
        goto errLabel;
      }

      if((rc = audio_record_play::start(app->arpH)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio start failed.");
        goto errLabel;
      }

      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), midi_record_play::event_index(app->mrpH) );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurAudioSecsId),  audio_record_play::current_loc_seconds(app->arpH) );
      
    errLabel:
      return rc;
    }

    rc_t _on_ui_stop( app_t* app )
    {
      rc_t rc;
      if((rc = midi_record_play::stop(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI start failed.");
        goto errLabel;
      }

      if((rc = audio_record_play::stop(app->arpH)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio start failed.");
        goto errLabel;
      }
      
    errLabel:
      return rc;
    }

    bool _get_record_state( app_t* app )
    {
      bool midi_record_fl  = midi_record_play::record_state(app->mrpH);
      bool audio_record_fl = audio_record_play::record_state(app->arpH);

      if( midi_record_fl != audio_record_fl )
      {
        cwLogError(kInvalidStateRC,"Inconsistent record state.");        
      }

      return midi_record_fl || audio_record_fl;
    }

    rc_t _set_record_state( app_t* app, bool record_fl )
    {
      rc_t rc0,rc1;
      
      if((rc0 = midi_record_play::set_record_state(app->mrpH,record_fl)) != kOkRC )
        rc0 = cwLogError(rc0,"%s MIDI record state failed.",record_fl ? "Enable" : "Disable" );
      
      if((rc1 = audio_record_play::set_record_state(app->arpH,record_fl)) != kOkRC )
        rc1 = cwLogError(rc1,"%s audio record state failed.",record_fl ? "Enable" : "Disable" );

      return rcSelect(rc0,rc1);
    }

    rc_t _set_midi_thru_state( app_t* app, bool thru_fl )
    {
      rc_t rc;
      
      if((rc = midi_record_play::set_thru_state(app->mrpH,thru_fl)) != kOkRC )
        rc = cwLogError(rc,"%s MIDI thru state failed.",thru_fl ? "Enable" : "Disable" );
      
      return rc;
    }

    
    rc_t _on_ui_clear( app_t* app )
    {
      rc_t rc;
      if((rc = midi_record_play::clear(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI clear failed.");
        goto errLabel;
      }

      if((rc = audio_record_play::stop(app->arpH)) != kOkRC )
      {
        rc = cwLogError(rc,"Audio clear failed.");
        goto errLabel;
      }

      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), midi_record_play::event_index(app->mrpH) );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurAudioSecsId),  audio_record_play::current_loc_seconds(app->arpH) );
      
      
    errLabel:
      return rc;
      
    }
    

    rc_t _onUiInit(app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;
      
      return rc;
    }

    rc_t _onUiValue(app_t* app, const io::ui_msg_t& m )
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
          report( app->mrpH );
          break;

        case kSaveBtnId:
          _on_ui_save(app);
          break;

        case kOpenBtnId:
          _on_ui_open(app);
          break;

        case kRecordCheckId:
          cwLogInfo("Record:%i",m.value->u.b);        
          _set_record_state(app, m.value->u.b);
          break;
        
        case kMidiThruCheckId:
          cwLogInfo("MIDI thru:%i",m.value->u.b);        
          _set_midi_thru_state(app, m.value->u.b);
          break;

        case kMidiMuteCheckId:
          midi_record_play::set_mute_state(app->mrpH,m.value->u.b);
          break;

        case kAudioMuteCheckId:
          audio_record_play::set_mute_state(app->arpH,m.value->u.b);
          break;
            
        case kStartBtnId:
          _on_ui_start(app);
          break;
            
        case kStopBtnId:
          _on_ui_stop(app);
          break;

        case kClearBtnId:
          _on_ui_clear(app);
          break;
            
        case kFnStringId:
          mem::release(app->directory);
          app->directory = mem::duplStr(m.value->u.s);
          printf("filename:%s\n",app->directory);
          break;
      }

      return rc;
    }
    
    rc_t _onUiEcho(app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;
      return rc;
    }
    
    rc_t _ui_callback( app_t* app, const io::ui_msg_t& m )
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

    // The main application callback
    rc_t _io_callback( void* arg, const io::msg_t* m )
    {
      rc_t rc = kOkRC;
      app_t* app = reinterpret_cast<app_t*>(arg);


      if( app->mrpH.isValid() )
      {
        midi_record_play::exec( app->mrpH, *m );
        if( midi_record_play::is_started(app->mrpH) )
          io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), midi_record_play::event_index(app->mrpH) );
      }
      
      if( app->arpH.isValid() )
      {
        audio_record_play::exec( app->arpH, *m );
        if( audio_record_play::is_started(app->arpH) )
          io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurAudioSecsId), audio_record_play::current_loc_seconds(app->arpH) );
      }

      switch( m->tid )
      {
      case io::kTimerTId:
        break;
            
      case io::kSerialTId:
        break;
          
      case io::kMidiTId:
        break;
          
      case io::kAudioTId:        
        break;

      case io::kAudioMeterTId:
        break;
          
      case io::kSockTId:
        break;
          
      case io::kWebSockTId:
        break;
          
      case io::kUiTId:
        rc = _ui_callback(app,m->u.ui);
        break;
        
      case io::kExecTId:
        break;

      default:
        assert(0);
        
      }

      return rc;
    }          
    
    
  }
}


cw::rc_t cw::audio_midi_app::main( const object_t* cfg )
{
  rc_t rc;
  app_t app = {};

  // Parse the configuration
  if((rc = _parseCfg(&app,cfg)) != kOkRC )
    goto errLabel;

  // create the io framework instance
  if((rc = io::create(app.ioH,cfg,_io_callback,&app,mapA,mapN)) != kOkRC )
    return rc;

  // create the MIDI record-play object
  if((rc = midi_record_play::create(app.mrpH,app.ioH,*app.midi_play_record_cfg,app.velTableFname)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI record-play object create failed.");
    goto errLabel;
  }
  
  // create the audio record-play object
  if((rc = audio_record_play::create(app.arpH,app.ioH,*cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio record-play object create failed.");
    goto errLabel;
  }
  
  // start the io framework instance
  if((rc = io::start(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio-MIDI app start failed.");
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
  io::destroy(app.ioH);
  printf("Audio-MIDI Done.\n");
  return rc;
  
}
