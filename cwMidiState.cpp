#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwMidi.h"
#include "cwMidiState.h"
#include "cwMidiFile.h"

namespace cw
{
  namespace midi_state
  {
    enum {
      kUpPedalStateId,
      kHalfPedalStateId,
      kDownPedalStateId
    };

    enum {
      kSustainPedalIdx,
      kSostenutoPedalIdx,
      kSoftPedalIdx,
      kPedalCnt,
    };
    
    typedef struct msg_cache_str
    {
      unsigned               msgN;
      msg_t*                 msgA;
      unsigned               next_idx;
      struct msg_cache_str* link;
    } msg_cache_t;

    typedef struct event_cache_str
    {
      unsigned                eventN;
      event_t*                eventA;
      unsigned                next_idx;
      struct event_cache_str* link;
    } event_cache_t;

    typedef struct event_chain_str
    {
      event_t*                begEvt; 
      event_t*                endEvt;
      event_t*                iter;
    } event_chain_t;

    typedef struct note_state_str
    {
      bool noteGateFl; // true if the note gate is on
      bool sndGateFl;  // true if the note is sounding
      bool sostHoldFl; // true if this note is being held by the sost. pedal
    } note_state_t;

    typedef struct ch_state_str
    {
      unsigned     chIdx;      // this channels MIDI ch index
      bool         sostFl;     // true if the sost. pedal is down
      bool         softFl;     // true if the soft pedal is down
      unsigned     dampState;  // current sustain pedal state: kUp/kHalf/kDown - PedalStateId
      note_state_t noteState[ midi::kMidiNoteCnt ]; // current note state
    } ch_state_t;
    
    typedef struct midi_state_str
    {
      config_t   cfg;
      
      callback_t cbFunc;
      void*      cbArg;
      
      msg_cache_t*   beg_msg_cache;
      msg_cache_t*   end_msg_cache;

      event_cache_t*  beg_event_cache;
      event_cache_t*  end_event_cache;
    
      ch_state_t      chState[ midi::kMidiChCnt ]; 

      event_chain_t   noteChains[  midi::kMidiChCnt * midi::kMidiNoteCnt ];
      event_chain_t   pedalChains[ midi::kMidiChCnt * kPedalCnt ];

      event_t* first_event;
    } midi_state_t;
     
    typedef struct map_str
    {
      unsigned index;
      unsigned midiId;
    } map_t;

    map_t map[] = {
      { kSustainPedalIdx,   midi::kSustainCtlMdId },
      { kSostenutoPedalIdx, midi::kSostenutoCtlMdId },
      { kSoftPedalIdx,      midi::kSoftPedalCtlMdId },
      { kInvalidIdx,        kInvalidIdx }
    };

    typedef struct flag_map_str
    {
      unsigned    flag;
      const char* label;
    } flag_map_t;

    flag_map_t flag_map[] = {
      { kNoteOnFl,   "Non"},
      { kNoteOffFl,  "Nof"},
      { kSoundOnFl,  "Son"},
      { kSoundOffFl, "Sof"},
      { kReattackFl, "Rat"},
      { kSoftPedalFl,"Sft"},
      { kUpPedalFl,  "PUp"},
      { kHalfPedalFl,"PHf"},
      { kDownPedalFl,"PDn"},
      { kNoChangeFl, "---"},
      { kPedalEvtFl, "EPd"},
      { kNoteEvtFl,  "ENt"},
      { kMarkerEvtFl,"EMk"},
      { 0, nullptr }
    };

    const char* _flagToLabel( unsigned flag )
    {
      for(unsigned i=0; flag_map[i].label!=nullptr; ++i)
        if( flag_map[i].flag == flag )
          return flag_map[i].label;
      return "???";      
    }

    unsigned _pedalMidiToIndex( uint8_t ctlId )
    {
      for(unsigned i=0; map[i].index!=kInvalidIdx; ++i)
        if( ctlId == map[i].midiId )
          return map[i].index;

      return kInvalidIdx;
    }

    unsigned _pedalIndexToMidi( uint8_t index )
    {
      for(unsigned i=0; map[i].index!=kInvalidIdx; ++i)
        if( index == map[i].index )
          return map[i].midiId;

      cwLogError(kInvalidArgRC,"Invalid pedal index:%i",index);
      return kInvalidIdx;
    }
      

    midi_state_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,midi_state_t>(h); }

    event_chain_t* _note_event_chain( midi_state_t* p, uint8_t ch, uint8_t pitch )
    {
      cwAssert( ch < midi::kMidiChCnt && pitch < midi::kMidiNoteCnt );
      return p->noteChains + (ch * midi::kMidiNoteCnt + pitch );
    }

    event_chain_t* _pedal_event_chain_from_pedal_idx( midi_state_t* p, uint8_t ch, unsigned pedal_idx )
    {
      if( pedal_idx >= kPedalCnt )
        return nullptr;
      
      return p->pedalChains + (ch * kPedalCnt + pedal_idx);
    }
    
    event_chain_t* _pedal_event_chain_from_midi_ctl_id( midi_state_t* p, uint8_t ch, uint8_t d0 )
    {
      return _pedal_event_chain_from_pedal_idx(p,ch,_pedalMidiToIndex(d0));
    }

    int _format_marker( const marker_msg_t* m, char* buf, unsigned bufCharN )
    {
      return snprintf(buf,bufCharN,"%5i %3i %5i %5i ",m->uid,m->ch,m->typeId,m->value);
    }

    int _format_midi_msg( const midi_msg_t* m, char* buf, unsigned bufCharN )
    {
      return snprintf(buf,bufCharN," %5i %3i 0x%2x %3i %3i ",m->uid,m->ch,m->status,m->d0, m->d1);
    }
    
    rc_t _format_event( const event_t* e, char *buf, unsigned bufCharN )
    {
      rc_t rc = kOkRC;      
      int n = snprintf(buf,bufCharN,"%7.3f ",e->secs);
      assert( n<=(int)bufCharN);
      buf += n;
      bufCharN -= n;

      if( e->msg != nullptr )
      {
        if( cwIsFlag(e->flags,kMarkerEvtFl) )
        {
          n = _format_marker(&e->msg->u.marker,buf,bufCharN);
          assert( n<=(int)bufCharN );
          buf += n;
          bufCharN -= n;
        }
      
        if( cwIsFlag(e->flags,kNoteEvtFl|kPedalEvtFl) )
        {
          n = _format_midi_msg(&e->msg->u.midi,buf,bufCharN);
          assert( n<=(int)bufCharN );
          buf += n;
          bufCharN -= n;
        }
      }
      
      if( bufCharN < flags_to_string_max_string_length() )
        rc = cwLogError(kBufTooSmallRC,"The event char buf is too small.");
      else
        rc = flags_to_string( e->flags, buf, bufCharN );

      if( rc != kOkRC )
        rc = cwLogError(rc,"Event format failed.");
      
      return rc;
    }

    
    // Rewind the chain iterator.
    void _rewind_chain_iterator( midi_state_t* p )
    {
      for(unsigned i=0; i<midi::kMidiChCnt; ++i)
        for(unsigned j=0; j<midi::kMidiNoteCnt; ++j)
        {
          event_chain_t* ec = _note_event_chain(p,i,j);
          ec->iter = ec->begEvt;
        }
       
      for(unsigned i=0; i<midi::kMidiChCnt; ++i)
        for(unsigned j=0; j<kPedalCnt; ++j)
        {
          event_chain_t* ec = _pedal_event_chain_from_pedal_idx(p,i,j);
          ec->iter = ec->begEvt;
        }
    }

    // Given a list events linked in time order return the event that
    // is equal to or minimally greater than 'sec'.
    // Return nullptr if all events are less than 'sec'.
    event_t* _goto_event_greater_than_or_equal( event_t* e, double sec  )
    {
      if( e == nullptr )
        return nullptr;

      // if sec-e->sec > 0 then e->sec is still behind sec
      while( e!=nullptr and sec - e->secs > 0 )
        e = e->link;

      // if e is nullptr then all events in the chain are less than 'sec'.
      
      return  e;
    }
      

    // Set the chain iterators to the event that is equal to, or minimally greater than,  'secs'.
    // For chains which end prior to 'sec'.
    void _seek_chain_iterator( midi_state_t* p, double secs )
    {
      _rewind_chain_iterator(p);
      
      for(unsigned i=0; i<midi::kMidiChCnt; ++i)
      {
        for(unsigned j=0; j<midi::kMidiNoteCnt; ++j)
        {
          event_chain_t* ec = _note_event_chain(p,i,j);
          ec->iter = _goto_event_greater_than_or_equal( ec->begEvt, secs  );
        }
       
        for(unsigned j=0; j<kPedalCnt; ++j)
        {
          event_chain_t* ec = _pedal_event_chain_from_pedal_idx(p,i,j);
          ec->iter = _goto_event_greater_than_or_equal( ec->begEvt, secs);
        }
      }
      
    }

    // use the chain iterators to return the next event in time
    event_t* _step_chain_iterator( midi_state_t* p )
    {
      event_chain_t* ec0 = nullptr;
      event_t*       e   = nullptr;
      
      for(uint8_t i=0; i<midi::kMidiChCnt; ++i)
      {
        for(uint8_t j=0; j<midi::kMidiNoteCnt; ++j)
        {
          event_chain_t* ec = _note_event_chain(p,i,j);
          if( ec->iter != nullptr && (ec0==nullptr || ec->iter->secs < ec0->iter->secs) )
            ec0 = ec;
        }

        for(uint8_t j=0; j<kPedalCnt; ++j)
        {
          event_chain_t* ec = _pedal_event_chain_from_pedal_idx(p,i,j);
          if( ec->iter != nullptr && (ec0==nullptr || ec->iter->secs < ec0->iter->secs) )
            ec0 = ec;
        }
      }

      if( ec0 != nullptr )
      {
        e = ec0->iter;
        ec0->iter = ec0->iter->link;
      }
          
      return e;
    }
    
    unsigned _count_null_tlinks( midi_state_t* p )
    {
      event_cache_t* ec = p->beg_event_cache;
      unsigned n = 0;
      for(; ec!=nullptr; ec=ec->link)
        for(unsigned i=0; i<ec->next_idx; ++i)
          if( ec->eventA[i].tlink == nullptr )
            ++n;

      return n;
    }

    const event_t* _get_first_link( midi_state_t* p )
    {
      // if the first_event has not yet been set ....
      if( p->first_event == nullptr )
      {
        event_t* e0 = nullptr;
        event_t* e1 = nullptr;
        // ... the use the chain iterator to get the time order of the events and set 'tlink'
        _seek_chain_iterator(p, 0.0);

        while((e1 = _step_chain_iterator( p )) != nullptr )
        {
          if( e0 == nullptr )
            p->first_event = e1;
          else
            e0->tlink = e1;
      
          e0 = e1;
        }
      }

      cwAssert( _count_null_tlinks(p) == 1 );

      return p->first_event;
    }
            
    void _reset( midi_state_t* p )
    {
      // TODO: it would be better if once allocated the cache memory
      // was reused on the next session rather than released
      // and reallocated

      p->first_event = nullptr;
      
      // release the MIDI cache
      msg_cache_t* mc = p->beg_msg_cache;
      while( mc != nullptr )
      {
        msg_cache_t* mc0 = mc->link;
        mem::release(mc->msgA);
        mem::release(mc);
        mc = mc0;
      }

      // release the event cache
      event_cache_t* ev = p->beg_event_cache;
      while( ev != nullptr )
      {
        event_cache_t* ev0 = ev->link;
        mem::release(ev->eventA);
        mem::release(ev);
        ev = ev0;
      }

      p->beg_msg_cache  = nullptr;
      p->end_msg_cache  = nullptr;
      p->beg_event_cache = nullptr;
      p->end_event_cache = nullptr;

      for(unsigned i=0; i<midi::kMidiChCnt; ++i)
      {
        for(unsigned j=0; j<midi::kMidiNoteCnt; ++j)
        {
          _note_event_chain(p, i, j )->begEvt = nullptr;
          _note_event_chain(p, i, j )->endEvt = nullptr;
        }

        for(unsigned j=0; j<kPedalCnt; ++j)
        {
          _pedal_event_chain_from_pedal_idx(p, i, j)->begEvt = nullptr;
          _pedal_event_chain_from_pedal_idx(p, i, j)->endEvt = nullptr;          
        }
      }       
    }
    
    rc_t _destroy( midi_state_t* p )
    {
      rc_t rc = kOkRC;

      _reset(p);
      
      mem::release(p);

      return rc;
    }

    msg_t* _fill_midi_msg( msg_t* m, unsigned uid, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      m->typeId = kMidiMsgTId;
      
      m->u.midi.uid    = uid;
      m->u.midi.ch     = ch;
      m->u.midi.status = status;
      m->u.midi.d0     = d0;
      m->u.midi.d1     = d1;
      return m;
    }

    msg_t* _fill_marker_msg( msg_t* m, unsigned uid, uint8_t ch, unsigned markerTypeId, unsigned markerValue )
    {
      m->typeId = kMarkerMsgTId;
      
      m->u.marker.uid    = uid;
      m->u.marker.ch     = ch;
      m->u.marker.typeId = markerTypeId;
      m->u.marker.value  = markerValue;
      return m;
    }
    
    msg_t* _insert_msg( midi_state_t* p)
    {
      if( p->end_msg_cache == nullptr || p->end_msg_cache->next_idx >= p->end_msg_cache->msgN )
      {
        msg_cache_t* mc = mem::allocZ<msg_cache_t>();
        mc->msgN = p->cfg.cacheBlockMsgN;
        mc->msgA = mem::allocZ<msg_t>(mc->msgN);
        if( p->end_msg_cache == nullptr )
          p->beg_msg_cache = mc;
        else
          p->end_msg_cache->link = mc;
        p->end_msg_cache = mc;
      }

      return p->end_msg_cache->msgA + p->end_msg_cache->next_idx;
    }
    
    msg_t* _insert_midi_msg( midi_state_t* p, unsigned uid, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      msg_t* m = _insert_msg(p);
      
      _fill_midi_msg(m, uid, ch, status, d0, d1 );
      
      p->end_msg_cache->next_idx++;

      return m;
    }

    msg_t* _insert_marker_msg( midi_state_t* p, unsigned uid, uint8_t ch, unsigned markerTypeId, unsigned markerValue )
    {
      msg_t* m = _insert_msg(p);
      
      _fill_marker_msg(m, uid, ch, markerTypeId, markerValue );
      
      p->end_msg_cache->next_idx++;

      return m;
    }
    
    event_t* _insert_event( midi_state_t* p, unsigned flags, double secs, const msg_t* m=nullptr )
    {
      if( p->end_event_cache == nullptr || p->end_event_cache->next_idx >= p->end_event_cache->eventN )
      {
        event_cache_t* ec = mem::allocZ<event_cache_t>();
        ec->eventN = p->cfg.cacheBlockMsgN;
        ec->eventA = mem::allocZ<event_t>(ec->eventN);
        if( p->end_event_cache == nullptr )
          p->beg_event_cache = ec;
        else
          p->end_event_cache->link = ec;
        p->end_event_cache = ec;
      }

      event_t* e = p->end_event_cache->eventA + p->end_event_cache->next_idx;
      e->flags = flags;
      e->secs = secs;
      e->msg   = m;
      e->link  = nullptr;
      e->tlink = nullptr;
      
      p->end_event_cache->next_idx++;

      return e;
    }
    

    void _onStateChange( midi_state_t* p, unsigned flags, double secs,  const msg_t* m )
    {
      // notice when a voice is being switched off
      if( cwIsFlag(flags,kSoundOffFl) )
        p->chState[m->u.midi.ch].noteState[m->u.midi.d0].sndGateFl = false;
      
      if( p->cbFunc != nullptr )
        p->cbFunc( p->cbArg, flags, secs, m );

      if( p->cfg.cacheEnableFl )
      {
        cwAssert( cwIsFlag( flags,kPedalEvtFl | kNoteEvtFl | kMarkerEvtFl ) );
        event_t*       e  = _insert_event( p, flags, secs, m );
        event_chain_t* ec = cwIsFlag(flags,kPedalEvtFl) ? _pedal_event_chain_from_midi_ctl_id(p,m->u.midi.ch,m->u.midi.d0) : _note_event_chain(p,m->u.midi.ch,m->u.midi.d0);

        if( ec->begEvt == nullptr )
          ec->begEvt = e;
        else
          ec->endEvt->link = e;
        
        ec->endEvt = e;        
      }
      
    }

    void _onMidiNoteStateChange( midi_state_t* p, unsigned flags, double secs, unsigned uid, unsigned chIdx, uint8_t status, uint8_t pitch, uint8_t vel )
    {
      msg_t msg;
      const msg_t* m = &msg;

      // if the cache is enabled then we need a valid msg_t record which will
      // be stored in the cached event - in _onStateChange() ...
      if( p->cfg.cacheEnableFl )
      {
        event_chain_t* ec = _note_event_chain( p, chIdx, pitch );
        assert( ec != nullptr && ec->endEvt != nullptr && ec->endEvt->msg != nullptr );
        m = ec->endEvt->msg;
      }
      else
      {
        // ... if cache is not enabled then we can pass a msg_t record which will
        // only be valid during the callback
        _fill_midi_msg(&msg, uid, chIdx, status, pitch, vel );
      }
      
      _onStateChange( p, flags, secs, m );

    }
    
        
    void _turn_off_all_released_notes( midi_state_t* p, ch_state_t* c, double sec, unsigned uid )
    {
      // if the sustain pedal is not up then all sounding notes remain sounding
      if( c->dampState == kUpPedalStateId )
      {
        // for each note
        for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
        {
          // if this note is sounding and not being held by the note gate or sostenuto pedal
          if( c->noteState[i].sndGateFl && c->noteState[i].sostHoldFl==false && c->noteState[i].noteGateFl==false )
          {
            // turn sounding note off
            _onMidiNoteStateChange(p,kSoundOffFl | kNoteEvtFl, sec, uid, c->chIdx, midi::kNoteOffMdId, i, 0);
            
          }
        }   
      }      
    }
    
    rc_t _setMidiNoteOnMsg( midi_state_t* p, double sec, const msg_t* m )
    {
      rc_t        rc    = kOkRC;
      unsigned    flags = kSoundOnFl | kNoteOnFl;
      ch_state_t* c     = p->chState + m->u.midi.ch;

      if( c->noteState[ m->u.midi.d0 ].noteGateFl )
        flags |= kReattackFl;

      if( p->chState[ m->u.midi.ch ].softFl )
        flags |= kSoftPedalFl;
      
      c->noteState[ m->u.midi.d0 ].noteGateFl = true;
      c->noteState[ m->u.midi.d0 ].sndGateFl  = true;
      
      _onStateChange( p, flags | kNoteEvtFl, sec, m );
      
      return rc;
    }

    rc_t _setMidiNoteOffMsg( midi_state_t* p, double sec, const msg_t* m )
    {
      rc_t        rc    = kOkRC;
      unsigned    flags = kNoteOffFl;
      ch_state_t* c     = p->chState + m->u.midi.ch;

      if( c->noteState[ m->u.midi.d0 ].noteGateFl == false )
        flags |= kNoChangeFl;

      c->noteState[ m->u.midi.d0 ].noteGateFl = false;

      // if the note is sounding and is not being held on by the sost or damper - then turn it off
      if( c->noteState[m->u.midi.d0].sndGateFl && (c->dampState == kUpPedalStateId && c->noteState[m->u.midi.d0].sostHoldFl==false) )
      {
        // turn off the note
        flags |= kSoundOffFl;
      }
      
      _onStateChange( p, flags | kNoteEvtFl, sec, m );
      
      return rc;      
    }

    rc_t _setMidiSustainMsg( midi_state_t* p, double sec, const msg_t* m )
    {
      rc_t        rc        = kOkRC;
      ch_state_t* c         = p->chState + m->u.midi.ch;
      unsigned    dampState = kDownPedalStateId;
      unsigned    flags     = kDownPedalFl;

      // if the pedal is going up
      if( m->u.midi.d1 < p->cfg.pedalHalfMinMidiValue )
      {
        dampState = kUpPedalStateId;
        flags     = kUpPedalFl;
      }
      else
      {
        // if the pedal is in the half pedal band
        if( m->u.midi.d1 <= p->cfg.pedalHalfMaxMidiValue )
        {
          dampState = kHalfPedalStateId;
          flags     = kHalfPedalFl;
        }
      }

      // if the pedal state is not changing
      if( dampState == c->dampState )
        flags |= kNoChangeFl;
      else
        c->dampState = dampState;

      // update client state
      _onStateChange( p, flags | kPedalEvtFl, sec, m );

      // if the pedal changed state
      if( !cwIsFlag( flags, kNoChangeFl ) )
      {
        // if the pedal went up - then release notes
        if( dampState == kUpPedalStateId )
          _turn_off_all_released_notes(p,c,sec,m->u.midi.uid);
        else
        {
          // if the pedal went into the half pedal range ...
          if(dampState == kHalfPedalStateId )
          {
            // ... then notify all notes that they should enter the half pedal range
            for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
              if( c->noteState[i].sndGateFl )
              {
                _onMidiNoteStateChange( p, flags | kHalfPedalFl | kNoteEvtFl, sec, m->u.midi.uid, m->u.midi.ch, m->u.midi.status, i, 0 );
              }
          }
        }
      }
      
      return rc;
    }
    
    rc_t _setMidiSostenutoMsg( midi_state_t* p, double sec, const msg_t* m )
    {
      rc_t        rc          = kOkRC;
      bool        pedalDownFl = m->u.midi.d1 > p->cfg.pedalUpMidiValue;
      unsigned    flags       = 0;
      ch_state_t* c           = p->chState + m->u.midi.ch;

      // if the sost pedal is not changing state
      if( c->sostFl == pedalDownFl )
        flags |= kNoChangeFl;
      else
        flags = pedalDownFl ? kDownPedalFl : kUpPedalFl;

      // update the sost pedal state
      c->sostFl = pedalDownFl;

      // if the sost pedal changed state
      if( !cwIsFlag(flags, kNoChangeFl ) )
      {
        // update the client state
        _onStateChange( p, flags | kPedalEvtFl, sec, m );
        
        // if the sost pedal went down...
        if( pedalDownFl )
        {
          // ... mark all notes whose note-gate is on to be held until the sost pedal goes up
          for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)         
            c->noteState[i].sostHoldFl = c->noteState[i].noteGateFl;
        }
        else // if the sost pedal went up
        {
          // turn off the sost hold flag on all notes
          for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
            c->noteState[i].sostHoldFl = false;

          // release any notes which were held by the sost pedal
          _turn_off_all_released_notes( p, c, sec, m->u.midi.uid);

        }
      }
      
      return rc;
    }
    
    rc_t _setMidiSoftPedalMsg( midi_state_t* p, double sec, const msg_t* m )
    {
      rc_t        rc          = kOkRC;
      bool        pedalDownFl = m->u.midi.d1 >= p->cfg.pedalUpMidiValue;
      unsigned    flags       = 0;
      ch_state_t* c           = p->chState + m->u.midi.ch;
      
      if( c->softFl == pedalDownFl )
        flags |= kNoChangeFl;
      else
        flags = pedalDownFl ? kDownPedalFl : kUpPedalFl;
      
      c->softFl = pedalDownFl;

      _onStateChange( p, flags | kPedalEvtFl, sec, m );
      
      return rc;
    }

  }
}


const char* cw::midi_state::flag_to_label( unsigned flag )
{
  return _flagToLabel(flag);
}

unsigned cw::midi_state::flags_to_string_max_string_length()
{
  unsigned charN = 0;
  for(unsigned i=0; flag_map[i].label != nullptr; ++i)
    charN += textLength(flag_map[i].label) + 1;
  
  return charN+1;
}

cw::rc_t cw::midi_state::flags_to_string( unsigned flags, char* str, unsigned strCharN )
{
  rc_t rc = kOkRC;
  unsigned si=0;
  for(unsigned i=0; flag_map[i].label != nullptr && si < strCharN; ++i)
    if( cwIsFlag(flags,flag_map[i].flag) )
      si += snprintf(str + si, strCharN-si,"%s ",flag_map[i].label);

  if( si >= strCharN )
    rc = cwLogError(kBufTooSmallRC,"The flags_to_string() buffer is too small.");
  
  return rc;
}

cw::rc_t  cw::midi_state::format_event( const event_t* e, char* buf, unsigned bufCharN )
{
  return _format_event(e,buf,bufCharN);
}


const cw::midi_state::config_t& cw::midi_state::default_config()
{
  static config_t c = {
    .cacheEnableFl         = true,
    .cacheBlockMsgN        = 1024,
    .pedalHalfMinMidiValue = 42,
    .pedalHalfMaxMidiValue = 46,
    .pedalUpMidiValue      = 64
  };
  
  return c;   
}

cw::rc_t cw::midi_state::create( handle_t&       hRef,
                                 callback_t      cbFunc,
                                 void*           cbArg,
                                 const config_t* cfg )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  midi_state_t* p = mem::allocZ<midi_state_t>();

  if( cfg == nullptr )
    cfg = &default_config();

  p->cbFunc = cbFunc;
  p->cbArg  = cbArg;
  p->cfg    = *cfg;

  for(unsigned i=0; i<midi::kMidiChCnt; ++i)
    p->chState[i].chIdx = i;

  hRef.set(p);
  
  return rc;
}

cw::rc_t cw::midi_state::create( handle_t&       hRef,
                                 callback_t      cbFunc,
                                 void*           cbArg,
                                 const object_t* cfg )
{
  rc_t       rc = kOkRC;
  config_t   c;

  if((rc = cfg->getv("cache_enable_fl",c.cacheEnableFl,
                     "cache_block_msg_count",c.cacheBlockMsgN,
                     "pedal_up_midi_value",c.pedalUpMidiValue,
                     "pedal_half_min_midi_value",c.pedalHalfMinMidiValue,
                     "pedal_half_max_midi_value",c.pedalHalfMaxMidiValue)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI state cfg. parse failed.");
    goto errLabel;
  }

  if((rc = create( hRef,cbFunc,cbArg,&c)) != kOkRC )
  {
    rc = cwLogError(rc,"midi_state object create faild.");
    goto errLabel;
  }
  
 errLabel:
  return rc;
}

    
cw::rc_t cw::midi_state::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  midi_state_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}

cw::rc_t cw::midi_state::setMidiMsg( handle_t h, double sec, unsigned uid, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
{
  rc_t                rc = kOkRC;
  midi_state_t*       p  = _handleToPtr(h);
  const msg_t*        m  = nullptr;
  msg_t               mr;

  status = status & 0xf0;  // be sure that the MIDI channel has been cleared from the status byte

  // convert the midi arg's into a midi_msg_t record
  if( p->cfg.cacheEnableFl )
    m = _insert_midi_msg(p, uid, ch, status, d0, d1 );
  else
    m = _fill_midi_msg(&mr, uid, ch, status, d0, d1 );
  
  switch( status )
  {
    case midi::kNoteOnMdId:
      if( d1 > 0 )
        rc = _setMidiNoteOnMsg(p,sec,m);
      else
        rc = _setMidiNoteOffMsg(p,sec,m);
      break;
      
    case midi::kNoteOffMdId:
      rc = _setMidiNoteOffMsg(p,sec,m);      
      break;
      
    case midi::kCtlMdId:
      switch( d0 )
      {
        case midi::kSustainCtlMdId:
          rc = _setMidiSustainMsg(p,sec,m);
          break;
          
        case midi::kSostenutoCtlMdId:
          rc = _setMidiSostenutoMsg(p,sec,m);
          break;
          
        case midi::kSoftPedalCtlMdId:
          rc = _setMidiSoftPedalMsg(p,sec,m);
          break;
      }
      break;
  }
  
  return rc;
}

cw::rc_t cw::midi_state::setMarker(  handle_t h, double sec, unsigned uid, uint8_t ch, unsigned typeId, unsigned value )
{
 rc_t                rc = kOkRC;
  midi_state_t*       p  = _handleToPtr(h);
  const msg_t*        m  = nullptr;
  msg_t               mr;
  
  // convert the midi arg's into a midi_msg_t record
  if( p->cfg.cacheEnableFl )
    m = _insert_marker_msg(p, uid, ch, typeId, value );
  else
    m = _fill_marker_msg(&mr, uid, ch, typeId, value );

  _onStateChange( p, kMarkerEvtFl, sec, m );

  return rc;
}


void cw::midi_state::reset( handle_t h )
{
  midi_state_t* p = _handleToPtr(h);
  _reset(p);
}

const cw::midi_state::event_t*  cw::midi_state::get_first_link( handle_t h )
{
  midi_state_t* p = _handleToPtr(h);
  return _get_first_link(p);
}


const cw::midi_state::event_t* cw::midi_state::note_event_list(  handle_t h, uint8_t ch, uint8_t pitch )
{
  midi_state_t* p = _handleToPtr(h);
  return _note_event_chain(p,ch,pitch)->begEvt;
}

const cw::midi_state::event_t* cw::midi_state::pedal_event_list( handle_t h, uint8_t ch, unsigned pedal_idx )
{
  midi_state_t*  p         = _handleToPtr(h);
  const event_t* e = nullptr;
  event_chain_t* ec;

  if((ec = _pedal_event_chain_from_pedal_idx(p,ch,pedal_idx)) != nullptr )
    e = ec->begEvt;
  else
    cwLogError(kInvalidArgRC,"'%i is not a valid pedal index.",pedal_idx);

  return e;
}

unsigned cw::midi_state::pedal_count( handle_t h )
{
  return kPedalCnt;
}

unsigned cw::midi_state::pedal_midi_ctl_id_to_index( unsigned midi_ctl_id )
{
  return _pedalMidiToIndex( midi_ctl_id );
}
unsigned cw::midi_state::pedal_index_to_midi_ctl_id( unsigned pedal_idx )
{
  return _pedalIndexToMidi( pedal_idx );
}


void cw::midi_state::get_note_extents(  handle_t h, uint8_t& minPitchRef, uint8_t& maxPitchRef, double& minSecRef, double& maxSecRef )
{
  minSecRef = -1;
  maxSecRef = 0;
  minPitchRef = midi::kMidiNoteCnt;
  maxPitchRef = 0;

  const event_t* e;
  for(unsigned i=0; i<midi::kMidiChCnt; ++i)
    for(unsigned j=0; j<midi::kMidiNoteCnt; ++j)
      if((e = note_event_list(h,i,j)) != nullptr )
      {
        minPitchRef = std::min(minPitchRef,(uint8_t)j);
        maxPitchRef = std::max(maxPitchRef,(uint8_t)j);
        
        for(; e!=nullptr; e=e->link)
        {
          minSecRef = minSecRef==-1 ? e->secs : std::min(minSecRef,e->secs);
          maxSecRef = std::max(maxSecRef,e->secs);
        }
      }
}

void cw::midi_state::get_pedal_extents( handle_t h, double& minSecRef, double& maxSecRef )
{
  minSecRef = -1;
  maxSecRef = 0;
  
  const event_t* e;
  for(unsigned i=0; i<midi::kMidiChCnt; ++i)
    for(unsigned j=0; j<kPedalCnt; ++j)
      if((e = pedal_event_list(h,i,j)) != nullptr )
        for(; e!=nullptr; e=e->link)
        {
          minSecRef = minSecRef==-1 ? e->secs : std::min(minSecRef,e->secs);
          maxSecRef = std::max(maxSecRef,e->secs);
        }
  

}

cw::rc_t cw::midi_state::load_from_midi_file( handle_t h, const char* midi_fname )
{
  rc_t                 rc;
  midi::file::handle_t mfH;

  // open the MIDI file
  if((rc = midi::file::open(mfH,midi_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI file open failed on '%s'.",cwStringNullGuard(midi_fname));
    goto errLabel;
  }
  else
  {
    const midi::file::trackMsg_t** msgA = msgArray(mfH);
    unsigned long long usec0 = 0;
    
    // for each MIDI msg
    for(unsigned i=0; i<msgCount(mfH); ++i)
    {
      const midi::file::trackMsg_t* msg = msgA[i];

      // if this is a channel msg
      if( midi::isChStatus(msg->status) )
      {

        unsigned long long micros = 0;
        if( usec0 == 0)
          usec0 = msg->amicro;
        else
          micros = msg->amicro - usec0;

        double sec = (double)micros/1000000.0;

        // cache the event
        if((rc = setMidiMsg(h, sec, msg->uid, msg->status & 0x0f, msg->status & 0xf0, msg->u.chMsgPtr->d0, msg->u.chMsgPtr->d1 ) ) != kOkRC )
        {
          rc = cwLogError(rc,"Error on MIDI event insertion.");
          goto errLabel;
        }
      }
    }
  }
 errLabel:

  // close the MIDI file
  close(mfH);
  return rc;
}

cw::rc_t cw::midi_state::report_events( handle_t h, const char* out_fname )
{
  rc_t           rc = kOkRC;
  midi_state_t*  p  = _handleToPtr(h);
  file::handle_t fH;
  
  if((rc = file::open(fH,out_fname,file::kWriteFl)) != kOkRC )
  {
    cwLogError(rc,"The report file create failed:'%s'.",out_fname);
    goto errLabel;
  }
  else
  {
    const unsigned bufCharN = 511;
    char buf[ bufCharN+1 ];
    const event_t* e = nullptr;
    const event_t* e0 = nullptr;
    if((e = _get_first_link(p)) != nullptr )
    {
      for(; e!=nullptr; e=e->tlink)
      {
        if((rc = _format_event( e, buf, bufCharN )) != kOkRC )
        {
          rc = cwLogError(rc,"Formst event failed.");
          goto errLabel;
        }

        double dsec = e0==nullptr ? 0 : e->secs - e0->secs;
        file::printf(fH,"%7.3f %s\n",dsec,buf);
        e0 = e;
      }
    }
  }
    
 errLabel:
  if((rc = file::close(fH)) != kOkRC )
    rc = cwLogError(rc,"The report file close failed:'%s'.",out_fname);

  return rc;
}


namespace cw
{
  namespace midi_state
  {
    typedef struct test_arg_str
    {
      bool printMsgsFl;
    } test_arg_t;
    
    void _testCallback( void* arg, unsigned flags, double secs, const msg_t* m )
    {
      test_arg_t* t = (test_arg_t*)arg;
      
      if( t->printMsgsFl && m->typeId == kMidiMsgTId )
      {
        printf("%6.3f %2x %4i : %2i %2x %3i %3i :", secs, flags, m->u.midi.uid, m->u.midi.ch, m->u.midi.status, m->u.midi.d0, m->u.midi.d1 );
        for(unsigned i=0; flag_map[i].label==nullptr; ++i)
          if( cwIsFlag(flags,flag_map[i].flag) )
            printf("%s ",flag_map[i].label);
        printf("\n");
      }
    }

    void _testPrintAllFlags()
    {
      unsigned    strCharN = flags_to_string_max_string_length();
      char str[ strCharN ];
      rc_t rc = flags_to_string( 0xffffffff, str, strCharN );
      printf("All Flags: %i %i %li : %s\n", rc, strCharN, strlen(str)+1, str );
    }
    
    void _testPrintNoteCount( handle_t h, uint8_t ch=0 )
    {
      const event_t* e = nullptr;
      for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
      {
        unsigned n = 0;
        if((e= note_event_list(h, ch, i )) != nullptr )
        {
          for(; e!=nullptr; e=e->link)
            if( cwIsFlag(e->flags, kNoteOnFl ) )
              ++n;

          printf("%3i %3i %3x %s\n",n,i,i,midi::midiToSciPitch(i));
        }
      }
    }
    
    void _testPrintNoteEvent( handle_t h, uint8_t ch, uint8_t pitch )
    {
      const event_t* e;
      if((e= note_event_list(h, ch, pitch )) == nullptr )
      {
        cwLogError(kInvalidArgRC,"There are no notes for pitch %i.",pitch);
      }
      else
      {
        unsigned flagStrCharN = flags_to_string_max_string_length();
        char flagStr[ flagStrCharN ];
        for(; e!=nullptr; e=e->link)
        {
          flags_to_string( e->flags, flagStr, flagStrCharN );
          printf("%6.3f %s\n",e->secs, flagStr);
        }
      }      
    }

    void _testPrintOrderedNoteEvent( handle_t h, uint8_t ch, uint8_t pitch )
    {
      midi_state_t* p = _handleToPtr(h);
      event_chain_t* ec = _note_event_chain(p,ch,pitch);
      
      if( ec->begEvt == nullptr )
      {
        cwLogWarning("Pitch %i on channel %i has no events.",pitch,ch);
      }
      else
      {
        event_t* e;
        unsigned flagStrCharN = flags_to_string_max_string_length();
        char     flagStr[ flagStrCharN+1 ];

        _seek_chain_iterator( p, ec->begEvt->secs );
        
        while((e = _step_chain_iterator(p)) != nullptr )
        {          
          // print all non-note (pedal & marker) events - but skip no-change events
          bool non_note_fl = cwIsNotFlag(e->flags,kNoteEvtFl) && cwIsNotFlag(e->flags,kNoChangeFl);

          // this is a note on the requested pitch
          bool pitch_fl    = cwIsFlag(e->flags,kNoteEvtFl) && e->msg->u.midi.d0 == pitch;

          unsigned uid = e->msg->typeId == kMidiMsgTId ? e->msg->u.midi.uid : 0;
          
          if( pitch_fl || non_note_fl )
          {
            flags_to_string( e->flags, flagStr, flagStrCharN );
            printf("%5i %6.3f %s\n",uid, e->secs, flagStr);   
          }
        }
      }
    }

    void _testPrintTimeLinkedNoteEvent( handle_t h, uint8_t ch, uint8_t pitch )
    {
        unsigned flagStrCharN = flags_to_string_max_string_length();
        char     flagStr[ flagStrCharN+1 ];
      
      for(const event_t* e=get_first_link(h); e!=nullptr; e=e->tlink)
      {
          // print all non-note (pedal & marker) events - but skip no-change events
          bool non_note_fl = cwIsNotFlag(e->flags,kNoteEvtFl) && cwIsNotFlag(e->flags,kNoChangeFl);

          // this is a note on the requested pitch
          bool pitch_fl    = cwIsFlag(e->flags,kNoteEvtFl) && e->msg->u.midi.d0 == pitch;

          unsigned uid = e->msg->typeId == kMidiMsgTId ? e->msg->u.midi.uid : 0;

          if( pitch_fl || non_note_fl )
          {
            flags_to_string( e->flags, flagStr, flagStrCharN );
            printf("%5i %6.3f %s\n",uid, e->secs, flagStr);   
          }          
      }
    }

    rc_t _testPrintPedalEvent( handle_t h, uint8_t ch, uint8_t pedalCtlId )
    {
      rc_t           rc        = kOkRC;
      const event_t* e         = nullptr;
      unsigned       pedal_idx = kInvalidIdx;

      if((pedal_idx = pedal_midi_ctl_id_to_index( pedalCtlId )) == kInvalidIdx )
      {
        rc = cwLogError(rc,"%i is not a valid pedal control id",pedalCtlId);
      }
      
      if((e= pedal_event_list(h, ch, pedal_idx )) == nullptr )
      {
        cwLogWarning("There are no events for pedal idx:%i ctl:%i.",pedal_idx,pedalCtlId);
      }
      else
      {
        unsigned flagStrCharN = flags_to_string_max_string_length();
        char flagStr[ flagStrCharN ];
        for(; e!=nullptr; e=e->link)
          if( cwIsNotFlag(e->flags,kNoChangeFl) )
          {
            flags_to_string( e->flags, flagStr, flagStrCharN );
            printf("%6.3f %s\n",e->secs, flagStr);
          }
      }
      
      return rc;
    }
    
  }
}

cw::rc_t cw::midi_state::test( const object_t* cfg )
{
  rc_t                           rc              = kOkRC;
  const char*                    midi_fname      = nullptr;
  bool                           cache_enable_fl = false;
  const object_t*                args            = nullptr;
  unsigned                       mN              = 0;
  const midi::file::trackMsg_t** mA              = nullptr;
  midi::file::handle_t           mfH;
  midi_state::handle_t           msH;
  test_arg_t test_arg = { .printMsgsFl=false };
 
  if((rc = cfg->getv("midi_fname",midi_fname,
                     "cache_enable_fl", cache_enable_fl,
                     "args", args)) != kOkRC )
  {
    cwLogError(rc,"MIDI state cfg. parse failed.");
    goto errLabel;
  }

  // create the midi_state object
  if((rc = midi_state::create(msH,
                              _testCallback,
                              &test_arg,
                              args  )) != kOkRC )
  {
    cwLogError(rc,"MIDI state object create failed.");
    goto errLabel;
  }

  // open a MIDI file
  if((rc = midi::file::open(mfH,midi_fname)) != kOkRC )
  {
    cwLogError(rc,"MIDI file '%s' open failed.",cwStringNullGuard(midi_fname));
    goto errLabel;
  }
  
  mN = msgCount(mfH);
  mA = msgArray(mfH);

  // update the MIDI state from the MIDI file - and print the changing state events from _testCallback()
  for(unsigned i=0; i<mN; ++i)
  {
    const midi::file::trackMsg_t* m = mA[i];
    
    switch( m->status )
    {
      case midi::kNoteOnMdId:
      case midi::kNoteOffMdId:
      case midi::kCtlMdId:
        {
          double sec = m->amicro / 1000000.0;
          setMidiMsg( msH, sec, m->uid, m->u.chMsgPtr->ch, m->status, m->u.chMsgPtr->d0, m->u.chMsgPtr->d1 );
        }
        break;
    }
  }
  
  //_testPrintNoteCount(msH);
  //_testPrintAllFlags();
  //_testPrintPedalEvent(msH, 0, midi::kSustainCtlMdId );
  //_testPrintNoteEvent(msH, 0, 60 );
  //_testPrintOrderedNoteEvent( msH, 0, 60 );
  _testPrintTimeLinkedNoteEvent( msH, 0, 33 );
  
 errLabel:

  // close the MIDI file
  if((rc = close(mfH)) != kOkRC )
  {
    cwLogError(rc,"MIDI file '%s' close failed.",cwStringNullGuard(midi_fname));
  }

  // close the midi_state object
  if((rc = destroy(msH)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI state object destroy failed.");
    goto errLabel;
  }
  
  return rc;
}

