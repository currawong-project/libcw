#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
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
      kNetPrintBtnId,
      kReportBtnId,
      
      kStartBtnId,
      kStopBtnId,
      kLiveCheckId,
      kTrackMidiCheckId,

      kPrintMidiCheckId,
      kPianoMidiCheckId,
      kSamplerMidiCheckId,
      kSyncDelayMsId,

      kWetInGainId,
      kWetOutGainId,
      kDryGainId,
      
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

      kPvWndSmpCntId,
      kSdBypassId,
      kSdInGainId,
      kSdCeilingId,
      kSdExpoId,
      kSdThreshId,
      kSdUprId,
      kSdLwrId,
      kSdMixId,
      kCmpBypassId,
      
      kAMtrIn0Id,
      kAMtrIn1Id,
      kAMtrOut0Id,
      kAMtrOut1Id,

      /*
      kHalfPedalPedalVel,
      kHalfPedalDelayMs,
      kHalfPedalPitch,
      kHalfPedalVel,
      kHalfPedalDurMs,
      kHalfPedalDnDelayMs,
      */
      
      kLogId,

      kFragListId,
      kFragPanelId,
      kFragMeasId,
      kFragBegLocId,
      kFragEndLocId,
      
      kFragPresetRowId,
      kFragPresetSelId,
      kFragPresetSeqSelId,
      kFragPresetOrderId,
      
      kFragInGainId,
      kFragOutGainId,
      kFragWetDryGainId,
      kFragFadeOutMsId,
      kFragBegPlayLocId,
      kFragEndPlayLocId,
      kFragPlayBtnId,
      kFragPlaySeqBtnId,
      kFragPlayAllBtnId,
      kFragNoteId
      
    };

    enum
    {
      kPiano_MRP_DevIdx  = 1,
      kSampler_MRP_DevIdx = 0
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
      { kPanelDivId,     kNetPrintBtnId,  "netPrintBtnId" },
      { kPanelDivId,     kReportBtnId,    "reportBtnId" },
        
      { kPanelDivId,     kStartBtnId,        "startBtnId" },
      { kPanelDivId,     kStopBtnId,         "stopBtnId" },
      { kPanelDivId,     kLiveCheckId,       "liveCheckId" },
      { kPanelDivId,     kTrackMidiCheckId,  "trackMidiCheckId" },

      { kPanelDivId,     kPrintMidiCheckId,  "printMidiCheckId" },
      { kPanelDivId,     kPianoMidiCheckId,  "pianoMidiCheckId" },
      { kPanelDivId,     kSamplerMidiCheckId,"samplerMidiCheckId" },
      { kPanelDivId,     kSyncDelayMsId,     "syncDelayMsId" },

      { kPanelDivId,     kWetInGainId,       "wetInGainId" },
      { kPanelDivId,     kWetOutGainId,      "wetOutGainId" },
      { kPanelDivId,     kDryGainId,         "dryGainId" },
      
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

      /*
      { kPanelDivId,     kHalfPedalPedalVel, "halfPedalPedalVelId" },
      { kPanelDivId,     kHalfPedalDelayMs,  "halfPedalDelayMsId"  },
      { kPanelDivId,     kHalfPedalPitch,    "halfPedalPitchId"    },
      { kPanelDivId,     kHalfPedalVel,      "halfPedalVelId"      },
      { kPanelDivId,     kHalfPedalDurMs,    "halfPedalDurMsId"    },
      { kPanelDivId,     kHalfPedalDnDelayMs, "halfPedalDnDelayMsId"    },
      */

      { kPanelDivId,     kPvWndSmpCntId, "pvWndSmpCntId" },
      { kPanelDivId,     kSdBypassId,    "sdBypassId" },
      { kPanelDivId,     kSdInGainId,    "sdInGainId" },
      { kPanelDivId,     kSdCeilingId,   "sdCeilingId" },
      { kPanelDivId,     kSdExpoId,      "sdExpoId" },
      { kPanelDivId,     kSdThreshId,    "sdThreshId" },
      { kPanelDivId,     kSdUprId,       "sdUprId" },
      { kPanelDivId,     kSdLwrId,       "sdLwrId" },
      { kPanelDivId,     kSdMixId,       "sdMixId" },
      { kPanelDivId,     kCmpBypassId,   "cmpBypassId" },
      
      { kPanelDivId,     kStatusId,        "statusId" },
      { kPanelDivId,     kAMtrIn0Id,       "aMtrIn0"  },
      { kPanelDivId,     kAMtrIn1Id,       "aMtrIn1"  },
      { kPanelDivId,     kAMtrOut0Id,      "aMtrOut0" },
      { kPanelDivId,     kAMtrOut1Id,      "aMtrOut1" },
      
      { kPanelDivId,     kLogId,          "logId" },

      { kPanelDivId,     kFragListId,       "fragListId"   },
      { kFragListId,     kFragPanelId,      "fragPanelId"  },
      { kFragPanelId,    kFragMeasId,       "fragMeasId"   },
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
      { kFragPanelId,    kFragPlaySeqBtnId, "fragPlaySeqBtnId" },
      { kFragPanelId,    kFragPlayAllBtnId, "fragPlayAllBtnId" },
      { kFragPanelId,    kFragNoteId,       "fragNoteId" },
      
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
      const char*     record_backup_dir;
      const char*     scoreFn;
      const object_t* midi_play_record_cfg;
      const object_t* presets_cfg;
      object_t*       flow_proc_dict;
      const object_t* flow_cfg;
      
      midi_record_play::handle_t  mrpH;

      score::handle_t scoreH;
      loc_map_t*      locMap;
      unsigned        locMapN;

      unsigned        insertLoc; // last valid insert location id received from the GUI
      
      unsigned        minLoc;
      unsigned        maxLoc;
      
      time::spec_t    beg_play_timestamp;
      time::spec_t    end_play_timestamp;
      
      unsigned        beg_play_loc;
      unsigned        end_play_loc;

      preset_sel::handle_t psH;
      io_flow::handle_t    ioFlowH;
      
      double   crossFadeSrate;
      unsigned crossFadeCnt;

      bool     printMidiFl;

      unsigned hpDelayMs;
      unsigned hpPedalVel;
      unsigned hpPitch;
      unsigned hpVel;
      unsigned hpDurMs;
      unsigned hpDnDelayMs;

      bool     seqActiveFl;  // true if the sequence is currently active (set by 'Play Seq' btn)
      unsigned seqStartedFl; // set by the first seq idle callback
      unsigned seqFragId;    // 
      unsigned seqPresetIdx; //

      bool     useLiveMidiFl;  // use incoming MIDI to drive program (otherwise use score file)
      bool     trackMidiFl;    // apply presets based on MIDI location (otherwise respond only to direct manipulation of fragment control)
      bool     audioFileSrcFl;

      unsigned	pvWndSmpCnt;
      bool	sdBypassFl;
      double	sdInGain;
      double	sdCeiling;
      double	sdExpo;
      double	sdThresh;
      double	sdUpr;
      double	sdLwr;
      double	sdMix;
      bool      cmpBypassFl;
      
    } app_t;

    rc_t _apply_command_line_args( app_t* app, int argc, const char* argv[] )
    {
      rc_t rc = kOkRC;
      
      for(int i=0; i<argc ; i+=2)
      {
        if( strcmp(argv[i],"record_fn")==0 )
        {
          app->record_fn = argv[i+1];
          goto found_fl;
        }
        
        if( strcmp(argv[i],"score_fn")==0 )
        {
          app->scoreFn = argv[i+1];
          goto found_fl;
        }

        if( strcmp(argv[i],"beg_play_loc")==0 )
        {
          string_to_number( argv[i+1], app->beg_play_loc );
          goto found_fl;
        }

        if( strcmp(argv[i],"end_play_loc")==0 )
        {
          string_to_number( argv[i+1], app->end_play_loc );
          goto found_fl;
        }

        rc = cwLogError(kSyntaxErrorRC,"The command line argument: '%s' was not recognized.",argv[i]);
        goto errLabel;

      found_fl:
        printf("Command line override '%s=%s' .\n",argv[i],argv[i+1]);
        
        
      }

    errLabel:
      return rc;
    }
    
    rc_t _parseCfg(app_t* app, const object_t* cfg, const object_t*& params_cfgRef, int argc, const char* argv[] )
    {
      rc_t rc = kOkRC;
      const char* flow_proc_dict_fn = nullptr;

      if((rc = cfg->getv( "params", params_cfgRef,
                          "flow",   app->flow_cfg)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App 'params' cfg record not found.");
        goto errLabel;
      }
        
      if((rc = params_cfgRef->getv( "record_dir",       app->record_dir,
                                    "record_fn",        app->record_fn,
                                    "record_fn_ext",    app->record_fn_ext,
                                    "score_fn",         app->scoreFn,
                                    "flow_proc_dict_fn",flow_proc_dict_fn,
                                    "midi_play_record", app->midi_play_record_cfg,
                                    "presets",          app->presets_cfg,
                                    "crossFadeSrate",   app->crossFadeSrate,
                                    "crossFadeCount",   app->crossFadeCnt,
                                    "beg_play_loc",     app->beg_play_loc,
                                    "end_play_loc",     app->end_play_loc)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App configuration parse failed.");
        goto errLabel;
      }

      _apply_command_line_args(app,argc,argv);

      if((app->scoreFn    = filesys::expandPath( app->scoreFn )) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The score file name is invalid.");
        goto errLabel;
      }
      
      if((app->record_dir = filesys::expandPath(app->record_dir)) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The record directory path is invalid.");
        goto errLabel;
      }

      if((app->record_backup_dir = filesys::makeFn(app->record_dir,"backup",nullptr,nullptr)) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The record backup directory path is invalid.");
        goto errLabel;
      }

      if((rc = objectFromFile( flow_proc_dict_fn, app->flow_proc_dict )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"The flow proc file '%s' parse failed.",app->flow_proc_dict);
        goto errLabel;
      }

      // verify that the output directory exists
      if((rc = filesys::makeDir(app->record_dir)) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to create the base output directory:%s.",cwStringNullGuard(app->record_dir));
        goto errLabel;
      }

      // verify that the output backup directory exists
      if((rc = filesys::makeDir(app->record_backup_dir)) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to create the output backup directory:%s.",cwStringNullGuard(app->record_backup_dir));
      }

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
      if( app.flow_proc_dict != nullptr )
        app.flow_proc_dict->free();
      
      mem::release((char*&)app.record_backup_dir);
      mem::release((char*&)app.record_dir);
      mem::release((char*&)app.scoreFn);
      preset_sel::destroy(app.psH);
      io_flow::destroy(app.ioFlowH);
      midi_record_play::destroy(app.mrpH);
      score::destroy( app.scoreH );
      mem::release(app.locMap);
      return kOkRC;
    }

    rc_t _apply_preset( app_t* app, const time::spec_t& ts, unsigned loc, const preset_sel::frag_t* frag=nullptr  )      
    {
      // if frag is NULL this is the beginning of a play session
      if( frag == nullptr )
      {
        preset_sel::track_loc_reset(app->psH);
        //preset_sel::track_timestamp( app->psH, ts, frag);
        
        preset_sel::track_loc( app->psH, loc, frag);
      }
      
      if( frag == nullptr )
        cwLogInfo("No preset fragment was found for the requested timestamp.");
      else
      {
        unsigned preset_idx;
        
        // if the preset sequence player is active then apply the next selected seq. preset
        // otherwise select the next primary preset for ths fragment
        unsigned seq_idx_n = app->seqActiveFl ? app->seqPresetIdx : kInvalidIdx;
        
        // get the preset index to play for this fragment
        if((preset_idx = fragment_play_preset_index(frag,seq_idx_n)) == kInvalidIdx )
          cwLogInfo("No preset has been assigned to the fragment at end loc. '%i'.",frag->endLoc );
        else
        {        
          const char* preset_label = preset_sel::preset_label(app->psH,preset_idx);
        
          _set_status(app,"Apply preset: '%s'.", preset_idx==kInvalidIdx ? "<invalid>" : preset_label);

          if( preset_label != nullptr )
          {
            io_flow::apply_preset( app->ioFlowH, flow_cross::kNextDestId, preset_label );

            io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "wet_in_gain", "gain", flow::kAnyChIdx, (dsp::real_t)frag->igain );
            io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "wet_out_gain","gain", flow::kAnyChIdx, (dsp::real_t)frag->ogain );
            io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "wd_bal",      "in",   flow::kAnyChIdx, (dsp::real_t)frag->wetDryGain );
            
            io_flow::begin_cross_fade( app->ioFlowH, frag->fadeOutMs );
          }
        }
      }

      return kOkRC;      
    }


    // Turn on the selection border for the clicked fragment
    rc_t _do_select_frag( app_t* app, unsigned clickedUuId )
    {
      rc_t rc = kOkRC;

      // get the last selected fragment
      unsigned prevFragId = preset_sel::ui_select_fragment_id(app->psH);
      unsigned prevUuId   = preset_sel::frag_to_gui_id(app->psH,prevFragId,false);
      
      // is the last selected fragment the same as the clicked fragment
      bool reclickFl = prevUuId == clickedUuId;

      // if a different fragment was clicked then deselect the last fragment in the UI
      if( !reclickFl )
      {
        if(prevUuId != kInvalidId )
          io::uiSetSelect(   app->ioH, prevUuId, false );

        // select or deselect the clicked fragment
        io::uiSetSelect(   app->ioH, clickedUuId, !reclickFl );
      }
      
      // Note: calls to uiSetSelect() produce callbacks to _onUiSelect().

      return rc;
    }

    void _midi_play_callback( void* arg, unsigned actionId, unsigned id, const time::spec_t timestamp, unsigned loc, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      app_t* app = (app_t*)arg;
      switch( actionId )
      {
        case midi_record_play::kPlayerStoppedActionId:
          app->seqStartedFl=false;
          _set_status(app,"Done");
          break;
          
        case midi_record_play::kMidiEventActionId:
        {
          if( app->printMidiFl )
          {
            const unsigned buf_byte_cnt = 256;
            char buf[ buf_byte_cnt ];
          
            // if this event is not in the score
            if( id == kInvalidId )
            {
              // TODO: print this out in the same format as event_to_string()
              snprintf(buf,buf_byte_cnt,"ch:%i status:0x%02x d0:%i d1:%i",ch,status,d0,d1);
            }
            else
              score::event_to_string( app->scoreH, id, buf, buf_byte_cnt );
            printf("%s\n",buf);
          }

          if( midi_record_play::is_started(app->mrpH) )
          {
            const preset_sel::frag_t* f = nullptr;
            //if( preset_sel::track_timestamp( app->psH, timestamp, f ) )


            // ZERO SHOULD BE A VALID LOC VALUE - MAKE -1 THE INVALID LOC VALUE
            
            if( loc != 0  && app->trackMidiFl )
            {  
              if( preset_sel::track_loc( app->psH, loc, f ) )  
              {          
                //printf("NEW FRAG: id:%i loc:%i\n", f->fragId, f->endLoc );
                _apply_preset( app, timestamp, loc, f );
                
                if( f != nullptr )
                  _do_select_frag( app, f->guiUuId );
              }
            }
          }
          break;
        }
      }
    }

    rc_t  _on_live_midi( app_t* app, const io::msg_t& msg )
    {
      rc_t rc = kOkRC;

      if( msg.u.midi != nullptr )
      {
        
        const io::midi_msg_t& m = *msg.u.midi;
        const midi::packet_t* pkt = m.pkt;
        // for each midi msg
        for(unsigned j=0; j<pkt->msgCnt; ++j)
        {
          // if this is a sys-ex msg
          if( pkt->msgArray == NULL )
          {
            cwLogError(kNotImplementedRC,"Sys-ex recording not implemented.");
          }
          else // this is a triple
          {
            midi::msg_t*  mm = pkt->msgArray + j;
            time::spec_t  timestamp;
            unsigned id = kInvalidId;
            unsigned loc = app->beg_play_loc;
            
            time::get(timestamp);
            
            if( midi::isChStatus(mm->status) )
            {
              if(midi_record_play::send_midi_msg( app->mrpH, kSampler_MRP_DevIdx, mm->status & 0x0f, mm->status & 0xf0, mm->d0, mm->d1 ) == kOkRC )                
                _midi_play_callback( app, midi_record_play::kMidiEventActionId, id, timestamp, loc, mm->status & 0x0f, mm->status & 0xf0, mm->d0, mm->d1 );
            }

          }
        }          
      }

      return rc;
    }
      

    // Find the closest locMap equal to or after 'loc'
    loc_map_t* _find_loc( app_t* app, unsigned loc )
    {
      unsigned i=0;
      loc_map_t* pre_loc_map = nullptr;
      
      for(; i<app->locMapN; ++i)
      {
        if( app->locMap[i].loc >= loc )
          return app->locMap +i;

        pre_loc_map = app->locMap + i;
           
      }
      
      return pre_loc_map;
    }

    rc_t _do_stop_play( app_t* app )
    {
      rc_t rc = kOkRC;

      if( app->audioFileSrcFl )
      {
        if((rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "aud_in",  "on_off",    flow::kAnyChIdx, false )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"Attempt to set audio in 'on/off' value to 'off' failed.");
          goto errLabel;
        }
      }
      
      if((rc = midi_record_play::stop(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI stop failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }
    
    
    rc_t _do_play( app_t* app, unsigned begLoc, unsigned endLoc )
    {
      rc_t rc = kOkRC;
      bool rewindFl = true;
      loc_map_t* begMap = nullptr;
      loc_map_t* endMap = nullptr;
      unsigned cur_loc = 0;

      // if the player is already playing then stop it
      if( midi_record_play::is_started(app->mrpH) )
      {
        rc = _do_stop_play(app);
        goto errLabel;
      }

      //midi_record_play::half_pedal_params( app->mrpH, app->hpDelayMs, app->hpPitch, app->hpVel, app->hpPedalVel, app->hpDurMs, app->hpDnDelayMs );

      if( app->audioFileSrcFl )
      {
        if((rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "aud_in",  "on_off",    flow::kAnyChIdx, true )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"Attempt to set audio in 'on/off' value to 'on' failed.");
          goto errLabel;
        }

        if((rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "aud_in",  "seekSecs",    flow::kAnyChIdx, 0.0f )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"Attempt to rewind audio file failed.");
          goto errLabel;
        }
      }
      
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

      
      if( !time::isZero(app->beg_play_timestamp) && !app->useLiveMidiFl )
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
      if((rc = _apply_preset( app, app->beg_play_timestamp, begMap->loc )) != kOkRC )
      {
        rc = cwLogError(rc,"Preset application failed prior to MIDI start.");
        goto errLabel;
      }

      // start the MIDI playback
      if( !app->useLiveMidiFl )
      {
        if((rc = midi_record_play::start(app->mrpH,rewindFl,&app->end_play_timestamp)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI start failed.");
          goto errLabel;
        }

        if((cur_loc = midi_record_play::event_loc(app->mrpH)) > 0 )
        {
          io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), cur_loc  );
        }
      }
      
    errLabel:
      return rc;
    }

    rc_t _apply_current_preset( app_t* app, unsigned fragId )
    {
      rc_t			rc		 = kOkRC;
      time::spec_t		ts		 = {0};
      const preset_sel::frag_t* frag		 = nullptr;
      bool			orig_seqActiveFl = app->seqActiveFl;

      app->seqActiveFl = false;

      if((frag = preset_sel::get_fragment( app->psH, fragId )) == nullptr )
      {
        rc = cwLogError(rc,"The fragment at id '%i' could not be accessed.",fragId);
        goto errLabel;
      }

      // apply the preset which is active at the start time
      if((rc = _apply_preset( app, ts, frag->begPlayLoc, frag )) != kOkRC )
      {
        rc = cwLogError(rc,"Preset application failed on fragment at id '%i'.",fragId);
        goto errLabel;        
      }

    errLabel:

      app->seqActiveFl = orig_seqActiveFl;
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

    rc_t _do_seq_play_fragment( app_t* app, unsigned fragId )
    {
      rc_t rc = kOkRC;
      
      if( app->seqActiveFl )
      {
        app->seqActiveFl = false;
      }
      else
      {
        app->seqFragId    = fragId;
        app->seqPresetIdx = 0;
        app->seqStartedFl = true;
        app->seqActiveFl  = true;        
      }

      // Note that if the MIDI player is already playing
      // calling '_do_play_fragment()' here will stop the player
      _do_play_fragment( app, app->seqFragId );

      return rc;
    }

    rc_t _do_seq_exec( app_t* app )
    {
      rc_t rc = kOkRC;
      
      // if the seq player is active but currently stopped
      if( app->seqActiveFl && app->seqStartedFl==false)
      {
        app->seqPresetIdx += 1;
        app->seqStartedFl = app->seqPresetIdx < preset_sel::fragment_seq_count( app->psH, app->seqFragId );
        app->seqActiveFl  = app->seqStartedFl;
        
        if( app->seqStartedFl )
          _do_play_fragment( app, app->seqFragId );
      }
      return rc;
    }
    
    void _update_event_ui( app_t* app )
    {
      //io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );
      //io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), midi_record_play::event_count(app->mrpH) );
      
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), app->maxLoc );
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
        unsigned uiChanId = fragId; //fragEndLoc;

        // Get the fragPanelUUid
        get_value( app->psH, fragId, preset_sel::kGuiUuIdVarId, kInvalidId, fragPanelUuId );

        // uuid of the frag preset row
        unsigned fragPresetRowUuId = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragPresetRowId, uiChanId );


        
        // Update each fragment preset control UI by getting it's current value from the fragment data record
        for(unsigned preset_idx=0; preset_idx<preset_sel::preset_count(app->psH); ++preset_idx)
        {
          _update_frag_ui( app, fragId, preset_sel::kPresetSelectVarId,   preset_idx, fragPresetRowUuId, kFragPresetSelId,    preset_idx,  bValue );          
          _update_frag_ui( app, fragId, preset_sel::kPresetOrderVarId,    preset_idx, fragPresetRowUuId, kFragPresetOrderId,  preset_idx,  uValue );          
          _update_frag_ui( app, fragId, preset_sel::kPresetSeqSelectVarId,preset_idx, fragPresetRowUuId, kFragPresetSeqSelId, preset_idx,  bValue );          
        }

        _apply_current_preset(app, fragId );
        
      }
      
      return rc;
    }

    // Update the fragment UI withh the fragment record associated with 'fragId'
    rc_t _update_frag_ui(app_t* app, unsigned fragId )
    {
      // Notes: 
      //        uiChanId = fragId for panel values     
      //     or uiChanId = preset_index for preset values

      rc_t     rc            = kOkRC;
      unsigned endLoc;

      // get the endLoc for this fragment 
      if((rc = _frag_id_to_endloc(app, fragId, endLoc )) == kOkRC )
      {
        unsigned uValue;
        double   dValue;
        const char* sValue;
        unsigned uiChanId      = fragId; //endLoc;
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
      bool     enableFl           = begPlayLoc < endPlayLoc;
      unsigned fragUuId           = kInvalidId;

      if((fragUuId = frag_to_gui_id( app->psH, blob->fragId )) != kInvalidId )
      {
        unsigned btnIdA[] = { kFragPlayBtnId, kFragPlaySeqBtnId, kFragPlayAllBtnId };
        unsigned btnIdN   = sizeof(btnIdA)/sizeof(btnIdA[0]);

        for(unsigned i=0; i<btnIdN; ++i)
        {
          unsigned btnUuId;
          if((btnUuId =  uiFindElementUuId( app->ioH, fragUuId, btnIdA[i], blob->presetId )) != kInvalidId )
            uiSetEnable( app->ioH, btnUuId, enableFl );

        }
      }

      if( enableFl )
        _clear_status(app);
      else
        _set_status(app,"Invalid fragment play range.");

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

        case preset_sel::kPresetSeqSelectVarId:
          _update_frag_select_flags( app, blob->fragId);
          break;
          
          
        case preset_sel::kPlayBtnVarId:
          _do_play_fragment( app, blob->fragId );
          break;

        case preset_sel::kPlaySeqBtnVarId:
          _do_seq_play_fragment( app, blob->fragId );
          break;

        case preset_sel::kPlaySeqAllBtnVarId:
          _do_seq_play_fragment( app, blob->fragId );
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

      // store a connection for the order control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetOrderVarId, preset_idx );

      // preset sequence select check
      if((rc = io::uiCreateCheck( app->ioH, uuId, colUuId, nullEleName, kFragPresetSeqSelId, chanId, nullClass, nullptr )) != kOkRC )
        goto errLabel;

      // store a connection for the sequence select control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetSeqSelectVarId, preset_idx );

    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Preset control index '%i' create failed.");
      return rc;
    }

    rc_t _create_frag_ui( app_t* app, unsigned endLoc, unsigned fragId )
    {
      rc_t       rc                = kOkRC;
      unsigned   fragListUuId      = io::uiFindElementUuId( app->ioH, kFragListId );
      unsigned   fragChanId        = fragId; //endLoc; // use the frag. endLoc as the channel id
      unsigned   fragPanelUuId     = kInvalidId;
      unsigned   fragBegLocUuId    = kInvalidId;
      unsigned   fragEndLocUuId    = kInvalidId;
      unsigned   fragPresetRowUuId = kInvalidId;
      unsigned   presetN           = preset_sel::preset_count( app->psH );
      unsigned   fragBegLoc        = 0;

      // create the UI object
      if((rc = io::uiCreateFromRsrc( app->ioH, "frag_panel",  fragListUuId, fragChanId )) != kOkRC )
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
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragInGainId,     fragChanId), fragId, preset_sel::kInGainVarId,        kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragOutGainId,    fragChanId), fragId, preset_sel::kOutGainVarId,       kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragWetDryGainId, fragChanId), fragId, preset_sel::kWetGainVarId,       kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragFadeOutMsId,  fragChanId), fragId, preset_sel::kFadeOutMsVarId,     kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragBegPlayLocId, fragChanId), fragId, preset_sel::kBegPlayLocVarId,    kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragEndPlayLocId, fragChanId), fragId, preset_sel::kEndPlayLocVarId,    kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragPlayBtnId,    fragChanId), fragId, preset_sel::kPlayBtnVarId,       kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragPlaySeqBtnId, fragChanId), fragId, preset_sel::kPlaySeqBtnVarId,    kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragPlayAllBtnId, fragChanId), fragId, preset_sel::kPlaySeqAllBtnVarId, kInvalidId );
      _frag_set_ui_blob(app, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragNoteId,       fragChanId), fragId, preset_sel::kNoteVarId,          kInvalidId );
      
      
      // create each of the preset controls
      for(unsigned preset_idx=0; preset_idx<presetN; ++preset_idx)
        if((rc = _create_frag_preset_ctl(app, fragId, fragPresetRowUuId, presetN, preset_idx )) != kOkRC )
          goto errLabel;

      // set the uuid associated with this fragment
      preset_sel::set_value( app->psH, fragId, preset_sel::kGuiUuIdVarId, kInvalidId, fragPanelUuId );

    errLabel:
      return rc;
      
    }
    
    
    rc_t _restore_fragment_data( app_t* app )
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

      //preset_sel::report( app->psH );

      f = preset_sel::get_fragment_base(app->psH);
      for(int i=0; f!=nullptr; f=f->link,++i)
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
      if((fn = filesys::makeFn(app->record_dir, app->record_fn, app->record_fn_ext, nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The preset select filename could not formed.");
        goto errLabel;
      }

      // backup the current output file to versioned copy
      if((rc = file::backup( app->record_dir, app->record_fn, app->record_fn_ext, app->record_backup_dir )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The preset select backup to '%s' failed.",cwStringNullGuard(app->record_backup_dir));
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

    int _compare_loc_map( const void* m0, const void* m1 )
    { return ((const loc_map_t*)m0)->loc - ((const loc_map_t*)m1)->loc; }

    rc_t _load_piano_score( app_t* app, unsigned& midiEventCntRef )
    {
      rc_t                          rc         = kOkRC;
      const score::event_t*         e          = nullptr;
      unsigned                      midiEventN = 0;      
      midi_record_play::midi_msg_t* m          = nullptr;

      midiEventCntRef = 0;
      
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
            m[i].loc    = e->loc;

            app->locMap[i].loc       = e->loc;
            app->locMap[i].timestamp = m[i].timestamp;

            app->minLoc = std::min(app->minLoc,e->loc);
            app->maxLoc = std::max(app->maxLoc,e->loc);
            
            ++i;
          }

        qsort( app->locMap, app->locMapN, sizeof(loc_map_t), _compare_loc_map );
        
        // load the player with the msg list
        if((rc = midi_record_play::load( app->mrpH, m, midiEventN )) != kOkRC )
        {
          cwLogError(rc,"MIDI player load failed.");
          goto errLabel;
        }

        cwLogInfo("%i MIDI events loaded from score.", midiEventN );

        mem::free(m);        
      }

    errLabel:

      midiEventCntRef = midiEventN;
      
      
      return rc;
      
    }


    rc_t _do_load( app_t* app )
    {
      rc_t     rc          = kOkRC;
      unsigned midiEventN  = 0;
      bool     firstLoadFl = !app->scoreH.isValid();

      cwLogInfo("Loading");
      _set_status(app,"Loading...");


      // Load the piano score (this set's app->min/maxLoc)
      if((rc = _load_piano_score(app,midiEventN)) != kOkRC )
        goto errLabel;

      
      // reset the timestamp tracker
      track_loc_reset( app->psH );
      
      // set the range of the global play location controls
      if( firstLoadFl )
      {
        unsigned begPlayLocUuId = io::uiFindElementUuId(app->ioH, kBegPlayLocNumbId);
        unsigned endPlayLocUuId = io::uiFindElementUuId(app->ioH, kEndPlayLocNumbId);

        if( app->end_play_loc == 0 )
          app->end_play_loc = app->maxLoc;

        if( !(app->minLoc <= app->beg_play_loc && app->beg_play_loc <= app->maxLoc) )
          app->beg_play_loc = app->minLoc;
        
        io::uiSetNumbRange( app->ioH, begPlayLocUuId, app->minLoc, app->maxLoc, 1, 0, app->beg_play_loc );
        io::uiSetNumbRange( app->ioH, endPlayLocUuId, app->minLoc, app->maxLoc, 1, 0, app->end_play_loc );

        io::uiSendValue( app->ioH, begPlayLocUuId, app->beg_play_loc);
        io::uiSendValue( app->ioH, endPlayLocUuId, app->end_play_loc);

        // enable the insert 'End Loc' number box since the score is loaded
        io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kInsertLocId ), true );

      }
      
      // update the current event and event count
      _update_event_ui(app);

      // enable the start/stop buttons
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStartBtnId ), true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStopBtnId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLiveCheckId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kTrackMidiCheckId ),  true );
        
      // restore the fragment records
      if( firstLoadFl )
        if((rc = _restore_fragment_data( app )) != kOkRC )
        {
          rc = cwLogError(rc,"Restore failed.");
          goto errLabel;
        }

      //io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLoadBtnId ), true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSaveBtnId ), true );
      
      
      cwLogInfo("'%s' loaded.",app->scoreFn);

    errLabel:
 
      _update_event_ui( app );

      if( rc != kOkRC )
        _set_status(app,"Load failed.");
      else
        _set_status(app,"%i MIDI events loaded.",midiEventN);

      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLoadBtnId ), false );

      return rc;
    }

    rc_t _on_ui_start( app_t* app )
    {
      return _do_play(app, app->beg_play_loc, app->end_play_loc );
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
      // the minimum possible value for the insert location is 1 because the previous end loc must be
      // less than the insert location (BUT what if we are inserting location 0???? A: loc 0 is invalid
      //  ???? FIX the loc values1 ....
      if( loc < 1 )
        return false;
      
      bool fl0 = _find_loc(app,loc) != nullptr;
      bool fl1 = preset_sel::is_fragment_end_loc( app->psH, loc-1)==false;

      return fl0 && fl1;
    }


    // Called when the global play locations change
    rc_t _on_ui_play_loc(app_t* app, unsigned appId, unsigned loc)
    {
      rc_t       rc = kOkRC;

      // verify that the location exists
      if( _find_loc(app,loc) == nullptr )
        _set_status(app,"%i is an invalid location.",loc);
      else
      {
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
      }
      
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
      rc_t                      rc      = kOkRC;
      unsigned                  fragId  = kInvalidId;
      loc_map_t*                loc_ts  = nullptr;
      const preset_sel::frag_t* f       = nullptr;
      unsigned                  end_loc = app->insertLoc - 1;
      

      // verify that the insertion location is valid
      if( app->insertLoc == kInvalidId )
      {
        rc = cwLogError(kInvalidIdRC,"The new fragment's 'End Loc' is not valid.");
        goto errLabel;
      }

      // verify that the end-loc is not already in use - this shouldn't be possible because the 'insert' btn should be disabled if the 'insertLoc' is not valid
      if( preset_sel::is_fragment_end_loc( app->psH, end_loc ) )
      {
        rc = cwLogError(kInvalidIdRC,"The new fragment's 'End Loc' is already in use.");
        goto errLabel;
      }

      // get the timestamp assoc'd with the the 'end-loc'
      if((loc_ts = _find_loc( app, end_loc )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The time stamp associated with the 'End Loc' '%i' could not be found.",app->insertLoc-1);
        goto errLabel;
      }

      // create the data record associated with the new fragment.
      if((rc = preset_sel::create_fragment( app->psH, end_loc, loc_ts->timestamp, fragId)) != kOkRC )
      {
        rc = cwLogError(rc,"Fragment data record create failed.");
        goto errLabel;
      }
      
      // create the fragment UI panel
      if((rc = _create_frag_ui( app, end_loc, fragId )) != kOkRC )
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
      rc_t                      rc     = kOkRC;
      unsigned                  fragId = kInvalidId;
      unsigned                  uuId   = kInvalidId;
      const preset_sel::frag_t* f      = nullptr;;

      // get the fragment id (uuid) of the selected (high-lighted) fragment
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

      // get a pointer to the fragment prior to the one to be deleted
      if((f = get_fragment(app->psH,fragId)) != nullptr )
        f = f->prev;

      // delete the fragment data record
      if((rc = preset_sel::delete_fragment(app->psH,fragId)) != kOkRC )
        goto errLabel;

      // delete the fragment UI element
      if((rc = io::uiDestroyElement( app->ioH, uuId )) != kOkRC )
        goto errLabel;

      // update the fragment prior to deleted fragment
      if(f != nullptr )
        _update_frag_ui(app, f->fragId );
      
      errLabel:
      
      if( rc != kOkRC )
        rc = cwLogError(rc,"Fragment delete failed.");
      else
        cwLogInfo("Fragment %i deleted.",fragId);

      return rc;
    }

    void _on_echo_midi_enable( app_t* app, unsigned uuId,  unsigned mrp_dev_idx )
    {
      if( mrp_dev_idx <= midi_record_play::device_count(app->mrpH)  )
      {
        bool     enableFl = midi_record_play::is_device_enabled(app->mrpH, mrp_dev_idx );

        io::uiSendValue( app->ioH, uuId, enableFl );
      }
    }

    void _on_midi_enable( app_t* app, unsigned checkAppId, unsigned mrp_dev_idx, bool enableFl )
    {
      unsigned midi_dev_n = midi_record_play::device_count(app->mrpH);
      
      if(  mrp_dev_idx < midi_dev_n  )
        midi_record_play::enable_device(app->mrpH, mrp_dev_idx, enableFl );
      else
        cwLogError(kInvalidArgRC,"%i is not a valid MIDI device index for device count:%i.",mrp_dev_idx,midi_dev_n);
    }
    
    rc_t _on_echo_master_value( app_t* app, unsigned varId, unsigned uuId )
    {
      rc_t rc = kOkRC;
      double val = 0;
      if((rc = get_value( app->psH, kInvalidId, varId, kInvalidId, val )) != kOkRC )
        rc = cwLogError(rc,"Unable to get the master value for var id:%i.",varId);
      else
        io::uiSendValue( app->ioH, uuId, val );
                        
      return rc;
    }
    
    rc_t _on_master_value( app_t* app, const char* inst_label, const char* var_label, unsigned varId, double value )
    {
      rc_t rc = kOkRC;
      if((rc = preset_sel::set_value( app->psH, kInvalidId, varId, kInvalidId, value )) != kOkRC )
        rc = cwLogError(rc,"Master value set failed on varId:%i %s.%s.",varId,cwStringNullGuard(inst_label),cwStringNullGuard(var_label));
      else
        if((rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, inst_label,  var_label,    flow::kAnyChIdx, (dsp::real_t)value )) != kOkRC )
            rc = cwLogError(rc,"Master value send failed on %s.%s.",cwStringNullGuard(inst_label),cwStringNullGuard(var_label));
      
      return rc;
    }

    rc_t  _on_master_audio_meter( app_t* app, const io::msg_t& msg )
    {
      io::audio_group_dev_t* agd = msg.u.audioGroupDev;
      unsigned n = std::min(agd->chCnt,2U);
      unsigned baseUiAppId = cwIsFlag(agd->flags,io::kInFl) ? kAMtrIn0Id : kAMtrOut0Id;
      
      for(unsigned i=0; i<n; ++i)
      {
	unsigned uuid = io::uiFindElementUuId( app->ioH, baseUiAppId+i );

	double lin_val = agd->meterA[i];
	unsigned meter_value = (unsigned)(lin_val < 1e-5 ? 0  : (100.0 + 20*log10(lin_val)));

	io::uiSendValue( app->ioH, uuid, meter_value );
      }
      
      return kOkRC;
    }

    rc_t _on_sd_control( app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;
      unsigned uuid = io::uiFindElementUuId( app->ioH, m.appId );
      const char* var_label = nullptr;
      assert(uuid != kInvalidId);
      
      switch( m.appId )
      {
	case kPvWndSmpCntId:
	  var_label="wndSmpN";
	  app->pvWndSmpCnt = m.value->u.u;
	  rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "pva",  var_label,    flow::kAnyChIdx, m.value->u.u );
	  break;
	  
	case kSdBypassId:
	  var_label="bypass";
	  app->sdBypassFl = m.value->u.b;
	  rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "sd",  var_label,    flow::kAnyChIdx, m.value->u.b );
	  break;
	  
	case kSdInGainId:
	  var_label = "igain";
	  app->sdInGain = m.value->u.d;
	  break;
	  
	case kSdCeilingId:
	  var_label = "ceiling";
	  app->sdCeiling = m.value->u.d;
	  break;
	  
	case kSdExpoId:
	  var_label = "expo";
	  app->sdExpo = m.value->u.d;
	  break;
	  
	case kSdThreshId:
	  var_label = "thresh";
	  app->sdThresh = m.value->u.d;
	  break;
	  
	case kSdUprId:
	  var_label = "upr";
	  app->sdUpr = m.value->u.d;
	  break;
	  
	case kSdLwrId:
	  var_label = "lwr";
	  app->sdLwr = m.value->u.d;
	  break;
	  
	case kSdMixId:
	  var_label = "mix";
	  app->sdMix = m.value->u.d;
	  break;

	case kCmpBypassId:
	  var_label="cmp-bypass";
	  app->cmpBypassFl = m.value->u.b;
	  rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "cmp",  "bypass",    flow::kAnyChIdx, m.value->u.b );
	  break;
	  
	default:
	  assert(0);
      }

      if( m.value->tid == ui::kDoubleTId )
	rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "sd",  var_label,    flow::kAnyChIdx, (dsp::real_t)m.value->u.d );

      if(rc != kOkRC )
	rc = cwLogError(rc,"Attempt to set a spec-dist variable '%s'",var_label );

      return rc;
    }


    /*
    rc_t _on_ui_half_pedal_value( app_t* app, unsigned appId, unsigned uuId, unsigned value )
    {
      switch( appId )
      {
        case kHalfPedalDelayMs:
          app->hpDelayMs = value;
          break;
          
        case  kHalfPedalPedalVel:
          app->hpPedalVel = value;
          break;
          
        case kHalfPedalPitch:
          app->hpPitch = value;
          break;
          
        case kHalfPedalVel:
          app->hpVel = value;
          break;
          
        case kHalfPedalDurMs:
          app->hpDurMs = value;
          break;
          
        case kHalfPedalDnDelayMs:
          app->hpDnDelayMs = value;
          break;

        default:
          { assert(0); }
          
      }
      return kOkRC;
    }
    
    rc_t _on_echo_half_pedal( app_t* app, unsigned appId, unsigned uuId )
    {
      switch( appId )
      {
        case kHalfPedalDelayMs:
          io::uiSendValue( app->ioH, uuId, app->hpDelayMs );
          break;
          
        case  kHalfPedalPedalVel:
          io::uiSendValue( app->ioH, uuId, app->hpPedalVel );
          break;
          
        case kHalfPedalPitch:
          io::uiSendValue( app->ioH, uuId, app->hpPitch );
          break;
          
        case kHalfPedalVel:
          io::uiSendValue( app->ioH, uuId, app->hpVel );
          break;
          
        case kHalfPedalDurMs:
          io::uiSendValue( app->ioH, uuId, app->hpDurMs );
          break;

        case kHalfPedalDnDelayMs:
          io::uiSendValue( app->ioH, uuId, app->hpDnDelayMs );
          break;
          
        default:
          { assert(0); }
          
      }
      return kOkRC;      
    }
    */
    
    rc_t _onUiInit(app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;

      _update_event_ui(app);

      // disable start and stop buttons until a score is loaded
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStartBtnId ),  false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStopBtnId ),   false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLiveCheckId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kTrackMidiCheckId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSaveBtnId ),   false );
      
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

        case kNetPrintBtnId:
          io_flow::print_network(app->ioFlowH,flow_cross::kCurDestId);
          break;
            
        case kReportBtnId:
          //preset_sel::report( app->psH );
          //io_flow::apply_preset( app->ioFlowH, 2000.0, app->tmp==0 ? "a" : "b");
          //app->tmp = !app->tmp;
          //io_flow::print(app->ioFlowH);
          io_flow::report(app->ioFlowH);
          //midi_record_play::save_csv(app->mrpH,"/home/kevin/temp/mrp_1.csv");
          //printf("%i %i\n",app->beg_play_loc,app->end_play_loc);
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
          _do_stop_play(app);
          break;

	case kLiveCheckId:
          app->useLiveMidiFl = m.value->u.b;
          break;

	case kTrackMidiCheckId:
	  app->trackMidiFl = m.value->u.b;
	  break;
	  
        case kPrintMidiCheckId:
          app->printMidiFl = m.value->u.b;
          break;
          
        case kPianoMidiCheckId:
          _on_midi_enable( app, m.appId, kPiano_MRP_DevIdx, m.value->u.b );
          break;

        case kSamplerMidiCheckId:
          _on_midi_enable( app, m.appId, kSampler_MRP_DevIdx, m.value->u.b );
          break;

        case kWetInGainId:
          _on_master_value( app, "mstr_wet_in_gain","gain", preset_sel::kMasterWetInGainVarId, m.value->u.d );
          break;
            
        case kWetOutGainId:
          _on_master_value( app, "mstr_wet_out_gain","gain",preset_sel::kMasterWetOutGainVarId, m.value->u.d );
          break;
          
        case kDryGainId:
          _on_master_value( app, "mstr_dry_out_gain", "gain", preset_sel::kMasterDryGainVarId, m.value->u.d );
          break;

        case kSyncDelayMsId:
          _on_master_value( app, "sync_delay","delayMs",preset_sel::kMasterSyncDelayMsVarId, (double)m.value->u.i );          
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

	case kPvWndSmpCntId:
	case kSdBypassId:
	case kSdInGainId:
	case kSdCeilingId:
	case kSdExpoId:
	case kSdThreshId:
	case kSdUprId:
	case kSdLwrId:
	case kSdMixId:
	case kCmpBypassId:
	  _on_sd_control(app,m);
	  break;
	  
          /*
        case kHalfPedalPedalVel:
        case kHalfPedalDelayMs:
        case kHalfPedalPitch:
        case kHalfPedalVel:
        case kHalfPedalDurMs:
        case kHalfPedalDnDelayMs:
          _on_ui_half_pedal_value( app, m.appId, m.uuId, m.value->u.u );
          break;
          */
          
        case kFragInGainId:
          _on_ui_frag_value( app, m.uuId, m.value->u.d);          
          break;

        case kFragOutGainId:
          _on_ui_frag_value( app, m.uuId, m.value->u.d);          
          break;
          
        case kFragWetDryGainId:
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
          
        case kFragPlaySeqBtnId:
          _on_ui_frag_value( app, m.uuId, m.value->u.b );
          break;

        case kFragPlayAllBtnId:
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

        case kFragPresetSeqSelId:
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
      switch( m.appId )
      {
        case kPrintMidiCheckId:
          break;
          
        case kPianoMidiCheckId:
          _on_echo_midi_enable( app, m.uuId, kPiano_MRP_DevIdx );
          break;
          
        case kSamplerMidiCheckId:
          _on_echo_midi_enable( app, m.uuId, kSampler_MRP_DevIdx );
          break;
          
        case kWetInGainId:
          _on_echo_master_value( app, preset_sel::kMasterWetInGainVarId, m.uuId );
          break;
          
        case kWetOutGainId:
          _on_echo_master_value( app, preset_sel::kMasterWetOutGainVarId, m.uuId );
          break;
          
        case kDryGainId:
          _on_echo_master_value( app, preset_sel::kMasterDryGainVarId, m.uuId );
          break;
          
        case kSyncDelayMsId:
          _on_echo_master_value( app, preset_sel::kMasterSyncDelayMsVarId, m.uuId );
          break;

        case kBegPlayLocNumbId:
          io::uiSendValue( app->ioH, m.uuId, app->beg_play_loc );
          break;
          
        case kEndPlayLocNumbId:
          io::uiSendValue( app->ioH, m.uuId, app->end_play_loc );
          break;

	case kLiveCheckId:
	  io::uiSendValue( app->ioH, m.uuId, app->useLiveMidiFl );
	  break;
	  
	case kTrackMidiCheckId:
	  io::uiSendValue( app->ioH, m.uuId, app->trackMidiFl );
	  break;

	case kPvWndSmpCntId:
	  io::uiSendValue( app->ioH, m.uuId, app->pvWndSmpCnt );
	  break;

	case kSdBypassId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdBypassFl );
	  break;
	  
	case kSdInGainId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdInGain );
	  break;
	  
	case kSdCeilingId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdCeiling );
	  break;
	  
	case kSdExpoId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdExpo );
	  break;
	  
	case kSdThreshId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdThresh );
	  break;
	  
	case kSdUprId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdUpr );
	  break;
	  
	case kSdLwrId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdLwr );
	  break;
	  
	case kSdMixId:
	  io::uiSendValue( app->ioH, m.uuId, app->sdMix );
	  break;
	  
	case kCmpBypassId:
	  io::uiSendValue( app->ioH, m.uuId, app->cmpBypassFl );
	  break;

	  
          /*
        case kHalfPedalPedalVel:
        case kHalfPedalDelayMs:
        case kHalfPedalPitch:
        case kHalfPedalVel:
        case kHalfPedalDurMs:
        case kHalfPedalDnDelayMs:
          _on_echo_half_pedal( app, m.appId, m.uuId );
          break;
          */
          
      }
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
        _do_select_frag( app, m.uuId );
        break;

      case ui::kSelectOpId:
        _onUiSelect( app, m );
        break;
        
      case ui::kEchoOpId:
        _onUiEcho( app, m );
        break;

      case ui::kIdleOpId:
        _do_seq_exec( app );
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

      if( app->mrpH.isValid() && !app->useLiveMidiFl )
      {
        midi_record_play::exec( app->mrpH, *m );
        if( midi_record_play::is_started(app->mrpH) )
        {
          unsigned cur_loc = midi_record_play::event_loc(app->mrpH);
          if( cur_loc > 0 )
          {
            io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), cur_loc );
          }
          
        }
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
        if( app->useLiveMidiFl && app->mrpH.isValid() )
          _on_live_midi( app, *m );
        break;
          
      case io::kAudioTId:        
        break;

      case io::kAudioMeterTId:
	_on_master_audio_meter(app, *m );
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


cw::rc_t cw::preset_sel_app::main( const object_t* cfg, int argc, const char* argv[] )
{

  rc_t rc;
  app_t app = { .hpDelayMs=250, .hpPedalVel=127, .hpPitch=64, .hpVel=64, .hpDurMs=500, .hpDnDelayMs=1000,
		.trackMidiFl = true,
		.pvWndSmpCnt = 512,
		.sdBypassFl  = false,
		.sdInGain    = 1.0,
		.sdCeiling   = 20.0,
		.sdExpo	     = 2.0,
		.sdThresh    = 60.0,
		.sdUpr	     = -1.1,
		.sdLwr	     = 2.0,
		.sdMix	     = 0.0,
		.cmpBypassFl = false
		
  };
  const object_t* params_cfg = nullptr;
  
  // Parse the configuration
  if((rc = _parseCfg(&app,cfg,params_cfg,argc,argv)) != kOkRC )
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
  if((rc = midi_record_play::create(app.mrpH,app.ioH,*app.midi_play_record_cfg,_midi_play_callback,&app)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI record-play object create failed.");
    goto errLabel;
  }
  
  // create the IO Flow controller
  if(app.flow_cfg==nullptr || app.flow_proc_dict==nullptr || (rc = io_flow::create(app.ioFlowH,app.ioH,app.crossFadeSrate,app.crossFadeCnt,*app.flow_proc_dict,*app.flow_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The IO Flow controller create failed.");
    goto errLabel;
  }
  
  // start the IO framework instance
  if((rc = io::start(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"Preset-select app start failed.");
    goto errLabel;    
  }

  
  // execute the io framework
  while( !io::isShuttingDown(app.ioH))
  {
    io::exec(app.ioH);
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
