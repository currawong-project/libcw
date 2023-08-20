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
      set_t*      setA[ score_parse::kVarCnt ];
    } rpt_event_t;


    sfscore_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,sfscore_t>(h); }

    
    void _destroy_set( set_t* s )
    {
      mem::release(s->eleArray);
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
        mem::release(p->setA[i].eleArray);
        mem::release(p->setA[i].sectArray);
      }      
      mem::release(p->setA);
      
      for(unsigned i=0; i<p->sectionN; ++i)
        _destroy_section( p->sectionA + i );
      mem::release(p->sectionA);
      
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
        event_t* e  = p->eventA + p->eventN;
        const score_parse::event_t* pe = pe_array  + i;

        if( cwIsFlag(pe->flags,score_parse::kOnsetFl) )
        {
          e->type       = pe->opId;
          e->secs       = pe->sec;
          e->index      = p->eventN;
          e->locIdx     = pe->oloc;
          e->pitch      = pe->d0;
          e->vel        = pe->d1;
          e->flags      = 0;
          e->dynVal     = pe->dynLevel;
          e->frac       = _calc_frac(pe->rval, pe->dotCnt);
          e->barNumb    = pe->barNumb;
          e->barNoteIdx = pe->barPitchIdx;
          e->csvRowNumb = pe->csvRowNumb;
          e->line       = pe->csvRowNumb;
          e->csvEventId = kInvalidId; // pe->csvId;
          e->hash       = pe->hash;

          for(unsigned i = score_parse::kMinVarIdx; i<score_parse::kVarCnt; ++i)
          {
            e->varA[i] = pe->varA[i].flags;
            e->flags  |= pe->varA[i].flags;
          }
          
          if( e->locIdx > p->locN )
            p->locN = e->locIdx;
            
          p->eventN += 1;
        }
      }

      p->locN += 1; // add one to convert locN from index to count

      cwLogInfo("%i locations.",p->locN);
      
    errLabel:
      return rc;
    }

    rc_t _create_loc_array( sfscore_t* p )
    {
      rc_t           rc      = kOkRC;
      unsigned       ebi      = 0;
      
      if( p->locN ==  0)
      {
        rc = cwLogError(kInvalidStateRC,"No locations were found.");
        goto errLabel;
      }

      p->locA = mem::allocZ<loc_t>(p->locN);

      for(unsigned i=0; i<p->eventN; ++i)
      {
        const event_t* e = p->eventA + i;
        
        if( e->locIdx != p->eventA[ebi].locIdx || i==p->eventN-1 )
        {
          unsigned locIdx = p->eventA[ebi].locIdx;
          
          assert( locIdx < p->locN);
          
          loc_t* loc    = p->locA + locIdx;
          loc->index    = p->eventA[ebi].locIdx;
          loc->secs     = p->eventA[ebi].secs;
          loc->barNumb  = p->eventA[ebi].barNumb;
          loc->evtCnt   = (i - ebi) + (i==p->eventN-1 ? 1 : 0);
          loc->evtArray = mem::allocZ<event_t*>( loc->evtCnt );
          
          for(unsigned j=0; j<loc->evtCnt; ++j)
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
    
    rc_t _create_section_array( sfscore_t* p )
    {
      rc_t rc = kOkRC;
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
        for(unsigned i=0; i<p->sectionN; ++i)
        {
          if( ps->begEvent != nullptr )
          {
            section_t* section = p->sectionA + i;
            event_t*   begEvt  = _hash_to_event(p,ps->begEvent->hash);

            if( begEvt == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The section '%s' does not have a 'begin' event with hash:%x.",cwStringNullGuard(ps->label),ps->begEvent->hash);
              goto errLabel;
            }
          
            section->label              = mem::duplStr(ps->label);
            section->index              = i;
            section->begEvtIndex        = begEvt->index;
            section->locPtr             = p->locA + p->eventA[begEvt->index].locIdx;
            section->locPtr->begSectPtr = section;

            for(unsigned j=0; j<score_parse::kVarCnt; ++j)
              section->vars[j] = DBL_MAX;
          }
          
          ps = ps->link;
        }
      }

    errLabel:
      return rc;
    }

    section_t* _label_to_section( sfscore_t* p, const char* label )
    {
      for(unsigned i=0; i<p->sectionN; ++i)
        if( textIsEqual(p->sectionA[i].label,label) )
          return p->sectionA + i;
      
      return nullptr;
    }
    
    rc_t _create_set_array( sfscore_t* p )
    {
      rc_t rc = kOkRC;
      const score_parse::set_t* ps = set_list(p->parserH);
      
      p->setN = score_parse::set_count(p->parserH);
      if( p->setN == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"No sets were found.");
        goto errLabel;
      }
      else
      {
        p->setA = mem::allocZ<set_t>( p->setN );

        // for each set
        for(unsigned i=0; i<p->setN; ++i,ps=ps->link)
        {
          assert(ps != nullptr);
          section_t* section = nullptr;
          set_t* set = p->setA + i;

          set->id          = ps->id;
          set->varId       = ps->varTypeId;

          // fill in the events belonging to this list
          set->eleCnt      = ps->eventN;
          set->eleArray    = mem::allocZ<event_t*>(set->eleCnt);
          for(unsigned j=0; j<set->eleCnt; ++j)
          {
            set->eleArray[j] = _hash_to_event(p, ps->eventA[j]->hash );
            
            if( set->eleArray[j] == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The '%s' set event in measure:%i with hash %x (CSV Row:%i) could not be found.",score_parse::var_index_to_char(ps->varTypeId),ps->eventA[j]->barNumb,ps->eventA[j]->hash,ps->eventA[j]->csvRowNumb);
              goto errLabel;
            }
          }
          
          // add this set to the setList for the set's end loc
          if( set->eleCnt > 0 )
          {
            loc_t* end_loc = p->locA + set->eleArray[set->eleCnt-1]->locIdx;
            set->llink = end_loc->setList;
            end_loc->setList = set;            
          }
          
          // set the target-section related fields fro this set
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
            
          }
        }
      }
    errLabel:
      return rc;
    }


    rc_t _create( handle_t& hRef,
                  score_parse::handle_t spH,
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

      if((rc = _create_set_array( p )) != kOkRC )
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
    
    
    void _report_print( sfscore_t* p, rpt_event_t* rptA, unsigned rptN )
    {
      unsigned    bar0      = 0;
      const char* sec0      = nullptr;
      const char* blank_str = "  ";
      const char* sec_str   = "S:";
      const char* bar_str   = "B:";
      
      printf("e idx oloc   secs   op  sectn  sdx  bar  bdx scip vel  frac\n");
      printf("----- ----- ------- --- ------ --- ----- --- ---- --- -----\n");

  
      for(rpt_event_t* r=rptA; r<rptA+rptN; ++r)
      {
        const event_t* e = r->event;
        const section_t* section = r->section;
        
        const char* d_bar_str = bar0 != e->barNumb ? bar_str : blank_str;
        const char* d_sec_str = textIsEqual(sec0,section->label) ? blank_str : sec_str;
        
        char sciPitch[5];
        midi::midiToSciPitch( e->pitch, sciPitch, 5 );

        bar0 = e->barNumb;
        sec0 = section->label;

        printf("%5i %5i %7.3f %3s %2s%4s %3i %2s%3i %3i %4s %3i %5.3f ",
               e->index,
               e->locIdx,
               e->secs,
               score_parse::opcode_id_to_label(e->type),
               d_sec_str,
               section==nullptr ? "    " : cwStringNullGuard(section->label),
               section->index,
               d_bar_str,
               e->barNumb,
               e->barNoteIdx,
               sciPitch,
               e->vel,
               e->frac );

        for(unsigned vi=score_parse::kMinVarIdx; vi<score_parse::kVarCnt; ++vi)
        {
          set_t* set = r->setA[vi];
          if( set == nullptr || set->sectCnt==0 )
            printf("           ");
          else
          {
            const char* sect_label = set->sectArray[0]==nullptr ? "****" : set->sectArray[0]->label;
            printf("%s-%03i-%s ",score_parse::var_flags_to_char(e->varA[vi]), set->id, sect_label);
          }
        }

        printf("\n");
      }
      
    }

    rpt_event_t* _report_create( sfscore_t* p )
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

      for(unsigned i=0; i<p->setN; ++i)
      {
        set_t* set = p->setA + i;
        for(unsigned j=0; j<set->eleCnt; ++j)
        {
          event_t*     e = set->eleArray[j];
          rpt_event_t* r = rptA + e->index;
          assert( r->setA[ set->varId ] == nullptr );
          r->setA[ set->varId ] = set;
        }
      }

      return rptA;
    }

    rc_t _report( sfscore_t* p )
    {
      rc_t rc = kOkRC;

      rpt_event_t* rptA = nullptr;

      if((rptA = _report_create(p)) != nullptr )
      {

        _report_print(p,rptA,p->eventN);
        mem::release(rptA);
      }

      
      return rc;      
    }
  }
}

cw::rc_t cw::sfscore::create( handle_t& hRef,
                              score_parse::handle_t spH )
{
  return _create(hRef,spH,false);
  
}

cw::rc_t cw::sfscore::create( handle_t&        hRef,
                              const char*      fname,
                              double           srate,
                              dyn_ref_tbl::handle_t dynRefH)
{
  rc_t rc;
  
  score_parse::handle_t spH;

  if((rc = score_parse::create(spH,fname,srate,dynRefH)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore parse failed.");
    goto errLabel;
  }

  
  rc = _create(hRef,spH,true);

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

cw::sfscore::event_t* cw::sfscore::event( handle_t h, unsigned idx )
{
  sfscore_t* p = _handleToPtr(h);
  return p->eventA + idx;
}

cw::sfscore::event_t* cw::sfscore::hash_to_event( handle_t h, unsigned hash )
{
  sfscore_t* p = _handleToPtr(h);
  for(unsigned i=0; p->eventN; ++i)
    if( p->eventA[i].hash == hash )
      return p->eventA + i;
  return nullptr;
}


unsigned cw::sfscore::loc_count( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  return p->locN;
}

cw::sfscore::loc_t* cw::sfscore::loc( handle_t h, unsigned idx )
{
  sfscore_t* p = _handleToPtr(h);
  return p->locA + idx;
}

void cw::sfscore::report( handle_t h, const char* out_fname )
{
  sfscore_t* p = _handleToPtr(h);
  printf("Score Report\n");
  _report(p);
}

void cw::sfscore::parse_report( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  report(p->parserH);
}

