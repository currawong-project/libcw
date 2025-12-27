//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwFile.h"
#include "cwMidi.h"
#include "cwFileSys.h"
#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwCsv.h"
#include "cwNumericConvert.h"
#include "cwTime.h"
#include "cwVectOps.h"
#include "cwMidi.h"

namespace cw
{
  namespace sfscore
  {
    
    typedef struct sfscore_str
    {
      bool                  deleteParserH_Fl;
      score_parse::handle_t parserH;

      double     srate;
      
      event_t*   eventA;
      unsigned   eventAllocN;
      unsigned   eventN;

      set_t*     setA;
      unsigned   setN;

      section_t* sectionA;
      unsigned   sectionN;

      loc_t*     locA;
      unsigned   locN;

    } sfscore_t;

    typedef struct rpt_evt_str
    {
      event_t*    event;
      loc_t*      loc;
      section_t*  section;
    } rpt_event_t;


    sfscore_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,sfscore_t>(h); }

    
    void _destroy_set( set_t* s )
    {
      mem::release(s->evtArray);
      mem::release(s->sectArray);
      mem::release(s);
    }

    void _destroy_section( section_t* s )
    {
      char* ss = (char*)(s->label);
      mem::release(ss);
      mem::release(s->setArray);
    }
    
    rc_t _destroy( sfscore_t* p )
    {
      rc_t rc = kOkRC;

      for(unsigned i=0; i<p->locN; ++i)
        mem::release(p->locA[i].evtArray);
      mem::release(p->locA);
      
      for(unsigned i=0; i<p->setN; ++i)
      {
        mem::release(p->setA[i].evtArray);
        mem::release(p->setA[i].sectArray);
      }      
      mem::release(p->setA);
      
      for(unsigned i=0; i<p->sectionN; ++i)
        _destroy_section( p->sectionA + i );
      mem::release(p->sectionA);


      for(unsigned i=0; i<p->eventN; ++i)
      {
        mem::release(p->eventA[i].sciPitch);
        mem::release(p->eventA[i].varA);
      }
      mem::release(p->eventA);

      if( p->deleteParserH_Fl )
        destroy(p->parserH);
      
      mem::release(p);
      return rc;
    }


    event_t* _hash_to_event( sfscore_t* p, unsigned hash )
    {
      for(unsigned i=0; i<p->eventN; ++i)
        if( p->eventA[i].hash == hash )
          return p->eventA + i;
      return nullptr;
    }

    double _calc_frac( double rval, unsigned dot_cnt )
    {
      double mult = 1.0;
      if(dot_cnt > 0)
      {
        for(unsigned i=0; i<dot_cnt; ++i)
          mult += 1.0 / (1 << i);
      }
      
      return mult/rval;      
    }

    rc_t _create_event_array( sfscore_t* p )
    {
      rc_t rc = kOkRC;
      
      const score_parse::event_t* pe_array = score_parse::event_array(p->parserH);
      
      p->eventAllocN = score_parse::event_count(p->parserH);
      p->eventN      = 0;
      
      if( pe_array == nullptr || p->eventAllocN == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"No events were found.");
        goto errLabel;
      }
      
      p->eventA = mem::allocZ<event_t>(p->eventAllocN);
      p->locN   = 0;

      for(unsigned i=0; i<p->eventAllocN; ++i)
      {
        event_t*                    e  = p->eventA + p->eventN;
        const score_parse::event_t* pe = pe_array  + i;

        if( cwIsFlag(pe->flags,score_parse::kOnsetFl) )
        {
          e->type        = pe->opId;
          e->secs        = pe->sec;
          e->index       = p->eventN;
          e->oLocId      = pe->oLocId;
          e->pitch       = pe->d0;
          e->vel         = pe->d1;
          e->flags       = pe->flags;
          e->dynLevel    = pe->dynLevel;
          e->frac        = _calc_frac(pe->rval, pe->dotCnt);
          e->barNumb     = pe->barNumb;
          e->barNoteIdx  = pe->barPitchIdx;
          e->csvRowNumb  = pe->csvRowNumb;
          e->line        = pe->csvRowNumb;
          e->parseEvtIdx = pe->index;
          e->hash        = pe->hash;
          e->sciPitch    = pe->sciPitch == nullptr ? nullptr : mem::duplStr(pe->sciPitch);
          e->bpm         = pe->bpm;
          e->bpm_rval    = pe->bpm_rval;
          

          e->varN = std::count_if( pe->varA, pe->varA + score_parse::kVarCnt, [](const score_parse::event_var_t& x){ return x.flags!=0; });
          e->varA = mem::allocZ<var_t>(e->varN);

          for(unsigned k = score_parse::kMinVarIdx,j=0; k<score_parse::kVarCnt && j<e->varN; ++k)
          {            
            if( pe->varA[k].flags != 0 )
            {
              assert( k == pe->varA[k].set->varTypeId );

              e->varA[j].flags = pe->varA[k].flags;
              e->varA[j].varId = pe->varA[k].set->varTypeId;
              e->flags |= pe->varA[k].flags;
              ++j;
            }            
          }
          
          if( e->oLocId > p->locN )
            p->locN = e->oLocId;
            
          p->eventN += 1;
        }
      }

      p->locN += 1;             // add one to convert locN from index to count

      cwLogInfo("%i locations.",p->locN);
      
    errLabel:
      return rc;
    }

    rc_t _create_loc_array( sfscore_t* p )
    {
      rc_t     rc = kOkRC;
      unsigned ebi      = 0;
      
      if( p->locN == 0)
      {
        rc = cwLogError(kInvalidStateRC,"No locations were found.");
        goto errLabel;
      }

      p->locA = mem::allocZ<loc_t>(p->locN);

      for(unsigned i = 0; i<p->eventN; ++i)
      {
        const event_t* e = p->eventA + i;
        
        if( e->oLocId != p->eventA[ebi].oLocId || i == p->eventN-1 )
        {
          unsigned oLocId = p->eventA[ebi].oLocId;
          
          assert( oLocId < p->locN);
          
          loc_t* loc    = p->locA + oLocId;
          loc->index    = p->eventA[ebi].oLocId;
          loc->secs     = p->eventA[ebi].secs;
          loc->barNumb  = p->eventA[ebi].barNumb;
          loc->evtCnt   = (i - ebi) + (i == p->eventN-1 ? 1 : 0);
          loc->evtArray = mem::allocZ<event_t*>( loc->evtCnt );

          for(unsigned j = 0; j<loc->evtCnt; ++j)
          {
            assert( ebi + j < p->eventN );
            loc->evtArray[j] = p->eventA + (ebi+j);
          }
          
          ebi = i;
        }        
      }
    errLabel:
      return rc;
    }

    rc_t _assign_section_to_events( sfscore_t* p )
    {
      for(unsigned si=0,ei=0; si<p->sectionN ; ++si)
      {
        // the last event in a section is the event just prior to the first event in the next section
        unsigned end_evt_idx = si>=p->sectionN-1 ? p->eventN : p->sectionA[si+1].begEvtIndex;
        
        for(; ei<end_evt_idx; ++ei)
          p->eventA[ ei ].section = p->sectionA + si;
        
      }
      return kOkRC;
    }
    
    rc_t _create_section_array( sfscore_t* p )
    {
      rc_t rc     = kOkRC;
      p->sectionN = score_parse::section_count(p->parserH);

      // the location array must have already been created.
      assert( p->locA != nullptr );
      
      if( p->sectionN == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"No sections were found.");
        goto errLabel;
      }
      else
      {
        p->sectionA = mem::allocZ<section_t>(p->sectionN);
        
        const score_parse::section_t* ps = score_parse::section_list(p->parserH);
        for(unsigned i = 0; i<p->sectionN; ++i)
        {
          if( ps->begEvent != nullptr )
          {
            section_t*                  section     = p->sectionA + i;
            unsigned                    beg_evt_idx = ps->begEvent->index;
            unsigned                    end_evt_idx = ps->endEvent->index;
            const score_parse::event_t* eventA      = event_array( p->parserH );
            unsigned                    eventN      = event_count( p->parserH );
            event_t*                    begEvt      = nullptr; 
            event_t*                    endEvt      = nullptr;
            
            // advance to the first and last onset event
            for(unsigned i = beg_evt_idx; i<=end_evt_idx && i<eventN; ++i)
            {
              if( eventA[i].oLocId != kInvalidId  )
              {
                if( begEvt == nullptr )
                  begEvt = _hash_to_event(p,eventA[i].hash);
                
                event_t* e;
                if((e = _hash_to_event(p,eventA[i].hash)) != nullptr )
                  endEvt = e;
              }
              
            }

            if( begEvt == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The section '%s' does not have a 'begin' event with hash:%x.",cwStringNullGuard(ps->label),ps->begEvent->hash);
              goto errLabel;
            }

            if( endEvt == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The section '%s' does not have an 'end' event with hash:%x.",cwStringNullGuard(ps->label),ps->endEvent->hash);
              goto errLabel;
            }
            
            section->label              = mem::duplStr(ps->label);
            section->index              = i;
            section->begEvtIndex        = begEvt->index;
            section->endEvtIndex        = endEvt->index;
            section->locPtr             = p->locA + p->eventA[begEvt->index].oLocId;
            section->locPtr->begSectPtr = section;

            //for(unsigned j     = 0; j<score_parse::kVarCnt; ++j)
            //  section->vars[j] = DBL_MAX;
          }
          
          ps = ps->link;
        }
      }

    errLabel:
      return rc;
    }

    section_t* _label_to_section( sfscore_t* p, const char* label )
    {
      for(unsigned i = 0; i<p->sectionN; ++i)
        if( textIsEqual(p->sectionA[i].label,label) )
          return p->sectionA + i;
      
      return nullptr;
    }
    
    rc_t _create_set_array( sfscore_t* p )
    {
      rc_t                      rc = kOkRC;
      const score_parse::set_t* ps = set_list(p->parserH);
      
      p->setN      = score_parse::set_count(p->parserH);
      if( p->setN == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"No sets were found.");
        goto errLabel;
      }
      else
      {
        p->setA = mem::allocZ<set_t>( p->setN );

        // for each set
        for(unsigned i = 0; i<p->setN; ++i,ps=ps->link)
        {
          assert(ps != nullptr);
          
          section_t* section  = nullptr;
          set_t*     set      = p->setA + i;
          unsigned   evtIdx0  = kInvalidIdx;
          unsigned   oLocId0  = kInvalidIdx;
          
          set->id    = ps->id;
          set->varId = ps->varTypeId;

          // fill in the events belonging to this set
          set->evtCnt   = ps->eventN;
          set->evtArray = mem::allocZ<event_t*>(set->evtCnt);
          for(unsigned j=0; j<set->evtCnt; ++j)
          {
            event_t* e = nullptr;
            unsigned k = 0;

            // locate the jth event
            if((e = _hash_to_event(p, ps->eventA[j]->hash )) == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The '%s' set event in measure:%i with hash %x (CSV Row:%i) could not be found.",score_parse::var_index_to_char(ps->varTypeId),ps->eventA[j]->barNumb,ps->eventA[j]->hash,ps->eventA[j]->csvRowNumb);
              goto errLabel;
            }

            // the set events must be in time order
            if( evtIdx0!=kInvalidIdx && e->index < evtIdx0 )
            {
              rc = cwLogError(kInvalidStateRC,"The '%s' set event in measure:%i with hash %x (CSV Row:%i) is out of time order.",score_parse::var_index_to_char(ps->varTypeId),ps->eventA[j]->barNumb,ps->eventA[j]->hash,ps->eventA[j]->csvRowNumb);
              goto errLabel;
            }
            evtIdx0 = e->index;

            // Track the count of locations used by this set.
            if( oLocId0==kInvalidIdx || e->oLocId != oLocId0 )
              set->locN += 1;
            oLocId0 = e->oLocId;
            
            set->evtArray[j] = e;

            // set the set pointer on this event to point back to this set
            for(k=0; k<e->varN; ++k)
              if( e->varA[k].varId == set->varId )
              {
                e->varA[k].set = set;
                break;
              }

            if( k == e->varN )
            {
              rc = cwLogError(kInvalidStateRC,"The event set slots at location '%i' (CSV row:%i) was not found for var type:%i.",e->oLocId,e->csvRowNumb,set->varId);
              goto errLabel;
            }                        
          }

          // add this set to the setList for the set's end loc
          if( set->evtCnt > 0 )
          {
            loc_t* end_loc = p->locA + set->evtArray[set->evtCnt-1]->oLocId;
            set->llink = end_loc->setList;
            end_loc->setList = set;            
          }
          
          // set the target-section related fields for this set
          if( ps->targetSection != nullptr )
          {
            if((section = _label_to_section(p,ps->targetSection->label)) == nullptr )
            {
              rc = cwLogError(kInvalidIdRC,"The section '%s' was not found.");
              goto errLabel;
            }

            set->sectCnt = 1;
            set->sectArray = mem::allocZ<section_t*>(set->sectCnt);
            set->sectArray[0] = section;

            section->setCnt += 1;
            section->setArray = mem::resizeZ(section->setArray,section->setCnt);
            section->setArray[ section->setCnt-1 ] = set;

            
            if(  set->evtCnt>0 )
            {
              // track the location of the last event in the last set that is applied to this section
              unsigned oLocId = set->evtArray[ set->evtCnt-1 ]->oLocId;
              
              if( section->measLocPtr == nullptr || oLocId > section->measLocPtr->index )
                  section->measLocPtr = p->locA + oLocId;                
            }
            
          }
        }
      }
    errLabel:
      return rc;
    }

    rc_t _set_tempo( sfscore_t* p )
    {
      rc_t rc = kOkRC;
      const score_parse::event_t* eventA = event_array(p->parserH);
      unsigned                    eventN = event_count(p->parserH);

      // Get the min BPM
      auto min_evt = std::min_element(eventA,eventA+eventN,[](auto& x0, auto& x1){ return x0.bpm<x1.bpm; });
      
      cwLogInfo("min tempo:%i at CSV row:%i",min_evt->bpm,min_evt->csvRowNumb);

      if( min_evt->bpm == 0 )
      {
        cwLogError(kInvalidArgRC,"The minimum tempo must be greater than zero.");
        goto errLabel;
      }

      // Set event.relTempo
      std::for_each(p->eventA, p->eventA+p->eventN, [min_evt](auto& x){ x.relTempo=(double)x.bpm / min_evt->bpm; } );

    errLabel:
      return rc;                    
    }

    rc_t _validate_dyn_set( sfscore_t* p, const set_t* set )
    {
      rc_t rc = kOkRC;
      for(event_t* const * ee=set->evtArray; ee<set->evtArray+set->evtCnt; ++ee)
      {
        assert( ee != nullptr && *ee != nullptr );
        const event_t* e = *ee;
        if( e->dynLevel == kInvalidIdx )
          rc = cwLogError(kInvalidArgRC,"No dynamic level has been assigned to the note (%) at score loc:%i CSV row:%i.",e->oLocId,e->csvRowNumb);
      }
      return rc;
    }

    // Given a list of note events calc the standard deviation of the inter-onset time between the notes.
    rc_t _calc_delta_time_std_dev( unsigned locN, event_t* const* evtA, unsigned evtN, double& stdRef )
    {
      rc_t rc = kOkRC;

      assert( locN > 1 );

      double   locSecV[ locN ];
      unsigned locCntV[ locN ];
      unsigned evtIdxV[ evtN ];

      vop::fill(locSecV,locN,0.0);
      vop::fill(locCntV,locN,0);
      vop::fill(evtIdxV,evtN,0);
      
      unsigned li = 0;
      for(unsigned ei=0; ei<evtN; ++ei)
      {
        if( ei>0 && evtA[ei]->secs != evtA[ei-1]->secs )
          ++li;
          
        locSecV[li] += evtA[ei]->secs;
        locCntV[li] += 1;
        evtIdxV[ei]  = li;
      }

      for(unsigned li=0; li<locN; ++li)
        locSecV[li] /= locCntV[li];

      assert( li == locN-1 );

      if( locN < 3 )
      {
        rc = cwLogError(kInvalidStateRC,"Cannot compute delta time std-dev on sequences with less than 3 elements.");
        goto errLabel;
      }
      else
      {
        double dsum = 0;        
        double sum = 0;
        
        for(unsigned i=1; i<locN; ++i)
          sum += locSecV[i] - locSecV[i-1];
      
        double mean = sum/(locN-1);

        for(unsigned i=1; i<locN; ++i)
        {
          double d = (locSecV[i] - locSecV[i-1]) - mean;
          dsum += d*d;
        }
        
        stdRef = sqrt(dsum/(locN-1));

      }
    errLabel:
      return rc;
    }

    void _print_even_set( sfscore_t* p, const set_t* set )
    {
      
      for(unsigned i=0; i<set->evtCnt; ++i)
      {
        const event_t* e = set->evtArray[i];

        double dsec = -1;
        if( i>0 && e->oLocId != set->evtArray[i-1]->oLocId )
          dsec = e->secs - set->evtArray[i-1]->secs;

        printf("%3i loc:%5i d:%6.3f f:%f %s\n",
               e->barNumb,
               e->oLocId,
               dsec,
               e->frac,
               score_parse::event_array(p->parserH)[ e->parseEvtIdx ].sciPitch );
      }           
    }
    
    rc_t _validate_even_set( sfscore_t* p, const set_t* set, bool show_warnings_fl )
    {
      rc_t rc;
      double std = 0;

      if( set->locN < 3 )
      {
        rc = cwLogError(kInvalidArgRC,"The even set id %i has less than 3 locations.",set->id);
        goto errLabel;
      }
      
      if((rc = _calc_delta_time_std_dev(set->locN,set->evtArray,set->evtCnt,std)) != kOkRC )
      {
        cwLogError(rc,"Even set score time validation failed.");
        goto errLabel;
      }

      if( std > 0.05 && show_warnings_fl )
      {
        printf("Even set periodcity out of range. set:%3i %3i : std:%6.4f\n",set->id,set->evtCnt,std);
        _print_even_set(p,set);
      }
    errLabel:
      return rc;
    }

    rc_t _calc_score_tempo( const set_t* set )
    {
      rc_t rc = kOkRC;
      
      // Both of these assertions should have been previously verified
      // by the score validation process.      
      assert( set->locN >= 2 );
      //assert( set->evtCnt >= 0 );

      bool printFl = false; //set->evtArray[0]->barNumb == 272;
      
      double   locSecV[ set->locN ];
      unsigned locCntV[ set->locN ];
      double   locFracV[ set->locN ];
      double   bpmV[ set->locN-1 ];
      double   bpm = 0.0;

      vop::fill(locSecV,set->locN,0);
      vop::fill(locCntV,set->locN,0);
      vop::fill(locFracV,set->locN,0);

      
      // Calc the oneset time at each location - this involves taking the mean time of all notes that that location.
      // Notes
      // 1. For the score this step is not necessary because all notes will fall on exactly the same time.
      // 2. It might be better to take the median rather than the mean to prevent outlier problems,
      unsigned cur_loc_idx = set->evtArray[0]->oLocId;
      unsigned li = 0;
      for(unsigned i=0; i<set->evtCnt; ++i)
      {
        if( set->evtArray[i]->oLocId != cur_loc_idx )
        {
          cur_loc_idx = set->evtArray[i]->oLocId;
          ++li;
        }

        assert( li < set->locN);
        
        locSecV[  li ]  += set->evtArray[i]->secs;
        locCntV[  li ]  += 1;

        //if( locFracV[li]!=0 && set->evtArray[i]->frac != locFracV[li] )
        //  cwLogWarning("Frac mismatch.");
        
        locFracV[ li ]  =  set->evtArray[i]->bpm_rval;
      }

      // Convert onset time sum to avg.
      for(unsigned i=0; i<set->locN; ++i)
        if( locCntV[i] != 0 )
          locSecV[i] /= locCntV[i];

      // Calc the BPM between each two notes in the sequence.
      for(unsigned i=1; i<set->locN;  ++i)
      {
        double d             = locSecV[i] - locSecV[i-1];
        double secs_per_beat = d;
        bpmV[i-1]            = 60.0/(secs_per_beat * locFracV[i-1]);

        // bpm = 60 / (spb*x)
        // bpm/(60)
        // 60/(bpm*sbp) = x
        double fact = 60/(set->evtArray[0]->bpm * secs_per_beat);
        double est_bpm = 60.0/(secs_per_beat * fact);
        
        if( printFl )
          printf("%3i : %f : %i d:%f frac:%f spb:%f fact:%f bpm:%f %f\n",
                 set->id,
                 locSecV[i-1],
                 locCntV[i-1],d,
                 locFracV[i-1],
                 secs_per_beat,
                 fact,
                 est_bpm,
                 bpmV[i-1]);
        
      }

      // take the avg bpm as the 
      unsigned bpmN = 0;
      for(unsigned i=0; i<set->locN-1; ++i)
        if( bpmV[i] > 0 )
        {
          bpm  += bpmV[i];
          bpmN += 1;
        }

      if( bpmN > 0 )
        bpm /= bpmN;

      if( printFl )
        printf("meas:%i locN:%i BPM:%i est:%f\n",set->evtArray[0]->barNumb,set->locN,set->evtArray[0]->bpm,bpm);
      
      return rc;      
    }
    
    rc_t _validate_tempo_set( sfscore_t* p, const set_t* set )
    {
      rc_t rc = kOkRC;

      if( set->locN < 2 )
      {
        rc = cwLogError(kInvalidArgRC,"The tempo set id %i has less than 2 locations.",set->id);
        goto errLabel;
      }
      
      if( set->evtCnt > 0 )
      {
        // all events in a tempo set must share the same tempo marking
        unsigned bpm = set->evtArray[0]->bpm;
        
        // Note we do not check the tempo of the last event (i.e. i<set->evtCnt-1) because it may
        // land on a tempo change. We therefore must take the tempo of the first event as the tempo
        // for all successive notes.
        
        for(unsigned i=1; i<set->evtCnt-1; ++i)
          if(set->evtArray[i]->bpm != bpm )
          {
            rc = cwLogError(kInvalidStateRC,"Tempo mismatch at tempo event loc:%i (CSV row:%i) in set %i",set->evtArray[i]->oLocId, set->evtArray[i]->csvRowNumb, set->id );
            goto errLabel;
          }


        _calc_score_tempo( set );
      }
        
    errLabel:
      return rc;
    }

    rc_t _validate_sets( sfscore_t* p, bool show_warnings_fl )
    {
      rc_t rc = kOkRC;
      
      for(const set_t* set = p->setA; set<p->setA + p->setN; ++set )
      {
        rc_t rc0 = kOkRC;

        if( set->evtCnt==0 || set->evtArray[0]==nullptr )
        {
          rc = cwLogError(kInvalidStateRC, "Set id %i of type %s has no events.",cwStringNullGuard(score_parse::var_index_to_char(set->varId)));
          
        }
        else
        {
          switch( set->varId )
          {
            case score_parse::kDynVarIdx:
              rc0 = _validate_dyn_set(p,set);
              break;
            
            case score_parse::kEvenVarIdx:
              rc0 = _validate_even_set(p,set,show_warnings_fl);
              break;
            
            case score_parse::kTempoVarIdx:
              rc0 = _validate_tempo_set(p,set);
              break;
          }

          if( rc0 != kOkRC )
          {
            cwLogError(rc0,"Validation failed on set id %i of type %s. The set starts at loc:%i (CSV row:%i).",
                       set->id,
                       cwStringNullGuard(score_parse::var_index_to_char(set->varId)),
                       set->evtArray[0]->oLocId,
                       set->evtArray[0]->csvRowNumb);
            rc = rc0;
          }
        }
      }

      return rc;
    }


    rc_t _create( handle_t& hRef,
                  score_parse::handle_t spH,
                  bool show_warnings_fl,
                  bool deleteParserH_Fl )
    {
      rc_t rc;
  
      if((rc = destroy(hRef)) != kOkRC )
        return rc;

      sfscore_t* p = mem::allocZ<sfscore_t>();

      p->deleteParserH_Fl = deleteParserH_Fl;
      p->parserH = spH;
  
      if((rc = _create_event_array( p )) != kOkRC )
        goto errLabel;

      if((rc = _create_loc_array( p )) != kOkRC )
        goto errLabel;
  
      if((rc = _create_section_array( p )) != kOkRC )
        goto errLabel;

      if((rc = _assign_section_to_events(p)) != kOkRC )
        goto errLabel;

      if((rc = _create_set_array( p )) != kOkRC )
        goto errLabel;

      if((rc = _set_tempo(p)) != kOkRC )
        goto errLabel;

      if((rc = _validate_sets(p,show_warnings_fl)) != kOkRC )
        goto errLabel;
  
      hRef.set(p);
  
    errLabel:
      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"sfscore create failed.");
        _destroy(p);
      }
      return rc;
    }
    
    
    void _report_print( sfscore_t* p, rpt_event_t* rptA, unsigned rptN, file::handle_t fH )
    {
      unsigned    bar0      = 0;
      const char* sec0      = nullptr;
      const char* blank_str = "  ";
      const char* sec_str   = "S:";
      const char* bar_str   = "B:";
      

  
      for(rpt_event_t* r=rptA; r<rptA+rptN; ++r)
      {
        const event_t* e = r->event;
        const section_t* section = r->section;

        bool        new_bar_fl = bar0 != e->barNumb;        
        const char* d_bar_str  = new_bar_fl ? bar_str : blank_str;
        const char* d_sec_str  = textIsEqual(sec0,section->label) ? blank_str : sec_str;

        if( r==rptA || new_bar_fl  )
        {
          printf(fH,"%i :\n",e->barNumb);
          printf(fH,"e idx oloc   secs   bpm b_rval rtmpo op  sectn  sdx  bar  bdx scip vel  frac g     hash  \n");
          printf(fH,"----- ----- ------- --- ------ ----- --- ------ --- ----- --- ---- --- ----- - ----------\n");
        }
        
        bar0 = e->barNumb;
        sec0 = section->label;

        printf(fH,"%5i %5i %7.3f %3i %6.4f %5.3f %3s %2s%4s %3i %2s%3i %3i %4s %3i %5.3f %c 0x%x ",
               e->index,
               e->oLocId,
               e->secs,
               e->bpm,
               e->bpm_rval,
               e->relTempo,
               score_parse::opcode_id_to_label(e->type),
               d_sec_str,
               section==nullptr ? "    " : cwStringNullGuard(section->label),
               section->index,
               d_bar_str,
               e->barNumb,
               e->barNoteIdx,
               e->sciPitch,
               e->vel,
               e->frac,
               cwIsFlag(e->flags,score_parse::kGraceFl) ? 'g' : ' ',
               e->hash);

        // for each possible var type
        for(unsigned vi=0; vi<score_parse::kVarCnt; ++vi)
        {
          // locate the associated var spec in event.varA[]
          var_t* var = std::find_if( e->varA, e->varA+e->varN, [vi](const var_t& x){return x.varId==vi;});

          // if this event is not a included in a set of type 'vi'
          if( var >= e->varA+e->varN )
            printf(fH,"           ");
          else
          {
            const char* sect_label = var->set->sectArray[0]==nullptr ? "****" : var->set->sectArray[0]->label;
            printf(fH,"%s-%03i-%s ",score_parse::var_flags_to_char(var->flags), var->set->id, sect_label);
          }
        }

        printf(fH,"\n");
      }
      
    }

    rpt_event_t*
    _report_create( sfscore_t* p )
    {
      rpt_event_t* rptA = mem::allocZ<rpt_event_t>( p->eventN );
      unsigned curSectionIdx = 0;
        
      // for each location
      for(unsigned i=0; i<p->locN; ++i)
      {
        loc_t* loc = p->locA + i;
        assert(loc->index == i );

        // for each event assigned to this location
        for(unsigned j=0; j<loc->evtCnt; ++j)
        {
          unsigned     event_idx = loc->evtArray[j]->index;
          rpt_event_t* r         = rptA + event_idx;

          // store the event
          r->event = p->eventA + event_idx; 
          r->loc   = loc;               
          
          assert( r->event->index == event_idx );
          assert( r->event->barNumb == loc->barNumb );

          // if this event is the first event in the next section
          if( curSectionIdx < p->sectionN-1 && r->event->index == p->sectionA[ curSectionIdx+1 ].begEvtIndex )
            curSectionIdx += 1;

          r->section = p->sectionA + curSectionIdx;

        }
      }
      
      return rptA;
    }

    rc_t _report( sfscore_t* p, const char* fname )
    {
      rc_t rc = kOkRC;

      rpt_event_t* rptA = nullptr;

      if((rptA = _report_create(p)) != nullptr )
      {
        file::handle_t fH;

        if((rc = file::open(fH,fname,file::kWriteFl)) != kOkRC )
        {
          rc = cwLogError(rc,"Score report file open failed on '%s'.",cwStringNullGuard(fname));
          goto errLabel;
        }
         
        _report_print(p,rptA,p->eventN, fH);

      errLabel:
        close(fH);
        mem::release(rptA);
      }
      
      return rc;      
    }
  }
}

cw::rc_t cw::sfscore::create( handle_t& hRef,
                              score_parse::handle_t spH,
                              bool show_warnings_fl)
{
  return _create(hRef,spH,show_warnings_fl,false);  
}

cw::rc_t cw::sfscore::create( handle_t&        hRef,
                              const char*      fname,
                              double           srate,
                              dyn_ref_tbl::handle_t dynRefH,
                              bool show_warnings_fl)
{
  rc_t rc;
  
  score_parse::handle_t spH;

  if((rc = score_parse::create(spH,fname,srate,dynRefH)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore parse failed.");
    goto errLabel;
  }
  
  rc = _create(hRef,spH,show_warnings_fl,true);

 errLabel:
  return rc;
  
}

cw::rc_t cw::sfscore::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  sfscore_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
  {
    rc = cwLogError(rc,"Destroy failed.");
    goto errLabel;
  }

  hRef.clear();
  
 errLabel:
  return rc;
}

void     cw::sfscore::clear_all_performance_data( handle_t h )
{
  sfscore_t* p  = _handleToPtr(h);
  std::for_each( p->eventA, p->eventA + p->eventN, [](event_t& e){ e.perfFl=false; e.perfCnt=0; e.perfVel=0, e.perfSec=0, e.perfMatchCost=std::numeric_limits<double>::max(); } );
  std::for_each( p->setA,    p->setA  + p->setN,   [](set_t&   s){ s.perfEventCnt=0; s.perfUpdateCnt=false; } );
}

cw::rc_t cw::sfscore::set_perf( handle_t h, unsigned event_idx, double secs, uint8_t pitch, uint8_t vel, double cost )
{
  rc_t       rc = kOkRC;
  sfscore_t* p  = _handleToPtr(h);
  event_t*   e  = p->eventA + event_idx;;
  
  if( event_idx >= p->eventN )
  {
    rc = cwLogError(kInvalidIdRC,"The performance event index %i is invalid.",event_idx);
    goto errLabel;
  }

  if( e->pitch != pitch )
  {
    rc = cwLogError(kInvalidStateRC,"The performance event pitch %x is not a match.",pitch);
    goto errLabel;
  }

  for(unsigned i=0; i<e->varN; ++i)
  {
    e->varA[i].set->perfUpdateCnt += 1;

    if( e->perfFl == false )
    {        
      e->varA[i].set->perfEventCnt += 1;
      
      if( e->varA[i].set->perfEventCnt > e->varA[i].set->evtCnt )
      {
        rc = cwLogError(kInvalidStateRC,"The perf. count of a set (id:%i) exeeded it's event count.", e->varA[i].set->evtCnt );
        goto errLabel;
      }
    }
  }

  e->perfCnt     += 1;
  e->perfFl       = true;
  e->perfVel      = vel;
  e->perfSec      = secs;
  e->perfDynLevel = dyn_ref_vel_to_level(p->parserH,vel);
  e->perfMatchCost= cost;

 errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"The performance score update failed.");
  
  return rc;  
}

bool cw::sfscore::are_all_loc_set_events_performed( handle_t h, unsigned locId )
{
  sfscore_t* p = _handleToPtr(h);
  
  if( locId >= p->locN )
  {
    cwLogError(kInvalidIdRC,"An invalid loc id %i was encountered while testing for performed events.",locId);
    assert(0);
    return false;
  }
  
  const loc_t* loc = p->locA + locId;
  for(unsigned i=0; i<loc->evtCnt; ++i)
    if( loc->evtArray[i]->varN > 0 && loc->evtArray[i]->perfFl == false)
        return false;
  
  return true;
}


double cw::sfscore::sample_rate( handle_t& h )
{
  sfscore_t* p = _handleToPtr(h);
  return sample_rate(p->parserH);
}

unsigned cw::sfscore::event_count( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->eventN;
}

const cw::sfscore::event_t* cw::sfscore::event( handle_t h, unsigned idx )
{
  sfscore_t* p = _handleToPtr(h);

  if( idx > p->eventN )
  {
    cwLogError(kInvalidIdRC,"The event index '%i' is not valid.",idx);
    return nullptr;
  }
  
  return p->eventA + idx;
}

const cw::sfscore::event_t* cw::sfscore::hash_to_event( handle_t h, unsigned hash )
{
  sfscore_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->eventN; ++i)
    if( p->eventA[i].hash == hash )
      return p->eventA + i;
  
  return nullptr;
}

const cw::sfscore::event_t*   cw::sfscore::bar_to_event( handle_t h, unsigned barNumb )
{
  sfscore_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->eventN; ++i)
    if( p->eventA[i].barNumb == barNumb )
      return p->eventA + i;
  return nullptr;
}


unsigned cw::sfscore::loc_count( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->locN;
}

const cw::sfscore::loc_t* cw::sfscore::loc_base( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->locA;
}

unsigned cw::sfscore::set_count( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->setN;
}

const cw::sfscore::set_t* cw::sfscore::set_base( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->setA;
}

unsigned cw::sfscore::section_count( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->sectionN;
}

const cw::sfscore::section_t* cw::sfscore::section_base( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->sectionA;
}

const cw::sfscore::section_t* cw::sfscore::event_index_to_section( handle_t h, unsigned event_idx )
{
  sfscore_t * p = _handleToPtr(h);
  for(unsigned i=0; i<p->sectionN; ++i)
    if( p->sectionA[i].begEvtIndex <= event_idx && event_idx <= p->sectionA[i].endEvtIndex )
      return p->sectionA + i;
  return nullptr;
}


void cw::sfscore::report( handle_t h, const char* out_fname )
{
  sfscore_t* p = _handleToPtr(h);
  printf("Score Report\n");
  _report(p,out_fname);
}

