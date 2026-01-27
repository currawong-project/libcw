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

#include "cwPianoScore.h"

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
        event_t*  seqEventA;
        unsigned  seqEventN;

        unsigned        pno_det_id;
        
        unsigned* order_cntA;
        unsigned  order_cntN;
        unsigned* match_order_cntA;

        bool        seq_trig_fl;        // set if the sequence detector triggered
        bool        pno_trig_fl;        // set if the pno detector triggered.
        
      } detector_t;

      typedef struct seq_det_str
      {
        detector_t* detA;
        unsigned    detN;
        unsigned    allocDetN;
        unsigned    pedal_thresh;
        unsigned    armed_det_idx;      // Index of currently armed (active) detector or kInvalidIdx if no detector is active
        unsigned    last_match_order;   // Last matched order of the currently active detector or kInvalidId if the currently active detector has not had it's first match.

        piano::handle_t pnoDetH;
        
      } seq_det_t;
      
      seq_det_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,seq_det_t>(h); }

      rc_t _destroy( seq_det_t* p )
      {
        if( p != nullptr )
        {
          for(unsigned i=0; i<p->detN; ++i)
          {
            mem::release(p->detA[i].seqEventA);
            mem::release(p->detA[i].order_cntA);
            mem::release(p->detA[i].match_order_cntA);
          }
          mem::release(p->detA);

          destroy(p->pnoDetH);
          
        }
        
        return kOkRC;
      }


      rc_t _is_detector_triggered( seq_det_t* p, bool& trig_fl_ref )
      {
        rc_t rc = kOkRC;
        
        trig_fl_ref = false;

        // if the detector is not armed there is nothing to do
        if( p->armed_det_idx == kInvalidIdx )
          return kOkRC;

        assert( p->armed_det_idx < p->detN );

        // get the armed detector
        detector_t* d = p->detA + p->armed_det_idx;

        // if the sequence detector has been triggered
        if( d->seq_trig_fl )
        {
          
          // if the piano detector has already triggered or there is no piano detector 
          if( d->pno_trig_fl || d->pno_det_id == kInvalidId )
            d->pno_trig_fl = true;
          else
          {
            // check the piano trigger
            if((rc = is_any_state_matched( p->pnoDetH, d->pno_det_id, d->pno_trig_fl )) != kOkRC )
            {
              rc = cwLogError(rc,"Internal piano match state access failed.");
              goto errLabel;
            }
          }
        }

        trig_fl_ref = d->pno_trig_fl;
        
      errLabel:
        return rc;
      }
      
      void _detector_match_reset(seq_det_t* p, detector_t* d )
      {
        for(unsigned i=0; i<d->seqEventN; ++i)
          d->seqEventA[i].match_fl = false;
        
        for(unsigned i=0; i<d->order_cntN; ++i)
          d->match_order_cntA[i] = 0;
        
        d->seq_trig_fl = false;
        d->pno_trig_fl = false;
          
        p->last_match_order = kInvalidId;
        
      }


      // Return true if the ch,status,d0 and release_fl of the MIDI msg match this state
      bool _does_midi_match_state( const seq_det_t* p, const midi::ch_msg_t* m, const state_t& s )
      {
        return s.ch == m->ch && s.d0 == m->d0;
      }

      bool _does_order_match( const seq_det_t* p, const detector_t* d, const event_t* e )
      {
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

        if( p->last_match_order != kInvalidId )
        {
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
        }
        
        
      errLabel:

        return match_fl;
      }

      bool _update_note_on_match_state( seq_det_t* p, detector_t* d, const midi::ch_msg_t* m )
      {
        bool match_fl = false;

        for(unsigned j=0; j<d->seqEventN; ++j)
        {
          event_t*       e = d->seqEventA + j;
          const state_t& s = e->state;

          // if no seq match has been started then the first match must be order 0 or less.
          if( p->last_match_order == kInvalidId && s.order != kInvalidId && s.order>0 )
            break;

          // if a seq match has started then the match must be made to the current order or the current order + 1
          if( p->last_match_order != kInvalidId && s.order != kInvalidId && s.order > p->last_match_order+1 )
            break;

          // if this pattern event was not already matched
          if(!e->match_fl)
          {
            // if this midi matches state[i]
            if( !_does_midi_match_state( p, m, s ) )
              continue;

            //printf("st:%i : %i %i\n",m->d0,s.order,p->last_match_order);
            
            // check that the order matches
            if( !_does_order_match( p, d, e )  )
              continue;

            //printf("od:%i : %i %i\n",m->d0,s.order,p->last_match_order);
            
            match_fl = true;

            // if order matched and the state specifies a valid order ...
            if( s.order != kInvalidId )
            {
              assert( s.order < d->order_cntN );
              
              // ... then advance the match state
              e->match_fl                     = true;
              d->match_order_cntA[ s.order ] += 1;
              p->last_match_order             = s.order;

              d->seq_trig_fl = (p->last_match_order == d->order_cntN-1) && (d->match_order_cntA[ p->last_match_order ] == d->order_cntA[ p->last_match_order ]);    
              
            }

            printf("Match: %i : o:%i d0:%i lmo:%i : trig_fl:%i\n",m->uid, s.order, s.d0,p->last_match_order,d->seq_trig_fl);
            break;
          }
        }

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

  if((rc = create(p->pnoDetH,allocDetN,p->pedal_thresh)) != kOkRC )
  {
    rc = cwLogError(rc,"The internal piano detector create failed.");
    goto errLabel;
  }

  for(unsigned i=0; i<allocDetN; ++i)
    p->detA[i].pno_det_id = kInvalidId;
  

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

cw::rc_t cw::midi_detect::seq::setup_detector( handle_t       h,
                                               const state_t* seqStateA,
                                               unsigned       seqStateN,
                                               const state_t* pnoStateA,
                                               unsigned       pnoStateN,
                                               unsigned&      det_id_ref )
{
  rc_t        rc        = kOkRC;
  detector_t* d         = nullptr;
  seq_det_t*  p         = _handleToPtr(h);
  unsigned    max_order = 0;
  unsigned    prv_order = kInvalidId;
  
  if( p->detN >= p->allocDetN )
  {
    rc= cwLogError(kBufTooSmallRC,"Detector setup failed. The internal detector array is full.");
    goto errLabel;
  }

  d = p->detA + p->detN;

  d->seqEventA = mem::allocZ<event_t>(seqStateN);
  d->seqEventN = seqStateN;

  // validate and store each of the seqStateA[] parameter records 
  for(unsigned i=0; i<seqStateN; ++i)
  {
    if( seqStateA[i].ch >= midi::kMidiChCnt )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI ch %i is not a valid MIDI channel. The MIDI channel value must be less than %i",seqStateA[i].ch,midi::kMidiChCnt);
      goto errLabel;
    }
    
    if( seqStateA[i].status != midi::kNoteOnMdId && seqStateA[i].status != midi::kCtlMdId )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI status 0x%x is not a valid value. The status byte must be either 'note-on' (144) or 'ctl-ch' (176).", seqStateA[i].status );
      goto errLabel;
    }
    
    if( seqStateA[i].d0 >= 128 )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI value %i is not a valid MIDI 'd0' value. The value must be less than 128.", seqStateA[i].d0 );
      goto errLabel;
    }

    // track 'max_order'
    if( seqStateA[i].order != kInvalidId && seqStateA[i].order > max_order )
      max_order = seqStateA[i].order;


    d->seqEventA[i].state = seqStateA[i];
    d->seqEventA[i].match_fl = false;
  }

  // sort seqEventA[] on increasing 'order' - with order==kInvalidId ordered first
  std::sort( d->seqEventA, d->seqEventA+d->seqEventN, [](auto a, auto b){return a.state.order==kInvalidId?true:(b.state.order==kInvalidId?false:(a.state.order<b.state.order)); });

  // verify that the state 'order' values are sequental, beginning with kInvalid, then 0, ...
  for(unsigned i=0; i<d->seqEventN; ++i)
  {
    const auto e = d->seqEventA + i;
    
    if( prv_order == kInvalidId )
    {
      if( e->state.order != kInvalidId && e->state.order != 0 )
      {
        rc = cwLogError(kInvalidArgRC,"The sequence orders must be set to kInvalidId or a set of sequential integers beginning with 0.");
        goto errLabel;
      }
    }
    else
    {
      if( e->state.order != prv_order && e->state.order != prv_order+1 )
      {
        rc = cwLogError(kInvalidArgRC,"The sequence orders must be sequential and increment by 0 or 1.");
        goto errLabel;        
      }
    }

    prv_order = e->state.order;
  }

  // if a piano detector was defined for this detector
  if( pnoStateA != nullptr and pnoStateN > 0 )
  {
    if((rc = setup_detector(p->pnoDetH,pnoStateA,pnoStateN,d->pno_det_id)) != kOkRC )
    {
      rc = cwLogError(rc,"The internal piano detecotr setup failed.");
      goto errLabel;
    }    
  }
    
  d->order_cntN       = max_order + 1;
  d->order_cntA       = mem::allocZ<unsigned>( d->order_cntN );
  d->match_order_cntA = mem::allocZ<unsigned>( d->order_cntN );

  for(unsigned i=0; i<seqStateN; ++i)
    if( seqStateA[i].order != kInvalidId )
      d->order_cntA[ seqStateA[i].order ] += 1;
  
  det_id_ref = p->detN;
  p->detN += 1;

errLabel:
  return rc;
}

cw::rc_t  cw::midi_detect::seq::reset( handle_t h )
{
  seq_det_t* p = _handleToPtr(h);
  p->armed_det_idx = kInvalidIdx;
  p->last_match_order = kInvalidId;
  
  reset(p->pnoDetH);
  
  return kOkRC;
}


cw::rc_t cw::midi_detect::seq::on_midi( handle_t h, const midi::ch_msg_t* msgA, unsigned msgN )
{
  rc_t        rc = kOkRC;
  detector_t* d  = nullptr;
  seq_det_t*  p  = _handleToPtr(h);

  // pass incoming MIDI to the piano detector
  on_midi(p->pnoDetH,msgA,msgN);

  if( p->armed_det_idx == kInvalidIdx )
    return kOkRC;

  // get the currently armed detector
  d = p->detA + p->armed_det_idx;

  // if the sequence detector has already triggered there is nothing to do
  if( d->seq_trig_fl )
    return kOkRC;

  // attempt to match each incoming midi msg ..
  for(unsigned i=0; i<msgN; ++i)
  {
    const midi::ch_msg_t* m = msgA + i;

    // we only handle 
    if( !midi::isNoteOn(m->status,m->d1) )
      continue;

    bool match_fl = _update_note_on_match_state( p, d, m );

    // if a match was underway but this MIDI msg did not match ... 
    if( p->last_match_order != kInvalidId && match_fl==false  )
    {
      //printf("clr\n");
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
  
  p->armed_det_idx = det_id;

  assert(det_id < p->detN);
  
  _detector_match_reset(p,p->detA + det_id);

errLabel:
  return rc;
}


bool cw::midi_detect::seq::is_detector_triggered( handle_t h )
{
  seq_det_t* p = _handleToPtr(h);
  bool trig_fl = false;
  _is_detector_triggered(p,trig_fl);
  return trig_fl;
}


cw::rc_t cw::midi_detect::test( const object_t* cfg )
{
  rc_t                       rc           = kOkRC;
  unsigned                   allocDetN    = 1;
  unsigned                   pedal_thresh = 30;
  const char*                score_fname  = nullptr;
  unsigned                   beg_loc      = 0;
  unsigned                   end_loc      = 0;
  const object_t*            seqStateL    = nullptr;
  const object_t*            pnoStateL    = nullptr;
  seq::handle_t              detH;
  unsigned                   det_id       = kInvalidId;
  perf_score::handle_t       pianoScoreH;
  const perf_score::event_t* evt             = nullptr;
  unsigned                   seqStateN       = 0;
  midi_detect::state_t*      seqStateA       = nullptr;
  unsigned                   pnoStateN       = 0;
  midi_detect::state_t*      pnoStateA       = nullptr;
  unsigned                   detect_cnt      = 0;
  
  if((rc = create( detH, allocDetN, pedal_thresh )) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI sequence detector test create failed.");
    goto errLabel;
  }

  if((rc = cfg->getv("score_fname",score_fname,
                     "beg_loc",beg_loc,
                     "end_loc",end_loc,
                     "seq_det_stateL",seqStateL,
                     "pno_det_stateL",pnoStateL)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI sequence detector test parameter record parse failed.");
    goto errLabel;
  }

  seqStateN = seqStateL->child_count();
  seqStateA = mem::allocZ<midi_detect::state_t>(seqStateN);
  
  for(unsigned i=0; i<seqStateN; ++i)
  {
    const object_t* evt_cfg = seqStateL->child_ele(i);
    auto s = seqStateA + i;
    if((rc = evt_cfg->getv("order",s->order,
                           "ch",s->ch,
                           "status",s->status,
                           "d0",s->d0 )) != kOkRC )
    {
      rc = cwLogError(rc,"MIDI sequence detector event pattern parsing failed on event index %i.",i);
      goto errLabel;
    }

    printf("order:%i ch:%i status:%i d0:%i rls:%i\n",s->order,s->ch,s->status,s->d0,s->release_fl);
  }

  pnoStateN = pnoStateL->child_count();
  pnoStateA = mem::allocZ<midi_detect::state_t>(pnoStateN);
  
  for(unsigned i=0; i<pnoStateN; ++i)
  {
    const object_t* evt_cfg = pnoStateL->child_ele(i);
    auto s = pnoStateA + i;
    if((rc = evt_cfg->getv("ch",s->ch,
                           "status",s->status,
                           "d0",s->d0,
                           "release_fl",s->release_fl)) != kOkRC )
    {
      rc = cwLogError(rc,"MIDI sequence detector event pattern parsing failed on event index %i.",i);
      goto errLabel;
    }

    printf("order:%i ch:%i status:%i d0:%i rls:%i\n",s->order,s->ch,s->status,s->d0,s->release_fl);
  }
  
  if((rc = setup_detector(detH,seqStateA,seqStateN,pnoStateA,pnoStateN,det_id)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI seq. detector setup failed failed.");
    goto errLabel;
  }

  if((rc = create(pianoScoreH,score_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI seq. test score create failed on '%s'.",cwStringNullGuard(score_fname));
    goto errLabel;
  }

  if((evt = loc_to_event(pianoScoreH,beg_loc)) == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The MIDI seq. test score loc: %i could not be found.",beg_loc);
    goto errLabel;
  }

  if((rc = arm_detector(detH,det_id)) != kOkRC )
  {
    rc = cwLogError(kInvalidArgRC,"The MIDI seq. arm failed.");
    goto errLabel;
  }
  
  for(unsigned i=0; evt!=nullptr && evt->loc != end_loc; evt=evt->link,++i)
  {
    midi::ch_msg_t m = {};

    m.uid    = evt->uid;
    m.ch     = evt->status & 0x0f;
    m.status = evt->status & 0xf0;
    m.d0     = evt->d0;
    m.d1     = evt->d1;
        
    if((rc = on_midi(detH,&m,1)) != kOkRC )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI seq. detector failed during MIDI event handling on event index %i.",i);
      goto errLabel;
    }

    if( is_detector_triggered(detH) )
    {
      cwLogInfo("PATTERN MATCHED: %i",i);
      reset(detH);
      detect_cnt += 1;
    }
        
  }

errLabel:
  if((rc = destroy(pianoScoreH)) != kOkRC )
  {
    cwLogError(rc,"Score destroy failed MIDI seq detector test.");
  }
  
  if((rc = destroy(detH)) != kOkRC )
  {
    cwLogError(rc,"MIDI seq detector test failed.");
  }

  mem::release(seqStateA);
  mem::release(pnoStateA);

  cwLogInfo("MIDI seq detector : detect count: %i",detect_cnt);
  
  return rc;
}
