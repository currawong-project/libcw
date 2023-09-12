#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwFileSys.h"
#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfMatch.h"

namespace cw
{
  namespace sfmatch
  {
    
    sfmatch_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,sfmatch_t>(h); }

    rc_t _destroy( sfmatch_t* p )
    {
      rc_t rc = kOkRC;

      unsigned i;
      for(i=0; i<p->locN; ++i)
        mem::release(p->loc[i].evtV);
        
      mem::release(p->loc);
      mem::release(p->m);
      mem::release(p->p_mem);
        
      mem::release(p);
      return rc;
    };

    void _cmScMatchInitLoc( sfmatch_t* p )
    {
      unsigned li,ei;

      p->locN = event_count(p->scH);
      p->loc  = mem::resizeZ<loc_t>(p->loc,p->locN);


      // for each score location  
      for(li=0,ei=0; li<loc_count(p->scH); ++li)
      {
        unsigned i,n;

        const sfscore::loc_t* lp = loc_base(p->scH) + li;

        // count the number of note events at location li
        for(n=0,i=0; i<lp->evtCnt; ++i)
          if( lp->evtArray[i]->type == score_parse::kNoteOnTId )
            ++n;

        assert( ei+n <= p->locN );

        // duplicate each note at location li n times
        for(i=0; i<n; ++i)
        {
          unsigned j,k;

          p->loc[ei+i].evtCnt   = n;
          p->loc[ei+i].evtV     = mem::allocZ<event_t>(n);
          p->loc[ei+i].scLocIdx = li;
          p->loc[ei+i].barNumb  = lp->barNumb;

          for(j=0,k=0; j<lp->evtCnt; ++j)
            if( lp->evtArray[j]->type == score_parse::kNoteOnTId )
            {
              p->loc[ei+i].evtV[k].pitch    = lp->evtArray[j]->pitch;
              p->loc[ei+i].evtV[k].scEvtIdx = lp->evtArray[j]->index;
              ++k;
            }
        }

        ei += n;
      }

      assert(ei<=p->locN);
      p->locN = ei;
    }

    rc_t  _cmScMatchInitMtx( sfmatch_t* p, unsigned rn, unsigned cn )
    {
      //if( rn >p->mrn && cn > p->mcn )
      if( rn*cn > p->mrn*p->mcn )
      {
        return cwLogError(kInvalidArgRC, "MIDI sequence length must be less than %i. Score sequence length must be less than %i.",p->mmn,p->msn); 
      }

      // if the size of the mtx is not changing then there is nothing to do
      if( rn == p->rn && cn == p->cn )
        return kOkRC;

      // update the mtx size
      p->rn = rn;
      p->cn = cn;

      // fill in the default values for the first row
      // and column of the DP matrix
      unsigned i,j,k;
      for(i=0; i<rn; ++i)
        for(j=0; j<cn; ++j)
        {
          unsigned v[] = {0,0,0,0};

          if( i == 0 )
          {
            v[kSmMinIdx] = j;
            v[kSmInsIdx] = j;
          }
          else
            if( j == 0 )
            {
              v[kSmMinIdx] = i;
              v[kSmDelIdx] = i;
            }

          // zero the value field
          for(k=0; k<kSmCnt; ++k)
            p->m[ i + (j*rn) ].v[k] = v[k];      
        }

      return kOkRC;
    }

    value_t* _cmScMatchValPtr( sfmatch_t* p, unsigned i, unsigned j, unsigned rn, unsigned cn )
    {
      assert( i < rn && j < cn );

      return p->m + i + (j*rn);
    }

    bool _cmScMatchIsTrans( sfmatch_t* p, const midi_t* midiV, const value_t* v1p, unsigned bsi, unsigned i, unsigned j, unsigned rn, unsigned cn )
    {
      bool     fl  = false;
      value_t* v0p = _cmScMatchValPtr(p,i,j,rn,cn);

      if( i>=1 && j>=1
          && v1p->v[kSmMinIdx] == v1p->v[kSmSubIdx] 
          && cwIsNotFlag(v1p->flags,kSmMatchFl) 
          && v0p->v[kSmMinIdx] == v0p->v[kSmSubIdx]
          && cwIsNotFlag(v0p->flags,kSmMatchFl) 
          )
      {
        unsigned        c00 = midiV[i-1].pitch;
        unsigned        c01 = midiV[i  ].pitch;
        loc_t* c10 = p->loc + bsi + j - 1;
        loc_t* c11 = p->loc + bsi + j;
        fl = is_match(c11,c00) && is_match(c10,c01);
      }
      return fl;
    }

    unsigned _cmScMatchMin( sfmatch_t* p, unsigned i, unsigned j, unsigned rn, unsigned cn )
    { 
      return _cmScMatchValPtr(p,i,j,rn,cn)->v[kSmMinIdx];
    }

    // Return false if bsi + cn > p->locN
    // pitchV[rn-1]
    bool  _cmScMatchCalcMtx( sfmatch_t* p, unsigned bsi, const midi_t* midiV, unsigned rn, unsigned cn )
    {
      // loc[begScanLocIdx:begScanLocIdx+cn-1] must be valid
      if( bsi + cn > p->locN )
        return false;

      unsigned i,j;

      for(j=1; j<cn; ++j)
        for(i=1; i<rn; ++i)
        {
          loc_t*   loc      = p->loc + bsi + j - 1;
          unsigned pitch    = midiV[i-1].pitch;
          value_t* vp       = _cmScMatchValPtr(p,i,j,rn,cn);
          unsigned idx      = match_index(loc,pitch);
          vp->flags         = idx==kInvalidIdx ? 0            : kSmMatchFl;
          vp->scEvtIdx      = idx==kInvalidIdx ? kInvalidIdx : loc->evtV[idx].scEvtIdx;
          unsigned cost     = cwIsFlag(vp->flags,kSmMatchFl) ? 0 : 1;
          vp->v[kSmSubIdx]  = _cmScMatchMin(p,i-1,j-1, rn, cn) + cost;
          vp->v[kSmDelIdx]  = _cmScMatchMin(p,i-1,j  , rn, cn) + 1;
          vp->v[kSmInsIdx]  = _cmScMatchMin(p,i,  j-1, rn, cn) + 1;
          vp->v[kSmMinIdx]  = std::min( vp->v[kSmSubIdx], std::min(vp->v[kSmDelIdx],vp->v[kSmInsIdx]));
          vp->flags        |= _cmScMatchIsTrans(p,midiV,vp,bsi,i-1,j-1,rn,cn) ? kSmTransFl : 0;
        }

      return true;
    }

    void _cmScMatchPrintMtx( sfmatch_t* r, unsigned rn, unsigned cn)
    {
      unsigned i,j,k;
      for(i=0; i<rn; ++i)
      {
        for(j=0; j<cn; ++j)
        {
          printf("(");
      
          const value_t* vp = _cmScMatchValPtr(r,i,j,rn,cn);

          for(k=0; k<kSmCnt; ++k)
          {
            printf("%i",vp->v[k]);
            if( k<kSmCnt-1)
              printf(", ");
            else
              printf(" ");
          }

          printf("%c%c)",cwIsFlag(vp->flags,kSmMatchFl)?'m':' ',cwIsFlag(vp->flags,kSmTransFl)?'t':' ');
      
        }
        printf("\n");
      }
    }

    void _cmScMatchPathPush( sfmatch_t* r, unsigned code, unsigned ri, unsigned ci, unsigned flags, unsigned scEvtIdx )
    {
      assert(r->p_avl != NULL );

      path_t* p = r->p_avl;
      r->p_avl = r->p_avl->next;

      p->code    = code;
      p->ri      = ri;
      p->ci      = ci;
      p->flags   = code==kSmSubIdx && cwIsFlag(flags,kSmMatchFl) ? kSmMatchFl  : 0;
      p->flags  |= cwIsFlag(flags,kSmTransFl) ? kSmTransFl : 0;
      p->scEvtIdx= scEvtIdx;
      p->next    = r->p_cur;  
      r->p_cur   = p;
    }

    void _cmScMatchPathPop( sfmatch_t* r )
    {
      assert( r->p_cur != NULL );
      path_t* tp    = r->p_cur->next;
      r->p_cur->next = r->p_avl;
      r->p_avl       = r->p_cur;
      r->p_cur       = tp;
    }


    double _cmScMatchCalcCandidateCost( sfmatch_t* r )
    {
      path_t* cp = r->p_cur;
      path_t* bp = r->p_cur;
      path_t* ep = NULL;

      // skip leading inserts
      for(; cp!=NULL; cp=cp->next)
        if( cp->code != kSmInsIdx )
        {
          bp = cp;
          break;
        }
  
      // skip to trailing inserts
      for(; cp!=NULL; cp=cp->next)
        if( cp->code!=kSmInsIdx )
          ep = cp;

      // count remaining path length
      assert( ep!=NULL );
      unsigned n=1;
      for(cp=bp; cp!=ep; cp=cp->next)
        ++n;

      double   gapCnt  = 0;
      double   penalty = 0;
      bool     pfl     = cwIsFlag(bp->flags,kSmMatchFl);
      unsigned i;

      cp = bp;
      for(i=0; i<n; ++i,cp=cp->next)
      {
        // a gap is a transition from a matching subst. to an insert or deletion
        //if( pc != cp->code && cp->code != kSmSubIdx && pc==kSmSubIdx && pfl==true )
        if( pfl==true && cwIsFlag(cp->flags,kSmMatchFl)==false )
          ++gapCnt;

        //
        switch( cp->code )
        {
          case kSmSubIdx:
            penalty += cwIsFlag(cp->flags,kSmMatchFl) ? 0 : 1;
            penalty -= cwIsFlag(cp->flags,kSmTransFl) ? 1 : 0;
            break;

          case kSmDelIdx:
            penalty += 1;
            break;

          case kSmInsIdx:
            penalty += 1;
            break;
        }

        pfl = cwIsFlag(cp->flags,kSmMatchFl);
    
      }

      double cost = gapCnt/n + penalty;

      //printf("n:%i gaps:%f gap_score:%f penalty:%f score:%f\n",n,gapCnt,gapCnt/n,penalty,score);

      return cost;

    }

    double _cmScMatchEvalCandidate( sfmatch_t* r, double min_cost, double cost )
    {
  
      if( min_cost == DBL_MAX || cost < min_cost)
      {
        // copy the p_cur to p_opt[]
        path_t* cp = r->p_cur;
        unsigned         i;

        for(i=0; cp!=NULL && i<r->pn; cp=cp->next,++i)
        {
          r->p_opt[i].code    = cp->code;
          r->p_opt[i].ri      = cp->ri;
          r->p_opt[i].ci      = cp->ci;
          r->p_opt[i].flags   = cp->flags;
          r->p_opt[i].scEvtIdx= cp->scEvtIdx;
          r->p_opt[i].next    = cp->next==NULL ? NULL : r->p_opt + i + 1;
        }
    
        assert( i < r->pn );
        r->p_opt[i].code = 0; // terminate with code=0        
        min_cost         = cost;
      }

      return min_cost;
    }


    // NOTE: IF THE COST CALCULATION WAS BUILT INTO THE RECURSION THEN 
    // THIS FUNCTION COULD BE MADE MORE EFFICIENT BECAUSE PATHS WHICH
    // EXCEEDED THE min_cost COULD BE SHORT CIRCUITED.
    // 
    // traverse the solution matrix from the lower-right to 
    // the upper-left.
    double _cmScMatchGenPaths( sfmatch_t* r, int i, int j, unsigned rn, unsigned cn, double min_cost )
    {
      unsigned m;

      // stop when the upper-right is encountered
      if( i==0 && j==0 )
        return _cmScMatchEvalCandidate(r, min_cost, _cmScMatchCalcCandidateCost(r) );

      value_t* vp = _cmScMatchValPtr(r,i,j,rn,cn);

      // for each possible dir: up,left,up-left
      for(m=1; m<kSmCnt; ++m)
        if( vp->v[m] == vp->v[kSmMinIdx] )
        {
          // prepend to the current candidate path: r->p_cur
          _cmScMatchPathPush(r,m,i,j,vp->flags,vp->scEvtIdx);

          int ii = i-1;
          int jj = j-1;

          switch(m)
          {
            case kSmSubIdx:
              break;

            case kSmDelIdx:
              jj = j;
              break;

            case kSmInsIdx:
              ii = i;
              break;

            default:
              { assert(0); }
          }

          // recurse!
          min_cost = _cmScMatchGenPaths(r,ii,jj,rn,cn,min_cost);

          // remove the first element from the current path
          _cmScMatchPathPop(r);
        }

      return min_cost;
  
    }

    double _cmScMatchAlign( sfmatch_t* p, unsigned rn, unsigned cn, double min_cost )
    {
      int      i = rn-1;
      int      j = cn-1;
      unsigned m = _cmScMatchMin(p,i,j,rn,cn);

      if( m==std::max(rn,cn) )
        printf("Edit distance is at max: %i. No Match.\n",m);
      else
        min_cost = _cmScMatchGenPaths(p,i,j,rn,cn,min_cost);

      return min_cost;
    }

    void _cmScMatchMidiEvtFlags( sfmatch_t* p, const loc_t* lp, unsigned evtIdx, char* s, unsigned sn )
    {
      const sfscore::loc_t* slp = sfscore::loc_base(p->scH) + lp->scLocIdx;

      assert( evtIdx < slp->evtCnt );

      const sfscore::event_t* ep = slp->evtArray[evtIdx];
      unsigned            i  = 0;

      s[0] = 0;

      if( cwIsFlag(ep->flags,score_parse::kEvenVarFl) )
        s[i++] = 'e';

      if( cwIsFlag(ep->flags,score_parse::kTempoVarFl) )
        s[i++] = 't';

      if( cwIsFlag(ep->flags,score_parse::kDynVarFl) )
        s[i++] = 'd';

      //if( cwIsFlag(ep->flags,sfscore::kGraceScFl) )
      //  s[i++] = 'g';

      s[i++] = 0;

      assert( i <= sn );
  
    }


    void _gen_match_test_input_from_score( sfscore::handle_t scoreH, unsigned begLocIdx, unsigned locCnt, double srate )
    {
      printf("[\n");
      for(unsigned i=begLocIdx, k=0; i<begLocIdx+locCnt; ++i)
      {
        assert( begLocIdx < sfscore::loc_count(scoreH) );
        
        const sfscore::loc_t* loc = sfscore::loc_base(scoreH) + i;
        
        for(unsigned j=0; j<loc->evtCnt; ++j)
        {
          const sfscore::event_t* e = loc->evtArray[j];
          unsigned smpIdx = e->secs * srate;
          printf("{ muid:%i smpIdx:%i pitch:%i vel:%i },\n",k,smpIdx,e->pitch,e->vel);
          ++k;
        }
        
      }
      printf("]\n");

    }    
  }
}

cw::rc_t cw::sfmatch::create( handle_t& hRef, sfscore::handle_t scoreH, unsigned maxScWndN, unsigned maxMidiWndN )
{
  rc_t rc = kOkRC;
  sfmatch_t* p = nullptr;

  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  p = mem::allocZ<sfmatch_t>();
  
  p->scH  = scoreH;
  p->mrn  = maxMidiWndN + 1;
  p->mcn  = maxScWndN   + 1;
  p->mmn  = maxMidiWndN;
  p->msn  = maxScWndN;

  _cmScMatchInitLoc(p);

  p->m     = mem::resizeZ<value_t>( p->m, p->mrn*p->mcn );
  p->pn    = p->mrn + p->mcn;
  p->p_mem = mem::resizeZ<path_t>( p->p_mem, 2*p->pn );
  p->p_avl = p->p_mem;
  p->p_cur = NULL;
  p->p_opt = p->p_mem + p->pn;

  // put pn path records on the available list
  for(unsigned i=0; i<p->pn; ++i)
  {
    p->p_mem[i].next = i<p->pn-1 ? p->p_mem + i + 1 : NULL;
    p->p_opt[i].next = i<p->pn-1 ? p->p_opt + i + 1 : NULL;
  }

  hRef.set(p);
  
  return rc;  
}

cw::rc_t cw::sfmatch::destroy( handle_t& hRef  )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  sfmatch_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    goto errLabel;

 errLabel:
  return rc;
}

cw::rc_t cw::sfmatch::exec(  handle_t h, unsigned oLocId, unsigned locN, const midi_t* midiV, unsigned midiN, double min_cost )
{
  rc_t   rc;
  unsigned rn = midiN + 1;
  unsigned cn = locN  + 1;
  sfmatch_t* p = _handleToPtr(h);

  // set the DP matrix default values
  if((rc = _cmScMatchInitMtx(p, rn, cn )) != kOkRC )
    return rc;

  // _cmScMatchCalcMtx() returns false if the score window exceeds the length of the score
  if(!_cmScMatchCalcMtx(p,oLocId, midiV, rn, cn) )
    return kEofRC;

  //_cmScMatchPrintMtx(p,rn,cn);
  
  // locate the path through the DP matrix with the lowest edit distance (cost)
  p->opt_cost =  _cmScMatchAlign(p, rn, cn, min_cost);

  return rc;
}

unsigned cw::sfmatch::sync( handle_t h, unsigned i_opt, midi_t* midiBuf, unsigned midiN, unsigned* missCntPtr )
{
  sfmatch_t* p       = _handleToPtr(h);
  path_t*    cp      = p->p_opt;
  unsigned   missCnt = 0;
  unsigned   esi     = kInvalidIdx;
  unsigned   i;
  
  for(i=0; cp!=NULL; cp=cp->next)
  {
    // there is no MIDI note associated with 'inserts'
    if( cp->code != kSmInsIdx )
    {
      assert( cp->ri > 0 );
      midiBuf[ cp->ri-1 ].oLocId = kInvalidIdx;
    }

    switch( cp->code )
    {
      case kSmSubIdx:
        midiBuf[ cp->ri-1 ].oLocId   = i_opt + i;
        midiBuf[ cp->ri-1 ].scEvtIdx = cp->scEvtIdx;

        if( cwIsFlag(cp->flags,kSmMatchFl) )
        {
          esi     = i_opt + i;
          missCnt = 0;
        }
        else
        {
          ++missCnt;
        }
        // fall through

      case kSmInsIdx:
        cp->oLocId = i_opt + i;
        ++i;
        break;

      case kSmDelIdx:
        cp->oLocId = kInvalidIdx;
        ++missCnt;
        break;
    }
  }

  if( missCntPtr != NULL )
    *missCntPtr = missCnt;

  return esi;
}

void cw::sfmatch::print_path( handle_t h, unsigned bsi, const midi_t* midiV )
{
  assert( bsi != kInvalidIdx );

  sfmatch_t* p     = _handleToPtr(h);
  path_t*    cp    = p->p_opt;
  path_t*    pp    = cp;
  int        polyN = 0;
  int        i;

  printf("loc: ");

  // get the polyphony count for the score window 
  for(i=0; pp!=NULL; pp=pp->next)
  {
    loc_t* lp = p->loc + bsi + pp->ci;
    if( pp->code!=kSmDelIdx  )
    {
      if(lp->evtCnt > (unsigned)polyN)
        polyN = lp->evtCnt;

      printf("%4i%4s ",bsi+i," ");
      ++i;
    }
    else
      printf("%4s%4s "," "," ");
  }

  printf("\n");

  // print the score notes
  for(i=polyN; i>0; --i)
  {
    printf("%3i: ",i);
    for(pp=cp; pp!=NULL; pp=pp->next)
    {

      if( pp->code!=kSmDelIdx )
      {
        int oLocId = bsi + pp->ci - 1;
        assert(0 <= oLocId && oLocId <= (int)p->locN);
        loc_t* lp = p->loc + oLocId;

        if( lp->evtCnt >= (unsigned)
            i )
        {
          unsigned sn = 6;
          char s[sn];
          _cmScMatchMidiEvtFlags(p,lp,i-1,s,sn );
          printf("%4s%-4s ",midi::midiToSciPitch(lp->evtV[i-1].pitch,NULL,0),s);
        }
        else
          printf("%4s%4s "," "," ");
      }
      else
        printf("%4s%4s ", (pp->code==kSmDelIdx? "-" : " ")," ");

      /*
        int oLocId = bsi + pp->ci - 1;
        assert(0 <= oLocId && oLocId <= p->locN);
        loc_t* lp = p->loc + oLocId;
        if( pp->code!=kSmDelIdx && lp->evtCnt >= i )
        printf("%4s ",cmMidiToSciPitch(lp->evtV[i-1].pitch,NULL,0));
        else
        printf("%4s ", pp->code==kSmDelIdx? "-" : " ");
      */
    }
    printf("\n");
  }

  printf("mid: ");

  // print the MIDI buffer
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    if( pp->code!=kSmInsIdx )
      printf("%4s%4s ",midi::midiToSciPitch(midiV[pp->ri-1].pitch,NULL,0)," ");
    else
      printf("%4s%4s ",pp->code==kSmInsIdx?"-":" "," ");
  }

  printf("\nvel: ");

  // print the MIDI velocity
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    if( pp->code!=kSmInsIdx )
      printf("%4i%4s ",midiV[pp->ri-1].vel," ");
    else
      printf("%4s%4s ",pp->code==kSmInsIdx?"-":" "," ");
  }

  printf("\nmni: ");

  // print the MIDI buffer index (mni)
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    if( pp->code!=kSmInsIdx )
      printf("%4i%4s ",midiV[pp->ri-1].mni," ");
    else
      printf("%4s%4s ",pp->code==kSmInsIdx?"-":" "," ");
  }

  printf("\n op: ");

  // print the substitute/insert/delete operation
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    char c = ' ';
    switch( pp->code )
    {
      case kSmSubIdx: c = 's'; break;
      case kSmDelIdx: c = 'd'; break;
      case kSmInsIdx: c = 'i'; break;
      default:
        { assert(0); }
    }

    printf("%4c%4s ",c," ");
  }

  printf("\n     ");

  // give substitute attribute (match or transpose)
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    char s[3];
    int  k = 0;
    if( cwIsFlag(pp->flags,kSmMatchFl) )
      s[k++] = 'm';

    if( cwIsFlag(pp->flags,kSmTransFl) )
      s[k++] = 't';

    s[k]   = 0;

    printf("%4s%4s ",s," ");
  }

  printf("\nscl: ");

  // print the stored location index
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    if( pp->oLocId == kInvalidIdx )
      printf("%4s%4s "," "," ");
    else
      printf("%4i%4s ",p->loc[pp->oLocId].scLocIdx," ");
  }
  
  printf("\nbar: ");

  // print the stored location index
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    if( pp->oLocId==kInvalidIdx || pp->scEvtIdx==kInvalidIdx )
      printf("%4s%4s "," "," ");
    else
    {
      const sfscore::event_t* ep = sfscore::event(p->scH, pp->scEvtIdx );
      printf("%4i%4s ",ep->barNumb," ");
    }
  }


  printf("\nsec: ");

  // print seconds
  unsigned begSmpIdx = kInvalidIdx;
  for(pp=cp; pp!=NULL; pp=pp->next)
  {
    if( pp->code!=kSmInsIdx )
    {
      if( begSmpIdx == kInvalidIdx )
        begSmpIdx = midiV[pp->ri-1].smpIdx;

      printf("%2.2f%4s ", (double)(midiV[pp->ri-1].smpIdx - begSmpIdx)/96000.0," ");
      
    }
    else
      printf("%4s%4s ",pp->code==kSmInsIdx?"-":" "," ");


  }

  
  printf("\n\n");

}

unsigned cw::sfmatch::loc_to_index( handle_t h, unsigned loc )
{
  sfmatch_t* p = _handleToPtr(h);
  const loc_t* y = std::find_if(p->loc, p->loc+p->locN, [&loc](const loc_t& x){return x.scLocIdx==loc;} );
  unsigned idx = y - p->loc;
  return idx<p->locN ? idx : kInvalidIdx;
}



cw::rc_t cw::sfmatch::test( const object_t* cfg, sfscore::handle_t scoreH )
{
  rc_t            rc                   = kOkRC;
  const object_t* perf                 = nullptr;
  const object_t* gen_perf_example     = nullptr;
  bool            gen_perf_enable_fl   = false;
  unsigned        gen_perf_beg_loc_idx = 0;
  unsigned        gen_perf_loc_cnt     = 0;
  midi_t*         midiA                = nullptr;
  unsigned        maxScWndN            = 10;
  unsigned        maxMidiWndN          = 7;
  unsigned        init_score_loc       = 0;
  unsigned        beg_perf_idx         = 0;
  unsigned        perf_cnt             = 6;
  sfmatch::handle_t   matchH;
  
  // parse the test cfg
  if((rc = cfg->getv( "maxScWndN", maxScWndN,
                      "maxMidiWndN", maxMidiWndN,
                      "gen_perf_example", gen_perf_example,
                      "init_score_loc",init_score_loc,
                      "beg_perf_idx", beg_perf_idx,
                      "perf_cnt", perf_cnt,
                      "perf", perf)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore test parse params failed.");
    goto errLabel;
  }

  if((rc = gen_perf_example->getv( "enable_fl",   gen_perf_enable_fl,
                                   "beg_loc_idx", gen_perf_beg_loc_idx,
                                   "loc_cnt",     gen_perf_loc_cnt )) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore test parse params 'gen_perf_example' failed.");
    goto errLabel;
  }

  if( gen_perf_enable_fl )
  {
    _gen_match_test_input_from_score( scoreH, gen_perf_beg_loc_idx, gen_perf_loc_cnt, sample_rate(scoreH) );
  }
  else
  {

    // Parse the cfg. perf[] array
    unsigned midiN = perf->child_count();
    midiA = mem::allocZ<midi_t>(midiN);
    
    for(unsigned i=0; i<midiN; ++i)
    {
      const object_t* node = perf->child_ele(i);
      
      if((rc = node->getv("muid",midiA[i].muid,
                          "smpIdx",midiA[i].smpIdx,
                          "pitch", midiA[i].pitch,
                          "vel", midiA[i].vel)) != kOkRC )
      {
        rc = cwLogError(rc,"parsing 'perf' record at index '%i' failed.",i);
        goto errLabel;
      }
    }

    // Create the cwMatch
    if((rc = create(matchH, scoreH, maxScWndN, maxMidiWndN )) != kOkRC )
    {
      rc = cwLogError(rc,"Score matcher create failed.");
      goto errLabel;
    }

    // Set the matcher to the first location to begin matching against
    unsigned oLocId = loc_to_index(matchH,init_score_loc);
    unsigned locN   = maxScWndN;

    // Count of MIDI events to track.
    perf_cnt = std::min(maxMidiWndN,perf_cnt);

    // Align MIDI notes to the score
    if((rc = exec(matchH, oLocId, locN, midiA + beg_perf_idx, perf_cnt, DBL_MAX )) != kOkRC )
    {
      rc = cwLogError(rc,"score match failed.");
      goto errLabel;
    }

    // Update the cwMatch to reflect the alignment found during the exec. 
    unsigned eli     = kInvalidIdx;
    unsigned missCnt = 0;
    unsigned i_opt   = oLocId;
    if(( eli = cw::sfmatch::sync( matchH, i_opt, midiA + beg_perf_idx, perf_cnt, &missCnt )) == kInvalidIdx )
    {
      rc = cwLogError(rc,"score match sync failed.");
      goto errLabel;
    }

    // Print the state alignment.
    unsigned bsi = oLocId;
    print_path( matchH, bsi, midiA );
    
  }
  
 errLabel:
  destroy(matchH);
  mem::release(midiA);
  
  return rc;
}
