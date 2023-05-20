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

#define PRE_REF_IDX 0

namespace cw
{
  namespace score_follower
  {
    struct ssf_ref_note_str;

    enum {
      kLocTId,
      kIdTId,
      kPreTId
    };

    // bar/section or location anchor
    // (one for each top row of UI elements)
    typedef struct ssf_ref_str
    {
      unsigned tid;          // See k??TId 
      unsigned left;
      unsigned top;
      unsigned col_bottom;

      struct ssf_ref_note_str* refNoteL; // ref_note's linked to this ref
      
      union {
        cmScoreLoc_t* loc;  // cmScore location
        unsigned      id;   // bar/section id
      } u;
    } ssf_ref_t;

    // ref. score note that performed notes should match to
    typedef struct ssf_ref_note_str
    {
      ssf_ref_t*               ref; // loc UI component that this ref. note belongs to 
      cmScoreEvt_t*            evt; // cmScore event assoc'd with this note
      unsigned                 top;
      unsigned                 cnt; // count of perf notes that matched to this ref-note
      unsigned                 cwLocId; 
      struct ssf_ref_note_str* link;
    } ssf_ref_note_t;

    typedef struct ssf_perf_note_str
    {
      unsigned              left;
      unsigned              top;  
      ssf_ref_note_t*       refNote;     // ref. note that this perf. note matches to
      const ssf_note_on_t*  msg;         // MIDI info for this performed note
      unsigned              seqId;       // performed note sequence id == index into perfA[] of this note
      bool                  isVisibleFl; // true if this rect is visible
      
      // 'duplicate' perf notes occur when the same perf note is connected
      // to multiple references. This can occur when the score follower backtracks.
      struct ssf_perf_note_str* dupl_list; // list of 'duplicate' perf. notes.
      struct ssf_perf_note_str* dupl_link;
      
    } ssf_perf_note_t;


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

      unsigned minMatchLoc;
      unsigned maxMatchLoc;
      
    } ssf_t;
    
    
    rc_t _write_rect( ssf_t* p, unsigned left, unsigned top, const char* label, const char* classLabel, const char* classLabel2=nullptr, unsigned cwLocId=kInvalidId )
    {
      rc_t rc = kOkRC;

      int n = textLength(classLabel) + textLength(classLabel2) + 2;
      char buf[ n + 1 ];
     
      if( classLabel2 != nullptr )
      {
        snprintf(buf,n,"%s %s",classLabel,classLabel2);
        buf[n] = 0;
        classLabel = buf;
      } 
            
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

      if( cwLocId != kInvalidId )
      {
        unsigned locIdCharN = 31;
        char locIdBuf[ locIdCharN + 1 ];
        snprintf(locIdBuf,locIdCharN,"%i",cwLocId);
        if((rc = svg::text(p->svgH,left,top+40,locIdBuf)) != kOkRC )
        {
          rc = cwLogError(rc,"Error writing SVG cwLocId text.");
          goto errLabel;
        }
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

    rc_t _write_rect_pitch( ssf_t* p, unsigned left, unsigned top, const char* classLabel, unsigned midi_pitch, bool noMatchFl,unsigned cnt=kInvalidCnt, unsigned cwLocId=kInvalidId)
    {
      const unsigned labelCharN  = 63;
      char           label[labelCharN+1];
      const char*    classLabel2 = noMatchFl ? "no_match" : nullptr;
      
      if( midi_pitch == midi::kInvalidMidiPitch )
        snprintf(label,labelCharN,"%s","pre");
      else
        if( cnt == kInvalidCnt )
          snprintf(label,labelCharN,"%s",midi::midiToSciPitch((uint8_t)midi_pitch));
        else
        {
          snprintf(label,labelCharN,"%s %i",midi::midiToSciPitch((uint8_t)midi_pitch),cnt);
          classLabel2 = "multi_match";
        }
      
      return _write_rect(p,left,top,label,classLabel,classLabel2,cwLocId);
    }
   
    rc_t _write( ssf_t* p, bool show_muid_fl )
    {      
      rc_t rc = kOkRC;

      bool     fl       = false;
      unsigned offset_x = 0;
      
      for(unsigned i=0; i<p->refN; ++i)
      {
        ssf_ref_t*  r          = p->refA + i;
        const char* classLabel = r->tid==kLocTId ? "loc" : (r->tid==kIdTId ? "bar" : "pre");
        unsigned    id         = r->tid==kLocTId ? r->u.loc->index : (r->tid==kIdTId ? r->u.id : 0);

        if( fl==false && r->tid==kLocTId && r->u.loc->index == p->minMatchLoc )
        {
          fl       = true;
          offset_x = r->left - (BOX_W + BORD_X) ;
        }

        if( fl && r->tid==kLocTId && r->u.loc->index > p->maxMatchLoc )          
          fl = false;

        if( !fl && r->tid != kPreTId )
          continue;

        if( r->left < offset_x && r->tid != kPreTId )
          continue;

        // write ref rect
        if((rc = _write_rect_id( p, r->left-offset_x, r->top, classLabel, id )) != kOkRC )
        {
          rc = cwLogError(rc,"Error writing 'loc' rect id:%i.",r->u.loc->index);
          goto errLabel;
        }

        // write the ref-note 
        for(ssf_ref_note_t* rn=r->refNoteL; rn!=nullptr; rn = rn->link)
        {
          unsigned pitch = rn->evt == nullptr ? midi::kInvalidMidiPitch : rn->evt->pitch;
          unsigned cnt   = rn->cnt > 1 ? rn->cnt : kInvalidCnt;
          bool noMatchFl = rn->cnt == 0;
          if((rc = _write_rect_pitch( p, r->left-offset_x, rn->top, "ref_note", pitch, noMatchFl, cnt, rn->cwLocId )) != kOkRC )
          {
            rc = cwLogError(rc,"Error writing 'ref_note' rect.");
            goto errLabel;
          }          
        }
      }

      // write the pref-notes
      for(unsigned i=0; i<p->perfN; ++i)
      {
        ssf_perf_note_t* pn = p->perfA + i;

        bool isPreFl   = pn->refNote !=nullptr && pn->refNote->ref->tid == kPreTId;
        bool noMatchFl = pn->refNote==nullptr || isPreFl;

        // Rectangles whose 'left' coord is less than 'offset' occur prior to p->minMatchLoc
        // (but the 'pre' column is always shown)
        if( pn->left >= offset_x || isPreFl )
        {
          pn->isVisibleFl  = true;
          unsigned left    = isPreFl ? pn->left : pn->left-offset_x;
          unsigned perf_id = show_muid_fl ? pn->msg->muid : pn->seqId;
          
          if((rc = _write_rect_pitch( p, left, pn->top, "perf_note", pn->msg->pitch, noMatchFl, kInvalidCnt, perf_id )) != kOkRC )
          {
            rc = cwLogError(rc,"Error writing 'pref_note' rect.");
            goto errLabel;
          }
        }
      }

      for(unsigned i=0; i<p->perfN; ++i)
      {
        ssf_perf_note_t* pn = p->perfA + i;
        if( pn->isVisibleFl)
        {
          ssf_perf_note_t* pn0 = pn;
          for(ssf_perf_note_t* pn1=pn->dupl_list; pn1!=nullptr; pn1=pn1->dupl_link)
          {
            if( pn1->isVisibleFl )
            {
              line(p->svgH,pn0->left-offset_x,pn0->top,pn1->left-offset_x,pn0->top);
              pn0 = pn1;
            }            
          }
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
        mem::release(p->perfA);
        mem::release(p);
      }
      
      return rc;
    }

    ssf_ref_t*  _create_ref( ssf_t* p, unsigned& x_coord_ref, unsigned y_coord )
    {
      ssf_ref_t* ref = nullptr;
      if( p->refN < p->refAllocN )
      {        
        ref             = p->refA + p->refN;
        ref->left       = x_coord_ref; 
        ref->top        = y_coord;
        ref->col_bottom = y_coord + BOX_H + BORD_Y;
        
        p->refN += 1;

        if( p->refN == p->refAllocN )
          cwLogWarning("SVG score follower reference array cache is full.");

        x_coord_ref += (BOX_W+BORD_X);

      }
      return ref;
    }

    void _create_pre_ref( ssf_t* p, unsigned& x_coord_ref, unsigned y_coord )
    {
      ssf_ref_t* ref;
      if((ref = _create_ref(p,x_coord_ref,y_coord)) != nullptr )
      {
        ref->tid = kPreTId;
      }
    }
    
    void _create_bar_ref( ssf_t* p, const cmScoreEvt_t* e, unsigned& x_coord_ref, unsigned y_coord )
    {
      ssf_ref_t* ref;
      if((ref = _create_ref(p,x_coord_ref,y_coord)) != nullptr )
      {
        ref->tid = kIdTId;
        ref->u.id = e->barNumb;
      }
    }

    void _create_loc_ref( ssf_t* p, const cmScoreEvt_t* e, unsigned& x_coord_ref, unsigned y_coord )
    {
      ssf_ref_t* ref;
      if((ref = _create_ref(p,x_coord_ref,y_coord)) != nullptr )
      {
        ref->tid = kLocTId;
        ref->u.loc = cmScoreLoc(p->cmScH, e->locIdx );

        cwAssert( ref->u.loc != nullptr and ref->u.loc->index == e->locIdx );
      }
    }
    
    void _create_ref_array( ssf_t* p, unsigned& x_coord_ref, unsigned y_coord )
    {

      // create the cmScoreLoc array
      unsigned cmEvtN = cmScoreEvtCount(p->cmScH);
      unsigned loc_idx = kInvalidIdx;

      p->refAllocN = cmEvtN + 1;
      p->refA      = mem::allocZ<ssf_ref_t>(p->refAllocN);
      p->refN      = 0;

      _create_pre_ref(p, x_coord_ref, y_coord );      
      
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

      // reset the min and max loc index
      p->minMatchLoc = loc_idx == kInvalidIdx ? 0 : loc_idx;
      p->maxMatchLoc = 0;
    }

    rc_t _attach_ref_note( ssf_t* p, unsigned ref_idx, cmScoreEvt_t* evt )
    {
      rc_t rc = kOkRC;
      ssf_ref_note_t* rn = p->refNoteA + p->refNoteN + 1;
      
      rn->ref           = p->refA + ref_idx;
      rn->evt           = evt;
      rn->top           = rn->ref->col_bottom;
      rn->cnt           = 0;
      rn->cwLocId       = evt==nullptr ? 0 : evt->csvEventId;
      rn->link          = rn->ref->refNoteL;
      
      rn->ref->refNoteL = rn;      
      rn->ref->col_bottom += BOX_H + BORD_Y;
      
      p->refNoteN += 1;
      if( p->refNoteN >= p->refNoteAllocN )
      {
        rc = cwLogError(kBufTooSmallRC,"The ref note array in the SVG score writer is too small.");
        goto errLabel;
      }

    errLabel:
      return rc;
      
    }

    rc_t _create_ref_note_array( ssf_t* p )
    {
      rc_t rc = kOkRC;
      
      p->refNoteAllocN = p->refAllocN + 1;
      p->refNoteA = mem::allocZ<ssf_ref_note_t>(p->refNoteAllocN);
      p->refNoteN = 0;

      // attach the 'pre' ref note
      _attach_ref_note(p,PRE_REF_IDX,nullptr);

      for(unsigned ri=PRE_REF_IDX+1; ri<p->refN; ++ri)
        if( p->refA[ri].tid == kLocTId )
          for(unsigned j=0; j<p->refA[ri].u.loc->evtCnt; ++j)
            if( p->refA[ri].u.loc->evtArray[j]->type == kNonEvtScId && p->refNoteN < p->refNoteAllocN )
            {
              if((rc = _attach_ref_note(p,ri,p->refA[ri].u.loc->evtArray[j])) != kOkRC )
                goto errLabel;
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
        if( r->tid == kLocTId and r->u.loc->index == e->locIdx )
        {
          // locate the ref note associated with e
          for(rn=r->refNoteL; rn!=nullptr; rn=rn->link )
            if( rn->evt->index == e->index )
              break;
        }
      }

      // rn SHOULD NEVER BE null BUT THIS TEST FAILS FOR PRE-PERF NOTES: SEE:****

      
      //assert( rn != nullptr );
      return rn;
    }

    rc_t _setup_perf_note( ssf_t* p, ssf_ref_t* ref, ssf_ref_note_t* rn, ssf_ref_note_t* dupl_rn, const ssf_note_on_t* msg, ssf_perf_note_t*& pnRef )
    {
      rc_t rc = kOkRC;
      
      if( p->perfN >= p->perfAllocN )
      {
        rc = cwLogError(kBufTooSmallRC,"The perf_note array is too small.");
      }
      else
      {
        ssf_perf_note_t* pn = p->perfA + p->perfN;

        if( ref->tid==kPreTId)
          rn = ref->refNoteL;
        
        pn->left         = ref->left;
        pn->top          = ref->col_bottom;
        pn->refNote      = rn;
        pn->msg          = msg;
        pn->seqId        = p->perfN;
        ref->col_bottom += BOX_H + BORD_Y;
        
        p->perfN += 1;

        if( rn != nullptr )
          rn->cnt += 1; 

        pnRef = pn;
      }
      
      return rc;
    }
    
    rc_t _create_perf_array( ssf_t* p, const ssf_note_on_t* midiA, unsigned midiN )
    {
      rc_t rc = kOkRC;

      p->perfAllocN = midiN*2;
      p->perfA      = mem::allocZ<ssf_perf_note_t>(p->perfAllocN);
      p->perfN      = 0;

      ssf_ref_t* r0 = p->refA + PRE_REF_IDX;

      // for each performed MIDI note
      for(unsigned muid=0; muid<midiN; ++muid)
      {
        cmScoreEvt_t*    e   = nullptr;
        ssf_ref_note_t*  rn  = nullptr;
        ssf_ref_note_t*  rn0 = nullptr;
        ssf_perf_note_t* pn0 = nullptr;
        
        // look for a 'match' record that refers to this perf note
        // by interating through the matcher->res[] result array
        for(unsigned j=0; j<p->matcher->ri; ++j)
        {

          // if this matcher result record matches the perf'd MIDI note ...
          if( p->matcher->res[j].muid == muid )
          {
            // this result record matches this perf'd note - but it was not matched
            if( p->matcher->res[j].scEvtIdx == kInvalidIdx )
              continue;


            // **** PRE-PERF NOTES SHOULD NEVER HAVE A MATCH - BUT FOR SOME
            // REASON THE scEvtIdx on the PRE is 0 instead of kInvalidIdx
            // AND THEREFORE WE CAN END UP HERE EVEN THOUGH PRE PERF
            // RECORDS - BY DEFINITION - CANNOT HAVE A VALID MATCH
            
            // ... locate the score evt assoc'd with this 'match' record
            if((e = cmScoreEvt( p->cmScH, p->matcher->res[j].scEvtIdx )) != nullptr )
            {
              if((rn = _find_ref_note( p, e )) == nullptr )
                break;
              
              assert( rn != nullptr );
              
              ssf_perf_note_t* pn = nullptr;
              
              if((rc = _setup_perf_note(p,rn->ref,rn,rn0,midiA+muid,pn)) != kOkRC )
                goto errLabel;

              rn0 = rn;
              r0 = rn->ref;

              // maintain a list of pointers to duplicate performance notes
              if( pn0 == nullptr )
                pn0 = pn;
              else
              {
                pn->dupl_link = pn0;
                pn0->dupl_list = pn;
              }

              if( e->locIdx < p->minMatchLoc )
                p->minMatchLoc = e->locIdx;
              
              if( e->locIdx > p->maxMatchLoc )
                p->maxMatchLoc = e->locIdx;
              
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
        {
          assert( r0 != nullptr );
          ssf_perf_note_t* dummy = nullptr;
          if((rc = _setup_perf_note(p,r0,nullptr,nullptr,midiA+muid,dummy)) != kOkRC )
            goto errLabel;         
        }        
      }
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"create_perf_array failed.");
      
      return rc;
    }


    void _create_css( ssf_t* p )
    {
      install_css(p->svgH,".pre","fill",0xb0b0b0,"rgb");
      install_css(p->svgH,".bar","fill",0xa0a0a0,"rgb");
      install_css(p->svgH,".loc","fill",0xe0e0e0,"rgb");      
      install_css(p->svgH,".ref_note","fill",0x40E0D0,"rgb");      
      install_css(p->svgH,".perf_note","fill",0xE0FFFF,"rgb");
      install_css(p->svgH,".no_match","stroke",0xFF0000,"rgb");
      install_css(p->svgH,".multi_match","stroke",0x0000FF,"rgb");
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

      // create the performance array
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
                                                  const char*     out_fname,
                                                  bool            show_muid_fl)
{
  rc_t rc = kOkRC;
  ssf_t* p;

  if((p = _create(cmScH, matcher, midiA, midiN )) == nullptr )
  {
    rc = cwLogError(rc,"Score follower SVG writer initialization failed.");
    goto errLabel;
  }
  
  if((rc = _write(p,show_muid_fl)) != kOkRC )
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




