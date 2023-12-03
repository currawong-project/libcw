#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwPianoScore.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwCsv.h"
#include "cwVectOps.h"

#define INVALID_PERF_MEAS (-1)

namespace cw
{
  namespace perf_score
  {
    /*
    enum {
      kMeasColIdx = 0,
      kLocColIdx,
      kSecColIdx,
      kSciPitchColIdx,
      kStatusColIdx,
      kD0ColIdx,
      kD1ColIdx,
      kBarColIdx,
      kSectionColIdx,
      kEvenColIdx,
      kDynColIdx,
      kTempoColIdx,
      kCostColIdx,
      kColCnt
    };

    typedef struct col_map_str
    {
      unsigned    colId;
      const char* label;
      bool        enableFl;
    } col_map_t;
    */
  
  typedef struct score_str
    {
      event_t* base;
      event_t* end;
      unsigned maxLocId;

      event_t** uid_mapA;
      unsigned  uid_mapN;
      unsigned  min_uid;

      bool has_locs_fl;
      
    } score_t;

    /*
    col_map_t col_map_array[] = { 
      { kMeasColIdx,     "meas",      true },
      { kLocColIdx,      "loc",       true },
      { kLocColIdx,      "oloc",      false },
      { kSecColIdx,      "sec",       true },
      { kSciPitchColIdx, "sci_pitch", true },
      { kStatusColIdx,   "status",    true },
      { kD0ColIdx,       "d0",        true },
      { kD1ColIdx,       "d1",        true },
      { kBarColIdx,      "bar",       true },
      { kSectionColIdx,  "section",   true },
      { kEvenColIdx,     "even",      true },
      { kDynColIdx,      "dyn",       true },
      { kTempoColIdx,    "tempo",     true },
      { kCostColIdx,     "cost",      true },
    };
    */
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


    rc_t _read_csv_line( score_t* p,  bool score_fl, csv::handle_t csvH )
    {
      rc_t     rc = kOkRC;
      event_t* e  = mem::allocZ<event_t>();
      const char* sci_pitch;
      unsigned sci_pitch_char_cnt;
      
      if((rc = getv(csvH,
                    "meas",e->meas,
                    "loc",e->loc,
                    "sec",e->sec,
                    "sci_pitch", sci_pitch,
                    "status", e->status,
                    "d0", e->d0,
                    "d1", e->d1,
                    "bar", e->bar,
                    "section", e->section )) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV.");
        goto errLabel;
      }

      if( score_fl )
      {
        if((rc = getv(csvH,"oloc",e->loc )) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing CSV.");
          goto errLabel;
        }
        
      }
      else
      {
        if((rc = getv(csvH,
                    "even", e->even,
                    "dyn", e->dyn,
                    "tempo", e->tempo,
                    "cost", e->cost )) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing CSV.");
          goto errLabel;
        }        
      }

      if((rc = field_char_count( csvH, title_col_index(csvH,"sci_pitch"), sci_pitch_char_cnt )) != kOkRC )
      {
        rc = cwLogError(rc,"Error retrieving the sci. pitch char count.");
        goto errLabel;
      }
      
      strncpy(e->sci_pitch,sci_pitch,sizeof(e->sci_pitch));
      
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
      bool        score_fl = false;
      /*
      //unsigned    titleN   = sizeof(col_map_array)/sizeof(col_map_array[0]);
      //const char* titleA[ titleN ];

      for(unsigned i=0; i<titleN; ++i)
      {
        titleA[i] = col_map_array[i].label;
        assert( col_map_array[i].colId == i );
      }
      */
      
      if((rc = csv::create(csvH,csvFname)) != kOkRC )
      {
        rc = cwLogError(rc,"CSV create failed on '%s'.");
        goto errLabel;
      }

      if( title_col_index(csvH,"oloc") != kInvalidIdx )
        score_fl = true;

      /*
      for(unsigned i=0; i<titleN; ++i)
        if( col_map_array[i].enableFl )
        {
          if((col_map_array[i].colIdx = title_col_index(csvH,col_map_array[i].label)) == kInvalidIdx )
          {
            rc = cwLogError(rc,"The performance score column '%s' was not found in the score file:'%s'.",col_map_array[i].label,cwStringNullGuard(csvFname));
            goto errLabel;
          }
        }
      */
      
      for(unsigned i=0; (rc = next_line(csvH)) == kOkRC; ++i )
        if((rc = _read_csv_line(p,score_fl,csvH)) != kOkRC )
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
    
    /*
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

    rc_t  _parse_csv_string( const char* lineBuf, unsigned bfi, unsigned efi, char* val, unsigned valCharN )
    {
      unsigned n = std::min(efi-bfi,valCharN);
      strncpy(val,lineBuf+bfi,n);
      val[std::min(n,valCharN-1)] = 0;
      return kOkRC;
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
        kDots_FIdx,
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
        kEven_FIdx,
        kDyn_FIdx,
        kTempo_FIdx,
        kCost_FIdx,
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
            case kMeas_FIdx:
              rc = _parse_csv_unsigned( line_buf, bfi, efi, e->meas );
              break;
              
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

            case kBar_FIdx:
              rc = _parse_csv_unsigned( line_buf, bfi, efi, e->bar );
              break;

            case kSection_FIdx:
              rc = _parse_csv_unsigned( line_buf, bfi, efi, e->section );
              break;

            case kSPitch_FIdx:
              rc = _parse_csv_string( line_buf, bfi, efi, e->sci_pitch, sizeof(e->sci_pitch) );              
              break;

            case kEven_FIdx:
              e->even = INVALID_PERF_MEAS;
              if( efi > bfi+1 )
                rc = _parse_csv_double( line_buf, bfi, efi, e->even );
              break;
              
            case kDyn_FIdx:
              e->dyn = INVALID_PERF_MEAS;
              if( efi > bfi+1 )
                rc = _parse_csv_double( line_buf, bfi, efi, e->dyn );
              break;
              
            case kTempo_FIdx:
              e->tempo = INVALID_PERF_MEAS;
              if( efi > bfi+1 )
                rc = _parse_csv_double( line_buf, bfi, efi, e->tempo );
              break;
              
            case kCost_FIdx:
              e->cost = INVALID_PERF_MEAS;
              if( efi > bfi+1 )
                rc = _parse_csv_double( line_buf, bfi, efi, e->cost );
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
      {
        if((rc = getLineAuto( fH, &lineBufPtr, &lineBufCharCnt )) != kOkRC )
        {
          if( rc != kEofRC )
            rc = cwLogError( rc, "Line read failed on '%s' line number '%i'.",cwStringNullGuard(fn),line+1);
          else
            rc = kOkRC;
          
          goto errLabel;
        }

        if( line > 0 ) // skip column title line
        {

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
      
      }

    errLabel:
      mem::release(lineBufPtr);
      file::close(fH);
        
      return rc;
    }
    */
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
    // if((rc = _parse_csv(p,fn)) != kOkRC )
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
