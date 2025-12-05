//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwFileSys.h"

#include "cwCsv.h"
#include "cwMidi.h"

#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwPerfMeas.h"

#include "cwPianoScore.h"

#include "cwPianoScore.h"
#include "cwScoreFollow2.h"
#include "cwScoreFollow2Test.h"

namespace cw
{
  namespace score_follow_2
  {
    enum {
      kCsvVersion0Id,
      kCsvVersion1Id,
    };
    
    typedef struct midi_evt_str
    {
      unsigned uid;
      uint8_t  pitch;
      uint8_t  vel;      
      unsigned sample_idx;
      double   sec;
    } midi_evt_t;

    typedef struct midi_file_str
    {
      midi_evt_t* evtA;
      unsigned    evtN;
      unsigned    sampleN;
    } midi_file_t;

    rc_t parse_midi_file( const char* midi_csv_fname,
                          double srate,
                          unsigned beg_row_idx,
                          unsigned end_row_idx,
                          unsigned filter_dev_idx,
                          unsigned version_id,
                          midi_file_t& mf )
    {
      rc_t          rc       = kOkRC;
      csv::handle_t csvH;
      const char*   v0_titleA[] = { "UID","trk","amicro","type","D0","D1","sci_pitch" };
      const char*   v1_titleA[] = { "sec","status","d0","d1" };
      const char**  titleA      = version_id==kCsvVersion0Id ? v0_titleA : v1_titleA;
      unsigned      titleN      = version_id==kCsvVersion0Id ? sizeof(v0_titleA)/sizeof(v0_titleA[0]) : sizeof(v1_titleA)/sizeof(v1_titleA[0]);
      unsigned      line_idx = 0;
      unsigned      lineN    = 0;
      
      if((rc = csv::create(csvH,midi_csv_fname,titleA,titleN)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI CSV file open failed.");
        goto errLabel;
      }

      if( filter_dev_idx != kInvalidIdx )
      {
        if( csv::title_col_index(csvH,"dev") == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The performance CSV file '%s' does not have a 'dev' column.",cwStringNullGuard(midi_csv_fname));
          goto errLabel;
        }
      }

      if((rc = line_count(csvH,lineN)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI CSV line count access failed.");
        goto errLabel;
      }

      mf.evtA = mem::allocZ<midi_evt_t>(lineN);
      mf.evtN = 0;
  
      for(; (rc = next_line(csvH)) == kOkRC; ++line_idx )
      {
        unsigned uid;
        unsigned amicro;
        const char* type = nullptr;
        unsigned status;
        unsigned d0;
        unsigned d1;
        unsigned dev_idx;
        double sec;

        // if a begin/end row range was given and this row is outside that range
        if( beg_row_idx != kInvalidIdx && (beg_row_idx > line_idx || line_idx > end_row_idx) )
          continue;

        if( version_id == 0)
        {
          if((rc = getv(csvH,"UID",uid,"amicro",amicro,"type",type,"D0",d0,"D1",d1)) != kOkRC )
          {
            cwLogError(rc,"Error reading CSV line number %i.",line_idx+1);
            goto errLabel;
          }

          sec = amicro/1000000.0;
        }
        else
        {
          if((rc = getv(csvH,"sec",sec,"status",status,"d0",d0,"d1",d1)) != kOkRC )
          {
            cwLogError(rc,"Error reading CSV line number %i.",line_idx+1);
            goto errLabel;
          }

          uid = line_idx;
          status &= 0xf0; // remove channel
          
        }

        if( filter_dev_idx != kInvalidIdx )
        {
          if((rc = getv(csvH,"dev",dev_idx)) != kOkRC )
          {
            cwLogError(rc,"Error reading CSV 'dev' column on line number:%i.",line_idx+1);
            goto errLabel;
          }

          // if a dev_idx filter was given and the device does not match 
          if( dev_idx != filter_dev_idx )
            continue;
        }

        
        bool is_note_on_fl = version_id==0 ? textIsEqual(type,"non") : status==144;
        
        if(is_note_on_fl)
        {
          midi_evt_t& m = mf.evtA[ mf.evtN++ ];

          assert( d0 <= 127 && d1 <= 127 );
          
          m.pitch      = (uint8_t)d0;
          m.vel        = (uint8_t)d1;
          m.sec        = sec;
          m.sample_idx = (unsigned)(m.sec * srate);
          m.uid        = uid;
          
          mf.sampleN = m.sample_idx;
        }
      }
      
    errLabel:
      destroy(csvH);
      
      if( rc == kEofRC )
        rc = kOkRC;

      if( rc != kOkRC )
        rc = cwLogError(rc,"MIDI csv file parse failed on '%s'.",cwStringNullGuard(midi_csv_fname));
      return rc;      
    }
    
    
    rc_t parse_midi_file1( const char* midi_csv_fname, double srate, unsigned beg_row_idx, unsigned end_row_idx, unsigned filter_dev_idx, midi_file_t& mf )
    {
      rc_t          rc       = kOkRC;
      csv::handle_t csvH;      
      const char*   titleA[] = { "UID","trk","amicro","type","ch","D0","D1","sci_pitch" };
      unsigned      titleN   = sizeof(titleA)/sizeof(titleA[0]);
      unsigned      line_idx = 0;
      unsigned      lineN    = 0;
      
      if((rc = csv::create(csvH,midi_csv_fname,titleA,titleN)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI CSV file open failed.");
        goto errLabel;
      }

      if( filter_dev_idx != kInvalidIdx )
      {
        if( csv::title_col_index(csvH,"dev") == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The performance CSV file '%s' does not have a 'dev' column.",cwStringNullGuard(midi_csv_fname));
          goto errLabel;
        }
      }

      if((rc = line_count(csvH,lineN)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI CSV line count access failed.");
        goto errLabel;
      }

      mf.evtA = mem::allocZ<midi_evt_t>(lineN);
      mf.evtN = 0;

      // if default row filter values were given then set them the first/last line of the file
      if( beg_row_idx == kInvalidIdx )
        beg_row_idx = 0;
      
      if( end_row_idx == kInvalidIdx )
        end_row_idx = lineN;
  
      for(; (rc = next_line(csvH)) == kOkRC; ++line_idx )
      {
        unsigned uid;
        unsigned amicro;
        const char* type = nullptr;
        unsigned ch;
        unsigned d0;
        unsigned d1;
        unsigned dev_idx;

        // if a begin/end row range was given and this row is outside that range
        if( beg_row_idx > line_idx || line_idx > end_row_idx) 
          continue;
        
        if((rc = getv(csvH,"UID",uid,"amicro",amicro,"type",type,"ch",ch,"D0",d0,"D1",d1)) != kOkRC )
        {
          cwLogError(rc,"Error reading CSV line number %i.",line_idx+1);
          goto errLabel;
        }

        if( filter_dev_idx != kInvalidIdx )
        {
          if((rc = getv(csvH,"dev",dev_idx)) != kOkRC )
          {
            cwLogError(rc,"Error reading CSV 'dev' column on line number:%i.",line_idx+1);
            goto errLabel;
          }

          // if a dev_idx filter was given and the device does not match 
          if( dev_idx != filter_dev_idx )
            continue;
        }

        if(textIsEqual(type,"non"))
        {
          midi_evt_t& m = mf.evtA[ mf.evtN++ ];

          assert( d0 <= 127 && d1 <= 127 );
          
          m.pitch      = (uint8_t)d0;
          m.vel        = (uint8_t)d1;
          m.sec        = amicro/1000000.0;
          m.sample_idx = (unsigned)(m.sec * srate);
          m.uid        = uid;
          
          mf.sampleN = m.sample_idx;
        }
      }
      
    errLabel:
      destroy(csvH);
      
      if( rc == kEofRC )
        rc = kOkRC;

      if( rc != kOkRC )
        rc = cwLogError(rc,"MIDI csv file parse failed on '%s'.",cwStringNullGuard(midi_csv_fname));
      return rc;      
    }
    
    rc_t run( perf_score::handle_t& scoreH,
              double          srate,
              unsigned        smp_per_cycle,
              const object_t* sf_cfg,
              unsigned        beg_loc_id,
              unsigned        end_loc_id,
              bool            rpt_per_note_fl,
              const midi_file_t& mf,
              score_follow_2::rpt_t& rpt_ref )
    {

      rc_t rc = kOkRC;
      score_follow_2::args_t   sf_args{};
      score_follow_2::handle_t sfH;
      unsigned                 midi_evt_idx = 0;
      unsigned                 resultN = 0;
      const score_follow_2::result_t* resultA = nullptr;
      // parse args
      if((rc = score_follow_2::parse_args(sf_cfg,sf_args)) != kOkRC )
      {
        rc = cwLogError(rc,"SF2 parse arg. failed.");
        goto errLabel;
      }

      
      //cwLogInfo("Following: (%i notes found) sample count:%i %s.",mf.evtN,mf.sampleN,midi_csv_fname);
      
      // create the score-follower
      if((rc = score_follow_2::create(sfH,sf_args,scoreH)) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower create failed.");
        goto errLabel;
      }

      if((rc = score_follow_2::reset(sfH,beg_loc_id,end_loc_id)) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower reset failed.");
        goto errLabel;
      }

      // for each cycle
      for(unsigned smp_idx=0; smp_idx < mf.sampleN; smp_idx += smp_per_cycle)        
      {
        while( smp_idx <= mf.evtA[midi_evt_idx].sample_idx && mf.evtA[midi_evt_idx].sample_idx < smp_idx + smp_per_cycle )
        {
          const midi_evt_t* e         = mf.evtA + midi_evt_idx++;
          unsigned          loc_id    = kInvalidId;
          unsigned          score_vel = -1;
          unsigned          meas_numb = -1;
          
          //printf("%f pitch:%i vel:%i\n",e->sec,e->pitch,e->vel);
          
          if((rc = on_new_note( sfH, e->uid, e->sec, e->pitch, e->vel, loc_id, meas_numb, score_vel )) != kOkRC )
          {
            rc = cwLogError(rc,"SF2 note processing failed on note UID:%i.",e->uid);
            goto errLabel;
          }
        }
        
        if((rc = do_exec(sfH)) != kOkRC )
        {
          rc = cwLogError(rc,"SF2 exec processing failed.");
          goto errLabel;
        }
        
      }


      if( rpt_per_note_fl)
      {
        if((resultA = result_array(sfH,resultN)) != nullptr )
        {
          cwLogInfo("uid,loc,pitch,vel");
          for(unsigned i=0; i<resultN; ++i)
          {
            const auto* r = resultA + i;        
            cwLogInfo("%i,%i,%i,%i",r->perf_uid,r->match_loc_id,r->perf_pitch,r->perf_vel);
          }
        }
      }
      score_follow_2::report_summary(sfH,rpt_ref);
      
    errLabel:
      destroy(sfH);
      
      return rc;
    }

    rc_t _run_from_take_list(const object_t*        fileEle,
                             perf_score::handle_t&  scoreH,
                             double                 srate,
                             unsigned               smp_per_cycle,
                             const object_t*        sf_cfg,
                             unsigned               beg_loc_id,
                             unsigned               end_loc_id,
                             unsigned               min_perf_noteN,
                             unsigned               max_spuriousN,
                             bool                   rpt_per_note_fl,
                             score_follow_2::rpt_t& sf_rpt)
    {
      rc_t            rc       = kOkRC;
      const object_t* takeL    = nullptr;
      const char*     c_folder = nullptr;
      char*           folder   = nullptr;
      
      if((rc = fileEle->getv("folder",c_folder,
                             "takeL",takeL)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse of 'folder' and 'takeL' of SF2 test case failed.");
        goto errLabel;
      }
      
      folder = filesys::expandPath( c_folder );

      // for each take number
      for(unsigned j=0; rc==kOkRC && j<takeL->child_count(); ++j)
      {
        unsigned take_num;

        // read the take number
        if((rc = takeL->child_ele(j)->value(take_num)) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing take number on test '%s' index '%i'.",cwStringNullGuard(c_folder),j);
        }
        else
        {
          midi_file_t mf{ .evtA = nullptr, .evtN=0, .sampleN=0 };
          unsigned beg_perf_idx = kInvalidIdx;
          unsigned end_perf_idx = kInvalidIdx;
          unsigned dev_idx      = kInvalidIdx;
          
          // form the record_### folder name
          char* record_folder = mem::printf<char>(nullptr,"record_%i",take_num);      
          
          // form MIDI csv file name
          char* midi_csv_fname = filesys::makeFn(folder,"fix_midi","csv", record_folder, nullptr );

          // parse MIDI file into an array
          if((rc = parse_midi_file( midi_csv_fname, srate, kInvalidIdx, kInvalidIdx, kInvalidIdx, kCsvVersion0Id, mf )) == kOkRC )
          {
            // Run the SF against the MIDI file
            if((rc = run(scoreH, srate, smp_per_cycle, sf_cfg, beg_loc_id, end_loc_id, rpt_per_note_fl, mf, sf_rpt )) != kOkRC )
              rc = cwLogError(rc,"SF2 test run failed on '%s'.",cwStringNullGuard(midi_csv_fname));
            else
              cwLogInfo("Notes:%i Match:%i Miss:%i Spurious:%i : %s",sf_rpt.perfNoteN,sf_rpt.matchN,sf_rpt.missN,sf_rpt.spuriousN,cwStringNullGuard(midi_csv_fname));

          }
                    
          mem::release(record_folder);
          mem::release(midi_csv_fname);
          mem::release(mf.evtA);
          
        }
      }

    errLabel:
      mem::release(folder);
      return rc;
    }

      rc_t _run_with_one_file(const object_t*        fileEle,
                              perf_score::handle_t&  scoreH,
                              double                 srate,
                              unsigned               smp_per_cycle,
                              const object_t*        sf_cfg,
                              unsigned               beg_loc_id,
                              unsigned               end_loc_id,
                              unsigned               min_perf_noteN,
                              unsigned               max_spuriousN,
                              bool                   rpt_per_note_fl,
                              score_follow_2::rpt_t& sf_rpt)
      {
        rc_t        rc;
        unsigned    beg_perf_idx  = kInvalidIdx;
        unsigned    end_perf_idx   = kInvalidIdx;
        const char* perf_csv_fname = nullptr;
        unsigned    dev_idx       = kInvalidIdx;
        midi_file_t mf{ .evtA = nullptr, .evtN=0, .sampleN=0 };
        
        if((rc = fileEle->getv("beg_perf_idx",beg_perf_idx,
                               "end_perf_idx",end_perf_idx,
                               "file",perf_csv_fname,
                               "dev_idx",dev_idx)) != kOkRC )
        {
          rc = cwLogError(rc,"Parse of 'file' in SF2 test case failed.");
          goto errLabel;
        }

        // parse MIDI file into an array
        if((rc = parse_midi_file( perf_csv_fname, srate, beg_perf_idx, end_perf_idx, dev_idx, kCsvVersion1Id, mf )) == kOkRC )
        {
          // Run the SF against the MIDI file
          if((rc = run(scoreH, srate, smp_per_cycle, sf_cfg, beg_loc_id, end_loc_id, rpt_per_note_fl, mf, sf_rpt )) != kOkRC )
            rc = cwLogError(rc,"SF2 test run failed on '%s'.",cwStringNullGuard(perf_csv_fname));
          else
          {
            cwLogInfo("Notes:%i Match:%i Miss:%i Spurious:%i : %s",sf_rpt.perfNoteN,sf_rpt.matchN,sf_rpt.missN,sf_rpt.spuriousN,cwStringNullGuard(perf_csv_fname));
          }

        }

      errLabel:

        mem::release(mf.evtA);
 
        return rc;
        
      }
    
  }
}


cw::rc_t cw::score_follow_2::test( const object_t* cfg )
{
  rc_t                 rc            = kOkRC;
  double               srate         = 48000.0;
  unsigned             smp_per_cycle = 64;
  unsigned             limit_perf_N  = -1;
  const char*          c_score_fname = nullptr;
  char*                score_fname   = nullptr;
  const object_t*      fileL         = nullptr;
  const object_t*      sf_cfg        = nullptr;
  bool                 rpt_per_note_fl = false;
  
  unsigned             tot_missN     = 0;
  unsigned             tot_spuriousN = 0;
  unsigned             tot_passN     = 0;
  unsigned             tot_failN     = 0;
  
  perf_score::handle_t scoreH;

  if((rc = cfg->getv("srate",srate,
                     "smp_per_cycle",smp_per_cycle,
                     "limit_perf_N",limit_perf_N,
                     "score_fname",c_score_fname,
                     "rpt_per_note_fl",rpt_per_note_fl,
                     "fileL",fileL,
                     "follower",sf_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"SF2 test argument parsing failed.");
    goto errLabel;
  }

  score_fname = filesys::expandPath( c_score_fname );

  // create the score
  if((rc = perf_score::create(scoreH,score_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Score create failed on '%s'.",cwStringNullGuard(score_fname));
    goto errLabel;
  }

  // for each file
  for(unsigned i=0; rc==kOkRC && i<fileL->child_count(); ++i)
  {
    unsigned        beg_loc_id     = kInvalidId;
    unsigned        end_loc_id     = kInvalidId;
    unsigned        min_perf_noteN = 0;
    unsigned        max_spuriousN  = 0;
    
    const object_t* takeL          = nullptr;
    
    score_follow_2::rpt_t sf_rpt;

    // read the file record
    if((rc = fileL->child_ele(i)->getv("beg_loc",beg_loc_id,
                                       "end_loc",end_loc_id,
                                       "min_perf_noteN",min_perf_noteN,
                                       "max_spuriousN", max_spuriousN)) != kOkRC )
    {
      rc = cwLogError(rc,"Parse of SF2 test case at index %i failed.",i);
      goto errLabel;
    }

    if( fileL->child_ele(i)->find_child("folder") != nullptr )
    {
      rc = _run_from_take_list(fileL->child_ele(i),scoreH,srate,smp_per_cycle,sf_cfg,beg_loc_id,end_loc_id,min_perf_noteN,max_spuriousN,rpt_per_note_fl,sf_rpt);
    }
    else
    {
      rc = _run_with_one_file(fileL->child_ele(i),scoreH,srate,smp_per_cycle,sf_cfg,beg_loc_id,end_loc_id,min_perf_noteN,max_spuriousN,rpt_per_note_fl,sf_rpt);
    }

    if( rc != kOkRC )
    {
      rc = cwLogError(rc,"SF2 test case index %i failed.",i);
      goto errLabel;
    }
    
    
    if( sf_rpt.perfNoteN > min_perf_noteN && sf_rpt.spuriousN < max_spuriousN )
    {
      tot_passN += 1;
      tot_missN += sf_rpt.missN;
      tot_spuriousN += sf_rpt.spuriousN;        
    }
    else
    {
      tot_failN += 1;
    }

      
    
  }

  cwLogInfo("Total M+S:%i missed:%i spurious:%i pass:%i fail:%i",tot_missN+tot_spuriousN,tot_missN,tot_spuriousN,tot_passN,tot_failN);
  
errLabel:
  destroy(scoreH);
  mem::release(score_fname);
  return rc;
}
