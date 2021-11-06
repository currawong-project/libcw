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
#include "cwPresetSel.h"

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

      kLocNumbId,

      kInsertLocId,
      kInsertBtnId,
      kDeleteBtnId,

      kFragListId,
      kFragPanelId,
      kFragEndLocId,
      
      kFragPresetRowId,
      kFragPresetSelId,
      kFragPresetOrderId,
      
      kFragGainId,
      kFragWetDryGainId,
      kFragFadeOutMsId
      
      
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

      { kPanelDivId,     kLocNumbId,      "locNumbId" },

      { kPanelDivId,     kInsertLocId,    "insertLocId" },
      { kPanelDivId,     kInsertBtnId,    "insertBtnId" },
      { kDeleteBtnId,    kDeleteBtnId,    "deleteBtnId" },

      { kPanelDivId,     kFragListId,       "fragListId" },
      { kFragListId,     kFragPanelId,      "fragPanelId"},
      { kFragPanelId,    kFragEndLocId,     "fragEndLocId" },
      { kFragPanelId,    kFragPresetRowId,  "fragPresetRowId" },
      { kFragPanelId,    kFragGainId,       "fragGainId" },
      { kFragPanelId,    kFragWetDryGainId, "fragWetDryGainId" },
      { kFragPanelId,    kFragFadeOutMsId,  "fragFadeOutMsId" }
      
    };

    unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);

    typedef struct loc_map_str
    {
      unsigned     loc;
      time::spec_t timestamp;
    } loc_map_t;
    
    typedef struct app_str
    {
      io::handle_t    ioH;
      
      const char*     record_dir;
      const char*     record_folder;
      const char*     record_fn_ext;
      char*           directory;
      const char*     scoreFn;
      const object_t* frag_panel_cfg;
      const object_t* presets_cfg;
      
      midi_record_play::handle_t  mrpH;

      score::handle_t scoreH;
      loc_map_t*      locMap;
      unsigned        locMapN;

      unsigned insertLoc; // last valid insert location id received from the GUI

      preset_sel::handle_t psH;
      
    } app_t;

    rc_t _parseCfg(app_t* app, const object_t* cfg, const object_t*& params_cfgRef )
    {
      rc_t rc = kOkRC;

      if((rc = cfg->getv( "params", params_cfgRef)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App 'params' cfg record not found.");
        goto errLabel;
      }
        
      if((rc = params_cfgRef->getv( "record_dir",    app->record_dir,
                                    "record_folder", app->record_folder,
                                    "record_fn_ext", app->record_fn_ext,
                                    "score_fn",      app->scoreFn,
                                    "frag_panel",    app->frag_panel_cfg,
                                    "presets",       app->presets_cfg)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App configuration parse failed.");
      }

      // verify that the output directory exists
      if((rc = filesys::isDir(app->record_dir)) != kOkRC )
        if((rc = filesys::makeDir(app->record_dir)) != kOkRC )
          rc = cwLogError(rc,"Unable to create the base output directory:%s.",cwStringNullGuard(app->record_dir));


      app->insertLoc = kInvalidId; // initialize 'insertLoc' to be invalid
      
    errLabel:
      
      return rc;
    }

    rc_t _free( app_t& app )      
    {
      preset_sel::destroy(app.psH);
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

    void _update_event_ui( app_t* app )
    {
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), midi_record_play::event_count(app->mrpH) );
    }
    
    template< typename T >
    rc_t _update_frag_ui( app_t* app, unsigned fragId, unsigned psVarId, unsigned psPresetId, unsigned uiParentUuId, unsigned uiVarAppId, unsigned uiChanId,  T& valRef )
    {
      rc_t     rc             = kOkRC;
      unsigned uuid           = kInvalidId;
      
      // Get the value from the data record
      if((rc = preset_sel::get_value( app->psH, fragId, psVarId, psPresetId, valRef )) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to locate the preset value for var:5i preset:%i.",psVarId,psPresetId);
        goto errLabel;
      }

      // Get the UI uuId
      if(( uuid = io::uiFindElementUuId( app->ioH, uiParentUuId, uiVarAppId, uiChanId )) == kInvalidId )
      {
        rc = cwLogError(rc,"Unable to locate the UI uuid for appid:%i chanId:%i.", uiVarAppId, uiChanId );
        goto errLabel;
      }

      // Send the value to the UI
      if((rc = io::uiSendValue( app->ioH, uuid, valRef )) != kOkRC )
      {
        rc = cwLogError(rc,"Transmission of fragment value failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _update_frag_ui(app_t* app, unsigned fragId )
    {
      // Notes: fragId == fragPanelUuId
      //        uiChanId = endLoc for panel values
      //     or uiChanId = preset_index for preset values

      rc_t     rc            = kOkRC;
      unsigned fragPanelUuId = fragId;
      unsigned endLoc;
      
      if((rc = get_value( app->psH, fragId, preset_sel::kEndLocVarId, kInvalidId, endLoc )) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to get the 'end loc' value for fragment id:%i.",fragId);
        goto errLabel;
      }
      else
      {
        bool     bValue;
        unsigned uValue;
        double   dValue;
        unsigned uiChanId = endLoc;

        
        _update_frag_ui( app, fragId, preset_sel::kEndLocVarId,    kInvalidId, fragPanelUuId, kFragEndLocId,     uiChanId,  uValue );          
        _update_frag_ui( app, fragId, preset_sel::kGainVarId,      kInvalidId, fragPanelUuId, kFragGainId,       uiChanId,  dValue );
        _update_frag_ui( app, fragId, preset_sel::kFadeOutMsVarId, kInvalidId, fragPanelUuId, kFragFadeOutMsId,  uiChanId,  dValue );
        _update_frag_ui( app, fragId, preset_sel::kWetGainVarId,   kInvalidId, fragPanelUuId, kFragWetDryGainId, uiChanId,  dValue );

        // uuid of the frag preset row
        unsigned fragPresetRowUuId = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragPresetRowId, uiChanId );
        
        for(unsigned preset_idx=0; preset_idx<preset_sel::preset_count(app->psH); ++preset_idx)
        {
          _update_frag_ui( app, fragId, preset_sel::kPresetSelectVarId, preset_idx, fragPresetRowUuId, kFragPresetSelId,   preset_idx,  bValue );          
          _update_frag_ui( app, fragId, preset_sel::kPresetOrderVarId,  preset_idx, fragPresetRowUuId, kFragPresetOrderId, preset_idx,  uValue );          
        }
      
      }
      
    errLabel:
      return rc;
    }

    template< typename T>
    rc_t _on_ui_frag_value( app_t* app, unsigned parentAppId, unsigned uuId, unsigned appId, unsigned chanId, const T& value )
    {
      rc_t rc = kOkRC;
      return rc;
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
      // enable the 'End Loc' number box since the score is loaded
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kInsertLocId ), true );

      // update the current event and event count
      _update_event_ui(app);
      
      cwLogInfo("'%s' loaded.",app->scoreFn);
      
    errLabel:

      _update_event_ui( app );
      
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

      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), midi_record_play::event_index(app->mrpH) );
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

    loc_map_t* _find_loc( app_t* app, unsigned loc )
    {
      unsigned i=0;
      for(; i<app->locMapN; ++i)
        if( app->locMap[i].loc == loc )
          return app->locMap + i;
      return nullptr;
    }
    
    bool _is_loc_valid( app_t* app, unsigned loc )
    {  return _find_loc(app,loc) != nullptr; }

    
    rc_t _on_ui_loc(app_t* app, unsigned loc)
    {
      rc_t rc = kOkRC;
      loc_map_t* map;

      // locate the map record
      if((map = _find_loc(app,loc)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The location '%i' could not be found.",loc);
        goto errLabel;
      }

      // seek the player to the requested loc
      if((rc = midi_record_play::seek( app->mrpH, map->timestamp )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI seek failed.");
        goto errLabel;
      }

      // start the player
      start( app->mrpH, false );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );

    errLabel:
      return rc;
    }

    
    rc_t _create_frag_preset_ctl( app_t* app, unsigned fragPresetRowUuId, unsigned presetN, unsigned preset_idx )
    {
      rc_t        rc           = kOkRC;
      unsigned    colUuId      = kInvalidId;
      unsigned    uuId         = kInvalidId;
      const char* nullEleName  = nullptr;
      const char* nullClass    = nullptr;
      unsigned    invalidAppId = kInvalidId;
      unsigned    chanId       = preset_idx; // chanId is the preset id
      const char* presetLabel  = preset_sel::preset_label( app->psH, preset_idx );
        
      assert( presetLabel != nullptr );

      // preset control column container
      if((rc = io::uiCreateDiv( app->ioH, colUuId, fragPresetRowUuId, nullEleName, invalidAppId, chanId, "col fragPresetCtl", nullptr )) != kOkRC )
        goto errLabel;

      // preset select check
      if((rc = io::uiCreateCheck( app->ioH, uuId, colUuId, nullEleName, kFragPresetSelId, chanId, nullClass, presetLabel )) != kOkRC )
        goto errLabel;

      // preset order number
      if((rc = io::uiCreateNumb( app->ioH, uuId,  colUuId, nullEleName, kFragPresetOrderId, chanId, nullClass, nullptr, 0, presetN, 1, 0 )) != kOkRC )
        goto errLabel;


    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Preset control index '%i' create failed.");
      return rc;
    }

    rc_t _on_ui_insert_btn( app_t* app )
    {
      rc_t rc = kOkRC;
      unsigned fragListUuId      = io::uiFindElementUuId( app->ioH, kFragListId );
      unsigned fragChanId        = app->insertLoc;   // use the frag. endLoc as the channel id
      unsigned fragPanelUuId     = kInvalidId;
      unsigned fragEndLocUuId    = kInvalidId;
      unsigned fragPresetRowUuId = kInvalidId;
      unsigned presetN           = preset_sel::preset_count( app->psH );

      // verify that frag panel resource object is initiailized
      if( app->frag_panel_cfg == nullptr)
      {
        rc = cwLogError(kInvalidStateRC,"The fragment UI resource was not initialized.");
        goto errLabel;
      }

      // verify that the insertion location is valid
      if( app->insertLoc == kInvalidId )
      {
        rc = cwLogError(kInvalidIdRC,"The new fragment's 'End Loc' is not valid.");
        goto errLabel;
      }

      // create the UI object
      if((rc = io::uiCreateFromObject( app->ioH, app->frag_panel_cfg,  fragListUuId, fragChanId )) != kOkRC )
      {
        rc = cwLogError(rc,"The fragments UI object creation failed.");
        goto errLabel;
      }

      // get the uuid's of the new fragment panel and the endloc number display
      fragPanelUuId     = io::uiFindElementUuId( app->ioH, fragListUuId,  kFragPanelId,     fragChanId ); 
      fragEndLocUuId    = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragEndLocId,    fragChanId );
      fragPresetRowUuId = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragPresetRowId, fragChanId );

      assert( fragPanelUuId     != kInvalidId );
      assert( fragEndLocUuId    != kInvalidId );
      assert( fragPresetRowUuId != kInvalidId );

      // create each of the preset controls
      for(unsigned preset_idx=0; preset_idx<presetN; ++preset_idx)
        if((rc = _create_frag_preset_ctl(app, fragPresetRowUuId, presetN, preset_idx )) != kOkRC )
          goto errLabel;
      
      // create the data record associated with the new fragment.
      if((rc = preset_sel::create_fragment( app->psH, fragPanelUuId, app->insertLoc)) != kOkRC )
      {
        rc = cwLogError(rc,"Fragment data record create failed.");
        goto errLabel;
      }

      // update the fragment UI
      _update_frag_ui(app, fragPanelUuId );
      
    errLabel:
      return rc;
          
    }


    rc_t _onUiInit(app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;

      _update_event_ui(app);

      const preset_sel::frag_t* f = preset_sel::get_fragment_base( app->psH );
      for(; f!=nullptr; f=f->link)
        _update_frag_ui( app, f->fragId );

      
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

        case kInsertLocId:
          {
            unsigned insertLocId = m.value->u.u;
            bool     enableFl    = _is_loc_valid(app,insertLocId);
            unsigned insertBtnUuId = io::uiFindElementUuId( app->ioH, kInsertBtnId );
            io::uiSetEnable( app->ioH, insertBtnUuId, enableFl );
            if( enableFl )
              app->insertLoc = insertLocId;
            else
            {
              app->insertLoc = kInvalidId;
              cwLogWarning("Location '%i' is not valid.",insertLocId);
            }
          }
          break;
          
        case kInsertBtnId:
          _on_ui_insert_btn(app);
          break;
          
        case kDeleteBtnId:
          break;

        case kFragGainId:
          _on_ui_frag_value( app, m.parentAppId, m.uuId, m.appId, m.chanId, m.value->u.d );
          break;
          
        case kFragWetDryGainId:
          _on_ui_frag_value( app, m.parentAppId, m.uuId, m.appId, m.chanId, m.value->u.d );
          break;
          
        case kFragFadeOutMsId:
          _on_ui_frag_value( app, m.parentAppId, m.uuId, m.appId, m.chanId, m.value->u.d );
          break;

        case kFragPresetOrderId:
          _on_ui_frag_value( app, m.parentAppId, m.uuId, m.appId, m.chanId, m.value->u.u );
          break;
            
        case kFragPresetSelId:
          _on_ui_frag_value( app, m.parentAppId, m.uuId, m.appId, m.chanId, m.value->u.b );
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
  app_t app = { };
  const object_t* params_cfg = nullptr;
  
  // Parse the configuration
  if((rc = _parseCfg(&app,cfg,params_cfg)) != kOkRC )
    goto errLabel;
        
  // create the io framework instance
  if((rc = io::create(app.ioH,cfg,_io_callback,&app,mapA,mapN)) != kOkRC )
  {
    rc = cwLogError(kOpFailRC,"IO Framework create failed.");
    goto errLabel;
  }

  // create the preset selection state object
  if((rc = create(app.psH, app.presets_cfg )) != kOkRC )
  {
    rc = cwLogError(kOpFailRC,"Preset state control object create failed.");
    goto errLabel;
  }


  // create the MIDI record-play object
  if((rc = midi_record_play::create(app.mrpH,app.ioH,*params_cfg,_midi_play_callback,&app)) != kOkRC )
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
