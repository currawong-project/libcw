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
#include "cwMidiFile.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwScoreFollowerPerf.h"
#include "cwIoMidiRecordPlay.h"
#include "cwIoPresetSelApp.h"
#include "cwVectOps.h"
#include "cwMath.h"
#include "cwDspTypes.h"
#include "cwMtx.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
#include "cwFlowCross.h"
#include "cwIoFlow.h"
#include "cwPresetSel.h"
#include "cwVelTableTuner.h"
#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfTrack.h"
#include "cwPerfMeas.h"
#include "cwPianoScore.h"
#include "cwScoreFollower.h"


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
      kIoRtReportBtnId,
      kPresetReportBtnId,
      kMRP_ReportBtnId,
      kNetPrintBtnId,
      kReportBtnId,
      kLatencyBtnId,

      
      kStartBtnId,
      kStopBtnId,
      kGotoBtnId,
      kBegPlayLocNumbId,
      kEndPlayLocNumbId,
      kLockLoctnCheckId,      
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
      kPerfSelId,
      kAltSelId,
      
      kPriPresetProbCheckId,
      kSecPresetProbCheckId,
      kPresetInterpCheckId,
      kPresetAllowAllCheckId,
      kPresetDryPriorityCheckId,
      kPresetDrySelectedCheckId,

      
      kEnaRecordCheckId,
      kMidiSaveBtnId,
      kMidiLoadBtnId,
      kMidiLoadFnameId,
      kSfResetBtnId,
      kSfResetLocNumbId,


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
      kFragPresetAltId,
      
      kFragInGainId,
      kFragOutGainId,
      kFragWetDryGainId,
      kFragFadeOutMsId,
      kFragBegPlayLocId,
      kFragEndPlayLocId,
      kFragPlayBtnId,
      kFragPlaySeqBtnId,
      kFragPlayAllBtnId,
      kFragNoteId,

      kVelTblMinId = vtbl::kVtMinId,
      kVelTblMaxId = vtbl::kVtMaxId,

      kPerfOptionBaseId = kVelTblMaxId + 1,
    };

    
    // Application Id's for the resource based UI elements.
    ui::appIdMap_t mapA[] =
    {
      { ui::kRootAppId,  kPanelDivId,     "panelDivId" },
      { kPanelDivId,     kQuitBtnId,      "quitBtnId" },
      { kPanelDivId,     kIoReportBtnId,  "ioReportBtnId" },
      { kPanelDivId,     kIoRtReportBtnId,"ioRtReportBtnId" },
      { kPanelDivId,     kNetPrintBtnId,  "netPrintBtnId" },
      { kPanelDivId,     kReportBtnId,    "reportBtnId" },
      { kPanelDivId,     kPresetReportBtnId, "presetReportBtnId" },
      { kPanelDivId,     kMRP_ReportBtnId, "MRP_ReportBtnId" },      
      { kPanelDivId,     kLatencyBtnId,   "latencyBtnId" },
        
      { kPanelDivId,     kStartBtnId,        "startBtnId" },
      { kPanelDivId,     kStopBtnId,         "stopBtnId" },
      { kPanelDivId,     kGotoBtnId,         "gotoBtnId" },
      
      { kPanelDivId,     kBegPlayLocNumbId,  "begLocNumbId" },
      { kPanelDivId,     kEndPlayLocNumbId,  "endLocNumbId" },      
      { kPanelDivId,     kLockLoctnCheckId,  "locLoctnCheckId" },
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
      { kPanelDivId,     kPerfSelId,      "perfSelId" },
      { kPanelDivId,     kAltSelId,       "altSelId" },
      { kPanelDivId,     kPriPresetProbCheckId,  "presetProbPriCheckId" },
      { kPanelDivId,     kSecPresetProbCheckId,  "presetProbSecCheckId" },
      { kPanelDivId,     kPresetInterpCheckId,   "presetInterpCheckId" },
      { kPanelDivId,     kPresetAllowAllCheckId, "presetAllowAllCheckId" },
      { kPanelDivId,     kPresetDryPriorityCheckId, "presetDryPriorityCheckId" },
      { kPanelDivId,     kPresetDrySelectedCheckId, "presetDrySelectedCheckId" },
      
      

      { kPanelDivId,     kEnaRecordCheckId,  "enaRecordCheckId" },
      { kPanelDivId,     kMidiSaveBtnId,     "midiSaveBtnId" },
      { kPanelDivId,     kMidiLoadBtnId,     "midiLoadBtnId" },
      { kPanelDivId,     kMidiLoadFnameId,   "midiLoadFnameId" },
      { kPanelDivId,     kSfResetBtnId,      "sfResetBtnId" },
      { kPanelDivId,     kSfResetLocNumbId,  "sfResetLocNumbId" },


      { kPanelDivId,     kInsertLocId,    "insertLocId" },
      { kPanelDivId,     kInsertBtnId,    "insertBtnId" },
      { kPanelDivId,     kDeleteBtnId,    "deleteBtnId" },

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


    typedef struct vel_tbl_str
    {
      const char* name;
      const char* device;
    } vel_tbl_t;
    
    typedef struct perf_recording_str
    {
      char*                      fname;     // perf recording
      unsigned                   sess_numb;
      unsigned                   take_numb;
      unsigned                   beg_loc;
      unsigned                   end_loc;
      char*                      label;     // menu label
      unsigned                   id;        // menu appId
      unsigned                   uuId;      // menu uuid
      vel_tbl_t*                 vel_tblA;  // vel_tblA[ velTblN ] 
      unsigned                   vel_tblN;  //
      struct perf_recording_str* link;
    } perf_recording_t;
    
    typedef struct app_str
    {
      io::handle_t ioH;

      // path components for reading/writing the preset assignments
      const char* record_dir;
      const char* record_fn;
      const char* record_fn_ext;
      const char* record_backup_dir;
      
      const char*     scoreFn;
      const object_t* perfDirL;
      const char*     velTableFname;
      const char*     velTableBackupDir;
      const object_t* midi_play_record_cfg;
      const object_t* presets_cfg;
      object_t*       flow_proc_dict;
      const object_t* flow_cfg;
      const object_t* score_follower_cfg;
      const char*     in_audio_dev_file;
      unsigned        in_audio_dev_idx;
      unsigned        in_audio_begin_loc;
      double          in_audio_offset_sec;

      score_follower::handle_t   sfH;
      midi_record_play::handle_t mrpH;

      perf_score::handle_t perfScoreH;
      loc_map_t*      locMap;    
      unsigned        locMapN;

      unsigned insertLoc;       // last valid insert location id received from the GUI
      
      unsigned minScoreLoc;          // min/max locations of the currently loaded score
      unsigned maxScoreLoc;          //
      unsigned minPerfLoc;           // min/max locations of the currently loaded performance
      unsigned maxPerfLoc;
      
      unsigned beg_play_loc;    // beg/end play loc's from the UI
      unsigned end_play_loc;
      bool     lockLoctnFl;

      preset_sel::handle_t      psH;      
      const preset_sel::frag_t* psNextFrag;
      time::spec_t              psLoadT0;
      
      vtbl::handle_t    vtH;
      io_flow::handle_t ioFlowH;
      
      double   crossFadeSrate;
      unsigned crossFadeCnt;

      bool printMidiFl;

      unsigned multiPresetFlags;

      bool     seqActiveFl;     // true if the sequence is currently active (set by 'Play Seq' btn)
      unsigned seqStartedFl;    // set by the first seq idle callback
      unsigned seqFragId;       // 
      unsigned seqPresetIdx;    //

      bool useLiveMidiFl;       // use incoming MIDI to drive program (otherwise use score file)
      bool trackMidiFl;         // apply presets based on MIDI location (otherwise respond only to direct manipulation of fragment control)

      bool        enableRecordFl; // enable recording of incoming MIDI 
      char*       midiRecordDir;
      const char* midiRecordFolder;
      char*       midiLoadFname;

      unsigned sfResetLoc;

      unsigned pvWndSmpCnt;
      bool     sdBypassFl;
      double   sdInGain;
      double   sdCeiling;
      double   sdExpo;
      double   sdThresh;
      double   sdUpr;
      double   sdLwr;
      double   sdMix;
      bool     cmpBypassFl;

      unsigned dfltSyncDelayMs;

      perf_recording_t* perfRecordingBeg;
      perf_recording_t* perfRecordingEnd;

      const char* dflt_perf_label;
      unsigned    dflt_perf_app_id;
      
      
    } app_t;

    rc_t _apply_command_line_args( app_t* app, int argc, const char* argv[] )
    {
      rc_t rc = kOkRC;
      
      for(int i=0; i<argc ; i+=2)
      {
        if( textCompare(argv[i],"record_fn") == 0 )
        {
          app->record_fn = argv[i+1];
          goto found_fl;
        }
        
        if( textCompare(argv[i],"score_fn") == 0 )
        {
          app->scoreFn = argv[i+1];
          goto found_fl;
        }

        if( textCompare(argv[i],"beg_play_loc") == 0 )
        {
          string_to_number( argv[i+1], app->beg_play_loc );
          goto found_fl;
        }

        if( textCompare(argv[i],"end_play_loc") == 0 )
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
      rc_t        rc                = kOkRC;
      const char* flow_proc_dict_fn = nullptr;
      const char* midi_record_dir;
      
      if((rc = cfg->getv( "params", params_cfgRef,
                          "flow",   app->flow_cfg)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App 'params' cfg record not found.");
        goto errLabel;
      }
        
      if((rc = params_cfgRef->getv( "record_dir",           app->record_dir,
                                    "record_fn",            app->record_fn,
                                    "record_fn_ext",        app->record_fn_ext,
                                    "score_fn",             app->scoreFn,
                                    "perfDirL",             app->perfDirL,
                                    "flow_proc_dict_fn",    flow_proc_dict_fn,                                    
                                    "midi_play_record",     app->midi_play_record_cfg,
                                    "vel_table_fname",      app->velTableFname,
                                    "vel_table_backup_dir", app->velTableBackupDir,
                                    "presets",              app->presets_cfg,
                                    "crossFadeCount",       app->crossFadeCnt,
                                    "beg_play_loc",         app->beg_play_loc,
                                    "end_play_loc",         app->end_play_loc,
                                    "dflt_perf_label",      app->dflt_perf_label,
                                    "live_mode_fl",         app->useLiveMidiFl,
                                    "enable_recording_fl",  app->enableRecordFl,
                                    "midi_record_dir",      midi_record_dir,
                                    "midi_record_folder",   app->midiRecordFolder,
                                    "sf_reset_loc",         app->sfResetLoc,
                                    "score_follower",       app->score_follower_cfg)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Preset Select App configuration parse failed.");
        goto errLabel;
      }

      if((rc = params_cfgRef->getv_opt( "in_audio_dev_file",  app->in_audio_dev_file,
                                        "in_audio_file_begin_loc", app->in_audio_begin_loc,
                                        "in_audio_file_offset_sec", app->in_audio_offset_sec)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse of optional cfg. params failed..");
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

      if((app->midiRecordDir = filesys::expandPath( midi_record_dir )) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The midi record path is invalid.");
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

      // verify that the midi record directory exists
      if((rc = filesys::makeDir(app->midiRecordDir)) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to create the MIDI recording directory:%s.",cwStringNullGuard(app->midiRecordDir));
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
      char      s[sN];
      vsnprintf(s,sN,fmt,vl);      
      uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kStatusId), s );
      //printf("Status:%s\n",s);
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

    void _free_perf_recording_recd( perf_recording_t* prp )
    {
      if( prp != nullptr )
      {
        mem::release( prp->label );
        mem::release( prp->fname );
        mem::release(prp->vel_tblA);
        mem::release(prp );
      }
    }

    rc_t _free( app_t& app )      
    {
      if( app.flow_proc_dict != nullptr )
        app.flow_proc_dict->free();

      perf_recording_t* prp = app.perfRecordingBeg;
      while(prp != nullptr )
      {
        perf_recording_t* tmp = prp->link;
        _free_perf_recording_recd(prp);
        prp = tmp;
      }
      

      mem::release((char*&)app.record_backup_dir);
      mem::release((char*&)app.record_dir);
      mem::release((char*&)app.scoreFn);
      mem::release(app.midiRecordDir);
      mem::release(app.midiLoadFname);
      vtbl::destroy(app.vtH);
      destroy(app.sfH);
      preset_sel::destroy(app.psH);
      io_flow::destroy(app.ioFlowH);
      midi_record_play::destroy(app.mrpH);
      perf_score::destroy( app.perfScoreH );
      mem::release(app.locMap);
      return kOkRC;
    }


    rc_t _load_perf_recording_menu( app_t* app )
    {
      rc_t              rc  = kOkRC;
      perf_recording_t* prp = nullptr;
      unsigned          id  = 0;
      unsigned          selectUuId = kInvalidId;
      
      // get the peformance menu UI uuid
      if((selectUuId = io::uiFindElementUuId( app->ioH, kPerfSelId )) == kInvalidId )
      {
        rc = cwLogError(rc,"The performance list base UI element does not exist.");
        goto errLabel;        
      }      

      // for each performance recording
      for(prp = app->perfRecordingBeg; prp!=nullptr; prp=prp->link)
      {
        // create an option entry in the selection ui
        if((rc = uiCreateOption( app->ioH, prp->uuId, selectUuId, nullptr, kPerfOptionBaseId+id, kInvalidId, "optClass", prp->label )) != kOkRC )
        {          
          rc = cwLogError(kSyntaxErrorRC,"The performance recording menu create failed on %s.",prp->label);
          goto errLabel;
        }

        if( app->dflt_perf_label )
          if( textIsEqual(prp->label,app->dflt_perf_label) )
          {
            app->dflt_perf_app_id = kPerfOptionBaseId+id;
            cwLogInfo("The default performance '%s' was found.",prp->label);
          }

        prp->id = id;
        id     += 1;
      }
      
    errLabel:
      
      return rc;
    }

    rc_t _load_alt_menu( app_t* app )
    {
      rc_t              rc  = kOkRC;
      unsigned          uuid;
      unsigned          selectUuId = kInvalidId;
      
      // get the peformance menu UI uuid
      if((selectUuId = io::uiFindElementUuId( app->ioH, kAltSelId )) == kInvalidId )
      {
        rc = cwLogError(rc,"The 'alt' list base UI element does not exist.");
        goto errLabel;        
      }      

      for(unsigned altId=0; altId<alt_count(app->psH); ++altId)
      {
        const char* label = alt_label(app->psH,altId);
        assert( label != nullptr );
        
        // create an option entry in the selection ui
        if((rc = uiCreateOption( app->ioH, uuid, selectUuId, nullptr, altId, kInvalidId, "optClass", label )) != kOkRC )
        {          
          rc = cwLogError(kSyntaxErrorRC,"The 'alt' menu create failed on %s.",cwStringNullGuard(label));
          goto errLabel;
        }        
      }
      
    errLabel:
      
      return rc;
    }

    
    rc_t _parse_perf_recording_vel_tbl( app_t* app, const object_t* velTblCfg, vel_tbl_t*& velTblA_Ref, unsigned& velTblN_Ref )
    {
      rc_t rc = kOkRC;
      
      velTblA_Ref = nullptr;
      velTblN_Ref = 0;
      
      unsigned   velTblN = velTblCfg->child_count();
      vel_tbl_t* velTblA = nullptr;
      
      if( velTblN > 0 )
      {
        velTblA = mem::allocZ<vel_tbl_t>(velTblN);

        for(unsigned i = 0; i<velTblN; ++i)
        {
          if((rc = velTblCfg->child_ele(i)->getv("name",velTblA[i].name,
                                                 "device",velTblA[i].device)) != kOkRC )
          {
            rc = cwLogError(rc,"The vel table at index '%i' parse failed.",i);
            goto errLabel;
          }
        }
      }

      velTblA_Ref = velTblA;
      velTblN_Ref = velTblN;
    errLabel:
      return rc;
    }

    rc_t _create_perf_recording_recd( app_t* app, const char* dir, const char* recording_folder, const char* fname, const object_t* velTblCfg )
    {
      rc_t              rc         = kOkRC;
      perf_recording_t* prp        = nullptr;
      object_t*         meta_cfg   = nullptr;
      const char*       take_label = nullptr;
      char*             perf_fname = nullptr;
      char*             meta_fname = nullptr;
      bool              skip_fl    = false;
      // create the performance recording file path
      if((perf_fname = filesys::makeFn(dir,fname,nullptr,recording_folder,nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The performance file name formation failed on directory '%s'.",cwStringNullGuard(recording_folder));
        goto errLabel;
      }

      // if path does not identify an existing file  - skip it
      if( !filesys::isFile(perf_fname) )
        goto errLabel;

      if((meta_fname = filesys::makeFn(dir,"meta","cfg",recording_folder,nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The performance meta file name formation failed on directory '%s'.",cwStringNullGuard(recording_folder));
        goto errLabel;          
      }
      
      // parse the perf. meta file
      if((rc = objectFromFile( meta_fname, meta_cfg )) != kOkRC )
      {
        rc = cwLogError(rc,"Performance meta file '%s' parse failed.",cwStringNullGuard(meta_fname));
        goto errLabel;
      }

      // allocate the perf_recording_t recd
      prp = mem::allocZ<perf_recording_t>();
        
      // read the meta file values
      if((rc = meta_cfg->getv("take_label",take_label,
                              "session_number",prp->sess_numb,
                              "take_number",prp->take_numb,
                              "beg_loc",prp->beg_loc,
                              "end_loc",prp->end_loc,
                              "skip_score_follow_fl",skip_fl)) != kOkRC )
      {
        rc = cwLogError(rc,"Performance meta file '%s' parse failed.",cwStringNullGuard(meta_fname));
        goto errLabel;          
      }

      if( skip_fl )
      {
        cwLogWarning("'Skip score follow flag' set.Skipping recorded performance '%s'.",cwStringNullGuard(take_label));
        goto errLabel;
      }
                           
      prp->label = mem::duplStr(take_label);
      prp->fname = mem::duplStr(perf_fname);
        
      if((rc = _parse_perf_recording_vel_tbl(app, velTblCfg, prp->vel_tblA, prp->vel_tblN )) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed on vel table entry for the recorded performance in '%s'.",cwStringNullGuard(dir));
        goto errLabel;
      }

      if( app->perfRecordingEnd == nullptr )
      {
        app->perfRecordingBeg = prp;
        app->perfRecordingEnd = prp;
      }
      else
      {          
        app->perfRecordingEnd->link = prp;            
        app->perfRecordingEnd = prp;
      }

    errLabel:
      if( rc != kOkRC || skip_fl )
        _free_perf_recording_recd( prp );

      mem::release(meta_fname);
      mem::release(perf_fname);
      return rc;
      
    }

    rc_t _parse_perf_recording_dir( app_t* app, const char* dir, const char* fname, const object_t* velTblCfg )
    {
      rc_t                 rc  = kOkRC;
      filesys::dirEntry_t* deA  = nullptr;
      unsigned             deN = 0;
      
      // get the directory entries based on 'dir'
      if((deA = filesys::dirEntries( dir, filesys::kDirFsFl, &deN )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The attempt to get the performance directory at '%s' failed.",cwStringNullGuard(dir));
        goto errLabel;
      }

      if( deN == 0 )
        cwLogWarning("The performance recording directory '%s' was found to be empty.",cwStringNullGuard(dir));

      // for each directory entry
      for(unsigned i=0; i<deN; ++i)
        if((rc = _create_perf_recording_recd(app, dir, deA[i].name, fname, velTblCfg )) != kOkRC )
          goto errLabel;
      
    errLabel:
      mem::release(deA);
      return rc;

    }
    
    rc_t _load_perf_dir_selection_menu( app_t* app )
    {
      rc_t rc = kOkRC;
      // verify that a performance list was given
      if( app->perfDirL == nullptr || app->perfDirL->child_count()==0)
      {
        rc = cwLogError(rc,"The performance directorty list is missing or empty.");
        goto errLabel;
      }

      // for each performance directory
      for(unsigned i=0; i<app->perfDirL->child_count(); ++i)
      {
        const object_t* d         = nullptr;;
        const char*     dir       = nullptr;
        const char*     fname     = nullptr;
        const object_t* velTblCfg = nullptr;

        // get the directory dict. from the cfg file
        if((d = app->perfDirL->child_ele(i)) == nullptr || !d->is_dict() )
        {
          rc = cwLogError(kSyntaxErrorRC,"The performance directory entry at index '%i' is malformed.",i);
          goto errLabel;
        }

        // get the directory 
        if((rc = d->getv("dir",dir,
                         "fname",fname,
                         "vel_table", velTblCfg)) != kOkRC )
        {
          rc = cwLogError(rc ,"Error parsing the performance directory entry at index '%i'.",i);
          goto errLabel;
        }

        // create the performance records from this directory
        if((rc = _parse_perf_recording_dir(app,dir,fname,velTblCfg)) != kOkRC )
        {
          rc = cwLogError(rc ,"Error creating the performance directory entry at index '%i'.",i);
          goto errLabel;          
        }        
      }      

      if((rc = _load_perf_recording_menu( app )) != kOkRC )
      {
        rc = cwLogError(rc,"The performance menu creation failed.");
        goto errLabel;
      }

      
    errLabel:

      if(rc != kOkRC )
        rc = cwLogError(rc,"An error occured while creating the recorded performance list.");
        
      return rc;
    }

    
    double _get_system_sample_rate( app_t* app, const char* groupLabel )
    {
      unsigned groupIdx = kInvalidIdx;
      double   srate    = 0;
      
      if((groupIdx = audioGroupLabelToIndex( app->ioH, groupLabel )) == kInvalidIdx )
      {
        cwLogError(kOpFailRC,"The audio group '%s' could not be found.", cwStringNullGuard(groupLabel));
        goto errLabel;
      }

      if(( srate = audioGroupSampleRate( app->ioH, groupIdx )) == 0 )
      {
        cwLogError(kOpFailRC,"The sample rate could not be determined for the audio group: '%s'.", cwStringNullGuard(groupLabel));
        goto errLabel;
      }

    errLabel:
      
      return srate;
    }

    rc_t _apply_preset( app_t* app, unsigned loc, const perf_score::event_t* score_evt=nullptr, const preset_sel::frag_t* frag=nullptr  )
    {
      // if frag is NULL this is the beginning of a play session
      if( frag == nullptr )
      {
        preset_sel::track_loc_reset(app->psH);        
        preset_sel::track_loc( app->psH, loc, frag);
      }
      
      if( frag == nullptr )
        cwLogInfo("No preset fragment was found for the requested timestamp.");
      else
      {
        unsigned    preset_idx        = kInvalidIdx;
        const char* preset_label      = nullptr;
        const char* preset_type_label = "<None>";
        rc_t        apply_rc          = kOkRC;
        
        // if the preset sequence player is active then apply the next selected seq. preset
        // otherwise select the next primary preset for ths fragment
        unsigned seq_idx_n = app->seqActiveFl ? app->seqPresetIdx : kInvalidIdx;

        if( score_evt != nullptr )
        {
          //printf("Meas:e:%f d:%f t:%f c:%f\n",score_evt->even,score_evt->dyn,score_evt->tempo,score_evt->cost);
          printf("Meas:e:%f d:%f t:%f c:%f\n",score_evt->featV[perf_meas::kEvenValIdx],score_evt->featV[perf_meas::kDynValIdx],score_evt->featV[perf_meas::kTempoValIdx],score_evt->featV[perf_meas::kMatchCostValIdx]);
        }

        // if we are not automatically sequencing through the presets and a score event was given
        if( seq_idx_n == kInvalidIdx && score_evt != nullptr  )
        {
          unsigned multiPresetN = 0;

          // allow-any,dry-priority,dry-selected may only be set when pri-prob is set
          bool allowAnyFl    = cwIsFlag(app->multiPresetFlags,flow::kAllowAllPresetFl)    && cwIsFlag(app->multiPresetFlags,flow::kPriPresetProbFl);
          bool dryPriorityFl = cwIsFlag(app->multiPresetFlags,flow::kDryPriorityPresetFl) && cwIsFlag(app->multiPresetFlags,flow::kPriPresetProbFl);
          bool drySelectedFl = cwIsFlag(app->multiPresetFlags,flow::kDrySelectedPresetFl) && cwIsFlag(app->multiPresetFlags,flow::kPriPresetProbFl);

          unsigned activePresetFlags  = 0;

          activePresetFlags = cwEnaFlag(activePresetFlags, preset_sel::kAllActiveFl,   allowAnyFl);
          activePresetFlags = cwEnaFlag(activePresetFlags, preset_sel::kDryPriorityFl, dryPriorityFl);
          activePresetFlags = cwEnaFlag(activePresetFlags, preset_sel::kDrySelectedFl, drySelectedFl);
                    
          flow::multi_preset_selector_t mp_sel =
            { .flags     = app->multiPresetFlags,
              .coeffV    = score_evt->featV,
              .coeffMinV = score_evt->featMinV,
              .coeffMaxV = score_evt->featMaxV,
              .coeffN    = perf_meas::kValCnt,
              .presetA   = fragment_active_presets(app->psH,frag,activePresetFlags,multiPresetN),
              .presetN   = multiPresetN
            };

          if( mp_sel.presetA == nullptr || mp_sel.presetN == 0 )
            cwLogWarning("No active presets were found for loc:%i at end loc:%i.",loc,frag->endLoc);
          else
          {
            if( app->ioFlowH.isValid() )
              apply_rc = io_flow::apply_preset( app->ioFlowH, flow_cross::kNextDestId, mp_sel );
          }
          
          preset_label = "(multi)"; 

          preset_type_label = "multi";
            
        }
        else
        {          
          // get the preset index to play for this fragment
          if((preset_idx = fragment_play_preset_index(app->psH, frag, seq_idx_n)) == kInvalidIdx )
            cwLogInfo("No preset has been assigned to the fragment at end loc. '%i'.",frag->endLoc );
          else
            preset_label = preset_sel::preset_label(app->psH,preset_idx);
          
          if( preset_label != nullptr )
          {
            if( app->ioFlowH.isValid() )
              apply_rc = io_flow::apply_preset( app->ioFlowH, flow_cross::kNextDestId, preset_label );

            preset_type_label = "single";
          }

          // don't display preset updates unless the score is actually loaded          
          printf("Apply %s preset: '%s' : loc:%i status:%i\n", preset_type_label, preset_label==nullptr ? "<invalid>" : preset_label, loc, apply_rc );
          
        }          

        _set_status(app,"Apply %s preset: '%s'.", preset_type_label, preset_label==nullptr ? "<invalid>" : preset_label);
        
        // apply the fragment defined gain settings
        if( app->ioFlowH.isValid() )
        {
          io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "wet_in_gain", "gain", flow::kAnyChIdx, (dsp::real_t)frag->igain );
          io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "wet_out_gain","gain", flow::kAnyChIdx, (dsp::real_t)frag->ogain );
          io_flow::set_variable_value( app->ioFlowH, flow_cross::kNextDestId, "wd_bal",      "in",   flow::kAnyChIdx, (dsp::real_t)frag->wetDryGain );

          // activate the cross-fade
          io_flow::begin_cross_fade( app->ioFlowH, frag->fadeOutMs );
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
      unsigned prevUuId = preset_sel::frag_to_gui_id(app->psH,prevFragId,false);
      
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

    rc_t _do_stop_play( app_t* app )
    {
      rc_t rc = kOkRC;
      unsigned evt_cnt = 0;
      
      if( app->in_audio_dev_idx != kInvalidIdx )
      {
        if((rc = audioDeviceEnable( app->ioH, app->in_audio_dev_idx, true, false )) != kOkRC )
        {
          rc = cwLogError(rc,"Enable failed on audio device input file.");
          goto errLabel;
        }
      }
            
      if((rc = midi_record_play::stop(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI stop failed.");
        goto errLabel;
      }

      if( app->enableRecordFl && (evt_cnt = midi_record_play::event_count(app->mrpH)) > 0 )
      {
        io::uiSendValue( app->ioH, io::uiFindElementUuId( app->ioH, kTotalMidiEvtCntId ), evt_cnt );
        io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kMidiSaveBtnId ), true );
      }

    errLabel:
      return rc;
    }

    unsigned _get_loc_from_score_follower( app_t* app, double secs, unsigned muid, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      unsigned loc = score_parse::kInvalidLocId;
            
      // if this is a MIDI note-on event - then udpate the score follower
      if( midi::isNoteOn(status,d1) && muid != kInvalidIdx )
      {
        unsigned smpIdx             = 0; // not functional - used to associate input with score follower output 
        bool     newMatchOccurredFl = false;
        if( exec( app->sfH, secs, smpIdx, muid, status, d0, d1, newMatchOccurredFl ) != kOkRC )
        {
          cwLogWarning("Score follower exec error.");
        }
        else
        {
          if( newMatchOccurredFl )
          {
            unsigned        matchLocN = 0;
            const unsigned* matchLocA = current_result_index_array( app->sfH, matchLocN );

            unsigned maxLocId = 0;

            printf("SF: ");
            for(unsigned i=0; i<matchLocN; ++i)
            {
              if( matchLocA[i] > maxLocId )
                maxLocId = matchLocA[i];
              printf("%i ",matchLocA[i]);
            }                  
            printf("\n");


            loc = maxLocId;
            
            clear_result_index_array(app->sfH);
          }
                
        }
      }
      
      return loc;      
    }
    
    void _midi_play_callback( void* arg, unsigned actionId, unsigned id, const time::spec_t timestamp, unsigned loc, const void* msg_arg, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      app_t* app = (app_t*)arg;
      switch( actionId )
      {
        case midi_record_play::kPlayerStoppedActionId:
          app->seqStartedFl = false;
          _do_stop_play(app);
          _set_status(app,"Done");
          break;
          
        case midi_record_play::kMidiEventActionId:
        {
          if( app->printMidiFl )
          {
            const unsigned buf_byte_cnt = 256;
            char           buf[ buf_byte_cnt ];
          
            // if this event is not in the score
            if( id == kInvalidId )
            {
              // TODO: print this out in the same format as event_to_string()
              snprintf(buf,buf_byte_cnt,"ch:%i status:0x%02x d0:%i d1:%i",ch,status,d0,d1);
            }
            else
              perf_score::event_to_string( app->perfScoreH, id, buf, buf_byte_cnt );
            printf("%s\n",buf);
          }

          if( midi_record_play::is_started(app->mrpH) )
          {
            const preset_sel::frag_t* f = nullptr;

            double sec = time::specToSeconds(timestamp);

            // call the score follower
            if( score_follower::is_enabled(app->sfH) )
              loc = _get_loc_from_score_follower( app, sec, id, status, d0, d1 );
          
            // TODO: ZERO SHOULD BE A VALID LOC VALUE - MAKE -1 THE INVALID LOC VALUE
            
            if( loc != score_parse::kInvalidLocId  && app->trackMidiFl )
            {
              if( preset_sel::track_loc( app->psH, loc, f ) )  
              {

                printf("Loc:%i\n",loc);
                _apply_preset( app, loc, (const perf_score::event_t*)msg_arg, f );
                
                if( f != nullptr )
                  _do_select_frag( app, f->guiUuId );
              }
            }
            
          }
          break;
        }
      }
    }

    rc_t  _on_live_midi_event( app_t* app, const io::msg_t& msg )
    {
      rc_t rc = kOkRC;

      if( msg.u.midi != nullptr )
      {        
        const io::midi_msg_t& m   = *msg.u.midi;
        const midi::packet_t* pkt = m.pkt;
        
        // for each midi msg
        for(unsigned j = 0; j<pkt->msgCnt; ++j)
        {
          // if this is a sys-ex msg
          if( pkt->msgArray == NULL )
          {
            cwLogError(kNotImplementedRC,"Sys-ex recording not implemented.");
          }
          else                  // this is a triple
          {
            time::spec_t timestamp;
            midi::msg_t* mm = pkt->msgArray + j;
            unsigned     id = app->enableRecordFl ? last_store_index(app->mrpH) : kInvalidId;
            unsigned     loc = app->beg_play_loc;
            
            time::get(timestamp);
            
            if( midi::isChStatus(mm->status) )
            {
              if(midi_record_play::send_midi_msg( app->mrpH, midi_record_play::kSampler_MRP_DevIdx, mm->ch, mm->status, mm->d0, mm->d1 ) == kOkRC )                
                _midi_play_callback( app, midi_record_play::kMidiEventActionId, id, timestamp, loc, nullptr, mm->ch, mm->status, mm->d0, mm->d1 );
            }

          }
        }          
      }

      return rc;
    }
      

    // Find the closest locMap equal to or after 'loc'
    loc_map_t* _find_loc( app_t* app, unsigned loc )
    {
      unsigned   i=0;
      loc_map_t* pre_loc_map = nullptr;
      
      for(; i<app->locMapN; ++i)
      {
        if( app->locMap[i].loc >= loc )
          return app->locMap +i;

        pre_loc_map = app->locMap + i;
           
      }
      
      return pre_loc_map;
    }

    rc_t  _loc_to_frame_index( app_t* app, unsigned loc, unsigned& frameIdxRef )
    {
      rc_t                  rc   = kOkRC;
      const perf_score::event_t* e0   = nullptr;
      const perf_score::event_t* e1   = nullptr;
      double                srate     = 0;
      double                secs = 0;

      frameIdxRef = kInvalidIdx;
      
      if( app->in_audio_begin_loc != score_parse::kInvalidLocId )
      {
        if((e0 = loc_to_event(app->perfScoreH,app->in_audio_begin_loc)) == nullptr )
        {
          rc = cwLogError(kInvalidArgRC,"The score event associated with the 'in_audio_beg_loc' loc:%i could not be found.",loc);
          goto errLabel;
        }
      }
      
      if((e1 = loc_to_event(app->perfScoreH,loc)) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The score event associated with the begin play loc:%i could not be found.",loc);
        goto errLabel;
      }

      if((srate = audioDeviceSampleRate(app->ioH, app->in_audio_dev_idx )) == 0 )
      {
        rc = cwLogError(kInvalidArgRC,"Audio device file sample rate could not be accessed.");
        goto errLabel;        
      }

      if( e1->sec < e0->sec )
        cwLogWarning("The audio file start time ('in_audio_beg_sec') (%f sec) is prior to the start play location %f sec.",e1->sec,e0->sec);
      else
      {
        secs = (e1->sec - e0->sec) + app->in_audio_offset_sec;      
        cwLogInfo("File offset %f seconds. %f %f",secs);
      }
      
      frameIdxRef = (unsigned)(secs * srate);
      
    errLabel:
      
      return rc;
    }

    bool _is_performance_loaded( app_t* app )
    {
      return app->perfScoreH.isValid() and event_count(app->perfScoreH) > 0;
    }

    rc_t _do_sf_reset( app_t* app, unsigned loc )
    {
      rc_t rc = kOkRC;

      track_loc_reset( app->psH );
      
      score_follower::reset(app->sfH,app->sfResetLoc);

      cwLogInfo("SF reset loc: %i",app->sfResetLoc);
      return rc;
    }
    
    

    rc_t _do_start( app_t* app, unsigned begLoc, unsigned endLoc )
    {
      rc_t       rc         = kOkRC;
      bool       rewindFl   = true;
      loc_map_t* begMap     = nullptr;
      loc_map_t* endMap     = nullptr;
      unsigned   score_loc  = app->sfResetLoc;
      unsigned   preset_loc = app->sfResetLoc;

      // if the player is already playing then stop it
      if( midi_record_play::is_started(app->mrpH) )
      {
        rc = _do_stop_play(app);
        goto errLabel;
      }

      // if we are using and audio file as the source
      if( app->in_audio_dev_idx != kInvalidIdx )
      {
        unsigned frameIdx = 0;
        if((rc = _loc_to_frame_index(app, begLoc, frameIdx )) != kOkRC )
        {
          rc = cwLogError(rc,"Frame index could not be calculated.");
          goto errLabel;
        }
        
        if((rc = audioDeviceSeek( app->ioH, app->in_audio_dev_idx, true, frameIdx )) != kOkRC )
        {
          rc = cwLogError(rc,"Seek failed on audio device input file.");
          goto errLabel;
        }

        if((rc = audioDeviceEnable( app->ioH, app->in_audio_dev_idx, true, true )) != kOkRC )
        {
          rc = cwLogError(rc,"Enable failed on audio device input file.");
          goto errLabel;
        }
      }

      // if a performance is loaded
      if( _is_performance_loaded(app) )
      {
        if((begMap = _find_loc(app,begLoc)) == nullptr )
        {
          rc = cwLogError(kInvalidArgRC,"The begin play location (%i) is not valid.",begLoc);
        goto errLabel;
        }

        if((endMap = _find_loc(app,endLoc)) == nullptr )
        {
          rc = cwLogError(kInvalidArgRC,"The end play location (%i) is not valid.",endLoc);
          goto errLabel;
        }

        if( !time::isZero(begMap->timestamp)  )
        {
          // seek the player to the requested loc
          if((rc = midi_record_play::seek( app->mrpH, begMap->timestamp )) != kOkRC )
          {
            rc = cwLogError(rc,"MIDI seek failed.");
            goto errLabel;
          }
          rewindFl = false;
        }

        score_loc  = begLoc;
        preset_loc = begMap->loc;
      }

      // if recording - empty the recording buffer
      if( app->enableRecordFl )
      {
        midi_record_play::set_record_state(app->mrpH,app->enableRecordFl);
        midi_record_play::clear(app->mrpH);
        io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kMidiSaveBtnId ), false );
      }
      
      // reset the score follower
      if((rc = _do_sf_reset(app,score_loc)) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower reset failed.");
        goto errLabel;        
      }
      
      // apply the preset which is active at the start time
      if((rc = _apply_preset( app, preset_loc )) != kOkRC )
      {
        rc = cwLogError(rc,"Preset application failed prior to MIDI start.");
        goto errLabel;
      }

      // start the MIDI record/play unit
      if( _is_performance_loaded(app) || app->enableRecordFl )
      {
        unsigned evt_cnt = 0;
        
        if((rc = midi_record_play::start(app->mrpH,rewindFl,&endMap->timestamp)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI start failed.");
          goto errLabel;
        }


        // update the current event loc/count
        if( _is_performance_loaded(app) )
          evt_cnt = midi_record_play::event_loc(app->mrpH);
        else
          if( app->enableRecordFl )
            evt_cnt = midi_record_play::event_count(app->mrpH);
          
        io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), evt_cnt  );
      }
      
    errLabel:
      return rc;
    }

    rc_t _do_goto( app_t* app, unsigned loc )
    {
      rc_t rc = kOkRC;
      unsigned fragGuiId;

      // scroll to the top - This is a hack - move this logic in ui.js.
      if((fragGuiId = loc_to_gui_id(app->psH,1)) == kInvalidId )
      {
        rc = cwLogError(kInvalidArgRC,"The fragment loc '%i' could not be found.");
        goto errLabel;
      }
      
      if((rc = io::uiSetScrollTop(app->ioH,fragGuiId)) != kOkRC )
      {
        rc = cwLogError(rc,"Scroll to top failed on fragment GUI id:%i.",fragGuiId);
        goto errLabel;
      }

      // scroll to loc
      if((fragGuiId = loc_to_gui_id(app->psH,loc)) == kInvalidId )
      {
        rc = cwLogError(kInvalidArgRC,"The fragment loc '%i' could not be found.");
        goto errLabel;
      }
      
      if((rc = io::uiSetScrollTop(app->ioH,fragGuiId)) != kOkRC )
      {
        rc = cwLogError(rc,"Scroll to top failed on fragment GUI id:%i.",fragGuiId);
        goto errLabel;
      }

    errLabel:
      return rc;
    }
    

    // This function is used to apply the selected (checked) preset immediately.
    rc_t _apply_current_preset( app_t* app, unsigned fragId )
    {
      rc_t                      rc               = kOkRC;
      const preset_sel::frag_t* frag             = nullptr;
      bool                      orig_seqActiveFl = app->seqActiveFl;

      // temporarily turn of the preset sequencer (if it is active)
      app->seqActiveFl = false;

      if((frag = preset_sel::get_fragment( app->psH, fragId )) == nullptr )
      {
        rc = cwLogError(rc,"The fragment at id '%i' could not be accessed.",fragId);
        goto errLabel;
      }

      // apply the preset which is active at the start time
      if((rc = _apply_preset( app, frag->begPlayLoc, nullptr, frag )) != kOkRC )
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
      rc_t     rc;
      unsigned begLoc = 0;
      unsigned endLoc = 0;

      // get the fragment starting location as currently set in the UI
      // (this may be different from the fragment begin location)
      if((rc = get_value( app->psH, fragId, preset_sel::kBegPlayLocVarId, kInvalidId, begLoc )) != kOkRC )
      {
        rc = cwLogError(rc,"Could not retrieve the begin play location for fragment id:%i.",fragId);
        goto errLabel;
      }

      // get the fragment ending location as currently set in the UI
      // (this may be different from the fragment end location)
      if((rc = get_value( app->psH, fragId, preset_sel::kEndPlayLocVarId, kInvalidId, endLoc )) != kOkRC )
      {
        rc = cwLogError(rc,"Could not retrieve the begin play location for fragment id:%i.",fragId);
        goto errLabel;
      }

      rc = _do_start(app,begLoc,endLoc);
      
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
      if( app->seqActiveFl && app->seqStartedFl == false)
      {
        app->seqPresetIdx += 1;
        app->seqStartedFl  = app->seqPresetIdx < preset_sel::fragment_seq_count( app->psH, app->seqFragId );
        app->seqActiveFl   = app->seqStartedFl;
        
        if( app->seqStartedFl )
          _do_play_fragment( app, app->seqFragId );
      }
      return rc;
    }
    
    void _update_event_ui( app_t* app )
    {
      //io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId),   midi_record_play::event_index(app->mrpH) );
      //io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), midi_record_play::event_count(app->mrpH) );
      
      io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kTotalMidiEvtCntId), app->maxPerfLoc-app->minPerfLoc );
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

      endLocRef                                                                                 = kInvalidId;
      if((rc = get_value( app->psH, fragId, preset_sel::kEndLocVarId, kInvalidId, endLocRef )) != kOkRC )
        rc                                                                                      = cwLogError(rc,"Unable to get the 'end loc' value for fragment id:%i.",fragId);

      return rc;
    }

    // Update the preset select check boxes on a fragment panel
    rc_t _update_frag_select_flags( app_t* app, unsigned fragId, unsigned fragEndLoc = kInvalidId, bool apply_preset_fl = true )
    {
      rc_t rc = kOkRC;

      if( fragEndLoc == kInvalidId )
      {
        // get the endLoc associated with this fragment
        rc = _frag_id_to_endloc(app, fragId, fragEndLoc );
      }

      if( rc == kOkRC )
      {
        bool     bValue;
        unsigned uValue;
        const char* sValue;
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
          _update_frag_ui( app, fragId, preset_sel::kPresetAltVarId,      preset_idx, fragPresetRowUuId, kFragPresetAltId,    preset_idx,  sValue );          
          _update_frag_ui( app, fragId, preset_sel::kPresetSeqSelectVarId,preset_idx, fragPresetRowUuId, kFragPresetSeqSelId, preset_idx,  bValue );          
        }

        if( apply_preset_fl )
          _apply_current_preset(app, fragId );
        
      }
      
      return rc;
    }

    // Update the fragment UI withh the fragment record associated with 'fragId'
    rc_t _update_frag_ui(app_t* app, unsigned fragId, bool apply_preset_fl=true )
    {
      // Notes: 
      // uiChanId = fragId for panel values or uiChanId = preset_index for preset values
                                      
      rc_t     rc = kOkRC;
      unsigned endLoc;

      // Get the endLoc for this fragment
      if((rc = _frag_id_to_endloc(app, fragId, endLoc )) == kOkRC )
      {
        unsigned    uValue;
        double      dValue;
        const char* sValue;
        unsigned    uiChanId      = fragId; //endLoc;
        unsigned    fragPanelUuId = kInvalidId;
        
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
        
        _update_frag_select_flags( app, fragId, endLoc, apply_preset_fl );
        
      }
      
      return rc;
    }

    rc_t  _frag_uuid_to_blob( app_t* app, unsigned uuId, ui_blob_t*& blobRef )
    {
      unsigned blobByteN = 0;
      
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
      bool     enableFl = begPlayLoc < endPlayLoc;
      unsigned fragUuId = kInvalidId;

      if((fragUuId = frag_to_gui_id( app->psH, blob->fragId )) != kInvalidId )
      {
        unsigned btnIdA[] = { kFragPlayBtnId, kFragPlaySeqBtnId, kFragPlayAllBtnId };
        unsigned btnIdN = sizeof(btnIdA)/sizeof(btnIdA[0]);

        for(unsigned i = 0; i<btnIdN; ++i)
        {
          unsigned btnUuId;
          if((btnUuId = uiFindElementUuId( app->ioH, fragUuId, btnIdA[i], blob->presetId )) != kInvalidId )
            uiSetEnable( app->ioH, btnUuId, enableFl );

        }
      }

      //if( enableFl )
      //  _clear_status(app);
      //else
      if( !enableFl )
      {
        _set_status(app,"Invalid fragment play range. beg:%i end:%i",begPlayLoc,endPlayLoc);
        cwLogError(kInvalidArgRC,"Invalid fragment play range. beg:%i end:%i",begPlayLoc,endPlayLoc);
      }

    }

    void _disable_frag_play_btn( app_t* app, unsigned fragBegEndUuId )
    {
      ui_blob_t* blob = nullptr;
      if(_frag_uuid_to_blob(app, fragBegEndUuId, blob) == kOkRC )
        _enable_frag_play_btn( app, blob, 1, (unsigned)0 );      
    }

    // Called when a UI value is changed in a fragment panel (e.g. gain, fadeMs, ...)
    template< typename T>
    rc_t _on_ui_frag_value( app_t* app, unsigned uuId, const T& value )
    {
      rc_t       rc   = kOkRC;
      ui_blob_t* blob = nullptr;

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

        case preset_sel::kPresetAltVarId:          
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
      ui_blob_t blob = { .fragId = fragId, .varId=varId, .presetId=presetId };
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

      /*
      // order/alt row container
      if((rc = io::uiCreateDiv( app->ioH, rowUuId, colUuId, nullEleName, invalidAppId, chanId, "uiRow", nullptr )) != kOkRC )
        goto errLabel;
      
      // preset order number
      if((rc = io::uiCreateNumb( app->ioH, uuId,  rowUuId, nullEleName, kFragPresetOrderId, chanId, "uiNumber fragLittleNumb", nullptr, 0, presetN, 1, 0 )) != kOkRC )
        goto errLabel;

      // store a connection for the order control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetOrderVarId, preset_idx );
      
      // preset alt letter
      if((rc = io::uiCreateStr( app->ioH, uuId,  rowUuId, nullEleName, kFragPresetAltId, chanId, "uiString fragLittleNumb", nullptr )) != kOkRC )
        goto errLabel;

      // store a connection for the order control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetAltVarId, preset_idx );
      */

      // preset order number
      if((rc = io::uiCreateNumb( app->ioH, uuId,  colUuId, nullEleName, kFragPresetOrderId, chanId, nullClass, nullptr, 0, presetN, 1, 0 )) != kOkRC )
        goto errLabel;

      // store a connection for the order control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetOrderVarId, preset_idx );
      
      // preset alt letter
      if((rc = io::uiCreateStr( app->ioH, uuId,  colUuId, nullEleName, kFragPresetAltId, chanId, nullClass, nullptr )) != kOkRC )
        goto errLabel;

      // store a connection for the order control back to the fragment record
      _frag_set_ui_blob(app, uuId, fragId, preset_sel::kPresetAltVarId, preset_idx );
      
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
      rc_t     rc                = kOkRC;
      unsigned fragListUuId      = io::uiFindElementUuId( app->ioH, kFragListId );
      unsigned fragChanId        = fragId; //endLoc; // use the frag. endLoc as the channel id
      unsigned fragPanelUuId     = kInvalidId;
      unsigned fragPresetRowUuId = kInvalidId;
      unsigned presetN           = preset_sel::preset_count( app->psH );
      unsigned fragBegLoc  = 0;

      // create the UI object
      if((rc = io::uiCreateFromRsrc( app->ioH, "frag_panel",  fragListUuId, fragChanId )) != kOkRC )
      {
        rc = cwLogError(rc,"The fragments UI object creation failed.");
        goto errLabel;
      }
      
      // get the uuid's of the new fragment panel and the endloc number display
      fragPanelUuId     = io::uiFindElementUuId( app->ioH, fragListUuId,  kFragPanelId,     fragChanId );
      fragPresetRowUuId = io::uiFindElementUuId( app->ioH, fragPanelUuId, kFragPresetRowId, fragChanId );

      assert( fragPanelUuId     != kInvalidId );
      assert( fragPresetRowUuId != kInvalidId );

      // Make the fragment panel clickable
      io::uiSetClickable(   app->ioH, fragPanelUuId);

      // Set the fragment panel order.
      io::uiSetOrderKey( app->ioH, fragPanelUuId, endLoc );
      
      // Set the fragment beg/end play range
      get_value( app->psH, fragId, preset_sel::kBegPlayLocVarId, kInvalidId, fragBegLoc );

      uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragBegPlayLocId, fragChanId), app->minScoreLoc, app->maxScoreLoc, 1, 0, fragBegLoc );
      uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, fragPanelUuId, kFragEndPlayLocId, fragChanId), app->minScoreLoc, app->maxScoreLoc, 1, 0, endLoc );

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
      for(unsigned preset_idx = 0; preset_idx<presetN; ++preset_idx)
        if((rc = _create_frag_preset_ctl(app, fragId, fragPresetRowUuId, presetN, preset_idx )) != kOkRC )
          goto errLabel;

      // set the uuid associated with this fragment
      preset_sel::set_value( app->psH, fragId, preset_sel::kGuiUuIdVarId, kInvalidId, fragPanelUuId );

    errLabel:
      return rc;
      
    }

    rc_t _fragment_load_data( app_t* app )
    {
      rc_t                      rc = kOkRC;
      char*                     fn = nullptr;
      
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

      get_loc_range(app->psH,app->minScoreLoc,app->maxScoreLoc);

      // Settting psNextFrag to a non-null value causes the
      app->psNextFrag = preset_sel::get_fragment_base(app->psH);

      _set_status(app,"Loaded fragment file.");

      time::get(app->psLoadT0);

    errLabel:
      mem::release(fn);

      return rc;
    }

rc_t _on_perf_select(app_t* app, unsigned optionAppId );
rc_t _on_ui_play_loc(app_t* app, unsigned appId, unsigned loc);

    
    rc_t _fragment_restore_ui( app_t* app )
    {
      rc_t rc = kOkRC;
      
      // if the fragment UI has not already been created
      if( app->psNextFrag != nullptr )
      {
        unsigned fragId = app->psNextFrag->fragId;

        // create a fragment UI
        if((rc = _create_frag_ui(app, app->psNextFrag->endLoc, fragId )) != kOkRC )
        {
          cwLogError(rc,"Frag UI create failed.");
          goto errLabel;
        }

        // update the fragment UI
        _update_frag_ui(app, fragId, false );

        _set_status(app,"Loaded fragment loc:%i.",app->psNextFrag->endLoc);

        // prepare to create the next fragment UI
        app->psNextFrag = app->psNextFrag->link;

        // if all the fragment UI's have been created
        if( app->psNextFrag == nullptr )
        {

          io::uiSendMsg( app->ioH, "{ \"op\":\"attach\" }" );
          
          // the fragments are loaded enable the 'load' and 'alt' menu
          io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPerfSelId ),   true );
          io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kAltSelId ),   true );

          _set_status(app,"Load complete.");
          
          cwLogInfo("Fragment restore complete: elapsed secs:%f",time::elapsedSecs(app->psLoadT0));
          io::uiRealTimeReport(app->ioH);

          /*
          if( app->dflt_perf_app_id != kInvalidId )
          {
            uiSendValue( app->ioH, io::uiFindElementUuId(app->ioH, kPerfSelId), app->dflt_perf_app_id );

            _on_perf_select(app, app->dflt_perf_app_id );

            

            uiSendValue( app->ioH, io::uiFindElementUuId(app->ioH, kBegPlayLocNumbId), 2538 );
            uiSendValue( app->ioH, io::uiFindElementUuId(app->ioH, kEndPlayLocNumbId), 3517 );

            _on_ui_play_loc(app, kBegPlayLocNumbId, 2538);
            _on_ui_play_loc(app, kBegPlayLocNumbId, 3517);

          }
          */
        }
      }

    errLabel:
      if( rc != kOkRC )
      {
        // TODO: HANDLE FAILURE
      }
      
      return rc;
    }

    /*
    rc_t _restore_fragment_data( app_t* app )
    {
      rc_t                      rc = kOkRC;
      char*                     fn = nullptr;
      const preset_sel::frag_t* f = nullptr;
      time::spec_t t0;

      time::get(t0);
      

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

      get_loc_range(app->psH,app->minScoreLoc,app->maxScoreLoc);
  
      //preset_sel::report( app->psH );

      f = preset_sel::get_fragment_base(app->psH);
      for(int i = 0; f!=nullptr; f=f->link,++i)
      {
        unsigned fragId = f->fragId;
        
        if((rc = _create_frag_ui(app, f->endLoc, fragId )) != kOkRC )
        {
          cwLogError(rc,"Frag UI create failed.");
          goto errLabel;
        }

        _update_frag_ui(app, fragId, false );
        
      }
      
    errLabel:
      mem::release(fn);

      printf("ELAPSED SECS:%f\n",time::elapsedSecs(t0));
      
      return rc;
    }
    */
    rc_t _on_ui_save( app_t* app )
    {
      rc_t  rc  = kOkRC;
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

    rc_t _load_midi_player( app_t* app, unsigned& midiEventCntRef )
    {
      rc_t                          rc         = kOkRC;
      const perf_score::event_t*    e          = nullptr;
      unsigned                      midiEventN = 0;      
      midi_record_play::midi_msg_t* m          = nullptr;
      midiEventCntRef = 0;
      
      // get the count of MIDI events
      if((e = perf_score::base_event( app->perfScoreH )) != nullptr )
        for(; e != nullptr; e=e->link)
          if( e->status != 0 )
            midiEventN  += 1;

      // copy the MIDI events
      if((e = perf_score::base_event( app->perfScoreH )) != nullptr )
      {
        // allocate the locMap[]
        app->locMap  = mem::resizeZ<loc_map_t>( app->locMap, midiEventN ); 
        app->locMapN = midiEventN;
        app->minPerfLoc  = score_parse::kInvalidLocId;
        app->maxPerfLoc  = score_parse::kInvalidLocId;
                
        // allocate the the player msg array
        m = mem::allocZ<midi_record_play::midi_msg_t>( midiEventN );

        // load the player msg array
        for(unsigned i = 0; e!=nullptr && i<midiEventN; e= e->link)
          if( e->status != 0 )
          {
            time::millisecondsToSpec(m[i].timestamp,  (unsigned)(e->sec*1000) );
            m[i].ch     = e->status & 0x0f;
            m[i].status = e->status & 0xf0;
            m[i].d0     = e->d0;
            m[i].d1     = e->d1;
            m[i].id     = e->uid;
            m[i].loc    = e->loc;
            m[i].arg    = e;

            app->locMap[i].loc = e->loc;
            app->locMap[i].timestamp = m[i].timestamp;

            if( e->loc != score_parse::kInvalidLocId )
            {
              if( app->minPerfLoc == score_parse::kInvalidLocId )
                app->minPerfLoc = e->loc;
              else            
                app->minPerfLoc = std::min(app->minPerfLoc,e->loc);
              
              if( app->maxPerfLoc == score_parse::kInvalidLocId )
                app->maxPerfLoc = e->loc;
              else            
                app->maxPerfLoc = std::max(app->maxPerfLoc,e->loc);
            }
            
            ++i;
          }

        qsort( app->locMap, app->locMapN, sizeof(loc_map_t), _compare_loc_map );
        
        // load the player with the msg list
        if((rc = midi_record_play::load( app->mrpH, m, midiEventN )) != kOkRC )
        {
          cwLogError(rc,"MIDI player load failed.");
          goto errLabel;
        }

        midiEventCntRef = midiEventN;

        cwLogInfo("%i MIDI events loaded from score. Loc Min:%i Max:%i", midiEventN , app->minPerfLoc, app->maxPerfLoc);
      }
      
    errLabel:
      
      mem::release(m);        
      return rc;  
    }

    rc_t _set_vel_table( app_t* app, const vel_tbl_t* vtA, unsigned vtN )
    {
      rc_t rc;
      const uint8_t* tableA = nullptr;
      unsigned tableN = 0;
      unsigned midiDevIdx = kInvalidIdx;
      unsigned assignN = 0;
      
      if((rc = vel_table_disable(app->mrpH)) != kOkRC )
      {
        rc = cwLogError(rc,"Velocity table disable failed.");
        goto errLabel;
      }

     
      for(unsigned i=0; i<vtN; ++i)
      {
        if((midiDevIdx = label_to_device_index( app->mrpH, vtA[i].device)) == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The MIDI device '%s' could not be found.",cwStringNullGuard(vtA[i].device));
          goto errLabel;
        }

        if((tableA = get_vel_table( app->vtH, vtA[i].name, tableN )) == nullptr )
        {
          rc = cwLogError(kInvalidArgRC,"The MIDI velocity table '%s' could not be found.",cwStringNullGuard(vtA[i].name));
          goto errLabel;
        }
        
        if((rc = vel_table_set(app->mrpH, midiDevIdx, tableA, tableN )) != kOkRC )
        {
          rc = cwLogError(rc,"Velocity table device:%s name:%s assignment failed.",cwStringNullGuard(vtA[i].device), cwStringNullGuard(vtA[i].name));
          goto errLabel;
        }

        cwLogInfo("Applied velocity table: %s to dev: %s.", cwStringNullGuard(vtA[i].name), cwStringNullGuard(vtA[i].device) );

        assignN += 1;
      }

      if( assignN == 0 )
        cwLogWarning("All velocity tables disabled.");
      
    errLabel:

      if( rc != kOkRC )
        rc = cwLogError(rc,"Velocity table assignment failed.");
      
      return rc;
    }

    rc_t _do_load_perf_score( app_t* app, const char* perf_fn, const vel_tbl_t* vtA=nullptr, unsigned vtN=0 )
    {
      rc_t     rc          = kOkRC;
      unsigned midiEventN  = 0;
      
      // only lock the current beg/end location settings if a valid perf. score is already loaded
      bool     lockLoctnFl = app->perfScoreH.isValid() && app->lockLoctnFl;
      
      cwLogInfo("Loading");
      _set_status(app,"Loading...");

      // load the performance
      if((rc= perf_score::create( app->perfScoreH, perf_fn )) != kOkRC )
      {
        cwLogError(rc,"Score create failed on '%s'.",perf_fn);
        goto errLabel;          
      }
      
      // load the midi player, create locMap[], set app->min/maxLoc
      if((rc = _load_midi_player(app, midiEventN )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI player load failed.");
        goto errLabel;          
      }

      // assign the vel. table 
      if((rc = _set_vel_table(app, vtA, vtN )) != kOkRC )
      {
        rc = cwLogError(rc,"Velocity table assignment failed.");
        goto errLabel;                  
      }
      
      // A performance is loaded so enable the UI
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStartBtnId ), true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStopBtnId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kGotoBtnId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLockLoctnCheckId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLiveCheckId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kEnaRecordCheckId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kTrackMidiCheckId ),  true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSaveBtnId ), true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kInsertLocId ), true );
      
      // set the UI begin/end play to the locations of the newly loaded performance
      if( !lockLoctnFl )
      {
        app->end_play_loc = app->maxPerfLoc;
        app->beg_play_loc = app->minPerfLoc;
      }
      
      // Update the master range of the play beg/end number widgets
      io::uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, kBegPlayLocNumbId), app->minPerfLoc, app->maxPerfLoc, 1, 0, app->beg_play_loc );
      io::uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, kEndPlayLocNumbId), app->minPerfLoc, app->maxPerfLoc, 1, 0, app->end_play_loc );

      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kBegPlayLocNumbId ), true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kEndPlayLocNumbId ), true );

      io::uiSendValue( app->ioH, io::uiFindElementUuId(app->ioH, kBegPlayLocNumbId), app->beg_play_loc);
      io::uiSendValue( app->ioH, io::uiFindElementUuId(app->ioH, kEndPlayLocNumbId), app->end_play_loc);

      // set the range of the SF reset number
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSfResetBtnId ),   true );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSfResetLocNumbId ),   true );

      io::uiSetNumbRange( app->ioH, io::uiFindElementUuId(app->ioH, kSfResetLocNumbId), app->minPerfLoc, app->maxPerfLoc, 1, 0, app->beg_play_loc );
      io::uiSendValue( app->ioH, io::uiFindElementUuId(app->ioH, kSfResetLocNumbId), app->minPerfLoc);
      
      cwLogInfo("'%s' loaded.",perf_fn);

    errLabel:
 
      _update_event_ui( app );

      if( rc != kOkRC )
        _set_status(app,"Load failed.");
      else
        _set_status(app,"%i MIDI events loaded.",midiEventN);

      //io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLoadBtnId ), false );

      return rc;
    }
    
    rc_t _on_perf_select(app_t* app, unsigned optionAppId )
    {
      rc_t              rc       = kOkRC;
      unsigned          perf_idx = kInvalidIdx;
      perf_recording_t* prp      = nullptr;

      // validate the selected menu id
      if( optionAppId < kPerfOptionBaseId  )
      {
        rc = cwLogError(kInvalidArgRC,"The performance request menu id is not valid.");
        goto errLabel;
      }

      perf_idx = optionAppId - kPerfOptionBaseId;

      // locate the selected performance record
      for(prp=app->perfRecordingBeg; prp!=nullptr; prp=prp->link)
        if( prp->id == perf_idx )
          break;          

      // if the selected performance record was not found
      if( prp == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The performance record with id:%i was not found.",perf_idx);
        goto errLabel;
      }

      printf("Loading:%s\n",prp->fname );
      
      // load the requested performance
      if((rc = _do_load_perf_score(app,prp->fname,prp->vel_tblA, prp->vel_tblN)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The performance load failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _on_alt_select(app_t* app, unsigned optionAppId )
    {
      rc_t rc = kOkRC;
      if( optionAppId == kInvalidId || optionAppId >= alt_count(app->psH))
      {
        rc = cwLogError(kInvalidArgRC,"The selected 'alt' id (%i) is invalid.",optionAppId);
        goto errLabel;
      }

      if((rc = set_alternative( app->psH, optionAppId )) != kOkRC )
      {
        rc = cwLogError(rc,"Alt selection failed.");
        goto errLabel;
      }
      
      cwLogInfo("Alt:%s selected.",alt_label(app->psH, optionAppId));

    errLabel:
      return rc;
    }

    
    rc_t _on_ui_start( app_t* app )
    {
      return _do_start(app, app->beg_play_loc, app->end_play_loc );
    }


    rc_t _set_midi_thru_state( app_t* app, bool thru_fl )
    {
      rc_t rc = kOkRC;
      
      if((rc = midi_record_play::set_thru_state(app->mrpH,thru_fl)) != kOkRC )
        rc   = cwLogError(rc,"%s MIDI thru state failed.",thru_fl ? "Enable" : "Disable" );
      
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
      bool fl1 = preset_sel::is_fragment_end_loc( app->psH, loc-1) == false;

      return fl0 && fl1;
    }


    // Called when the global play locations change
    rc_t _on_ui_play_loc(app_t* app, unsigned appId, unsigned loc)
    {
      rc_t rc = kOkRC;

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
      rc_t                      rc   = kOkRC;
      unsigned                  fragId = kInvalidId;
      unsigned                  uuId = kInvalidId;
      const preset_sel::frag_t* f      = nullptr;;

      // get the fragment id (uuid) of the selected (high-lighted) fragment
      if((fragId = preset_sel::ui_select_fragment_id(app->psH)) == kInvalidId )
      {
        rc = cwLogError(kInvalidStateRC,"There is no selected fragment to delete.");
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
        f                                     = f->prev;

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

    void _update_enable_midi_load_btn( app_t* app )
    {
      bool enableBtnFl = !app->enableRecordFl && filesys::isFile(app->midiLoadFname);
      
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kMidiLoadBtnId ), enableBtnFl );
      
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kMidiLoadFnameId ), !app->enableRecordFl );
      
    }

    rc_t _on_ui_record_check( app_t* app, bool enableRecordFl )
    {
      app->enableRecordFl = enableRecordFl;
      midi_record_play::set_record_state(app->mrpH,app->enableRecordFl);

      _update_enable_midi_load_btn(app);
      return kOkRC;
    }

    rc_t _save_midi_meta_data(app_t* app, const char* dir )
    {
      rc_t  rc       = kOkRC;
      char* fname    = nullptr;
      int   bufCharN = 255;
      unsigned bN    = 0;
      char     buf[ bufCharN+1 ]; 
      
      // form the filename of the file to save
      if((fname = filesys::makeFn( dir, "meta", "cfg", nullptr, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The MIDI save filename formation failed.");
        goto errLabel;
      }

      if((bN = snprintf(buf,bufCharN,"{ begLoc:%i }",app->sfResetLoc)) == 0)
      {
        rc = cwLogError(kOpFailRC,"The meta data buffer formation failed.");
        goto errLabel;
      }
      
      if((rc = file::fnWrite(fname,buf,bN)) != kOkRC )
        goto errLabel;
      

    errLabel:

      if( rc != kOkRC )
        rc = cwLogError(rc,"MIDI meta data file save failed on %s.",cwStringNullGuard(fname));

      mem::release(fname);
      
      return rc;
      
    }
    
    rc_t _on_midi_save_btn(app_t* app)
    {
      rc_t  rc  = kOkRC;
      char* dir = nullptr;
      char* fname = nullptr;

      // create a versioned folder in the directory specified by app->midiRecordDir
      if((dir = filesys::makeVersionedDirectory(app->midiRecordDir,app->midiRecordFolder)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The versioned directory creation failed on: '%s/%s'.",cwStringNullGuard(app->midiRecordDir),cwStringNullGuard(app->midiRecordFolder));
        goto errLabel;
      }

      // form the filename of the MIDI csv file to save
      if((fname = filesys::makeFn( dir, "midi", "csv", nullptr, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The MIDI save filename formation failed.");
        goto errLabel;
      }

      // save the MIDI data as a CSV
      if((rc = midi_record_play::save_csv(app->mrpH,fname)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The MIDI save failed on:.", cwStringNullGuard(fname));
        goto errLabel;
      }
      
      mem::release(fname);

      // write the current score reset location as the meta data
      if((rc = _save_midi_meta_data(app,dir)) != kOkRC )
        goto errLabel;
        

      // form the filename of the MIDI SVG file to save
      if((fname = filesys::makeFn( dir, "midi_svg", "html", nullptr, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The MIDI SVG filename formation failed.");
        goto errLabel;
      }

      // write the MIDI data as an SVG file
      if((rc = midi_record_play::write_svg(app->mrpH,fname)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The MIDI save failed on:'%s'.", cwStringNullGuard(fname));
        goto errLabel;
      }

      mem::release(fname);
      
      if( has_stored_performance(app->sfH) )
      {
        
        // form the filename of the MIDI csv file to save
        if((fname = filesys::makeFn( dir, "score_follow", "html", nullptr, nullptr )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The score follower SVG filename formation failed.");
          goto errLabel;
        }

        // write the score follower SVG file
        if((rc = write_svg_file( app->sfH, fname )) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower SVG create failed on:'%s'.", cwStringNullGuard(fname));
          goto errLabel;
        }

        mem::release(fname);

        // form the filename of the sync'd MIDI csv file to save
        if((fname = filesys::makeFn( dir, "play_score", "csv", nullptr, nullptr )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The score follower SVG filename formation failed.");
          goto errLabel;
        }

        // sync the performance records to the score follow info
        if((rc = score_follower::sync_perf_to_score( app->sfH )) != kOkRC )
        {
          rc = cwLogError(rc,"The score follower sync failed.");
          goto errLabel;
        }

        // write the sync'd CSV file
        if((rc = save_synced_csv( app->mrpH, fname, score_follower::perf_base(app->sfH), score_follower::perf_count(app->sfH))) != kOkRC )
        {
          rc = cwLogError(rc,"Sync'd performance CSV write failed on '%s'.",cwStringNullGuard(fname));
          goto errLabel;
        }

        // set the load filename to the name of 'play_score.csv' 
        io::uiSendValue( app->ioH, io::uiFindElementUuId( app->ioH, kMidiLoadFnameId ), fname );

      }
      
      _set_status(app,"%i events saved to %s. SVG files updated.",midi_record_play::event_count(app->mrpH),cwStringNullGuard(fname));

      
      
    errLabel:
      mem::release(fname);
      mem::release(dir);
      
      
      return rc;
    }

    /*
    rc_t _on_midi_load_btn_0(app_t* app)
    {      
      rc_t                 rc    = kOkRC;
      filesys::pathPart_t* pp    = nullptr;
      char*                fname = nullptr;
      object_t*            cfg   = nullptr;
      unsigned             sfResetLoc;
      
      if((rc = perf_score::create_from_midi_csv( app->perfScoreH, app->midiLoadFname )) != kOkRC )
      {
        rc = cwLogError(rc,"Piano score performance load failed on '%s'.",cwStringNullGuard(app->midiLoadFname));
        goto errLabel;
      }

      if((rc = _do_load_perf_score(app,nullptr)) != kOkRC )
      {
        rc = cwLogError(rc,"Performance load failed on '%s'.",cwStringNullGuard(app->midiLoadFname));
        goto errLabel;
      }
      
      if((pp = filesys::pathParts( app->midiLoadFname )) == nullptr )
      {
        rc = cwLogError(rc,"Splitting the path '%s' failed.", cwStringNullGuard(app->midiLoadFname));
        goto errLabel;
      }

      if((fname = filesys::makeFn(pp->dirStr,"meta","cfg",nullptr,nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Meta file name formation failed.");
        goto errLabel;
      }

      if((rc = objectFromFile(fname,cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Meta file read failed on '%s'.",cwStringNullGuard(fname));
        goto errLabel;
      }

      if((rc = cfg->getv("begLoc",sfResetLoc)) != kOkRC )
      {
        rc = cwLogError(rc,"Meta parse failed on '%s'.",cwStringNullGuard(fname));
        goto errLabel;
      }

      io::uiSendValue( app->ioH, io::uiFindElementUuId( app->ioH, kSfResetLocNumbId ), sfResetLoc );

      app->sfResetLoc = sfResetLoc;
      
    errLabel:

      mem::release(pp);
      mem::release(fname);
      
      return rc;
    }
    */

    rc_t _on_midi_load_btn(app_t* app)
    {      
      rc_t                 rc    = kOkRC;

      if((rc = _do_load_perf_score(app,app->midiLoadFname)) != kOkRC )
      {
        rc = cwLogError(rc,"Piano score performance load failed on '%s'.",cwStringNullGuard(app->midiLoadFname));
        goto errLabel;        
      }
      
      
    errLabel:      
      return rc;
    }
    
    rc_t _on_midi_load_fname(app_t* app, const char* fname)
    {
      rc_t rc = kOkRC;

      app->midiLoadFname = mem::reallocStr( app->midiLoadFname, fname );

      _update_enable_midi_load_btn(app);
      
      return rc;
    }

    void _on_echo_midi_enable( app_t* app, unsigned uuId,  unsigned mrp_dev_idx )
    {
      if( mrp_dev_idx <= midi_record_play::device_count(app->mrpH)  )
      {
        bool enableFl = midi_record_play::is_device_enabled(app->mrpH, mrp_dev_idx );

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
      rc_t rc= kOkRC;
      double val = 0;
      if((rc  = get_value( app->psH, kInvalidId, varId, kInvalidId, val )) != kOkRC )
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
      {
        if( app->ioFlowH.isValid() )
          if((rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, inst_label,  var_label,    flow::kAnyChIdx, (dsp::real_t)value )) != kOkRC )
            rc = cwLogError(rc,"Master value send failed on %s.%s.",cwStringNullGuard(inst_label),cwStringNullGuard(var_label));
      }
      return rc;
    }

    rc_t  _on_master_audio_meter( app_t* app, const io::msg_t& msg )
    {
      io::audio_group_dev_t* agd         = msg.u.audioGroupDev;
      unsigned               n           = std::min(agd->chCnt,2U);
      unsigned               baseUiAppId = cwIsFlag(agd->flags,io::kInFl) ? kAMtrIn0Id : kAMtrOut0Id;
      
      for(unsigned i = 0; i<n; ++i)
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
      rc_t rc               = kOkRC;
      //unsigned    uuid      = io::uiFindElementUuId( app->ioH, m.appId );
      const char* var_label = nullptr;
      //assert(uuid != kInvalidId);
      
      switch( m.appId )
      {
        case kPvWndSmpCntId:
          var_label        = "wndSmpN";
          app->pvWndSmpCnt = m.value->u.u;
          if( app->ioFlowH.isValid() )
            rc  = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "pva",  var_label,    flow::kAnyChIdx, m.value->u.u );
          break;
	  
        case kSdBypassId:
          var_label       = "bypass";
          app->sdBypassFl = m.value->u.b;
          if( app->ioFlowH.isValid() )
            rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "sd",  var_label,    flow::kAnyChIdx, m.value->u.b );
          break;
	  
        case kSdInGainId:
          var_label     = "igain";
          app->sdInGain = m.value->u.d;
          break;
	  
        case kSdCeilingId:
          var_label      = "ceiling";
          app->sdCeiling = m.value->u.d;
          break;
	  
        case kSdExpoId:
          var_label   = "expo";
          app->sdExpo = m.value->u.d;
          break;
	  
        case kSdThreshId:
          var_label     = "thresh";
          app->sdThresh = m.value->u.d;
          break;
	  
        case kSdUprId:
          var_label  = "upr";
          app->sdUpr = m.value->u.d;
          break;
	  
        case kSdLwrId:
          var_label  = "lwr";
          app->sdLwr = m.value->u.d;
          break;
	  
        case kSdMixId:
          var_label  = "mix";
          app->sdMix = m.value->u.d;
          break;

        case kCmpBypassId:
          var_label = "cmp-bypass";
          app->cmpBypassFl = m.value->u.b;
          if( app->ioFlowH.isValid() )
            rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "cmp",  "bypass",    flow::kAnyChIdx, m.value->u.b );
          break;
	  
        default:
          assert(0);
      }

      if( m.value->tid == ui::kDoubleTId && app->ioFlowH.isValid() )
        rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "sd",  var_label,    flow::kAnyChIdx, (dsp::real_t)m.value->u.d );

      if(rc != kOkRC )
        rc = cwLogError(rc,"Attempt to set a spec-dist variable '%s'",var_label );

      return rc;
    }

    rc_t _on_live_midi_checkbox( app_t* app, bool useLiveMidiFl )
    {
      rc_t rc = kOkRC;
      dsp::real_t value;
      
      if( useLiveMidiFl )
      {
        if((rc = get_value( app->psH, kInvalidId, preset_sel::kMasterSyncDelayMsVarId, kInvalidId, app->dfltSyncDelayMs )) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to access the sync delay value.");
          goto errLabel;
        }

        value = 0;
        io::uiSendValue( app->ioH, io::uiFindElementUuId( app->ioH, kSyncDelayMsId ), 0 );        
      }
      else
      {
        value = app->dfltSyncDelayMs;
        io::uiSendValue( app->ioH, io::uiFindElementUuId( app->ioH, kSyncDelayMsId ), app->dfltSyncDelayMs );        
      }

      if( app->ioFlowH.isValid() )
        if((rc = io_flow::set_variable_value( app->ioFlowH, flow_cross::kAllDestId, "sync_delay",  "delayMs",    flow::kAnyChIdx, (dsp::real_t)value )) != kOkRC )
          rc = cwLogError(rc,"Error setting sync delay 'flow' value.");
      
      
      app->useLiveMidiFl = useLiveMidiFl;
      
    errLabel:
      return rc;
    }

    rc_t _onUiInit(app_t* app, const io::ui_msg_t& m )
    {
      rc_t rc = kOkRC;

      _update_event_ui(app);

      /*
      // disable start and stop buttons until a score is loaded
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStartBtnId ),  false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kStopBtnId ),   false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kLiveCheckId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kEnaRecordCheckId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kTrackMidiCheckId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSaveBtnId ),   false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSfResetBtnId ),   false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kSfResetLocNumbId ),   false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPerfSelId ),   false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kInsertLocId ), false );
      */

      io::uiSendValue( app->ioH, io::uiFindElementUuId( app->ioH, kMidiLoadFnameId), app->midiRecordDir);

      // load the fragment data - and begin the fragment UI creation processes
      // (This is not the correct place to do this - it should be in main()
      //  but putting it here guarantees that the basic UI is already built
      // and thus will be available to _fragment_restore_ui() when it is called
      // from the io::exec callback.  FIX-THIS)
      if((rc = _fragment_load_data(app)) != kOkRC )
        rc = cwLogError(rc,"Preset data restore failed.");

      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetInterpCheckId ),      false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetAllowAllCheckId ),    false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetDryPriorityCheckId ), false );
      io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetDrySelectedCheckId ), false );

      
      _on_live_midi_checkbox(app,app->useLiveMidiFl);

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

        case kIoRtReportBtnId:
          io::realTimeReport(app->ioH);
          break;
          
        case kNetPrintBtnId:
          if( app->ioFlowH.isValid() )
            io_flow::print_network(app->ioFlowH,flow_cross::kCurDestId);
          break;
            
        case kReportBtnId:
          //preset_sel::report( app->psH );
          //io_flow::apply_preset( app->ioFlowH, 2000.0, app->tmp==0 ? "a" : "b");
          //app->tmp = !app->tmp;
          //io_flow::print(app->ioFlowH);
          //io_flow::report(app->ioFlowH);
          //midi_record_play::save_csv(app->mrpH,"/home/kevin/temp/mrp_1.csv");
          //printf("%i %i\n",app->beg_play_loc,app->end_play_loc);
          //io::realTimeReport(app->ioH);
          //score_follower::write_svg_file(app->sfH,"/home/kevin/temp/temp_sf.html");
          //score_follower::midi_state_rt_report( app->sfH, "/home/kevin/temp/temp_midi_state_rt_report.txt" );
          //score_follower::score_report(app->sfH,"/home/kevin/temp/temp_cm_score_report.txt");
          break;
          
        case kPresetReportBtnId:
          preset_sel::report_presets(app->psH);
          break;

        case kMRP_ReportBtnId:
          midi_record_play::report(app->mrpH);
          break;
          
        case kLatencyBtnId:
          latency_measure_report(app->ioH);
          latency_measure_setup(app->ioH);
          break;

        case kSaveBtnId:
          _on_ui_save(app);
          break;

        case kPerfSelId:
          _on_perf_select(app,m.value->u.u);          
          break;

        case kAltSelId:
          _on_alt_select(app,m.value->u.u);
          break;

        case kPriPresetProbCheckId:
          app->multiPresetFlags = cwEnaFlag(app->multiPresetFlags,flow::kPriPresetProbFl,m.value->u.b);

          io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetAllowAllCheckId ),    m.value->u.b );
          io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetDrySelectedCheckId ), m.value->u.b );

          break;
          
        case kSecPresetProbCheckId:
          app->multiPresetFlags = cwEnaFlag(app->multiPresetFlags,flow::kSecPresetProbFl,m.value->u.b);

          io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetInterpCheckId ),  m.value->u.b );
          
          break;

        case kPresetInterpCheckId:
          app->multiPresetFlags = cwEnaFlag(app->multiPresetFlags,flow::kInterpPresetFl,m.value->u.b);
          break;
          
        case kPresetAllowAllCheckId:
          app->multiPresetFlags = cwEnaFlag(app->multiPresetFlags,flow::kAllowAllPresetFl,m.value->u.b);
          io::uiSetEnable( app->ioH, io::uiFindElementUuId( app->ioH, kPresetDryPriorityCheckId ), m.value->u.b );
          break;

        case kPresetDryPriorityCheckId:
          app->multiPresetFlags = cwEnaFlag(app->multiPresetFlags,flow::kDryPriorityPresetFl,m.value->u.b);
          break;

        case kPresetDrySelectedCheckId:
          app->multiPresetFlags = cwEnaFlag(app->multiPresetFlags,flow::kDrySelectedPresetFl,m.value->u.b);
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

        case kGotoBtnId:
          _do_goto(app, app->beg_play_loc);
          break;
          
        case kBegPlayLocNumbId:
          _on_ui_play_loc(app, m.appId, m.value->u.i);
          break;
          
        case kEndPlayLocNumbId:
          _on_ui_play_loc(app, m.appId, m.value->u.i);
          break;

        case kLockLoctnCheckId:
          app->lockLoctnFl = m.value->u.b;
          break;

        case kLiveCheckId:
          _on_live_midi_checkbox(app, m.value->u.b );
          break;
          
        case kTrackMidiCheckId:
          app->trackMidiFl = m.value->u.b;
          break;
	  
        case kPrintMidiCheckId:
          app->printMidiFl = m.value->u.b;
          break;
          
        case kPianoMidiCheckId:
          _on_midi_enable( app, m.appId, midi_record_play::kPiano_MRP_DevIdx, m.value->u.b );
          break;

        case kSamplerMidiCheckId:
          _on_midi_enable( app, m.appId, midi_record_play::kSampler_MRP_DevIdx, m.value->u.b );
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
          
        case kInsertLocId:
          _on_ui_insert_loc(app, m.value->u.u );
          break;
          
        case kInsertBtnId:
          _on_ui_insert_btn(app);
          break;
          
        case kDeleteBtnId:
          _on_ui_delete_btn(app);
          break;

        case kEnaRecordCheckId:
          _on_ui_record_check(app, m.value->u.b);
          break;

        case kMidiSaveBtnId:
          _on_midi_save_btn(app);
          break;
          
        case kMidiLoadBtnId:
          _on_midi_load_btn(app);
          break;
          
        case kMidiLoadFnameId:
          _on_midi_load_fname(app,m.value->u.s);
          break;

        case kSfResetBtnId:
          _do_sf_reset(app,app->sfResetLoc);
          break;

        case kSfResetLocNumbId:
          app->sfResetLoc = m.value->u.u;
          _do_sf_reset(app,app->sfResetLoc);
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

        case kFragPresetAltId:          
          _on_ui_frag_value( app, m.uuId, m.value->u.s );
          break;
          
        case kFragPresetSelId:
          _on_ui_frag_value( app, m.uuId, m.value->u.b );
          break;

        case kFragPresetSeqSelId:
          _on_ui_frag_value( app, m.uuId, m.value->u.b );
          break;

        default:
          if( kVelTblMinId <= m.appId  && m.appId < kVelTblMaxId )
            vtbl::on_ui_value( app->vtH, m);
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
          _on_echo_midi_enable( app, m.uuId, midi_record_play::kPiano_MRP_DevIdx );
          break;
          
        case kSamplerMidiCheckId:
          _on_echo_midi_enable( app, m.uuId, midi_record_play::kSampler_MRP_DevIdx );
          break;

        case kPriPresetProbCheckId:
          io::uiSendValue( app->ioH, m.uuId, preset_cfg_flags(app->ioFlowH) & flow::kPriPresetProbFl );
          break;

        case kSecPresetProbCheckId:
          io::uiSendValue( app->ioH, m.uuId, preset_cfg_flags(app->ioFlowH) & flow::kSecPresetProbFl );
          break;
          
        case kPresetInterpCheckId:
          io::uiSendValue( app->ioH, m.uuId, preset_cfg_flags(app->ioFlowH) & flow::kInterpPresetFl );
          break;

        case kPresetAllowAllCheckId:
          io::uiSendValue( app->ioH, m.uuId, preset_cfg_flags(app->ioFlowH) & flow::kAllowAllPresetFl );
          break;

        case kPresetDryPriorityCheckId:
          io::uiSendValue( app->ioH, m.uuId, preset_cfg_flags(app->ioFlowH) & flow::kDryPriorityPresetFl );
          break;

        case kPresetDrySelectedCheckId:
          io::uiSendValue( app->ioH, m.uuId, preset_cfg_flags(app->ioFlowH) & flow::kDrySelectedPresetFl );
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
          //_on_echo_master_value( app, preset_sel::kMasterSyncDelayMsVarId, m.uuId );
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

        case kEnaRecordCheckId:
          io::uiSendValue( app->ioH, m.uuId, app->enableRecordFl );
          break;

        case kMidiLoadBtnId:
          _update_enable_midi_load_btn(app);
          break;

        case kSfResetLocNumbId:
          io::uiSendValue( app->ioH, m.uuId, app->sfResetLoc );
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

        default:
          if( kVelTblMinId <= m.appId && m.appId <= kVelTblMaxId )
            vtbl::on_ui_echo( app->vtH, m );
	  
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

      if( app->mrpH.isValid() )
      {
        midi_record_play::exec( app->mrpH, *m );
        
        if( midi_record_play::is_started(app->mrpH)  )
        {
          unsigned evt_cnt = 0;
          
          if( app->useLiveMidiFl )
          {
            evt_cnt = app->enableRecordFl ? midi_record_play::event_index(app->mrpH) : 0;
          }
          else
          {
            evt_cnt  = midi_record_play::event_loc(app->mrpH);
          }
          
          if( evt_cnt > 0 )
            io::uiSendValue( app->ioH, uiFindElementUuId(app->ioH,kCurMidiEvtCntId), evt_cnt );
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
            _on_live_midi_event( app, *m );
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
          _fragment_restore_ui( app );
          vtbl::exec(app->vtH);
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
  app_t app = {};
  app.trackMidiFl      = true;
  app.pvWndSmpCnt      = 512;
  app.sdBypassFl       = false;
  app.sdInGain         = 1.0;
  app.sdCeiling        = 20.0;
  app.sdExpo           = 2.0;
  app.sdThresh         = 60.0;
  app.sdUpr            = -1.1;
  app.sdLwr            = 2.0;
  app.sdMix            = 0.0;
  app.cmpBypassFl      = false;
  app.dflt_perf_app_id = kInvalidId;
	
  
  const object_t* params_cfg = nullptr;

  unsigned              vtMapN  = vtbl::get_ui_id_map_count();
  const ui::appIdMap_t* vtMap   = vtbl::get_ui_id_map( kPanelDivId );  
  unsigned              bigMapN = mapN + vtMapN;
  ui::appIdMap_t        bigMap[ bigMapN ];
  double                sysSampleRate = 0;

  for(unsigned i=0; i<mapN; ++i)
    bigMap[i] = mapA[i];
  
  for(unsigned i=0; i<vtMapN; ++i)
    bigMap[mapN+i] = vtMap[i];
  
  
  // Parse the configuration
  if((rc = _parseCfg(&app,cfg,params_cfg,argc,argv)) != kOkRC )
    goto errLabel;
        
  // create the io framework instance
  if((rc = io::create(app.ioH,cfg,_io_callback,&app,bigMap,bigMapN)) != kOkRC )
  {
    rc = cwLogError(kOpFailRC,"IO Framework create failed.");
    goto errLabel;
  }

  log::setOutputCb( log::globalHandle(),_log_output_func,&app);
  setLevel( log::globalHandle(), log::kPrint_LogLevel );
    
  //io::report(app.ioH);

  // if an input audio file is being used
  app.in_audio_dev_idx = kInvalidIdx;
  if( app.in_audio_dev_file != nullptr )
  {
    if((app.in_audio_dev_idx = audioDeviceLabelToIndex(app.ioH, app.in_audio_dev_file )) == kInvalidIdx )
    {
      rc = cwLogError(kInvalidArgRC,"The input audio device file '%s' could not be found.",cwStringNullGuard(app.in_audio_dev_file));
      goto errLabel;          
    }

    if((rc = audioDeviceEnable( app.ioH, app.in_audio_dev_idx, true, false )) != kOkRC )
    {
      rc = cwLogError(rc,"The input audio device file disable failed.");
      goto errLabel;                
    }
  }

  // get the system sample rate
  if((sysSampleRate = _get_system_sample_rate(&app, "main" )) == 0)
  {
    rc = cwLogError(rc,"The system sample rate could not be determined.");
    goto errLabel;
  }

  cwLogInfo("System sample rate:%f",sysSampleRate);

  // create the preset selection state object
  if((rc = create(app.psH, app.presets_cfg )) != kOkRC )
  {
    rc = cwLogError(kOpFailRC,"Preset state control object create failed.");
    goto errLabel;
  }

  get_value( app.psH, kInvalidId, preset_sel::kMasterSyncDelayMsVarId, kInvalidId, app.dfltSyncDelayMs );  

  // create the score follower
  if((rc = create(app.sfH, app.score_follower_cfg, sysSampleRate )) != kOkRC )
  {
    rc = cwLogError(kOpFailRC,"score follower create failed.");
    goto errLabel;
  }

  // create the MIDI record-play object
  if((rc = midi_record_play::create(app.mrpH,app.ioH,*app.midi_play_record_cfg,nullptr,_midi_play_callback,&app)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI record-play object create failed.");
    goto errLabel;
  }

  // create the vel table tuner
  if((rc = create( app.vtH, app.ioH, app.mrpH, app.velTableFname, app.velTableBackupDir)) != kOkRC )
  {
    rc = cwLogError(rc,"velocity table tuner create failed.");
    goto errLabel;
  }

  // create the performance selection menu
  if((rc= _load_perf_dir_selection_menu(&app)) != kOkRC )
  {
    rc = cwLogError(rc,"The performance list UI create failed.");
    goto errLabel;
  }

  // create the alt. selection menu
  if((rc = _load_alt_menu(&app)) != kOkRC )
  {
    rc = cwLogError(rc,"The 'alt' list UI create failed.");
    goto errLabel;
  }
  
  // create the IO Flow controller
  if( !audioIsEnabled(app.ioH) )
  {
    cwLogInfo("Audio disabled.");
  }
  else
  {
    if(app.flow_cfg==nullptr || app.flow_proc_dict==nullptr || (rc = io_flow::create(app.ioFlowH,app.ioH,sysSampleRate,app.crossFadeCnt,*app.flow_proc_dict,*app.flow_cfg)) != kOkRC )
    {
      rc = cwLogError(rc,"The IO Flow controller create failed.");
      goto errLabel;
    }
    app.multiPresetFlags = preset_cfg_flags(app.ioFlowH);
  }

  printf("ioFlow is %s valid.\n",app.ioFlowH.isValid() ? "" : "not");
  
  // start the IO framework instance
  if((rc = io::start(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"Preset-select app start failed.");
    goto errLabel;    
  }

  //io::uiReport(app.ioH);

  
  // execute the io framework
  while( !io::isShuttingDown(app.ioH))
  {
    //time::spec_t t0;
    //time::get(t0);

    // This call may block on the websocket handle.
    io::exec(app.ioH);
    
    //unsigned dMs = time::elapsedMs(t0);
    //if( dMs < 50 && app.psNextFrag == nullptr )
    //{      
    //  sleepMs( 50-dMs );      
    //}

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
