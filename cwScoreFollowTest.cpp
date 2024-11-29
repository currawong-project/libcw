#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwFileSys.h"
#include "cwMidi.h"
#include "cwMidiFile.h"

#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfMatch.h"
#include "cwSfTrack.h"
#include "cwPerfMeas.h"
#include "cwScoreFollowerPerf.h"
#include "cwScoreFollower.h"
#include "cwScoreFollowTest.h"

#include "cwSfAnalysis.h"
#include "cwSvgScoreFollow.h"

namespace cw
{
  namespace score_follow_test
  {
    typedef struct test_str
    {
      double          srate;
      const char*     score_csv_fname;
      bool            scoreParseWarnFl;
      bool            scoreWarnFl;
      bool            score_report_fl; 
      const char*     out_score_rpt_fname;
      bool            dyn_tbl_report_fl;
      const object_t* dynRefDict;

      const object_t* perfL;
      const object_t* gen_synced_cfg;
      bool            print_rt_events_fl; // print real-time score follow events from cwScoreFollowTest.cpp
      bool            pre_test_fl;     // insert a dummy note prior to the first perf. note to test the 'pre' functionality of the SVG generation
      bool            show_muid_fl;    // true = show perf. note 'muid' in SVG file, false=show sequence id
      
      score_follower::args_t sf_args;

      bool            meas_enable_fl;
      bool            meas_setup_report_fl;
      
      char*           out_dir;

      bool            write_perf_meas_json_fl;
      const char*     out_perf_meas_json_fname;

      bool            write_sf_analysis_csv_fl;
      const char*     out_sf_analysis_csv_fname;
      
      bool            write_svg_file_fl;
      const char*     out_svg_fname;
      
      bool            write_midi_csv_fl;
      const char*     out_midi_csv_fname;

    } test_t;

    
    rc_t _test_destroy( test_t* p )
    {
      rc_t rc = kOkRC;
      mem::release(p->out_dir);
      
      return rc;
    }
    
    rc_t _test_parse_cfg( test_t* p, const object_t* cfg )
    {
      rc_t        rc                         = kOkRC;
      const char* out_dir                    = nullptr;
      
      // read the test cfg.
      if((rc = cfg->getv("srate",           p->srate,
                         "score_csv_fname", p->score_csv_fname,
                         "score_parse_warn_fl",p->scoreParseWarnFl,
                         "score_warn_fl",   p->scoreWarnFl,
                         "dyn_tbl_report_fl",  p->dyn_tbl_report_fl,
                         "score_report_fl", p->score_report_fl,
                         "dyn_ref",         p->dynRefDict,

                         "perfL",        p->perfL,
                         "gen_synced_perf_files", p->gen_synced_cfg,
                         
                         "score_wnd_locN",  p->sf_args.scoreWndLocN,
                         "midi_wnd_locN",   p->sf_args.midiWndLocN,
                         "track_print_fl",  p->sf_args.trackPrintFl,
                         "track_results_backtrack_fl", p->sf_args.trackResultsBacktrackFl,

                         "print_rt_events_fl", p->print_rt_events_fl,
                         "pre_test_fl",  p->pre_test_fl,
                         "show_muid_fl", p->show_muid_fl,

                         "meas_enable_fl",       p->meas_enable_fl,
                         "meas_setup_report_fl", p->meas_setup_report_fl,
                         
                         "out_dir",             out_dir,
                         
                         "write_perf_meas_json_fl",  p->write_perf_meas_json_fl,
                         "out_perf_meas_json_fname", p->out_perf_meas_json_fname,

                         "write_sf_analysis_csv_fl", p->write_sf_analysis_csv_fl,
                         "out_sf_analysis_csv_fname",    p->out_sf_analysis_csv_fname,
                         
                         "write_svg_file_fl",   p->write_svg_file_fl,
                         "out_svg_fname",       p->out_svg_fname,
                         
                         "write_midi_csv_fl",   p->write_midi_csv_fl,                         
                         "out_midi_csv_fname",  p->out_midi_csv_fname,

                         "out_score_rpt_fname", p->out_score_rpt_fname)) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower test cfg. parse failed.");
        goto errLabel;
      }

      // expand the output directory
      if((p->out_dir = filesys::expandPath( out_dir)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The output directory path expansion failed.");
        goto errLabel;        
      }

      // create the output directory
      if( !filesys::isDir(p->out_dir))
      {
        if((rc = filesys::makeDir(p->out_dir)) != kOkRC )
        {
          cwLogError(kOpFailRC,"The output directory '%s' could not be created.", cwStringNullGuard(p->out_dir));
          goto errLabel;          
        }
      }

    errLabel:
      return rc;
    }

    rc_t _score_follow_midi_file( const char*              midi_fname,
                                  sfscore::handle_t        scoreH,
                                  score_follower::handle_t sfH,
                                  perf_meas::handle_t      perfMeasH, // perfMeasH is optional
                                  unsigned                 beg_loc,
                                  unsigned                 end_loc,
                                  double                   srate,
                                  bool                     print_rt_events_fl,                                  
                                  bool                     pre_test_fl = false,
                                  const char*              synced_perf_fname = nullptr)
    {
      rc_t                           rc   = kOkRC;
      unsigned                       msgN = 0;
      const midi::file::trackMsg_t** msgA = nullptr;
      midi::file::handle_t           mfH;
            
      // open the midi file
      if((rc = midi::file::open( mfH, midi_fname )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file open failed on '%s'.",cwStringNullGuard(midi_fname));
        goto errLabel;
      }

      // set the score follower to the start location
      if((rc = reset( sfH, beg_loc)) != kOkRC )
      {
        cwLogError(rc,"Score follower reset failed.");
        goto errLabel;
      }
      
      // set the perf meas obj to the start location
      if((rc = reset( perfMeasH, beg_loc)) != kOkRC )
      {
        cwLogError(rc,"Perf meas. unit reset failed.");
        goto errLabel;
      }
      
      // get a pointer to a time sorted list of MIDI messages in the file
      if(((msgN = msgCount( mfH )) == 0) || ((msgA = midi::file::msgArray( mfH )) == nullptr) )
      {
        rc = cwLogError(rc,"MIDI file msg array is empty or corrupt");
        goto errLabel;    
      }

      // for each midi msg
      for(unsigned i=0; i<msgN; ++i)
      {
        const midi::file::trackMsg_t* m = msgA[i];

        // if this is a note-on msg
        if( midi::file::isNoteOn( m ) )
        {
          double        sec        = (double)m->amicro/1e6;      
          unsigned long smpIdx     = (unsigned long)(srate * m->amicro/1e6);
          bool          newMatchFl = false;

          if( pre_test_fl )
          {
            // test the 'pre' ref location by adding an extra note (pitch=60) before the first note
            exec(sfH, sec, smpIdx, m->uid-1, m->status, 60, m->u.chMsgPtr->d1, newMatchFl);
            pre_test_fl = false;
          }

          //printf("%f %li %5i %3x %3i %3i\n",sec, smpIdx, m->uid, m->status, m->u.chMsgPtr->d0, m->u.chMsgPtr->d1);

          // send the note-on to the score follower
          if((rc = exec(sfH, sec, smpIdx, m->uid, m->status, m->u.chMsgPtr->d0, m->u.chMsgPtr->d1, newMatchFl )) != kOkRC )
          {
            rc = cwLogError(rc,"score follower exec failed.");
            break;
          }

          // if this note matched a score event
          if( newMatchFl )
          {
            unsigned        perfN = perf_count(sfH);
            unsigned        resN  = track_result_count(sfH);
            unsigned        resultIdxN = 0;
            const unsigned* resultIdxA = current_result_index_array(sfH, resultIdxN );

            for(unsigned i=0; i<resultIdxN; ++i)
              if( resultIdxA[i] != kInvalidIdx )
              {
                assert( resultIdxA[i] < score_follower::track_result_count(sfH) );
                
                const sftrack::result_t* r = score_follower::track_result(sfH) + resultIdxA[i];                
                const sfscore::event_t* e = event(scoreH, r->scEvtIdx );
                
                // store the performance data in the score
                set_perf( scoreH, r->scEvtIdx, r->sec, r->pitch, r->vel, r->cost );

                if( perfMeasH.isValid() )
                {
                  perf_meas::result_t pmr = {};

                  // Call performance measurement unit
                  if( perf_meas::exec( perfMeasH, e, pmr ) == kOkRC && pmr.loc != kInvalidIdx && pmr.valueA != nullptr )
                  {
                    double v[ perf_meas::kValCnt ];
                    for(unsigned i=0; i<perf_meas::kValCnt; ++i)
                      v[i] = pmr.valueA[i] == std::numeric_limits<double>::max() ? -1 : pmr.valueA[i];

                    if( print_rt_events_fl )
                      cwLogInfo("Section '%s' triggered loc:%i : dyn:%f even:%f tempo:%f cost:%f",
                                cwStringNullGuard(pmr.sectionLabel),
                                pmr.sectionLoc,
                                v[ perf_meas::kDynValIdx ],
                                v[ perf_meas::kEvenValIdx ],
                                v[ perf_meas::kTempoValIdx ],
                                v[ perf_meas::kMatchCostValIdx ] );
                  }
                }

                if(print_rt_events_fl)
                {
                  const sfscore::event_t* e = event(scoreH, r->scEvtIdx );
                  cwLogInfo("Match:%i %i %i :  %i : %i %s",resultIdxN,perfN,resN,resultIdxA[i],e->oLocId,e->sciPitch);
                }
              }
            
            clear_result_index_array( sfH );
          }
        }
      }


      if( (rc==kOkRC) and (synced_perf_fname != nullptr) )
      {
        if((rc = write_sync_perf_csv( sfH, synced_perf_fname, msgA, msgN )) != kOkRC )
        {
          rc = cwLogError(rc,"Sync-perf file generation failed on '%s'.", synced_perf_fname);
          goto errLabel;
        }
      }

    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Score follow of '%s' failed.",cwStringNullGuard(midi_fname));

      close(mfH);
      
      return rc;
    }

    rc_t _gen_synced_perf_files( test_t* p,
                                 sfscore::handle_t        scoreH,
                                 score_follower::handle_t sfH,
                                 perf_meas::handle_t      perfMeasH)
    {
      rc_t            rc        = kOkRC;
      const object_t* jobL      = nullptr;
      const char*     out_fname = nullptr;
      bool            enable_fl = false;

      // parse the cfg record
      if((rc = p->gen_synced_cfg->getv("enable_fl",enable_fl,
                                       "jobL",jobL,
                                       "out_fname",out_fname)) != kOkRC )
      {
        rc = cwLogError(rc,"Sync perf file arg. parse failed.");
        goto errLabel;
      }

      if( !enable_fl )
        goto errLabel;
      
      
      // for each job
      for(unsigned i=0; i<jobL->child_count() && rc == kOkRC; ++i)
      {
        const object_t*      job_obj = nullptr;
        const char*          dir           = nullptr;
        filesys::dirEntry_t* dirEntryArray = nullptr;
        unsigned             dirEntryCnt   = 0;

        
        if((job_obj = jobL->child_ele(i)) == nullptr || !job_obj->is_dict()  )
        {
          rc = cwLogError(kSyntaxErrorRC,"The jobL entry at index %i could not be parsed.",i);
          goto errLabel;
        }


        if((rc = job_obj->getv("dir",dir)) != kOkRC )
        {
          rc = cwLogError(rc,"The jobL entry at index '%i' parse failed.",i);
          goto errLabel;
        }

        if(( dirEntryArray = filesys::dirEntries( dir, filesys::kDirFsFl | filesys::kFullPathFsFl, &dirEntryCnt )) == nullptr )
          goto errLabel;

        for(unsigned i=0; i<dirEntryCnt and rc==kOkRC; ++i)
        {
          char*     meta_fname           = filesys::makeFn(  dirEntryArray[i].name, "meta", "cfg", NULL);
          char*     midi_fname           = filesys::makeFn(  dirEntryArray[i].name, "midi", "mid", NULL);
          char*     sync_perf_fname      = filesys::makeFn(  dirEntryArray[i].name, out_fname, NULL, NULL);
          object_t* meta_obj             = nullptr;
          unsigned  beg_loc              = kInvalidId;
          unsigned  end_loc              = kInvalidId;
          bool      skip_score_follow_fl = false;


          cwLogInfo("\nProcessing:%s",cwStringNullGuard(dirEntryArray[i].name));
          
          // read the meta object
          if((rc = objectFromFile( meta_fname, meta_obj)) != kOkRC )
            rc = cwLogError(rc,"An object could not be formed from the meta data file '%s'.",cwStringNullGuard(meta_fname));
          else
          // parse the meta object
            if((rc = meta_obj->getv("beg_loc",beg_loc,"end_loc",end_loc,"skip_score_follow_fl",skip_score_follow_fl)) != kOkRC )
              rc = cwLogError(rc,"The meta data file '%s' could not be parsed.",cwStringNullGuard(meta_fname));
            else
              if( skip_score_follow_fl )
                cwLogInfo("The `skip_score_follow_fl` is set in '%s'.",cwStringNullGuard(meta_fname));
              else
                if((rc = _score_follow_midi_file( midi_fname,
                                                  scoreH,
                                                  sfH,
                                                  perfMeasH,
                                                  beg_loc,
                                                  end_loc,
                                                  p->srate,
                                                  p->print_rt_events_fl,
                                                  false,
                                                  sync_perf_fname )) != kOkRC )
                {
                  rc = cwLogError(rc,"The score follower failed on '%s'. Consider setting the 'skip_score_follow_fl' in '%s'.",cwStringNullGuard(midi_fname),cwStringNullGuard(meta_fname));
                }
          
          mem::release(midi_fname);
          mem::release(sync_perf_fname);
          mem::release(meta_fname);
          if( meta_obj != nullptr )
            meta_obj->free();
        }

        mem::release(dirEntryArray);
      }
    errLabel:

      if( rc != kOkRC )
        rc = cwLogError(rc,"Synced performance file generation failed.");
  
      return rc;
  
    }

    
    rc_t _score_follow_one_perf( test_t* p,
                                 score_follower::handle_t sfH,
                                 sfscore::handle_t       scoreH,
                                 perf_meas::handle_t     perfMeasH,
                                 unsigned                perf_idx )
    {
      rc_t                           rc             = kOkRC;
      bool                           enable_fl      = true;
      const char*                    perf_label     = nullptr;
      const char*                    player_name    = nullptr;
      const char*                    perf_date      = nullptr;
      unsigned                       perf_take_numb = kInvalidId;
      const char*                    midi_fname     = nullptr;
      char*                          out_dir        = nullptr;
      char*                          fname          = nullptr;
      unsigned                       start_loc      = 0;
      unsigned                       end_loc        = 0;
      const object_t*                perf           = nullptr;
      filesys::pathPart_t*                    pathParts      = nullptr;
      midi::file::handle_t           mfH;      

      // get the perf. record
      if((perf = p->perfL->child_ele(perf_idx)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"Error accessing the cfg record for perf. record index:%i.",perf_idx);
        goto errLabel;
      }

      // parse the performance record
      if((rc = perf->getv("label",perf_label,
                          "enable_fl",enable_fl,
                          "player",player_name,
                          "perf_date",perf_date,
                          "take",perf_take_numb,
                          "start_loc", start_loc,
                          "end_loc", end_loc,
                          "midi_fname", midi_fname)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Error parsing cfg record for perf. record index:%i.",perf_idx);
        goto errLabel;
      }

      if( !enable_fl )
        goto errLabel;

      if((pathParts = filesys::pathParts(midi_fname)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"MIDI file name parse failed on '%s'.",cwStringNullGuard(midi_fname));
        goto errLabel;                
      }

      /*
      // create the output filename
      if((out_dir = filesys::makeFn(p->out_dir,perf_label,nullptr,nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Directory name formation failed on '%s'.",cwStringNullGuard(out_dir));
        goto errLabel;        
      }
      */

      out_dir = mem::duplStr(pathParts->dirStr);
      
      mem::release(pathParts);

      // create the output directory
      if((rc = filesys::makeDir(out_dir)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"mkdir failed on '%s'.",cwStringNullGuard(out_dir));
        goto errLabel;        
      }

      /*
      // expand the MIDI filename
      if((fname = filesys::expandPath( midi_fname )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The MIDI file path expansion failed.");
        goto errLabel;
      }
      */
      
      if((rc = _score_follow_midi_file( midi_fname,
                                        scoreH,
                                        sfH,
                                        perfMeasH,
                                        start_loc,
                                        end_loc,
                                        p->srate,
                                        p->print_rt_events_fl,
                                        p->pre_test_fl )) != kOkRC )
      {
        goto errLabel;
      }
      
      //mem::release(fname);      

      if( p->write_perf_meas_json_fl )
      {
        // create the JSON output filename
        if((fname = filesys::makeFn(out_dir,p->out_perf_meas_json_fname,nullptr,nullptr)) == nullptr )
        {
          cwLogError(kOpFailRC,"The output perf. meas. filename formation failed.");
          goto errLabel;
        }

        cwLogInfo("Writing JSON score-follow perf. meas. result to:%s",cwStringNullGuard(fname));
      
        if((rc = write_result_json(perfMeasH,player_name,perf_date,perf_take_numb,fname)) != kOkRC )
        {
          rc = cwLogError(rc,"Perf. meas. report file create failed.");
          goto errLabel;
        }
      
        mem::release(fname);
        
      }

      // Write the score following results to a CSV
      if( p->write_sf_analysis_csv_fl )
      {
        // create the JSON output filename
        if((fname = filesys::makeFn(out_dir,p->out_sf_analysis_csv_fname,nullptr,nullptr)) == nullptr )
        {
          cwLogError(kOpFailRC,"The output SF analysis CSV filename formation failed.");
          goto errLabel;
        }

        cwLogInfo("Writing SF analysis result to:%s",cwStringNullGuard(fname));

        if((rc = sf_analysis::gen_analysis( scoreH,
                                            track_result(sfH),
                                            track_result_count(sfH),
                                            start_loc,
                                            end_loc,
                                            fname )) != kOkRC )
        {
          rc = cwLogError(rc,"SF analysis CSV report failed.");
          goto errLabel;
        }
      
        mem::release(fname);
        
      }


      // write the score following result SVG
      if( p->write_svg_file_fl )
      {
        // create the SVG output filename
        if((fname = filesys::makeFn(out_dir,p->out_svg_fname,nullptr,nullptr)) == nullptr )
        {
          cwLogError(kOpFailRC,"The output SVG filename formation failed.");
          goto errLabel;
        }

        cwLogInfo("Writing SVG score-follow result to:%s",cwStringNullGuard(fname));
      
        if((rc = write_svg_file(sfH,fname,p->show_muid_fl)) != kOkRC )
        {
          rc = cwLogError(rc,"SVG report file create failed.");
          goto errLabel;
        }
      
        mem::release(fname);
      }
      

      if( p->write_midi_csv_fl )
      {
        // create the MIDI file as a CSV
        if((fname = filesys::makeFn(out_dir,p->out_midi_csv_fname,nullptr,nullptr)) == nullptr )
        {
          cwLogError(kOpFailRC,"The output MIDI CSV filename formation failed.");
          goto errLabel;
        }
        
        cwLogInfo("Writing MIDI as CSV to:%s",cwStringNullGuard(fname));

        // convert the MIDI file to a CSV
      
        if((rc = midi::file::genCsvFile( filename(mfH), fname, false)) != kOkRC )
        {
          cwLogError(rc,"MIDI file to CSV failed on '%s'.",cwStringNullGuard(filename(mfH)));
        }
        
        mem::release(fname);
      }

      
    errLabel:
      mem::release(pathParts);
      mem::release(out_dir);
      mem::release(fname);
      close(mfH);

      return rc;
    }    
     
  }
}


cw::rc_t cw::score_follow_test::test( const object_t* cfg )
{
  rc_t rc = kOkRC;
  
  score_follower::handle_t sfH;
  dyn_ref_tbl::handle_t    dynRefH;
  score_parse::handle_t    scParseH;
  perf_meas::handle_t      perfMeasH;
  perf_meas::params_t perf_meas_params;
  
  test_t t = {};

  t.sf_args.enableFl = true;
  
  char* fname = nullptr;
  
  // parse the test cfg
  if((rc = _test_parse_cfg( &t, cfg )) != kOkRC )
    goto errLabel;

  // parse the dynamics reference array
  if((rc = dyn_ref_tbl::create(dynRefH,t.dynRefDict)) != kOkRC )
  {
    rc = cwLogError(rc,"The reference dynamics array parse failed.");
    goto errLabel;
  }

  if( t.dyn_tbl_report_fl )
    report(dynRefH);

  // parse the score
  if((rc = create( scParseH, t.score_csv_fname, t.srate, dynRefH, t.scoreParseWarnFl )) != kOkRC )
  {
    rc = cwLogError(rc,"Score parse failed.");
    goto errLabel;
  }

  // create the SF score
  if((rc = create( t.sf_args.scoreH, scParseH, t.scoreWarnFl)) != kOkRC )
  {
    rc = cwLogError(rc,"sfScore create failed.");
    goto errLabel;
  }

  // Create a Perf Measurement object
  perf_meas_params.print_rt_events_fl = t.print_rt_events_fl;
  if((rc = create( perfMeasH, t.sf_args.scoreH, perf_meas_params )) != kOkRC )
  {
    rc = cwLogError(rc,"Perf. Measurement unit create failed.");
    goto errLabel;
  }

  if( t.meas_setup_report_fl )
    report( perfMeasH );
  
  // create the score follower
  if((rc = create( sfH, t.sf_args )) != kOkRC )
  {
    rc = cwLogError(rc,"Score follower create failed.");
    goto errLabel;
  }
  
  // create score report filename
  if((fname = filesys::makeFn(t.out_dir,t.out_score_rpt_fname,nullptr,nullptr)) == nullptr )
  {
    cwLogError(kOpFailRC,"The output cm score filename formation failed.");
    goto errLabel;
  }
      
  // write the cm score report
  if( t.score_report_fl )
    score_report(sfH,fname);

  // score follow each performance
  for(unsigned perf_idx=0; perf_idx<t.perfL->child_count(); ++perf_idx)
    _score_follow_one_perf(&t,sfH,t.sf_args.scoreH,perfMeasH,perf_idx);

  // score follow and generate synced performance files
  rc = _gen_synced_perf_files(&t,t.sf_args.scoreH,sfH,perfMeasH);
  
 errLabel:
  mem::release(fname);
  destroy(sfH);
  destroy(perfMeasH);
  destroy(t.sf_args.scoreH);
  destroy(scParseH);
  destroy(dynRefH);
  _test_destroy(&t);
  
  return rc;
}

