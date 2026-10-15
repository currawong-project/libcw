#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwCsv.h"
#include "cwVectOps.h"
#include "cwScoreFollow3.h"
#include "cwScoreFollow3Impl.h"

namespace cw
{
  namespace score_follow_3
  {

    idLabelPair_t _match_typeA[] = {
      { kBlankTId, "<blank>"},
      { kMatchTId, "match"},
      { kMismatchTId, "mismatch" },
      { kNoMatchTId, "nomatch" },
      {kInvalidId,"<invalid>" }
    };
    
    
    sf_t* _handleToPtr(handle_t h)
    {
      return handleToPtr<handle_t,sf_t>(h);
    }
    
    rc_t _destroy( sf_t* p )
    {
      rc_t rc = kOkRC;
      if( p != nullptr )
      {
        if((rc = _destroy_impl(p)) != kOkRC )
        {
          rc = cwLogError(rc,"Destroy implementation failed.");
          goto errLabel;
        } 
        
        
        mem::release(p->noteA);
        mem::release(p->locA);
        mem::release(p);
      }
    errLabel:
      return rc;
    }

    rc_t _create_note_array( sf_t* p, const score_event_t* score_evtA, unsigned score_evtN )
    {
      rc_t rc = kOkRC;
      double max_sec = 0.0;

      p->min_score_loc_id = kInvalidId;
      p->max_score_loc_id = 0;
      
      p->noteN = score_evtN;
      p->noteA = mem::allocZ<note_t>(p->noteN);

      for(unsigned i=0; i<p->noteN; ++i)
      {
        const score_event_t& se = score_evtA[i];

        // since we are testing for increasing loc_id we only need to set the min_loc_id on the first iteration
        if( p->min_score_loc_id == kInvalidId )
          p->min_score_loc_id = se.loc_id;

        // track the min and max location id's and verify that the location id's are in increasing order
        if( se.loc_id < p->min_score_loc_id || se.loc_id < p->max_score_loc_id )
        {
          rc = cwLogError(kInvalidStateRC,"The location id's should be in increasing order. line:%i min:%i %i max:%i",i+1,p->min_score_loc_id,se.loc_id,p->max_score_loc_id);
          goto errLabel;
        }

        // since we are testing for increasing loc_id we set the max_loc_id on every iteration
        p->max_score_loc_id = se.loc_id;

        // verify that the score events are given in time order
        if( se.sec < max_sec )
        {
          rc = cwLogError(kInvalidStateRC,"The score events must be in strictly increasing time order. line:%i (%f max:%f).", i+1,se.sec,max_sec);
          goto errLabel;
        }
        else
        {
          max_sec = se.sec;
          
      
          note_t& n = p->noteA[i];
          n.uid      = i;
          n.pitch    = se.pitch;
          n.velocity = se.velocity;
          n.grace_fl = se.grace_fl;
        }
      
      }

    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Note array creation failed.");
      return rc;
      
    }

    rc_t _create_loc_array( sf_t* p, const score_event_t* score_evtA, unsigned score_evtN )
    {
      rc_t rc = kOkRC;

      if( score_evtN == 0 )
        return cwLogError(kInvalidArgRC,"The score appears to be empty.");

      p->locN = (p->max_score_loc_id - p->min_score_loc_id) + 1;
      p->locA = mem::allocZ<loc_t>(p->locN);

      unsigned loc_idx      = 0;
      unsigned cur_loc_id   = score_evtA[0].loc_id;
      unsigned beg_note_idx = 0;
            
      for(unsigned i=0; i<score_evtN; ++i)
      {
        const score_event_t& se = score_evtA[i];
        
        // if we have passed into the next loc.
        if( cur_loc_id != se.loc_id )
        {
          loc_idx      += 1;
          beg_note_idx  = i;
          cur_loc_id    = se.loc_id;
        }

        if( loc_idx > p->locN )
        {
          rc = cwLogError(kInvalidStateRC,"The location array initialization failed.");
          goto errLabel;
        }
        
        p->locA[loc_idx].sec          = se.sec;
        p->locA[loc_idx].loc_id       = se.loc_id;
        p->locA[loc_idx].meas_num     = se.meas_num;
        p->locA[loc_idx].beg_note_idx = beg_note_idx;
        p->locA[loc_idx].end_note_idx = i;

        if( i > p->noteN )
        {
          rc = cwLogError(kInvalidStateRC,"The note index %i was found to be out of range:%i.",i,p->noteN);
          goto errLabel;
        }

        p->noteA[i].loc = p->locA + loc_idx;
        
      }

      p->locN = loc_idx+1;

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"The location array create failed.");
      return rc;
    }

    rc_t _loc_id_to_index( sf_t* p, unsigned loc_id, unsigned& loc_idx_ref )
    {
      loc_idx_ref = kInvalidIdx;
      
      for(unsigned i=0; i<p->locN; ++i)
        if( p->locA[i].loc_id == loc_id )
        {
          loc_idx_ref = i;
          return kOkRC;
        }

      return cwLogError(kInvalidArgRC,"The loc. id %i was not found.",loc_id);
    }

    rc_t _reset( sf_t* p, unsigned beg_loc_id, unsigned end_loc_id )
    {
      rc_t rc = kOkRC;
      
      if((rc = _loc_id_to_index(p,beg_loc_id,p->active_beg_loc_idx)) != kOkRC )
      {
        goto errLabel;
      }
      
      if((rc = _loc_id_to_index(p,end_loc_id,p->active_end_loc_idx)) != kOkRC )
      {
        goto errLabel;
      }
      
      p->perf_base_time = std::numeric_limits<double>::infinity();

      if((rc = _reset_impl(p)) != kOkRC )
      {
        rc = cwLogError(rc,"Rset implementation failed.");
        goto errLabel;
      }
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Reset failed.");
      return rc;
    }
    
    
  }
}


const char* cw::score_follow_3::match_status_id_to_label( match_status_id_t tid )
{
  return idToLabelNull( _match_typeA, tid, kInvalidId );  
}

cw::rc_t cw::score_follow_3::match_status_label_to_id( const char* label, match_status_id_t& id_ref )
{
  rc_t rc = kOkRC;
  unsigned id = kInvalidId;
  
  if( (id = labelToId(_match_typeA, label, kInvalidId )) == kInvalidId )
  {
    rc = kEleNotFoundRC;
  }
  else
  {
    id_ref = (match_status_id_t)id;
  }
  
  return rc;
}



cw::rc_t cw::score_follow_3::create( handle_t& hRef, const args_t& args, const score_event_t* scoreA, unsigned scoreN )
{
  rc_t rc = kOkRC;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  sf_t* p = mem::allocZ<sf_t>();

  if((rc = _create_note_array(p,scoreA,scoreN)) != kOkRC )
  {
    goto errLabel;
  }
  
  if((rc = _create_loc_array(p,scoreA,scoreN)) != kOkRC )
  {
    goto errLabel;
  }
  
  if((rc = _create_impl(p,args)) != kOkRC )
  {
    goto errLabel;
  }
  
  if((rc = _reset(p,p->min_score_loc_id, p->max_score_loc_id)) != kOkRC )
  {
    goto errLabel;
  }


  hRef.set(p);

  
errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::score_follow_3::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  sf_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
  {
    goto errLabel;
  }

  hRef.clear();
  
errLabel:
  return rc;
  
}

cw::rc_t cw::score_follow_3::reset( handle_t h, unsigned beg_loc_id, unsigned end_loc_id )
{
  rc_t rc = kOkRC;
  sf_t* p = _handleToPtr(h);
  if((rc =  _reset(p,beg_loc_id,end_loc_id)) != kOkRC )
  {
    goto errLabel;
  }

errLabel:
  return rc;
}

   
cw::rc_t cw::score_follow_3::on_new_note( handle_t h,
                                          unsigned perf_uid,
                                          double sec,
                                          unsigned perf_pitch,
                                          unsigned perf_vel,
                                          result_t& result_ref )
{
  rc_t rc = kOkRC;
  sf_t* p = _handleToPtr(h);
  
  result_ref.loc_id          = kInvalidId;
  result_ref.perf_uid        = perf_uid;
  result_ref.match_status_id = kBlankTId;
  result_ref.confidence      = 0;

  if( !std::isfinite(p->perf_base_time) )
    p->perf_base_time = sec;

  if( sec < p->perf_base_time )
  {
    cwLogWarning("Notes arrived out of time order: (%f < start time:%f).",sec,p->perf_base_time);
    sec = p->perf_base_time;
  }
  
  double perf_sec = sec - p->perf_base_time;

  if((rc = _on_new_note_impl(p,perf_uid,perf_sec,perf_pitch, perf_vel, result_ref )) != kOkRC )
  {
    rc = cwLogError(rc,"On-new-note implemenation failed.");
    goto errLabel;
  }
  
errLabel:
  return rc;
}



cw::rc_t cw::score_follow_3::exec( handle_t h, double sec )
{
  rc_t rc = kOkRC;
  sf_t* p = _handleToPtr(h);

  // if the first note has not yet arrived
  // or the active region has already been fully tracked there is nothing to do.
  if( !std::isfinite(p->perf_base_time) || is_done(h) )
    return kOkRC;


  if( sec < p->perf_base_time )
  {
    cwLogWarning("exec() occurred out of time order: (%f < perf start time:%f).",sec,p->perf_base_time);
    sec = p->perf_base_time;
  }
  
  double perf_sec = sec - p->perf_base_time;

  if((rc = _exec_impl(p, perf_sec )) != kOkRC )
  {
    rc = cwLogError(rc,"exec implementation failed.");
    goto errLabel;
  }
  
errLabel:
  return rc;
}

bool cw::score_follow_3::is_done( handle_t h )
{
  sf_t* p = _handleToPtr(h);
  return _is_done_impl(p);
}
