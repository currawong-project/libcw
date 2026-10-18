#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwCsv.h"

#include "cwCsv.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwScoreFollow3.h"
#include "cwScoreFollow3Test.h"

namespace cw
{
  namespace score_follow_3
  {
    typedef struct perf_note_str
    {
      unsigned uid;
      double   sec;
      unsigned pitch;
      unsigned vel;      
    } perf_note_t;

    typedef struct gnd_truth_note_str
    {
      unsigned          perf_uid;
      unsigned          pitch;
      unsigned          loc_id;
      unsigned          meas_num;
      match_status_id_t status;
    } gnd_truth_note_t;
    
    typedef struct run_args_str
    {
      unsigned     beg_meas_num;
      unsigned     end_meas_num;
      
      unsigned     beg_loc_id;
      unsigned     end_loc_id;

      unsigned     beg_perf_uid;
      unsigned     end_perf_uid;

      double       self_test_time_scale; // >0 if this is a self-test run (see _create_self_test_perf())
      bool         end_timeout_fl;       // passed to reset(): wait for the end timeout rather than ending at the last location

      char*        perf_fname;   // performance file name or the generated self-test name
      char*        gnd_truth_fname;
      char*        result_fname;
      
      perf_note_t* noteA;
      unsigned     noteN;

      result_t*    resultA;
      unsigned     resultN;

      gnd_truth_note_t* gnd_truthA;
      unsigned          gnd_truthN;

      double*           seed_agree_pctA; // seed_agree_pctA[seed_cnt] - gnd-truth agreement of the run with each random seed (see test())

      double*           note_usA;        // note_usA[seed_cnt*noteN] - on_new_note() duration in microseconds of each note with each random seed
      unsigned*         seed_note_cntA;  // seed_note_cntA[seed_cnt] - count of notes timed with each random seed

      unsigned*         status_cntA;     // status_cntA[kStatusCntN] - over all seeds: count of results with each status_id_t, then the count of tracking/searching changes (see _check_status())
      
    } run_args_t;
    
    typedef struct test_cfg_str
    {
      double      srate;
      unsigned    smp_per_cycle;
      char*       score_fname;
      char*       out_dir;
      bool        output_json_fl;
      double      exec_tail_sec;   // duration of exec() calls after the last note, which allows the end of region timeout to expire
      bool        end_timeout_fl;  // default for run_args_t.end_timeout_fl
      unsigned    seed_cnt;        // each run is repeated with the seeds sf_args.rand_seed to sf_args.rand_seed+seed_cnt-1

      args_t      sf_args;

      run_args_t* run_argsA;
      unsigned    run_argsN;

      score_event_t* score_evtA;  
      unsigned       score_evtN;
      
    } test_args_t;

    rc_t _test_args_destroy( test_args_t& test_args )
    {
      rc_t rc = kOkRC;
      
      for(unsigned i=0; i<test_args.run_argsN; ++i)
      {
        mem::release(test_args.run_argsA[i].noteA);
        mem::release(test_args.run_argsA[i].resultA);
        mem::release(test_args.run_argsA[i].gnd_truthA);
        mem::release(test_args.run_argsA[i].perf_fname);
        mem::release(test_args.run_argsA[i].result_fname);
        mem::release(test_args.run_argsA[i].gnd_truth_fname);
        mem::release(test_args.run_argsA[i].seed_agree_pctA);
        mem::release(test_args.run_argsA[i].note_usA);
        mem::release(test_args.run_argsA[i].seed_note_cntA);
        mem::release(test_args.run_argsA[i].status_cntA);
      }
      
      test_args.run_argsN = 0;
      mem::release(test_args.score_evtA);
      mem::release(test_args.run_argsA);
      mem::release(test_args.score_fname);
      mem::release(test_args.out_dir);
      
      return rc;
    }

    rc_t _parse_perf_file( run_args_t& run_args )
    {
      rc_t           rc           = kOkRC;
      file::handle_t fH;
      char*          buf          = nullptr;
      unsigned       buf_byte_cnt = 0;
      unsigned       line_idx     = 0;
      unsigned       line_cnt = kInvalidCnt;

      if((rc = file::open(fH,run_args.perf_fname,file::kReadFl)) != kOkRC )
      {
        rc = cwLogError(rc,"Performance file open failed on '%s'.",cwStringNullGuard(run_args.perf_fname));
        goto errLabel;
                        
      }

      if((rc = lineCount(fH,line_cnt)) != kOkRC )
      {
        rc = cwLogError(rc,"The line count of the performance file '%s' could not be determined.",cwStringNullGuard(run_args.perf_fname));
        goto errLabel;
      }

      run_args.noteA = mem::allocZ<perf_note_t>(line_cnt);
      run_args.noteN = 0;

      for(; (rc = file::getLineAuto(fH,&buf,buf_byte_cnt))==kOkRC; ++line_idx)
      {
        unsigned       uid    = 0;
        unsigned       trk    = 0;
        unsigned       dtick  = 0;
        unsigned       atick  = 0;
        unsigned       amicro = -1;
        const unsigned typeN  = 16;
        char           msg_type[ typeN ];
        unsigned       ch     = 0;
        unsigned       d0     = -1;
        unsigned       d1     = -1;
        
        if( line_idx == 0 )
          continue;

        msg_type[0] = 0;
        int tok_cnt = sscanf(buf,"%u,%u,%u,%u,%u,%15[^,], %u,%u,%u",
                               &uid,&trk,&dtick,&atick,&amicro,msg_type,&ch,&d0,&d1);
        if( tok_cnt != 9 )
        {
          // if the end-of-file was encountered
          if( tok_cnt == 6 && textIsEqual(msg_type,"eot") )
          {
            continue;
          }
          
          rc = cwLogError(kSyntaxErrorRC,"Parse failed on perf. file line number:%i. %i tokens parsed",line_idx+1,tok_cnt);
          goto errLabel;
        }

        // verify that the uid is the in the configured beg/end range 
        if( uid < run_args.beg_perf_uid || run_args.end_perf_uid < uid )
          continue;

        // if this is a note-on
        if( textIsEqual(nextNonWhiteChar(msg_type),"non",3) )
        {
          perf_note_t& note = run_args.noteA[run_args.noteN];

          note.uid   = uid;
          note.sec   = (double)amicro / 1000000.0;
          note.pitch = d0;
          note.vel   = d1;
          
          run_args.noteN += 1;
        }        
      }

      run_args.resultN = run_args.noteN;
      run_args.resultA = mem::allocZ<result_t>(run_args.resultN);
      
      if( rc == kEofRC )
        rc = kOkRC;
      
      
    errLabel:
      file::close(fH);

      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Parsing failed on the performance file '%s'.",cwStringNullGuard(run_args.perf_fname));
      }
      return rc;
    }

    const gnd_truth_note_t* _uid_to_gnd_truth_note( const run_args_t& ra, unsigned perf_uid, unsigned max_search_idx )
    {
      for(unsigned i=0; i<=max_search_idx && i<ra.gnd_truthN; ++i)
        if( ra.gnd_truthA[i].perf_uid == perf_uid )
          return ra.gnd_truthA + i;
      return nullptr;
    }

    rc_t  _perf_uid_to_note( const run_args_t& ra, unsigned perf_note_uid, const perf_note_t*& perf_note_ref )
    {
      perf_note_ref = nullptr;
      for(unsigned i=0; i<ra.noteN; ++i)
        if( ra.noteA[i].uid == perf_note_uid )
        {
          perf_note_ref = ra.noteA + i;
          return kOkRC;
        }

      return cwLogError(kEleNotFoundRC,"The perf. note associated with UID %i could not be found.",perf_note_uid);
    }
    
    rc_t _validate_gt(const test_args_t& ta, const run_args_t& ra, const gnd_truth_note_t& gt )
    {
      rc_t               rc             = kOkRC;
      const perf_note_t* perf_note      = nullptr;
      bool               loc_is_null_fl = false;
      unsigned           loc_cnt        = 0;      
      bool               ok_fl          = false;

      // verify that this ground-truth record is qualified for this validation
      if( gt.perf_uid==kInvalidId )
      {
        rc = cwLogError(kInvalidStateRC,"The perf. uid for the ground truth is not a valid value.");
        goto errLabel;
      }

      if( gt.perf_uid < ra.beg_perf_uid || ra.end_perf_uid < gt.perf_uid )
      {
        rc = cwLogError(kInvalidStateRC,"The ground truth note uid:%i is outside the specified perf. uid range: %i to %i .",gt.perf_uid,ra.beg_perf_uid,ra.end_perf_uid);
        goto errLabel;
      }

      // every ground-truth record must reference a perf. note record 
      if((rc = _perf_uid_to_note( ra, gt.perf_uid, perf_note )) != kOkRC )
      {
        rc = cwLogError(kInvalidStateRC,"The ground truth note uid:%i could not be found in the perf. note list.",gt.perf_uid);
        goto errLabel;
      }

      // verify that the perf. note pitc matches the  gnd-truth pitch
      if( perf_note->pitch != gt.pitch )
      {
        rc = cwLogError(kInvalidStateRC,"The ground truth pitch %i does not match the perf. note pitch %i for the perf. note uid:%i.",gt.pitch,perf_note->pitch,gt.perf_uid);
        goto errLabel;
      }
      
      switch( gt.status )
      {
        case kBlankTId:
          ok_fl = true;
          goto errLabel; 
          break;
          
        case kMatchTId:
          loc_is_null_fl = gt.loc_id == kInvalidId;
          break;
          
        case kMismatchTId:
          loc_is_null_fl = gt.loc_id == kInvalidId;
          break;
          
        case kNoMatchTId:
          ok_fl = true;
          goto errLabel;
          break;
          
        default:
          rc = cwLogError(kInvalidStateRC,"An unknown match status id %i was encountered.",gt.status);
          goto errLabel;
      }

      assert( gt.status==kMatchTId || gt.status==kMismatchTId );
      
      // if the location id is set to kInvalidId on a 'match' or 'mismatch' location
      if( loc_is_null_fl )
      {
        rc = cwLogError(kInvalidStateRC,"A 'match' or 'mismatch' ground-truth record must have a valid location id.");
        goto errLabel;
      }


      // verify that the score loc pitch matches the ground-truth pittch
      for(unsigned i=0; i<ta.score_evtN; ++i)
        if( ta.score_evtA[i].loc_id == gt.loc_id )
        {
          loc_cnt += 1;

          if( gt.meas_num != ta.score_evtA[i].meas_num )
          {
            rc = cwLogError(kInvalidStateRC,"The ground truth measure number %i does not match the score measure number %i on perf uid:%i.",gt.meas_num, ta.score_evtA[i].meas_num, gt.perf_uid);
            goto errLabel;
          }
          
          if( gt.status==kMatchTId && ta.score_evtA[i].pitch == gt.pitch)
          {
            ok_fl = true;
            break;
          }

          if( gt.status==kMismatchTId )
          {
            if( ta.score_evtA[i].pitch == gt.pitch )
            {
              rc = cwLogError(kInvalidStateRC,"The ground truth location id:%i pitch:%i is marked as 'mismatch' but the pitches match.",gt.loc_id,gt.pitch);
              goto errLabel;
            }
          }
        }

      if( loc_cnt == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"The ground truth location id:%i was not found in the score.",gt.loc_id);
        goto errLabel;
      }

      // ground truth 'mismatch' records cannot be marked as mismatching until all pitches at gt.loc_id have been
      // verified to not match the performed pitch
      if( gt.status == kMismatchTId )
        ok_fl = true;
      
      if( !ok_fl )
      {
        rc = cwLogError(kInvalidStateRC,"The pitch at location %i does not coincide with the 'matched' ground truth pitch (%i) for uid %i.",gt.loc_id,gt.pitch,gt.perf_uid);
        goto errLabel;
      }
      
    errLabel:
      return rc;
    }


    rc_t _parse_gnd_truth_file( const test_args_t& test_args, run_args_t& ra )
    {      
      rc_t           rc        = kOkRC;
      rc_t           rc0       = kOkRC;
      const char*    titlesA[] = { "perf_uid","pitch","loc_id","meas","match_status_id" };
      const unsigned titlesN   = std::size(titlesA);
      unsigned       line_cnt  = 0;
      unsigned       line_idx  = 0;
      int            loc_id    = 0;
      unsigned       gt_allocN  = 0;
      csv::handle_t  csvH;

      // Open the CSV and verify the existence of some of the columns
      if((rc = csv::create(csvH,ra.gnd_truth_fname,titlesA,titlesN)) != kOkRC )
      {
        rc = cwLogError(rc,"The ground-truth CSV file '%s' could not be opened.",cwStringNullGuard(ra.gnd_truth_fname));
        goto errLabel;
      }

      // get a count of the lines (including the title line) in the CSV
      if((rc = line_count(csvH,line_cnt)) != kOkRC )
      {
        rc = cwLogError(rc,"Line count determination failed.");
        goto errLabel;
      }

      if( line_cnt == 0 )
      {
        cwLogWarning("The ground truth file '%s' is empty.",cwStringNullGuard(ra.gnd_truth_fname));
        goto errLabel;        
      }

      // allocate the ground truth array
      gt_allocN     = line_cnt - 1;      
      ra.gnd_truthA = mem::allocZ<gnd_truth_note_t>(gt_allocN);
      ra.gnd_truthN = 0;

      // for each line in the gnd-truth CSV
      for(line_idx=0; (rc = next_line(csvH)) == kOkRC && line_idx<gt_allocN; ++line_idx )
      {
        gnd_truth_note_t& gt = ra.gnd_truthA[ra.gnd_truthN];
        const char* status_label = nullptr;

        // parse the rest of the fields
        if((rc = getv(csvH,
                      "perf_uid",gt.perf_uid,
                      "pitch",gt.pitch,
                      "loc_id",loc_id,
                      "meas",gt.meas_num )) != kOkRC )
        {
          rc = cwLogError(rc,"Ground truth record parse failed on line index:%i.",line_idx);
          goto errLabel;
        }

        // ignore GT records that are out of the specified performance range
        if( gt.perf_uid < ra.beg_perf_uid || ra.end_perf_uid < gt.perf_uid )
          continue;

        // commit to storing the new GT record
        ra.gnd_truthN += 1;
        
        // get the status field as a string
        if((rc = parse_field(csvH,title_col_index(csvH,"match_status_id"),status_label)) != kOkRC || status_label==nullptr)
        {
          rc = cwLogError(rc,"The ground truth 'status' field could not be parsed on line index:%i.",line_idx);
          goto errLabel;
        }

        // convert the status label into an id.
        if((rc = match_status_label_to_id(status_label,gt.status)) != kOkRC )
        {
          rc0 = cwLogError(rc,"The matching status label '%s' on line index:%i is not valid.",cwStringNullGuard(status_label),line_idx);
        }

        
        gt.loc_id = loc_id<0 ? kInvalidId : (unsigned)loc_id;

        // verify that the perf_uid is not duplicated 
        if( ra.gnd_truthN>1 && _uid_to_gnd_truth_note(ra, gt.perf_uid, ra.gnd_truthN-2 ) != nullptr )
        {
          rc0 = cwLogError(kInvalidStateRC,"The perf. note uid=%i is duplicated in the ground truth data on line index:%i.",gt.perf_uid,line_idx);
        }

        if((rc = _validate_gt(test_args,ra,gt) ) != kOkRC )
        {
          rc0 = cwLogError(rc,"The perf. note uid:%i does not pass the status validation check on line index:%i.",gt.perf_uid,line_idx);
        }          


      }

      // if the end-of-file was encountered - this is not an error
      if( rc == kEofRC )
      {
        rc = kOkRC;

        if( line_idx != gt_allocN )
        {
          rc = cwLogError(kInvalidStateRC,"The count of lines parsed (%i) does not match the count of lines in the ground truth file (%i).",line_idx,gt_allocN);
          goto errLabel;
        }
      }
      
    errLabel:
      csv::destroy(csvH);

      rc = rcSelect(rc,rc0);
      
      if( rc != kOkRC )
        rc = cwLogError(rc,"Parsing failed on ground-truth file '%s'.",cwStringNullGuard(ra.gnd_truth_fname));

      return rc;
      
    }

    // Set 'return_last_loc_id_fl' to true if the last loc_id in the measure should be returned rather than the first.
    rc_t _meas_to_loc_id(const test_args_t& test_args, unsigned meas_num, bool return_last_loc_id_fl, unsigned& loc_id_ref)      
    {
      rc_t rc = kOkRC;
      loc_id_ref = kInvalidId;
      
      for(unsigned i=0; i<test_args.score_evtN; ++i)
        if( test_args.score_evtA[i].meas_num == meas_num )
        {
          loc_id_ref = test_args.score_evtA[i].loc_id;
          
          if(!return_last_loc_id_fl)
            return rc;
        }
        else
        {
          if( loc_id_ref != kInvalidId )
            return rc;
        }
          

      if( loc_id_ref == kInvalidId )
        rc = cwLogError(kInvalidArgRC,"The score measure %i is not valid in the score '%s'.",meas_num,cwStringNullGuard(test_args.score_fname));
      
    errLabel:
      return rc;
      
    }

    // Self-test: form the performance from the score notes of the region beg_loc_id:end_loc_id
    // rather than from a performance file.
    //
    // Each note is performed at its score time, relative to the first note of the region,
    // multiplied by 'self_test_time_scale' (e.g. 1.25 is a performance 25% slower than the score).
    // The perf_uid of each note is its index into test_args.score_evtA[].
    //
    // A ground-truth record, labeling each note as a match at its own location, is generated
    // for every note. The follower should therefore match close to 100% of the notes and its
    // mean time_fact should converge to 'self_test_time_scale'.
    rc_t _create_self_test_perf( const test_args_t& test_args, run_args_t& ra )
    {
      rc_t     rc      = kOkRC;
      unsigned note_cnt = 0;
      double   beg_sec  = 0;

      if( ra.self_test_time_scale <= 0 )
      {
        rc = cwLogError(kInvalidArgRC,"The self-test time scale (%f) must be greater than 0.",ra.self_test_time_scale);
        goto errLabel;
      }

      // count the score notes in the region and locate the time of the first one
      for(unsigned i=0; i<test_args.score_evtN; ++i)
      {
        const score_event_t& se = test_args.score_evtA[i];
        if( ra.beg_loc_id <= se.loc_id && se.loc_id <= ra.end_loc_id )
        {
          if( note_cnt == 0 )
            beg_sec = se.sec;
          note_cnt += 1;
        }
      }

      if( note_cnt == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"The self-test score region loc:%i-%i is empty.",ra.beg_loc_id,ra.end_loc_id);
        goto errLabel;
      }

      ra.noteA      = mem::allocZ<perf_note_t>(note_cnt);
      ra.resultA    = mem::allocZ<result_t>(note_cnt);
      ra.gnd_truthA = mem::allocZ<gnd_truth_note_t>(note_cnt);

      // score_evtA[] is in time order so the generated performance is also in time order
      for(unsigned i=0; i<test_args.score_evtN; ++i)
      {
        const score_event_t& se = test_args.score_evtA[i];

        if( ra.beg_loc_id <= se.loc_id && se.loc_id <= ra.end_loc_id )
        {
          perf_note_t&      note = ra.noteA[ ra.noteN++ ];
          gnd_truth_note_t& gt   = ra.gnd_truthA[ ra.gnd_truthN++ ];

          note.uid   = i;
          note.sec   = (se.sec - beg_sec) * ra.self_test_time_scale;
          note.pitch = se.pitch;
          note.vel   = se.velocity;

          gt.perf_uid = i;
          gt.pitch    = se.pitch;
          gt.loc_id   = se.loc_id;
          gt.meas_num = se.meas_num;
          gt.status   = kMatchTId;
        }
      }

      ra.resultN = ra.noteN;

      // name the run for the log
      ra.perf_fname = mem::printf<char>(nullptr,"self_test_m%i-%i_x%03i",ra.beg_meas_num,ra.end_meas_num,(unsigned)std::lround(ra.self_test_time_scale*100));

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Self-test performance creation failed.");
      return rc;
    }

    rc_t _parse_perf_list_cfg( const object_t* perf_cfgL, test_args_t& test_args )
    {
      rc_t rc = kOkRC;

      // validate the perf. list
      if( perf_cfgL == nullptr || !perf_cfgL->is_list() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'perfL' is missing or not a list.");
        goto errLabel;
      }

      // allocate the perf. array
      test_args.run_argsN = perf_cfgL->child_count();
      test_args.run_argsA = mem::allocZ<run_args_t>(test_args.run_argsN);

      // for each performance file
      for(unsigned i=0; i<test_args.run_argsN; ++i)
      {
        const object_t* run_cfg   = perf_cfgL->child_ele(i);
        const char*     fname     = nullptr;
        const char*     gt_fname  = nullptr;
        const char*     res_fname = nullptr;
        run_args_t&     ra        = test_args.run_argsA[i];

        // the per-run value is optional and defaults to the global value
        ra.end_timeout_fl = test_args.end_timeout_fl;

        ra.seed_agree_pctA = mem::allocZ<double>(test_args.seed_cnt);

        // read the configuration record
        if((rc = run_cfg->readv("beg_meas",kReqFl,ra.beg_meas_num,
                                "end_meas",kReqFl,ra.end_meas_num,
                                "beg_perf_uid",kReqFl,ra.beg_perf_uid,
                                "end_perf_uid",kReqFl,ra.end_perf_uid,
                                "fn",      kOptFl,fname,
                                "gt_fn",   kOptFl,gt_fname,
                                "result_fn",kReqFl,res_fname,
                                "self_test_time_scale",kOptFl,ra.self_test_time_scale,
                                "end_timeout_fl",kOptFl,ra.end_timeout_fl)) != kOkRC )
        {
          rc = cwLogError(rc,"Parse error on perfL element at index %i.",i);
          goto errLabel;
        }

        // a run is either a performance file or a self-test
        if( (fname != nullptr) == (ra.self_test_time_scale != 0) )
        {
          rc = cwLogError(kSyntaxErrorRC,"The perfL element at index %i must have exactly one of 'fn' or 'self_test_time_scale'.",i);
          goto errLabel;
        }

        // a self-test generates its own ground truth
        if( ra.self_test_time_scale != 0 && gt_fname != nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"The perfL element at index %i is a self-test and therefore may not have a 'gt_fn'.",i);
          goto errLabel;
        }

        // convert the start measure to a score location
        if((rc = _meas_to_loc_id(test_args,ra.beg_meas_num,false,ra.beg_loc_id)) != kOkRC )
        {
          goto errLabel;
        }
        
        // convert the end measure to a score location
        if((rc = _meas_to_loc_id(test_args,ra.end_meas_num,true,ra.end_loc_id)) != kOkRC )
        {
          goto errLabel;
        }

        // validate the performance range
        if( ra.beg_perf_uid > ra.end_perf_uid )
        {
          rc = cwLogError(kSyntaxErrorRC,"The performance uid start uid:%i is greater than the end uid:%i.",ra.beg_perf_uid,ra.end_perf_uid);
          goto errLabel;
        }

        if( ra.self_test_time_scale != 0 )
        {
          // form the performance, and its ground truth, from the score
          if((rc = _create_self_test_perf( test_args, ra )) != kOkRC )
          {
            goto errLabel;
          }
        }
        else
        {
          // expand the perf. filename
          if((ra.perf_fname = filesys::expandPath(fname)) == nullptr )
          {
            rc = cwLogError(kOpFailRC,"Performance file path expansion failed on '%s'.",cwStringNullGuard(fname));
            goto errLabel;
          }

          // parse the file in to run_cfg_t.noteA[]
          if((rc = _parse_perf_file( ra )) != kOkRC )
          {
            goto errLabel;
          }

          // if a ground truth file was given
          if( gt_fname != nullptr )
          {
            // expand the gnd-truth filename
            if((ra.gnd_truth_fname = filesys::expandPath(gt_fname))  == nullptr )
            {
              rc = cwLogError(kOpFailRC,"Performance ground-truth path expansion failed on '%s'.",cwStringNullGuard(gt_fname));
              goto errLabel;
            }

            // verify that the ground truth file actually exists - it won't on the first pass because the ground truth
            // file is created from the first result file.
            if( !filesys::isFile(ra.gnd_truth_fname)  )
            {
              cwLogWarning("Ground truth analysis will be skipped. The specified file: %s does not exist.",cwStringNullGuard(ra.gnd_truth_fname));
              mem::release(ra.gnd_truth_fname); // 
            }
            else
            {
              // parse the gnd-truth file
              if((rc = _parse_gnd_truth_file(test_args,ra)) != kOkRC )
              {
                goto errLabel;
              }
            }
          }
        }
        
        // the result file name is given explicitly because several runs may share one performance file
        if((ra.result_fname = filesys::makeFn(test_args.out_dir, res_fname, nullptr, nullptr )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The result file name could not be formed from '%s'.",cwStringNullGuard(res_fname));
          goto errLabel;
        }

        // a duplicate result file name would silently overwrite an earlier run's results
        for(unsigned j=0; j<i; ++j)
          if( textIsEqual(test_args.run_argsA[j].result_fname,ra.result_fname) )
          {
            rc = cwLogError(kSyntaxErrorRC,"The perfL elements at index %i and %i have the same result file '%s'.",j,i,cwStringNullGuard(ra.result_fname));
            goto errLabel;
          }

        cwLogInfo("Perf notes:%i meas:%i-%i loc:%i-%i %s.",ra.noteN,ra.beg_meas_num,ra.end_meas_num,ra.beg_loc_id,ra.end_loc_id,cwStringNullGuard(ra.perf_fname));
        
      }
    errLabel:
      return rc;
    }
    
    rc_t _parse_follower_cfg( const object_t* sf_cfg, args_t& sf_args, double srate, unsigned smp_per_cycle )
    {
      rc_t rc = kOkRC;

      if((rc = sf_cfg->readv("pitch_min",kReqFl,sf_args.pitch_min,
                            "pitch_max",kReqFl,sf_args.pitch_max,
                            "wnd_pre_sec",kReqFl,sf_args.wnd_pre_sec,
                            "wnd_post_sec",kReqFl,sf_args.wnd_post_sec,
                            "wnd_pre_loc_cnt",kReqFl,sf_args.wnd_pre_loc_cnt,
                            "wnd_post_loc_cnt",kReqFl,sf_args.wnd_post_loc_cnt,
                            "particle_cnt",kReqFl,sf_args.particle_cnt,
                            "rand_seed",kReqFl,sf_args.rand_seed,
                            "prior_match",kReqFl,sf_args.prior_match,
                            "prior_mismatch",kReqFl,sf_args.prior_mismatch,
                            "prior_nomatch",kReqFl,sf_args.prior_nomatch,
                            "time_sigma_sec",kReqFl,sf_args.time_sigma_sec,
                            "time_fact_prior_sigma",kReqFl,sf_args.time_fact_prior_sigma,
                            "time_fact_walk_sigma",kReqFl,sf_args.time_fact_walk_sigma,
                            "pos_walk_sigma",kReqFl,sf_args.pos_walk_sigma,
                            "time_fact_min",kReqFl,sf_args.time_fact_min,
                            "time_fact_max",kReqFl,sf_args.time_fact_max,
                            "jump_stay",kReqFl,sf_args.jump_stay,
                            "jump_next",kReqFl,sf_args.jump_next,
                            "jump_skip",kReqFl,sf_args.jump_skip,
                            "jump_back",kReqFl,sf_args.jump_back,
                            "jump_decay",kReqFl,sf_args.jump_decay,
                            "loc_group_sec",kReqFl,sf_args.loc_group_sec,
                            "late_grp_cnt",kReqFl,sf_args.late_grp_cnt,
                            "jump_late",kReqFl,sf_args.jump_late,
                            "late_sigma_sec",kReqFl,sf_args.late_sigma_sec,
                            "end_zone_sec",kReqFl,sf_args.end_zone_sec,
                            "done_timeout_mult",kReqFl,sf_args.done_timeout_mult,
                            "done_timeout_min_sec",kReqFl,sf_args.done_timeout_min_sec,
                            "status_wnd_cnt",kReqFl,sf_args.status_wnd_cnt,
                            "status_lo",kReqFl,sf_args.status_lo,
                            "status_hi",kReqFl,sf_args.status_hi,
                            "report_level",kReqFl,sf_args.report_level)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed on follower args.");
        goto errLabel;
      }

      sf_args.exec_period_sec = smp_per_cycle/srate;
      
    errLabel:
      return rc;
    }


    rc_t _parse_score_get_note_count( const object_t* loc_cfgD, unsigned& note_cnt_ref )
    {
      rc_t rc = kOkRC;
      unsigned locN = 0;
      
      note_cnt_ref = 0;
      
      if( loc_cfgD == nullptr || !loc_cfgD->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The score 'locD' is not a dictionary.");
        goto errLabel;
      }
      
      locN = loc_cfgD->child_count();

      for(unsigned score_idx=0; score_idx<locN; ++score_idx)
      {
        const object_t* loc_pair = loc_cfgD->child_ele(score_idx);
        const object_t* note_cfgL = nullptr;
                
        if(loc_pair == nullptr || !loc_pair->is_pair() || loc_pair->pair_label() == nullptr || loc_pair->pair_value() == nullptr || !loc_pair->pair_value()->is_dict() )
        {
          rc = cwLogError(kSyntaxErrorRC,"The location pair at score file location index %i is invalid.",score_idx);
          goto errLabel;
        }

        if((rc = loc_pair->pair_value()->getv("noteL",note_cfgL)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"Parse failed on 'noteL' access at score file location %i.",score_idx);
          goto errLabel;
        }

        if(note_cfgL == nullptr || !note_cfgL->is_list() )
        {
          rc = cwLogError(kSyntaxErrorRC,"The 'noteL' at score file location %i is not a list.",score_idx);
          goto errLabel;
        }

        note_cnt_ref += note_cfgL->child_count();        
      }
      
    errLabel:      
      return rc;
    }
    
    rc_t _parse_score( test_args_t& test_args )
    {
      rc_t            rc                = kOkRC;
      object_t*       score_cfg         = nullptr;
      const object_t* loc_cfgD          = nullptr;
      unsigned        score_cfg_loc_cnt = 0;
      unsigned        score_evt_idx     = 0;
      
      if((rc = objectFromFile(test_args.score_fname,score_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"File to object conversion of the score file failed.");
        goto errLabel;
      }

      if((rc = score_cfg->getv("locD",loc_cfgD)) != kOkRC )
      {
        rc = cwLogError(rc,"Score parse error extracing the 'locD' structure.");
        goto errLabel;
      }

      if((rc = _parse_score_get_note_count( loc_cfgD, test_args.score_evtN )) != kOkRC )
      {
        goto errLabel;
      }

      test_args.score_evtA = mem::allocZ<score_event_t>( test_args.score_evtN );
      
      score_cfg_loc_cnt = loc_cfgD->child_count();
      

      for(unsigned i=0; i<score_cfg_loc_cnt; ++i)
      {
        const object_t* loc_pair     = loc_cfgD->child_ele(i);
        const object_t* note_cfgL    = nullptr;
        unsigned        loc_id       = kInvalidId;
        unsigned        loc_note_cnt = 0;
        double          sec          = 0;
        unsigned        meas_num     = 0;
        
        if((rc = string_to_number(loc_pair->pair_label(),loc_id)) != kOkRC )
        {
          rc = cwLogError(rc,"Location id string to int conversion of %s failed at score file index %i.",cwStringNullGuard(loc_pair->pair_label()),i);
          goto errLabel;
        }

        if((rc = loc_pair->pair_value()->getv("meas",meas_num,
                                              "sec",sec,
                                              "noteL",note_cfgL)) != kOkRC )
        {
          rc = cwLogError(rc,"Parse failed at score file index %i.",i);
          goto errLabel;
        }

        loc_note_cnt = note_cfgL->child_count();

        for(unsigned ni=0; ni<loc_note_cnt; ++ni,++score_evt_idx)
        {
          const object_t* note_cfg = note_cfgL->child_ele(ni);

          if( score_evt_idx >= test_args.score_evtN )
          {
            rc = cwLogError(kBufTooSmallRC,"The test score event array is too small.");
            goto errLabel;
          }
          
          score_event_t& se = test_args.score_evtA[score_evt_idx];

          if((rc = note_cfg->getv("pitch",se.pitch,
                                  "dyn",se.velocity,
                                  "grace_fl",se.grace_fl)) != kOkRC )
          {
            rc = cwLogError(rc,"Parse of note at score file index %i failed.",i);
            goto errLabel;
          }

          se.meas_num = meas_num;
          se.sec      = sec;
          se.loc_id   = loc_id;
        }        
      }

      if( score_evt_idx != test_args.score_evtN )
      {
        rc = cwLogError(kInvalidStateRC,"The count of score events parsed (%i) does not match the expected count (%i).",score_evt_idx,test_args.score_evtN);
        goto errLabel;
      }

      std::sort(test_args.score_evtA,test_args.score_evtA+test_args.score_evtN, [](const score_event_t& se0,const score_event_t& se1){ return se0.sec<se1.sec; } );
      
    errLabel:
      if( score_cfg != nullptr )
        score_cfg->free();
      
      if(rc != kOkRC )
        rc = cwLogError(rc,"Score file parse failed on '%s'.",cwStringNullGuard(test_args.score_fname));

      
      return rc;
    }

    
    rc_t _parse_cfg( const object_t* cfg, test_args_t& test_args )
    {
      rc_t            rc          = kOkRC;
      const char*     score_fname = nullptr;
      const char*     out_dir     = nullptr;
      const object_t* perf_cfgL   = nullptr;
      const object_t* sf_cfg      = nullptr;

      // Parse the top level of the test cfg.
      if((rc = cfg->getv("srate",test_args.srate,
                         "smp_per_cycle",test_args.smp_per_cycle,
                         "score_fname",score_fname,
                         "out_dir",out_dir,
                         "output_json_fl",test_args.output_json_fl,
                         "exec_tail_sec",test_args.exec_tail_sec,
                         "end_timeout_fl",test_args.end_timeout_fl,
                         "seed_cnt",test_args.seed_cnt,
                         "perfL", perf_cfgL,
                         "follower",sf_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse top level test cfg. failed.");
        goto errLabel;
      }

      if( test_args.seed_cnt < 1 )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'seed_cnt' must be at least 1.");
        goto errLabel;
      }

      // The comparison is written so that NaN fails it.
      if( !(test_args.exec_tail_sec >= 0) )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'exec_tail_sec' (%f) must not be negative.",test_args.exec_tail_sec);
        goto errLabel;
      }

      // expand the score filename
      if(( test_args.score_fname = filesys::expandPath(score_fname)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Score file name expansion failed.");
        goto errLabel;
      }

      if(( test_args.out_dir = filesys::expandPath(out_dir)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The output directory name expansion failed.");
        goto errLabel;
      }

      // if the output directory does not exist
      if( !filesys::isDir(test_args.out_dir) )
      {
        if((rc = filesys::makeDir( test_args.out_dir )) != kOkRC )
        {
          rc = cwLogError(rc,"The output directory create attempt failed on: '%s'.",cwStringNullGuard(test_args.out_dir));
          goto errLabel;
        }
      }

      // parse the 'follower' args
      if((rc = _parse_follower_cfg( sf_cfg, test_args.sf_args, test_args.srate, test_args.smp_per_cycle )) != kOkRC )
      {
        rc = cwLogError(rc,"Parse score follower args. failed.");
        goto errLabel;
      }

      // parse the score file
      if((rc = _parse_score( test_args )) != kOkRC )
      {
        rc = cwLogError(rc,"Score parse failed.");
        goto errLabel;
      }

      cwLogInfo("Score events:%i %s.",test_args.score_evtN,cwStringNullGuard(test_args.score_fname));


      // parse the test performane list
      if((rc = _parse_perf_list_cfg( perf_cfgL, test_args )) != kOkRC )
      {
        rc = cwLogError(rc,"Parse perf. list failed.");
        goto errLabel;
      }
      
    errLabel:
      if(rc != kOkRC )
        cwLogError(rc,"Parse test cfg. failed.");
          
      return rc;
    }


    rc_t _loc_id_to_meas( const test_args_t& ta, unsigned loc_id, unsigned& meas_ref )
    {
      meas_ref = 0;

      // if the loc_id is invalid then we return an invalid meas as 0 (which is also invalid)
      if( loc_id == kInvalidId )
        return kOkRC;
      
      for(unsigned i=0; i<ta.score_evtN; ++i)
        if( ta.score_evtA[i].loc_id == loc_id )
        {
          meas_ref = ta.score_evtA[i].meas_num;
          return kOkRC;
        }
     
      return cwLogError(kEleNotFoundRC,"The score event associated with loc_id %i could not be found.",loc_id);
    }

    rc_t _write_result_json( const test_args_t& ta, const run_args_t& ra, unsigned result_cnt )
    {
      rc_t rc = kOkRC;
      
      object_t* root    = newDictObject();
      object_t* resultL = newListObject();
      root->insert_pair("resultL",resultL);

      for(unsigned i=0; i<result_cnt; ++i)
      {
        const result_t& res = ra.resultA[i];
        object_t*          d                  = newDictObject(resultL);
        const char*        match_status_label = match_status_id_to_label(res.match_status_id);
        const perf_note_t* perf_note          = nullptr;
        unsigned           meas_num           = 0;
        
        if((rc = _perf_uid_to_note(ra,res.perf_uid,perf_note)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _loc_id_to_meas( ta, res.loc_id, meas_num )) != kOkRC )
        {
          goto errLabel;
        }

        
        d->insert_pair("perf_uid",res.perf_uid);
        d->insert_pair("pitch",perf_note->pitch);
        d->insert_pair("loc_id",res.loc_id);
        d->insert_pair("meas",meas_num);
        d->insert_pair("match_status_id",cwStringNullGuard(match_status_label));
        d->insert_pair("confidence", res.confidence);
        d->insert_pair("score_vel", res.score_vel==kInvalidId ? -1 : (int)res.score_vel);
        d->insert_pair("status", cwStringNullGuard(status_id_to_label(res.status_id)));
      }

      if((rc = objectToFile( ra.result_fname, root )) != kOkRC )
      {
        rc = cwLogError(rc,"Result file write failed on '%s'.",cwStringNullGuard(ra.result_fname));
        goto errLabel;
      }

    errLabel:
      if( root != nullptr )
        root->free();

      return rc;
    }

    rc_t _write_result_csv( const test_args_t& ta, const run_args_t& ra, unsigned result_cnt )
    {
      rc_t rc = kOkRC;
      file::handle_t fH;
      
      if((rc = file::open(fH,ra.result_fname,file::kWriteFl)) != kOkRC )
      {
        goto errLabel;
      }

      if((rc = file::printf(fH,"perf_uid,pitch,loc_id,meas,confidence,match_status_id,score_vel,status\n")) != kOkRC )
      {
        rc = cwLogError(rc,"Title write failed.");
        goto errLabel;
      }
      
      for(unsigned i=0; i<result_cnt; ++i)
      {
        const result_t&    res                = ra.resultA[i];
        const char*        match_status_label = match_status_id_to_label(res.match_status_id);
        const perf_note_t* perf_note          = nullptr;
        unsigned           meas_num           = 0;
        
        if((rc = _perf_uid_to_note(ra,res.perf_uid,perf_note)) != kOkRC )
        {
          goto errLabel;
        }
        
        if((rc = _loc_id_to_meas( ta, res.loc_id, meas_num )) != kOkRC )
        {
          goto errLabel;
        }
        
        if((rc = file::printf(fH,"%i,%i,%i,%i,%f,%s,%i,%s\n",res.perf_uid,perf_note->pitch,res.loc_id,meas_num,res.confidence,match_status_label,res.score_vel==kInvalidId ? -1 : (int)res.score_vel,cwStringNullGuard(status_id_to_label(res.status_id)))) != kOkRC )
        {
          rc = cwLogError(rc,"Data write failed on line index:%i.",i);
          goto errLabel;
        }
      }
      
    errLabel:
      file::close(fH);

      if( rc != kOkRC )
        rc = cwLogError(rc,"Result file write failed on file '%s'.",cwStringNullGuard(ra.result_fname));
      
      return rc;
    }

    // Verify result_t.score_vel against the score: a match must report the velocity of the score note at loc_id with the
    // performed pitch and every other result must report -1. score_evtA[] is in increasing loc_id order (see create()).
    // If report_fl is set the count of checked matches is logged.
    rc_t _check_score_vel( const test_args_t& ta, const run_args_t& ra, unsigned result_cnt, bool report_fl )
    {
      unsigned match_cnt = 0;  // count of match results checked against a score velocity
      unsigned err_cnt   = 0;  // count of results whose score_vel is wrong

      for(unsigned i=0; i<result_cnt; ++i)
      {
        const result_t&    res          = ra.resultA[i];
        const perf_note_t* perf_note    = nullptr;
        unsigned           expected_vel = kInvalidId;

        if( _perf_uid_to_note(ra,res.perf_uid,perf_note) != kOkRC )
          return kInvalidStateRC;

        if( res.match_status_id == kMatchTId && res.loc_id != kInvalidId )
        {
          const score_event_t* se = std::lower_bound(ta.score_evtA, ta.score_evtA + ta.score_evtN, res.loc_id,
                                                     [](const score_event_t& e, unsigned loc_id){ return e.loc_id < loc_id; });

          for(; se < ta.score_evtA + ta.score_evtN && se->loc_id == res.loc_id; ++se)
            if( se->pitch == perf_note->pitch )
            {
              expected_vel = se->velocity;
              break;
            }

          match_cnt += 1;
        }

        if( res.score_vel != expected_vel )
        {
          if( err_cnt < 10 )
            cwLogError(kInvalidStateRC,"score_vel mismatch on uid:%i pitch:%i loc:%i status:%s: expected:%i actual:%i",
                       res.perf_uid, perf_note->pitch, res.loc_id, cwStringNullGuard(match_status_id_to_label(res.match_status_id)),
                       (int)expected_vel, (int)res.score_vel);
          err_cnt += 1;
        }
      }

      if( err_cnt > 0 )
        return cwLogError(kInvalidStateRC,"%i of %i results reported the wrong score_vel on '%s'.",err_cnt,result_cnt,cwStringNullGuard(ra.result_fname));

      if( report_fl )
        cwLogInfo("score_vel check: %i results OK (%i matches) on '%s'.",result_cnt,match_cnt,cwStringNullGuard(ra.result_fname));

      return kOkRC;
    }

    enum { kStatusChangeIdx = kDoneStatusId+1, kStatusCntN };  // status_cntA[] layout: one count per status_id_t then the change count

    // Check the status reported during a run and add its counts to r.status_cntA[]:
    // - the status after the run is kDoneStatusId exactly when is_done() is true;
    // - a result after a done result is also done (done lasts until the next reset());
    // - no result of a self-test run is searching.
    // If report_fl is set the run's status counts are logged.
    rc_t _check_status( const run_args_t& r, handle_t sfH, unsigned result_cnt, bool report_fl )
    {
      rc_t     rc            = kOkRC;
      unsigned cntA[ kStatusCntN ] = {};
      bool     done_fl       = false;
      bool     searching_fl  = false;

      if( (status(sfH) == kDoneStatusId) != is_done(sfH) )
        rc = cwLogError(kInvalidStateRC,"The final status (%s) does not agree with is_done() (%i) on '%s'.",
                        cwStringNullGuard(status_id_to_label(status(sfH))),is_done(sfH),cwStringNullGuard(r.result_fname));

      for(unsigned i=0; i<result_cnt; ++i)
      {
        status_id_t id = r.resultA[i].status_id;

        if( id > kDoneStatusId )
        {
          rc = cwLogError(kInvalidStateRC,"An unknown status id %i was reported on perf uid:%i.",id,r.resultA[i].perf_uid);
          continue;
        }

        if( done_fl && id != kDoneStatusId )
          rc = cwLogError(kInvalidStateRC,"The status changed from done to %s on perf uid:%i on '%s'.",
                          cwStringNullGuard(status_id_to_label(id)),r.resultA[i].perf_uid,cwStringNullGuard(r.result_fname));

        if( id == kSearchingStatusId && r.self_test_time_scale > 0 )
          rc = cwLogError(kInvalidStateRC,"A self-test reported searching on perf uid:%i on '%s'.",r.resultA[i].perf_uid,cwStringNullGuard(r.result_fname));

        // count the changes between tracking and searching
        if( id == kTrackingStatusId || id == kSearchingStatusId )
        {
          if( (id == kSearchingStatusId) != searching_fl && i > 0 )
            cntA[ kStatusChangeIdx ] += 1;
          searching_fl = id == kSearchingStatusId;
        }

        done_fl     = id == kDoneStatusId;
        cntA[id]   += 1;
      }

      for(unsigned i=0; i<kStatusCntN; ++i)
        r.status_cntA[i] += cntA[i];

      if( report_fl )
        cwLogInfo("Status: %s  tracking:%i searching:%i finishing:%i done:%i changes:%i",cwStringNullGuard(r.result_fname),
                  cntA[kTrackingStatusId],cntA[kSearchingStatusId],cntA[kFinishingStatusId],cntA[kDoneStatusId],cntA[kStatusChangeIdx]);

      return rc;
    }

    double _pct( unsigned n, unsigned d )
    { return d==0 ? 0.0 : (100.0 * n) / d; }

    // Compare the follower results to the ground truth and print a summary.
    // Results whose perf. note has no ground-truth record, or whose ground-truth status is 'blank', are not scored.
    // Score the results against the gnd-truth and return the agreement (status and location) percentage in agree_pct_ref.
    // If report_fl is false nothing is logged.
    rc_t _report_gnd_truth_score( const test_args_t& ta, const run_args_t& ra, unsigned result_cnt, bool report_fl, double& agree_pct_ref )
    {
      rc_t           rc            = kOkRC;
      const unsigned statusN       = kNoMatchTId + 1;
      unsigned       confuseM[ statusN ][ statusN ] = {};  // confuseM[ gnd-truth status ][ result status ]
      unsigned       gt_label_cnt  = 0;  // count of gnd-truth records with a non-blank status
      unsigned       scored_cnt    = 0;  // count of results that were compared to a gnd-truth record
      unsigned       status_ok_cnt = 0;  // count of results whose status matches the gnd-truth status
      unsigned       loc_cnt       = 0;  // count of gnd-truth 'match' and 'mismatch' records which were scored
      unsigned       loc_ok_cnt    = 0;  // count of results whose location matches a gnd-truth 'match' or 'mismatch' location
      unsigned       agree_cnt     = 0;  // count of results that agree with the gnd-truth in both status and location

      for(unsigned i=0; i<ra.gnd_truthN; ++i)
        if( ra.gnd_truthA[i].status != kBlankTId )
          gt_label_cnt += 1;

      if( report_fl && ta.sf_args.report_level >= 2 )
        cwLogInfo("Ground truth disagreements: %s",cwStringNullGuard(ra.result_fname)); // unique per run, unlike the perf. and gnd-truth files

      // for each result
      for(unsigned i=0; i<result_cnt; ++i)
      {
        const result_t&         res      = ra.resultA[i];
        const gnd_truth_note_t* gt       = _uid_to_gnd_truth_note(ra, res.perf_uid, kInvalidIdx );
        bool                    loc_fl   = false;
        bool                    status_fl= false;
        bool                    loc_ok_fl= true;
        unsigned                res_meas = 0;

        // skip results which are not labeled in the gnd-truth
        if( gt == nullptr || gt->status == kBlankTId )
          continue;

        if( res.match_status_id >= statusN )
        {
          rc = cwLogError(kInvalidStateRC,"An unknown result match status id %i was encountered on perf uid:%i.",res.match_status_id,res.perf_uid);
          goto errLabel;
        }

        scored_cnt += 1;
        confuseM[ gt->status ][ res.match_status_id ] += 1;

        status_fl = res.match_status_id == gt->status;
        if( status_fl )
          status_ok_cnt += 1;

        // only 'match' and 'mismatch' gnd-truth records have a location to compare against
        loc_fl = gt->status == kMatchTId || gt->status == kMismatchTId;
        if( loc_fl )
        {
          loc_cnt  += 1;
          loc_ok_fl = res.loc_id == gt->loc_id;
          if( loc_ok_fl )
            loc_ok_cnt += 1;
        }

        if( status_fl && loc_ok_fl )
        {
          agree_cnt += 1;
          continue;
        }

        if( report_fl && ta.sf_args.report_level >= 2 )
        {
          if((rc = _loc_id_to_meas( ta, res.loc_id, res_meas )) != kOkRC )
            goto errLabel;

          cwLogInfo("uid:%5i pitch:%3i  gt: loc:%5i meas:%3i %-8s  sf: loc:%5i meas:%3i %-8s conf:%5.3f",
                    res.perf_uid, gt->pitch,
                    gt->loc_id==kInvalidId ? -1 : (int)gt->loc_id, gt->meas_num, cwStringNullGuard(match_status_id_to_label(gt->status)),
                    res.loc_id==kInvalidId ? -1 : (int)res.loc_id, res_meas,     cwStringNullGuard(match_status_id_to_label(res.match_status_id)),
                    res.confidence);
        }
      }

      agree_pct_ref = _pct(agree_cnt,scored_cnt);

      if( !report_fl )
        goto errLabel;

      cwLogInfo("Ground truth score: %s",cwStringNullGuard(ra.perf_fname));
      cwLogInfo("  labeled:%i scored:%i (labeled notes not reached by the run:%i)",gt_label_cnt,scored_cnt,gt_label_cnt-scored_cnt);
      cwLogInfo("  agree (status and loc): %4i / %4i  %6.2f%%",agree_cnt,scored_cnt,_pct(agree_cnt,scored_cnt));
      cwLogInfo("  status correct:         %4i / %4i  %6.2f%%",status_ok_cnt,scored_cnt,_pct(status_ok_cnt,scored_cnt));
      cwLogInfo("  loc correct:            %4i / %4i  %6.2f%%  (gt match + mismatch only)",loc_ok_cnt,loc_cnt,_pct(loc_ok_cnt,loc_cnt));

      // print the confusion matrix: rows=gnd-truth status, columns=result status
      cwLogInfo("  confusion (rows=gt, cols=sf):  %8s %8s %8s %8s",
                match_status_id_to_label(kBlankTId),match_status_id_to_label(kMatchTId),match_status_id_to_label(kMismatchTId),match_status_id_to_label(kNoMatchTId));

      for(unsigned gt_tid=kMatchTId; gt_tid<statusN; ++gt_tid)
        cwLogInfo("  %28s:  %8i %8i %8i %8i",
                  match_status_id_to_label((match_status_id_t)gt_tid),
                  confuseM[gt_tid][kBlankTId],confuseM[gt_tid][kMatchTId],confuseM[gt_tid][kMismatchTId],confuseM[gt_tid][kNoMatchTId]);

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Ground truth scoring failed on '%s'.",cwStringNullGuard(ra.perf_fname));
      return rc;
    }

    // How and when is_done() first became true during a run (see _run_test()).
    typedef enum
    {
      kNotDoneId,     // is_done() never became true
      kEndLocDoneId,  // on_new_note() reported the last location of the region
      kTimeoutDoneId  // exec() found that the end of region timeout had expired
    } done_by_id_t;

    typedef struct done_str
    {
      done_by_id_t id;
      double       sec;     // performance time at which is_done() became true
      unsigned     noteN;   // count of notes given to the follower before is_done() became true
    } done_t;

    // Record the first time that is_done() becomes true.
    void _check_done( handle_t sfH, done_by_id_t id, double sec, unsigned noteN, done_t& done_ref )
    {
      if( done_ref.id == kNotDoneId && is_done(sfH) )
      {
        done_ref.id    = id;
        done_ref.sec   = sec;
        done_ref.noteN = noteN;
      }
    }

    // Report how the end of the region was detected.
    // The detection was premature if a note after it has a gnd-truth location before the last location of the region.
    // Notes of the final location which arrive after it was reported are therefore not counted.
    void _report_done( const test_args_t& ta, const run_args_t& r, const done_t& done )
    {
      double last_sec = r.noteA[ r.noteN-1 ].sec;

      if( done.id == kNotDoneId )
      {
        cwLogInfo("End detection: %s  mode:%s NOT DONE after a %.1f sec tail.",cwStringNullGuard(r.result_fname),r.end_timeout_fl ? "wait" : "immediate",ta.exec_tail_sec);
        return;
      }

      unsigned premature_cnt = 0;
      for(unsigned i=done.noteN; i<r.noteN; ++i)
      {
        const gnd_truth_note_t* gt = _uid_to_gnd_truth_note(r, r.noteA[i].uid, kInvalidIdx );
        if( gt != nullptr && (gt->status == kMatchTId || gt->status == kMismatchTId) && gt->loc_id < r.end_loc_id )
          premature_cnt += 1;
      }

      char premature_buf[64];
      if( r.gnd_truthA == nullptr )
        snprintf(premature_buf,sizeof(premature_buf),"n/a");
      else
        snprintf(premature_buf,sizeof(premature_buf),"%s (%i notes)",premature_cnt>0 ? "PREMATURE" : "no",premature_cnt);

      cwLogInfo("End detection: %s  mode:%s by:%s at %.2f sec (%.2f sec after the last note) notes after done:%i premature:%s",
                cwStringNullGuard(r.result_fname), r.end_timeout_fl ? "wait" : "immediate",
                done.id == kEndLocDoneId ? "end_loc" : "timeout",
                done.sec, done.sec - last_sec, r.noteN - done.noteN, premature_buf);
    }

    // Run one performance. If report_fl is false the result file is not written and nothing is reported,
    // but the gnd-truth agreement is still stored in r.seed_agree_pctA[seed_idx].
    rc_t _run_test( test_args_t& test_args, handle_t sfH, const run_args_t& r, unsigned seed_idx, bool report_fl )
    {
      done_t done = { kNotDoneId, 0, 0 };

      rc_t   rc   = kOkRC;
      double sec0 = 0.0;
      unsigned note_idx = 0;
      
      if( r.noteN == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"The performance note list is empty.");
        goto errLabel;
      }

      // Reset the active score follower section to beg_loc_id:end_loc_id
      if((rc = reset(sfH,r.beg_loc_id,r.end_loc_id,r.end_timeout_fl)) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower reset failed on locations: beg=%i end=%i.",r.beg_loc_id,r.end_loc_id);
        goto errLabel;
      }

      if( status(sfH) != kResetStatusId )
      {
        rc = cwLogError(kInvalidStateRC,"The status after reset() is %s rather than reset.",cwStringNullGuard(status_id_to_label(status(sfH))));
        goto errLabel;
      }

      sec0 = r.noteA[0].sec;
      
      // for each performed note ...
      for(note_idx=0; note_idx<r.noteN; ++note_idx)
      {
        const perf_note_t note = r.noteA[note_idx];

        assert((note.sec - sec0) >= 0);

        // run 'exec' for the elapsed time between notes
        for(double exec_sec = sec0; note_idx>0 && exec_sec < note.sec; exec_sec += test_args.sf_args.exec_period_sec )
        {
          if((rc = exec(sfH,exec_sec)) != kOkRC )
          {
            rc = cwLogError(rc,"Score follower 'exec' failed.");
            goto errLabel;
          }

          _check_done(sfH, kTimeoutDoneId, exec_sec, note_idx, done);
        }
        
        sec0 = note.sec;

        // submit the note to the score follower
        time::spec_t t0 = time::current_time();
        
        if((rc = on_new_note(sfH,note.uid,note.sec,note.pitch,note.vel,r.resultA[note_idx])) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower failed on note uid:%i.",note.uid);
          goto errLabel;
        }

        time::spec_t t1 = time::current_time();
        r.note_usA[ seed_idx*r.noteN + note_idx ] = (t1.tv_sec - t0.tv_sec)*1e6 + (t1.tv_nsec - t0.tv_nsec)*1e-3;
        r.seed_note_cntA[ seed_idx ] = note_idx + 1;

        _check_done(sfH, kEndLocDoneId, note.sec, note_idx+1, done);
      }

      // Continue to run 'exec' after the last note so that the end of region timeout can expire.
      for(double exec_sec = sec0; done.id == kNotDoneId && exec_sec <= sec0 + test_args.exec_tail_sec; exec_sec += test_args.sf_args.exec_period_sec )
      {
        if((rc = exec(sfH,exec_sec)) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower 'exec' failed.");
          goto errLabel;
        }

        _check_done(sfH, kTimeoutDoneId, exec_sec, note_idx, done);
      }

      if( report_fl )
        _report_done(test_args, r, done);

    errLabel:

      rc_t rc1 = kOkRC;
      if( report_fl )
      {
        if( test_args.output_json_fl )
          rc1 = _write_result_json(test_args,r,note_idx);
        else
          rc1 = _write_result_csv(test_args,r,note_idx);
      }

      rc1 = rcSelect(rc1,_check_score_vel(test_args,r,note_idx,report_fl));
      rc1 = rcSelect(rc1,_check_status(r,sfH,note_idx,report_fl));

      // if ground truth was given then score the results
      if( r.gnd_truthA != nullptr )
        rc1 = rcSelect(rc1,_report_gnd_truth_score(test_args,r,note_idx,report_fl,r.seed_agree_pctA[seed_idx]));

      if( rc != kOkRC )
        cwLogError(rc,"Run test failed on '%s'.",cwStringNullGuard(r.perf_fname));
      
      return rcSelect(rc,rc1);
      
    }
    // Report the mean, min. and max. gnd-truth agreement of each run over the random seeds.
    void _report_seed_summary( const test_args_t& ta )
    {
      unsigned seed_beg = ta.sf_args.rand_seed;

      cwLogInfo("Seed summary: agreement (status and loc) over %i seeds (%i-%i)",ta.seed_cnt,seed_beg,seed_beg+ta.seed_cnt-1);

      for(unsigned i=0; i<ta.run_argsN; ++i)
      {
        const run_args_t& r = ta.run_argsA[i];

        if( r.gnd_truthA == nullptr )
        {
          cwLogInfo("  %-45s  (no gnd-truth)",cwStringNullGuard(r.result_fname));
          continue;
        }

        double sum = 0;
        double mn  = r.seed_agree_pctA[0];
        double mx  = r.seed_agree_pctA[0];

        for(unsigned k=0; k<ta.seed_cnt; ++k)
        {
          sum += r.seed_agree_pctA[k];
          mn   = std::min(mn, r.seed_agree_pctA[k]);
          mx   = std::max(mx, r.seed_agree_pctA[k]);
        }

        // report the result file name without the directory since it is unique per run
        const char* name = r.result_fname;
        for(const char* c=r.result_fname; c!=nullptr && *c; ++c)
          if( *c == '/' )
            name = c + 1;

        cwLogInfo("  %-45s mean:%6.2f%%  min:%6.2f%%  max:%6.2f%%",cwStringNullGuard(name),sum/ta.seed_cnt,mn,mx);
      }
    }

    // Sort vA[] in place and log the count, mean, median, p99 and max.
    void _report_timing_line( const char* label, double* vA, unsigned vN )
    {
      if( vN == 0 )
      {
        cwLogInfo("  %-45s  (no timed notes)",cwStringNullGuard(label));
        return;
      }
      
      double sum = 0;
      for(unsigned i=0; i<vN; ++i)
        sum += vA[i];

      std::sort(vA,vA+vN);

      cwLogInfo("  %-45s n:%6i  mean:%8.1f  p50:%8.1f  p99:%8.1f  max:%8.1f",cwStringNullGuard(label),vN,sum/vN,vA[vN/2],vA[ std::min(vN-1,(unsigned)(0.99*vN)) ],vA[vN-1]);
    }
    
    // Report the on_new_note() duration statistics of each run and of all runs together.
    // The reported seed (k==0) is left out when the follower logs (report_level > 0) because the log time is in the call.
    // Report, for each run over all seeds, the percentage of results with each status and the mean count of tracking/searching changes per seed.
    void _report_status_summary( const test_args_t& ta )
    {
      cwLogInfo("Status summary: percent of notes in each status over %i seeds, and tracking/searching changes per seed",ta.seed_cnt);

      for(unsigned i=0; i<ta.run_argsN; ++i)
      {
        const run_args_t& r = ta.run_argsA[i];
        unsigned          n = 0;

        if( r.status_cntA == nullptr )
          continue;

        for(unsigned k=0; k<=kDoneStatusId; ++k)
          n += r.status_cntA[k];

        // report the result file name without the directory since it is unique per run
        const char* name = r.result_fname;
        for(const char* c=r.result_fname; c!=nullptr && *c; ++c)
          if( *c == '/' )
            name = c+1;

        cwLogInfo("  %-45s tracking:%6.2f%% searching:%6.2f%% finishing:%6.2f%% done:%6.2f%% changes:%5.1f",cwStringNullGuard(name),
                  _pct(r.status_cntA[kTrackingStatusId],n),_pct(r.status_cntA[kSearchingStatusId],n),
                  _pct(r.status_cntA[kFinishingStatusId],n),_pct(r.status_cntA[kDoneStatusId],n),
                  (double)r.status_cntA[kStatusChangeIdx]/ta.seed_cnt);
      }
    }

    void _report_timing_summary( const test_args_t& ta )
    {
      unsigned seed_beg = ta.sf_args.report_level == 0 ? 0 : 1;
      unsigned allN     = 0;
      double*  allA     = nullptr;
      unsigned allIdx   = 0;
      
      for(unsigned i=0; i<ta.run_argsN; ++i)
        allN += ta.run_argsA[i].noteN * ta.seed_cnt;

      allA = mem::allocZ<double>(allN);

      cwLogInfo("Timing summary: on_new_note() duration in microseconds over seeds %i-%i",ta.sf_args.rand_seed+seed_beg,ta.sf_args.rand_seed+ta.seed_cnt-1);

      for(unsigned i=0; i<ta.run_argsN; ++i)
      {
        const run_args_t& r      = ta.run_argsA[i];
        unsigned          runIdx = allIdx;

        if( r.note_usA != nullptr )
          for(unsigned k=seed_beg; k<ta.seed_cnt; ++k)
            for(unsigned j=0; j<r.seed_note_cntA[k]; ++j)
              allA[ allIdx++ ] = r.note_usA[ k*r.noteN + j ];

        // report the result file name without the directory since it is unique per run
        const char* name = r.result_fname;
        for(const char* c=r.result_fname; c!=nullptr && *c; ++c)
          if( *c == '/' )
            name = c + 1;

        // sort a copy so that allA[] keeps the run's values for the overall line
        double* runA = allIdx > runIdx ? mem::allocDupl<double>(allA + runIdx, allIdx - runIdx) : nullptr;
        _report_timing_line(name, runA, allIdx - runIdx);
        mem::release(runA);
      }

      _report_timing_line("all", allA, allIdx);
      
      mem::release(allA);
    }
  }
}

cw::rc_t cw::score_follow_3::test( const object_t* cfg )
{
  rc_t rc = kOkRC;
  handle_t sfH;
  test_args_t test_args{};
  
  if((rc = _parse_cfg(cfg,test_args)) != kOkRC )
  {
    goto errLabel;
  }

  // Each run is repeated once for each random seed. The seed is an args_t field, applied at create() and
  // re-applied at each reset(), so the follower is re-created for each seed. Only the first seed, which is the
  // configured rand_seed, writes the result file and reports. The other seeds only record their gnd-truth agreement.
  for(unsigned i=0; i<test_args.run_argsN; ++i)
  {
    for(unsigned k=0; k<test_args.seed_cnt; ++k)
    {
      args_t sf_args    = test_args.sf_args;
      bool   report_fl  = k == 0;

      sf_args.rand_seed = test_args.sf_args.rand_seed + k;

      // the follower's log is only of interest for the reported seed
      if( !report_fl )
        sf_args.report_level = 0;

      if((rc = create( sfH, sf_args, test_args.score_evtA, test_args.score_evtN )) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower create failed.");
        goto errLabel;
      }

      if( test_args.run_argsA[i].note_usA == nullptr )
      {
        test_args.run_argsA[i].note_usA       = mem::allocZ<double>(test_args.seed_cnt * test_args.run_argsA[i].noteN);
        test_args.run_argsA[i].seed_note_cntA = mem::allocZ<unsigned>(test_args.seed_cnt);
        test_args.run_argsA[i].status_cntA    = mem::allocZ<unsigned>(kStatusCntN);
      }

      rc = _run_test(test_args, sfH, test_args.run_argsA[i], k, report_fl);

      destroy( sfH );

      if( rc != kOkRC )
        goto errLabel;
    }
  }

  _report_seed_summary( test_args );
  _report_status_summary( test_args );
  _report_timing_summary( test_args );

errLabel:
  if(rc != kOkRC )
     rc = cwLogError(rc,"Score-follower-3 test failed.");

  
  destroy( sfH );

  
  _test_args_destroy( test_args );
  
  return rc;
}

