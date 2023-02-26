#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwPianoScore.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwFile.h"

namespace cw
{
  namespace score
  {
    typedef struct score_str
    {
      event_t* base;
      event_t* end;
      unsigned maxLocId;

      event_t** uid_mapA;
      unsigned  uid_mapN;
      unsigned  min_uid;
      
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

    unsigned _scan_to_end_of_field( const char* lineBuf, unsigned buf_idx, unsigned bufCharCnt )
    {
      for(; buf_idx < bufCharCnt; ++buf_idx )
      {
        if( lineBuf[buf_idx] == '"' )
        {
          for(++buf_idx; buf_idx < bufCharCnt; ++buf_idx)
            if( lineBuf[buf_idx] == '"' )
              break;
        }

        if( lineBuf[buf_idx] == ',')
          break;
      }

      return buf_idx;
    }

    
    rc_t  _parse_csv_double( const char* lineBuf, unsigned bfi, unsigned efi, double &valueRef )
    {
      errno = 0;
      valueRef = strtod(lineBuf+bfi,nullptr);
      if( errno != 0 )
        return cwLogError(kOpFailRC,"CSV String to number conversion failed.");

      return kOkRC;
    }

    rc_t  _parse_csv_unsigned( const char* lineBuf, unsigned bfi, unsigned efi, unsigned &valueRef )
    {
      rc_t rc;
      double v;
      if((rc = _parse_csv_double(lineBuf,bfi,efi,v)) == kOkRC )
        valueRef = (unsigned)v;
      return rc;
    }
    
    rc_t _parse_csv_line( score_t* p, event_t* e, char* line_buf, unsigned lineBufCharCnt )
    {
      enum
      {
        kMeas_FIdx,
        kIndex_FIdx,
        kVoice_FIdx,
        kLoc_FIdx,
        kTick_FIdx,
        kSec_FIdx,
        kDur_FIdx,
        kRval_FIdx,
        kSPitch_FIdx,
        kDMark_FIdx,
        kDLevel_FIdx,
        kStatus_FIdx,
        kD0_FIdx,
        kD1_FIdx,
        kBar_FIdx,
        kSection_FIdx,
        kBpm_FIdx,
        kGrace_FIdx,
        kPedal_FIdx,
        kMax_FIdx
      };

      rc_t     rc        = kOkRC;
      unsigned bfi       = 0;
      unsigned efi       = 0;
      unsigned field_idx = 0;
      

      for(field_idx=0; field_idx != kMax_FIdx; ++field_idx)
      {        
        if((efi = _scan_to_end_of_field(line_buf,efi,lineBufCharCnt)) == kInvalidIdx )
        {
          rc = cwLogError( rc, "End of field scan failed");
          goto errLabel;          
        }
        

        if( bfi != efi )
        {
          switch( field_idx )
          {
            case kLoc_FIdx:
              rc = _parse_csv_unsigned( line_buf, bfi, efi, e->loc );
              break;
            
            case kSec_FIdx:
              rc = _parse_csv_double( line_buf, bfi, efi, e->sec );
              break;
            
            case kStatus_FIdx:
              rc = _parse_csv_unsigned( line_buf, bfi, efi, e->status );
              break;
            
            case kD0_FIdx:
              rc = _parse_csv_unsigned( line_buf, bfi, efi, e->d0 );
              break;
            
            case kD1_FIdx:
              rc = _parse_csv_unsigned( line_buf, bfi, efi, e->d1 );
              break;
            
            default:
              break;
          }
        }
        
        bfi = efi + 1;
        efi = efi + 1;
        
      }

    errLabel:

      return rc;      
    }

    
    rc_t _parse_csv( score_t* p, const char* fn )
    {
      rc_t rc;
      file::handle_t fH;
      unsigned       line_count     = 0;
      char*          lineBufPtr     = nullptr;
      unsigned       lineBufCharCnt = 0;
      event_t*       e              = nullptr;
      
      if((rc = file::open( fH, fn, file::kReadFl )) != kOkRC )
      {
        rc = cwLogError( rc, "Piano score file open failed on '%s'.",cwStringNullGuard(fn));
        goto errLabel;
      }

      if((rc = file::lineCount(fH,&line_count)) != kOkRC )
      {
        rc = cwLogError( rc, "Line count query failed on '%s'.",cwStringNullGuard(fn));
        goto errLabel;
      }

      p->min_uid = kInvalidId;
      p->uid_mapN = 0;
      
      for(unsigned line=0; line<line_count; ++line)
        if( line > 0 ) // skip column title line
        {

          if((rc = getLineAuto( fH, &lineBufPtr, &lineBufCharCnt )) != kOkRC )
          {
            rc = cwLogError( rc, "Line read failed on '%s' line number '%i'.",cwStringNullGuard(fn),line+1);
            goto errLabel;
          }

          e = mem::allocZ<event_t>();
          
          if((rc = _parse_csv_line( p, e, lineBufPtr, lineBufCharCnt )) != kOkRC )
          {
            mem::release(e);
            rc = cwLogError( rc, "Line parse failed on '%s' line number '%i'.",cwStringNullGuard(fn),line+1);
            goto errLabel;            
          }

          // assign the UID
          e->uid = line;          
        
          // link the event into the event list
          if( p->end != nullptr )
            p->end->link = e;
          else
            p->base = e;
        
          p->end  = e;

          // track the max 'loc' id
          if( e->loc > p->maxLocId )
            p->maxLocId = e->loc;

          if( p->min_uid == kInvalidId || e->uid < p->min_uid )
            p->min_uid = e->uid;

          p->uid_mapN += 1;        
        }
      
      

    errLabel:
      mem::release(lineBufPtr);
      file::close(fH);
        
      return rc;
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

          
          if( sci_pitch != nullptr )
            strncpy(e->sci_pitch,sci_pitch,sizeof(e->sci_pitch));
          
          if( dmark != nullptr )
            strncpy(e->dmark,dmark,sizeof(e->dmark));

          if( grace_mark != nullptr )
            strncpy(e->grace_mark,grace_mark,sizeof(e->grace_mark));

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


cw::rc_t cw::score::create( handle_t& hRef, const char* fn )
{
  rc_t rc;
  object_t* cfg = nullptr;
  score_t* p  = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  // parse the cfg file
  /*
  if((rc = objectFromFile( fn, cfg )) != kOkRC )
  {
    rc = cwLogError(rc,"Score parse failed on file: '%s'.", fn);
    goto errLabel;
  }

  rc = create(hRef,cfg);
  */
  
  p = mem::allocZ< score_t >();
  
  rc = _parse_csv(p,fn);

  
  
  hRef.set(p);

  //errLabel:

  if( cfg != nullptr )
    cfg->free();

  if( rc != kOkRC )
    destroy(hRef);
  
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

const cw::score::event_t* cw::score::loc_to_event( handle_t h, unsigned loc )
{
  score_t* p = _handleToPtr(h);  
  return _loc_to_event(p,loc);
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

unsigned cw::score::loc_to_measure( handle_t h, unsigned locId )
{
  score_t* p  = _handleToPtr(h);
  const event_t* e;
  if((e = _loc_to_event(p,locId)) == nullptr )
    return 0;

  return kInvalidId;
}

unsigned  cw::score::loc_to_next_note_on_measure( handle_t h, unsigned locId )
{
  score_t* p  = _handleToPtr(h);
  const event_t* e = _loc_to_event(p,locId);
  
  while( e != nullptr )
    if( midi::isNoteOn(e->status,e->d1))
      return e->meas;
      
  return kInvalidId;
}

double  cw::score::locs_to_diff_seconds( handle_t h, unsigned loc0Id, unsigned loc1Id )
{
  score_t* p  = _handleToPtr(h);
  const event_t* e0 = _loc_to_event(p,loc0Id);
  const event_t* e1 = _loc_to_event(p,loc1Id);

  return e1->sec - e0->sec;  
}

const cw::score::event_t* cw::score::uid_to_event( handle_t h, unsigned uid )
{
  //hscore_t* p  = _handleToPtr(h);
  return nullptr;
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
    
    snprintf(buf,buf_byte_cnt,"uid:%5i meas:%4i loc:%4i tick:%8i sec:%8.3f %4s %5s %3s (st:0x%02x d0:0x%02x d1:0x%02x)", e->uid, e->meas, e->loc, e->tick, e->sec, sci_pitch, dyn_mark, grace_mark, e->status, e->d0, e->d1 );
  }

  return rc;
}

cw::rc_t cw::score::test( const object_t* cfg )
{
  handle_t     h;
  rc_t         rc    = kOkRC;
  const  char* fname = nullptr;
  
  if((rc = cfg->getv( "filename", fname )) != kOkRC )
  {
    rc = cwLogError(rc,"Arg. parsing failed.");
    goto errLabel;
  }

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
