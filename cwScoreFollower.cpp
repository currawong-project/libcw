#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwFileSys.h"
#include "cwMidi.h"
#include "cwMidiFile.h"
#include "cwCmInterface.h"
#include "cwScoreFollowerPerf.h"
#include "cwScoreFollower.h"
#include "cwMidiState.h"

#include "cmGlobal.h"
#include "cmFloatTypes.h"
#include "cmRpt.h"
#include "cmErr.h"
#include "cmCtx.h"
#include "cmMem.h"
#include "cmSymTbl.h"
#include "cmLinkedHeap.h"
#include "cmTime.h"
#include "cmMidi.h"
#include "cmSymTbl.h"
#include "cmMidiFile.h"
#include "cmAudioFile.h"
#include "cmScore.h"
#include "cmTimeLine.h"
#include "cmProcObj.h"
#include "cmProc4.h"

#include "cwSvgScoreFollow.h"

namespace cw
{
  namespace score_follower
  {
    typedef struct score_follower_str
    {
      double       srate;
      unsigned     search_area_locN;
      unsigned     key_wnd_locN;
      char*        cm_score_csv_fname;
      cmCtx_t*     cmCtx;
      cmScH_t      cmScoreH;
      cmScMatcher* matcher;
      
      unsigned*    cwLocToCmLocA;  // cwLocToCmLocA[ cwLocToCmLocN ]
      unsigned     cwLocToCmLocN;

      unsigned*    cmLocToCwLocA;  // cmLocToCwLocA[ cmLocToCwLocN ]
      unsigned     cmLocToCwLocN;
      
      unsigned*    match_idA;
      unsigned     match_id_allocN;
      unsigned     match_id_curN;

      ssf_note_on_t* perfA;
      unsigned       perfN;
      unsigned       perf_idx; 
  
    } score_follower_t;

    score_follower_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,score_follower_t>(h); }


    // parse the score follower parameter record
    rc_t _parse_cfg( score_follower_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;

      const char* cm_score_csv_fname;
      
      if((rc = cfg->getv("cm_score_csv_fname", cm_score_csv_fname,
                         "search_area_locN",    p->search_area_locN,
                         "key_wnd_locN",        p->key_wnd_locN )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC, "Score follower argument parsing failed.");
        goto errLabel;
      }

      if((p->cm_score_csv_fname = filesys::expandPath( cm_score_csv_fname )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Score follower score file expansion failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    // get the max cw loc value
    rc_t  _get_max_cw_loc( score_follower_t* p, unsigned& maxCwLocRef )
    {
      rc_t rc = kOkRC;
      
      // Note: cmScoreEvt_t.csvEventId is the app score 'loc' value.
      
      unsigned cmEvtN = cmScoreEvtCount( p->cmScoreH );
      
      maxCwLocRef = 0;
      
      for(unsigned i=0; i<cmEvtN; ++i)
      {
        cmScoreEvt_t* cmEvt = cmScoreEvt( p->cmScoreH, i );

        if( cmEvt == nullptr )
        {
          cwLogError(kOpFailRC,"Unexpected missing cm event at index '%i while scanning cmScore.h",i);
          goto errLabel;
        }
        
        if( cmEvt->csvEventId > maxCwLocRef )
          maxCwLocRef = cmEvt->csvEventId;
      }
      
    errLabel:
      return rc;
    }

    // Create cw-to-cm location map
    rc_t _createCwLocToCmLocMap( score_follower_t* p )
    {
      unsigned cmEvtN =0;
      rc_t rc = kOkRC;
      
      if((rc = _get_max_cw_loc(p,p->cwLocToCmLocN)) != kOkRC )
        goto errLabel;

      p->cwLocToCmLocN += 1; // incr. by 1 because _get_max_cw_loc() returns an index but we need a count 
      p->cwLocToCmLocA = mem::allocZ<unsigned>( p->cwLocToCmLocN );

      cmEvtN = cmScoreEvtCount( p->cmScoreH );

      for(unsigned i=0; i<cmEvtN; ++i)
      {
        const cmScoreEvt_t* cmEvt = cmScoreEvt( p->cmScoreH, i );
        assert( cmEvt != nullptr );
        p->cwLocToCmLocA[ cmEvt->csvEventId ] = cmEvt->locIdx;
      }

    errLabel:
      
      return rc;      
    }

    // create cm-to-cw location map
    rc_t _createCmLocToCwLocMap( score_follower_t* p )
    {
      rc_t rc = kOkRC;

      unsigned cmEvtN = cmScoreEvtCount( p->cmScoreH );

      p->cmLocToCwLocN = cmEvtN;
      p->cmLocToCwLocA = mem::allocZ<unsigned>( p->cmLocToCwLocN );

      for(unsigned i=0; i<cmEvtN; ++i)
      {
        const cmScoreEvt_t* cmEvt = cmScoreEvt( p->cmScoreH, i );
        assert( cmEvt != nullptr );
        p->cmLocToCwLocA[i] = cmEvt->csvEventId;
      }
      
      return rc;
    }

    rc_t _update_midi_state( score_follower_t* p, midi_state::handle_t msH )
    {
      rc_t rc;
      for(unsigned i=0; i<p->perf_idx; ++i)
      {
        if((rc = setMidiMsg( msH, p->perfA[i].sec, i, 0, midi::kNoteOnMdId, p->perfA[i].pitch, p->perfA[i].vel )) != kOkRC )
        {
          rc = cwLogError(rc,"midi_state update failed.");
          goto errLabel;
        }
      }
      
    errLabel:
      return rc;
    }

    rc_t _destroy( score_follower_t* p)
    {
      mem::release(p->cmLocToCwLocA);
      mem::release(p->cwLocToCmLocA);                   
      mem::release(p->cm_score_csv_fname);
      mem::release(p->perfA);
      mem::release(p->match_idA);
      cmScMatcherFree(&p->matcher);
      cmScoreFinalize(&p->cmScoreH);
      mem::release(p);
      return kOkRC;
    }


    extern "C" void _score_follower_cb( struct cmScMatcher_str* smp, void* arg, cmScMatcherResult_t* r )
    {
      score_follower_t* p = (score_follower_t*)arg;
      cmScoreEvt_t* cse;

      //printf("%4i %4i %4i %3i %3i %4s : ", r->locIdx, r->mni, r->muid, r->flags, r->pitch, midi::midiToSciPitch( r->pitch, nullptr, 0 ));

      if( r->scEvtIdx == cmInvalidIdx )
      {
        //cwLogInfo("Score Follower: MISS");
      }
      else
      {
        // get a pointer to the matched event
        if((cse = cmScoreEvt( p->cmScoreH, r->scEvtIdx )) == nullptr )
        {
          cwLogError(kInvalidStateRC,"cm Score event index (%i) reported by the score follower is invalid.",r->scEvtIdx );
        }
        else
        {
          if( p->match_id_curN >= p->match_id_allocN )
          {
            cwLogError(kInvalidStateRC,"The score follower match id array is full.");
          }
          else
          {
            // the csvEventId corresponds to the cwPianoScore location 
            p->match_idA[ p->match_id_curN++ ] = cse->csvEventId;
            
            //cwLogInfo("Score Follower: MATCH %i\n",cse->csvEventId);
            
          }
        }
      }
    }
  }
}

cw::rc_t cw::score_follower::create( handle_t& hRef, const object_t* cfg, cm::handle_t cmCtxH, double srate )
{
  rc_t        rc        = kOkRC;
  cmSymTblH_t cmSymTblH = cmSTATIC_NULL_HANDLE;
  cmScRC_t    scoreRC   = cmOkRC;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  score_follower_t* p = mem::allocZ<score_follower_t>();

  p->cmCtx     = context(cmCtxH);
  
  if((rc = _parse_cfg(p,cfg)) != kOkRC )
    goto errLabel;

  // create the the score follower reference score
  if((scoreRC = cmScoreInitialize( p->cmCtx, &p->cmScoreH, p->cm_score_csv_fname, srate, nullptr, 0, nullptr, nullptr, cmSymTblH )) != cmOkRC )
  {
    rc = cwLogError(kOpFailRC,"The score could not be initialized from '%s'. cmRC:%i.",p->cm_score_csv_fname);
    goto errLabel;
  }

  // create the score follower
  if((p->matcher = cmScMatcherAlloc( proc_context(cmCtxH),  // Program context.
                                     nullptr,               // Existing cmScMatcher to reallocate or NULL to allocate a new cmScMatcher.
                                     srate,                 // System sample rate.
                                     p->cmScoreH,           // Score handle.  See cmScore.h.
                                     p->search_area_locN,   // Length of the scores active search area. ** See Notes.
                                     p->key_wnd_locN,       // Length of the MIDI active note buffer.    ** See Notes.
                                     _score_follower_cb,    // A cmScMatcherCb_t function to be called to notify the recipient of changes in the score matcher status.
                                     p )) == nullptr )      // User argument to 'cbFunc'.
  {
    cwLogError(kOpFailRC,"The score follower allocation failed.");
    goto errLabel;
  }
  
  // create the cw-to-cm loc idx map
  if((rc = _createCwLocToCmLocMap( p )) != kOkRC )
  {
    rc = cwLogError(rc,"cw-to-cm loc map creation failed.");
    goto errLabel;
  }

  if((rc = _createCmLocToCwLocMap( p )) != kOkRC )
  {
    rc = cwLogError(rc,"cm-to-cw loc map creation failed.");
    goto errLabel;
  }

  
  p->srate           = srate;
  p->match_id_allocN = cmScoreEvtCount( p->cmScoreH )*2;  // give plenty of extra space for the match_idA[]
  p->match_idA       = mem::allocZ<unsigned>(p->match_id_allocN);

  p->perfN = cmScoreEvtCount(p->cmScoreH)*2;
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

cw::rc_t cw::score_follower::reset( handle_t h, unsigned cwLocId )
{
  rc_t              rc   = kOkRC;
  cmRC_t            cmRC = cmOkRC;  
  score_follower_t* p    = _handleToPtr(h);

  if( cwLocId != kInvalidId )
  {
    unsigned cmLocId = p->cwLocToCmLocA[cwLocId];

    if( cmLocId == kInvalidId )
    {
      rc = cwLogWarning("The cw loc id:%i does not translate to a cm loc id.",cwLocId);
      goto errLabel;
    }

    printf("SF Reset: cw:%i cm:%i\n",cwLocId,cmLocId);
    
    if((cmRC = cmScMatcherReset( p->matcher, cmLocId )) != cmOkRC )
    {
      rc = cwLogError(kOpFailRC,"The score follower reset failed.");
      goto errLabel;
    }

    p->perf_idx = 0;
    clear_match_id_array(h);
  }
  
 errLabel:
  return rc;
}
    
cw::rc_t cw::score_follower::exec(  handle_t h, double sec, unsigned smpIdx, unsigned muid, unsigned status, uint8_t d0, uint8_t d1, bool& newMatchFlRef )
{
  rc_t              rc                 = kOkRC;
  score_follower_t* p                  = _handleToPtr(h);
  unsigned          scLocIdx           = cmInvalidIdx;
  unsigned          pre_match_id_curN = p->match_id_curN;

  newMatchFlRef = false;

  // Note: pass p->perf_idx as 'muid' to the score follower
  cmRC_t cmRC = cmScMatcherExec(  p->matcher, smpIdx, p->perf_idx, status, d0, d1, &scLocIdx );
  
  switch( cmRC )
  {
    case cmOkRC:
      newMatchFlRef = p->match_id_curN != pre_match_id_curN;
      //printf("NM_FL:%i\n",newMatchFlRef);
      break;
        
    case cmEofRC:
      rc = cwLogInfo("Score match complete.");
      break;
        
    case cmInvalidArgRC:
      rc = cwLogError(kInvalidStateRC,"Score follower state is invalid.");
      break;
        
    case cmSubSysFailRC:
      rc = cwLogError(kOpFailRC,"The score follower failed during a resync attempt.");
      break;
        
    default:
      rc = cwLogError(kOpFailRC,"The score follower failed with an unknown error. cmRC:%i",cmRC);
  }

  // store note-on messages
  if( p->perf_idx < p->perfN && midi::isNoteOn(status,(unsigned)d1) )
  {

    ssf_note_on_t* pno = p->perfA + p->perf_idx;
    pno->sec   = sec;
    pno->muid  = muid;
    pno->pitch = d0;
    pno->vel   = d1;    
    p->perf_idx += 1;
    if( p->perf_idx >= p->perfN )
      cwLogWarning("The cw score follower performance cache is full.");
  }
      
  return rc;
}

const unsigned* cw::score_follower::current_match_id_array( handle_t h, unsigned& cur_match_id_array_cnt_ref )
{
  score_follower_t* p = _handleToPtr(h);
  cur_match_id_array_cnt_ref = p->match_id_curN;
  return p->match_idA;
}

void cw::score_follower::clear_match_id_array( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  p->match_id_curN = 0;
}

cw::rc_t cw::score_follower::cw_loc_range( handle_t h, unsigned& minLocRef, unsigned& maxLocRef )
{
  rc_t rc = kOkRC;
  score_follower_t* p = _handleToPtr(h);

  minLocRef = kInvalidId;
  maxLocRef = kInvalidId;
  
  if( p->cwLocToCmLocA==nullptr || p->cwLocToCmLocN == 0 )
  {
    rc = cwLogError(kInvalidStateRC,"The cw location range is not yet set.");
    goto errLabel;
  }
  
  minLocRef = 1;
  maxLocRef = p->cwLocToCmLocN-1;

 errLabel:
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
    
unsigned cw::score_follower::has_stored_performance( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return p->perf_idx > 0 && p->matcher->ri > 0;
}

cw::rc_t cw::score_follower::sync_perf_to_score( handle_t h )
{
  rc_t rc = kOkRC;
  score_follower_t* p = _handleToPtr(h);

  if( !has_stored_performance(h) )
  {
    rc = cwLogError(kInvalidStateRC,"No performance to sync.");
    goto errLabel;
  }
  
  for(unsigned i=0; i<p->matcher->ri; ++i)
  {
    unsigned perf_idx = p->matcher->res[i].muid;

    // the matcher result 'muid' is the perf. array index of the matching perf. record
    if( perf_idx >= p->perf_idx )
    {
      rc = cwLogError(kInvalidStateRC,"Inconsistent match to perf. map: index mismatch.");
      goto errLabel;
    }

    // verify the pitch of the matching records 
    if( p->perfA[ perf_idx ].pitch != p->matcher->res[i].pitch )
    {
      rc = cwLogError(kInvalidStateRC,"Inconsistent match to perf. map: pitch mismatch %i != %i.",p->perfA[ perf_idx ].pitch ,p->matcher->res[i].pitch);
      goto errLabel;
    }

    assert( p->matcher->res[i].scEvtIdx == kInvalidIdx || p->matcher->res[i].scEvtIdx < p->cmLocToCwLocN );

    if( p->matcher->res[i].scEvtIdx != kInvalidIdx )
      p->perfA[ perf_idx ].loc = p->cmLocToCwLocA[ p->matcher->res[i].scEvtIdx ];
  }

 errLabel:
  return rc;
}

unsigned cw::score_follower::perf_count( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return p->perfN;
}

const cw::score_follower::ssf_note_on_t* cw::score_follower::perf_base( handle_t h )
{
  score_follower_t* p = _handleToPtr(h);
  return p->perfA;
}

cw::rc_t cw::score_follower::write_svg_file( handle_t h, const char* out_fname, bool show_muid_fl )
{
  score_follower_t* p = _handleToPtr(h);

  return svgScoreFollowWrite( p->cmScoreH, p->matcher, p->perfA, p->perf_idx, out_fname, show_muid_fl );
}

void cw::score_follower::score_report( handle_t h, const char* out_fname )
{
  score_follower_t* p = _handleToPtr(h);
  cmScoreReport( p->cmCtx,  p->cm_score_csv_fname, out_fname );  
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


namespace cw {
  namespace score_follower {

    typedef struct test_str
    {
      double          srate;      
      char*           out_dir;
      const object_t* cfg;      
      const char*     out_svg_fname;
      const char*     out_midi_csv_fname;
      const char*     out_cm_score_rpt_fname;
      const object_t* perfL;
      bool  pre_test_fl;   // insert a dummy note prior to the first perf. note to test the 'pre' functionality of the SVG generation
      bool  show_muid_fl;  // true=show perf. note 'muid' in SVG file, false=show sequence id
    } test_t;

    
    rc_t _test_destroy( test_t* p )
    {
      rc_t rc = kOkRC;
      mem::release(p->out_dir);
      
      return rc;
    }

    
    rc_t _test_parse_cfg( test_t* p, const object_t* cfg )
    {
      rc_t        rc      = kOkRC;
      const char* out_dir = nullptr;
      
      // read the test cfg.
      if((rc = cfg->getv("perfL",        p->perfL,
                         "srate",        p->srate,
                         "pre_test_fl",  p->pre_test_fl,
                         "show_muid_fl", p->show_muid_fl,
                         "cfg",          p->cfg,
                         "out_dir",      out_dir,
                         "out_svg_fname",p->out_svg_fname,
                         "out_midi_csv_fname", p->out_midi_csv_fname,
                         "out_cm_score_rpt_fname", p->out_cm_score_rpt_fname)) != kOkRC )
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


    rc_t _score_follow_one_perf( test_t* p, handle_t& sfH, unsigned perf_idx )
    {
      rc_t                 rc         = kOkRC;
      bool                 pre_test_fl= p->pre_test_fl;
      bool                 enable_fl  = true;
      const char*          perf_label = nullptr;
      const char*          midi_fname = nullptr;
      char*                out_dir    = nullptr;
      char*                fname      = nullptr;
      unsigned             start_loc  = 0;
      const object_t*      perf       = nullptr;
      unsigned             msgN       = 0;
      midi::file::handle_t mfH;      
      const midi::file::trackMsg_t** msgA = nullptr;

      // get the perf. record
      if((perf = p->perfL->child_ele(perf_idx)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"Error accessing the cfg record for perf. record index:%i.",perf_idx);
        goto errLabel;
      }

      // parse the performance record
      if((rc = perf->getv("label",perf_label,
                          "enable_fl",enable_fl,
                          "start_loc", start_loc,
                          "midi_fname", midi_fname)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Error parsing cfg record for perf. record index:%i.",perf_idx);
        goto errLabel;
      }

      if( !enable_fl )
        goto errLabel;
          
      // create the output directory
      if((out_dir = filesys::makeFn(p->out_dir,perf_label,nullptr,nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Directory name formation failed on '%s'.",cwStringNullGuard(out_dir));
        goto errLabel;        
      }

      // create the output directory
      if((rc = filesys::makeDir(out_dir)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"mkdir failed on '%s'.",cwStringNullGuard(out_dir));
        goto errLabel;        
      }

      // expand the MIDI filename
      if((fname = filesys::expandPath( midi_fname )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The MIDI file path expansion failed.");
        goto errLabel;
      }
      
      // open the midi file
      if((rc = midi::file::open( mfH, fname )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file open failed on '%s'.",cwStringNullGuard(fname));
        goto errLabel;
      }

      mem::release(fname);

      // set the score follower to the start location
      if((rc = reset( sfH, start_loc)) != kOkRC )
        goto errLabel;

      
      // get a pointer to a time sorted list of MIDI messages in the file
      if(((msgN = msgCount( mfH )) == 0) || ((msgA = midi::file::msgArray( mfH )) == nullptr) )
      {
        rc = cwLogError(rc,"MIDI file msg array is empty or corrupp->");
        goto errLabel;    
      }

      for(unsigned i=0; i<msgN; ++i)
      {
        const midi::file::trackMsg_t* m = msgA[i];

        if( midi::file::isNoteOn( m ) )
        {
          double        sec        = (double)m->amicro/1e6;      
          unsigned long smpIdx     = (unsigned long)(p->srate * m->amicro/1e6);
          bool          newMatchFl = false;

          if( pre_test_fl )
          {
            // test the 'pre' ref location by adding an extra note before the first note
            exec(sfH, sec, smpIdx, m->uid-1, m->status, 60, m->u.chMsgPtr->d1, newMatchFl);
            pre_test_fl = false;
          }
      
          if((rc = exec(sfH, sec, smpIdx, m->uid, m->status, m->u.chMsgPtr->d0, m->u.chMsgPtr->d1, newMatchFl )) != kOkRC )
          {
            rc = cwLogError(rc,"score follower exec failed.");
            break;
          }

          if( newMatchFl )
          {
            unsigned matchIdN = 0;
            const unsigned* matchIdA = current_match_id_array(sfH, matchIdN );

            for(unsigned i=0; i<matchIdN; ++i)
              cwLogInfo("Match:%i",matchIdA[i]);
        
            clear_match_id_array( sfH );
          }
        }
      }


      // create the SVG output filename
      if((fname = filesys::makeFn(out_dir,p->out_svg_fname,nullptr,nullptr)) == nullptr )
      {
        cwLogError(kOpFailRC,"The output SVG filename formation failed.");
        goto errLabel;
      }

      // write the score following result SVG
      if((rc = write_svg_file(sfH,fname,p->show_muid_fl)) != kOkRC )
      {
        rc = cwLogError(rc,"SVG report file create failed.");
        goto errLabel;
      }
      
      mem::release(fname);
      

      // create the MIDI file as a CSV
      if((fname = filesys::makeFn(out_dir,p->out_midi_csv_fname,nullptr,nullptr)) == nullptr )
      {
        cwLogError(kOpFailRC,"The output MIDI CSV filename formation failed.");
        goto errLabel;
      }

      // convert the MIDI file to a CSV
      if((rc = midi::file::genCsvFile( filename(mfH), fname, false)) != kOkRC )
      {
        cwLogError(rc,"MIDI file to CSV failed on '%s'.",cwStringNullGuard(filename(mfH)));
      }
      
      mem::release(fname);
      
    errLabel:
      mem::release(out_dir);
      mem::release(fname);
      close(mfH);

      return rc;
    }

    
  }
}

cw::rc_t cw::score_follower::test( const object_t* cfg )
{
  rc_t rc = kOkRC;

  test_t t = {0};
  
  cm::handle_t cmCtxH;
  handle_t sfH;
  char* fname = nullptr;
  
  // parse the test cfg
  if((rc = _test_parse_cfg( &t, cfg )) != kOkRC )
    goto errLabel;

  // create a cm context record
  if((rc = cm::create(cmCtxH)) != kOkRC )
    goto errLabel;

  // create the score follower
  if((rc = create( sfH, t.cfg, cmCtxH, t.srate )) != kOkRC )
    goto errLabel;

  // create the cm score report filename
  if((fname = filesys::makeFn(t.out_dir,t.out_cm_score_rpt_fname,nullptr,nullptr)) == nullptr )
  {
    cwLogError(kOpFailRC,"The output cm score filename formation failed.");
    goto errLabel;
  }
      
  // write the cm score report
  score_report(sfH,fname);

  // score follow each performance
  for(unsigned perf_idx=0; perf_idx<t.perfL->child_count(); ++perf_idx)
    _score_follow_one_perf(&t,sfH,perf_idx);
  
  
 errLabel:
  mem::release(fname);
  destroy(sfH);
  cm::destroy(cmCtxH);
  _test_destroy(&t);
  
  return rc;
}
