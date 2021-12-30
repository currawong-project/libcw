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
#include "cwVectOps.h"
#include "cwMath.h"
#include "cwDspTypes.h"
#include "cwMtx.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
#include "cwFlowCross.h"
#include "cwIoFlow.h"
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

      kBegPlayLocNumbId,
      kEndPlayLocNumbId,

      kInsertLocId,
      kInsertBtnId,
      kDeleteBtnId,

      kStatusId,

      kLogId,

      kFragListId,
      kFragPanelId,
      kFragBegLocId,
      kFragEndLocId,
      
      kFragPresetRowId,
      kFragPresetSelId,
      kFragPresetOrderId,
      
      kFragInGainId,
      kFragOutGainId,
      kFragWetDryGainId,
      kFragFadeOutMsId,
      kFragBegPlayLocId,
      kFragEndPlayLocId,
      kFragPlayBtnId,
      kFragNoteId
      
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

      { kPanelDivId,     kBegPlayLocNumbId,   "begLocNumbId" },
      { kPanelDivId,     kEndPlayLocNumbId,   "endLocNumbId" },

      { kPanelDivId,     kInsertLocId,    "insertLocId" },
      { kPanelDivId,     kInsertBtnId,    "insertBtnId" },
      { kPanelDivId,     kDeleteBtnId,    "deleteBtnId" },
      { kPanelDivId,     kStatusId,       "statusId" },
      { kPanelDivId,     kLogId,          "logId" },

      { kPanelDivId,     kFragListId,       "fragListId"   },
      { kFragListId,     kFragPanelId,      "fragPanelId"  },
      { kFragPanelId,    kFragBegLocId,     "fragBegLocId" },
      { kFragPanelId,    kFragEndLocId,     "fragEndLocId" },
      { kFragPanelId,    kFragPresetRowId,  "fragPresetRowId" },
      { kFragPanelId,    kFragInGainId,     "fragInGainId"  },
      { kFragPanelId,    kFragOutGainId,    "fragOutGainId" },
      { kFragPanelId,    kFragWetDryGainId, "fragWetDryGainId" },
      { kFragPanelId,    kFragFadeOutMsId,  "fragFadeOutMsId"  },
      { kFragPanelId,    kFragBegPlayLocId, "fragBegPlayLocId" },
      { kFragPanelId,    kFragEndPlayLocId, "fragEndPlayLocId" },
      { kFragPanelId,    kFragPlayBtnId,    "fragPlayBtnId" },
      { kFragPanelId,     kFragNoteId,       "fragNoteId" },
      
    };

    unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);

    typedef struct loc_map_str
    {
      unsigned     loc;
      time::spec_t timestamp;
    } loc_map_t;

    typedef struct ui_blob_str
    {
      unsigned fragId;
      unsigned varId;
      unsigned presetId;
    } ui_blob_t;
    
    typedef struct app_str
    {
      io::handle_t    ioH;
      
      const char*     record_dir;
      const char*     record_fn;
      const char*     record_fn_ext;
      const char*     scoreFn;
      const object_t* frag_panel_cfg;
      const object_t* presets_cfg;
      const object_t* flow_cfg;
      
      midi_record_play::handle_t  mrpH;

      score::handle_t scoreH;
      loc_map_t*      locMap;
      unsigned        locMapN;

      unsigned        insertLoc; // last valid insert location id received from the GUI

      unsigned        minLoc;
      unsigned        maxLoc;
      
      unsigned        beg_play_loc;
      unsigned        end_play_loc;
      
      time::spec_t    beg_play_timestamp;
      time::spec_t    end_play_timestamp;

      preset_sel::handle_t psH;
      io_flow::handle_t    ioFlowH;
      
      unsigned tmp;

      double   crossFadeSrate;
      unsigned crossFadeCnt;
      
    } app_t;

    rc_t _parseCfg(app_t* app, const object_t* cfg, const object_t*& params_cfgRef )
    {
      rc_t rc = kOkRC;

      if((rc = cfg->getv( "params", params_cfgRef,
                          "flow",   app->flow_cfg)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App 'params' cfg record not found.");
        goto errLabel;
      }
        
      if((rc = params_cfgRef->getv( "record_dir",    app->record_dir,
                                    "record_fn",     app->record_fn,
                                    "record_fn_ext", app->record_fn_ext,
                                    "score_fn",      app->scoreFn,
                                    "frag_panel",    app->frag_panel_cfg,
                                    "presets",       app->presets_cfg,
                                    "crossFadeSrate",app->crossFadeSrate,
                                    "crossFadeCount",app->crossFadeCnt)) != kOkRC )
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

    void _set_statusv( app_t* app, const char* fmt, va_list vl )
    {
      const int sN = 128;
      char s[sN];
      vsnprintf(s,sN,fmt,vl);      
      uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kStatusId), s );
    }

    
    void _set_status( app_t* app, const char* fmt, ... )
    {
      va_list vl;
      va_start(vl,fmt);
      _set_statusv(app, fmt, vl );
      va_end(vl);
    }

    void _clear_status( app_t* app )
    {
      _set_status(app,"Ok");
    }

    void _log_output_func( void* arg, unsigned level, const char* text )
    {
      app_t*   app     = (app_t*)arg;
      unsigned logUuId = uiFindElementUuId( app->ioH, kLogId);

      uiSetLogLine( app->ioH, logUuId, text );
      log::defaultOutput(nullptr,level,text);
    }


    rc_t _free( app_t& app )      
    {
      preset_sel::destroy(app.psH);
      io_flow::destroy(app.ioFlowH);
      midi_record_play::destroy(app.mrpH);
      score::destroy( app.scoreH );
      mem::release(app.locMap);
      return kOkRC;
    }


    rc_t _apply_preset( app_t* app, const time::spec_t& ts, const preset_sel::frag_t* frag=nullptr  )      
    {
      if( frag == nullptr )
        preset_sel::track_timestamp( app->psH, ts, frag);

      if( frag == nullptr )
        cwLogInfo("No preset fragment was found for the requested timestamp.");
      else
      {
        unsigned preset_idx;

        if((preset_idx = fragment_play_preset_index(frag)) == kInvalidIdx )
          cwLogInfo("No preset has been assigned to the fragment at end loc. '%i'.",frag->endLoc );
        else
        {        
          const char* preset_label = preset_sel::preset_label(app->psH,preset_idx);
        
          cwLogInfo("Apply preset: '%s'.\n", preset_idx==kInvalidIdx ? "<invalid>" : preset_label);

          if( preset_label != nullptr )
          {
            io_flow::apply_preset( app->ioFlowH, flow_cross::kNextDestId, preset_label );

            io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "wd_bal",    "in",    flow::kAnyChIdx, (dsp::real_t)frag->wetDryGain );
            io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "split_wet", "gain",  flow::kAnyChIdx, (dsp::real_t)frag->igain );
            io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "cmp",       "ogain", flow::kAnyChIdx, (dsp::real_t)frag->ogain );
            
            io_flow::begin_cross_fade( app->ioFlowH, frag->fadeOutMs );
          }
        }
      }

      return kOkRC;      
    }

    void _midi_play_callback( void* arg, unsigned id, const time::spec_t timestamp, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      app_t* app = (app_t*)arg;
      if( id != kInvalidId )
      {
        const unsigned buf_byte_cnt = 256;
        char buf[ buf_byte_cnt ];
        event_to_string( app->scoreH, id, buf, buf_byte_cnt );
        printf("%s\n",buf);

        const preset_sel::frag_t* f = nullptr;
        if( preset_sel::track_timestamp( app->psH, timestamp, f ) )
        {          
          //printf("NEW FRAG: id:%i loc:%i\n", f->fragId, f->endLoc );
          _apply_preset( app, timestamp, f );                
        }
        

      }
    }

    loc_map_t* _find_loc( app_t* app, unsigned loc )
    {
      unsigned i=0;
      for(; i<app->locMapN; ++i)
        if( app->locMap[i].loc == loc )
          return app->locMap + i;
      return nullptr;
    }
    
    
    rc_t _do_play( app_t* app, unsigned begLoc, unsigned endLoc )
    {
      rc_t rc = kOkRC;
      bool rewindFl = true;
      loc_map_t* begMap = nullptr;
      loc_map_t* endMap = nullptr;

      if((begMap = _find_loc(app,begLoc)) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The begin play location is not valid.");
        goto errLabel;
      }

      if((endMap = _find_loc(app,endLoc)) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The end play location is not valid.");
        goto errLabel;
      }
      
      app->beg_play_timestamp = begMap->timestamp;
      app->end_play_timestamp = endMap->timestamp;

      
      if( !time::isZero(app->beg_play_timestamp) )
      {
          // seek the player to the requested loc
          if((rc = midi_record_play::seek( app->mrpH, app->beg_play_timestamp )) != kOkRC )
          {
            rc = cwLogError(rc,"MIDI seek failed.");
            goto errLabel;
          }
          rewindFl = false;
      }

      // apply the preset which is active at the start time
      if((rc = _apply_preset( app, app->beg_play_timestamp )) != kOkRC )
      {
        rc = cwLogError(rc,"Preset application failed prior to MIDI start.");
        goto errLabel;
      }

      // start the MIDI playback
      if((rc = midi_record_play::start(app->mrpH,rewindFl,&app->end_play_timestamp)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI start failed.");
        goto errLabel;
      }

      
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), midi_record_play::event_index(app->mrpH) );

    errLabel:
      return rc;
    }

    rc_t _do_play_fragment( app_t* app, unsigned fragId )
    {
      rc_t rc;
      unsigned begLoc = 0;
      unsigned endLoc = 0;
      
      if((rc = get_value( app->psH, fragId, preset_sel::kBegPlayLocVarId, kInvalidId, begLoc )) != kOkRC )
      {
        rc = cwLogError(rc,"Could not retrieve the begin play location for fragment id:%i.",fragId);
        goto errLabel;
      }

      if((rc = get_value( app->psH, fragId, preset_sel::kEndPlayLocVarId, kInvalidId, endLoc )) != kOkRC )
      {
        rc = cwLogError(rc,"Could not retrieve the begin play location for fragment id:%i.",fragId);
        goto errLabel;
      }

      rc = _do_play(app,begLoc,endLoc);
      
    errLabel:
      return rc;
      
    }

    
    void _update_event_ui( app_t* app )
    {
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), midi_record_play::event_count(app->mrpH) );
    }

    // Update the UI with the value from the the fragment data record.
    template< typename T >
    rc_t _update_frag_ui( app_t* app, unsigned fragId, unsigned psVarId, unsigned psPresetId, unsigned uiParentUuId, unsigned uiVarAppId, unsigned uiChanId,  T& valRef )
    {
      rc_t     rc             = kOkRC;
      unsigned uuid           = kInvalidId;
      
      // Get the value from the data record
      if((rc = preset_sel::get_value( app->psH, fragId, psVarId, psPresetId, valRef )) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to locate the preset value for var:%i preset:%i.",psVarId,psPresetId);
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

    // Get the endLoc associated with this fragment id
    rc_t _frag_id_to_endloc( app_t* app, unsigned fragId, unsigned& endLocRef )
    {
      rc_t rc = kOkRC;

      endLocRef = kInvalidId;
      if((rc = get_value( app->psH, fragId, preset_sel::kEndLocVarId, kInvalidId, endLocRef )) != kOkRC )
        rc = cwLogError(rc,"Unable to get the 'end loc' value for fragment id:%i.",fragId);

      return rc;
    }

    // Update the preset select check boxes on a fragment panel
    rc_t _update_frag_select_flags( app_t* app, unsigned fragId, unsigned fragEndLoc=kInvalidId )
    {
      rc_t     rc = kOkRC;

      // if 
      if( fragEndLoc == kInvalidId )
      {
        // get the endLoc associated with this fragment
        rc = _frag_id_to_endloc(app, fragId, fragEndLoc );
      }

      if( rc == kOkRC )
      {
        bool     bValue;
        unsigned uValue;
        unsigned fragPanelUuId;
        
        // The uiChan is the fragment endLoc
        unsigned uiChanId = fragEndLoc;

        // Get the fragPanelUUid
        get_value( app->psH, fragId, preset_sel::kGuiUuIdVarId, kInvalidId, fragPanelUuId );

        // uuid of the frag preset row
        unsigned fragPresetRowUuId = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragPresetRowId, uiChanId );


        
        // Update each fragment preset control UI by getting it's current value from the fragment data record
        for(unsigned preset_idx=0; preset_idx<preset_sel::preset_count(app->psH); ++preset_idx)
        {
          _update_frag_ui( app, fragId, preset_sel::kPresetSelectVarId, preset_idx, fragPresetRowUuId, kFragPresetSelId,   preset_idx,  bValue );          
          _update_frag_ui( app, fragId, preset_sel::kPresetOrderVarId,  preset_idx, fragPresetRowUuId, kFragPresetOrderId, preset_idx,  uValue );          
        }
        
      }
      
      return rc;
    }

    // Update the fragment UI withh the fragment record associated with 'fragId'
    rc_t _update_frag_ui(app_t* app, unsigned fragId )
    {
      // Notes: 
      //        uiChanId = endLoc for panel values
      //     or uiChanId = preset_index for preset values

      rc_t     rc            = kOkRC;
      unsigned endLoc;

      // get the endLoc for this fragment 
      if((rc = _frag_id_to_endloc(app, fragId, endLoc )) == kOkRC )
      {
        unsigned uValue;
        double   dValue;
        const char* sValue;
        unsigned uiChanId      = endLoc;
        unsigned fragPanelUuId = kInvalidId;
        
        get_value( app->psH, fragId, preset_sel::kGuiUuIdVarId, kInvalidId, fragPanelUuId );

        
        _update_frag_ui( app, fragId, preset_sel::kBegLocVarId,     kInvalidId, fragPanelUuId, kFragBegLocId,     uiChanId,  uValue );          
        _update_frag_ui( app, fragId, preset_sel::kEndLocVarId,     kInvalidId, fragPanelUuId, kFragEndLocId,     uiChanId,  uValue );          
        _update_frag_ui( app, fragId, preset_sel::kInGainVarId,     kInvalidId, fragPanelUuId, kFragInGainId,     uiChanId,  dValue );
        _update_frag_ui( app, fragId, preset_sel::kInGainVarId,     kInvalidId, fragPanelUuId, kFragOutGainId,    uiChanId,  dValue );
        _update_frag_ui( app, fragId, preset_sel::kFadeOutMsVarId,  kInvalidId, fragPanelUuId, kFragFadeOutMsId,  uiChanId,  dValue );
        _update_frag_ui( app, fragId, preset_sel::kWetGainVarId,    kInvalidId, fragPanelUuId, kFragWetDryGainId, uiChanId,  dValue );
        _update_frag_ui( app, fragId, preset_sel::kBegPlayLocVarId, kInvalidId, fragPanelUuId, kFragBegPlayLocId, uiChanId,  uValue );          
        _update_frag_ui( app, fragId, preset_sel::kEndPlayLocVarId, kInvalidId, fragPanelUuId, kFragEndPlayLocId, uiChanId,  uValue );          
        _update_frag_ui( app, fragId, preset_sel::kNoteVarId,       kInvalidId, fragPanelUuId, kFragNoteId,       uiChanId,  sValue );          
        
        _update_frag_select_flags( app, fragId, endLoc );
        
      }
      
      return rc;
    }

    rc_t  _frag_uuid_to_blob( app_t* app, unsigned uuId, ui_blob_t*& blobRef )
    {
      unsigned   blobByteN = 0;
      
      if(( blobRef = (ui_blob_t*)io::uiGetBlob( app->ioH, uuId, blobByteN )) == nullptr || blobByteN != sizeof(ui_blob_t) )
        return cwLogError(kInvalidStateRC,"A fragment UI blob was missing or corrupt for GUI uuid:%i.",uuId);

      return kOkRC;
    }

    void _enable_global_play_btn( app_t* app, bool enableFl )
    {
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStartBtnId ), enableFl );
      //io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStopBtnId ),  enableFl );      
    }

    void _enable_frag_play_btn( app_t* app, ui_blob_t* blob, unsigned, const char* ){}
    void _enable_frag_play_btn( app_t* app, ui_blob_t* blob, const char*, unsigned  ){}
    void _enable_frag_play_btn( app_t* app, ui_blob_t* blob, unsigned begPlayLoc, unsigned endPlayLoc )
    {
      bool     enableFl        = begPlayLoc < endPlayLoc;
      unsigned fragUuId        = kInvalidId;
      unsigned fragPlayBtnUuId = kInvalidId;

      if((fragUuId = frag_to_gui_id( app->psH, blob->fragId )) != kInvalidId )
        if((fragPlayBtnUuId =  uiFindElementUuId( app->ioH, fragUuId, kFragPlayBtnId, blob->presetId )) != kInvalidId )
        {
          uiSetEnable( app->ioH, fragPlayBtnUuId, enableFl );
          if( enableFl )
            _clear_status(app);
          else
            _set_status(app,"Invalid fragment play range.");
        }
      
    }

    void _disable_frag_play_btn( app_t* app, unsigned fragBegEndUuId )
    {
      ui_blob_t* blob      = nullptr;
      if(_frag_uuid_to_blob(app, fragBegEndUuId, blob) == kOkRC )
        _enable_frag_play_btn( app, blob, 1, (unsigned)0 );      
    }

    // Called when a UI value is changed in a fragment panel (e.g. gain, fadeMs, ...)
    template< typename T>
    rc_t _on_ui_frag_value( app_t* app, unsigned uuId, const T& value )
    {
      rc_t       rc        = kOkRC;
      ui_blob_t* blob      = nullptr;

      if((rc = _frag_uuid_to_blob(app, uuId, blob)) != kOkRC )
        goto errLabel;

      rc = preset_sel::set_value( app->psH, blob->fragId, blob->varId, blob->presetId, value );

      if( rc != kOkRC )
      {
        // TODO: Set the error indicator on the GUI
      }
      else
      {
        // TODO: Clear the error indicator on the GUI
      }

      //
      switch( blob->varId )
      {
        case preset_sel::kPresetSelectVarId:
          _update_frag_select_flags( app, blob->fragId);
          break;
          
        case preset_sel::kPlayBtnVarId:
          _do_play_fragment( app, blob->fragId );
          break;

        case preset_sel::kBegPlayLocVarId:
          {
            unsigned endPlayLoc;
            get_value( app->psH, blob->fragId, preset_sel::kEndPlayLocVarId, blob->presetId, endPlayLoc );
            _enable_frag_play_btn( app, blob, value, endPlayLoc );
          }
          break;
          
        case preset_sel::kEndPlayLocVarId:
          {
            unsigned begPlayLoc;
            get_value( app->psH, blob->fragId, preset_sel::kBegPlayLocVarId, blob->presetId, begPlayLoc );
            _enable_frag_play_btn( app, blob, begPlayLoc, value );
          }
          break;
      }

    errLabel:
      return rc;
    }


    rc_t _frag_set_ui_blob( app_t* app, unsigned uuId, unsigned fragId, unsigned varId, unsigned presetId )
    {
      ui_blob_t   blob = { .fragId=fragId, .varId=varId, .presetId=presetId };
      return io::uiSetBlob( app->ioH, uuId, &blob, sizeof(blob) );      
    }

    
    rc_t _create_frag_preset_ctl( app_t* app, unsigned fragId, unsigned fragPresetRowUuId, unsigned presetN, unsigned preset_idx )
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

      // store a connection for the select control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetSelectVarId, preset_idx );


      // preset order number
      if((rc = io::uiCreateNumb( app->ioH, uuId,  colUuId, nullEleName, kFragPresetOrderId, chanId, nullClass, nullptr, 0, presetN, 1, 0 )) != kOkRC )
        goto errLabel;

      // store a connection for the select control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetOrderVarId, preset_idx );

    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Preset control index '%i' create failed.");
      return rc;
    }

    rc_t _create_frag_ui( app_t* app, unsigned endLoc, unsigned fragId )
    {
      rc_t       rc                = kOkRC;
      unsigned   fragListUuId      = io::uiFindElementUuId( app->ioH, kFragListId );
      unsigned   fragChanId        = endLoc; // use the frag. endLoc as the channel id
      unsigned   fragPanelUuId     = kInvalidId;
      unsigned   fragBegLocUuId    = kInvalidId;
      unsigned   fragEndLocUuId    = kInvalidId;
      unsigned   fragPresetRowUuId = kInvalidId;
      unsigned   presetN           = preset_sel::preset_count( app->psH );
      unsigned   fragBegLoc        = 0;

      // create the UI object
      if((rc = io::uiCreateFromObject( app->ioH, app->frag_panel_cfg,  fragListUuId, fragChanId )) != kOkRC )
      {
        rc = cwLogError(rc,"The fragments UI object creation failed.");
        goto errLabel;
      }
      
      // get the uuid's of the new fragment panel and the endloc number display
      fragPanelUuId     = io::uiFindElementUuId( app->ioH, fragListUuId,  kFragPanelId,     fragChanId );
      fragBegLocUuId    = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragBegLocId,    fragChanId );
      fragEndLocUuId    = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragEndLocId,    fragChanId );
      fragPresetRowUuId = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragPresetRowId, fragChanId );

      assert( fragPanelUuId     != kInvalidId );
      assert( fragBegLocUuId    != kInvalidId );
      assert( fragEndLocUuId    != kInvalidId );
      assert( fragPresetRowUuId != kInvalidId );

      // Make the fragment panel clickable
      io::uiSetClickable(   app->ioH, fragPanelUuId);

      // Set the fragment panel order 
      io::uiSetOrderKey( app->ioH, fragPanelUuId, endLoc );

      // Set the fragment beg/end play range
      get_value( app->psH, fragId, preset_sel::kBegPlayLocVarId, kInvalidId, fragBegLoc );
      uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragBegPlayLocId, fragChanId), app->minLoc, app->maxLoc, 1, 0, fragBegLoc );
      uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragEndPlayLocId, fragChanId), app->minLoc, app->maxLoc, 1, 0, endLoc );


      // Attach blobs to the UI to allow convenient access back to the prese_sel data record
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragInGainId,     fragChanId), fragId, preset_sel::kInGainVarId,     kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragOutGainId,    fragChanId), fragId, preset_sel::kOutGainVarId,    kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragWetDryGainId, fragChanId), fragId, preset_sel::kWetGainVarId,    kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragFadeOutMsId,  fragChanId), fragId, preset_sel::kFadeOutMsVarId,  kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragBegPlayLocId, fragChanId), fragId, preset_sel::kBegPlayLocVarId, kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragEndPlayLocId, fragChanId), fragId, preset_sel::kEndPlayLocVarId, kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragPlayBtnId,    fragChanId), fragId, preset_sel::kPlayBtnVarId,    kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragNoteId,       fragChanId), fragId, preset_sel::kNoteVarId,       kInvalidId );
      
      
      // create each of the preset controls
      for(unsigned preset_idx=0; preset_idx<presetN; ++preset_idx)
        if((rc = _create_frag_preset_ctl(app, fragId, fragPresetRowUuId, presetN, preset_idx )) != kOkRC )
          goto errLabel;

      // set the uuid associated with this fragment
      preset_sel::set_value( app->psH, fragId, preset_sel::kGuiUuIdVarId, kInvalidId, fragPanelUuId );

    errLabel:
      return rc;
      
    }
    
    
    rc_t _restore( app_t* app )
    {
      rc_t  rc = kOkRC;
      char* fn = nullptr;
      const preset_sel::frag_t* f = nullptr;

      // form the output file name
      if((fn = filesys::makeFn(app->record_dir, app->record_fn, app->record_fn_ext, NULL)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The preset select filename could not formed.");
        goto errLabel;
      }

      if( !filesys::isFile(fn) )
      {
        cwLogInfo("The preset selection data file: '%s' does not exist.",cwStringNullGuard(fn));
        goto errLabel;
      }

      // read the preset data file
      if((rc = preset_sel::read( app->psH, fn)) != kOkRC )
      {
        rc = cwLogError(rc,"File write failed on preset select.");
        goto errLabel;
      }

      preset_sel::report( app->psH );

      f = preset_sel::get_fragment_base(app->psH);
      for(; f!=nullptr; f=f->link)
      {
        unsigned fragId = f->fragId;
        
        if((rc = _create_frag_ui(app, f->endLoc, fragId )) != kOkRC )
        {
          cwLogError(rc,"Frag UI create failed.");
          goto errLabel;
        }

        _update_frag_ui(app, fragId );

      }
      

    errLabel:
      mem::release(fn);

      

      return rc;
    }
    
    rc_t _on_ui_save( app_t* app )
    {
      rc_t  rc = kOkRC;
      char* fn = nullptr;

      // form the output file name
      if((fn = filesys::makeFn(app->record_dir, app->record_fn, app->record_fn_ext, NULL)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The preset select filename could not formed.");
        goto errLabel;
      }

      // write the preset data file
      if((rc = preset_sel::write( app->psH, fn)) != kOkRC )
      {
        rc = cwLogError(rc,"File write failed on preset select.");
        goto errLabel;
      }

    errLabel:
      mem::release(fn);

      if( rc == kOkRC )
        _clear_status(app);
      else
        _set_status(app,"Save failed.");

      return rc;
    }


    rc_t _do_load( app_t* app )
    {
      rc_t                          rc         = kOkRC;
      const score::event_t*         e          = nullptr;
      unsigned                      midiEventN = 0;      
      midi_record_play::midi_msg_t* m          = nullptr;

      // if the score is already loaded
      if( app->scoreH.isValid() )
        return rc;

      cwLogInfo("Loading");
      _set_status(app,"Loading...");

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
        app->minLoc  = midiEventN;
        app->maxLoc  = 0;
        
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

            app->minLoc = std::min(app->minLoc,e->loc);
            app->maxLoc = std::max(app->maxLoc,e->loc);
            
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

      // set the range of the global play location controls
      io::uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, kBegPlayLocNumbId), app->minLoc, app->maxLoc, 1, 0, app->minLoc );
      io::uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, kEndPlayLocNumbId), app->minLoc, app->maxLoc, 1, 0, app->maxLoc );

      
      // enable the 'End Loc' number box since the score is loaded
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kInsertLocId ), true );

      // update the current event and event count
      _update_event_ui(app);


      // enable the start/stop buttons
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStartBtnId ), true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStopBtnId ),  true );

      // restore the fragment records
      if((rc = _restore( app )) != kOkRC )
      {
        rc = cwLogError(rc,"Restore failed.");
        goto errLabel;
      }

      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLoadBtnId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSaveBtnId ), true );
      
      cwLogInfo("'%s' loaded.",app->scoreFn);
      
    errLabel:

      _update_event_ui( app );

      if( rc != kOkRC )
        _set_status(app,"Load failed.");
      else
        _set_status(app,"%i MIDI events loaded.",midiEventN);

      return rc;
    }


    rc_t _on_ui_start( app_t* app )
    {
      return _do_play(app, app->beg_play_loc, app->end_play_loc );
    }

    rc_t _on_ui_stop( app_t* app )
    {
      rc_t rc = kOkRC;

      if((rc = midi_record_play::stop(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI start failed.");
        goto errLabel;
      }

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

    bool _is_valid_insert_loc( app_t* app, unsigned loc )
    {
      bool fl0 = _find_loc(app,loc) != nullptr;
      bool fl1 = preset_sel::is_fragment_loc( app->psH, loc)==false;

      return fl0 && fl1;
    }


    // Called when the global play locations change
    rc_t _on_ui_play_loc(app_t* app, unsigned appId, unsigned loc)
    {
      rc_t       rc = kOkRC;

      switch( appId )
      {
        case kBegPlayLocNumbId:
          app->beg_play_loc = loc;
          break;
          
        case kEndPlayLocNumbId:
          app->end_play_loc = loc;
          break;
      }

      bool enableFl = app->beg_play_loc < app->end_play_loc;
      
      _enable_global_play_btn(app, enableFl );

      if(enableFl)
        _clear_status(app);
      else
        _set_status(app,"Invalid play location range.");

      
      return rc;
    }

    rc_t _on_ui_insert_loc( app_t* app, unsigned insertLoc )
    {
      rc_t     rc            = kOkRC;
      bool     enableFl      = _is_valid_insert_loc(app,insertLoc);
      unsigned insertBtnUuId = io::uiFindElementUuId( app->ioH, kInsertBtnId );
      
      io::uiSetEnable( app->ioH, insertBtnUuId, enableFl );
      
      if( enableFl )
      {
        app->insertLoc = insertLoc;
        _clear_status(app);
      }
      else
      {
        app->insertLoc = kInvalidId;
        _set_status(app,"Location '%i' is not valid.",insertLoc);
      }
      return rc;
    }


    rc_t _on_ui_insert_btn( app_t* app )
    {
      rc_t                      rc     = kOkRC;
      unsigned                  fragId = kInvalidId;
      loc_map_t*                loc_ts = nullptr;
      const preset_sel::frag_t* f      = nullptr;;

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

      // verify that the end-loc is not already in use - this shouldn't be possible because the 'insert' btn should be disabled if the 'insertLoc' is not valid
      if( preset_sel::is_fragment_loc( app->psH, app->insertLoc ) )
      {
        rc = cwLogError(kInvalidIdRC,"The new fragment's 'End Loc' is already in use.");
        goto errLabel;
      }

      // get the timestamp assoc'd with the the 'end-loc'
      if((loc_ts = _find_loc( app, app->insertLoc )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The time stamp associated with the 'End Loc' '%i' could not be found.",app->insertLoc);
        goto errLabel;
      }

      // create the data record associated with the new fragment.
      if((rc = preset_sel::create_fragment( app->psH, app->insertLoc, loc_ts->timestamp, fragId)) != kOkRC )
      {
        rc = cwLogError(rc,"Fragment data record create failed.");
        goto errLabel;
      }
      
      // create the fragment UI panel
      if((rc = _create_frag_ui( app, app->insertLoc, fragId )) != kOkRC )
      {
        rc = cwLogError(rc,"Fragment UI panel create failed.");
        goto errLabel;
      }
      
      
      // update the fragment UI
      _update_frag_ui(app, fragId );
      
      if((f = get_fragment(app->psH,fragId)) != nullptr && f->link != nullptr )
        _update_frag_ui(app, f->link->fragId );

      
      
    errLabel:
      return rc;
          
    }

    rc_t _on_ui_delete_btn( app_t* app )
    {
      rc_t     rc     = kOkRC;
      unsigned fragId = kInvalidId;
      unsigned uuId   = kInvalidId;

      // get the fragment id (uuid) of the selected fragment
      if((fragId = preset_sel::ui_select_fragment_id(app->psH)) == kInvalidId )
      {
        rc =  cwLogError(kInvalidStateRC,"There is no selected fragment to delete.");
        goto errLabel;
      }

      // locate the uuid assocated with the specified fragid
      if((uuId = preset_sel::frag_to_gui_id(app->psH,fragId)) == kInvalidId )
      {
        rc = cwLogError(kInvalidIdRC,"The uuId associated with the fragment id %i could not be found.",fragId);
        goto errLabel;
      }

      // delete the fragment data record
      if((rc = preset_sel::delete_fragment(app->psH,fragId)) != kOkRC )
        goto errLabel;

      // delete the fragment UI element
      if((rc = io::uiDestroyElement( app->ioH, uuId )) != kOkRC )
        goto errLabel;

      errLabel:
      
      if( rc != kOkRC )
        rc = cwLogError(rc,"Fragment delete failed.");
      
      return rc;
    }

    rc_t _onUiInit(app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;

      _update_event_ui(app);

      // disable start and stop buttons until a score is loaded
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStartBtnId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStopBtnId ),  false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSaveBtnId ),  false );
      
      const preset_sel::frag_t* f = preset_sel::get_fragment_base( app->psH );
      for(; f!=nullptr; f=f->link)
        _update_frag_ui( app, f->fragId );

      //_do_load(app);
      
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
          //preset_sel::report( app->psH );
          //io_flow::apply_preset( app->ioFlowH, 2000.0, app->tmp==0 ? "a" : "b");
          //app->tmp = !app->tmp;
          break;

        case kSaveBtnId:
          _on_ui_save(app);
          break;

        case kLoadBtnId:
          _do_load(app);
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

        case kBegPlayLocNumbId:
          _on_ui_play_loc(app, m.appId, m.value->u.i);
          break;
          
        case kEndPlayLocNumbId:
          _on_ui_play_loc(app, m.appId, m.value->u.i);
          break;

        case kInsertLocId:
          _on_ui_insert_loc(app, m.value->u.u );
          break;
          
        case kInsertBtnId:
          _on_ui_insert_btn(app);
          break;
          
        case kDeleteBtnId:
          _on_ui_delete_btn(app);
          break;

        case kFragInGainId:
          _on_ui_frag_value( app, m.uuId, m.value->u.d);          
          break;

        case kFragOutGainId:
          _on_ui_frag_value( app, m.uuId, m.value->u.d);          
          break;
          
        case kFragWetDryGainId:
          //printf("UI wet/dry:%f\n",m.value->u.d);
          _on_ui_frag_value( app, m.uuId, m.value->u.d );
          break;
          
        case kFragFadeOutMsId:
          _on_ui_frag_value( app, m.uuId, m.value->u.u );
          break;

        case kFragBegPlayLocId:
          _on_ui_frag_value( app, m.uuId, m.value->u.u );
          break;

        case kFragEndPlayLocId:
          _on_ui_frag_value( app, m.uuId, m.value->u.u );
          break;

        case kFragPlayBtnId:
          _on_ui_frag_value( app, m.uuId, m.value->u.b );
          break;

        case kFragNoteId:
          _on_ui_frag_value( app, m.uuId, m.value->u.s );
          break;
          
        case kFragPresetOrderId:
          _on_ui_frag_value( app, m.uuId, m.value->u.u );
          break;
            
        case kFragPresetSelId:
          _on_ui_frag_value( app, m.uuId, m.value->u.b );
          break;
      }

      return rc;
    }

    rc_t _onUiCorrupt( app_t* app, const io::ui_msg_t& m )
    {
      switch( m.appId )
      {
      case kBegPlayLocNumbId:
      case kEndPlayLocNumbId:
        _enable_global_play_btn(app,false);
        break;

      case kInsertLocId:
        io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kInsertBtnId ),  false );
        break;
        
      case kFragBegPlayLocId:
      case kFragEndPlayLocId:
        _disable_frag_play_btn(app, m.uuId );
        break;
        
      default:
        break;
      }

      return kOkRC;
    }

    rc_t _onUiClick( app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;

      // get the last selected fragment
      unsigned prevFragId = preset_sel::ui_select_fragment_id(app->psH);
      unsigned prevUuId   = preset_sel::frag_to_gui_id(app->psH,prevFragId,false);
      
      // is the last selected fragment the same as the clicked fragment
      bool reclickFl = prevUuId == m.uuId;

      // if a different fragment was clicked then deselect the last fragment in the UI
      if( !reclickFl )
      {
        if(prevUuId != kInvalidId )
          uiSetSelect(   app->ioH, prevUuId, false );

        // select or deselect the clicked fragment
        uiSetSelect(   app->ioH, m.uuId, !reclickFl );
      }
      
      // Note: calls to uiSetSelect() produce callbacks to _onUiSelect().

      return rc;
    }

    rc_t _onUiSelect( app_t* app, const io::ui_msg_t& m )
    {
      rc_t       rc       = kOkRC;
      bool       selectFl = m.value->u.b; // True/False if the fragment is being selected/deselected
      unsigned   fragId   = kInvalidId;
      
      if((fragId = preset_sel::gui_to_frag_id(app->psH,m.uuId)) == kInvalidId )
      {
        rc = cwLogError(kInvalidIdRC,"The fragment assoicated with the UuId %i could not be found.",m.uuId);
        goto errLabel;
      }
      
      // track the currently selected fragment.
      preset_sel::ui_select_fragment( app->psH, fragId, selectFl );

      // enable/disable the delete fragment button
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kDeleteBtnId ), selectFl );

    errLabel:
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

      case ui::kCorruptOpId:
        _onUiCorrupt( app, m );
        break;

      case ui::kClickOpId:
        _onUiClick( app, m );
        break;

      case ui::kSelectOpId:
        _onUiSelect( app, m );
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

      if( app->ioFlowH.isValid() )
      {
        io_flow::exec( app->ioFlowH, *m );
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

      default:
        assert(0);
        
      }

      return rc;
    }          
    
    
  }
}


cw::rc_t cw::preset_sel_app::main( const object_t* cfg, const object_t* flow_proc_dict )
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

  log::setOutputCb( log::globalHandle(),_log_output_func,&app);

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

  // create the IO Flow controller
  if(app.flow_cfg==nullptr || flow_proc_dict==nullptr || (rc = io_flow::create(app.ioFlowH,app.ioH,app.crossFadeSrate,app.crossFadeCnt,*flow_proc_dict,*app.flow_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The IO Flow controller create failed.");
    goto errLabel;
  }
  
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

  // stop the io framework
  if((rc = io::stop(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"IO API stop failed.");
    goto errLabel;    
  }
  
 errLabel:

  
  _free(app);
  io::destroy(app.ioH);
  printf("Preset-select Done.\n");
  return rc;
  
}
