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
#include "cwIoMidiRecordPlay.h"
#include "cwIoPresetSelApp.h"
#include "cwPianoScore.h"

namespace cw
{
  namespace preset_sel_app
  {
    // Application Id's for UI elements
    enum
    {
      // Resource Based elements
      kPanelDivId = 1000,
      kQuitBtnId,
      kIoReportBtnId,
      kReportBtnId,
      
      kStartBtnId,
      kStopBtnId,
      
      kMidiThruCheckId,
      kCurMidiEvtCntId,
      kTotalMidiEvtCntId,
      
      kCurAudioSecsId,
      kTotalAudioSecsId,
                
      kSaveBtnId,
      kLoadBtnId,
      kFnStringId,

      kLocNumbId
      
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
        
      { kPanelDivId,     kStartBtnId,        "startBtnId" },
      { kPanelDivId,     kStopBtnId,         "stopBtnId" },
      
      { kPanelDivId,     kMidiThruCheckId,   "midiThruCheckId" },
      { kPanelDivId,     kCurMidiEvtCntId,   "curMidiEvtCntId" },      
      { kPanelDivId,     kTotalMidiEvtCntId, "totalMidiEvtCntId" },
      
      { kPanelDivId,     kCurAudioSecsId,    "curAudioSecsId" },
      { kPanelDivId,     kTotalAudioSecsId,  "totalAudioSecsId" },
      
        
      { kPanelDivId,     kSaveBtnId,      "saveBtnId" },
      { kPanelDivId,     kLoadBtnId,      "loadBtnId" },
      { kPanelDivId,     kFnStringId,     "filenameId" },

      { kPanelDivId,     kLocNumbId,      "locNumbId" }
        
    };

    unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);

    typedef struct loc_map_str
    {
      unsigned     loc;
      time::spec_t timestamp;
    } loc_map_t;
    
    typedef struct app_str
    {
      io::handle_t  ioH;
      
      const char* record_dir;
      const char* record_folder;
      const char* record_fn_ext;
      char*       directory;
      const char* scoreFn;
      
      midi_record_play::handle_t  mrpH;
      //audio_record_play::handle_t arpH;

      score::handle_t scoreH;
      loc_map_t*      locMap;
      unsigned        locMapN;
      
    } app_t;

    rc_t _parseCfg(app_t* app, const object_t* cfg )
    {
      rc_t rc = kOkRC;

      const object_t* params  = nullptr;

      if((rc = cfg->getv( "params", params)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App 'params' cfg record not found.");
        goto errLabel;
      }
        
      if((rc = params->getv( "record_dir",    app->record_dir,
                             "record_folder", app->record_folder,
                             "record_fn_ext", app->record_fn_ext,
                             "score_fn",      app->scoreFn )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App configuration parse failed.");
      }

      // verify that the output directory exists
      if((rc = filesys::isDir(app->record_dir)) != kOkRC )
        if((rc = filesys::makeDir(app->record_dir)) != kOkRC )
          rc = cwLogError(rc,"Unable to create the base output directory:%s.",cwStringNullGuard(app->record_dir));

      
    errLabel:
      
      return rc;
    }

    rc_t _free( app_t& app )      
    {
      mem::release(app.locMap);
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

    void _midi_play_callback( void* arg, unsigned id, const time::spec_t timestamp, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      app_t* app = (app_t*)arg;
      const unsigned buf_byte_cnt = 256;
      char buf[ buf_byte_cnt ];
      if( id != kInvalidId )
      {
        event_to_string( app->scoreH, id, buf, buf_byte_cnt );
        printf("%s\n",buf);
      }
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
      
      if((fn = filesys::makeFn(dir,"audio","wav",nullptr)) != nullptr )
      {        
        //if((rc1 = audio_record_play::save( app->arpH, fn )) != kOkRC )
        // rc1 = cwLogError(rc1,"Audio file '%s' save failed.",fn);

        mem::release(fn);
      }

    errLabel:
      mem::release(dir);
      
      return rcSelect(rc0,rc1);
    }


    rc_t _on_ui_load( app_t* app )
    {
      rc_t                          rc         = kOkRC;
      const score::event_t*         e          = nullptr;
      unsigned                      midiEventN = 0;      
      midi_record_play::midi_msg_t* m          = nullptr;

      printf("Loading\n");
      
      // create the score
      if((rc = score::create( app->scoreH, app->scoreFn )) != kOkRC )
      {
        cwLogError(rc,"Score create failed on '%s'.",app->scoreFn);
        goto errLabel;          
      }

      // get the count of MIDI events
      if((e = score::base_event( app->scoreH )) != nullptr )
        for(; e!=nullptr; e=e->link)
          if( e->status != 0 )
            midiEventN += 1;

      // copy the MIDI events
      if((e = score::base_event( app->scoreH )) != nullptr )
      {

        // allocate the locMap[]
        mem::free(app->locMap);
        app->locMap  = mem::allocZ<loc_map_t>( midiEventN );
        app->locMapN = midiEventN;
        
        // allocate the the player msg array
        m = mem::allocZ<midi_record_play::midi_msg_t>( midiEventN );

        // load the player msg array
        for(unsigned i=0; e!=nullptr && i<midiEventN; e=e->link)
          if( e->status != 0 )
          {
            time::millisecondsToSpec(m[i].timestamp,  (unsigned)(e->sec*1000) );
            m[i].ch     = e->status & 0x0f;
            m[i].status = e->status & 0xf0;
            m[i].d0     = e->d0;
            m[i].d1     = e->d1;
            m[i].id     = e->uid;

            app->locMap[i].loc       = e->loc;
            app->locMap[i].timestamp = m[i].timestamp;
            
            ++i;
          }

        // load the player with the msg list
        if((rc = midi_record_play::load( app->mrpH, m, midiEventN )) != kOkRC )
        {
          cwLogError(rc,"MIDI player load failed.");
          goto errLabel;
        }

        mem::free(m);
        
      }
      
      cwLogInfo("'%s' loaded.",app->scoreFn);
      
    errLabel:

      io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );
      io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), midi_record_play::event_count(app->mrpH) );
      
      return rc;
    }

    rc_t _on_ui_start( app_t* app )
    {
      rc_t rc=kOkRC;
      
      if((rc = midi_record_play::start(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI start failed.");
        goto errLabel;
      }

      //if((rc = audio_record_play::start(app->arpH)) != kOkRC )
      //{
      //  rc = cwLogError(rc,"Audio start failed.");
      //  goto errLabel;
      //}

      io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), midi_record_play::event_index(app->mrpH) );
      //io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kCurAudioSecsId),  audio_record_play::current_loc_seconds(app->arpH) );
      
      errLabel:
      return rc;
    }

    rc_t _on_ui_stop( app_t* app )
    {
      rc_t rc = kOkRC;
      
      if((rc = midi_record_play::stop(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI start failed.");
        goto errLabel;
      }

      //if((rc = audio_record_play::stop(app->arpH)) != kOkRC )
      //{
      //  rc = cwLogError(rc,"Audio start failed.");
      //  goto errLabel;
      //}
      
    errLabel:
      return rc;
    }

    rc_t _set_midi_thru_state( app_t* app, bool thru_fl )
    {
      rc_t rc = kOkRC;
      
      if((rc = midi_record_play::set_thru_state(app->mrpH,thru_fl)) != kOkRC )
        rc = cwLogError(rc,"%s MIDI thru state failed.",thru_fl ? "Enable" : "Disable" );
      
      return rc;
    }

    
    rc_t _on_ui_loc(app_t* app, unsigned loc)
    {
      rc_t rc = kOkRC;
      unsigned i=0;
      for(; i<app->locMapN; ++i)
      {
        if( app->locMap[i].loc == loc )
        {
          //  
          if((rc = midi_record_play::seek( app->mrpH, app->locMap[i].timestamp )) != kOkRC )
          {
            rc = cwLogError(rc,"MIDI seek failed.");
          }
          else
          {
            start( app->mrpH, false );
            io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );
          }
          break;
          
        }     
      }
      
      if( i == app->locMapN )
        rc = cwLogError(kOpFailRC,"The location '%i' could not be found.",loc);

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
          break;

        case kSaveBtnId:
          _on_ui_save(app);
          break;

        case kLoadBtnId:
          _on_ui_load(app);
          break;
        
        case kMidiThruCheckId:
          cwLogInfo("MIDI thru:%i",m.value->u.b);        
          _set_midi_thru_state(app, m.value->u.b);
          break;
            
        case kStartBtnId:
          _on_ui_start(app);
          break;
            
        case kStopBtnId:
          _on_ui_stop(app);
          break;

        case kFnStringId:
          mem::release(app->directory);
          app->directory = mem::duplStr(m.value->u.s);
          //printf("filename:%s\n",app->directory);
          break;

        case kLocNumbId:
          _on_ui_loc(app, m.value->u.i);
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
          io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), midi_record_play::event_index(app->mrpH) );
      }
      
      /*
      if( app->arpH.isValid() )
      {
        audio_record_play::exec( app->arpH, *m );
        if( audio_record_play::is_started(app->arpH) )
          io::uiSendValue( app->ioH, kInvalidId, uiFindElementUuId(app->ioH,kCurAudioSecsId), audio_record_play::current_loc_seconds(app->arpH) );
      }
      */
      
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

      default:
        assert(0);
        
      }

      return rc;
    }          
    
    
  }
}


cw::rc_t cw::preset_sel_app::main( const object_t* cfg )
{
  rc_t rc;
  app_t app = {};

  // Parse the configuration
  if((rc = _parseCfg(&app,cfg)) != kOkRC )
    goto errLabel;
        
  // create the io framework instance
  if((rc = io::create(app.ioH,cfg,_io_callback,&app,mapA,mapN)) != kOkRC )
  {
    rc = cwLogError(kOpFailRC,"IO Framework create failed.");
    goto errLabel;
  }

  // create the MIDI record-play object
  if((rc = midi_record_play::create(app.mrpH,app.ioH,*cfg,_midi_play_callback,&app)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI record-play object create failed.");
    goto errLabel;
  }

  
  /*
  // create the audio record-play object
  if((rc = audio_record_play::create(app.arpH,app.ioH,*cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio record-play object create failed.");
    goto errLabel;
  }
  */
  
  // start the io framework instance
  if((rc = io::start(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"Preset-select app start failed.");
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
  printf("Preset-select Done.\n");
  return rc;
  
}
