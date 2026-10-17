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

      char*        perf_fname;   // performance file name or the generated self-test name
      char*        gnd_truth_fname;
      char*        result_fname;
      
      perf_note_t* noteA;
      unsigned     noteN;

      result_t*    resultA;
      unsigned     resultN;

      gnd_truth_note_t* gnd_truthA;
      unsigned          gnd_truthN;
      
    } run_args_t;
    
    typedef struct test_cfg_str
    {
      double      srate;
      unsigned    smp_per_cycle;
      char*       score_fname;
      char*       out_dir;
      bool        output_json_fl;

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

        // read the configuration record
        if((rc = run_cfg->readv("beg_meas",kReqFl,ra.beg_meas_num,
                                "end_meas",kReqFl,ra.end_meas_num,
                                "beg_perf_uid",kReqFl,ra.beg_perf_uid,
                                "end_perf_uid",kReqFl,ra.end_perf_uid,
                                "fn",      kOptFl,fname,
                                "gt_fn",   kOptFl,gt_fname,
                                "result_fn",kReqFl,res_fname,
                                "self_test_time_scale",kOptFl,ra.self_test_time_scale)) != kOkRC )
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
                                  "vel",se.velocity,
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
                         "perfL", perf_cfgL,
                         "follower",sf_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse top level test cfg. failed.");
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

      if((rc = file::printf(fH,"perf_uid,pitch,loc_id,meas,confidence,match_status_id\n")) != kOkRC )
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
        
        if((rc = file::printf(fH,"%i,%i,%i,%i,%f,%s\n",res.perf_uid,perf_note->pitch,res.loc_id,meas_num,res.confidence,match_status_label)) != kOkRC )
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

    double _pct( unsigned n, unsigned d )
    { return d==0 ? 0.0 : (100.0 * n) / d; }

    // Compare the follower results to the ground truth and print a summary.
    // Results whose perf. note has no ground-truth record, or whose ground-truth status is 'blank', are not scored.
    rc_t _report_gnd_truth_score( const test_args_t& ta, const run_args_t& ra, unsigned result_cnt )
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

      if( ta.sf_args.report_level >= 2 )
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

        if( ta.sf_args.report_level >= 2 )
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

    rc_t _run_test( test_args_t& test_args, handle_t sfH, const run_args_t& r )
    {
      rc_t   rc   = kOkRC;
      double sec0 = 0.0;
      unsigned note_idx = 0;
      
      if( r.noteN == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"The performance note list is empty.");
        goto errLabel;
      }

      // Reset the active score follower section to beg_loc_id:end_loc_id
      if((rc = reset(sfH,r.beg_loc_id,r.end_loc_id)) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower reset failed on locations: beg=%i end=%i.",r.beg_loc_id,r.end_loc_id);
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
        }
        
        sec0 = note.sec;

        // submit the note to the score follower
        if((rc = on_new_note(sfH,note.uid,note.sec,note.pitch,note.vel,r.resultA[note_idx])) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower failed on note uid:%i.",note.uid);
          goto errLabel;
        }

      }

    errLabel:

      rc_t rc1 = kOkRC;
      if( test_args.output_json_fl )
        rc1 = _write_result_json(test_args,r,note_idx);
      else
        rc1 = _write_result_csv(test_args,r,note_idx);

      // if ground truth was given then score the results
      if( r.gnd_truthA != nullptr )
        rc1 = rcSelect(rc1,_report_gnd_truth_score(test_args,r,note_idx));

      if( rc != kOkRC )
        cwLogError(rc,"Run test failed on '%s'.",cwStringNullGuard(r.perf_fname));
      
      return rcSelect(rc,rc1);
      
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

  if((rc = create( sfH, test_args.sf_args, test_args.score_evtA, test_args.score_evtN )) != kOkRC )
  {
    rc = cwLogError(rc,"Score follower create failed.");
    goto errLabel;
  }

  for(unsigned i=0; i<test_args.run_argsN; ++i)
  {
    if((rc = _run_test(test_args, sfH, test_args.run_argsA[i])) != kOkRC )
    {
      goto errLabel;
    }
  }

errLabel:
  if(rc != kOkRC )
     rc = cwLogError(rc,"Score-follower-3 test failed.");

  
  destroy( sfH );

  
  _test_args_destroy( test_args );
  
  return rc;
}

