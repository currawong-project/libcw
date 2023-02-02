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
#include "cwScoreFollower.h"

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

namespace cw
{
  namespace score_follower
  {

    typedef struct score_follower_str
    {
      double       srate;
      unsigned     search_area_locN;
      unsigned     key_wnd_locN;
      char*        score_csv_fname;
      cmScH_t      cmScoreH;
      cmScMatcher* matcher;
      unsigned*    match_idA;
      unsigned     match_id_allocN;
      unsigned     match_id_curN;
    } score_follower_t;

    score_follower_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,score_follower_t>(h); }

    
    rc_t _parse_cfg( score_follower_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;

      const char* score_csv_fname;
      
      if((rc = cfg->getv("score_csv_fname",     score_csv_fname,
                         "search_area_locN",    p->search_area_locN,
                         "key_wnd_locN",        p->key_wnd_locN )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC, "Score follower argument parsing failed.");
        goto errLabel;
      }

      if((p->score_csv_fname = filesys::expandPath( score_csv_fname )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Score follower score file expansion failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    unsigned _get_max_cw_loc_value( score_follower_t* p )
    {
      // Note: cmScoreEvt_t.csvEventId is the app score 'loc' value.
      
      unsigned cmEvtN = cmScoreEvtCount( p->cmScoreH );
      unsigned maxCwLoc = 0;
      for(unsigned i=0; i<cmEvtN; ++i)
      {
      }
      
      return maxCwLoc; 
    }
    
    void _createCwLocToCmLocMap( score_follower_t* p )
    {
    }

    rc_t _destroy( score_follower_t* p)
    {
      mem::release(p->score_csv_fname);
      mem::release(p);
      cmScMatcherFree(&p->matcher);
      cmScoreFinalize(&p->cmScoreH);
      return kOkRC;
    }


    extern "C" void _score_follower_cb( struct cmScMatcher_str* smp, void* arg, cmScMatcherResult_t* r )
    {
      score_follower_t* p = (score_follower_t*)arg;
      cmScoreEvt_t* cse;

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
        }
      }
      
    }


    
  }
}

cw::rc_t cw::score_follower::create( handle_t& hRef, const object_t* cfg, cm::handle_t cmCtxH, double srate )
{
  rc_t        rc        = kOkRC;
  cmCtx_t*    cmCtx     = context(cmCtxH);
  cmProcCtx*  cmProcCtx = proc_context(cmCtxH);
  cmSymTblH_t cmSymTblH = cmSTATIC_NULL_HANDLE;
  cmScRC_t    scoreRC   = cmOkRC;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  score_follower_t* p = mem::allocZ<score_follower_t>();

  if((rc = _parse_cfg(p,cfg)) != kOkRC )
    goto errLabel;

  if((scoreRC = cmScoreInitialize( cmCtx, &p->cmScoreH, p->score_csv_fname, srate, nullptr, 0, nullptr, nullptr, cmSymTblH )) != cmOkRC )
  {
    cwLogError(kOpFailRC,"The score could not be initialized from '%s'. cmRC:%i.",p->score_csv_fname);
    goto errLabel;
  }


  if((p->matcher = cmScMatcherAlloc(  cmProcCtx,           // Program context.
                                     nullptr,             // Existing cmScMatcher to reallocate or NULL to allocate a new cmScMatcher.
                                     srate,               // System sample rate.
                                     p->cmScoreH,           // Score handle.  See cmScore.h.
                                     p->search_area_locN, // Length of the scores active search area. ** See Notes.
                                     p->key_wnd_locN,     // Length of the MIDI active note buffer.    ** See Notes.
                                     _score_follower_cb,  // A cmScMatcherCb_t function to be called to notify the recipient of changes in the score matcher status.
                                     p )) == nullptr )    // User argument to 'cbFunc'.
  {
    cwLogError(kOpFailRC,"The score follower allocation failed.");
    goto errLabel;
  }
  
  
  p->srate = srate;
  p->match_id_allocN = 128;
  p->match_idA       = mem::allocZ<unsigned>(p->match_id_allocN);
  
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

cw::rc_t cw::score_follower::reset( handle_t h, unsigned loc )
{
  rc_t rc = kOkRC;
  //score_follower_t* p = _handleToPtr(h);

  
  // cmRC_t       cmScMatcherReset( cmScMatcher* p, unsigned scLocIdx );
  
  return rc;
}
    
cw::rc_t cw::score_follower::exec(  handle_t h, unsigned smpIdx, unsigned muid, unsigned status, uint8_t d0, uint8_t d1, bool& newMatchFlRef )
{
  rc_t              rc                 = kOkRC;
  cmRC_t            cmRC               = cmOkRC;
  score_follower_t* p                  = _handleToPtr(h);
  unsigned          scLocIdx           = cmInvalidIdx;
  unsigned          pre_match_id_curN = p->match_id_curN;

  newMatchFlRef = false;
    
  if((cmRC = cmScMatcherExec(  p->matcher, smpIdx, muid, status, d0, d1, &scLocIdx )) != cmOkRC )
  {
    switch( cmRC )
    {
      case cmOkRC:
        newMatchFlRef = p->match_id_curN != pre_match_id_curN;
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
      
  }

  
  return rc;

}


unsigned* cw::score_follower::current_match_id_array( handle_t h, unsigned& cur_match_id_array_cnt_ref )
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


namespace cw {
  namespace score_follower {

    typedef struct test_str
    {
      char*                midi_fname;
      char*                out_dir;
      double               srate;
      const object_t*      cfg;
      midi::file::handle_t mfH;
      
    } test_t;

    
    rc_t _test_destroy( test_t* p )
    {
      rc_t rc = kOkRC;
      mem::release(p->midi_fname);
      mem::release(p->out_dir);

      midi::file::close(p->mfH);
      
      return rc;
    }
    
    rc_t _test_parse_cfg( test_t* p, const object_t* cfg )
    {
      rc_t        rc;
      const char* midi_fname = nullptr;
      const char* out_dir    = nullptr;

      // read the test cfg.
      if((rc = cfg->getv("midi_fname", midi_fname,
                         "srate",      p->srate,
                         "cfg",        p->cfg,
                         "out_dir",    out_dir )) != kOkRC )
      {
        rc = cwLogError(rc,"Score follower test cfg. parse failed.");
        goto errLabel;
      }

      // expand the MIDI filename
      if((p->midi_fname = filesys::expandPath( midi_fname )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The MIDI file path expansion failed.");
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
      if(rc != kOkRC )
      {
        _test_destroy(p);
        rc = cwLogError(rc,"Score follower test parse cfg. failed.");
      }

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
  unsigned msgN = 0;
  const midi::file::trackMsg_t** msgA = nullptr;
    
  // parse the test cfg
  if((rc = _test_parse_cfg( &t, cfg )) != kOkRC )
    goto errLabel;

  // create a cm context record
  if((rc = cm::create(cmCtxH)) != kOkRC )
    goto errLabel;

  // create the score follower
  if((rc = create( sfH, t.cfg, cmCtxH, t.srate )) != kOkRC )
    goto errLabel;

  // open the midi file
  if((rc = midi::file::open( t.mfH, t.midi_fname )) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI file open failed on '%s'.",cwStringNullGuard(t.midi_fname));
    goto errLabel;
  }

  // get a pointer to a time sorted list of MIDI messages in the file
  if(((msgN = msgCount( t.mfH )) == 0) || ((msgA = midi::file::msgArray( t.mfH )) == nullptr) )
  {
    rc = cwLogError(rc,"MIDI file msg array is empty or corrupt.");
    goto errLabel;    
  }

  for(unsigned i=0; i<msgN; ++i)
  {
    const midi::file::trackMsg_t* m = msgA[i];

    if( midi::file::isNoteOn( m ) )
    {
      unsigned long smpIdx = (unsigned long)(t.srate * m->amicro/1e6);
      bool newMatchFl = false;
      if((rc = exec(sfH, smpIdx, m->uid, m->status, m->u.chMsgPtr->d0, m->u.chMsgPtr->d1, newMatchFl )) != kOkRC )
      {
        rc = cwLogError(rc,"score follower exec failed.");
        goto errLabel;
      }

      if( newMatchFl )
      {
        unsigned matchIdN = 0;
        unsigned* matchIdA = current_match_id_array(sfH, matchIdN );

        for(unsigned i=0; i<matchIdN; ++i)
          cwLogInfo("Match:%i",matchIdA[i]);
        
        clear_match_id_array( sfH );
      }
    }
  }
  
 errLabel:
  destroy(sfH);
  cm::destroy(cmCtxH);
  _test_destroy(&t);
  
  return rc;
}
