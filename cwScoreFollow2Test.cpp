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

    rc_t parse_midi_file( const char* midi_csv_fname, double srate, midi_file_t& mf )
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
        unsigned ch;
        unsigned d0;
        unsigned d1;
        
        if((rc = getv(csvH,"UID",uid,"amicro",amicro,"type",type,"ch",ch,"D0",d0,"D1",d1)) != kOkRC )
        {
          cwLogError(rc,"Error reading CSV line %i.",line_idx+1);
          goto errLabel;
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
              const char*     midi_csv_fname,
              const object_t* sf_cfg,
              unsigned        beg_loc_id,
              unsigned        end_loc_id,
              score_follow_2::rpt_t& rpt_ref )
    {

      rc_t rc = kOkRC;
      midi_file_t              mf{ .evtA = nullptr, .evtN=0, .sampleN=0 };
      score_follow_2::args_t   sf_args{};
      score_follow_2::handle_t sfH;
      unsigned                 midi_evt_idx = 0;

      // parse args
      if((rc = score_follow_2::parse_args(sf_cfg,sf_args)) != kOkRC )
      {
        rc = cwLogError(rc,"SF2 parse arg. failed.");
        goto errLabel;
      }

      // parse MIDI file into an array
      if((rc = parse_midi_file( midi_csv_fname, srate, mf )) != kOkRC )
        goto errLabel;
      
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
          const midi_evt_t* e =  mf.evtA + midi_evt_idx++;
          unsigned loc_id = kInvalidId;
          unsigned score_vel = -1;
          
          //printf("%f pitch:%i vel:%i\n",e->sec,e->pitch,e->vel);
          
          if((rc = on_new_note( sfH, e->uid, e->sec, e->pitch, e->vel, loc_id, score_vel )) != kOkRC )
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

      score_follow_2::report_summary(sfH,rpt_ref);
      
    errLabel:
      mem::release(mf.evtA);
      destroy(sfH);
      
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
  
  unsigned             tot_missN     = 0;
  unsigned             tot_spuriousN = 0;
  unsigned             tot_passN     = 0;
  unsigned             tot_failN     = 0;
  
  perf_score::handle_t scoreH;

  if((rc = cfg->getv("srate",srate,
                     "smp_per_cycle",smp_per_cycle,
                     "limit_perf_N",limit_perf_N,
                     "score_fname",c_score_fname,
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
    const char*     c_folder       = nullptr;
    char*           folder         = nullptr;
    const object_t* takeL          = nullptr;
    
    score_follow_2::rpt_t sf_rpt;

    // read the file record
    if((rc = fileL->child_ele(i)->getv("beg_loc",beg_loc_id,
                                      "end_loc",end_loc_id,
                                       "min_perf_noteN",min_perf_noteN,
                                       "max_spuriousN", max_spuriousN,
                                      "folder",c_folder,
                                      "takeL",takeL)) != kOkRC )
    {
      rc = cwLogError(rc,"Parse of SF2 test case at index %i.",i);
      goto errLabel;
    }

    folder = filesys::expandPath( c_folder );

    // for each take number
    for(unsigned j=0; j<takeL->child_count(); ++j)
    {
      char* record_folder = nullptr;
      unsigned take_num;

      // read the take number
      if((rc = takeL->child_ele(j)->value(take_num)) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing take number on test '%s' index '%i'.",cwStringNullGuard(c_folder),j);
        break;
      }

      // form the record_### folder name
      record_folder = mem::printf<char>(nullptr,"record_%i",take_num);      

      // form MIDI csv file name
      char* midi_csv_fname = filesys::makeFn(folder,"fix_midi","csv", record_folder, nullptr );

      if((rc = run(scoreH, srate, smp_per_cycle, midi_csv_fname, sf_cfg, beg_loc_id, end_loc_id, sf_rpt )) != kOkRC )
      {
        rc = cwLogError(rc,"SF2 test run failed on '%s'.",cwStringNullGuard(midi_csv_fname));
        break;
      }

      cwLogInfo("Notes:%i Match:%i Miss:%i Spurious:%i : %s",sf_rpt.perfNoteN,sf_rpt.matchN,sf_rpt.missN,sf_rpt.spuriousN,cwStringNullGuard(midi_csv_fname));

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
      
      //printf("%s\n",fname);

      mem::release(record_folder);
      mem::release(midi_csv_fname);
      
    }

    mem::release(folder);

    
  }

  cwLogInfo("Total M+S:%i missed:%i spurious:%i pass:%i fail:%i",tot_missN+tot_spuriousN,tot_missN,tot_spuriousN,tot_passN,tot_failN);
  
errLabel:
  destroy(scoreH);
  mem::release(score_fname);
  return rc;
}
