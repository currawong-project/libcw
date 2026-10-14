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
    
    typedef struct run_args_str
    {
      unsigned     beg_meas_num;
      unsigned     end_meas_num;
      
      unsigned     beg_loc_id;
      unsigned     end_loc_id;
      
      char*        fname;
      
      perf_note_t* noteA;
      unsigned     noteN;
    } run_args_t;
    
    typedef struct test_cfg_str
    {
      double      srate;
      unsigned    smp_per_cycle;
      char*       score_fname;

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
        mem::release(test_args.run_argsA[i].fname);        
      }
      
      test_args.run_argsN = 0;
      mem::release(test_args.score_evtA);
      mem::release(test_args.run_argsA);
      mem::release(test_args.score_fname);
      
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

      if((rc = file::open(fH,run_args.fname,file::kReadFl)) != kOkRC )
      {
        rc = cwLogError(rc,"Performance file open failed on '%s'.",cwStringNullGuard(run_args.fname));
        goto errLabel;
                        
      }

      if((rc = lineCount(fH,line_cnt)) != kOkRC )
      {
        rc = cwLogError(rc,"The line count of the performance file '%s' could not be determined.",cwStringNullGuard(run_args.fname));
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

          note.uid   = run_args.noteN;
          note.sec   = (double)amicro / 1000000.0;
          note.pitch = d0;
          note.vel   = d1;
          
          run_args.noteN += 1;
        }        
      }

      if( rc == kEofRC )
        rc = kOkRC;
      
      
    errLabel:
      file::close(fH);

      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Parsing failed on the performance file '%s'.",cwStringNullGuard(run_args.fname));
      }
      return rc;
    }

    rc_t _meas_to_loc_id(const test_args_t& test_args, unsigned meas_num, unsigned& loc_id_ref)      
    {
      rc_t rc = kOkRC;
      loc_id_ref = kInvalidId;
      
      for(unsigned i=0; i<test_args.score_evtN; ++i)
        if( test_args.score_evtA[i].meas_num == meas_num )
        {
          loc_id_ref = test_args.score_evtA[i].loc_id;
          break;
        }

      if( loc_id_ref == kInvalidId )
        rc = cwLogError(kInvalidArgRC,"The score measure %i is not valid in the score '%s'.",meas_num,cwStringNullGuard(test_args.score_fname));
      
    errLabel:
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
        const object_t* run_cfg = perf_cfgL->child_ele(i);
        const char*     fname   = nullptr;
        run_args_t&     ra      = test_args.run_argsA[i];

        // read the configuration record
        if((rc = run_cfg->getv("beg_meas",ra.beg_meas_num,
                               "end_meas",ra.end_meas_num,
                               "fn",      fname)) != kOkRC )
        {
          rc = cwLogError(rc,"Parse error on perfL element at index %i.",i);
          goto errLabel;
        }

        // expand the filename
        if((ra.fname = filesys::expandPath(fname)) == nullptr )
        {
          rc = cwLogError(rc,"Performance file path expansion failed on '%s'.",cwStringNullGuard(fname));
          goto errLabel;
        }

        // parse the file in to run_cfg_t.noteA[]
        if((rc = _parse_perf_file( ra )) != kOkRC )
        {
          goto errLabel;
        }

        // convert the start measure to a score location
        if((rc = _meas_to_loc_id(test_args,ra.beg_meas_num,ra.beg_loc_id)) != kOkRC )
        {
          goto errLabel;
        }
        
        // convert the end measure to a score location
        if((rc = _meas_to_loc_id(test_args,ra.end_meas_num,ra.end_loc_id)) != kOkRC )
        {
          goto errLabel;
        }

        cwLogInfo("Perf notes:%i meas:%i-%i loc:%i-%i %s.",ra.noteN,ra.beg_meas_num,ra.end_meas_num,ra.beg_loc_id,ra.end_loc_id,cwStringNullGuard(ra.fname));
        
      }
    errLabel:
      return rc;
    }
    
    rc_t _parse_follower_cfg( const object_t* sf_cfg, args_t& sf_args, double srate, unsigned smp_per_cycle )
    {
      rc_t rc = kOkRC;

      if((rc = sf_cfg->getv("pitch_min",sf_args.pitch_min,
                            "pitch_max",sf_args.pitch_max,
                            "wnd_pre_sec",sf_args.wnd_pre_sec,
                            "wnd_post_sec",sf_args.wnd_post_sec,
                            "wnd_pre_loc_cnt",sf_args.wnd_pre_loc_cnt,
                            "wnd_post_loc_cnt",sf_args.wnd_post_loc_cnt,
                            "report_level",sf_args.report_level)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed on follower args.");
        goto errLabel;
      }

      sf_args.exec_period_sec = srate/smp_per_cycle;
      
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
      const object_t* perf_cfgL   = nullptr;
      const object_t* sf_cfg      = nullptr;

      // Parse the top level of the test cfg.
      if((rc = cfg->getv("srate",test_args.srate,
                         "smp_per_cycle",test_args.smp_per_cycle,
                         "score_fname",score_fname,
                         "perfL", perf_cfgL,
                         "follower",sf_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse top level test cfg. failed.");
        goto errLabel;
      }

      // expand the score filename
      if(( test_args.score_fname = filesys::expandPath(score_fname)) == nullptr )
      {
        rc = cwLogError(rc,"Score file name expansion failed.");
        goto errLabel;
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

    rc_t _run_test( test_args_t& test_args, handle_t sfH, const run_args_t& r )
    {
      rc_t              rc = kOkRC;

      // Reset the active score follower section to beg_loc_id:end_loc_id
      if((rc = reset(sfH,r.beg_loc_id,r.end_loc_id)) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower reset failed on locations: beg=%i end=%i.",r.beg_loc_id,r.end_loc_id);
        goto errLabel;
      }

      // for each performed note ...
      for(unsigned i=0; i<r.noteN; ++i)
      {
        result_t          result;
        const perf_note_t note = r.noteA[i];

        // submit the note to the score follower
        if((rc = on_new_note(sfH,note.uid,note.sec,note.pitch,note.vel,result)) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower failed on note uid:%i.",note.uid);
          goto errLabel;
        }

      }

    errLabel:
      if( rc != kOkRC )
        cwLogError(rc,"Run test failed on '%s'.",cwStringNullGuard(r.fname));
      return rc;
      
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

