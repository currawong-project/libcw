#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwFileSys.h"
#include "cwCsv.h"
#include "cwNumericConvert.h"
#include "cwTime.h"
#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwVectOps.h"

namespace cw
{
  namespace score_parse
  {
    typedef struct label_id_str
    {
      unsigned    id;
      const char* label;
    } label_id_t;

    typedef struct var_ref_str
    {
      unsigned    flags;
      unsigned    id;
      const char* label;
    } var_ref_t;
    
    typedef struct dynRef_str
    {
      char*    label;
      unsigned level;
      uint8_t  vel;
    } dynRef_t;

    typedef struct score_parse_str
    {
      csv::handle_t csvH;

      double        srate;

      dyn_ref_tbl::handle_t dynRefH;

      set_t*        begSetL;
      set_t*        endSetL;
      
      section_t*    sectionL;
      
      unsigned      eventAllocN;
      unsigned      eventN;
      event_t*      eventA;
      
    } score_parse_t;

    label_id_t _opcode_ref[] = {
      { kBarTId,     "bar" },
      { kSectionTId, "sec" },
      { kBpmTId,     "bpm" },
      { kNoteOnTId,  "non" },
      { kNoteOffTId, "nof" },
      { kPedalTId,   "ped" },
      { kRestTId,    "rst" },
      { kCtlTId,     "ctl" },
      { kInvalidTId, "<inv>" }
    };

    var_ref_t _var_ref[] = {
      { kDynVarFl,                 kDynVarIdx,   "d" },
      { kDynVarFl | kSetEndVarFl,  kDynVarIdx,   "D" },
      { kEvenVarFl,                kEvenVarIdx,  "e" },
      { kEvenVarFl | kSetEndVarFl, kEvenVarIdx,  "E" },
      { kTempoVarFl,               kTempoVarIdx, "t" },
      { kTempoVarFl| kSetEndVarFl, kTempoVarIdx, "T" },
      { 0,                         kInvalidIdx, "<inv>" }
    };

    

    score_parse_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,score_parse_t>(h); }

    rc_t _destroy( score_parse_t* p )
    {
      rc_t rc = kOkRC;

 
      section_t* s = p->sectionL;
      while( s != nullptr )
      {
        section_t* s0 = s->link;
        mem::release(s->label);
        mem::release(s->setA);
        mem::release(s);
        s = s0;        
      }

      set_t* set = p->begSetL;
      while( set!=nullptr )
      {
        set_t* s0 = set->link;
        mem::release(set->eventA);
        mem::release(set);
        set = s0;
      }
      

      for(unsigned i=0; i<p->eventN; ++i)
        mem::release(p->eventA[i].sciPitch);
      
      mem::release(p->eventA);
      mem::release(p);
      return rc;
    }
    
    rc_t _parse_bar_row( score_parse_t* p, csv::handle_t csvH, event_t* e  )
    {
      rc_t rc;
      
      if((rc = getv(csvH,"bar",e->barNumb)) != kOkRC )
        rc = cwLogError(rc,"Bar row parse failed.");

      return rc;
    }

    event_t* _hash_to_event( score_parse_t* p, unsigned hash )
    {
      for(unsigned i=0; i<p->eventN; ++i)
        if( p->eventA[i].hash == hash )
          return p->eventA + i;
      return nullptr;
    }

    section_t* _find_section( score_parse_t* p, const char* sectionLabel )
    {
      section_t* s = p->sectionL;
      for(; s!=nullptr; s=s->link)
        if( textIsEqual(s->label,sectionLabel) )
          return s;

      return nullptr;
    }
    
    section_t* _find_or_create_section( score_parse_t* p, const char* sectionLabel )
    {
      section_t* s;
      if((s = _find_section(p,sectionLabel)) == nullptr )
      {
        s = mem::allocZ<section_t>();

        s->label = mem::duplStr(sectionLabel);
        s->link  = p->sectionL;
        p->sectionL = s;        
      }

      //if( s != nullptr )
      //  printf("Section:%s\n",s->label);
      
      return s;
    }

    rc_t _parse_section_stats( score_parse_t* p, csv::handle_t csvH, event_t* e )
    {
      rc_t rc;
      
      if((rc = getv(csvH,
                    "even_min",  e->section->statsA[ kEvenStatIdx ].min,
                    "even_max",  e->section->statsA[ kEvenStatIdx ].max,
                    "even_mean", e->section->statsA[ kEvenStatIdx ].mean,
                    "even_std",  e->section->statsA[ kEvenStatIdx ].std,
                    "dyn_min",   e->section->statsA[ kDynStatIdx ].min,
                    "dyn_max",   e->section->statsA[ kDynStatIdx ].max,
                    "dyn_mean",  e->section->statsA[ kDynStatIdx ].mean,
                    "dyn_std",   e->section->statsA[ kDynStatIdx ].std,
                    "tempo_min", e->section->statsA[ kTempoStatIdx ].min,
                    "tempo_max", e->section->statsA[ kTempoStatIdx ].max,
                    "tempo_mean",e->section->statsA[ kTempoStatIdx ].mean,
                    "tempo_std", e->section->statsA[ kTempoStatIdx ].std,
                    "cost_min",  e->section->statsA[ kCostStatIdx ].min,
                    "cost_max",  e->section->statsA[ kCostStatIdx ].max,
                    "cost_mean", e->section->statsA[ kCostStatIdx ].mean,
                    "cost_std",  e->section->statsA[ kCostStatIdx ].std )) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV meas. stats field.");
        goto errLabel;        
      }

    errLabel:
      
      return rc;
    }

    
    rc_t _parse_section_row( score_parse_t* p, csv::handle_t csvH, event_t* e  )
    {
      rc_t rc;
      const char* sectionLabel = nullptr;
      
      if((rc = getv(csvH,"section",sectionLabel)) != kOkRC )
      {
        rc = cwLogError(rc,"Section row parse failed.");
        goto errLabel;
      }

      if((e->section = _find_or_create_section(p,sectionLabel)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Section find/create failed for section: '%s'.",cwStringNullGuard(sectionLabel));
        goto errLabel;
      }

      //if((rc = _parse_section_stats(p,csvH,e)) != kOkRC )
      //  goto errLabel;        

      e->section->csvRowNumb = e->csvRowNumb;
      
    errLabel:
      return rc;
    }
    
    rc_t _parse_bpm_row( score_parse_t* p, csv::handle_t csvH, event_t* e  )
    {
      rc_t rc;
      
      if((rc = getv(csvH,
                    "bpm",e->bpm,
                    "rval",e->bpm_rval )) != kOkRC )
      {
        rc = cwLogError(rc,"BPM row parse failed.");
      }
      
      return rc;
    }

    rc_t _parse_advance_to_var_label_section( const char*& text )
    {
      rc_t rc = kOkRC;
      
      // the label must have at least 3 characters to include both the flag character, a space, and a section identifier
      if( textLength(text) < 3 )
      {
        text += textLength(text);
        return rc;
      }
      
      if((text = nextWhiteChar(text)) != nullptr )
        if((text = nextNonWhiteChar(text)) != nullptr)
          if( textLength(text) > 0)
            goto errLabel;
        
      rc = cwLogError(kSyntaxErrorRC,"Parse of var target section failed on '%s'.",cwStringNullGuard(text));

    errLabel:
      return rc;
    }

    rc_t _parse_var_label( score_parse_t* p, const char* text, unsigned varIdx, unsigned varFlag, event_t* e )
    {
      rc_t        rc           = kOkRC;
      section_t*  section      = nullptr;
      
      if( textLength(text) == 0 )
        return rc;
      
      unsigned flags = var_char_to_flags(text);

      e->varA[ varIdx ].flags = flags;

      if( !cwIsFlag(flags,varFlag) )
      {
        rc = cwLogError(kSyntaxErrorRC,"Unknown attribute flag '%s'. Expected: '%s'.",text,var_flags_to_char(varFlag));
        goto errLabel;
      }

      // The last set in a group will have a target section id appended to the character identifier
      if((rc = _parse_advance_to_var_label_section(text)) != kOkRC )
        goto errLabel;

      // if this set has a target section id
      if( textLength(text) > 0 )
      {
        if((section = _find_or_create_section(p, text )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The var target section find/create failed.");
          goto errLabel;
        }

        e->varA[ varIdx ].target_section  = section;
        e->varA[ varIdx ].flags          |= kSetEndVarFl;  // if a target section was included then this is also the 'end' id in the set.
      }
    errLabel:
      return rc;
      
    }
    
    rc_t _parse_note_on_row( score_parse_t* p, csv::handle_t csvH, event_t* e  )
    {
      rc_t rc,rc0=kOkRC,rc1=kOkRC,rc2=kOkRC;
      const char* sciPitch   = nullptr;
      const char* dmark      = nullptr;
      const char* graceLabel = nullptr;
      const char* tieLabel   = nullptr;
      const char* onsetLabel = nullptr;
      const char* dynLabel   = nullptr;
      const char* evenLabel  = nullptr;
      const char* tempoLabel = nullptr;
      const char* oLocId       = nullptr;
      
      if((rc = getv(csvH,
                    "oloc",  oLocId,
                    "rval",  e->rval,
                    "dots",  e->dotCnt,
                    "sci_pitch",sciPitch,
                    "dmark",dmark,
                    "status",e->status,
                    "d0",    e->d0,
                    "d1",    e->d1,
                    "grace", graceLabel,
                    "tie",   tieLabel,
                    "onset", onsetLabel,
                    "dyn",   dynLabel,
                    "even",  evenLabel,
                    "tempo", tempoLabel)) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV note-on row.");
        goto errLabel;
      }

      if( textLength(oLocId) > 0 )
        if((rc = string_to_number(oLocId,e->oLocId)) != kOkRC )
        {
          rc = cwLogError(rc,"Error converting oLocId (%s) to number.",oLocId);
          goto errLabel;
        }
      
      rc0 = _parse_var_label(p, dynLabel,  kDynVarIdx,   kDynVarFl,   e );
      rc1 = _parse_var_label(p, evenLabel, kEvenVarIdx,  kEvenVarFl,  e );
      rc2 = _parse_var_label(p, tempoLabel,kTempoVarIdx, kTempoVarFl, e );

      if( textIsEqual(graceLabel,"g") )
        e->flags |= kGraceFl;

      if( textIsEqual(tieLabel,"t") )
        e->flags |= kTieBegFl;
      
      if( textIsEqual(tieLabel,"_") )
        e->flags |= kTieContinueFl;
      
      if( textIsEqual(tieLabel,"T") )
        e->flags |= kTieEndFl;

      if( textIsEqual(onsetLabel, "o" ))
          e->flags |= kOnsetFl;

      if( sciPitch != nullptr )
        e->sciPitch = mem::duplStr(sciPitch);

      if( e->d1 > 0 )
        if((e->dynLevel = marker_to_level(p->dynRefH,dmark))  == kInvalidIdx )
        {
          rc = cwLogError(kSyntaxErrorRC,"An invalid dynamic mark (%s) was encountered.",cwStringNullGuard(dmark));
          goto errLabel;
        }

      if( cwIsFlag(e->flags,kOnsetFl) )
      {
        if( cwIsFlag(e->flags,kTieContinueFl | kTieEndFl)  )
        {
          rc = cwLogError(kSyntaxErrorRC,"The '%s' event has both an onset flag and tie continue/end flag.",cwStringNullGuard(sciPitch));
          goto errLabel;
        }

      }
      else
      {
        if( e->varA[kDynVarIdx].flags )
        {
          rc = cwLogError(kSyntaxErrorRC,"The '%s' event has no onset flag but is included in a dynamics set.",cwStringNullGuard(sciPitch));
          goto errLabel;
        }
        
      }
      
    errLabel:
      return rcSelect(rc,rc0,rc1,rc2);                    
    }
    
    rc_t _parse_note_off_row( score_parse_t* p, csv::handle_t csvH, event_t* e  )
    {
      rc_t rc;
      
      if((rc = getv(csvH,
                    "status",e->status,
                    "d0",e->d0,
                    "d1",e->d1)) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV note-off row.");
      }
      return rc;      
    }
    
    rc_t _parse_pedal_row( score_parse_t* p, csv::handle_t csvH, event_t* e  )
    {
      rc_t rc;
      
      if((rc = getv(csvH,
                    "status",e->status,
                    "d0",e->d0,
                    "d1",e->d1)) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV pedal row.");
      }
      return rc;
    }
    
    rc_t _parse_ctl_row( score_parse_t* p, csv::handle_t csvH, event_t* e  )
    {
      rc_t rc;
      
      if((rc = getv(csvH,
                    "status",e->status,
                    "d0",e->d0,
                    "d1",e->d1)) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing CSV ctl row.");
      }
      return rc;
    }

    rc_t _set_hash( score_parse_t* p, unsigned opId, unsigned barNumb, uint8_t midiPitch, unsigned barPitchIdx, unsigned& hashRef )
    {
      rc_t rc = kOkRC;

      hashRef = 0;
      
      unsigned hash = form_hash(opId,barNumb,midiPitch,barPitchIdx);
              
      if( _hash_to_event(p,hash) != nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The event hash '%x' is is duplicated.",hash);
        goto errLabel;
      }

      hashRef = hash;
      
    errLabel:
      return rc;
    }
    
    rc_t _parse_events( score_parse_t* p, csv::handle_t csvH )
    {
      rc_t       rc           = kOkRC;
      section_t* cur_section  = nullptr;
      unsigned   cur_bar_numb = 1;
      unsigned   bar_evt_idx  = 0;
      unsigned   bpm          = 0;
      double     bpm_rval     = 0;
      unsigned   barPitchCntV[ midi::kMidiNoteCnt ];
      vop::zero(barPitchCntV,midi::kMidiNoteCnt);

      while((rc = next_line(csvH)) == kOkRC )
      {
        const char* opcodeLabel = nullptr;

        if( p->eventN >= p->eventAllocN )
        {
          rc = cwLogError(kBufTooSmallRC,"Event array full.");
          break;
        }
        
        event_t* e = p->eventA + p->eventN;

        e->oLocId = kInvalidIdx;
        
        if((rc = getv(csvH,
                      "opcode",opcodeLabel,
                      "voice", e->voice,
                      "eloc",  e->eLocId,
                      "tick",  e->tick,
                      "sec",   e->sec )) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing score CSV row." );
          goto errLabel;          
        }

        e->csvRowNumb = cur_line_index(csvH) + 1;
        e->opId       = opcode_label_to_id(opcodeLabel);
        e->index      = p->eventN;
        e->dynLevel   = kInvalidIdx;
        
        switch( e->opId )
        {
          case kBarTId:
            if((rc = _parse_bar_row(p, csvH, e )) == kOkRC )
            {
              cur_bar_numb = e->barNumb;
              bar_evt_idx  = 0;
              vop::zero(barPitchCntV,midi::kMidiNoteCnt);

              if((rc = _set_hash( p, e->opId, cur_bar_numb, 0, 0, e->hash )) != kOkRC )
              {
                rc = cwLogError(rc,"Error setting event hash for bar line:%i",cur_bar_numb);
                goto errLabel;
              }

              //printf("%i : 0x%x\n",cur_bar_numb,e->hash);
              
            }
            break;
            
          case kSectionTId:
            if((rc = _parse_section_row(p, csvH, e )) == kOkRC )
            {
              if( cur_section == nullptr )
                for(event_t* e0 = e-1; e0 >= p->eventA; --e0)
                  e0->section = e->section;
              
              cur_section = e->section;
            }
            break;
            
          case kBpmTId:
            if((rc = _parse_bpm_row(p,csvH, e )) == kOkRC )
            {
              // if the cur BPM has not yet been set then go backward setting
              // all events prior to this to the initial BPM
              if( bpm == 0 && e->bpm != 0 )
                std::for_each(p->eventA,e,[e](auto& x){ x.bpm = e->bpm; x.bpm_rval=e->bpm_rval; });

              // if the parsed BPM is invalid ...
              if( e->bpm == 0 || e->bpm_rval==0 )
              {
                e->bpm      = bpm; // ... then ignore it
                e->bpm_rval = bpm_rval;
              }
              else
              {
                bpm      = e->bpm; // ... otherwise make it the current BPM
                bpm_rval = e->bpm_rval;
              }

              // Be sure that all events on this location have the same BPM
              for(event_t* e0 = e - 1; e0>=p->eventA && e0->eLocId==e->eLocId; --e0)
              {
                e0->bpm      = e->bpm;
                e0->bpm_rval = e->bpm_rval;
              }
            }
            break;
            
          case kNoteOnTId:
            if((rc = _parse_note_on_row(p,csvH,e)) == kOkRC )
            {
              e->barPitchIdx = barPitchCntV[e->d0];

              if((rc = _set_hash( p, e->opId, cur_bar_numb, e->d0, e->barPitchIdx, e->hash )) != kOkRC )
              {
                rc = cwLogError(rc,"Error setting hash for note-on event: bar:%i pitch:%i bpi:%i", cur_bar_numb, e->d0, e->barPitchIdx );
                goto errLabel;
              }
              
              barPitchCntV[e->d0] += 1;
            }
            break;
            
          case kNoteOffTId:
            rc = _parse_note_off_row(p,csvH,e);
            break;
            
          case kPedalTId:
            rc = _parse_ctl_row(p,csvH,e);
            break;
            
          case kRestTId:
            break;
            
          case kCtlTId:
            rc = _parse_ctl_row(p,csvH,e);
            break;
            
          case kInvalidTId:
            rc = cwLogError(kInvalidIdRC,"Invalid opocde '%s'.", cwStringNullGuard(opcodeLabel));
            break;
            
          default:
            rc = cwLogError(kInvalidIdRC,"Unknown opocde '%s'.", cwStringNullGuard(opcodeLabel));
        }

        if( rc != kOkRC )
          break;

        e->section   = cur_section;
        e->barNumb   = cur_bar_numb;
        e->barEvtIdx = bar_evt_idx++;
        e->bpm       = bpm;
        e->bpm_rval  = bpm_rval;
        p->eventN   += 1;
        
      }

    errLabel:
      switch( rc )
      {
        case kOkRC:
          assert(0);
          break;
          
        case kEofRC:
          rc = kOkRC;
          break;
          
        default:
          cwLogError(rc,"CSV parse event failed on row:%i.", cur_line_index(csvH)+1 );
      }
       
      return rc;
    }
    
    rc_t _parse_csv( score_parse_t* p, const char* fname )
    {
        rc_t        rc       = kOkRC;
        const char* titleA[] = { "opcode","meas","index","voice","loc","eloc","oloc","tick","sec",
                                 "dur","rval","dots","sci_pitch","dmark","dlevel","status","d0","d1",
                                 "bar","section","bpm","grace","tie","onset","pedal","dyn","even","tempo" };
        
        unsigned      titleN   = sizeof(titleA)/sizeof(titleA[0]);
        csv::handle_t csvH;

        // open the CSV file and validate the title row
        if((rc = create( csvH, fname, titleA, titleN )) != kOkRC )
        {
          rc = cwLogError(rc,"Score CSV parse failed on '%s'.",fname);
          goto errLabel;
        }

       // get the line count from the CSV file
        if((rc = line_count(csvH,p->eventAllocN)) != kOkRC )
        {
          rc = cwLogError(rc,"Score CSV line count failed.");
          goto errLabel;    
        }

        // allocate the event array
        p->eventA = mem::allocZ<event_t>(p->eventAllocN);
        
        // parse the events
        if((rc = _parse_events(p, csvH )) != kOkRC )
          goto errLabel;

      errLabel:
        if(rc != kOkRC )
          rc = cwLogError(rc,"CSV parse failed on '%s'.",fname);
        destroy(csvH);
        return rc;      
    }

    set_t* _find_set( score_parse_t* p, unsigned setId )
    {
      for(set_t* set=p->begSetL; set!=nullptr; set=set->link)
        if( set->id == setId )
          return set;

      return nullptr;
    }
    
    set_t* _find_or_create_set( score_parse_t* p, unsigned setId, unsigned varTypeId )
    {
      set_t* set;
      if((set = _find_set(p,setId)) == nullptr )
      {
        set = mem::allocZ<set_t>();
        set->id = setId;
        set->varTypeId = varTypeId;
        
        if( p->endSetL == nullptr )
        {
          p->endSetL = set;
          p->begSetL = set;
        }
        else
        {
          p->endSetL->link = set;
          p->endSetL       = set;
        }
        
      }

      return set;
    }

    void _create_sets( score_parse_t* p )
    {
      set_t*   cur_set    = nullptr;
      unsigned setId      = 0;
      unsigned setNoteIdx = 0;
      unsigned endLocId     = kInvalidIdx;
        
      for(unsigned vi=0; vi<kVarCnt; ++vi)
        for(unsigned ei=0; ei<p->eventN; ++ei)
        {
          event_t* e = p->eventA + ei;
          
          // if an end evt has been located for this set
          // and this loc is past the loc of the end event
          // then the set is complete
          // (this handles the case where there are multiple events
          //  on the same end set location)
          if( endLocId != kInvalidIdx && (e->eLocId > endLocId || ei==p->eventN-1) )
          {
            cur_set->eventA  = mem::allocZ<event_t*>(cur_set->eventN);
            setId           += 1;
            setNoteIdx       = 0;
            cur_set          = nullptr;
            endLocId         = kInvalidIdx;
          }

          // if this event 
          if( e->varA[vi].flags != 0 )
          {
            
            if( cur_set == nullptr )
              cur_set = _find_or_create_set(p,setId,vi);
            
            e->varA[vi].set         = cur_set;
            e->varA[vi].setNoteIdx  = setNoteIdx++;
            cur_set->eventN        += 1;
            
            if( cwIsFlag(e->varA[vi].flags,kSetEndVarFl) )
              endLocId = e->eLocId;              
          }
        }
    }

    void _fill_sets( score_parse_t* p )
    {
      for(unsigned ei=0; ei<p->eventN; ++ei)
        for(unsigned vi=0; vi<kVarCnt; ++vi)
          if( p->eventA[ei].varA[vi].set != nullptr )
          {
            event_t* e = p->eventA + ei;
            
            assert( e->varA[vi].setNoteIdx < e->varA[vi].set->eventN );
            
            e->varA[vi].set->eventA[ e->varA[vi].setNoteIdx ] = e;        
          }
      
    }

    unsigned _set_count( score_parse_t* p )
    {
      unsigned n = 0;
      for(set_t* s = p->begSetL; s!=nullptr; s=s->link)
        ++n;
      return n;      
    }
        
    void _order_set_ids_by_time( score_parse_t* p )
    {
      typedef struct set_order_str
      {
        unsigned beg_evt_idx;
        set_t*   set;
      } set_order_t;
      
      unsigned     setAllocN = _set_count(p);
      unsigned     setN      = 0;
      set_order_t* setA      = mem::allocZ<set_order_t>(setAllocN);
      
      for(set_t* s=p->begSetL; s!=nullptr; s=s->link)
      {
        if( s->eventN > 0 )
        {
          setA[setN].beg_evt_idx = s->eventA[0]->index;
          setA[setN].set         = s;
          setN += 1;
        }
      }

      std::sort( setA, setA+setN, [](auto a, auto b){return a.beg_evt_idx<b.beg_evt_idx;});

      unsigned set_id = 0;
      std::for_each( setA, setA+setN, [&](auto a){ a.set->id = set_id++;  });

      mem::release(setA);
    }
    
    rc_t _validate_sets( score_parse_t* p )
    {
      rc_t rc = kOkRC;
      for(set_t* set = p->begSetL; set!=nullptr; set=set->link)
      {
        unsigned loc[]      = { kInvalidIdx, kInvalidIdx, kInvalidIdx };
        unsigned locN       = sizeof(loc)/sizeof(loc[0]);
        unsigned loc_i      = 0;
        unsigned csvRowNumb = -1;
        unsigned barNumb    = -1;
        
        // for each event in this set.
        for(unsigned i=0; i<set->eventN; ++i)
        {
          if( set->eventA[i] == nullptr )
          {
            rc = cwLogError(kInvalidStateRC,"The set %i of type %i section:%s.\n", set->id, set->varTypeId, set->targetSection->label );
            continue;
          }

          // track the last valid csv row numb
          csvRowNumb = set->eventA[i]->csvRowNumb;
          barNumb    = set->eventA[i]->barNumb;
          
          // track the number of locations the events in this set occur at
          if( loc_i < locN )
          {
            unsigned j;
            for(j=0; j<loc_i; ++j)
              if( loc[j] == set->eventA[i]->eLocId )
                break;

            if( j == loc_i )
              loc[ loc_i++ ] = set->eventA[i]->eLocId;
          }
        }

        // 'even' and 'tempo' sets must have at least three events
        if( set->varTypeId == kEvenVarIdx  && loc_i < locN )
        {
          rc = cwLogError(kSyntaxErrorRC,"The 'even' set %i (CSV row:%i bar:%i) of type %i does must have at least 3 events at different locations.",set->id,csvRowNumb,barNumb,set->varTypeId);
        }

        if(  set->varTypeId == kTempoVarIdx && loc_i < 2 )
        {
          rc = cwLogError(kSyntaxErrorRC,"The 'tempo' set %i (CSV row:%i bar:%i) of type %i does must have at least 2 events at different locations.",set->id,csvRowNumb,barNumb,set->varTypeId);
        }
      }

      if( rc != kOkRC )
        rc = cwLogError(rc,"Var set validation failed.");
      
      return rc;
    }

    void _fill_target_sections( score_parse_t* p )
    {
      
      for(unsigned vi=0; vi<kVarCnt; ++vi)
      {
        section_t* cur_sec = nullptr;
        
        for(int ei=(int)p->eventN; ei>=0; --ei)
          if( p->eventA[ei].varA[vi].set != nullptr )
            if( cwIsFlag(p->eventA[ei].varA[vi].flags,kSetEndVarFl) )
            {
              if( p->eventA[ei].varA[vi].target_section == nullptr )
                p->eventA[ei].varA[vi].target_section = cur_sec;
              else
                cur_sec = p->eventA[ei].varA[vi].target_section;

              p->eventA[ei].varA[vi].set->sectionSetIdx      = cur_sec->setN;
              p->eventA[ei].varA[vi].set->targetSection = cur_sec;
              
              cur_sec->setN += 1;

            }
      }
    }

    void _fill_section_sets( score_parse_t* p )
    {
      // allocate memory to hold the set ptr arrays in each section
      for(section_t* s = p->sectionL; s!=nullptr; s=s->link)
        s->setA = mem::allocZ<set_t*>(s->setN);
      
      //  fill the section->setA[] ptrs
      for(set_t* set = p->begSetL; set!=nullptr; set=set->link)
      {
        assert( set->sectionSetIdx < set->targetSection->setN );
        set->targetSection->setA[ set->sectionSetIdx ] = set;

        // set the section beg/end events
        if( set->targetSection->begSetEvent == nullptr )
        {
          set->targetSection->begSetEvent = set->eventA[0];
          set->targetSection->endSetEvent = set->eventA[ set->eventN-1 ];
        }
        else
        {
          if( set->eventA[0]->sec < set->targetSection->begSetEvent->sec )
            set->targetSection->begSetEvent = set->eventA[0];
          
          if( set->eventA[set->eventN-1]->sec > set->targetSection->endSetEvent->sec )
            set->targetSection->endSetEvent = set->eventA[ set->eventN-1 ];
        }
      }      
    }

    rc_t _fill_section_beg_end_evt( score_parse_t* p )
    {
      rc_t rc = kOkRC;
      for(unsigned i=0; i<p->eventN; ++i)
      {
        event_t* e = p->eventA + i;
        assert( e->section != nullptr );
        if( e->section->begEvent == nullptr || e->index < e->section->begEvent->index )
          e->section->begEvent = e;
        
        if( e->section->endEvent == nullptr || e->index > e->section->endEvent->index )
          e->section->endEvent = e;
      }

      return rc;
    }

    bool _compare_sections(const section_t*  sec0, const section_t* sec1)
    {
      return sec0->csvRowNumb < sec1->csvRowNumb;
    }
    
    void _sort_sections( score_parse_t* p )
    {
      // get count of sections
      unsigned secN = 0;
      for(section_t* s=p->sectionL; s!=nullptr; s=s->link)
        ++secN;

      // load secA[] with sections
      section_t** secA = mem::allocZ<section_t*>(secN);
      unsigned    i    = 0;
      for(section_t* s=p->sectionL; s!=nullptr; s=s->link)
        secA[i++]      = s;

      // sort the sections
      std::sort( secA, secA+secN, _compare_sections);

      // rebuild the section list in order
      section_t* begSec = nullptr;
      section_t* endSec = nullptr;
      
      for(i=0; i<secN; ++i)
      {
        secA[i]->link = nullptr;
        
        if( begSec == nullptr )
        {
          begSec = secA[i];
          endSec = secA[i];
        }
        else
        {
          endSec->link = secA[i];
          endSec       = secA[i];          
        }
      }

      p->sectionL = begSec;
      
      mem::release(secA);
    }   

    rc_t _validate_sections( score_parse_t* p, bool show_warnings_fl )
    {
      rc_t rc = kOkRC;

      section_t* s0 = nullptr;
      
      for(section_t* s=p->sectionL; s!=nullptr; s = s->link)
      {
        if( s->setN == 0 )
        {
          if( show_warnings_fl )
            cwLogWarning("The section '%s' does not have any sets assigned to it.",cwStringNullGuard(s->label));
        }
        else
          if( s->begSetEvent == nullptr || s->endSetEvent == nullptr )
          {
            rc = cwLogError(kInvalidStateRC,"The section '%s' does not beg/end events.",cwStringNullGuard(s->label));
            continue;
          }
        
        if( s0 != nullptr && (textCompare( s0->label, s->label ) >= 0 || s0->csvRowNumb > s->csvRowNumb ))
        {
          rc = cwLogError(kInvalidStateRC,"The section label '%s' is out of order with '%s'.",cwStringNullGuard(s->label),cwStringNullGuard(s0->label));
          continue;
        }

        s0 = s;
      }


      // verify that there are no event gaps between the sections
      if( p->sectionL != nullptr and p->sectionL->link != nullptr )
      {
        s0 = p->sectionL;
        for(section_t* s=s0->link; s!=nullptr; s=s->link)
        {
          if( s0->endEvent == nullptr )
            rc = cwLogError(kInvalidStateRC,"The section '%s' does not have an end event.",cwStringNullGuard(s0->label));
          else
          {
            if( s->begEvent == nullptr )
              rc = cwLogError(kInvalidStateRC,"The section '%s' does not have a begin event.",cwStringNullGuard(s->label));
            else
            {
              if( s0->endEvent->index+1 != s->begEvent->index )
                rc = cwLogError(kInvalidStateRC,"The sections '%s' and '%s' do not begin/end on consecutive events.",cwStringNullGuard(s0->label),cwStringNullGuard(s->label));
            }
          }
        
          s0 = s;
        }
      }

      
      if( rc != kOkRC )
        rc = cwLogError(rc,"Section validation failed.");
      return rc;
    }
    
    void _var_print( const event_t* e, unsigned varId, char* text, unsigned textCharN )
    {
      if( e->varA[varId].set == nullptr )
        snprintf(text,textCharN,"%s","               ");
      else
      {
        snprintf(text,textCharN,"%3s-%03i-%02i %4s",var_flags_to_char(e->varA[varId].flags),
                 e->varA[varId].set->id,
                 e->varA[varId].setNoteIdx,
                 e->varA[varId].flags & kSetEndVarFl ? e->varA[varId].set->targetSection->label : " ");
      }
    }
  }
}

const char* cw::score_parse::opcode_id_to_label( unsigned opId )
{
  for(unsigned i=0; _opcode_ref[i].id != kInvalidTId; ++i)
    if( _opcode_ref[i].id == opId )
      return _opcode_ref[i].label;
  return nullptr;
}

unsigned    cw::score_parse::opcode_label_to_id( const char* label )
{
  for(unsigned i=0; _opcode_ref[i].id != kInvalidTId; ++i)
    if( textIsEqual(_opcode_ref[i].label,label) )
      return _opcode_ref[i].id;
  return kInvalidTId;
}

unsigned    cw::score_parse::var_char_to_flags( const char* label )
{
  for(unsigned i=0; _var_ref[i].flags != 0; ++i)
    if( textIsEqual(_var_ref[i].label,label,1) )
      return _var_ref[i].flags;
  return 0; 
}

const char* cw::score_parse::var_flags_to_char( unsigned flags )
{
  for(unsigned i=0; _var_ref[i].flags != 0; ++i)
    if( _var_ref[i].flags == flags )
      return _var_ref[i].label;
  return nullptr;  
}

const char* cw::score_parse::var_index_to_char( unsigned var_idx )
{
  for(unsigned i=0; _var_ref[i].flags != 0; ++i)
    if( _var_ref[i].id == var_idx )
      return _var_ref[i].label;
  return nullptr;  
}

const char* cw::score_parse::dyn_ref_level_to_label( handle_t h, unsigned level )
{
  score_parse_t* p = _handleToPtr(h);
  return level_to_marker( p->dynRefH, level );
}

unsigned cw::score_parse::dyn_ref_label_to_level( handle_t h, const char* label )
{
  score_parse_t* p = _handleToPtr(h);
  return marker_to_level( p->dynRefH, label );
}

unsigned cw::score_parse::dyn_ref_vel_to_level( handle_t h, uint8_t vel )
{
  score_parse_t* p = _handleToPtr(h);
  return velocity_to_level(p->dynRefH,vel);
}

unsigned    cw::score_parse::form_hash( unsigned op_id, unsigned bar, uint8_t midi_pitch, unsigned barPitchIdx )
{
  unsigned hash = 0;

  assert(barPitchIdx < 256 && bar < 0x7ff && op_id < 0xf );
  
  hash += (barPitchIdx & 0x000000ff);
  hash += (midi_pitch  & 0x000000ff) << 8;
  hash += (bar         & 0x00000fff) << 16;
  hash += (op_id       & 0x0000000f) << 28;
  
  return hash;
}

void        cw::score_parse::parse_hash( unsigned hash, unsigned& op_idRef, unsigned& barRef, uint8_t& midi_pitchRef, unsigned& barPitchIdxRef )
{
  barPitchIdxRef = hash &  0x000000ff;
  midi_pitchRef  = (hash & 0x0000ff00) >>  8;
  barRef         = (hash & 0x0fff0000) >> 16;
  op_idRef       = (hash & 0xf0000000) >> 28; 
}


cw::rc_t cw::score_parse::create( handle_t& hRef, const char* fname, double srate, dyn_ref_tbl::handle_t dynRefH, bool show_warnings_fl )
{
  rc_t           rc = kOkRC;
  score_parse_t* p  = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  p = mem::allocZ<score_parse_t>();

  p->srate   = srate;
  p->dynRefH = dynRefH;
  
  if((rc = _parse_csv(p,fname)) != kOkRC )
  {
    rc = cwLogError(rc,"CSV parse failed.");
    goto errLabel;
  }

  _create_sets(p);

  _fill_sets(p);

  _order_set_ids_by_time( p );
  
  if((rc = _validate_sets(p)) != kOkRC )
    goto errLabel;
  
  _fill_target_sections(p);

  _fill_section_sets(p);

  _sort_sections(p);

  _fill_section_beg_end_evt(p);
  
  if((rc = _validate_sections(p,show_warnings_fl)) != kOkRC )
    goto errLabel;
  
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
  {
    rc = cwLogError(rc,"Score parse failed on '%s'.",cwStringNullGuard(fname));
    _destroy(p);
  }
  
  return rc;  
}

cw::rc_t cw::score_parse::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  score_parse_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}

double cw::score_parse::sample_rate( handle_t h )
{
  score_parse_t* p = _handleToPtr(h);
  return p->srate;
}

unsigned cw::score_parse::event_count( handle_t h )
{
  score_parse_t* p = _handleToPtr(h);
  return p->eventN;
}

const cw::score_parse::event_t* cw::score_parse::event_array( handle_t h )
{
  score_parse_t* p = _handleToPtr(h);
  return p->eventA;
}

unsigned cw::score_parse::section_count( handle_t h )
{
  score_parse_t* p = _handleToPtr(h);
  unsigned n = 0;
  for(section_t* s = p->sectionL; s!=nullptr; s=s->link)
    ++n;
  return n;
}

const cw::score_parse::section_t* cw::score_parse::section_list( handle_t h )
{
  score_parse_t* p = _handleToPtr(h);
  return p->sectionL;
}

unsigned cw::score_parse::set_count( handle_t h )
{
  score_parse_t* p = _handleToPtr(h);
  return _set_count(p);
}

const cw::score_parse::set_t* cw::score_parse::set_list( handle_t h )
{
  score_parse_t* p = _handleToPtr(h);
  return p->begSetL;
}

void cw::score_parse::report( handle_t h )
{
  score_parse_t* p        = _handleToPtr(h);
  unsigned       textBufN = 255;
  char           textBuf[ textBufN+1 ];
  const char*    S        = "S:";
  const char*    B        = "B:";
  const char*    blank    = " ";
  const unsigned flN      = 6;
  
  printf("row  op  section bpm b_rval bar  bei voc tick  sec     rval   dot eloc   oloc flags bpm stat  d0  d1  spich   hash  \n");
  printf("---- --- ------- --- ------ ----- --- --- ---- ------- ------ --- ----- ----- ----- --- ---- ---- --- ----- --------\n");

  for(unsigned i=0; i<p->eventN; ++i)
  {
    const event_t* e        = p->eventA + i;
    const char*    secLabel = i==0 || !textIsEqual(e->section->label,p->eventA[i-1].section->label) ? S : blank;
    const char*    barLabel = i==0 || e->barNumb != p->eventA[i-1].barNumb ? B : blank;

    unsigned       fli      = 0;
    char           flag_str[ flN ] = {0};
    
    if( cwIsFlag(e->flags,kGraceFl))       { flag_str[fli++] = 'g'; }
    if( cwIsFlag(e->flags,kTieBegFl))      { flag_str[fli++] = 't'; }
    if( cwIsFlag(e->flags,kTieContinueFl)) { flag_str[fli++] = '_'; }
    if( cwIsFlag(e->flags,kTieEndFl))      { flag_str[fli++] = 'T'; }
    if( cwIsFlag(e->flags,kOnsetFl))       { flag_str[fli++] = 'o'; }
    
    printf("%4i %3s  %2s%4s %3i %6.4f %2s%3i %3i %3i %4i %7.3f %6.3f %3i %5i %5i %5s %3i 0x%02x 0x%02x %3i %5s",
           e->csvRowNumb,
           opcode_id_to_label(e->opId),
           secLabel,
           e->section  == nullptr || e->section->label==nullptr ? "" : e->section->label ,
           e->bpm,
           e->bpm_rval,
           barLabel,
           e->barNumb,
           e->barEvtIdx,
           e->voice,
           e->tick,
           e->sec,
           e->rval,
           e->dotCnt,
           e->eLocId,
           e->oLocId,
           flag_str,
           e->bpm,
           e->status,
           e->d0,
           e->d1,
           e->sciPitch == nullptr ? "" : e->sciPitch);

    if( e->hash != 0 )
      printf(" %08x",e->hash);

    for(unsigned vi = 0; vi<kVarCnt; ++vi)
    {
      _var_print(e, vi, textBuf, textBufN );
      printf(" %s",textBuf);
    }
    printf("\n");
  }
  
}

