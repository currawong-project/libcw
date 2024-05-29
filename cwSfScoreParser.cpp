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
#include "cwSfScoreParser.h"
#include "cwSfScore.h"

namespace cw
{
  namespace sfscore
  {
    typedef struct varMap_str
    {
      unsigned    typeId;
      unsigned    flag;
      unsigned    endFl;
      const char* label;
    } varMap_t;

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
      { kEvenVarScId, kEvenFl,                0,           "e"},
      { kEvenVarScId, kEvenFl | kEvenEndFl,   kEvenEndFl,  "E"},
      { kDynVarScId,  kDynFl,                 0,           "d"},
      { kDynVarScId,  kDynFl | kDynEndFl,     kDynEndFl,   "D"},
      { kTempoVarScId,kTempoFl,               0,           "t"},
      { kTempoVarScId,kTempoFl | kTempoEndFl, kTempoEndFl, "T"},      
      { kInvalidId,   0,                      0,           "<invalid>"}
    };

    namespace parser
    {
      typedef struct dynRef_str
      {
        char*    label;
        unsigned labelCharCnt;
        uint8_t  vel;
      } dynRef_t;
      
      typedef struct sfscore_parser_str
      {
        unsigned  dynRefN;
        dynRef_t* dynRefA;
        
        p_section_t* begSectionL;
        p_section_t* endSectionL;

        unsigned nextSetId;
        p_set_t* begSetL;
        p_set_t* endSetL;

        unsigned   eventAllocN;
        unsigned   eventN;
        p_event_t* eventA;
      
      } sfscore_parser_t;

      sfscore_parser_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,sfscore_parser_t>(h); }

      unsigned _dyn_label_to_level( sfscore_parser_t* p, const char* dynLabel )
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
        return kInvalidDynVel;
      }
      
      rc_t _destroy( sfscore_parser_t* p )
      {
        for(p_set_t* s = p->begSetL; s!=nullptr; )
        {
          p_set_t* s0 = s->link;
          mem::release(s->eventA);
          mem::release(s);
          s = s0;
        }
        
        for(p_section_t* s = p->begSectionL; s!=nullptr; )
        {
          p_section_t* s0 = s->link;
          mem::release(s->label);
          mem::release(s);
          s = s0;
        }
        
        for(p_event_t* e = p->eventA; e<p->eventA+p->eventN; ++e)
        {
          mem::release(e->sciPitch);
          for(unsigned i=kMinVarScId; i<kScVarCnt; ++i)            
            mem::release(e->sectionLabelA[i]);
        }

        for(dynRef_t* d=p->dynRefA; d<p->dynRefA + p->dynRefN; ++d)
          mem::release(d->label);
        
        mem::release(p->dynRefA);
        mem::release(p->eventA);
        mem::release(p);
        
        return kOkRC;
      }

      p_section_t* _find_section( sfscore_parser_t* p, const char* section_label)
      {
        for(p_section_t* s = p->begSectionL; s!=nullptr; s=s->link)
          if( textIsEqual(s->label,section_label) )
            return s;
        return nullptr;
      }

      p_section_t* _create_section( sfscore_parser_t* p, const char* section_label, unsigned begEvtIdx )
      {
        p_section_t* s;

        if((s = _find_section(p,section_label)) != nullptr )
        {
          s = nullptr;
          cwLogError(kInvalidIdRC,"Duplicate section label (%s) detected.",cwStringNullGuard(section_label));
          goto errLabel;
        }
        else
        {
          s = mem::allocZ<p_section_t>();

          s->label     = mem::duplStr(section_label);
          s->begEvtIdx = begEvtIdx;
          
          if( p->endSectionL == nullptr )
          {
            p->endSectionL = s;
            assert( p->begSectionL == nullptr );
            p->begSectionL = s;
          }
          else
          {
            p->endSectionL->link = s;
            p->endSectionL = s;          
          }
        }

      errLabel:
        return s;
        
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
      rc_t _parse_var_set_section_label(sfscore_parser_t* p, p_event_t* e, const char* label, unsigned varTypeEndFlag, char*& sectionLabelRef )
      {      
        rc_t rc = kOkRC;

        const char* section_label = nullptr;

        sectionLabelRef = nullptr;
        
        // attempt to get the section id following the var marker
        if((rc = _parse_set_end_section_label(label,section_label)) != kOkRC )
          goto errLabel;

        // if a section id was found then duplicate it
        if( section_label != nullptr )
        {
          sectionLabelRef = mem::duplStr(section_label);
          e->flags |= varTypeEndFlag;
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
      rc_t _parse_var_spec( sfscore_parser_t* p, p_event_t* e, const char* mark_label, unsigned varTypeFlag, unsigned varTypeEndFlag, char*& sectionLabelRef )
      {
        rc_t rc = kOkRC;

        unsigned flags = 0;
        sectionLabelRef = nullptr;
        
        if( mark_label==nullptr || textLength(mark_label)==0)
          return rc;

        switch( varTypeFlag )
        {
          case kDynFl:
            if((e->dynVal = _dyn_label_to_level(p,mark_label)) == kInvalidDynVel )
            {
              cwLogError(kSyntaxErrorRC,"Note dynamic var spec parse failed on label:%s",cwStringNullGuard(mark_label));
              goto errLabel;
            }
            flags |= kDynFl;
            break;
            
          case kEvenFl:
          case kTempoFl:
            flags= var_label_to_type_flag(mark_label);            
            break;

          default:
            rc = cwLogError(kSyntaxErrorRC,"The var spec flag '%s' is not valid.",cwStringNullGuard(mark_label));
            goto errLabel;
        }


        if( cwIsFlag(e->flags,varTypeFlag ) )
        {
          rc = cwLogError(kInvalidIdRC,"The var spec flag was expected to be '%s' but instead was '%s'.",var_type_flag_to_label(varTypeFlag),var_type_flag_to_label(flags));
          goto errLabel;
        }
        
        // parse the optional section id.
        if((rc = _parse_var_set_section_label(p,e,mark_label,varTypeEndFlag,sectionLabelRef)) != kOkRC )
          goto errLabel;

            e->flags |= flags;
      errLabel:
        return rc;
      }

      rc_t _parse_csv_note(sfscore_parser_t* p,
                           p_event_t* e,
                           uint8_t d0,
                           uint8_t d1,
                           const char* sciPitch,
                           const char* evenLabel,
                           const char* tempoLabel,
                           const char* dynLabel)
      {
        rc_t rc = kOkRC;

        if( textLength(sciPitch)<2 )
        {
          rc = cwLogError(kSyntaxErrorRC,"Blank or invalid scientific pitch.");
          goto errLabel;
        }

        if((rc = _parse_var_spec(p,e,evenLabel,kEvenFl,kEvenEndFl,e->sectionLabelA[kEvenVarScId] )) != kOkRC )
          goto errLabel;
      
        if((rc = _parse_var_spec(p,e,tempoLabel,kTempoFl,kTempoEndFl,e->sectionLabelA[kTempoVarScId] )) != kOkRC )
          goto errLabel;

        if((rc = _parse_var_spec(p,e,dynLabel,kDynFl,kDynEndFl,e->sectionLabelA[kDynVarScId])) != kOkRC )
          goto errLabel;
      
        e->pitch  = d0;
        e->vel    = d1;
        e->sciPitch = mem::duplStr(sciPitch);
        
      errLabel:
        return rc;
      }


      rc_t _parse_csv_row( sfscore_parser_t* p,
                           csv::handle_t&    csvH,
                           unsigned          cur_line_idx,
                           double&           curSecRef,
                           unsigned&         curLocIdxRef,
                           p_section_t*&     curSectionRef,
                           unsigned&         curSectionNoteIdxRef,
                           unsigned&         curBarNumbRef,
                           unsigned&         curBarNoteIdxRef )
      {
        rc_t rc = kOkRC;
      
        const char* opcodeLabel;
        const char* arg0;
        const char* evenLabel;
        const char* tempoLabel;
        const char* dynLabel;
        uint8_t d0,d1;
        const char* sectionLabel;
        p_event_t* e  = p->eventA + p->eventN;

        // verify that there is an available slot in the event array
        if( p->eventN >= p->eventAllocN )
        {
          rc = cwLogError(kBufTooSmallRC,"The event record array is too small.");
          goto errLabel;
        }
                  
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
                       "t_frac",e->t_frac,
                       "dyn",dynLabel,
                       "section",sectionLabel )) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing score CSV row." );
          goto errLabel;
        }

        // validate the opcode 
        if((e->typeId = opcode_label_to_id( opcodeLabel )) == kInvalidId )
        {
          cwLogError(kSyntaxErrorRC,"The opcode type:'%s' is not valid.",cwStringNullGuard(opcodeLabel));
          goto errLabel;
        }
          
        switch( e->typeId )
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
            {
              if( e->secs != curSecRef )
              {
                curSecRef    = e->secs;
                curLocIdxRef += 1;
              }
              
              e->locIdx     = curLocIdxRef;
              e->index      = p->eventN;
              e->line       = cur_line_idx;
              e->csvRowNumb = cur_line_idx+1;                      
              e->barNumb    = curBarNumbRef;
              e->barNoteIdx = curBarNoteIdxRef++;
                            
              if((rc = _parse_csv_note(p,e,d0,d1,arg0,evenLabel,tempoLabel,dynLabel)) != kOkRC )
              {
                cwLogError(rc,"Note parse failed.");
                goto errLabel;
              }
              
              // if this event has a section label
              if( sectionLabel != nullptr and textLength(sectionLabel)>0 )
              {
                // locate the section record
                if((curSectionRef = _create_section(p,sectionLabel,e->index)) == nullptr )
                {
                  rc = cwLogError(kOpFailRC,"The section label '%s' create failed.",cwStringNullGuard(sectionLabel));
                  goto errLabel;
                }
                curSectionNoteIdxRef = 0;
              }

              

              e->section            = curSectionRef;
              e->sectionIdx         = curSectionNoteIdxRef;
              curSectionNoteIdxRef += 1;
              
              p->eventN += 1;
            }
            break;
              
          default:
            cwLogError(kInvalidArgRC,"The opcode type '%s' is not valid in this context.",cwStringNullGuard(opcodeLabel));
            goto errLabel;
            break;
        }

      errLabel:
        return rc;
      }

      rc_t _parse_events( sfscore_parser_t* p, csv::handle_t csvH )
      {
        rc_t         rc;
        unsigned     cur_line_idx      = 0;
        double       curSec            = 0;
        unsigned     curLocIdx         = 0;
        p_section_t* curSection        = nullptr;
        unsigned     curSectionNoteIdx = 0;
        unsigned     curBarNumb        = kInvalidId;
        unsigned     curBarNoteIdx     = 0;
        
        // get the line count from the CSV file
        if((rc = line_count(csvH,p->eventAllocN)) != kOkRC )
        {
          rc = cwLogError(rc,"Score CSV line count failed.");
          goto errLabel;    
        }

        // allocate the event array
        p->eventA = mem::allocZ<p_event_t>(p->eventAllocN);

        do
        {
          cur_line_idx = cur_line_index(csvH);

          // advance the CSV line cursor        
          switch(rc = next_line(csvH))
          {
            case kOkRC:
              {
                        
                if((rc = _parse_csv_row(p, csvH, cur_line_idx, curSec, curLocIdx, curSection, curSectionNoteIdx, curBarNumb, curBarNoteIdx )) != kOkRC )
                  goto errLabel;              

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

      rc_t _parse_csv( sfscore_parser_t* p, const char* fname )
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

        // parse the events
        if((rc = _parse_events(p, csvH )) != kOkRC )
          goto errLabel;

      errLabel:
        if(rc != kOkRC )
          rc = cwLogError(rc,"CSV parse failed on '%s'.",fname);
        destroy(csvH);
        return rc;
      }


      
      // Allocate a new set record.
      p_set_t* _alloc_set( sfscore_parser_t* p, unsigned varTypeId, p_event_t* beg_evt )
      {
        p_set_t* s   = mem::allocZ<p_set_t>();
        s->varTypeId = varTypeId;
        s->beg_event = beg_evt;
        s->id        = p->nextSetId++;

        if( p->endSetL == nullptr )
        {
          p->begSetL = s;
          p->endSetL = s;
        }
        else
        {
          p->endSetL->link = s;
          p->endSetL = s;
        }
          
        return s;
      }

      // Fill a set eventA[] by iterating from set->beg_event to 'end_event'
      // and storing all events of type set->varTypeid.
      rc_t _fill_set( sfscore_parser_t* p, p_set_t* set, p_event_t* end_event )
      {
        rc_t     rc         = kOkRC;
        unsigned refVarFlag = var_type_id_to_flag(set->varTypeId);
        unsigned evtIdx     = 0;
        
        set->eventA = mem::allocZ<p_event_t*>(set->eventN);
        assert( set->beg_event <= end_event );

        for(p_event_t* e = set->beg_event; e<=end_event && evtIdx < set->eventN; ++e)
          if( cwIsFlag(e->flags,refVarFlag) )
            set->eventA[evtIdx++] = e;            

        if( evtIdx != set->eventN )
        {
          rc = cwLogError(kOpFailRC,"%i events were located for set '%i' but %i were filled.",set->eventN,set->id,evtIdx);
          goto errLabel;
        }
        
      errLabel:
        return rc;        
      }


      // Returns true if the event 'e' is the last event in a set of type 'varTypeId'.
      bool _is_end_of_set( sfscore_parser_t* p, unsigned varTypeId, p_event_t* e )
      {

        unsigned varTypeMask = var_type_id_to_mask(varTypeId);

        // if this e is marked as end-of-set
        if( cwAllFlags(e->flags,varTypeMask) )
        {
          
          if( e->index < p->eventN-1 )
          {
            p_event_t* e1 = e + 1;
            // ... and the next event is at the same loc and also marked as end-of-set don't end the set
            if( e1->locIdx == e->locIdx && cwAllFlags(e1->flags,varTypeMask) )
            {
              e->flags = cwClrFlag(e->flags,var_type_id_to_end_flag(varTypeId));
              return false;
            }
          }
          
          return true;
        }
        
        return false;
      }

      // Register an event to the set to which it belongs.  If the set does not exist then create it.
      // Setup pointers from the set to the event and the set to the event.
      rc_t  _register_event_with_set( sfscore_parser_t* p, p_event_t* e, unsigned varTypeId,  p_set_t*& setRef )
      {
        rc_t rc = kOkRC;

        // if there is no set to register this event in then create a new set
        if( setRef == nullptr )
          setRef = _alloc_set(p,varTypeId,e);
          
        setRef->eventN += 1;
        
        e->setA[ varTypeId ] = setRef;

        // if this event is marked as an end-of-set
        if( _is_end_of_set(p,varTypeId,e) )
        {
          if((rc = _fill_set(p, setRef, e )) != kOkRC )
            goto errLabel;

          setRef = nullptr;
        }
        
      errLabel:
        return kOkRC;
      }

      // Check for the existence of each var type on each event and then register the event with the set.
      rc_t _create_sets( sfscore_parser_t* p )
      {
        rc_t       rc = kOkRC;
        p_set_t*   setA[ kScVarCnt ];
        p_event_t* e  = p->eventA;

        for(unsigned i=kMinVarScId; i<kScVarCnt; ++i)
          setA[i] = nullptr;

        // for each event
        for(; e<p->eventA + p->eventN; ++e)
        {
          // for each possible var type
          for(unsigned refVarTypeId=0; refVarTypeId<kScVarCnt; ++refVarTypeId)
          {
            // get the flag for this var type
            unsigned refVarTypeFlag = var_type_id_to_flag(refVarTypeId);

            // is this event of this type of var
            if( cwIsFlag(e->flags,refVarTypeFlag) )
            {
              // insert the event into a set
              if((rc = _register_event_with_set(p, e, refVarTypeId, setA[ refVarTypeId ])) != kOkRC )
              {
                rc = cwLogError(rc,"Event '%s' set registration failed.",var_type_id_to_label(refVarTypeId));
                goto errLabel;
              }
            }
          }
        }

      errLabel:
        return rc;
      }


      // Assign a target section to each set. Since only the last set of a consecutive set of sets
      // has a target section id this function iterates backwards throught the events, 
      // notices the last event in the last set of a group of sets, and then propogates the
      // associated target section back to previous sets.
      rc_t _assign_set_sections(sfscore_parser_t* p )
      {
        rc_t rc = kOkRC;

        // for each possible var type
        for(unsigned refVarTypeId=kMinVarScId; refVarTypeId<kScVarCnt; ++refVarTypeId )
        {
          p_section_t* section = nullptr;
          
          p_event_t* e = p->eventA + p->eventN - 1;

          // iterate backward through the event list
          for(; e>=p->eventA; --e)            
          {
            // if this event is the end of a section for this variable type - then previous sets of this type will be assigned to this section
            if( e->sectionLabelA[refVarTypeId]!=nullptr )
            {
              // locate the section
              if((section = _find_section( p, e->sectionLabelA[ refVarTypeId ] )) == nullptr )
              {
                rc = cwLogError(kInvalidIdRC,"The section label '%s' at CSV line '%i' could not be found.", cwStringNullGuard(e->sectionLabelA[ refVarTypeId ]),e->csvRowNumb);
                goto errLabel;
              }
              
            }

            // if this event is assigned to a set of type refVarTypeId
            if( e->setA[ refVarTypeId ] != nullptr )
            {

              // has this set already been assigned to a target
              if( e->setA[ refVarTypeId ]->target_section != nullptr && e->setA[ refVarTypeId ]->target_section != section )
              {
                cwLogWarning("The set '%i' was previously assigned to a different section:'%s' and is now assigned to '%s'.",
                             e->setA[ refVarTypeId ]->id,
                             e->setA[ refVarTypeId ]->target_section->label,
                             section->label);
              }

              // assign a section to this set
              e->setA[ refVarTypeId ]->target_section = section;
            }
            
          }
        }

      errLabel:
        return rc;
      }

      // Validate as many set invariants as possible.
      rc_t _validate_sets( sfscore_parser_t* p )
      {
        rc_t rc = kOkRC;
        
        for(p_set_t* s = p->begSetL; s!=nullptr; s=s->link)
        {
          // the set must be assigned a target section
          if( s->target_section == nullptr )
          {
            rc = cwLogError(kInvalidStateRC,"Set (id=%i) of type '%s' beginning at (csv row:%i bar:%i bni:%i) has not been assigned a target section.", s->id, var_type_id_to_label(s->varTypeId), s->beg_event->csvRowNumb, s->beg_event->barNumb, s->beg_event->barNoteIdx );
            continue;
          }

          // tempo and even sets must have at least 3 members
          if( (s->varTypeId == kEvenVarScId || s->varTypeId == kTempoVarScId) && s->eventN < 3 )
          {
            cwLogWarning("Set (id=%i) of type '%s' beginning at (csv row:%i bar:%i bni:%i) should have at least 3 members.", s->id, var_type_id_to_label(s->varTypeId), s->beg_event->csvRowNumb, s->beg_event->barNumb, s->beg_event->barNoteIdx );
            continue;
          }

          p_event_t* e0 = nullptr;

          // for each set member
          for(unsigned i=0; i<s->eventN; ++i)
          {
            p_event_t* e1 = s->eventA[i];

            // the set->eventA[] must be filled with valid pointers
            if( e1 == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"Set (id=%i) of type '%s' beginning at (csv row:%i bar:%i bni:%i) contains a NULL event.", s->id, var_type_id_to_label(s->varTypeId), s->beg_event->csvRowNumb, s->beg_event->barNumb, s->beg_event->barNoteIdx );
              goto errLabel;
            }

            // the events contained in this set must also point to this set
            if( e1->setA[ s->varTypeId ] != s )
              rc = cwLogError(kInvalidStateRC,"Set (id=%i) of type '%s' contains an event (csv row:%i bar:%i bni:%i) whose set pointer is not pointing to this set.", s->id, var_type_id_to_label(s->varTypeId), e1->csvRowNumb, e1->barNumb, e1->barNoteIdx );

            // the events must be ordered in increasing time
            if( e0 != nullptr && e0->secs > e1->secs )
              rc = cwLogError(kInvalidStateRC,"Set (id=%i) of type '%s' contains an event (csv row:%i bar:%i bni:%i) which is out of time order with the previous set event.", s->id,var_type_id_to_label(s->varTypeId), e1->csvRowNumb, e1->barNumb, e1->barNoteIdx );

            // the locations must be ordered in increasing time
            if( e0 != nullptr && e0->locIdx > e1->locIdx )
              rc = cwLogError(kInvalidStateRC,"Set (id=%i) of type '%s' contains an event (csv row:%i bar:%i bni:%i) which is out of loc. order with the previous set event.", s->id,var_type_id_to_label(s->varTypeId), e1->csvRowNumb, e1->barNumb, e1->barNoteIdx );

            e0 = e1;
          }

        } 
      errLabel:
        return rc;
      }
      
      
      // Set section targets must occur on events prior to the first event in the target section
      rc_t _validate_section_assignments( sfscore_parser_t* p )
      {
        rc_t rc = kOkRC;
        
        // for each event
        for(p_event_t* e=p->eventA; e<p->eventA+p->eventN; ++e)
        {

          // verify that this event has been assigned to a section
          if( e->section == nullptr )
          {
            rc = cwLogError(kInvalidStateRC,"The event at csv row:%i bar:%i bni:%i was not assigned to a section.",
                            e->csvRowNumb,e->barNumb,e->barNoteIdx);
            continue;
          }

          // for each possible var type that this event may belong to
          for(unsigned refVarTypeId=kMinVarScId; refVarTypeId<kScVarCnt; ++refVarTypeId)
            if( e->setA[ refVarTypeId ] != nullptr )
            {
              // verify that the target section for this set begins after this event
              if( textCompare(e->setA[ refVarTypeId ]->target_section->label,e->section->label) <= 0  )
              {                
                rc = cwLogError(kInvalidStateRC,"The target section for '%s' of the event at csv row:%i bar:%i bni:%i is after the event.",
                                var_type_id_to_label(refVarTypeId),e->csvRowNumb,e->barNumb,e->barNoteIdx);
                continue;
              }
            }
        }

        return rc;
      }
      
    }
  }
}

unsigned    cw::sfscore::opcode_label_to_id( const char* label )
{
  unsigned id;
  if((id = labelToId( _opcodeMapA, label, kInvalidId )) == kInvalidId )
    cwLogError(kInvalidArgRC,"'%s' is not a valid event opcode type label.",cwStringNullGuard(label));
  
  return id;        
}

const char* cw::sfscore::opcode_id_to_label( unsigned opcode_id )
{
  const char* label;
  if((label = idToLabelNull( _opcodeMapA, opcode_id, kInvalidEvtScId)) == nullptr )
    cwLogError(kInvalidArgRC,"The event opcode type id '%i' is not valid.",opcode_id);
  
  return label;
}

unsigned    cw::sfscore::var_label_to_type_id( const char* label )
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

unsigned    cw::sfscore::var_label_to_type_flag( const char* varLabel )
{
  if(varLabel==nullptr || textLength(varLabel) == 0)
    return 0;
  
  for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
    if( textCompare(_varMapA[i].label,varLabel,textLength(_varMapA[i].label))==0 )
      return _varMapA[i].flag;


  cwLogError(kInvalidArgRC,"The variable type label '%s' is not valid.",cwStringNullGuard(varLabel));
      
  return 0;  
}

const char* cw::sfscore::var_type_id_to_label( unsigned varTypeId )
{
  for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
    if( _varMapA[i].typeId == varTypeId )
      return _varMapA[i].label;
      
  cwLogError(kInvalidArgRC,"The variable type id '%i' is not valid.",varTypeId);
      
  return nullptr;
}
  
const char* cw::sfscore::var_type_flag_to_label( unsigned varTypeFlag )
{
  for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
    if( _varMapA[i].flag == (varTypeFlag & kFlagMask) )
      return _varMapA[i].label;
        
  return nullptr;  
}

char cw::sfscore::var_type_flag_to_char( unsigned varTypeFlag )
{
  const char* s;
  if((s = var_type_flag_to_label(varTypeFlag)) != nullptr )
    return *s;
  return ' ';
}

  
unsigned    cw::sfscore::var_type_id_to_flag( unsigned varTypeId )
{
  for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
    if( _varMapA[i].typeId == varTypeId && _varMapA[i].endFl == false )
      return _varMapA[i].flag;
      
  return 0;      
}

unsigned    cw::sfscore::var_type_id_to_mask( unsigned varTypeId )
{
  for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
    if( _varMapA[i].typeId == varTypeId && _varMapA[i].endFl )
      return _varMapA[i].flag;
      
  return 0;      
}

unsigned    cw::sfscore::var_type_id_to_end_flag( unsigned varTypeId )
{
  for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
    if( _varMapA[i].typeId == varTypeId && _varMapA[i].endFl )
      return _varMapA[i].endFl;
      
  return 0;      
}


unsigned    cw::sfscore::var_type_flag_to_id( unsigned varTypeFlag )
{
  for(unsigned i=0; _varMapA[i].typeId != kInvalidId; ++i)
    if( _varMapA[i].flag == varTypeFlag )
      return _varMapA[i].typeId;
      
  cwLogError(kInvalidArgRC,"The variable flag id '0x%x' is not valid.",varTypeFlag);
  return kInvalidId;      
}


    
cw::rc_t cw::sfscore::parser::create( handle_t& hRef, const char* fname, const dyn_ref_t* dynRefA, unsigned dynRefN )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  sfscore_parser_t* p = mem::allocZ<sfscore_parser_t>();

  p->dynRefN = dynRefN;
  p->dynRefA = mem::allocZ<dynRef_t>(p->dynRefN);
  for(unsigned i=0; i<p->dynRefN;  ++i)
  {
    if( dynRefA[i].vel == kInvalidDynVel )
    {
      cwLogError(kInvalidArgRC,"The value '%i' is reserved to mark invalid values and cannot be used in the dynamic reference array.",kInvalidDynVel);
      goto errLabel;
    }
    else
    {
      p->dynRefA[i].label        = mem::duplStr(dynRefA[i].label);
      p->dynRefA[i].labelCharCnt = textLength(dynRefA[i].label);
      p->dynRefA[i].vel          = dynRefA[i].vel;
    }
  }

  if((rc = _parse_csv( p, fname )) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore CSV parse failed.");
    goto errLabel;
  }

  if((rc = _create_sets(p)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore var. set creation failed.");
    goto errLabel;
  }

  if((rc = _assign_set_sections(p)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore set target section assignmet failed.");
    goto errLabel;
  }


  if((rc = _validate_sets( p )) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore set validation failed.");
    goto errLabel;
  }
  
  if((rc = _validate_section_assignments( p )) != kOkRC )
    goto errLabel;

  
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
  {
    rc = cwLogError(rc,"sfscore create failed on '%s'.",cwStringNullGuard(fname));
    _destroy(p);
  }
  return rc;
    
}

cw::rc_t cw::sfscore::parser::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  sfscore_parser_t* p = nullptr;;
  
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
  
}

unsigned cw::sfscore::parser::event_count( handle_t h )
{
  sfscore_parser_t* p = _handleToPtr(h);
  return p->eventN;
}

const cw::sfscore::parser::p_event_t* cw::sfscore::parser::event_array( handle_t h )
{
  sfscore_parser_t* p = _handleToPtr(h);
  return p->eventA;
}

unsigned cw::sfscore::parser::section_count( handle_t h )
{
  sfscore_parser_t* p = _handleToPtr(h);
  unsigned n=0;
  for(p_section_t* s=p->begSectionL; s!=nullptr; s=s->link)
    ++n;
  return n;
}

const cw::sfscore::parser::p_section_t* cw::sfscore::parser::section_list( handle_t h )
{
  sfscore_parser_t* p = _handleToPtr(h);
  return p->begSectionL;
}

unsigned cw::sfscore::parser::set_count( handle_t h )
{
  sfscore_parser_t* p = _handleToPtr(h);
  unsigned n=0;
  for(p_set_t* s=p->begSetL; s!=nullptr; s=s->link)
    ++n;
  return n;  
}

const cw::sfscore::parser::p_set_t* cw::sfscore::parser::set_list( handle_t h )
{
  sfscore_parser_t* p = _handleToPtr(h);
  return p->begSetL;
}

void cw::sfscore::parser::report( handle_t h )
{
  sfscore_parser_t* p = _handleToPtr(h);
  unsigned bar0 = 0;
  const char* sec0 = nullptr;
  const char* blank_str = "  ";
  const char* sec_str = "S:";
  const char* bar_str = "B:";
    
  printf("e idx  loc   secs   op  sectn  sdx  bar  bdx scip vel  frac\n");
  printf("----- ----- ------- --- ------ --- ----- --- ---- --- -----\n");

  
  for(p_event_t* e = p->eventA; e<p->eventA+p->eventN; ++e)
  {

    const char* d_bar_str = bar0 != e->barNumb ? bar_str : blank_str;
    const char* d_sec_str = e->section != nullptr && textIsEqual(sec0,e->section->label) ? blank_str : sec_str;
      
    bar0 = e->barNumb;
    if( e->section != nullptr )
      sec0 = e->section->label;

    printf("%5i %5i %7.3f %3s %2s%4s %3i %2s%3i %3i %4s %3i %5.3f ",
           e->index,
           e->locIdx,
           e->secs,
           opcode_id_to_label(e->typeId),
           d_sec_str,
           e->section==nullptr ? "    " : cwStringNullGuard(e->section->label),
           e->sectionIdx,
           d_bar_str,
           e->barNumb,
           e->barNoteIdx,
           e->sciPitch,
           e->vel,
           e->t_frac );

    for(unsigned refVarTypeId=kMinVarScId; refVarTypeId<kScVarCnt; ++refVarTypeId)
    {
      if( e->setA[refVarTypeId] == nullptr )
        printf("           ");
      else
      {
        unsigned varRefFlags = var_type_id_to_mask(refVarTypeId);
        const char* sect_label = e->setA[refVarTypeId]->target_section==nullptr ? "****" : e->setA[refVarTypeId]->target_section->label;
        printf("%c-%03i-%s ",var_type_flag_to_char(e->flags & varRefFlags), e->setA[refVarTypeId]->id, sect_label);
      }
    }

    printf("\n");
  }
 
}

cw::rc_t cw::sfscore::parser::test( const char* fname, const dyn_ref_t* dynRefA, unsigned dynRefN )
{
  handle_t h;
  rc_t rc;
  if((rc = create(h,fname,dynRefA,dynRefN)) != kOkRC )
    goto errLabel;

 errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"Parser test failed.");
  
  rc_t rc1 = destroy(h);

  return rcSelect(rc,rc1);
}
