#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwPianoScore.h"
#include "cwMidi.h"

namespace cw
{
  namespace score
  {
    typedef struct score_str
    {
      event_t* base;
      event_t* end;
      unsigned maxLocId;
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

    rc_t _parse_event_list( score_t* p, const object_t* cfg )
    {
      rc_t            rc;
      const object_t* eventL;
      
      if((rc = cfg->getv( "evtL", eventL )) != kOkRC || eventL->is_list()==false )
        rc = cwLogError( rc, "Unable to locate the 'evtL' configuration tag.");
      else
      {
        unsigned eventN = eventL->child_count();
        
        for(unsigned i=0; i<eventN; ++i)
        {
          const char*     sci_pitch  = nullptr;
          const char*     dmark      = nullptr;
          const char*     grace_mark = nullptr;
          
          const object_t* evt_cfg    = eventL->child_ele(i);

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

          
          if( sci_pitch != nullptr )
            memcpy(e->sci_pitch,sci_pitch,sizeof(e->sci_pitch));
          
          if( dmark != nullptr )
            memcpy(e->dmark,dmark,sizeof(e->dmark));

          if( grace_mark != nullptr )
            memcpy(e->grace_mark,grace_mark,sizeof(e->grace_mark));

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

    const event_t* _uid_to_event( score_t* p, unsigned uid )
    {
      const event_t* e = p->base;
      for(; e!=nullptr; e=e->link)
        if( e->uid == uid )
          return e;

      return nullptr;
    }
    
  }
}


cw::rc_t cw::score::create( handle_t& hRef, const char* fn )
{
  rc_t rc;
  object_t* cfg = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  // parse the cfg file
  if((rc = objectFromFile( fn, cfg )) != kOkRC )
  {
    rc = cwLogError(rc,"Score parse failed on file: '%s'.", fn);
    goto errLabel;
  }

  rc = create(hRef,cfg);

 errLabel:
  return rc;
}

cw::rc_t cw::score::create( handle_t& hRef, const object_t* cfg )
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
  
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    destroy(hRef);
  
  return rc;
}

cw::rc_t cw::score::destroy( handle_t& hRef )
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
  

unsigned       cw::score::event_count( handle_t h )
{
  score_t* p = _handleToPtr(h);
  unsigned n = 0;
  for(event_t* e=p->base; e!=nullptr; e=e->link)
    ++n;

  return n;  
}

const cw::score::event_t* cw::score::base_event( handle_t h )
{
  score_t* p = _handleToPtr(h);
  return p->base;
}


cw::rc_t  cw::score::event_to_string( handle_t h, unsigned uid, char* buf, unsigned buf_byte_cnt )
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
    
    snprintf(buf,buf_byte_cnt,"uid:%5i meas:%4i loc:%4i tick:%8i sec:%8.3f %4s %5s %3s (st:0x%02x d1:0x%02x d0:0x%02x)", e->uid, e->meas, e->loc, e->tick, e->sec, sci_pitch, dyn_mark, grace_mark, e->status, e->d0, e->d1 );
  }

  return rc;
}

unsigned       cw::score::loc_count( handle_t h )
{
  score_t* p  = _handleToPtr(h);
  return p->maxLocId;
}

bool cw::score::is_loc_valid( handle_t h, unsigned locId )
{
  score_t* p  = _handleToPtr(h);
  return locId < p->maxLocId;
}
