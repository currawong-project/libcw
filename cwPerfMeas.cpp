#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwPerfMeas.h"
#include "cwVectOps.h"

/*

1. if a performed event is part of a set then mark the set complete when all events in it have been performed.
2. when a set is marked as complete it should be evaluated immediately.
3. once a complete set is evaluated it is never evaluated again (although incomplete sets may be evaluated again)

4. if a performed location is marked as a 'calc' location and all sets have been evaluated then the calc. is executed.
5. if a performed location is past the next-calc-loc then the calc is executed.
6. if a calc has already been executed and an event that is part of the calc is recieved then the
associated set is updated and the calc is executed again.

7. if a performed event occurs on a trigger location then the section update is executed.
8. if a performed event occurs on a trigger location that has already been executed then the section update is NOT
performed again.
  
 */

namespace cw
{
  namespace perf_meas
  {
    struct calc_str;
    
    typedef struct section_str
    {
      
      const struct section_str* prev_section; // section prior to this section
      const sfscore::section_t* section;      // score section this section_t represents
      struct calc_str*          calc;         // calc record for this section
      bool                      triggeredFl;  // This section has already been triggered
    } section_t;
    
    typedef struct set_str
    {
      const sfscore::set_t* set;   // score set this set_t represents
      unsigned              lastPerfUpdateCnt; 
      double                value; // The value associated with this set. DBL_MAX on initialization
      struct calc_str*      calc;  // calc record to which this set belongs
      struct set_str*       alink; // perf_meas_t* links
      struct set_str*       slink; // loc_t.setL links
      struct set_str*       clink; // calc_t.setL links
    } set_t;

    // The 'calc_t' record hold pointers to all the sets assigned to a given section.
    // The record is different from a section record because it has a location assignment
    // prior to the section which it is represents. This allows all the sets for
    // a given section to be evaluated prior to the section being triggered.
    // The 'calc' record is assigned to the location of the last event in it's latest set. 
    typedef struct calc_str
    {
      set_t*     setL;             // list of sets that this calc is applied to (links via set_t.clink)
      section_t* section;          // section where this calc is applied
      double     value[ kValCnt ]; // Aggregate var values for this section
    } calc_t;
    
    typedef struct
    {
      unsigned   locId;         // oloc location id and the index of this record into p->locA[]
      section_t* section;       // section that begins on this location
      set_t*     setL;          // sets that end on this location (links via loc_t.slink)
      calc_t*    calc;          // calc that can be completed on this location.
    } loc_t;
    
    typedef struct perf_meas_str
    {
      sfscore::handle_t scoreH;

      loc_t*            locA;   // locA[locN] 
      unsigned          locN;   // length of locA[] 
      set_t*            setL;   // sets linked on alink

      // Location index of the next section to be triggered.
      unsigned          last_section_loc_idx;

      // Location of the next calc record to be evaluated.
      unsigned          next_section_loc_idx; 

      unsigned          next_calc_loc_idx;
      
    } perf_meas_t;

    perf_meas_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,perf_meas_t>(h); }

    inline unsigned _loc_id_to_index( perf_meas_t* p, unsigned locId )
    {
      if( locId >= p->locN  )
        return kInvalidIdx;
      
      assert( locId == p->locA[locId].locId );
      
      return locId;
    }
    
    rc_t _destroy( perf_meas_t* p )
    {
      rc_t rc = kOkRC;

      for(unsigned i=0; i<p->locN; ++i)
      {
        set_t* s = p->locA[i].setL;
        while(s!=nullptr)
        {
          set_t* s0 = s->slink;
          mem::release(s);
          s = s0;
        }
        
        mem::release(p->locA[i].section);
        mem::release(p->locA[i].calc);
      }

      mem::release(p->locA);
      mem::release(p);
      return rc;
    }

    set_t* _find_set( perf_meas_t* p, unsigned setId )
    {
      for(set_t* s=p->setL; s!=nullptr; s=s->alink)
        if( s->set->id == setId )
          return s;

      cwLogError(kInvalidIdRC,"The setId '%i' is not valid.", setId );
      return nullptr;
    }
    
    calc_t* _create_calc_record( perf_meas_t* p, sfscore::section_t* section )
    {
      rc_t rc = kOkRC;

      calc_t* calc = mem::allocZ<calc_t>();
      
      for(unsigned i=0; i<section->setCnt; ++i)
      {
        set_t* s;
        if((s = _find_set(p,section->setArray[i]->id)) == nullptr )
        {
          rc = cwLogError(kInvalidIdRC,"Error forming section set list for the section '%s'.",cwStringNullGuard(section->label));
          goto errLabel;
        }

        s->calc  = calc;
        s->clink = calc->setL;
        calc->setL = s;        
      }

    errLabel:

      if( rc != kOkRC )
        mem::release(calc);
      
      return calc;
    }

    void _advance_next_calc_and_section_indexes( perf_meas_t* p, unsigned cur_section_idx )
    {
      p->last_section_loc_idx = cur_section_idx;
      p->next_calc_loc_idx    = kInvalidIdx;
      p->next_section_loc_idx = kInvalidIdx;
      
      // Set 'last_section_loc_idx' and 'next_calc_loc_idx'
      for(unsigned i=cur_section_idx+1; i<p->locN && (p->next_section_loc_idx==kInvalidIdx || p->next_calc_loc_idx==kInvalidIdx); ++i)
      {
        if( p->next_section_loc_idx == kInvalidIdx && p->locA[i].section != nullptr )
          p->next_section_loc_idx = i;

        if( p->next_calc_loc_idx == kInvalidIdx && p->locA[i].calc != nullptr )
          p->next_calc_loc_idx = i;
      }

      if( p->next_section_loc_idx == kInvalidIdx )
        cwLogInfo("End-of-score reached on section index.");
      else
        if( p->next_calc_loc_idx == kInvalidIdx )
          cwLogWarning("No 'next calc index' was found for the section at location:%i",p->next_section_loc_idx);
    }

    void _reset_loc( loc_t& loc)
    {
      if( loc.section != nullptr )
        loc.section->triggeredFl = false;
      
      if( loc.calc != nullptr )
      {
        for(set_t* set = loc.calc->setL; set!=nullptr; set=set->clink)
        {
          set->value = std::numeric_limits<double>::max();
          set->lastPerfUpdateCnt = 0;
        }
        std::for_each(loc.calc->value,loc.calc->value+kValCnt,[](double& x){ x=std::numeric_limits<double>::max(); });
      }
    }
    
    rc_t _reset( perf_meas_t* p, unsigned init_locId )
    {
      rc_t     rc = kOkRC;
      unsigned i  = 0;

      unsigned init_loc_idx = kInvalidIdx;
      
      for(; i<p->locN; ++i)
      {
        if( p->locA[i].locId == init_locId )
          init_loc_idx = i;

        // reset all locations at and after the init. loc
        if( init_loc_idx != kInvalidIdx )
          _reset_loc(p->locA[i]);
        
      }
      
      if( init_loc_idx == kInvalidIdx )
      {
        rc = cwLogError(kInvalidIdRC,"The initial location id '%i' is not valid.",init_locId);
        goto errLabel;
      }

      _advance_next_calc_and_section_indexes(p,init_loc_idx);
  
    errLabel:
      return rc;
    }

    double _calc_set_list_mean( const set_t* setL, unsigned varTypeId )
    {
      double sum = 0.0;
      unsigned n = 0;
      
      for(const set_t* s=setL; s!=nullptr; s=s->clink)
        if( s->set->varId == varTypeId && s->value != std::numeric_limits<double>::max() )          
        {
          sum += s->value;
          n   += 1;
        }

      return n>0 ? sum/n : 0.0;
    }

    void _eval_one_dynamic_set(perf_meas_t* p, set_t* set )
    {
      double sum = 0;
      unsigned n = 0;
      for(unsigned i=0; i<set->set->evtCnt; ++i)
      {
        const sfscore::event_t* e = set->set->evtArray[i];
        if( e->perfFl )
        {
          double d = e->dynLevel>e->perfDynLevel ? e->dynLevel - e->perfDynLevel : e->perfDynLevel - e->dynLevel;
          sum += d * d;
          n += 1;
        }
      }

      set->value = n==0 ? 0 : sqrt(sum/n);
    }

    // Calc chord onset time as the avg. onset over of all notes in the chord.
    rc_t _calc_loc_sec( const sfscore::set_t* set, double* locV, unsigned* locNV, unsigned& evtSkipN_Ref )
    {
      rc_t rc = kOkRC;

      evtSkipN_Ref = 0;
      
      vop::zero(locV,  set->locN);
      vop::zero(locNV, set->locN);
      
      // Get the time for each location by taking the mean
      // of all events on the same location.
      unsigned li       = 0;
      unsigned curLocId = set->evtArray[0]->oLocId;
      
      for(unsigned i=0; i<set->evtCnt; ++i)
      {
        if( !set->evtArray[i]->perfFl )
        {
          ++evtSkipN_Ref;
        }
        else
        {        
          if( curLocId != set->evtArray[i]->oLocId )
          {
            li += 1;
            curLocId = set->evtArray[i]->oLocId;
          }

          if( li >= set->locN )
          {
            rc = cwLogError(kAssertFailRC,"An invalid location id was encountered %i >= %i.",li,set->locN);
            goto errLabel;
          }

          locV[ li] += set->evtArray[i]->perfSec;
          locNV[li] += 1;
        }
      }

      // Calc. mean.
      for(unsigned i=0; i<set->locN; ++i)
        if( locNV[i] > 0 )
          locV[i] /= locNV[i];
      
    errLabel:
      return rc;
    }

    void _interpolate_time_of_missing_notes( perf_meas_t* p, const sfscore::set_t* set, double* locV, unsigned* locNV, bool* statusV, unsigned& bi_ref, unsigned& ei_ref, unsigned& insertN_ref )
    {
      bi_ref      = kInvalidIdx;
      ei_ref      = kInvalidIdx;
      insertN_ref = 0;
      
      unsigned missN = 0;
      unsigned ei    = kInvalidIdx;
      unsigned bi    = kInvalidIdx;
      
      vop::fill(statusV, set->locN, false);
   
      // for each location
      for(unsigned i=0; i<set->locN; ++i)
      {
        // if this location was missed or out of time order
        if( locNV[i] == 0 || (ei != kInvalidIdx && (locV[i]-locV[ei])<0)  )
        {
          missN += 1;
        }
        else
        {
          // if there are unplayed notes between this note
          // and the last played note at 'ei' then 
          // fill in the missing note times by splitting
          // the gap time evenly - note that this will
          // bias the evenness std to be lower than it
          // should be.
          if( missN > 0 && ei!=kInvalidIdx )
          {            
            double dsec = (locV[i] - locV[ei])/(missN+1);
            for(unsigned j=ei+1; j<i; ++j)
            {
              locV[j] = locV[j-1] + dsec;
              insertN_ref += 1;
            }
          }

          if( bi == kInvalidIdx )
            bi = i;
          
          statusV[i] = true;
          missN = 0;
          ei    = i;  // ei=last valid location in locV[]
        }                
      }
      
      bi_ref = bi;
      ei_ref = ei;
    }

    rc_t _eval_one_even_set(perf_meas_t* p, set_t* pm_set )
    {
      rc_t rc = kOkRC;
      
      const sfscore::set_t* set = pm_set->set;
      
      double   locV[    set->locN ]; 
      unsigned locNV[   set->locN ];
      double   stdV[    set->locN ];
      bool     statusV[ set->locN ];
      
      unsigned bi       = kInvalidIdx;
      unsigned ei       = kInvalidIdx;
      unsigned insertN  = 0;
      unsigned evtSkipN = 0;
      unsigned locSkipN = 0;

      if((rc = _calc_loc_sec(set,locV,locNV,evtSkipN)) != kOkRC )
        goto errLabel;

      _interpolate_time_of_missing_notes(p, set, locV, locNV, statusV, bi, ei, insertN );
      
      vop::zero(stdV,     set->locN);

      // Calc the std. deviation of the note delta times
      // of all notes [bi:ei].  Note that if the notes
      // before bi/after ei  were skipped then they are
      // left out of the calculation.  However skipped notes
      // interal to the range bi:ei were interpolated
      // as perfectly even.
      
      if( (ei - bi)+1 > 2 )
      {
        // calc the delta time for each time in locV[]
        unsigned stdN = 0;
        for(unsigned i=bi+1; i<=ei; ++i)
          stdV[stdN++] = locV[i] - locV[i-1];
        
        
        printf("Skipped evt:%i Skipped locs:%i Insert:%i bi:%i ei:%i final N:%i\n",evtSkipN,locSkipN,insertN,bi,ei,stdN);
        vop::print( locV,  set->locN, "%f ", "locV:" );
        vop::print( locNV, set->locN, "%i ", "locN:" );
        vop::print( stdV, stdN, "%f ", "std:" );
        vop::print( statusV, set->locN, "%i ", "ok:");

            
        pm_set->value = vop::std(stdV, stdN );
      }

    errLabel:
      return rc;      
    }

    
    void _eval_one_tempo_set(perf_meas_t* p, set_t* set )
    {
      set->value = 3.0;
    }

    void _aggregate_dynamic_meas_set_values( perf_meas_t* p, calc_t* calc )
    {
      calc->value[ kDynValIdx ] = _calc_set_list_mean( calc->setL, score_parse::kDynVarIdx );
    }
    
    void _aggregate_even_meas_set_values( perf_meas_t* p, calc_t* calc )
    {
      calc->value[ kEvenValIdx ] = _calc_set_list_mean( calc->setL, score_parse::kEvenVarIdx );
    }
    
    void _aggregate_tempo_meas_set_values( perf_meas_t* p, calc_t* calc )
    {
      calc->value[ kTempoValIdx ] = _calc_set_list_mean( calc->setL, score_parse::kTempoVarIdx );
    }

    void _eval_cost_calc( perf_meas_t* p, calc_t* calc )
    {
      if( calc->section->prev_section != nullptr )
      {
        unsigned              beg_loc_idx = calc->section->prev_section->section->locPtr->index;
        unsigned              end_loc_idx = calc->section->section->locPtr->index;
        const sfscore::loc_t* loc         = loc_base(p->scoreH) + beg_loc_idx;
        const sfscore::loc_t* loc_end     = loc_base(p->scoreH) + end_loc_idx;
        double                sum         = 0;
        unsigned              n           = 0;
        
        for(; loc<loc_end; ++loc)
          for(unsigned i=0; i<loc->evtCnt; ++i)
            if( loc->evtArray[i]->perfFl && loc->evtArray[i]->perfMatchCost != std::numeric_limits<double>::max() )
            {
              sum += loc->evtArray[i]->perfMatchCost;
              n   += 1;
            }
        
        
        calc->value[ kMatchCostValIdx ] = sum / n;
      }
    }

    set_t* _use_slink(set_t* s){ return s->slink; }
    set_t* _use_clink(set_t* s){ return s->clink; }

    // Set 'force_fl' to true if the sets should be evaluated even if they are not complete.
    void _eval_set_list( perf_meas_t* p, set_t* setL, set_t* (*link_func)(set_t*)  )
    {
      // for each set at this location
      for(set_t* s=setL; s!=nullptr; s=link_func(s))
      {
        // if this set has not been eval'd or has been updated with new perf. information since it was last eval'd
        if( s->lastPerfUpdateCnt==0 || s->set->perfUpdateCnt > s->lastPerfUpdateCnt )
        {
          s->lastPerfUpdateCnt = s->set->perfUpdateCnt;

          cwLogInfo("Set %i eval.",s->set->id);

          switch( s->set->varId )
          {
            case score_parse::kDynVarIdx:
              _eval_one_dynamic_set(p,s);
              break;
              
            case score_parse::kEvenVarIdx:
              _eval_one_even_set(p,s);
              break;
              
            case score_parse::kTempoVarIdx:
              _eval_one_tempo_set(p,s);
              break;

            default:
              cwLogError(kInvalidIdRC,"Unknown var type (%i) encountered while evaluating sets.",s->set->varId );
              assert(0);
          }
        }
      }
    }              
    
    rc_t _update_sets( perf_meas_t* p, unsigned loc_idx )
    {
      rc_t rc = kOkRC;
      if( p->locA[loc_idx].setL != nullptr && are_all_loc_set_events_performed( p->scoreH, loc_idx ) )
        _eval_set_list(p,p->locA[loc_idx].setL,_use_slink);
      
      return rc;      
    }

    rc_t _update_calc( perf_meas_t* p, unsigned loc_idx )
    {
      rc_t rc = kOkRC;
      // if the loc is past the next_calc_loc_idx or is equal to next_calc_loc_idx and all the set events have been performed for the location
      if( p->next_calc_loc_idx != kInvalidIdx
          && (loc_idx > p->next_calc_loc_idx
              || (loc_idx == p->next_calc_loc_idx
                  && are_all_loc_set_events_performed( p->scoreH, loc_idx ))))
      {

        cwLogInfo("Calc: Loc:%i  nci:%i",loc_idx,p->next_calc_loc_idx );
        
        calc_t* calc = p->locA[ p->next_calc_loc_idx ].calc;
        assert( calc != nullptr );

        // eval any sets that have not already been evaluated - even if they are not complete
        _eval_set_list(p,calc->setL, _use_clink);

        // aggregate the measurements from each measurement type into a single scalar values.
        _aggregate_dynamic_meas_set_values(p,calc);
        _aggregate_even_meas_set_values(p,calc);
        _aggregate_tempo_meas_set_values(p,calc);
      }
      return rc;
    }

    rc_t _update_section( perf_meas_t* p, unsigned loc_idx, result_t& resultRef )
    {
      rc_t rc = kOkRC;

      // if next_section_loc_idx == kInvalidIdx then the end of the score has been encountered
      // (or we are in an invalid state)
      if( p->next_section_loc_idx == kInvalidIdx )
      {
        cwLogWarning("End-of-score or invalid state encountered (next_section_loc_idx==kInvalidIdx).");
        goto errLabel;
      }

      // TODO: Should the following be a loop which iterates
      // until p->next_section_loc_idx > loc_idx
      
      if( p->next_section_loc_idx <= loc_idx )
      {      
        assert( p->locA[p->next_section_loc_idx].section != nullptr );

        section_t* section = p->locA[p->next_section_loc_idx].section;

        if( section->triggeredFl == false )
        {
          assert( section->section != nullptr && section->section->locPtr != nullptr );
          
          resultRef.loc          = loc_idx;
          resultRef.sectionLoc   = section->section->locPtr->index;
          resultRef.sectionLabel = section->section->label;
          if( section->calc == nullptr )
            cwLogWarning("No sets assigned to section %s",cwStringNullGuard(section->section->label));
          else
          {
            resultRef.valueA       = section->calc->value;
            resultRef.valueN       = kValCnt;
          }
        
          section->triggeredFl = true;
          
          _advance_next_calc_and_section_indexes(p,p->next_section_loc_idx);

          //cwLogInfo("Section %s triggered.",cwStringNullGuard(resultRef.sectionLabel));
          
        }
      }
    errLabel:
      return rc;
    }
    
  }
}

cw::rc_t cw::perf_meas::create( handle_t& hRef, sfscore::handle_t scoreH )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  perf_meas_t*          p            = mem::allocZ<perf_meas_t>();
  const sfscore::loc_t* locA         = sfscore::loc_base(scoreH);
  const section_t*      prev_section = nullptr;
  
  p->scoreH               = scoreH;
  p->locN                 = loc_count(scoreH);
  p->locA                 = mem::allocZ<loc_t>(p->locN);
  p->next_section_loc_idx = kInvalidIdx;
  p->next_calc_loc_idx    = kInvalidIdx;
  
  // for each score location
  for(unsigned i=0; i<p->locN; ++i)
  {
    // the index of the record into p->locA[]
    // is the same as the locId of the associated location
    assert( i == locA[i].index );
    
    p->locA[i].locId = locA[i].index;

    // if this location is the end of a set (or sets)  ...
    for(sfscore::set_t* s=locA[i].setList; s!=nullptr; s=s->llink)
    {
      // ... link the sets onto this location record
      set_t* set      = mem::allocZ<set_t>();
      set->set        = s;
      set->slink      = p->locA[i].setL;
      p->locA[i].setL = set;
      set->alink      = p->setL;
      p->setL         = set;
    }
    
    // if this location is the start of a new section
    if( locA[i].begSectPtr != nullptr )
    {
      p->locA[i].section               = mem::allocZ<section_t>();
      p->locA[i].section->prev_section = prev_section;
      p->locA[i].section->section      = locA[i].begSectPtr;
      prev_section                     = p->locA[i].section;
      
      if(locA[i].begSectPtr->measLocPtr != nullptr )
      {
        // get the loc of the last event in the last set that applies to this section
        // (this is where the sets that supply this section will be evaluated)
        unsigned calc_loc_idx = locA[i].begSectPtr->measLocPtr->index;
        assert( calc_loc_idx < p->locN);

        // verify that the 'calc' location is <= the section location
        if( calc_loc_idx > locA[i].index )
        {
          rc = cwLogError(kInvalidStateRC,"The last loc %i of the last set in section '%s' is after the section start at loc: %i.", calc_loc_idx, cwStringNullGuard(locA[i].begSectPtr->label),i);
          goto errLabel;
        }

        // verify that that the calc location is available
        if( p->locA[ calc_loc_idx ].calc != nullptr )
        {
          rc = cwLogError(kInvalidStateRC,"A given location (%i) may only have one 'calc' record.",calc_loc_idx);
          goto errLabel;
        }
        
        // create the 'calc' record preceding this section 
        if(( p->locA[ calc_loc_idx ].calc = _create_calc_record( p, locA[i].begSectPtr )) == nullptr )
        {
          rc = cwLogError(kInvalidIdRC,"The 'calc' object create failed at location %i",calc_loc_idx);
          goto errLabel;
        }

        // set the calc.section pointer and the section.calc pointer
        p->locA[ calc_loc_idx ].calc->section = p->locA[i].section;
        p->locA[i].section->calc              = p->locA[ calc_loc_idx ].calc;
      }      
    }
  }

  if((rc = _reset(p,0)) != kOkRC )
  {
    rc = cwLogError(rc,"Perf. meas initial reset failed.");
    goto errLabel;
  }
  
  hRef.set(p);

 errLabel:
  if(rc != kOkRC )
    _destroy(p);
  
  return rc;
}


cw::rc_t cw::perf_meas::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  perf_meas_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
  {
    rc = cwLogError(rc,"Destroy failed.");
    return rc;
  }

  hRef.clear();

  return rc;
}

cw::rc_t cw::perf_meas::reset( handle_t h, unsigned init_locId )
{
  perf_meas_t* p = _handleToPtr(h);
  return _reset(p,init_locId);
}


cw::rc_t cw::perf_meas::exec( handle_t h, const sfscore::event_t* event, result_t& resultRef )
{
  rc_t         rc  = kOkRC;
  perf_meas_t* p   = _handleToPtr(h);
  unsigned     loc_idx;

  resultRef = {};
  resultRef.loc = kInvalidIdx;
  resultRef.sectionLoc = kInvalidIdx;
  
  // get the index of the location record associated with this event
  if((loc_idx = _loc_id_to_index(p,event->oLocId )) == kInvalidIdx )
  {
    cwLogError(kInvalidIdRC,"The event loc %i is not valid.",event->oLocId);
    goto errLabel;
  }

  // if this event is prior to the last triggered section then ignore it
  if( p->last_section_loc_idx != kInvalidIdx && loc_idx < p->last_section_loc_idx )
  {
    cwLogWarning("Backtrack before last triggered section loc:%i < last section:%i.",loc_idx,p->last_section_loc_idx);
    goto errLabel;
  }


  // if this location is attached to a set then eval the set if it is complete
  if((rc = _update_sets(p,loc_idx)) != kOkRC )
  {
    rc = cwLogError(rc,"Set update failed.");
    goto errLabel;
  }

  // if this location is at or after 'next_calc_loc_idx' then evaluate the calc record
  if((rc = _update_calc(p,loc_idx)) != kOkRC )
  {
    rc = cwLogError(rc,"Calc. update failed.");
    goto errLabel;
  }

  // if this location is at or after the next section then update the section from the calc.
  if((rc = _update_section(p,loc_idx,resultRef)) != kOkRC )
  {
    rc = cwLogError(rc,"Section update failed.");
    goto errLabel;
  }
  
 errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"Perf-meas exec failed.");
  return rc;
}

void cw::perf_meas::report( handle_t h )
{
  perf_meas_t* p   = _handleToPtr(h);
  
  for(unsigned i=0; i<p->locN; ++i)
  {
    loc_t* loc = p->locA + i;
    bool fl = loc->section || loc->setL || loc->calc;
    
    if( fl )
      printf("%i : ",loc->locId );
    
    if( loc->section )
    {
      printf("section:%s ", loc->section->section->label );
    }
    
    set_t* s;
    if( loc->setL != nullptr )
    {
      printf("set: ");
      for(s=loc->setL; s!=nullptr; s=s->slink)
        printf("%i ", s->set->id);
    }
    
    if( loc->calc != nullptr )
    {
      printf("calc: ");
      for(s=loc->calc->setL; s!=nullptr; s=s->clink)
        printf("%i ", s->set->id);
    }

    if(fl)
      printf("\n");
      
  }
}
  
