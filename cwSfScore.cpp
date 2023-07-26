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

namespace cw
{
  namespace sfscore
  {

    /*
    typedef struct varMap_str
    {
      unsigned    typeId;
      unsigned    flag;
      const char* label;
    } varMap_t;

    typedef struct dynRef_str
    {
      char*    label;
      unsigned labelCharCnt;
      uint8_t  vel;
    } dynRef_t;

    typedef idLabelPair_t opcodeMap_t;
    
    opcodeMap_t _opcodeMapA[] = 
    {
      { kTimeSigEvtScId, "tsg" },
      { kKeySigEvtScId,  "ksg" },
      { kTempoEvtScId,   "tmp" },
      { kTrackEvtScId,   "trk" },
      { kTextEvtScId,    "txt" },
      { kNameEvtScId,    "nam" },
      { kEOTrackEvtScId, "eot" },
      { kCopyEvtScId,    "cpy" },
      { kBlankEvtScId,   "blk" },
      { kBarEvtScId,     "bar" },
      { kPgmEvtScId,     "pgm" },
      { kCtlEvtScId,     "ctl" },
      { kNonEvtScId,     "non" },
      { kPedalEvtScId,   "ped" },
      { kInvalidId,      "<invalid>" }
    };

    varMap_t _varMapA[] = 
    {
      { kEvenVarScId, kEvenScFl, "e"},
      { kDynVarScId,  kDynScFl,  "d"},
      { kTempoVarScId,kTempoScFl,"t"},
      { kInvalidId,  0,         "<invalid>"}
    };
    */
    
    typedef struct sfscore_str
    {
      //dynRef_t*  dynRefA;
      //unsigned   dynRefN;

      parser::handle_t parserH;
      
      event_t* eventA;
      unsigned eventN;
      unsigned eventAllocN;

      set_t* setL;

      section_t* sectionA;
      unsigned   sectionN;
      unsigned   sectionAllocN;

    } sfscore_t;

    sfscore_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,sfscore_t>(h); }

    /*
    unsigned _opcode_label_to_id( const char* label )
    {
      unsigned id;
      if((id = labelToId( _opcodeMapA, label, kInvalidId )) == kInvalidId )
        cwLogError(kInvalidArgRC,"'%s' is not a valid event opcode type label.",cwStringNullGuard(label));
      
      return id;      
    }

    const char* _opcode_id_to_label( unsigned opcodeId )
    {
      const char* label;
      if((label = idToLabel( _opcodeMapA, opcodeId, kInvalidEvtScId)) == nullptr )
        cwLogError(kInvalidArgRC,"The event opcode type id '%i' is not valid.",opcodeId);

      return label;
    }

    
    unsigned _varLabelToTypeId( const char* label )
    {
      if( label!=nullptr && textLength(label)>0 )
      {
        char varLabel[] = { label[0],0 };
        for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
          if( textCompare(varLabel,_varMapA[i].label) == 0 )
            return _varMapA[i].typeId;
      }
      
      cwLogError(kInvalidArgRC,"The variable label '%s' is not valid.",cwStringNullGuard(label));
      
      return kInvalidId;
    }
    const char* _varTypeIdToLabel( unsigned varTypeId )
    {
      for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
        if( _varMapA[i].typeId == varTypeId )
          return _varMapA[i].label;
      
      cwLogError(kInvalidArgRC,"The variable type id '%i' is not valid.",varTypeId);
      
      return nullptr;
    }

    unsigned _varTypeIdToFlag( unsigned varTypeId )
    {
      for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
        if( _varMapA[i].typeId == varTypeId )
          return _varMapA[i].flag;
      
      cwLogError(kInvalidArgRC,"The variable type id '%i' is not valid.",varTypeId);
      return 0;      
    }

    unsigned _varFlagToTypeId( unsigned varFlag )
    {
      for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
        if( _varMapA[i].flag == varFlag )
          return _varMapA[i].typeId;
      
      cwLogError(kInvalidArgRC,"The variable flag id '0x%x' is not valid.",varFlag);
      return kInvalidId;      
    }

    unsigned _varLabelToFlag( const char* varLabel )
    {
      unsigned varId;
      if(varLabel==nullptr || textLength(varLabel)==0 || (varId = _varLabelToTypeId(varLabel)) == kInvalidId )
        return 0;
      return _varTypeIdToFlag(varId);
    }

    const char* _varTypeFlagToLabel( unsigned varFlag )
    {
      unsigned varId;
      if((varId = _varFlagToTypeId(varFlag)) != kInvalidId )
        return _varTypeIdToLabel(varId);
      
      return nullptr;  
    }

    unsigned _dynLabelToLevel( sfscore_t* p, const char* dynLabel )
    {
      const char* end;
      unsigned char_cnt;
      
      if( dynLabel == nullptr || textLength(dynLabel)==0 )
        return kInvalidDynVel;

      end = nextWhiteCharEOS(dynLabel);
      assert(end != nullptr );
      char_cnt = end - dynLabel;
      
      for(unsigned i=0; i<p->dynRefN; ++i)
        if( textCompare(p->dynRefA[i].label,dynLabel,char_cnt) == 0 )
          return p->dynRefA[i].vel;
      
      cwLogError(kSyntaxErrorRC,"The dynamic label '%s' is not valid.",cwStringNullGuard(dynLabel));
      return 0;
    }
    */
    
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
    }
    
    rc_t _destroy( sfscore_t* p )
    {
      rc_t rc = kOkRC;

      set_t* s = p->setL;
      while( s!= nullptr )
      {
        set_t* s0 = s->llink;
        _destroy_set(s);
        s=s0;
      }

      for(unsigned i=0; i<p->sectionN; ++i)
        _destroy_section( p->sectionA + i );
      mem::release(p->sectionA);
      
      mem::release(p->eventA);

      /*
      for(unsigned i=0; i<p->dynRefN; ++i)
        mem::release(p->dynRefA[i].label);
      mem::release(p->dynRefA);
      */

      destroy(p->parserH);
      
      mem::release(p);
      return rc;
    }

    /*
    rc_t _parse_bar( sfscore_t* p, event_t* e, const char* barNumbString )
    {
      rc_t rc = kOkRC;
      return rc;
    }

    // Iterate backward from the last event in a set to locate beginning of the set and return
    // a pointer to the first event in the set and the count of events in the set.
    unsigned _calc_set_event_count( sfscore_t* p, const event_t* prev_end_evt, event_t* end_set_evt, unsigned varTypeFlag, event_t*& begEvtRef )
    {
      unsigned n = 0;
      event_t* e0 = end_set_evt;

      begEvtRef = nullptr;
      
      for(; e0>=p->eventA; --e0)
        if( cwIsFlag(e0->flags,varTypeFlag) )
        {
          if( e0 == prev_end_evt )
            break;
          
          ++n;
          begEvtRef = e0;
        }

      return n;      
    }

    // Get the ending event from the previous set of the same var type. 
    const event_t* _get_prev_end_event( sfscore_t* p, unsigned varTypeFlag )
    {
      const set_t* s;
      unsigned varTypeId = _varFlagToTypeId(varTypeFlag);
      for(s=p->setL; s!=nullptr; s=s->llink)
        if( s->varId == varTypeId )
          return s->eleArray[ s->eleCnt-1 ];

      return nullptr;
    }

    section_t* _find_section(sfscore_t* p, const char* sectionLabel )
    {
      for(unsigned i=0; i<p->sectionN; ++i)
        if( textIsEqual(p->sectionA[i].label,sectionLabel) )
          return p->sectionA + i;

      return nullptr;          
    }
    
    rc_t _connect_section_to_set( sfscore_t* p, set_t* set, const char* section_label )
    {
      rc_t rc = kOkRC;

      section_t* section;
      if((section = _find_section(p,section_label)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The section label '%s' could not be found.",cwStringNullGuard(section_label));
        goto errLabel;
      }

      set->sectCnt += 1;
      set->sectArray = mem::resizeZ<section_t*>(set->sectArray,set->sectCnt);
      set->sectArray[ set->sectCnt-1 ] = section;
      
    errLabel:
      return rc;
    }
    
    
    // Create a 'set' record.
    rc_t _create_set( sfscore_t* p, event_t* end_set_evt, unsigned varTypeFlag, const char* section_label )
    {
      rc_t           rc           = kOkRC;
      event_t*       beg_set_evt  = nullptr;
      event_t*       e            = nullptr;
      unsigned       set_eventN   = 0;
      const event_t* prev_end_evt = _get_prev_end_event(p,varTypeFlag);

      // iterate backward to determine the beginning event in this set and the count of events in it
      if((set_eventN = _calc_set_event_count( p, prev_end_evt, end_set_evt, varTypeFlag, beg_set_evt )) == 0 || beg_set_evt == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Unable to locate the '%s' var set.",_varTypeFlagToLabel(varTypeFlag));
        goto errLabel;
      }
      else
      {
        set_t*     set     = mem::allocZ<set_t>();
        unsigned   i;
        
        set->varId        = _varFlagToTypeId(varTypeFlag);
        set->eleCnt       = set_eventN;
        set->eleArray     = mem::allocZ<event_t*>(set_eventN);

        // assign section to set
        if((rc = _connect_section_to_set(p,set,section_label)) != kOkRC )
        {
          cwLogError(rc,"Section '%s' assignment to set failed.",section_label);
          goto errLabel;
        }
        
            
        // fill in s->eleArray[]
        for(i=0,e=beg_set_evt; e<=end_set_evt; ++e)
          if( cwIsFlag(e->flags,varTypeFlag) )
          {
            assert( e > prev_end_evt  );
            assert( i < set->eleCnt );
            set->eleArray[i++] = e;          
          }

        assert( i == set->eleCnt );

        set->llink = p->setL;
        p->setL = set;
      }
      
    errLabel:
      
      return rc;
    }

    // Once the var. spec. label is known to have a section id this function extracts the section label
    unsigned _parse_set_end_section_label( const char* label, const char*& sectionLabelRef )
    {
      rc_t rc = kOkRC;
      const char* c = nullptr;
      
      sectionLabelRef = nullptr;

      // a label with a section will have internal whitespace
      if((c = nextWhiteChar(label)) == nullptr )
        return rc;

      // advance past the white space to the first char of the section id
      if( (c=nextNonWhiteChar(c)) == nullptr )
        goto errLabel;
      
      // parse the section id
      sectionLabelRef = c;

      return rc;
      
    errLabel:
      return cwLogError(kSyntaxErrorRC,"The section number could not be parsed from '%s'.",label);
    }

    
    // Parse the optional set section number following the var spec. label.
    rc_t _parse_var_set_section_label(sfscore_t* p, event_t* e, const char* label, unsigned varTypeFlag )
    {      
      rc_t rc = kOkRC;
      const char* section_label = nullptr;

      // attempt to get the section id following the var marker
      if((rc = _parse_set_end_section_label(label,section_label)) != kOkRC )
        goto errLabel;

      // if a section id was found ...
      if( section_label != nullptr )
      {
        // then create the set
        if((rc = _create_set(p,e,varTypeFlag,section_label)) != kOkRC )
        {
          rc = cwLogError(rc,"Variable set create failed for var type:%s.",_varTypeFlagToLabel(varTypeFlag));
          goto errLabel;
        }
      }
                
    errLabel:
      
      return rc;
    }

    // A variable specification indicate how a note is to be measured. It is contained
    // in the 'tempo','even', and 'dyn' columns.  The specification contains two parts:
    // a char string id ('t','e',<dyn mark> (e.g. p,pp,mf,fff,etc)) followed by
    // an optional section identifier.  The section identifer marks the end
    // of a 'set' of var. spec's and also idicates the section which will be modified
    // according to the measurements.
    rc_t _parse_var_spec( sfscore_t* p, event_t* e, const char* label, unsigned varTypeFlag )
    {
      rc_t rc = kOkRC;
      
      if( label==nullptr || textLength(label)==0)
        return rc;

      // if this is a 'dynamics' marking
      if( varTypeFlag == kDynScFl )
      {
        if((e->dynVal = _dynLabelToLevel(p,label)) == kInvalidDynVel )
        {
          cwLogError(kSyntaxErrorRC,"Note dynamic var spec parse failed on label:%s",cwStringNullGuard(label));
          goto errLabel;
        }
      }
      else
      {
        // this is a 'tempo' or 'even' marking
        if(_varLabelToFlag(label) != varTypeFlag )
        {
          cwLogError(kSyntaxErrorRC,"Note %s var spec parse failed on label:%s.",_varTypeFlagToLabel(varTypeFlag),cwStringNullGuard(label));
          goto errLabel;
        } 
      }

      // parse the optional section id.
      if((rc = _parse_var_set_section_label(p,e,label,varTypeFlag)) != kOkRC )
        goto errLabel;
      
      e->flags |= varTypeFlag;
      
    errLabel:
        return rc;
    }

    rc_t _parse_csv_note(sfscore_t* p,
                         event_t* e,
                         uint8_t d0,
                         uint8_t d1,
                         const char* sciPitch,
                         const char* evenLabel,
                         const char* tempoLabel,
                         const char* dynLabel)
    {
      rc_t rc = kOkRC;

      if((rc = _parse_var_spec(p,e,evenLabel,kEvenScFl )) != kOkRC )
        goto errLabel;
      
      if((rc = _parse_var_spec(p,e,tempoLabel,kTempoScFl )) != kOkRC )
        goto errLabel;

      if((rc = _parse_var_spec(p,e,dynLabel,kDynScFl)) != kOkRC )
        goto errLabel;
      
      e->flags |= _varLabelToFlag(tempoLabel);
      
      e->pitch  = d0;
      e->vel    = d1;

    errLabel:
      return rc;
    }


    rc_t _parse_csv_row( sfscore_t* p, csv::handle_t& csvH, event_t* e, unsigned& curBarNumbRef, unsigned& curBarNoteIdxRef )
    {
      rc_t rc = kOkRC;
      
          const char* opcodeLabel;
          const char* arg0;
          const char* evenLabel;
          const char* tempoLabel;
          const char* dynLabel;
          uint8_t d0,d1;
          const char* sectionLabel;
          
          if((rc = getv( csvH,
                         "opcode",opcodeLabel,
                         "evt", e->csvEventId,
                         "micros",e->secs,
                         "d0",d0,
                         "d1",d1,
                         "arg0",arg0,
                         "bar",e->barNumb,
                         "even",evenLabel,
                         "tempo",tempoLabel,
                         "t_frac",e->frac,
                         "dyn",dynLabel,
                         "section",sectionLabel )) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing score CSV row." );
            goto errLabel;
          }

          // validate the opcode 
          if((e->type = _opcode_label_to_id( opcodeLabel )) == kInvalidId )
          {
            cwLogError(kSyntaxErrorRC,"The opcode type:'%s' is not valid.",cwStringNullGuard(opcodeLabel));
            goto errLabel;
          }
          
          switch( e->type )
          {
            case kBarEvtScId:
              if( curBarNumbRef != kInvalidId && e->barNumb != curBarNumbRef+1 )
              {
                rc = cwLogError(kInvalidStateRC,"Missig bar number %i. Jumped from bar:%i to bar:%i.", curBarNumbRef+1,curBarNumbRef,e->barNumb);
                goto errLabel;
              }
              
              curBarNumbRef = e->barNumb;
              curBarNoteIdxRef = 0;
              break;
              
            case kCtlEvtScId:
              break;
              
            case kNonEvtScId:
              e->barNumb = curBarNumbRef;
              e->barNoteIdx = curBarNoteIdxRef++;
              if((rc = _parse_csv_note(p,e,d0,d1,arg0,evenLabel,tempoLabel,dynLabel)) != kOkRC )
              {
                cwLogError(rc,"Note parse failed.");
                goto errLabel;
              }
              break;
              
            default:
              cwLogError(kInvalidArgRC,"The opcode type '%s' is not valid in this context.",cwStringNullGuard(opcodeLabel));
              goto errLabel;
              break;
          }


          // if this event has a section label
          if( sectionLabel != nullptr and textLength(sectionLabel)>0 )
          {
            section_t* section;
            
            // locate the section record
            if((section = _find_section(p,sectionLabel)) == nullptr )
            {
              rc = cwLogError(kSyntaxErrorRC,"The section label '%s' could not be found.",cwStringNullGuard(sectionLabel));
              goto errLabel;
            }

            // verify that this section was not already assigned a starting event index
            if( section->begEvtIndex != kInvalidIdx )
            {
              rc = cwLogError(kInvalidIdRC,"The section label '%s' appears to be duplicated.",cwStringNullGuard(sectionLabel));
              goto errLabel;
            }

            // assign a starting event index to this event
            section->begEvtIndex = e->index;
          }

    errLabel:
          return rc;
    }

    rc_t _parse_csv_events( sfscore_t* p, csv::handle_t csvH )
    {
      rc_t     rc;
      unsigned cur_line_idx  = 0;
      unsigned curBarNumb    = kInvalidId;
      unsigned curBarNoteIdx = 0;
      
      // get the line count from the CSV file
      if((rc = line_count(csvH,p->eventAllocN)) != kOkRC )
      {
        rc = cwLogError(rc,"Score CSV line count failed.");
        goto errLabel;    
      }

      // allocate the event array
      p->eventA = mem::allocZ<event_t>(p->eventAllocN);

      do
      {
        cur_line_idx = cur_line_index(csvH);

        // advance the CSV line cursor        
        switch(rc = next_line(csvH))
        {
          case kOkRC:
            {
              // verify that there is an available slot in the event array
              if( p->eventN >= p->eventAllocN )
              {
                rc = cwLogError(kBufTooSmallRC,"The event buffer is too small.");
                goto errLabel;
              }
              
              event_t* e    = p->eventA + p->eventN;
              e->index      = p->eventN;
              e->line       = cur_line_idx;
              e->csvRowNumb = cur_line_idx+1;
          
              if((rc = _parse_csv_row(p, csvH, e, curBarNumb, curBarNoteIdx )) != kOkRC )
                goto errLabel;
              

              p->eventN += 1;              
            }
            break;

          case kEofRC:
            break;

          default:
            rc = cwLogError(rc,"CSV line iteration error on CSV event parse.");
            goto errLabel;            
        }
        
      }while( rc != kEofRC );

      rc = kOkRC;

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"CSV parse failed on row number:%i.",cur_line_idx+1);

      return rc;
    }

    // scan the CSV and count the total number of sections
    rc_t  _get_section_count( csv::handle_t csvH, unsigned& sectionCntRef )
    {
      rc_t     rc           = kOkRC ;
      unsigned cur_line_idx = 0;
      unsigned n            = 0;
      
      sectionCntRef = 0;
      do
      {
        cur_line_idx = cur_line_index(csvH);

        // advance the CSV line cursor        
        switch(rc = next_line(csvH))
        {
          case kEofRC:
            break;
            
          case kOkRC:
            {
              const char* sectionLabel = nullptr;
              if((rc = getv(csvH,"section",sectionLabel)) != kOkRC )
              {
                rc = cwLogError(rc,"Error parsing section field.");
                goto errLabel;
              }
              
              if( sectionLabel != nullptr )
                n += 1;
            }
            break;
            
          default:
            rc = cwLogError(rc,"CSV line iteration failed.");
        }
        
      } while( rc != kEofRC );

      rc = kOkRC;

      sectionCntRef = n;
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Section count calculation failed on line index:%i.",cur_line_idx);
      
      return rc;
    }

    // scan the CSV and create a record for every section
    rc_t _parse_section_events(sfscore_t* p, csv::handle_t csvH, unsigned sectionCnt )
    {
      rc_t     rc           = kOkRC;
      unsigned cur_line_idx = 0;

      p->sectionAllocN = sectionCnt;
      p->sectionN      = 0;
      p->sectionA      = mem::allocZ<section_t>(sectionCnt);
      
      do
      {
        cur_line_idx = cur_line_index(csvH);

        // advance the CSV line cursor        
        switch(rc = next_line(csvH))
        {            
          case kEofRC:
            break;
                      
          case kOkRC:
            {
              section_t*  section      = nullptr;
              const char* sectionLabel = nullptr;

              // read the section column
              if((rc = getv(csvH,"section",sectionLabel)) != kOkRC )
              {
                rc = cwLogError(rc,"Error parsing section field.");
                goto errLabel;
              }

              // if the section column is not blank
              if( sectionLabel != nullptr && textLength(sectionLabel)>0)
              {
                // verify that this section was not already created                
                if((section = _find_section(p,sectionLabel)) != nullptr )
                {
                  rc = cwLogError(kInvalidIdRC,"Multiple sections have the id '%s'.",sectionLabel);
                  goto errLabel;
                }
                else
                {
                  // verify that an available slot exists to store the new section reocrd
                  if(p->sectionN >= p->sectionAllocN )
                  {
                    rc = cwLogError(kBufTooSmallRC,"The section array is too small. The section count estimation is incomplete.");
                    goto errLabel;
                  }

                  // create the section record
                  section               = p->sectionA + p->sectionN;
                  section->label        = mem::duplStr(sectionLabel);
                  section->index        = p->sectionN;
                  section->begEvtIndex  = kInvalidIdx;
                  vop::fill(section->vars,kScVarCnt,DBL_MAX);
                  p->sectionN          += 1;
                }
              } 
            }
            break;
          default:
            rc = cwLogError(rc,"CSV line iteration failed.");
        }
        
      } while( rc != kEofRC );

      rc = kOkRC;

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Section event parse failed on line index:%i.",cur_line_idx);
      
      return rc;
    }
    
    rc_t _parse_sections(sfscore_t* p,csv::handle_t csvH)
    {
      rc_t rc;
      unsigned sectionCnt = 0;

      if((rc = _get_section_count(csvH, sectionCnt )) != kOkRC )
        goto errLabel;

      if((rc = rewind(csvH)) != kOkRC )
        goto errLabel;

      if((rc = _parse_section_events(p,csvH,sectionCnt)) != kOkRC )
        goto errLabel;
          
    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Section parse failed.");
      return rc;
    }

    
    rc_t _parse_csv( sfscore_t* p, const char* fname )
    {
      rc_t        rc       = kOkRC;
      const char* titleA[] = { "id","trk","evt","opcode","dticks","micros","status","meta",
                                 "ch","d0","d1","arg0","arg1","bar","skip","even","grace",
                                 "tempo","t_frac","dyn","section","play_recd","remark" };
      unsigned      titleN   = sizeof(titleA)/sizeof(titleA[0]);
      csv::handle_t csvH;

      // open the CSV file and validate the title row
      if((rc = create( csvH, fname, titleA, titleN )) != kOkRC )
      {
        rc = cwLogError(rc,"Score CSV parse failed on '%s'.",fname);
        goto errLabel;
      }

      // create p->sectionA[]
      if((rc = _parse_sections(p,csvH)) != kOkRC )
        goto errLabel;

      if((rc = rewind(csvH)) != kOkRC )
        goto errLabel;

      // create p->eventA[] 
      if((rc = _parse_csv_events(p,csvH)) != kOkRC )        
        goto errLabel;
        
    errLabel:
      if(rc != kOkRC )
         rc = cwLogError(rc,"CSV parse failed on '%s'.",fname);
      destroy(csvH);
      return rc;
    }
    */
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

void cw::sfscore::report( handle_t h )
{
  sfscore_t* p = _handleToPtr(h);
  report(p->parserH);
}

cw::rc_t cw::sfscore::test( const object_t* cfg )
{
  rc_t rc = kOkRC;
  const char* cm_score_fname = nullptr;
  const object_t* dynArrayNode = nullptr;
  dyn_ref_t* dynRefA = nullptr;
  unsigned dynRefN = 0;
  double srate = 0;
  handle_t h;
  time::spec_t t0;
  

  // parse the test cfg
  if((rc = cfg->getv( "cm_score_fname", cm_score_fname,
                      "srate", srate,
                      "dyn_ref", dynArrayNode )) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore test parse params failed on.");
    goto errLabel;
  }

  // parse the dynamics ref. array
  dynRefN = dynArrayNode->child_count();
  dynRefA = mem::allocZ<dyn_ref_t>(dynRefN);
  
  for(unsigned i=0; i<dynRefN; ++i)
  {
    const object_t* pair = dynArrayNode->child_ele(i);
    
    if( !pair->is_pair() || pair->pair_label()==nullptr || pair->pair_value()==nullptr || pair->pair_value()->value( dynRefA[i].vel)!=kOkRC )
    {
      rc = cwLogError(kSyntaxErrorRC,"Error parsing the dynamics reference array.");
      goto errLabel;
    }
    
    dynRefA[i].label = pair->pair_label();
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
