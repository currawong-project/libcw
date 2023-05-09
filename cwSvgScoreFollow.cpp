#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwText.h"
#include "cwTime.h"
#include "cwMidi.h"
#include "cwPianoScore.h"
#include "cwSvg.h"
#include "cwCmInterface.h"

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

#include "cwScoreFollower.h"
#include "cwSvgScoreFollow.h"

#define BOX_W  50
#define BOX_H  50 
#define BORD_X 2
#define BORD_Y 2

#define ROW_0_Y 0


namespace cw
{
  namespace score_follower
  {
    struct ssf_ref_note_str;
    
    typedef struct ssf_ref_str
    {
      bool            locFl;    // true = u.loc false=u.id
      unsigned        left;
      unsigned        top;
      unsigned        col_bottom;

      struct ssf_ref_note_str* refNoteL; // ref_note's linked to this ref
      
      union {
        cmScoreLoc_t* loc;
        unsigned      id;       // bar/section id
      } u;
    } ssf_ref_t;

    typedef struct ssf_ref_note_str
    {
      ssf_ref_t*    ref;
      cmScoreEvt_t* evt;
      unsigned      top;
      struct ssf_ref_note_str* link;
    } ssf_ref_note_t;

    typedef struct ssf_perf_note_str
    {
      unsigned        left;
      unsigned        top;
      ssf_ref_note_t* refNote;
      const ssf_note_on_t*  msg;

      struct ssf_perf_note_str* dupl_link;
      
    } ssf_perf_note_t;


    typedef struct ssf_match_str
    {
      
      ssf_note_on_t*   perf;
      ssf_ref_note_t*  ref;
    } ssf_match_t;
    

    typedef struct ssf_str
    {
      svg::handle_t svgH;
      cmScH_t       cmScH;
      cmScMatcher*  matcher;

      
      ssf_ref_t*    refA;
      unsigned      refAllocN;
      unsigned      refN;

      ssf_ref_note_t* refNoteA;
      unsigned        refNoteAllocN;
      unsigned        refNoteN;

      ssf_perf_note_t* perfA;
      unsigned         perfAllocN;
      unsigned         perfN;
      
    } ssf_t;
    
    
    rc_t _write_rect( ssf_t* p, unsigned left, unsigned top, const char* label, const char* classLabel )
    {
      rc_t rc = kOkRC;
      if((rc = svg::rect(p->svgH,left,top,BOX_W,BOX_H,"class",classLabel,nullptr)) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG rect.");
        goto errLabel;
      }
      
      if((rc = svg::text(p->svgH,left,top+20,label)) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG text.");
        goto errLabel;
      }
      
    errLabel:
      return rc;
    }

    rc_t _write_rect_id( ssf_t* p, unsigned left, unsigned top, const char* classLabel, unsigned id )
    {
      const unsigned labelCharN = 31;
      char label[labelCharN+1];
      snprintf(label,labelCharN,"%5i",id);
      return _write_rect(p,left,top,label,classLabel);
    }

    rc_t _write_rect_id_pitch( ssf_t* p, unsigned left, unsigned top, const char* classLabel, unsigned id, unsigned midi_pitch )
    {
      assert( midi_pitch < 128 );
      const unsigned labelCharN = 63;
      char label[labelCharN+1];
      snprintf(label,labelCharN,"%s %5i",midi::midiToSciPitch((uint8_t)midi_pitch),id);
      return _write_rect(p,left,top,label,classLabel);
    }

    rc_t _write_rect_pitch( ssf_t* p, unsigned left, unsigned top, const char* classLabel, unsigned midi_pitch )
    {
      assert( midi_pitch < 128 );
      const unsigned labelCharN = 63;
      char label[labelCharN+1];
      snprintf(label,labelCharN,"%s",midi::midiToSciPitch((uint8_t)midi_pitch));
      return _write_rect(p,left,top,label,classLabel);
    }
    
    
    rc_t _write_ref( ssf_t* p )
    {      
      rc_t rc = kOkRC;
      
      for(unsigned i=0; i<p->refN; ++i)
      {
        ssf_ref_t* r          = p->refA + i;
        const char* classLabel = r->locFl ? "loc" : "bar";
        unsigned   id         = r->locFl ? r->u.loc->index : r->u.id;

        // write ref rect
        if((rc = _write_rect_id( p, r->left, r->top, classLabel, id )) != kOkRC )
        {
          rc = cwLogError(rc,"Error writing 'loc' rect id:%i.",r->u.loc->index);
          goto errLabel;
        }

        // write the ref-note 
        for(ssf_ref_note_t* rn=r->refNoteL; rn!=nullptr; rn=rn->link)
        {
          if((rc = _write_rect_pitch( p, r->left, rn->top, "ref_note", rn->evt->pitch )) != kOkRC )
          {
            rc = cwLogError(rc,"Error writing 'ref_note' rect.");
            goto errLabel;
          }          
        }
      }

      for(unsigned i=0; i<p->perfN; ++i)
      {
        ssf_perf_note_t* pn = p->perfA + i;
        if((rc = _write_rect_pitch( p, pn->left, pn->top, "perf_note", pn->msg->pitch )) != kOkRC )
        {
            rc = cwLogError(rc,"Error writing 'pref_note' rect.");
            goto errLabel;
        }
        
      }
      
    errLabel:
      return rc;
    }


    rc_t _destroy( ssf_t* p )
    {
      rc_t rc;
      
      if((rc = svg::destroy(p->svgH)) != kOkRC )
        rc = cwLogError(rc,"SVG file object destroy failed.");
      else
      {
        mem::release(p->refA);
        mem::release(p->refNoteA);
        mem::release(p);
      }
      
      return rc;
    }

    ssf_ref_t*  _create_ref( ssf_t* p, unsigned& x_coord_ref, unsigned y_coord )
    {
      ssf_ref_t* ref = nullptr;
      if( p->refN < p->refAllocN )
      {
        
        ref = p->refA + p->refN;
        ref->left = x_coord_ref; 
        ref->top  = y_coord;
        ref->col_bottom = y_coord + BOX_H + BORD_Y;
        
        p->refN += 1;

        if( p->refN == p->refAllocN )
          cwLogWarning("SVG score follower reference array cache is full.");

        x_coord_ref += (BOX_W+BORD_X);

      }
      return ref;
    }

    void _create_bar_ref( ssf_t* p, const cmScoreEvt_t* e, unsigned& x_coord_ref, unsigned y_coord )
    {
      ssf_ref_t* ref;
      if((ref = _create_ref(p,x_coord_ref,y_coord)) != nullptr )
      {
        ref->locFl = false;
        ref->u.id = e->barNumb;
      }
    }

    void _create_loc_ref( ssf_t* p, const cmScoreEvt_t* e, unsigned& x_coord_ref, unsigned y_coord )
    {
      ssf_ref_t* ref;
      if((ref = _create_ref(p,x_coord_ref,y_coord)) != nullptr )
      {
        ref->locFl = true;
        ref->u.loc = cmScoreLoc(p->cmScH, e->locIdx );

        cwAssert( ref->u.loc != nullptr and ref->u.loc->index == e->locIdx );
      }
    }
    
    void _create_ref_array( ssf_t* p, unsigned& x_coord_ref, unsigned y_coord )
    {

      // create the cmScoreLoc array
      unsigned cmEvtN = cmScoreEvtCount(p->cmScH);
      unsigned loc_idx = kInvalidIdx;

      p->refAllocN = cmEvtN;
      p->refA      = mem::allocZ<ssf_ref_t>(p->refAllocN);
      p->refN      = 0;
      
      for(unsigned i=0; i<cmEvtN; ++i)
      {
        const cmScoreEvt_t* e = cmScoreEvt(p->cmScH,i);

        // if this is a bar marker
        if( e->type == kBarEvtScId )
          _create_bar_ref(p,e,x_coord_ref,y_coord);

        // if this is the start of a new location
        if( e->locIdx != loc_idx)
        {
          _create_loc_ref(p,e,x_coord_ref,y_coord);
          loc_idx = e->locIdx;
        }        
      }      
    }

    rc_t _create_ref_note_array( ssf_t* p )
    {
      rc_t rc = kOkRC;
      
      p->refNoteAllocN = p->refAllocN;
      p->refNoteA = mem::allocZ<ssf_ref_note_t>(p->refNoteAllocN);
      p->refNoteN = 0;

      for(unsigned i=0; i<p->refN; ++i)
        if( p->refA[i].locFl )
          for(unsigned j=0; j<p->refA[i].u.loc->evtCnt; ++j)
            if( p->refA[i].u.loc->evtArray[j]->type == kNonEvtScId && p->refNoteN < p->refNoteAllocN )
            {
              ssf_ref_note_t* rn = p->refNoteA + p->refNoteN + 1;
              rn->ref       = p->refA + i;
              rn->evt       = p->refA[i].u.loc->evtArray[j];
              rn->top       = rn->ref->col_bottom;
              rn->link      = rn->ref->refNoteL;
              rn->ref->refNoteL = rn;

              rn->ref->col_bottom += BOX_H + BORD_Y;

              p->refNoteN += 1;
              if( p->refNoteN >= p->refNoteAllocN )
              {
                rc = cwLogError(kBufTooSmallRC,"The ref note array in the SVG score writer is too small.");
                goto errLabel;
              }              
            }

    errLabel:
      return rc;      
    }

    ssf_ref_note_t* _find_ref_note( ssf_t* p, const cmScoreEvt_t* e )
    {
      ssf_ref_note_t*  rn = nullptr;
      for(unsigned i=0; i<p->refN; ++i)
      {
        ssf_ref_t* r = p->refA + i;
        // if this is the ref record for the target location
        if( r->locFl and r->u.loc->index == e->locIdx )
        {
          // locate the ref note associated with e
          for(rn=r->refNoteL; rn!=nullptr; rn=rn->link )
            if( rn->evt->index == e->index )
              break;
        }
      }
      
      // the reference must be found for the event
      assert( rn != nullptr );
      return rn;
    }

    rc_t _setup_perf_note( ssf_t* p, ssf_ref_t* ref, ssf_ref_note_t* rn, ssf_ref_note_t* dupl_rn, const ssf_note_on_t* msg )
    {
      rc_t rc = kOkRC;
      
      if( p->perfN >= p->perfAllocN )
      {
        rc = cwLogError(kBufTooSmallRC,"The perf_note array is too small.");
      }
      else
      {
        ssf_perf_note_t* pn = p->perfA + p->perfN;

        pn->left         = ref->left;
        pn->top          = ref->col_bottom;
        pn->refNote      = rn;
        pn->msg          = msg;
        ref->col_bottom += BOX_H + BORD_Y;
        
        p->perfN += 1;
      }
      
      return rc;
    }
    
    rc_t _create_perf_array( ssf_t* p, const ssf_note_on_t* midiA, unsigned midiN )
    {
      rc_t rc = kOkRC;

      p->perfAllocN = midiN*2;
      p->perfA = mem::allocZ<ssf_perf_note_t>(p->perfAllocN);
      p->perfN = 0;

      ssf_ref_t* r0 = nullptr;

      // for each performed MIDI note
      for(unsigned muid=0; muid<midiN; ++muid)
      {
        cmScoreEvt_t*   e   = nullptr;
        ssf_ref_note_t* rn  = nullptr;
        ssf_ref_note_t* rn0 = nullptr;
        
        // look for a 'match' record that refers to this perf note
        for(unsigned j=0; j<p->matcher->ri; ++j)
        {
          if( p->matcher->res[j].muid == muid )
          {
            assert( p->matcher->res[j].scEvtIdx != kInvalidIdx );

            // locate the score evt assoc'd with this 'match' record
            if((e = cmScoreEvt( p->cmScH, p->matcher->res[j].scEvtIdx )) != nullptr )
            {
              rn = _find_ref_note( p, e );
              assert( rn != nullptr );

              if((rc = _setup_perf_note(p,rn->ref,rn,rn0,midiA+muid)) != kOkRC )
                goto errLabel;

              rn0 = rn;
              r0 = rn->ref;
            }
            else
            {
              // this should be impossible
              assert(0);
            }
          }
        }

        // if this perf note does not have any matches
        if( rn == nullptr )
          if((rc = _setup_perf_note(p,r0,nullptr,nullptr,midiA+muid)) != kOkRC )
            goto errLabel;
        
      }

      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"create_perf_array failed.");
      
      return rc;
    }


    void _create_css( ssf_t* p )
    {
      install_css(p->svgH,".bar","fill",0xc0c0c0,"rgb");
      install_css(p->svgH,".loc","fill",0xe0e0e0,"rgb");      
      install_css(p->svgH,".ref_note","fill",0x20B2AA,"rgb");      
      install_css(p->svgH,".perf_note","fill",0xE0FFFF,"rgb");      
    }
    
    ssf_t* _create( cmScH_t cmScH, cmScMatcher* matcher, const ssf_note_on_t* midiA, unsigned midiN  )
    {
      rc_t rc;
      ssf_t* p = mem::allocZ<ssf_t>();

      unsigned x_coord = 0;
      unsigned y_coord = 0;
      
      p->cmScH   = cmScH;
      p->matcher = matcher;
  
      // create the SVG file object
      if((rc = svg::create(p->svgH)) != kOkRC )
      {
        rc = cwLogError(rc,"SVG file object create failed.");
        goto errLabel;
      }

      _create_css(p);
      
      // create the refence row (the top row or bar and loc markers)
      _create_ref_array( p, x_coord, y_coord );

      // create the reference notes
      _create_ref_note_array( p );

      _create_perf_array(p,midiA,midiN );

    errLabel:
      if(rc != kOkRC )
        _destroy(p);
      
      return p;
      
    }

    
  }
}

cw::rc_t cw::score_follower::svgScoreFollowWrite( cmScH_t         cmScH,
                                                  cmScMatcher*    matcher,
                                                  ssf_note_on_t*  midiA,
                                                  unsigned        midiN,
                                                  const char*     out_fname )
{
  rc_t rc = kOkRC;
  ssf_t* p;

  if((p = _create(cmScH, matcher, midiA, midiN )) == nullptr )
  {
    rc = cwLogError(rc,"Score follower SVG writer initialization failed.");
    goto errLabel;
  }
  
  if((rc = _write_ref(p)) != kOkRC )
  {
    rc = cwLogError(rc,"Error writing SSF reference row.");
    goto errLabel;
  }

  if((rc = write( p->svgH, out_fname, nullptr, svg::kStandAloneFl )) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating SSF output file.");
    goto errLabel;
  }


 errLabel:
  _destroy(p);
    
  return rc;
  
}




