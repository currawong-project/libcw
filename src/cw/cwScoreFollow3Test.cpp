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
      
      char*        perf_fname;
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
      ra.gnd_truthN = line_cnt - 1;
      ra.gnd_truthA = mem::allocZ<gnd_truth_note_t>(ra.gnd_truthN);

      // for each line in the gnd-truth CSV
      for(line_idx=0; (rc = next_line(csvH)) == kOkRC && line_idx<ra.gnd_truthN; ++line_idx )
      {
        gnd_truth_note_t& gt = ra.gnd_truthA[line_idx];
        const char* status_label = nullptr;

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

        gt.loc_id = loc_id<0 ? kInvalidId : (unsigned)loc_id;

        // verify that the perf_uid is not duplicated 
        if( line_idx>0 && _uid_to_gnd_truth_note(ra, gt.perf_uid, line_idx-1 ) != nullptr )
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

        if( line_idx != ra.gnd_truthN )
        {
          rc = cwLogError(kInvalidStateRC,"The count of lines parsed (%i) does not match the count of lines in the ground truth file (%i).",line_idx,ra.gnd_truthN);
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

    rc_t _form_result_file_name( const char* out_dir, const char* in_fname, const char* fn_ext, char*& out_fname_ref )
    {
      rc_t                 rc = kOkRC;
      filesys::pathPart_t* pp = nullptr;
      char*                fn = nullptr;

      
      out_fname_ref = nullptr;

      if((pp = filesys::pathParts(in_fname)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The performance input file name '%s' could not be parsed.",cwStringNullGuard(in_fname));
        goto errLabel;
      }

      if((out_fname_ref = filesys::makeFn(out_dir, pp->fnStr, fn_ext, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"File parts join operation failed.");
        goto errLabel;
      }

    errLabel:
      mem::release(pp);

      if( rc != kOkRC )
        rc = cwLogError(rc,"The output filename could not be formed.");

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
        const object_t* run_cfg  = perf_cfgL->child_ele(i);
        const char*     fname    = nullptr;
        const char*     gt_fname = nullptr;
        run_args_t&     ra       = test_args.run_argsA[i];
        const char*     fn_ext   = test_args.output_json_fl ? ".json" : ".csv";

        // read the configuration record
        if((rc = run_cfg->readv("beg_meas",kReqFl,ra.beg_meas_num,
                                "end_meas",kReqFl,ra.end_meas_num,
                                "fn",      kReqFl,fname,
                                "gt_fn",   kOptFl,gt_fname)) != kOkRC )
        {
          rc = cwLogError(rc,"Parse error on perfL element at index %i.",i);
          goto errLabel;
        }

        // expand the filename
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

          // parse the gnd-truth file
          if((rc = _parse_gnd_truth_file(test_args,ra)) != kOkRC )
          {
            goto errLabel;
          }
        }
        
        if((rc = _form_result_file_name( test_args.out_dir, ra.perf_fname, fn_ext, ra.result_fname )) != kOkRC )
        {
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
                            "time_fact_min",kReqFl,sf_args.time_fact_min,
                            "time_fact_max",kReqFl,sf_args.time_fact_max,                                                        
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

