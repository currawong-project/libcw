//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwFile.h"
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
#include "cwMidiState.h"

#include "cwSvgScoreFollow.h"


namespace cw
{
  namespace score_follower
  {
    typedef struct meas_set_str
    {
      unsigned vsi;
      unsigned nsi;
    } meas_set_t;
    
    typedef struct score_follower_str
    {
      bool         enableFl;
      double       srate;
      unsigned     search_area_locN;
      unsigned     key_wnd_locN;
      char*        score_csv_fname;

      bool                deleteScoreFl;
      sfscore::handle_t   scoreH;
      sftrack::handle_t   trackH;
      //perf_meas::handle_t measH;
      
      unsigned*    result_idxA;        //
      unsigned     result_idx_allocN;  //
      unsigned     result_idx_curN;    //

      ssf_note_on_t* perfA;            // perfA[ perfN ] stored performance
      unsigned       perfN;            //
      unsigned       perf_idx;         

      dyn_ref_tbl::handle_t dynRefH;

      unsigned track_flags;
  
    } score_follower_t;

    score_follower_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,score_follower_t>(h); }


    // parse the score follower parameter record
    rc_t _parse_cfg( score_follower_t* p, const object_t* cfg )
    {
      rc_t            rc                         = kOkRC;
      const char*     score_csv_fname            = nullptr;
      const object_t* dyn_ref_dict               = nullptr;
      bool            track_print_fl             = false;
      bool            track_results_backtrack_fl = false;
      
      if((rc = cfg->getv("enable_flag",      p->enableFl,
                         "score_csv_fname",  score_csv_fname,
                         "search_area_locN", p->search_area_locN,
                         "key_wnd_locN",     p->key_wnd_locN,
                         "dyn_ref",          dyn_ref_dict,
                         "track_print_fl",   track_print_fl,
                         "track_results_backtrack_fl", track_results_backtrack_fl)) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC, "Score follower argument parsing failed.");
        goto errLabel;
      }

      if((p->score_csv_fname = filesys::expandPath( score_csv_fname )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Score follower score file expansion failed.");
        goto errLabel;
      }

      if((rc = dyn_ref_tbl::create( p->dynRefH, dyn_ref_dict )) != kOkRC )
      {
        rc = cwLogError(rc,"Dynamics reference array parse failed.");
        goto errLabel;
      }

      p->track_flags += track_print_fl ? sftrack::kPrintFl : 0;
      p->track_flags += track_results_backtrack_fl ? sftrack::kBacktrackResultsFl : 0;
      
    errLabel:
      return rc;
    }
    
    rc_t _update_midi_state( score_follower_t* p, midi_state::handle_t msH )
    {
      rc_t rc;
      for(unsigned i=0; i<p->perf_idx; ++i)
      {
        if((rc = setMidiMsg( msH, p->perfA[i].sec, i, 0, midi::kNoteOnMdId, p->perfA[i].pitch, p->perfA[i].vel, 0 )) != kOkRC )
        {
          rc = cwLogError(rc,"midi_state update failed.");
          goto errLabel;
        }
      }
      
    errLabel:
      return rc;
    }

    bool _processPedal(const char* pedalLabel, unsigned barNumb, bool curPedalDownStateFl, uint8_t d1)
    {
      bool newStateFl = midi::isPedalDown(d1);
      /*
      if( newStateFl == curPedalDownStateFl )
      {
        const char* upDownLabel = newStateFl ? "down" : "up";
        cwLogWarning("Double %s pedal %s in bar number:%i.",pedalLabel,upDownLabel,barNumb);
      }
      */
      
      return newStateFl;
    }
    

    rc_t _destroy( score_follower_t* p)
    {
      destroy(p->dynRefH);
      mem::release(p->score_csv_fname);
      mem::release(p->perfA);
      mem::release(p->result_idxA);
      destroy(p->trackH);
      if( p->deleteScoreFl)
        destroy(p->scoreH);
      //destroy(p->measH);
      mem::release(p);
      return kOkRC;
    }

    void _score_follower_cb( void* arg, sftrack::result_t* r )
    {
      score_follower_t* p = (score_follower_t*)arg;

      //printf("%4i %4i %4i %3i %3i %4s : ", r->oLocId, r->mni, r->muid, r->flags, r->pitch, midi::midiToSciPitch( r->pitch, nullptr, 0 ));

      if( r->scEvtIdx == kInvalidIdx )
      {
        //cwLogInfo("Score Follower: MISS");
      }
      else
      {
        const sfscore::event_t* score_event;
        
        // get a pointer to the matched score event
        if((score_event = event( p->scoreH, r->scEvtIdx )) == nullptr )
        {
          cwLogError(kInvalidStateRC,"cm Score event index (%i) reported by the score follower is invalid.",r->scEvtIdx );
        }
        else
        {
          // verify that the matched event buffer has available space
          if( p->result_idx_curN >= p->result_idx_allocN )
          {
            cwLogError(kInvalidStateRC,"The score follower match id array is full.");
          }
          else
          {
            assert( score_event->index == r->scEvtIdx );

            /*

            // store the performance data in the score
            set_perf( p->scoreH, score_event->index, r->sec, r->pitch, r->vel, r->cost );
            
            perf_meas::result_t pmr = {0};

            // Call performance measurement unit
            if( perf_meas::exec( p->measH, score_event, pmr ) == kOkRC && pmr.loc != kInvalidIdx && pmr.valueA != nullptr )
            {
              double v[ perf_meas::kValCnt ];
              for(unsigned i=0; i<perf_meas::kValCnt; ++i)
                v[i] = pmr.valueA[i] == std::numeric_limits<double>::max() ? -1 : pmr.valueA[i];
              
              cwLogInfo("Section '%s' triggered loc:%i : dyn:%f even:%f tempo:%f cost:%f",
                        cwStringNullGuard(pmr.sectionLabel),
                        pmr.sectionLoc,
                        v[ perf_meas::kDynValIdx ],
                        v[ perf_meas::kEvenValIdx ],
                        v[ perf_meas::kTempoValIdx ],
                        v[ perf_meas::kMatchCostValIdx ] );
            }
            
            */
            
            
            // store a pointer to the m matched sfscore event
            p->result_idxA[ p->result_idx_curN++ ] = r->index; //score_event->index;
          }
        }
      }
    }
  }
}

cw::rc_t cw::score_follower::create( handle_t& hRef, const args_t& args )
{
  rc_t        rc        = kOkRC;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  score_follower_t* p = mem::allocZ<score_follower_t>();

  unsigned track_flags = 0;

  track_flags += args.trackPrintFl            ? sftrack::kPrintFl            : 0;
  track_flags += args.trackResultsBacktrackFl ? sftrack::kBacktrackResultsFl : 0;

  p->scoreH = args.scoreH;
  
  // create the score tracker
  if((rc = sftrack::create( p->trackH, args.scoreH, args.scoreWndLocN, args.midiWndLocN, track_flags, _score_follower_cb, p )) != kOkRC )
  {
    cwLogError(kOpFailRC,"The score follower create failed.");
    goto errLabel;
  }

  p->srate           = sample_rate(p->scoreH);
  p->result_idx_allocN = event_count( p->scoreH )*2;  // give plenty of extra space for the result_idxA[]
  p->result_idxA       = mem::allocZ<unsigned>(p->result_idx_allocN);

  p->perfN = event_count(p->scoreH)*2;
  p->perfA = mem::allocZ<ssf_note_on_t>( p->perfN );
  p->perf_idx = 0;
  p->enableFl = args.enableFl;
  hRef.set(p);

  
 errLabel:
  if(rc != kOkRC )
    _destroy(p);
  
  return rc;
}


cw::rc_t cw::score_follower::create( handle_t& hRef, const object_t* cfg, double srate )
{
  rc_t        rc        = kOkRC;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  score_follower_t* p = mem::allocZ<score_follower_t>();

  if((rc = _parse_cfg(p,cfg)) != kOkRC )
    goto errLabel;

  // create the score
  if((rc = sfscore::create( p->scoreH, p->score_csv_fname, srate, p->dynRefH )) != kOkRC )
  {
    rc = cwLogError(kOpFailRC,"The score could not be initialized from '%s'. cmRC:%i.",cwStringNullGuard(p->score_csv_fname));
    goto errLabel;    
  }

  p->deleteScoreFl = true;
  
  // create the score tracker
  if((rc = sftrack::create( p->trackH, p->scoreH, p->search_area_locN, p->key_wnd_locN, p->track_flags, _score_follower_cb, p )) != kOkRC )
  {
    cwLogError(kOpFailRC,"The score follower create failed.");
    goto errLabel;
  }
  /*
  if((rc = perf_meas::create( p->measH, p->scoreH )) != kOkRC )
  {
    cwLogError(kOpFailRC,"The perf. measure object create failed.");
    goto errLabel;    
  }
  */
  p->srate           = srate;
  p->result_idx_allocN = event_count( p->scoreH )*2;  // give plenty of extra space for the result_idxA[]
  p->result_idxA       = mem::allocZ<unsigned>(p->result_idx_allocN);

  p->perfN = event_count(p->scoreH)*2;
  p->perfA = mem::allocZ<ssf_note_on_t>( p->perfN );
  p->perf_idx = 0;

  hRef.set(p);

 errLabel:
  if( rc != kOkRC )
  {
    _destroy(p);
    cwLogError(rc,"Score follower create failed.");
  }
  
  return rc;
}
    
cw::rc_t cw::score_follower::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  score_follower_t* p = nullptr;
  
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;    
}

bool cw::score_follower::is_enabled( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return p->enableFl;
}

void cw::score_follower::enable( handle_t h, bool enable_fl )
{
  score_follower_t* p = _handleToPtr(h);
  p->enableFl = enable_fl;
}


cw::rc_t cw::score_follower::reset( handle_t h, unsigned locId )
{
  rc_t              rc   = kOkRC;
  score_follower_t* p    = _handleToPtr(h);

  if( locId != kInvalidId )
  {

    cwLogInfo("SF Reset: loc:%i",locId);
    
    if((rc = reset( p->trackH, locId )) != kOkRC )
    {
      rc = cwLogError(rc,"The score follower reset failed.");
      goto errLabel;
    }

    /*
    if((rc = reset( p->measH, locId )) != kOkRC )
    {
      rc = cwLogError(rc,"The measurement unit reset failed.");
      goto errLabel;
    }
    */
    
    p->perf_idx = 0;
    clear_result_index_array(h);    
    clear_all_performance_data(p->scoreH);
    
  }
  
 errLabel:
  return rc;
}
    
cw::rc_t cw::score_follower::exec(  handle_t h,
                                    double   sec,
                                    unsigned smpIdx,
                                    unsigned muid,
                                    unsigned status,
                                    uint8_t  d0,
                                    uint8_t  d1,
                                    bool&    newMatchFlRef )
{
  rc_t              rc                = kOkRC;
  score_follower_t* p                 = _handleToPtr(h);
  unsigned          scLocIdx          = kInvalidIdx;
  unsigned          pre_result_idx_curN = p->result_idx_curN;

  if( !p->enableFl )
    return cwLogError(kInvalidStateRC,"The score follower is not enabled.");
  
  newMatchFlRef = false;

  // This call results in a callback to: _score_follower_cb()
  // Note: pass p->perf_idx as 'muid' to the score follower
  rc = exec(  p->trackH, sec, smpIdx, p->perf_idx, status, d0, d1, &scLocIdx );
  
  switch( rc )
  {
    case kOkRC:
      newMatchFlRef = p->result_idx_curN != pre_result_idx_curN;
      //printf("NM_FL:%i\n",newMatchFlRef);
      break;
        
    case kEofRC:
      rc = cwLogInfo("Score match complete.");
      break;
        
    case kInvalidArgRC:
      rc = cwLogError(kInvalidStateRC,"Score follower state is invalid.");
      break;
        
    case kOpFailRC:
      rc = cwLogError(kOpFailRC,"The score follower failed during a resync attempt.");
      break;
        
    default:
      rc = cwLogError(rc,"The score follower failed with an the error:%i",rc);
  }

  // store note-on messages
  if( p->perf_idx < p->perfN && midi::isNoteOn(status,(unsigned)d1) )
  {
    ssf_note_on_t* pno = p->perfA + p->perf_idx;
    pno->sec     = sec;
    pno->muid    = muid;
    pno->pitch   = d0;
    pno->vel     = d1;    
    p->perf_idx += 1;
    if( p->perf_idx >= p->perfN )
      cwLogWarning("The cw score follower performance cache is full.");
  }
      
  return rc;
}

const unsigned* cw::score_follower::current_result_index_array( handle_t h, unsigned& cur_result_idx_array_cnt_ref )
{
  score_follower_t* p = _handleToPtr(h);
  cur_result_idx_array_cnt_ref = p->result_idx_curN;
  return p->result_idxA;
}

void cw::score_follower::clear_result_index_array( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  p->result_idx_curN = 0;
}
/*
cw::rc_t cw::score_follower::cw_loc_range( handle_t h, unsigned& minLocRef, unsigned& maxLocRef )
{
  rc_t rc = kOkRC;
  score_follower_t* p = _handleToPtr(h);

  minLocRef = 0;
  maxLocRef = loc_count(p->scoreH);

  return rc;
}

bool cw::score_follower::is_loc_in_range( handle_t h, unsigned loc )
{
  rc_t rc;
  unsigned minLoc = 0;
  unsigned maxLoc = 0;
  
  if((rc = cw_loc_range(h,minLoc,maxLoc)) != kOkRC )
    return false;
  
  return minLoc <= loc && loc <= maxLoc;
}
*/   
unsigned cw::score_follower::has_stored_performance( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return p->perf_idx > 0 && result_count(p->trackH) > 0; 
}

cw::rc_t cw::score_follower::sync_perf_to_score( handle_t h )
{
  rc_t                     rc      = kOkRC;
  score_follower_t*        p       = _handleToPtr(h);
  unsigned                 resultN = 0;
  const sftrack::result_t* resultA = nullptr;
    
  if( !has_stored_performance(h) )
  {
    rc = cwLogError(kInvalidStateRC,"No performance to sync.");
    goto errLabel;
  }

  resultN = result_count(p->trackH);
  resultA = result_base(p->trackH);
  
  for(unsigned i=0; i<resultN; ++i)
  {
    unsigned perf_idx = resultA[i].muid;

    // the matcher result 'muid' is the perf. array index of the matching perf. record
    if( perf_idx >= p->perf_idx )
    {
      rc = cwLogError(kInvalidStateRC,"Inconsistent match to perf. map: index mismatch.");
      goto errLabel;
    }

    // verify the pitch of the matching records 
    if( p->perfA[ perf_idx ].pitch != resultA[i].pitch )
    {
      rc = cwLogError(kInvalidStateRC,"Inconsistent match to perf. map: pitch mismatch %i != %i.",p->perfA[ perf_idx ].pitch ,resultA[i].pitch);
      goto errLabel;
    }

    //assert( resultA[i].scEvtIdx == kInvalidIdx || resultA[i].scEvtIdx < p->cmLocToCwLocN );

    // if( resultA[i].scEvtIdx != kInvalidIdx )
    //    p->perfA[ perf_idx ].loc = p->cmLocToCwLocA[ resultA[i].scEvtIdx ];

    if( resultA[i].scEvtIdx != kInvalidIdx )
      p->perfA[ perf_idx ].loc = resultA[i].oLocId;
  }

 errLabel:
  return rc;
}

unsigned cw::score_follower::track_result_count( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return result_count(p->trackH);
}

const cw::sftrack::result_t* cw::score_follower::track_result( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return result_base(p->trackH);
}

unsigned cw::score_follower::perf_count( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return p->perf_idx;
}

const cw::score_follower::ssf_note_on_t* cw::score_follower::perf_base( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return p->perfA;
}

cw::rc_t cw::score_follower::write_svg_file( handle_t h, const char* out_fname, bool show_muid_fl )
{
  score_follower_t* p = _handleToPtr(h);

  return svgScoreFollowWrite( p->scoreH, p->trackH, p->perfA, p->perf_idx, out_fname, show_muid_fl );
}

void cw::score_follower::score_report( handle_t h, const char* out_fname )
{
  score_follower_t* p = _handleToPtr(h);
  report(p->scoreH,out_fname);
}

cw::rc_t cw::score_follower::midi_state_rt_report( handle_t h, const char* out_fname )
{
  score_follower_t*    p     = _handleToPtr(h);
  midi_state::config_t msCfg = midi_state::default_config();
  rc_t                 rc    = kOkRC;
  midi_state::handle_t msH;
  
  if((rc = midi_state::create(msH,nullptr,nullptr,&msCfg)) != kOkRC )
  {
    rc = cwLogError(rc,"midi_state create failed.");
    goto errLabel;
  }

  if((rc = _update_midi_state(p,msH)) != kOkRC )
    goto errLabel;    

  if((rc = report_events(msH,out_fname)) != kOkRC )
    goto errLabel;

 errLabel:
  if( rc != kOkRC )
    cwLogError(rc,"Score follower midi_state_rt_report() failed.");
  
  midi_state::destroy(msH);
  return rc;
}

cw::rc_t cw::score_follower::write_sync_perf_csv( handle_t h, const char* out_fname,  const midi::file::trackMsg_t** msgA, unsigned msgN )
{
  score_follower_t* p               = _handleToPtr(h);  
  rc_t              rc              = kOkRC;
  unsigned          resultN         = result_count(p->trackH);
  auto              resultA         = result_base(p->trackH);
  bool              dampPedalDownFl = false;
  bool              sostPedalDownFl = false;
  bool              softPedalDownFl = false;
  unsigned          curBarNumb          = 1;
  file::handle_t    fH;

  if( msgN == 0 )
  {
    cwLogWarning("Nothing to write.");
    return rc;
  }

  if( resultN == 0 )
  {
    cwLogWarning("The score follower does not have any score sync. info.");
    return rc;
  }

  // open the file
  if((rc = file::open(fH,out_fname,file::kWriteFl)) != kOkRC )
  {
    rc = cwLogError(kOpenFailRC,"Unable to create the file '%s'.",cwStringNullGuard(out_fname));
    goto errLabel;
  }

  // write the header line
  file::printf(fH,"meas,index,voice,loc,tick,sec,dur,rval,dots,sci_pitch,dmark,dlevel,status,d0,d1,bar,section,bpm,grace,damp_down_fl,soft_down_fl,sost_down_fl\n");
      
  for(unsigned i=0; i<msgN; ++i)
  {
    const midi::file::trackMsg_t* m = msgA[i];

    double secs = (msgA[i]->amicro - msgA[0]->amicro)/1000000.0;
        
    // write the event line
    if( midi::isChStatus(m->status) )
    {
      uint8_t d0 = m->u.chMsgPtr->d0;
      uint8_t d1 = m->u.chMsgPtr->d1;

          
      if(  midi::isNoteOn(m->status,d1) )
      {        
        unsigned    bar           = 0;
        const char* sectionLabel  = "";
        unsigned    loc           = score_parse::kInvalidLocId;
        unsigned    dlevel        = -1;
        char sciPitch[ midi::kMidiSciPitchCharCnt + 1 ];
        
        midi::midiToSciPitch( d0, sciPitch, midi::kMidiSciPitchCharCnt );

        // locate score matching record for this performed note
        for(unsigned i=0; i<resultN; ++i)
        {
          const sfscore::event_t* e;
          // FIX THIS:
          // THE perfA[] INDEX IS STORED IN resultA[i].muid
          // this isn't right.
          assert( resultA[i].muid != kInvalidIdx && resultA[i].muid < p->perfN );
          
          if( p->perfA[resultA[i].muid].muid == m->uid && resultA[i].scEvtIdx != kInvalidIdx)
          {
            assert( resultA[i].pitch == d0 );

            if((e = event( p->scoreH, resultA[i].scEvtIdx )) == nullptr )
            {
              cwLogError(kInvalidStateRC,"The performed, and matched, note with muid %i does not have a valid score event index.",m->uid);
              goto errLabel;
            }

            bar          = e->barNumb;
            sectionLabel = e->section != nullptr ? e->section->label : "";
            curBarNumb   = std::max(bar,curBarNumb);
            dlevel       = e->dynLevel;
            loc          = resultA[i].oLocId == kInvalidId ? (unsigned)score_parse::kInvalidLocId : resultA[i].oLocId;
            break;
          }
        }

       
        
        rc = file::printf(fH, "%i,%i,%i,%i,0,%f,0.0,0.0,0,%s,,%i,%i,%i,%i,,%s,,,%i,%i,%i\n",
                          bar,i,1,loc,secs,sciPitch,dlevel,m->status,d0,d1,sectionLabel,dampPedalDownFl,softPedalDownFl,sostPedalDownFl);
      }        
      else
      {

        if( midi::isPedal(m->status,d0) )
        {
          switch( d0 )
          {
            case midi::kSustainCtlMdId:
              dampPedalDownFl = _processPedal("damper",curBarNumb,dampPedalDownFl,d1);
              break;
              
            case midi::kSostenutoCtlMdId:
              sostPedalDownFl = _processPedal("sostenuto",curBarNumb,sostPedalDownFl,d1);
              break;
                
            case midi::kSoftPedalCtlMdId:
              softPedalDownFl = _processPedal("soft",curBarNumb,softPedalDownFl,d1);
              break;
          }  
        }
        
        rc = file::printf(fH, ",%i,,,%i,%f,,,,,,,%i,%i,%i,,,,,%i,%i,%i\n",i,0,secs,m->status,d0,d1,dampPedalDownFl,softPedalDownFl,sostPedalDownFl);
        
      }
    }
        
    if( rc != kOkRC )
    {
      rc  = cwLogError(rc,"Write failed on line:%i", i+1 );
      goto errLabel;
    }
  }

 errLabel:
  file::close(fH);

  cwLogInfo("Saved %i events to sync perf. file. '%s'.", msgN, out_fname );

  return rc;
  
}
