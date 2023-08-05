#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwFileSys.h"
#include "cwSfScore.h"
#include "cwCsv.h"
#include "cwNumericConvert.h"
#include "cwTime.h"
#include "cwVectOps.h"
#include "cwSfScoreParser.h"
#include "cwMidi.h"

namespace cw
{
  namespace sfscore
  {
    
    typedef struct sfscore_str
    {
      parser::handle_t parserH;
      
      event_t*   eventA;
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
      set_t*      setA[ kScVarCnt ];
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

      destroy(p->parserH);
      
      mem::release(p);
      return rc;
    }

    rc_t _create_event_array( sfscore_t* p )
    {
      rc_t rc = kOkRC;
      
      const parser::p_event_t* pe_array = parser::event_array(p->parserH);
      
      p->eventN = parser::event_count(p->parserH);

      if( pe_array == nullptr || p->eventN == 0 )
      {
        rc = cwLogError(kInvalidStateRC,"No events were found.");
        goto errLabel;
      }
      
      p->eventA = mem::allocZ<event_t>(p->eventN);
      p->locN   = 0;

      for(unsigned i=0; i<p->eventN; ++i)
      {
        event_t*                 e  = p->eventA + i;
        const parser::p_event_t* pe = pe_array  + i;

        e->type       = pe->typeId;
        e->secs       = pe->secs;
        e->index      = pe->index;
        e->locIdx     = pe->locIdx;
        e->pitch      = pe->pitch;
        e->vel        = pe->vel;
        e->flags      = pe->flags;
        e->dynVal     = pe->dynVal;
        e->frac       = pe->t_frac;
        e->barNumb    = pe->barNumb;
        e->barNoteIdx = pe->barNoteIdx;
        e->csvRowNumb = pe->csvRowNumb;
        e->line       = pe->line;
        e->csvEventId = pe->csvEventId;

        if( e->locIdx > p->locN )
          p->locN = e->locIdx;
        
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
      p->sectionN = parser::section_count(p->parserH);

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
        
        const parser::p_section_t* ps = parser::section_list(p->parserH);
        for(unsigned i=0; i<p->sectionN; ++i)
        {
          section_t* section          = p->sectionA + i;
          section->label              = mem::duplStr(ps->label);
          section->index              = i;
          section->begEvtIndex        = ps->begEvtIdx;
          section->locPtr             = p->locA + p->eventA[ps->begEvtIdx].locIdx;
          section->locPtr->begSectPtr = section;

          for(unsigned j=0; j<kScVarCnt; ++j)
            section->vars[j] = DBL_MAX;
          
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
      const parser::p_set_t* ps = set_list(p->parserH);
      
      p->setN = parser::set_count(p->parserH);
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
            set->eleArray[j] = p->eventA + ps->eventA[j]->index;

          // add this set to the setList for the set's end loc
          if( set->eleCnt > 0 )
          {
            loc_t* end_loc = p->locA + set->eleArray[set->eleCnt-1]->locIdx;
            set->llink = end_loc->setList;
            end_loc->setList = set;            
          }
          
          // set the target-section related fields fro this set
          if( ps->target_section != nullptr )
          {
            if((section = _label_to_section(p,ps->target_section->label)) == nullptr )
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

    void _report_print( sfscore_t* p, rpt_event_t* rptA, unsigned rptN )
    {
      unsigned    bar0      = 0;
      const char* sec0      = nullptr;
      const char* blank_str = "  ";
      const char* sec_str   = "S:";
      const char* bar_str   = "B:";
      
      printf("e idx  loc   secs   op  sectn  sdx  bar  bdx scip vel  frac\n");
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
               opcode_id_to_label(e->type),
               d_sec_str,
               section==nullptr ? "    " : cwStringNullGuard(section->label),
               section->index,
               d_bar_str,
               e->barNumb,
               e->barNoteIdx,
               sciPitch,
               e->vel,
               e->frac );

        for(unsigned refVarTypeId=kMinVarScId; refVarTypeId<kScVarCnt; ++refVarTypeId)
        {
          set_t* set = r->setA[refVarTypeId];
          if( set == nullptr || set->sectCnt==0 )
            printf("           ");
          else
          {
            unsigned varRefFlags = var_type_id_to_mask(refVarTypeId);
            
            const char* sect_label = set->sectArray[0]==nullptr ? "****" : set->sectArray[0]->label;
            printf("%c-%03i-%s ",var_type_flag_to_char(e->flags & varRefFlags), set->id, sect_label);
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

cw::rc_t cw::sfscore::create( handle_t&        hRef,
                              const char*      fname,
                              double           srate,
                              const dyn_ref_t* dynRefA,
                              unsigned         dynRefN )
{
  rc_t rc;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  sfscore_t* p = mem::allocZ<sfscore_t>();

  if((rc = parser::create(p->parserH,fname,dynRefA,dynRefN)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore parse failed.");
    goto errLabel;
  }

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

cw::rc_t cw::sfscore::parse_dyn_ref_cfg( const object_t* cfg, dyn_ref_t*& refArrayRef, unsigned& refArrayNRef)
{
  rc_t       rc      = kOkRC;
  dyn_ref_t* dynRefA = nullptr;

  refArrayRef = nullptr;
  refArrayNRef   = 0;
  
  // parse the dynamics ref. array
  unsigned dynRefN = cfg->child_count();
  
  if( dynRefN == 0 )
    cwLogWarning("The dynamic reference array cfg. is empty.");
  else
  {
    dynRefA = mem::allocZ<dyn_ref_t>(dynRefN);
  
    for(unsigned i=0; i<dynRefN; ++i)
    {
      const object_t* pair = cfg->child_ele(i);
    
      if( !pair->is_pair() || pair->pair_label()==nullptr || pair->pair_value()==nullptr || pair->pair_value()->value( dynRefA[i].vel)!=kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Error parsing the dynamics reference array.");
        goto errLabel;
      }
    
      dynRefA[i].label = pair->pair_label();
    }
  }

  
 errLabel:
  if( rc != kOkRC )
    mem::release(dynRefA);
  else               
  {
    refArrayRef = dynRefA;
    refArrayNRef = dynRefN;
  }
  
  return rc;
}


void cw::sfscore::report( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  if( p->parserH.isValid() )
  {
    //printf("Score Parser Report\n");
    //report(p->parserH);
  }

  printf("Score Report\n");
  _report(p);
  

  
}

cw::rc_t cw::sfscore::test( const object_t* cfg )
{
  rc_t            rc             = kOkRC;
  const char*     cm_score_fname = nullptr;
  const object_t* dynArrayNode   = nullptr;
  dyn_ref_t*      dynRefA        = nullptr;
  unsigned        dynRefN        = 0;
  double          srate          = 0;
  handle_t        h;
  time::spec_t    t0;
  

  // parse the test cfg
  if((rc = cfg->getv( "cm_score_fname", cm_score_fname,
                      "srate", srate,
                      "dyn_ref", dynArrayNode )) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore test parse params failed on.");
    goto errLabel;
  }

  if((rc = parse_dyn_ref_cfg( dynArrayNode, dynRefA, dynRefN )) != kOkRC )
  {
    rc = cwLogError(rc,"The reference dynamics array parse failed.");
    goto errLabel;
  }

  time::get(t0);
  
  if((rc = create(h,cm_score_fname,srate,dynRefA,dynRefN)) != kOkRC )
  {
    rc = cwLogError(rc,"Score test create failed.");
    goto errLabel;
  }

  report(h);
  
  printf("%i events %i ms\n",event_count(h), time::elapsedMs(t0) );
  
 errLabel:
  destroy(h);
  mem::release(dynRefA);
  return rc;
}
