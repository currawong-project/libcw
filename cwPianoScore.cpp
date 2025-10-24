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
#include "cwTime.h"
#include "cwFile.h"
#include "cwCsv.h"
#include "cwVectOps.h"

#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfTrack.h"
#include "cwPerfMeas.h"
#include "cwPianoScore.h"

#define INVALID_PERF_MEAS (-1)

namespace cw
{
  namespace perf_score
  {
  typedef struct score_str
    {
      event_t* base;
      event_t* end;
      unsigned maxLocId;

      event_t** uid_mapA;
      unsigned  uid_mapN;
      unsigned  min_uid;

      bool has_locs_fl;
      bool uses_oloc_fl;

      
    } score_t;

    score_t* _handleToPtr(handle_t h)
    {
      return handleToPtr<handle_t,score_t>(h);
    }

    rc_t _destroy( score_t* p )
    {
      rc_t rc = kOkRC;
      event_t* e = p->base;
      while( e != nullptr )
      {
        event_t* e0 = e->link;

        mem::free(e);
        
        e = e0;
      }
      return rc;
    }

    void _set_bar_pitch_index( score_t* p )
    {
      unsigned cntA[ midi::kMidiNoteCnt ];
      unsigned barNumb = kInvalidId;
      
      for(event_t* e=p->base; e!=nullptr; e=e->link)
      {
        if( barNumb != e->meas )
        {
          vop::fill(cntA,midi::kMidiNoteCnt,0);
          barNumb = e->meas;
        }

        if( midi::isNoteOn(e->status,e->d1) )
        {
          e->barPitchIdx = cntA[ e->d0 ];
          cntA[ e->d0 ] += 1;
        }
      }      
    }

    void _setup_feat_vectors( score_t* p )
    {
      for(event_t* e=p->base; e!=nullptr; e=e->link)
        if( e->valid_stats_fl )
        {
          for(unsigned i=0; i<perf_meas::kValCnt; ++i)
          {
            unsigned stat_idx = e->statsA[i].id;
            
            switch( e->statsA[i].id )
            {
              case perf_meas::kEvenValIdx:      e->featV[ stat_idx ] = e->even;  break;
              case perf_meas::kDynValIdx:       e->featV[ stat_idx ] = e->dyn;   break;
              case perf_meas::kTempoValIdx:     e->featV[ stat_idx ] = e->tempo; break;
              case perf_meas::kMatchCostValIdx: e->featV[ stat_idx ] = e->cost;  break;
            }
            
            e->featMinV[ stat_idx ] = e->statsA[i].min;
            e->featMaxV[ stat_idx ] = e->statsA[i].max;
            
          }
        }
    }

    unsigned _get_loc_count( const score_t* p )
    {
      unsigned max_loc = 0;
      for(const event_t* e=p->base; e!=nullptr; e=e->link)
        if(midi::isNoteOn(e->status,e->d1))
          if( e->loc != kInvalidId && e->loc > max_loc )
            max_loc = e->loc;
      
      return max_loc+1;        
    }
    
    void _setup_chord_info( score_t* p )
    {
      unsigned locN = _get_loc_count(p);
      unsigned* locA = mem::allocZ<unsigned>(locN);

      // get the count of notes per loc and set event_t.chord_note_cnt
      for(event_t* e=p->base; e!=nullptr; e=e->link)
      {
        if(midi::isNoteOn(e->status,e->d1))
        {
          if( e->loc != kInvalidId )
          {
            assert( e->loc < locN );
            e->chord_note_idx = locA[ e->loc ];
            locA[ e->loc ] += 1;
          }
        }
        else
        {
          e->chord_note_idx = kInvalidIdx;
          e->chord_note_cnt = 0;
        }
      }

      // set the event_t.chord_note_cnt
      for(event_t* e=p->base; e!=nullptr; e=e->link)
        if(midi::isNoteOn(e->status,e->d1) && e->loc != kInvalidId )
        {
          assert( e->loc < locN );
          e->chord_note_cnt = locA[ e->loc ];
        }
      
      mem::free(locA);
      
    }


    rc_t _read_meas_stats( score_t* p, csv::handle_t csvH, event_t* e )
    {
      rc_t rc;
      
      if((rc = getv(csvH,                    
                    "even_min",  e->statsA[ perf_meas::kEvenValIdx ].min,
                    "even_max",  e->statsA[ perf_meas::kEvenValIdx ].max,
                    "even_mean", e->statsA[ perf_meas::kEvenValIdx ].mean,
                    "even_std",  e->statsA[ perf_meas::kEvenValIdx ].std,
                    "dyn_min",   e->statsA[ perf_meas::kDynValIdx ].min,
                    "dyn_max",   e->statsA[ perf_meas::kDynValIdx ].max,
                    "dyn_mean",  e->statsA[ perf_meas::kDynValIdx ].mean,
                    "dyn_std",   e->statsA[ perf_meas::kDynValIdx ].std,
                    "tempo_min", e->statsA[ perf_meas::kTempoValIdx ].min,
                    "tempo_max", e->statsA[ perf_meas::kTempoValIdx ].max,
                    "tempo_mean",e->statsA[ perf_meas::kTempoValIdx ].mean,
                    "tempo_std", e->statsA[ perf_meas::kTempoValIdx ].std,
                    "cost_min",  e->statsA[ perf_meas::kMatchCostValIdx ].min,
                    "cost_max",  e->statsA[ perf_meas::kMatchCostValIdx ].max,
                    "cost_mean", e->statsA[ perf_meas::kMatchCostValIdx ].mean,
                    "cost_std",  e->statsA[ perf_meas::kMatchCostValIdx ].std )) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV meas. stats field.");
        goto errLabel;        
      }

      e->statsA[ perf_meas::kEvenValIdx ].id     = perf_meas::kEvenValIdx;
      e->statsA[ perf_meas::kDynValIdx ].id       = perf_meas::kDynValIdx;
      e->statsA[ perf_meas::kTempoValIdx ].id     = perf_meas::kTempoValIdx;
      e->statsA[ perf_meas::kMatchCostValIdx ].id = perf_meas::kMatchCostValIdx;

    errLabel:
      
      return rc;
    }

    rc_t _read_csv_line( score_t* p,  bool score_fl, csv::handle_t csvH )
    {
      rc_t     rc = kOkRC;
      event_t* e  = mem::allocZ<event_t>();
      const char* sci_pitch;
      unsigned sci_pitch_char_cnt;
      int has_stats_fl = 0;
        
      if((rc = getv(csvH,
                    "meas",e->meas,
                    "loc",e->loc,
                    "sec",e->sec,
                    "sci_pitch", sci_pitch,
                    "status", e->status,
                    "d0", e->d0,
                    "d1", e->d1,
                    "bar", e->bar,
                    "section", e->section)) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV.");
        goto errLabel;
      }

      if( has_field(csvH,"has_stats_fl") )
        if((rc = getv(csvH,"has_stats_fl",has_stats_fl)) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing optional fields.");
          goto errLabel;
        }
      
      e->valid_stats_fl = has_stats_fl;
      
      if( score_fl )
      {
        const char*     grace_mark = nullptr;

        e->loc = kInvalidId;
        
        if((rc = getv(csvH,
                      "oloc",  e->loc,
                      "index", e->uid,
                      "grace", grace_mark )) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing score CSV.");
          goto errLabel;
        }

        if( has_field(csvH,"player_id") )
        {
          if((rc = getv(csvH,"player_id",e->player_id)) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing score CSV 'player' field.");
            goto errLabel;
          }
        }

        if( has_field(csvH,"piano_id"))
        {
          if((rc = getv(csvH,"piano_id",e->piano_id)) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing score CSV 'piano_id' field.");
            goto errLabel;
          }
        }

        
        if( e->section > 0 && has_stats_fl )
          if((rc = _read_meas_stats(p,csvH,e)) != kOkRC )
            goto errLabel;

        textCopy( e->grace_mark, sizeof(e->grace_mark),grace_mark);
                 
      }
      else
      {
        if((rc = getv(csvH,
                      "even", e->even,
                      "dyn", e->dyn,
                      "tempo", e->tempo,
                      "cost", e->cost)) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing CSV.");
          goto errLabel;
        }

        if( has_stats_fl )
          if((rc = _read_meas_stats(p,csvH,e)) != kOkRC )
            goto errLabel;
          
      }

      if((rc = field_char_count( csvH, title_col_index(csvH,"sci_pitch"), sci_pitch_char_cnt )) != kOkRC )
      {
        rc = cwLogError(rc,"Error retrieving the sci. pitch char count.");
        goto errLabel;
      }
      
      strncpy(e->sci_pitch,sci_pitch,sizeof(e->sci_pitch)-1);
      
      if( p->end == nullptr )
      {
        p->base = e;
        p->end  = e;
      }
      else
      {
        p->end->link = e;
        p->end = e;
      }

      // track the max 'loc' id
      if( e->loc > p->maxLocId )
        p->maxLocId = e->loc;

      if( p->min_uid == kInvalidId || e->uid < p->min_uid )
        p->min_uid = e->uid;

      p->uid_mapN += 1;        
      
      
    errLabel:

      if( rc != kOkRC )
        mem::release(e);
      
      return rc;
    }
    
    rc_t _read_csv( score_t* p, const char* csvFname )
    {
      csv::handle_t csvH;
      rc_t        rc       = kOkRC;

      p->uses_oloc_fl = false;
      
      if((rc = csv::create(csvH,csvFname)) != kOkRC )
      {
        rc = cwLogError(rc,"CSV create failed on '%s'.");
        goto errLabel;
      }

      // Distinguish between the score file which has
      // an 'oloc' field and the recorded performance
      // files which do not.
      if( title_col_index(csvH,"oloc") != kInvalidIdx )
        p->uses_oloc_fl = true;

      for(unsigned i=0; (rc = next_line(csvH)) == kOkRC; ++i )
        if((rc = _read_csv_line(p,p->uses_oloc_fl,csvH)) != kOkRC )
        {
          rc = cwLogError(rc,"Error reading CSV line number:%i.",i+1);
          goto errLabel;
        }

      if( rc == kEofRC )
        rc = kOkRC;

    errLabel:
      destroy(csvH);
      return rc;
    }

    bool _does_file_have_loc_info( const char* fn )
    {
      rc_t          rc              = kOkRC;
      bool          has_loc_info_fl = false;
      csv::handle_t csvH;
      
      if((rc = create( csvH, fn)) != kOkRC )
        goto errLabel;

      for(unsigned i=0; i<col_count(csvH); ++i)
        if( textIsEqual( col_title(csvH,i), "loc") )
        {
          has_loc_info_fl = true;
          break;
        }
            
      destroy(csvH);
    errLabel:
      
      return has_loc_info_fl;
        
    }
    
    rc_t _parse_event_list( score_t* p, const object_t* cfg )
    {
      rc_t            rc;
      const object_t* eventL = nullptr;
      
      if((rc = cfg->getv( "evtL", eventL )) != kOkRC || eventL==nullptr || eventL->is_list()==false )
        rc = cwLogError( rc, "Unable to locate the 'evtL' configuration tag.");
      else
      {
        //unsigned eventN = eventL->child_count();
        const object_t* evt_cfg = eventL->next_child_ele(nullptr);
        
        for(unsigned i=0; evt_cfg != nullptr; evt_cfg = eventL->next_child_ele(evt_cfg),++i)
        {
          const char*     sci_pitch  = nullptr;
          const char*     dmark      = nullptr;
          const char*     grace_mark = nullptr;
          
          //const object_t* evt_cfg    = eventL->next_child_ele(i);

          event_t* e = mem::allocZ<event_t>();

          if((rc = evt_cfg->getv( "meas",      e->meas,
                                  "voice",     e->voice,
                                  "loc",       e->loc,
                                  "tick",      e->tick,
                                  "sec",       e->sec )) != kOkRC )
          {
            rc = cwLogError(rc,"Score parse failed on required event fields at event index:%i.",i);
            goto errLabel;
          }

          if((rc = evt_cfg->getv_opt( "rval",      e->rval,
                                      "sci_pitch", sci_pitch,
                                      "dmark",     dmark,
                                      "dlevel",  e->dlevel,
                                      "status",  e->status,
                                      "d0",      e->d0,
                                      "d1",      e->d1,
                                      "grace",   grace_mark,
                                      "section", e->section,
                                      "bpm",     e->bpm,
                                      "bar",     e->bar)) != kOkRC )
          {
            rc = cwLogError(rc,"Score parse failed on optional event fields at event index:%i.",i);
            goto errLabel;
          }


          
          textCopy( e->sci_pitch,sizeof(e->sci_pitch),sci_pitch);
          textCopy( e->dmark,sizeof(e->dmark),dmark);
          textCopy( e->grace_mark, sizeof(e->grace_mark),grace_mark);
          
          // assign the UID
          e->uid = i;          
          
          // link the event into the event list
          if( p->end != nullptr )
            p->end->link = e;
          else
            p->base = e;
          
          p->end  = e;

          // track the max 'loc' id
          if( e->loc > p->maxLocId )
            p->maxLocId = e->loc;
          
        }
        
      }
    errLabel:
      return rc;
    }
    
    rc_t _parse_midi_csv( score_t* p, const char* fn )
    {
      rc_t rc = kOkRC;
      csv::handle_t csvH;
      const char* titleA[] = { "dev","port","microsec","id","sec","ch","status","sci_pitch","d0","d1" };
      const unsigned titleN = sizeof(titleA)/sizeof(titleA[0]);
      
      if((rc = csv::create(csvH,fn,titleA, titleN)) != kOkRC )
      {
        rc = cwLogError(rc,"CSV object create failed on '%s'.",cwStringNullGuard(fn));
        goto errLabel;
      }

      for(unsigned i=0;  (rc = next_line(csvH)) == kOkRC; ++i )
      {
        unsigned ch = 0;
        event_t* e = mem::allocZ<event_t>();

        
        if((rc = csv::getv(csvH,
                           "id",e->uid,
                           "sec",e->sec,
                           "ch",ch,
                           "status",e->status,
                           "d0",e->d0,
                           "d1",e->d1)) != kOkRC )
        {
          rc = cwLogError(rc,"CSV parse failed on line index:%i of '%s'.",i,cwStringNullGuard(fn));
          mem::release(e);
          goto errLabel;
        }

        e->status += ch;
        e->loc = e->uid;
        
        // link the event into the event list
        if( p->end != nullptr )
          p->end->link = e;
        else
          p->base = e;
        
        p->end  = e;
        
      }


    errLabel:
      if((rc = csv::destroy(csvH)) != kOkRC )
        rc = cwLogError(rc,"CSV object destroy failed on '%s'.",cwStringNullGuard(fn));
      
      return rc;
    }

    
    const event_t* _uid_to_event( score_t* p, unsigned uid )
    {
      const event_t* e = p->base;
      for(; e!=nullptr; e=e->link)
        if( e->uid == uid )
          return e;

      return nullptr;
    }

    const event_t* _loc_to_event( score_t* p, unsigned loc )
    {
      const event_t* e = p->base;
      for(; e!=nullptr; e=e->link)
        if( e->loc == loc )
          return e;

      return nullptr;
      
    }
    
  }
}


cw::rc_t cw::perf_score::create( handle_t& hRef, const char* fn )
{
  rc_t rc;
  object_t* cfg = nullptr;
  score_t* p  = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  p = mem::allocZ< score_t >();

  if( _does_file_have_loc_info(fn) )
  {
    if((rc = _read_csv( p, fn )) != kOkRC )
      goto errLabel;

    _set_bar_pitch_index(p);
    
    p->has_locs_fl = true;    
  }
  else
  {
    if((rc = _parse_midi_csv(p, fn)) != kOkRC )
      goto errLabel;
    
    p->has_locs_fl = false;    
  }

  if( p->uses_oloc_fl )
    _setup_chord_info(p);
  
  _setup_feat_vectors(p);
    
  hRef.set(p);

 errLabel:  
  if( cfg != nullptr )
    cfg->free();

  if( rc != kOkRC )
  {
    destroy(hRef);
    rc = cwLogError(rc,"Performance score create failed on '%s'.",fn);
  }

  
  return rc;
}

cw::rc_t cw::perf_score::create( handle_t& hRef, const object_t* cfg )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  score_t* p = mem::allocZ< score_t >();

  // parse the event list
  if((rc = _parse_event_list(p, cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Score event list parse failed.");
    goto errLabel;
  }

  _set_bar_pitch_index(p);
  
  p->has_locs_fl = true;
  
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    destroy(hRef);
  
  return rc;
}

cw::rc_t cw::perf_score::create_from_midi_csv( handle_t& hRef, const char* fn )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  score_t* p = mem::allocZ< score_t >();

  // parse the event list
  if((rc = _parse_midi_csv(p, fn)) != kOkRC )
  {
    rc = cwLogError(rc,"Score event list parse failed.");
    goto errLabel;
  }

  _set_bar_pitch_index(p);
  
  p->has_locs_fl = false;
  
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    destroy(hRef);
  
  return rc;
}


cw::rc_t cw::perf_score::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return rc;
    
  score_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  mem::release(p);
  hRef.clear();

  return rc; 
}

bool cw::perf_score::has_loc_info_flag( handle_t h )
{
  score_t* p = _handleToPtr(h);
  return p->has_locs_fl;
}


unsigned       cw::perf_score::event_count( handle_t h )
{
  score_t* p = _handleToPtr(h);
  unsigned n = 0;
  for(event_t* e=p->base; e!=nullptr; e=e->link)
    ++n;

  return n;  
}

const cw::perf_score::event_t* cw::perf_score::base_event( handle_t h )
{
  score_t* p = _handleToPtr(h);
  return p->base;
}

const cw::perf_score::event_t* cw::perf_score::loc_to_event( handle_t h, unsigned loc )
{
  score_t* p = _handleToPtr(h);  
  return _loc_to_event(p,loc);
}


cw::rc_t  cw::perf_score::event_to_string( handle_t h, unsigned uid, char* buf, unsigned buf_byte_cnt )
{
  score_t*       p  = _handleToPtr(h);
  const event_t* e  = nullptr;
  rc_t           rc = kOkRC;
  
  if((e = _uid_to_event( p, uid )) == nullptr )
    rc = cwLogError(kInvalidIdRC,"A score event with uid=%i does not exist.",uid);
  else
  {
    const char* sci_pitch = strlen(e->sci_pitch)  ? e->sci_pitch  : "";
    const char* dyn_mark  = strlen(e->dmark)      ? e->dmark      : "";
    const char* grace_mark= strlen(e->grace_mark) ? e->grace_mark : "";

    if( midi::isSustainPedal( e->status, e->d0 ) )
      sci_pitch = midi::isPedalDown( e->status, e->d0, e->d1 ) ? "Dv" : "D^";
    else
      if( midi::isSostenutoPedal( e->status, e->d0 ) )
        sci_pitch = midi::isPedalDown( e->status, e->d0, e->d1 ) ? "Sv" : "S^";
    
    snprintf(buf,buf_byte_cnt,"uid:%5i meas:%4i loc:%4i tick:%8i sec:%8.3f %4s %5s %3s (st:0x%02x d0:0x%02x d1:0x%02x)", e->uid, e->meas, e->loc, e->tick, e->sec, sci_pitch, dyn_mark, grace_mark, e->status, e->d0, e->d1 );
  }

  return rc;
}

cw::rc_t cw::perf_score::test( const object_t* cfg )
{
  handle_t     h;
  rc_t         rc    = kOkRC;
  const  char* fname = nullptr;
  
  if((rc = cfg->getv( "filename", fname )) != kOkRC )
  {
    rc = cwLogError(rc,"Arg. parsing failed.");
    goto errLabel;
  }

  cwLogInfo("Creating score from '%s'.",cwStringNullGuard(fname));
  
  if((rc = create( h, fname )) != kOkRC )
  {
    rc = cwLogError(rc,"Score create failed.");
    goto errLabel;
  }

  cwLogInfo("Score created from '%s'.",cwStringNullGuard(fname));
 errLabel:

  destroy(h);
  
  return rc;
}
