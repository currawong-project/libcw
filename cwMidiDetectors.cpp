//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwTime.h"

#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwMidiDetectors.h"


namespace cw {

  namespace midi_detect {

    namespace piano {

      
      typedef struct detector_str
      {        
        state_t* stateA;
        unsigned stateN;        
      } detector_t;

      typedef struct piano_det_str
      {
        unsigned pedal_thresh;
        
        detector_t* detA;
        unsigned    detN;
        unsigned    allocDetN;

        unsigned* keyM;   // 128 * 16
        unsigned* ctlM;   // 128 * 16
        
      } piano_det_t;

      piano_det_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,piano_det_t>(h); }

      rc_t _destroy( piano_det_t* p )
      {
        if( p != nullptr )
        {
          for(unsigned i=0; i<p->detN; ++i)
            mem::release(p->detA[i].stateA);

          mem::release(p->detA);
          mem::release(p->keyM);
          mem::release(p->ctlM);
        }

        return kOkRC;
      }

      bool _is_state_matched( piano_det_t* p, const state_t* s )
      {
        
        switch( s->status )
        {
          case midi::kNoteOnMdId:
            {
              unsigned v = p->keyM[(s->ch*midi::kMidiNoteCnt) + s->d0];

              return s->release_fl ? v==0 : v>0;
            }
            break;
            
          case midi::kCtlMdId:
            {
              unsigned v = p->ctlM[(s->ch*midi::kMidiNoteCnt) + s->d0];

              return s->release_fl ? v<p->pedal_thresh : v>=p->pedal_thresh;
            }
            break;
          default:
            assert(false);
        }

        return false;
      }
      
    }
  }
}


cw::rc_t cw::midi_detect::piano::create( handle_t& hRef, unsigned allocDetN, unsigned pedal_thresh )
{
  rc_t rc;
  piano_det_t* p = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    goto errLabel;

  p = mem::allocZ<piano_det_t>();

  p->pedal_thresh = pedal_thresh;
  p->allocDetN    = allocDetN;
  p->detA         = mem::allocZ<detector_t>(allocDetN);
  p->detN         = 0;
  p->keyM         = mem::allocZ<unsigned>( midi::kMidiNoteCnt * midi::kMidiChCnt );
  p->ctlM         = mem::allocZ<unsigned>( midi::kMidiCtlCnt  * midi::kMidiChCnt );
  hRef.set(p);
errLabel:
  return rc;  
}

cw::rc_t cw::midi_detect::piano::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  piano_det_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  mem::release(p);
  hRef.clear();
  return rc;
}


cw::rc_t cw::midi_detect::piano::setup_detector( handle_t h, const state_t* stateA, unsigned stateN, unsigned& det_id_ref )
{
  rc_t        rc = kOkRC;
  detector_t* d  = nullptr;

  piano_det_t* p = _handleToPtr(h);
  
  if( p->detN >= p->allocDetN )
  {
    rc= cwLogError(kBufTooSmallRC,"Detector setup failed. The internal detector array is full.");
    goto errLabel;
  }

  d = p->detA + p->detN;

  d->stateA = mem::allocZ<state_t>(stateN);
  d->stateN = stateN;

  for(unsigned i=0; i<stateN; ++i)
  {
    if( stateA[i].ch >= midi::kMidiChCnt )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI ch %i is not a valid MIDI channel. The MIDI channel value must be less than %i",stateA[i].ch,midi::kMidiChCnt);
      goto errLabel;
    }
    
    if( stateA[i].status != midi::kNoteOnMdId && stateA[i].status != midi::kCtlMdId )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI status 0x%x is not a valid value. The status byte must be either 'note-on' (144) or 'ctl-ch' (176).", stateA[i].status );
      goto errLabel;
    }
    
    if( stateA[i].d0 >= 128 )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI value %i is not a valid MIDI 'd0' value. The value must be less than 128.", stateA[i].d0 );
      goto errLabel;
    }
      
    
    d->stateA[i] = stateA[i];
  }
  
  det_id_ref = p->detN;
  p->detN += 1;

errLabel:
  return rc;
}


cw::rc_t cw::midi_detect::piano::reset( handle_t h )
{
  piano_det_t* p = _handleToPtr(h);

  memset(p->keyM,0,sizeof(p->keyM[0])*midi::kMidiChCnt*midi::kMidiNoteCnt);
  memset(p->ctlM,0,sizeof(p->ctlM[0])*midi::kMidiChCnt*midi::kMidiNoteCnt);

  return kOkRC;
}

cw::rc_t cw::midi_detect::piano::on_midi( handle_t h, const midi::ch_msg_t* msgA, unsigned msgN  )
{
  rc_t rc = kOkRC;
  piano_det_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<msgN; ++i)
  {
    const midi::ch_msg_t* m = msgA + i;
    
    switch( m->status )
    {
      case midi::kNoteOnMdId:
        if( m->ch < midi::kMidiChCnt && m->d0 < midi::kMidiCtlCnt )
          p->keyM[(m->ch * midi::kMidiNoteCnt) + m->d0] = m->d1;
        break;
        
      case midi::kNoteOffMdId:
        p->keyM[(m->ch * midi::kMidiNoteCnt) + m->d0] = 0;
        break;
        
      case midi::kCtlMdId:
        p->ctlM[(m->ch * midi::kMidiCtlCnt) + m->d0] = m->d1;
        break;
        
      default:
        break;          
    }
  }
  
  return rc;
}

cw::rc_t cw::midi_detect::piano::is_any_state_matched( handle_t h, unsigned det_id, bool& match_fl_ref )
{
  rc_t rc = kOkRC;
  piano_det_t* p = _handleToPtr(h);
  detector_t* d = nullptr;

  match_fl_ref = false;
  
  if( det_id == kInvalidId || det_id >= p->detN )
  {
    rc = cwLogError(kInvalidArgRC,"The MIDI piano detector id %s is invalid.",det_id);
    goto errLabel;
  }

  d = p->detA + det_id;

  for(unsigned i=0; i<d->stateN; ++i)
    if( _is_state_matched(p,d->stateA + i) )
    {
      match_fl_ref = true;
      break;
    }
  
errLabel:
  return rc;
}

//==================================================================================================================================
//
//

namespace cw {

  namespace midi_detect {
    
    namespace seq {

      typedef struct event_str
      {
        state_t state;
        bool    match_fl;
      } event_t;

      typedef struct detector_str
      {
        event_t*  eventA;
        unsigned  eventN;
        unsigned* order_cntA;
        unsigned  order_cntN;
        unsigned* match_order_cntA;
      } detector_t;

      typedef struct seq_det_str
      {
        detector_t* detA;
        unsigned    detN;
        unsigned    allocDetN;
        unsigned    pedal_thresh;
        unsigned    armed_det_idx;      // Index of currently armed (active) detector or kInvalidIdx if no detector is active
        unsigned    last_match_order;   // Last matched order of the currently active detector or kInvalidId if the currently active detector has not had it's first match.
        
      } seq_det_t;
      
      seq_det_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,seq_det_t>(h); }

      rc_t _destroy( seq_det_t* p )
      {
        if( p != nullptr )
        {
          for(unsigned i=0; i<p->detN; ++i)
          {
            mem::release(p->detA[i].eventA);
          }
          mem::release(p->detA);
        }
        
        return kOkRC;
      }


      bool _is_detector_triggered( seq_det_t* p )
      {
        if( p->armed_det_idx == kInvalidIdx )
          return false;

        assert( p->armed_det_idx < p->detN );
        
        const detector_t* d = p->detA + p->armed_det_idx;
        
        return p->last_match_order != kInvalidId &&
          p->last_match_order == d->order_cntN-1 &&
          d->match_order_cntA[ p->last_match_order ] == d->order_cntA[ p->last_match_order ];    
      }
      
      void _detector_match_reset(seq_det_t* p, detector_t* d )
      {
        for(unsigned i=0; i<d->eventN; ++i)
          d->eventA[i].match_fl = false;
        
        for(unsigned i=0; i<d->order_cntN; ++i)
          d->match_order_cntA[i] = 0;

        p->last_match_order = kInvalidId;
      }


      // Return true if the ch,status,d0 and release_fl of the MIDI msg match this state
      bool _does_midi_match_state( const seq_det_t* p, const midi::ch_msg_t* m, const state_t& s )
      {
        bool match_fl = false;
        
        // if the ch, status and d0 match 
        if( s.ch == m->ch && s.status == m->status && s.d0 == m->d0 )
        {
          bool match_fl = false;
          switch( m->status )
          {
            case midi::kNoteOnMdId:
              match_fl = s.release_fl == (m->d1 == 0);
              break;
              
            case midi::kNoteOffMdId:
              match_fl = s.release_fl == true;
              break;
              
            case midi::kCtlMdId:
              match_fl = s.release_fl == (m->d1 < p->pedal_thresh);
              break;
          }
        }

        return match_fl;
      }

      bool _does_order_match( const seq_det_t* p, const detector_t* d, const event_t* e )
      {
        rc_t rc = kOkRC;

        bool match_fl = true;
        const state_t& s = e->state;

        if( s.order == kInvalidId )
        {
          // The match is successful, however, 
          // if s.order == kInvalidId then we don't test the order
          // and it doesn't contribute to the match count, but it not a mismatch
          goto errLabel;
        }

        assert( s.order < d->order_cntN );

        // if this is the first match then s.order must be 0
        if( p->last_match_order == kInvalidId && s.order != 0 )
        {
          match_fl = false;
        }

        // If the last match order is the same as this match order then verify that the count has not already been satisfied for this order
        if( p->last_match_order == s.order && d->match_order_cntA[ s.order  ] >= d->order_cntA[ s.order ] )
        {
          match_fl = false;
        }

        // if the match order is advancing then verify that the previous order is complete
        if( p->last_match_order+1 == s.order && d->match_order_cntA[ p->last_match_order  ] != d->order_cntA[ p->last_match_order ] )
        {
          match_fl = false;
        }

        
      errLabel:

        return match_fl;
      }

    }
  }
}


cw::rc_t cw::midi_detect::seq::create( handle_t& hRef, unsigned allocDetN, unsigned pedal_thresh )
{
  rc_t rc;
  seq_det_t* p = nullptr;
  if((rc = destroy(hRef)) != kOkRC )
    goto errLabel;

  p                   = mem::allocZ<seq_det_t>();
  p->allocDetN        = allocDetN;
  p->detA             = mem::allocZ<detector_t>(allocDetN);
  p->armed_det_idx    = kInvalidIdx;
  p->pedal_thresh     = pedal_thresh;
  p->last_match_order = kInvalidId;

  hRef.set(p);
  
errLabel:
  return rc;
}

cw::rc_t cw::midi_detect::seq::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return kOkRC;

  seq_det_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  mem::release(p);
  hRef.clear();
  return rc;  
}

cw::rc_t cw::midi_detect::seq::setup_detector( handle_t h, const state_t* stateA, unsigned stateN, unsigned& det_id_ref )
{
  rc_t        rc        = kOkRC;
  detector_t* d         = nullptr;
  seq_det_t*  p         = _handleToPtr(h);
  unsigned    max_order = 0;
  unsigned    prv_order = kInvalidIdx;
  
  if( p->detN >= p->allocDetN )
  {
    rc= cwLogError(kBufTooSmallRC,"Detector setup failed. The internal detector array is full.");
    goto errLabel;
  }

  d = p->detA + p->detN;

  d->eventA = mem::allocZ<event_t>(stateN);
  d->eventN = stateN;

  for(unsigned i=0; i<stateN; ++i)
  {
    if( stateA[i].ch >= midi::kMidiChCnt )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI ch %i is not a valid MIDI channel. The MIDI channel value must be less than %i",stateA[i].ch,midi::kMidiChCnt);
      goto errLabel;
    }
    
    if( stateA[i].status != midi::kNoteOnMdId && stateA[i].status != midi::kCtlMdId )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI status 0x%x is not a valid value. The status byte must be either 'note-on' (144) or 'ctl-ch' (176).", stateA[i].status );
      goto errLabel;
    }
    
    if( stateA[i].d0 >= 128 )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI value %i is not a valid MIDI 'd0' value. The value must be less than 128.", stateA[i].d0 );
      goto errLabel;
    }


    //
    // verify that state[].order is kInvalidId, equal to the previous valid order, or one greater than the previous valid order
    //

    if( stateA[i].order != kInvalidId )
    {
      // if prv_order has not been set and state order is valid then state order must be 0
      if( prv_order == kInvalidId && stateA[i].order != 0  )
      {
        if( stateA[i].order != 0 )
        {
          rc = cwLogError(kInvalidArgRC,"The first valid state order must be 0.");
          goto errLabel;
        }
      }
      else // the prev order has been set ...
      {
        // ... so the state order must equal prv_order or advance prv_order by 1
        if( stateA[i].order != prv_order && stateA[i].order != prv_order+1 )
        {
          rc = cwLogError(kInvalidArgRC,"The state order (%i) must be the same or one greater then the previous state order (%i).",stateA[i].order,prv_order);
          goto errLabel;
        }
      }

      prv_order = stateA[i].order;

      if( stateA[i].order > max_order )
        max_order = stateA[i].order;
      
    }
    
    d->eventA[i].state = stateA[i];
    d->eventA[i].match_fl = false;
    
  }

  d->order_cntA       = mem::allocZ<unsigned>( max_order );
  d->match_order_cntA = mem::allocZ<unsigned>( max_order );
  d->order_cntN       = max_order;

  for(unsigned i=0; i<stateN; ++i)
    if( stateA[i].order != kInvalidId )
      d->order_cntA[ stateA[i].order ] += 1;
  
  det_id_ref = p->detN;
  p->detN += 1;

errLabel:
  return rc;
}

cw::rc_t  cw::midi_detect::seq::reset( handle_t h)
{
  seq_det_t* p = _handleToPtr(h);
  p->armed_det_idx = kInvalidIdx;
  p->last_match_order = kInvalidId;
  return kOkRC;
}

cw::rc_t cw::midi_detect::seq::on_midi( handle_t h, const midi::ch_msg_t* msgA, unsigned msgN )
{
  rc_t        rc = kOkRC;
  detector_t* d  = nullptr;
  seq_det_t*  p  = _handleToPtr(h);

  if( p->armed_det_idx == kInvalidIdx )
    return kOkRC;
  
  d = p->detA + p->armed_det_idx;

  // attempt to match each incoming midi msg ..
  for(unsigned i=0; i<msgN; ++i)
  {
    const midi::ch_msg_t* m = msgA + i;
    bool match_fl = false;
    
    // ... to one of the events
    for(unsigned j=0; j<d->eventN; ++j)
    {
      event_t*       e = d->eventA + j;
      const state_t& s = e->state;

      // if this midi matches state[i]
      if( _does_midi_match_state( p, m, s ) )
      {
        // check that the order matches
        if( _does_order_match( p, d, e )  )
        {
          match_fl = true;

          // if order matched and the state specifies a valid order ...
          if( s.order != kInvalidId )
          {
            assert( s.order < d->order_cntN );

            // ... then advance the match state
            e->match_fl                     = true;
            d->match_order_cntA[ s.order ] += 1;
            p->last_match_order             = s.order;

            
          }
          
        }
      }
    }

    // if a match was underway but this MIDI msg did not match ... 
    if( p->last_match_order != kInvalidId && match_fl==false  )
    {
      // ... then the whole match process must begin again
      _detector_match_reset(p,d);
    }
    
  }

  
  return rc;
}

cw::rc_t cw::midi_detect::seq::arm_detector( handle_t h, unsigned det_id )
{
  rc_t rc = kOkRC;
  
  seq_det_t*  p  = _handleToPtr(h);

  if( det_id >= p->detN )
  {
    rc = cwLogError(kInvalidArgRC,"The detector id '%i' is out of range.",det_id);
    goto errLabel;
  }
  
  p->armed_det_idx    = det_id;

  assert(det_id < p->detN);
  
  _detector_match_reset(p,p->detA + det_id);

errLabel:
  return rc;
}


bool cw::midi_detect::seq::is_detector_triggered( handle_t h )
{
  seq_det_t* p = _handleToPtr(h);
  return _is_detector_triggered(p);  
}


cw::rc_t cw::midi_detect::seq::test( const object_t* cfg )
{
  rc_t rc = kOkRC;
  
  return rc;
}
