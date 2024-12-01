//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwVectOps.h"
#include "cwMidi.h"
#include "cwMidiDecls.h"
#include "cwFlowDecl.h"
#include "cwPresetSel.h"
#include "cwFile.h"

#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwPerfMeas.h"

#include "cwPianoScore.h"


namespace cw
{
  namespace preset_sel
  {
    typedef struct preset_label_str
    {
      char* label;
    } preset_label_t;

    typedef struct alt_label_str
    {
      char*    label;
    } alt_label_t;

    
    typedef struct preset_sel_str
    {
      preset_label_t*  presetLabelA;
      unsigned         presetLabelN;
      
      flow::preset_order_t* presetOrderA;  // presetOrderA[ presetLabelN ]
      flow::preset_order_t* multiPresetA; // activePresetA[ presetLabelN ]
      flow::preset_order_t* dryPresetOrder;   // pointer to the dry preset in presetOrderA[]

      alt_label_t*     altLabelA;
      unsigned         altLabelN;
      
      double           defaultGain;
      double           defaultWetDryGain;
      double           defaultFadeOutMs;
      unsigned         defaultPresetIdx;
      
      struct frag_str* fragL;
      unsigned         next_frag_id;
      
      frag_t*          last_ts_frag;

      double           master_wet_in_gain;
      double           master_wet_out_gain;
      double           master_dry_gain;
      double           master_sync_delay_ms;

      unsigned         sel_frag_id; // fragment id assoc'd with last selected frag. ui element

      unsigned         cur_alt_idx;

      
    } preset_sel_t;

    preset_sel_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,preset_sel_t>(h); }

    const char* _preset_label( preset_sel_t* p, unsigned preset_idx )
    {
      return p->presetLabelA[ preset_idx ].label;
    }

    unsigned _preset_label_to_index( preset_sel_t* p, const char* label )
    {
      for(unsigned i=0; i<p->presetLabelN; ++i)
        if( textIsEqual(p->presetLabelA[i].label,label) )
          return i;

      return kInvalidIdx;
    }

    const char* _alt_index_to_label( preset_sel_t* p, unsigned alt_idx )
    {
      if( alt_idx >= p->altLabelN )
        return nullptr;
      
      return p->altLabelA[ alt_idx ].label;
    }

    
    unsigned _alt_char_to_index( preset_sel_t* p, char label )
    {
      // Note that we start at 1 because 0 is the <no select label>
      for(unsigned i=1; i<p->altLabelN; ++i)
        if( p->altLabelA[i].label[0] == std::toupper(label) )
          return i;

      return kInvalidIdx;
    }


    rc_t _delete_fragment( preset_sel_t* p, unsigned fragId )
    {
      rc_t    rc = kOkRC;
      frag_t* f = p->fragL;
  
      for(; f!=nullptr; f=f->link)
      {
        if( f->fragId == fragId )
        {
          // if this is the first frag in the list
          if( f->prev == nullptr )
            p->fragL = f->link;
          else
          {

            // link the prev fragment to the next fragment
            f->prev->link = f->link;

            // dur of prev frag now include the dur of the deleted frag
            f->prev->endLoc = f->endLoc; 
          }

          // link the next fragment back to the previous fragment
          if( f->link != nullptr )
            f->link->prev = f->prev;

          for(unsigned i=0; i<f->presetN; ++i)
            mem::release(f->presetA[i].alt_str);

          // release the fragment
          mem::release(f->note);
          mem::release(f->presetA);
          mem::release(f->altPresetIdxA);
          //mem::release(f->multiPresetA);
          mem::release(f);
          goto errLabel;
        }
      }

      rc = cwLogError(kEleNotFoundRC,"The fragment with id %i was not found.",fragId);

    errLabel:
      return rc;
    }
    
    void _destroy_all_frags( preset_sel_t* p )
    {
      while( p->fragL != nullptr)
        _delete_fragment(p, p->fragL->fragId );
    }
    
    rc_t _destroy( preset_sel_t* p )
    {
      _destroy_all_frags(p);
      
      for(unsigned i=0; i<p->presetLabelN; ++i)
        mem::release( p->presetLabelA[i].label );
      mem::release( p->presetLabelA );
      mem::release( p->presetOrderA );
      mem::release( p->multiPresetA );
      
      for(unsigned i=0; i<p->altLabelN; ++i)
        mem::release( p->altLabelA[i].label );
      mem::release( p->altLabelA );
      
      p->presetLabelN = 0;
      mem::release(p);

      return kOkRC;
    }

    void _print_preset_alts( preset_sel_t* p, const frag_t* f, const char* label )
    {
      printf("%s : ",label);
      for(unsigned i=0; i<p->altLabelN; ++i)
        printf("%i ",f->altPresetIdxA[i]);
      printf("\n");
    }
    
    void _clear_all_preset_alts( preset_sel_t* p, frag_t* f, unsigned preset_idx )
    {
      // skip the 0th alt because it is controlled by the 'select' play flag
      for(unsigned i=1; i<p->altLabelN; ++i)
        if( f->altPresetIdxA[i] == preset_idx )
          f->altPresetIdxA[i] = kInvalidIdx;
    }
    
    // clear preset of all alternative pointers
    void _deselect_preset_as_alt( preset_sel_t* p, frag_t* f, unsigned preset_idx )
    {
      assert( preset_idx < f->presetN);
      
      mem::release(f->presetA[ preset_idx ].alt_str);

      _clear_all_preset_alts(p,f,preset_idx);
      
    }

    void _remove_alt_char( frag_t* f, unsigned preset_idx, char c )
    {
      assert( preset_idx < f->presetN );

      if( f->presetA[preset_idx].alt_str != nullptr )
      {
        char* s = f->presetA[preset_idx].alt_str;
        bool fl = false;
        for(unsigned i=0; s[i]; ++i)
        {
          if( s[i] == c )
            fl = true;
          
          if(fl)
            s[i] = s[i+1];
        }

        if( textLength(f->presetA[preset_idx].alt_str) == 0)
          mem::release(f->presetA[preset_idx].alt_str);

      }
    }

    rc_t _set_alt( preset_sel_t* p, frag_t* f, unsigned preset_idx, char c )
    {
      rc_t rc = kOkRC;
      unsigned alt_idx;
      
      if((alt_idx = _alt_char_to_index(p,c)) == kInvalidIdx )
      {
        if( !std::isspace(c) )
          cwLogWarning("The alternative '%c' is not valid.",c);
        rc = kInvalidArgRC;
      }
      else
      {
        assert( alt_idx <= p->altLabelN );
      
        if( f->altPresetIdxA[ alt_idx ] != kInvalidIdx )
          _remove_alt_char(f,f->altPresetIdxA[ alt_idx ],c);
      
        f->altPresetIdxA[ alt_idx ] = preset_idx;
      }

      return rc;
    }
    
    void _set_alt_str( preset_sel_t* p, frag_t* f, unsigned sel_preset_idx, const char* alt_str )
    {
      if( alt_str == nullptr )
      {
        _deselect_preset_as_alt(p,f,sel_preset_idx);
      }
      else
      {
        unsigned alt_strN           = textLength(alt_str);
        char alt_str_buf[ alt_strN+1  ];
        unsigned asi                = 0;
          
        // clear the alt's pointing to the selected preset - because the 'alt_str' has changed
        // and some previous alt's may have been removed.
        _clear_all_preset_alts( p, f, sel_preset_idx );

        // scan each char in the alt_str[] and update f->altPresetIdxA[]
        for(unsigned i=0; alt_str[i]; ++i)
          if( _set_alt(p, f, sel_preset_idx, alt_str[i] ) == kOkRC )
          {
            // if this was a legal alt label then add it to alt_str_buf[]
            assert( asi < alt_strN );
            alt_str_buf[ asi++ ] = alt_str[i];
          } 

        assert( asi <= alt_strN );
        alt_str_buf[asi] = 0;
        
        // store the preset's new alt str.
        f->presetA[ sel_preset_idx ].alt_str = mem::reallocStr(f->presetA[ sel_preset_idx ].alt_str, alt_str_buf);
      }
    }

    frag_t* _find_frag( preset_sel_t* p, unsigned fragId )
    {
      frag_t* f;
      for(f=p->fragL; f!=nullptr; f=f->link)
        if( f->fragId == fragId )
          return f;
      
      return nullptr;
    }

    rc_t _find_frag( preset_sel_t* p, unsigned fragId, frag_t*& fragPtrRef )
    {
      rc_t rc = kOkRC;
      if((fragPtrRef = _find_frag(p,fragId )) == nullptr )
        rc = cwLogError(kInvalidIdRC,"'%i' is not a valid fragment id.",fragId);
        
      return rc;
    }

    unsigned _generate_unique_frag_id( preset_sel_t* p )
    {
      unsigned fragId = 0;
      frag_t* f;
      for(f=p->fragL; f!=nullptr; f=f->link)
        fragId = std::max(fragId,f->fragId);

      return fragId + 1;
          
    }
    

    frag_t* _index_to_frag( preset_sel_t* p, unsigned frag_idx )
    {
      frag_t*  f;
      unsigned i = 0;
      for(f=p->fragL; f!=nullptr; f=f->link,++i)
        if( i == frag_idx )
          break;

      if( f == nullptr )
        cwLogError(kInvalidArgRC,"'%i' is not a valid fragment index.",frag_idx);
      
      return f;
    }

    frag_t* _loc_to_frag( preset_sel_t* p, unsigned loc )
    {
      frag_t* f;
      for(f=p->fragL; f!=nullptr; f=f->link)
        if( f->endLoc == loc )
          return f;
      
      return nullptr;      
    }

    bool _ts_is_in_frag( const frag_t* f, const time::spec_t& ts )
    {
      // if f is the earliest fragment
      if( f->prev == nullptr )
        return time::isLTE(ts,f->endTimestamp);

      // else  f->prev->end_ts < ts && ts <= f->end_ts
      return time::isLT(f->prev->endTimestamp,ts) && time::isLTE(ts,f->endTimestamp);
    }

    bool _ts_is_before_frag( const frag_t* f, const time::spec_t& ts )
    {
      // if ts is past f
      if( time::isGT(ts,f->endTimestamp ) )
        return false;

      // ts may now only be inside or before f

      // if f is the first frag then ts must be inside it
      if( f->prev == nullptr )
        return false;

      // is ts before f
      return time::isLTE(ts,f->prev->endTimestamp);      
    }

    bool _ts_is_after_frag( const frag_t* f, const time::spec_t& ts )
    {
      return time::isGT(ts,f->endTimestamp);
    }

    // Scan from through the fragment list to find the fragment containing ts.
    frag_t* _timestamp_to_frag( preset_sel_t* p, const time::spec_t& ts, frag_t* init_frag=nullptr )
    {
      frag_t* f = init_frag==nullptr ? p->fragL : init_frag;
      for(; f!=nullptr; f=f->link)
        if( _ts_is_in_frag(f,ts) )
          return f;
      
      return nullptr;
    }

    bool _loc_is_in_frag( const frag_t* f, unsigned loc )
    {
      // if f is the earliest fragment
      if( f->prev == nullptr )
        return loc <= f->endLoc;

      // else  f->prev->end_loc < loc && loc <= f->end_loc
      return f->prev->endLoc < loc && loc <= f->endLoc;
    }

    bool _loc_is_before_frag( const frag_t* f, unsigned loc )
    {
      // if loc is past f
      if( loc > f->endLoc ) 
        return false;

      // loc may now only be inside or before f

      // if f is the first frag then loc must be inside it
      if( f->prev == nullptr )
        return false;

      // is loc before f
      return loc <= f->prev->endLoc;      
    }

    bool _loc_is_after_frag( const frag_t* f, unsigned loc )
    {
      return loc > f->endLoc;
    }

    // Scan from through the fragment list to find the fragment containing loc.
    frag_t* _fast_loc_to_frag( preset_sel_t* p, unsigned loc, frag_t* init_frag=nullptr )
    {
      frag_t* f = init_frag==nullptr ? p->fragL : init_frag;
      for(; f!=nullptr; f=f->link)
        if( _loc_is_in_frag(f,loc) )
          return f;
      
      return nullptr;
    }
    

    rc_t _validate_preset_id( const frag_t* frag, unsigned preset_id )
    {
      rc_t rc = kOkRC;
      
      if(  (preset_id >= frag->presetN) || (frag->presetA[ preset_id ].preset_idx != preset_id)  )
        rc = cwLogError(kInvalidIdRC,"The preset id '%i' is invalid on the fragment at loc:%i.",preset_id,frag->endLoc);
      
      return rc;        
    }

    bool _is_master_var_id( unsigned varId )
    { return varId > kBaseMasterVarId; }

    template< typename T >
    rc_t _set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, const T& value )
    {
      rc_t          rc = kOkRC;
      preset_sel_t* p  = _handleToPtr(h);
      frag_t*       f  = nullptr;

      // if this is not a 'master' variable then locate the requested fragment
      if( !_is_master_var_id(varId) )
      {
        if((rc = _find_frag(p,fragId,f)) != kOkRC )
          goto errLabel;

        p->sel_frag_id = fragId;
      }
      
      switch( varId )
      {
      case kGuiUuIdVarId:
        f->guiUuId = value;
        break;
        
      case kPresetSelectVarId:        
        for(unsigned i=0; i<f->presetN; ++i)
          if((f->presetA[i].playFl = f->presetA[i].preset_idx == presetId ? value : false) == true)
            f->altPresetIdxA[0] = i;
         break;

      case kPresetSeqSelectVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          f->presetA[ presetId ].seqFl = value;
        break;

      case kPresetOrderVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          f->presetA[ presetId ].order = value;
        break;
        
      case kPresetAltVarId:
        assert(0);
        break;
        
      case kInGainVarId:
        f->igain = value;
        break;

      case kOutGainVarId:
        f->ogain = value;
        break;
        
      case kFadeOutMsVarId:
        f->fadeOutMs = value;
        break;
    
      case kWetGainVarId:
        f->wetDryGain = value;
        break;

      case kBegPlayLocVarId:
        f->begPlayLoc = value;
        break;
          
      case kEndPlayLocVarId:
        f->endPlayLoc = value;
        break;

      case kPlayBtnVarId:
        break;

      case kPlaySeqBtnVarId:
        f->seqAllFl = false;
        break;

      case kPlaySeqAllBtnVarId:
        f->seqAllFl = true;
        break;
        
      case kMasterWetInGainVarId:
        p->master_wet_in_gain = value;
        break;

      case kMasterWetOutGainVarId:
        p->master_wet_out_gain = value;
        break;

      case kMasterDryGainVarId:
        p->master_dry_gain = value;
        break;
        
      case kMasterSyncDelayMsVarId:
        p->master_sync_delay_ms = value;
        break;
        
      default:
        rc = cwLogError(kInvalidIdRC,"There is no preset variable with var id:%i.",varId);
        goto errLabel;
      }

    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Variable value assignment failed on fragment '%i' variable:%i preset:%i",fragId,varId,presetId);
  
      return rc;
     
    }

    template< typename T >
    rc_t _get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, T& valueRef )
    {
      rc_t          rc = kOkRC;
      preset_sel_t* p  = _handleToPtr(h);
      frag_t*       f  = nullptr;

      // if this is not a 'master' variable then locate the requested fragment
      if( !_is_master_var_id( varId ) )
        if((rc = _find_frag(p,fragId,f)) != kOkRC )
          goto errLabel;
  
      switch( varId )
      {
      case kGuiUuIdVarId:
        valueRef = f->guiUuId;
        break;

      case kBegLocVarId:
        valueRef = f->prev == nullptr ? 0 : f->prev->endLoc + 1;
        break;
        
      case kEndLocVarId:
        valueRef = f->endLoc;
        break;
        
      case kPresetSelectVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          valueRef = f->presetA[ presetId ].playFl;
         break;
        
      case kPresetSeqSelectVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          valueRef = f->presetA[ presetId ].seqFl;
         break;

      case kPresetOrderVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          valueRef = f->presetA[ presetId ].order;
        break;

      case kPresetAltVarId:
        assert(0);
        break;  
        
      case kInGainVarId:
        valueRef = f->igain;
        break;
        
      case kOutGainVarId:
        valueRef = f->ogain;
        break;
    
      case kFadeOutMsVarId:
        valueRef = f->fadeOutMs;
        break;
    
      case kWetGainVarId:
        valueRef = f->wetDryGain;
        break;

      case kBegPlayLocVarId:
        valueRef = f->begPlayLoc;
        break;

      case kEndPlayLocVarId:
        valueRef = f->endPlayLoc;
        break;
        
      case kPlayBtnVarId:
        break;
        
      case kPlaySeqBtnVarId:
        break;

      case kPlaySeqAllBtnVarId:
        valueRef = f->seqAllFl;
        break;

      case kMasterWetInGainVarId:
        valueRef = p->master_wet_in_gain;
        break;

      case kMasterWetOutGainVarId:
        valueRef = p->master_wet_out_gain;
        break;

      case kMasterDryGainVarId:
        valueRef = p->master_dry_gain;
        break;
        
      case kMasterSyncDelayMsVarId:
        valueRef = p->master_sync_delay_ms;
        break;
        
      default:
        rc = cwLogError(kInvalidIdRC,"There is no preset variable with var id:%i.",varId);
        goto errLabel;
      }

    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Variable value access failed on fragment '%i' variable:%i preset:%i",fragId,varId,presetId);

      return rc;     
    }

    rc_t _report_with_piano_score( preset_sel_t* p, const char* rpt_fname, perf_score::handle_t pianoScoreH )
    {
      rc_t                       rc = kOkRC;  
      const perf_score::event_t* e  = base_event( pianoScoreH );
      file::handle_t fH;

      if((rc = open(fH,rpt_fname,file::kWriteFl)) != kOkRC )
      {
        rc = cwLogError(rc,"Preset sel Piano Score report open failed.");
        goto errLabel;
      }
      

      if( e == nullptr )
      {
        cwLogWarning("The piano score is empty during preset reporting.");
      }
      else
      {
        const frag_t* frag     = p->fragL;
        unsigned      frag_loc = frag == nullptr ? 0 : frag->endLoc;
        unsigned      frag_id  = 0;
        unsigned      colN     = 0;
        
        double   esec  = 0;
        for(; e!=nullptr; e=e->link)
        {
          // if this event is on a new location - then print a newline
          if( esec != e->sec )
          {
            esec = e->sec;
            if( colN )
            {
              printf(fH,"\n");
              colN = 0;
            }
          }

          // if this event is  on the next fragment end loc
          if( e->loc == frag_loc )
          {
            // this event is on the next frag 
            if( colN != 0 )
              printf(fH,"\n");
            
            printf(fH,"%3i %5i %3i ",frag_id, frag_loc, e->meas);
            colN = 1;
            
            if( frag != nullptr )
            {
              frag = frag->link;
              if( frag != nullptr )
              {
                frag_loc = frag->endLoc;
                frag_id += 1;
              }
            }
          }

          // if this event is a note-on then print the pitch
          if( midi::isNoteOn( e->status, e->d1 ))
          {            
            if( colN == 0 )
            {
              printf(fH,"          %3i ",e->meas);
              colN = 1;
            }

            printf(fH,"%s ",e->sci_pitch);
            colN += 1;
          }
        }
      }

    errLabel:
      close(fH);
      
      return rc;
    }

    rc_t _report_with_sfscore(  preset_sel_t* p, const char* rpt_fname, sfscore::handle_t scoreH  )
    {
      rc_t                    rc       = kOkRC;  
      unsigned                eventN   = event_count( scoreH );
      const frag_t*           frag     = p->fragL;
      unsigned                frag_loc = frag == nullptr ? 0 : frag->endLoc;
      unsigned                frag_id  = 0;
      const sfscore::event_t *e        = event( scoreH, 0);
      file::handle_t fH;

      if((rc = open(fH,rpt_fname,file::kWriteFl)) != kOkRC )
      {
        rc = cwLogError(rc,"File open failed on sfscore preset sel report file.");
        goto errLabel;
      }
      
      if( e == nullptr )
      {
        cwLogWarning("The score is empty during preset sel reporting.");
      }
      else
      {
        unsigned eloc = e->oLocId;
        unsigned colN = 0;
        for(unsigned i=0; i<eventN; ++i)
        {

          e  = event( scoreH, i);

          // are we on a new location?
          if( eloc != e->oLocId )
          {
            eloc = e->oLocId;
            colN = 0;
            printf(fH,"\n");            
          }

          // is this the location of the next fragment
          if( eloc >= frag_loc )
          {
            if( colN != 0 )
              printf(fH,"\n");
            
            
            printf(fH,"%3i %5i %3i ",frag_id,frag_loc,e->barNumb);
            colN = 1;
            if( frag != nullptr )
            {
              frag = frag->link;
              if( frag != nullptr )
              {
                frag_loc = frag->endLoc;
                frag_id += 1;
              }
            }
          }

          if( colN == 0 )
          {
            printf(fH,"          %3i ",e->barNumb);
            colN = 1;
          }

          //printf(fH,"%s ",e->sciPitch);
          printf(fH,"%s ",midi::midiToSciPitch(e->pitch));
          colN += 1;
      
        }
      }
    errLabel:
      close(fH);
      
      return rc;
    }

    const perf_score::event_t* _loc_to_prev_note_on( perf_score::handle_t pianoScoreH, unsigned loc )
    {
      const perf_score::event_t* e;
      unsigned orig_loc = loc;
      
      for(; loc>0; --loc)
      {
        if((e = loc_to_event(pianoScoreH, loc )) != nullptr )          
          if( orig_loc==1 or midi::isNoteOn(e->status,e->d1) )
            break;
      }

      if( orig_loc!=1 && loc == 0 )
      {
        cwLogError(kInvalidStateRC,"No sounding note found before loc: %i.",orig_loc );
        e = nullptr;
      }
      
      return e;
    }


    const perf_score::event_t* _loc_to_next_note_on( perf_score::handle_t pianoScoreH, unsigned loc )
    {
      const perf_score::event_t* e;
      
      if((e = loc_to_event(pianoScoreH, loc )) == nullptr )
      {
        cwLogError(kInvalidStateRC,"The loc %i for fragment was not found in the score.",loc);
        goto errLabel;        
      }

      for(; e!=nullptr; e=e->link)
        if( midi::isNoteOn(e->status,e->d1) )
          break;

      if( e == nullptr )
        cwLogError(kInvalidStateRC,"No sounding note found after loc: %i.",loc );

    errLabel:
      return e;
    }

    const flow::preset_order_t* _load_active_multi_preset_array( preset_sel_t* p, const frag_t* f, unsigned flags, unsigned& cnt_ref )
    {
      bool has_zero_fl = false;
      cnt_ref = 0;
      
      for(unsigned i=0,j=1; i<f->presetN; ++i)
      {
        if( f->presetA[i].order > 0 || f->presetA[i].playFl )
        {
          unsigned out_idx;
          
          // Exactly one preset can have the 'playFl' set.
          // This is the highest priority preset.
          // It is always placed in the first slot.
          if( !f->presetA[i].playFl )
            out_idx = j++;
          else
          {
            out_idx = 0;
            has_zero_fl = true;
          }

          assert( out_idx < p->presetLabelN );
          
          p->multiPresetA[out_idx].preset_label = _preset_label( p, f->presetA[i].preset_idx );
          p->multiPresetA[out_idx].order        = f->presetA[i].order;
          ++cnt_ref;
        }
      }
            
      // sort the presets on 'order' - being careful to not move the zeroth preset (if it exists)
      if( (has_zero_fl && cnt_ref > 2) || (!has_zero_fl && cnt_ref>1)  )
      {
        std::sort(p->multiPresetA+1,
                  p->multiPresetA+(cnt_ref-1),
                  [](const flow::preset_order_t& a,const flow::preset_order_t& b){ return a.order<b.order; } );
      }

      
      return has_zero_fl ? p->multiPresetA : p->multiPresetA+1;
    }
    
  }
}


cw::rc_t cw::preset_sel::create(  handle_t& hRef, const object_t* cfg  )
{
  rc_t            rc                   = kOkRC;
  preset_sel_t*   p                    = nullptr;
  const object_t* preset_labelL        = nullptr;
  const object_t* alt_labelL           = nullptr;
  const char*     default_preset_label = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  p = mem::allocZ<preset_sel_t>();

  // parse the cfg
  if((rc = cfg->getv( "preset_labelL",                preset_labelL,
                      "alt_labelL",                   alt_labelL,
                      "default_gain",                 p->defaultGain,
                      "default_wet_dry_gain",         p->defaultWetDryGain,
                      "default_fade_ms",              p->defaultFadeOutMs,
                      "default_preset",               default_preset_label,
                      "default_master_wet_in_gain",   p->master_wet_in_gain,
                      "default_master_wet_out_gain",  p->master_wet_out_gain,
                      "default_master_dry_gain",      p->master_dry_gain,
                      "default_master_sync_delay_ms", p->master_sync_delay_ms)) != kOkRC )
  {
    rc = cwLogError(rc,"The preset configuration parse failed.");
    goto errLabel;
  }

  // allocate the label array
  p->presetLabelN = preset_labelL->child_count();
  p->presetLabelA = mem::allocZ<preset_label_t>(p->presetLabelN);
  p->presetOrderA = mem::allocZ<flow::preset_order_t>(p->presetLabelN);
  p->multiPresetA = mem::allocZ<flow::preset_order_t>(p->presetLabelN);
  
  // get the preset labels
  for(unsigned i=0; i<p->presetLabelN; ++i)
  {
    const char*     label     = nullptr;
    const object_t* labelNode = preset_labelL->child_ele(i);

    if( labelNode!=nullptr )
      rc = labelNode->value(label);
    
    if( rc != kOkRC || label == nullptr || textLength(label) == 0 )
    {
      rc = cwLogError(kInvalidStateRC,"A empty preset label was encountered while reading the preset label list.");
      goto errLabel;
    }
    
    p->presetLabelA[i].label = mem::duplStr(label);
    p->presetOrderA[i].preset_label = p->presetLabelA[i].label;
    p->presetOrderA[i].order = 1;

    if( textIsEqual(p->presetOrderA[i].preset_label,"dry") )
      p->dryPresetOrder = p->presetOrderA + i;
  }


  // allocate the alt label array
  p->altLabelN = alt_labelL->child_count() + 1;
  p->altLabelA = mem::allocZ<alt_label_t>(p->altLabelN);

  p->altLabelA[0].label = mem::duplStr("*");

  // get the alt labels
  for(unsigned i=1,j=0; i<p->altLabelN; ++i,++j)
  {
    const char*     label     = nullptr;
    const object_t* labelNode = alt_labelL->child_ele(j);

    if( labelNode!=nullptr )
      rc = labelNode->value(label);
    
    if( rc != kOkRC || label == nullptr || textLength(label) == 0 )
    {
      rc = cwLogError(kInvalidStateRC,"A empty alt label was encountered while reading the alt label list.");
      goto errLabel;
    }
    
    p->altLabelA[i].label = mem::duplStr(label);
  }
  

  p->defaultPresetIdx = kInvalidIdx;
  if( default_preset_label != nullptr )
    if((p->defaultPresetIdx = _preset_label_to_index(p,default_preset_label)) ==kInvalidIdx )
      cwLogError(kInvalidIdRC,"The default preset label '%s' could not be found.",cwStringNullGuard(default_preset_label));
    
  if( p->defaultPresetIdx == kInvalidIdx )
    cwLogError(kInvalidStateRC,"No default preset was set.");

  if( p->dryPresetOrder == nullptr )
    rc = cwLogError(kInvalidStateRC,"The 'dry' preset was not found.");
  
  
  hRef.set(p);
  
 errLabel:
  return rc;
}

cw::rc_t cw::preset_sel::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  preset_sel_t* p = nullptr;
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;  
}

unsigned    cw::preset_sel::preset_count( handle_t h )
{
  preset_sel_t* p = _handleToPtr(h);
  return p->presetLabelN;
}

const char* cw::preset_sel::preset_label( handle_t h, unsigned preset_idx )
{
  preset_sel_t* p = _handleToPtr(h);
  return _preset_label(p,preset_idx);
}

const cw::flow::preset_order_t* cw::preset_sel::preset_order_array( handle_t h )
{
  preset_sel_t* p = _handleToPtr(h);
  return p->presetOrderA;
}

unsigned    cw::preset_sel::alt_count( handle_t h )
{
  preset_sel_t* p = _handleToPtr(h);
  return p->altLabelN;
}

const char* cw::preset_sel::alt_label( handle_t h, unsigned alt_idx )
{
  preset_sel_t* p = _handleToPtr(h);
  return _alt_index_to_label(p,alt_idx);
}

void cw::preset_sel::get_loc_range( handle_t h, unsigned& minLocRef, unsigned& maxLocRef )
{
  preset_sel_t* p = _handleToPtr(h);

  if( p->fragL == nullptr )
  {
    minLocRef = score_parse::kInvalidLocId;
    maxLocRef = score_parse::kInvalidLocId;
  }
  else
  {
    minLocRef = 1;

    for(const frag_t* f = p->fragL; f!=nullptr; f=f->link)
      maxLocRef = f->endLoc;
  }
}


unsigned cw::preset_sel::fragment_count( handle_t h )
{
  preset_sel_t* p = _handleToPtr(h);
  unsigned      n = 0;
  
  for(const frag_t* f=p->fragL; f!=nullptr; f=f->link)
    ++n;
  
  return n;
}

const cw::preset_sel::frag_t* cw::preset_sel::get_fragment_base( handle_t h )
{
  preset_sel_t* p = _handleToPtr(h);
  return p->fragL;
}

const cw::preset_sel::frag_t* cw::preset_sel::get_fragment( handle_t h, unsigned fragId )
{
  preset_sel_t* p = _handleToPtr(h);
  return _find_frag(p,fragId);
}

const cw::preset_sel::frag_t* cw::preset_sel::gui_id_to_fragment(handle_t h, unsigned guiUuId )
{
  frag_t* f;
  preset_sel_t* p = _handleToPtr(h);
  for(f=p->fragL; f!=nullptr; f=f->link)
    if( f->guiUuId == guiUuId )
      return f;

  cwLogError(kInvalidIdRC,"The fragment associated with GUI UU id %i could not be found.",guiUuId);
  
  return nullptr;
}

unsigned cw::preset_sel::frag_to_gui_id( handle_t h, unsigned fragId, bool showErrorFl )
{
  preset_sel_t* p  = _handleToPtr(h);
  
  const frag_t* f;
  if((f = _find_frag(p,fragId)) != nullptr )
    return f->guiUuId;

  if( showErrorFl )
    cwLogError(kInvalidIdRC,"The GUI uuid associated with the fragment id '%i' could not be found.",fragId);
  return kInvalidId;
}

unsigned cw::preset_sel::gui_to_frag_id( handle_t h, unsigned guiUuId, bool showErrorFl )
{
  const frag_t* f;
  if((f = gui_id_to_fragment(h,guiUuId)) != nullptr )
    return f->fragId;

  if( showErrorFl )
    cwLogError(kInvalidIdRC,"The fragment id associated with the GUI uuid '%i' could not be found.",guiUuId);
  return kInvalidId;
}

unsigned cw::preset_sel::loc_to_gui_id(  handle_t h, unsigned loc )
{
  preset_sel_t* p  = _handleToPtr(h);
  frag_t* f;

  if((f = _fast_loc_to_frag( p, loc)) == nullptr )
    return kInvalidId;

  return f->guiUuId;

}


    
cw::rc_t cw::preset_sel::create_fragment( handle_t h, unsigned end_loc, time::spec_t end_timestamp, unsigned& fragIdRef )
{
  
  preset_sel_t* p  = _handleToPtr(h);
  frag_t*       f0 = nullptr;
  frag_t*       f1 = nullptr;
  frag_t*       f  = mem::allocZ<frag_t>();
  f->endLoc        = end_loc;
  f->endTimestamp  = end_timestamp;
  f->igain         = p->defaultGain;
  f->ogain         = p->defaultGain;
  f->wetDryGain    = p->defaultWetDryGain;
  f->fadeOutMs     = p->defaultFadeOutMs;
  f->presetA       = mem::allocZ<preset_t>(p->presetLabelN);
  f->presetN       = p->presetLabelN;
  f->altPresetIdxA = mem::allocZ<unsigned>(p->altLabelN);
  f->fragId        = _generate_unique_frag_id(p);
  f->begPlayLoc    = 0;
  f->endPlayLoc    = end_loc;
  f->note          = mem::duplStr("");

  // set all but the first 
  vop::fill(f->altPresetIdxA+1,p->altLabelN-1,kInvalidIdx);
  
  // set the return value
  fragIdRef        = f->fragId;

  // intiialize the preset array elements
  for(unsigned i=0; i<p->presetLabelN; ++i)
  {
    f->presetA[i].preset_idx = i;

    if( i == p->defaultPresetIdx )
      f->presetA[i].playFl = true;
  }

  // if the list is empty
  if( p->fragL == nullptr )
  {
    p->fragL = f;
    goto doneLabel;
  }

  // search forward to the point where this fragment should be
  // inserted to keep this fragment list in time order
  for(f0 = p->fragL; f0!=nullptr; f0 = f0->link)
  {
    if( end_loc < f0->endLoc )
      break;
    f1 = f0;
  }
  
  // if f is after the last fragment ...
  if( f0 == nullptr )
  {
    assert( f1 != nullptr );
    
    // ... insert f at the end of the list
    f1->link = f;
    f->prev  = f1;
  }
  else
  {
    // Insert f before f0

    f->link = f0;
    f->prev = f0->prev;

    // if f0 was first on the list
    if( f0->prev == nullptr )
    {
      assert( p->fragL == f0 );
      p->fragL = f;
    }
    else
    {
      f0->prev->link = f;
    }
  
    f0->prev = f;
  }

  if( f->prev != nullptr )
    f->begPlayLoc = f->prev->endLoc + 1;
  
 doneLabel:
  return kOkRC;
}
  
cw::rc_t cw::preset_sel::delete_fragment( handle_t h, unsigned fragId )
{
  preset_sel_t* p  = _handleToPtr(h);
  return _delete_fragment(p,fragId);
}

bool cw::preset_sel::is_fragment_end_loc( handle_t h, unsigned loc )
{
  preset_sel_t* p  = _handleToPtr(h);
  return _loc_to_frag(p,loc) != nullptr;
}

cw::rc_t cw::preset_sel::set_alternative( handle_t h, unsigned alt_idx )
{
  rc_t          rc = kOkRC;
  preset_sel_t* p  = _handleToPtr(h);
  
  if( alt_idx >= p->altLabelN )
  {
    rc = cwLogError(kInvalidArgRC,"The alternative index %i is invalid.",alt_idx);
    goto errLabel;
  }

  p->cur_alt_idx = alt_idx;
 errLabel:
  return rc;
}

unsigned cw::preset_sel::ui_select_fragment_id( handle_t h )
{
  preset_sel_t* p  = _handleToPtr(h);
  for(frag_t* f = p->fragL; f!= nullptr; f=f->link)
    if( f->uiSelectFl )
      return f->fragId;
  
  return kInvalidId;  
}

void cw::preset_sel::ui_select_fragment( handle_t h, unsigned fragId, bool selectFl )
{
  preset_sel_t* p  = _handleToPtr(h);
  frag_t* f = p->fragL;

  for(; f!= nullptr; f=f->link)
    f->uiSelectFl = f->fragId == fragId ? selectFl : false;
}


cw::rc_t cw::preset_sel::set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool value )
{ return _set_value(h,fragId,varId,presetId,value); }

cw::rc_t cw::preset_sel::set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned value )
{ return _set_value(h,fragId,varId,presetId,value); }

cw::rc_t cw::preset_sel::set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double value )
{ return _set_value(h,fragId,varId,presetId,value); }

cw::rc_t cw::preset_sel::set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, const char* value )
{
  rc_t          rc = kOkRC;
  preset_sel_t* p  = _handleToPtr(h);
  frag_t*       f  = nullptr;
  
  // locate the requested fragment
  if((rc = _find_frag(p,fragId,f)) != kOkRC )
    goto errLabel;
  
  switch( varId )
  {
  case kNoteVarId:
    mem::release(f->note);
    if( value != nullptr )
      f->note = mem::duplStr(value);
    break;

  case kPresetAltVarId:
    if((rc = _validate_preset_id(f, presetId )) == kOkRC )
    {
      _set_alt_str( p, f, presetId, value );
      
      cwLogInfo("Set Preset Alt : %s",value);
    }   
    break;
    
  default:
    rc = cwLogError(kInvalidIdRC,"There is no preset variable of type 'string' with var id:%i.",varId);
  }
  
 errLabel:
  return rc;
}

cw::rc_t cw::preset_sel::get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool&     valueRef )
{ return _get_value(h,fragId,varId,presetId,valueRef); }

cw::rc_t cw::preset_sel::get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned& valueRef )
{ return _get_value(h,fragId,varId,presetId,valueRef); }

cw::rc_t cw::preset_sel::get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double&   valueRef )
{ return _get_value(h,fragId,varId,presetId,valueRef); }

cw::rc_t cw::preset_sel::get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, const char*&   valueRef )
{
  rc_t          rc = kOkRC;
  preset_sel_t* p  = _handleToPtr(h);
  frag_t*       f  = nullptr;
  
  // locate the requested fragment
  if((rc = _find_frag(p,fragId,f)) != kOkRC )
    goto errLabel;
  
  switch( varId )
  {
  case kNoteVarId:
    valueRef = f->note;
    break;

  case kPresetAltVarId:
    if((rc = _validate_preset_id(f, presetId )) == kOkRC )
    {

      if( f->presetA[ presetId].alt_str == nullptr )
        f->presetA[ presetId].alt_str = mem::duplStr("");
      
      valueRef = f->presetA[ presetId].alt_str;

      //cwLogInfo("Get Preset Alt Flags: 0x%x : %s",f->presetA[ presetId].altFlags,valueRef);

    }
    break;
        
  default:
    rc = cwLogError(kInvalidIdRC,"There is no preset variable of type 'string' with var id:%i.",varId);
  }
  
 errLabel:
  return rc;
}

void cw::preset_sel::track_timestamp_reset( handle_t h )
{
  preset_sel_t* p = _handleToPtr(h);
  p->last_ts_frag = nullptr;
}

bool cw::preset_sel::track_timestamp( handle_t h, const time::spec_t& ts, const cw::preset_sel::frag_t*& frag_Ref )
{
  preset_sel_t* p               = _handleToPtr(h);
  frag_t*       f               = nullptr;
  bool          frag_changed_fl = false;

  time::spec_t t0;
  time::setZero(t0);
  //unsigned elapsedMs = time::elapsedMs(t0,ts);
  //double mins = elapsedMs / 60000.0;

  
  // if this is the first call to 'track_timestamp()'.
  if( p->last_ts_frag == nullptr )
    f = _timestamp_to_frag(p,ts);
  else
    // if the 'ts' is in the same frag as previous call.
    if( _ts_is_in_frag(p->last_ts_frag,ts) )
      f = p->last_ts_frag;
    else
      // if 'ts' is in a later frag
      if( _ts_is_after_frag(p->last_ts_frag,ts) )
        f = _timestamp_to_frag(p,ts,p->last_ts_frag);  
      else // ts is prior to 'last_ts_frag'
        f = _timestamp_to_frag(p,ts); 
  
  // 'f' will be null at this point if 'ts' is past the last preset.
  // In this case we should leave 'last_ts_frag' unchanged.

  // if 'f' is valid but different from 'last_ts_frag'
  if( f != nullptr && f != p->last_ts_frag )
  {
    p->last_ts_frag = f;
    frag_changed_fl = true;
  }

  frag_Ref = p->last_ts_frag;
  
  return frag_changed_fl;
}


void cw::preset_sel::track_loc_reset( handle_t h )
{
  preset_sel_t* p = _handleToPtr(h);
  p->last_ts_frag = nullptr;
}

bool cw::preset_sel::track_loc( handle_t h, unsigned loc, const cw::preset_sel::frag_t*& frag_Ref )
{
  preset_sel_t* p               = _handleToPtr(h);
  frag_t*       f               = nullptr;
  bool          frag_changed_fl = false;

  
  // if this is the first call to 'track_timestamp()'.
  if( p->last_ts_frag == nullptr )
    f = _fast_loc_to_frag(p,loc);
  else
    // if the 'ts' is in the same frag as previous call.
    if( _loc_is_in_frag(p->last_ts_frag,loc) )
      f = p->last_ts_frag;
    else
      // if 'ts' is in a later frag
      if( _loc_is_after_frag(p->last_ts_frag,loc) )
        f = _fast_loc_to_frag(p,loc,p->last_ts_frag);  
      else // ts is prior to 'last_ts_frag'
        f = _fast_loc_to_frag(p,loc); 
  
  // 'f' will be null at this point if 'ts' is past the last preset.
  // In this case we should leave 'last_ts_frag' unchanged.

  // if 'f' is valid but different from 'last_ts_frag'
  if( f != nullptr && f != p->last_ts_frag )
  {
    // don't allow the selected fragment to go backwards
    if( p->last_ts_frag == nullptr || (p->last_ts_frag != nullptr && p->last_ts_frag->endLoc < f->endLoc) )
    {    
      p->last_ts_frag = f;
      frag_changed_fl = true;
    }
  }

  frag_Ref = p->last_ts_frag;
  
  return frag_changed_fl;
}

unsigned cw::preset_sel::fragment_play_preset_index( handle_t h, const frag_t* frag, unsigned preset_seq_idx )
{
  unsigned      n = 0;
  preset_sel_t* p = _handleToPtr(h);

  //cwLogInfo("preset_seq_idx:%i frag id:%i sel_frag_id:%i cur_alt_idx:%i ",preset_seq_idx,frag->fragId,p->sel_frag_id, p->cur_alt_idx);

  //_print_preset_alts( p, frag, "" );
    
  if( preset_seq_idx==kInvalidIdx || frag->fragId != p->sel_frag_id )
  {
    assert( p->cur_alt_idx < p->altLabelN );

    unsigned preset_idx = frag->altPresetIdxA[ p->cur_alt_idx ];
    if( preset_idx == kInvalidIdx )
      preset_idx = frag->altPresetIdxA[0];
    
    return preset_idx;
  }
  

  
  // for each preset
  for(unsigned i=0; i<frag->presetN; ++i)
  {
    /*
    // if 'preset_seq_idx' is not valid ...
    if( preset_seq_idx==kInvalidIdx || frag->fragId != p->sel_frag_id )
    {
      // ...then select the first preset whose 'playFl' is set.
      if( frag->presetA[i].playFl  )
        return frag->presetA[i].preset_idx;
    }
    else
    {
    */
      // ... otherwise select the 'nth' preset whose 'seqFl' is set      
      if( frag->presetA[i].seqFl || frag->seqAllFl )
      {
        if( n == preset_seq_idx )
          return frag->presetA[i].preset_idx;
        ++n;
      }
      //}
  }
  
  return kInvalidIdx;
}

unsigned cw::preset_sel::fragment_seq_count( handle_t h, unsigned fragId )
{
  rc_t          rc = kOkRC;
  preset_sel_t* p  = _handleToPtr(h);
  frag_t*       f  = nullptr;
  unsigned      n  = 0;
  
  if((rc = _find_frag(p,fragId,f)) != kOkRC )
    return 0;

  if( f->seqAllFl )
    return f->presetN;
  
  for(unsigned i=0; i<f->presetN; ++i)
    if( f->presetA[i].seqFl )
      ++n;

  return n;
}


const cw::flow::preset_order_t*  cw::preset_sel::fragment_active_presets( handle_t h, const frag_t* f, unsigned flags, unsigned& count_ref )
{
  preset_sel_t* p  = _handleToPtr(h);
  const flow::preset_order_t* preset_order;
  
  count_ref = 0;

  // Note that kAllActiveFl,kDryPriorityFl,kDrySelectedFl will only be set
  // when the preset is being selected probabilistically

  // if this fragment is dry-selected or dry-only and the associated flags are set
  // then select then return the 'dry' preset
  if( (cwIsFlag(flags,kDrySelectedFl) && f->drySelectedFl) || (cwIsFlag(flags,kDryPriorityFl) && f->dryOnlyFl) )
  {
    preset_order = p->dryPresetOrder;
    count_ref  = 1;
  }
  else
  {
    // if all active is set then return all presets ...
    if( cwIsFlag(flags,kAllActiveFl) )
    {
      preset_order = p->presetOrderA;
      count_ref    = p->presetLabelN;
    }
    else // ... otherwise return the active presets only      
    {
      preset_order = _load_active_multi_preset_array(p,f,flags,count_ref);
    }
  }

  return preset_order;
}


cw::rc_t cw::preset_sel::write( handle_t h, const char* fn )
{
  rc_t          rc        = kOkRC;
  preset_sel_t* p         = _handleToPtr(h);
  object_t*     root      = newDictObject();
  unsigned      fragN     = 0;
  object_t*     fragL_obj = newListObject(nullptr);

  
  for(frag_t* f=p->fragL; f!=nullptr; f=f->link,++fragN)
  {
    object_t* frag_obj = newDictObject(nullptr);

    fragL_obj->append_child(frag_obj);
    
    newPairObject("fragId",            f->fragId,               frag_obj );
    newPairObject("endLoc",            f->endLoc,               frag_obj );
    newPairObject("endTimestamp_sec",  f->endTimestamp.tv_sec,  frag_obj );
    newPairObject("endTimestamp_nsec", f->endTimestamp.tv_nsec, frag_obj );
    newPairObject("inGain",            f->igain,                frag_obj );
    newPairObject("outGain",           f->ogain,                frag_obj );    
    newPairObject("wetDryGain",        f->wetDryGain,           frag_obj );
    newPairObject("fadeOutMs",         f->fadeOutMs,            frag_obj );
    newPairObject("begPlayLoc",        f->begPlayLoc,           frag_obj );
    newPairObject("endPlayLoc",        f->endPlayLoc,           frag_obj );
    newPairObject("note",              f->note,                 frag_obj );
    newPairObject("presetN",           f->presetN,              frag_obj );

    // note: newPairObject() return a ptr to the pair value node.
    object_t* presetL_obj = newPairObject("presetL", newListObject( nullptr ), frag_obj );
    
    for(unsigned i=0; i<f->presetN; ++i)
    {
      object_t* presetD_obj = newDictObject( nullptr );

      newPairObject("order",                         f->presetA[i].order,        presetD_obj );
      newPairObject("alt_str",                       f->presetA[i].alt_str,      presetD_obj );
      newPairObject("preset_label", _preset_label(p, f->presetA[i].preset_idx ), presetD_obj );
      newPairObject("play_fl",                       f->presetA[i].playFl,       presetD_obj );

      presetL_obj->append_child( presetD_obj );
    }
  }

  newPairObject("fragL",             fragL_obj,              root);
  newPairObject("masterWetInGain",   p->master_wet_in_gain,  root );
  newPairObject("masterWetOutGain",  p->master_wet_out_gain, root );
  newPairObject("masterDryGain",     p->master_dry_gain,     root );
  newPairObject("masterSyncDelayMs", p->master_sync_delay_ms,root );

  unsigned bytes_per_frag = 1024;
  
  do
  {
    unsigned s_byteN      = fragN * bytes_per_frag;    
    char*    s            = mem::allocZ<char>( s_byteN );    
    unsigned actual_byteN = root->to_string(s,s_byteN);
    
    if( actual_byteN < s_byteN )      
      if((rc = file::fnWrite(fn,s,strlen(s))) != kOkRC )        
        rc = cwLogError(rc,"Preset select failed on '%s'.",fn);

    
    mem::release(s);
    s = nullptr;

    if( actual_byteN < s_byteN )
      break;

    bytes_per_frag *= 2;

  }while(1);
  
  
  return rc;
}


cw::rc_t cw::preset_sel::read( handle_t h, const char* fn )
{
  rc_t            rc           = kOkRC;
  preset_sel_t*   p            = _handleToPtr(h);  
  object_t*       root         = nullptr;
  const object_t* fragL_obj    = nullptr;
  unsigned        dryPresetIdx = _preset_label_to_index(p,"dry");

  // parse the preset  file
  if((rc = objectFromFile(fn,root)) != kOkRC )
  {
    rc = cwLogError(rc,"The preset select file parse failed on '%s'.", cwStringNullGuard(fn));
    goto errLabel;
  }

  // remove all existing fragments
  _destroy_all_frags(p);

  // parse the root level
  if((rc = root->getv( "fragL",            fragL_obj,
                       "masterWetInGain",  p->master_wet_in_gain,
                       "masterWetOutGain", p->master_wet_out_gain,
                       "masterDryGain",    p->master_dry_gain,
                       "masterSyncDelayMs",p->master_sync_delay_ms)) != kOkRC )
  {
    rc = cwLogError(rc,"Root preset select parse failed on '%s'.", cwStringNullGuard(fn));
    goto errLabel;
  }


  // for each fragment
  for(unsigned i=0; i<fragL_obj->child_count(); ++i)
  {
    frag_t* f = nullptr;
    const object_t* r = fragL_obj->child_ele(i);

    unsigned        fragId       = kInvalidId;
    unsigned        endLoc       = 0;
    unsigned        presetN      = 0;
    unsigned        activePresetN = 0;
    unsigned        begPlayLoc   = 0;
    unsigned        endPlayLoc   = 0;
    double          igain        = 0;
    double          ogain        = 0;
    double          wetDryGain   = 0;
    double          fadeOutMs    = 0;
    const char*     note         = nullptr;
    const object_t* presetL_obj  = nullptr;
    time::spec_t    end_ts;

    // parse the fragment record
    if((rc = r->getv("fragId",fragId,
                     "endLoc",endLoc,
                     "endTimestamp_sec",end_ts.tv_sec,
                     "endTimestamp_nsec",end_ts.tv_nsec,
                     "inGain",igain,
                     "outGain",ogain,
                     "wetDryGain",wetDryGain,
                     "fadeOutMs",fadeOutMs,
                     "begPlayLoc",begPlayLoc,
                     "endPlayLoc",endPlayLoc,
                     "note",note,
                     "presetN", presetN,
                     "presetL", presetL_obj )) != kOkRC )
    {
      rc = cwLogError(rc,"Fragment restore record parse failed.");
      goto errLabel;
    }


    // create a new fragment
    if((rc = create_fragment( h, endLoc, end_ts, fragId)) != kOkRC )
    {
      rc = cwLogError(rc,"Fragment record create failed.");
      goto errLabel;
    }

    // update the fragment variables
    set_value( h, fragId, kInGainVarId,     kInvalidId, igain );
    set_value( h, fragId, kOutGainVarId,    kInvalidId, ogain );
    set_value( h, fragId, kFadeOutMsVarId,  kInvalidId, fadeOutMs );
    set_value( h, fragId, kWetGainVarId,    kInvalidId, wetDryGain );
    set_value( h, fragId, kBegPlayLocVarId, kInvalidId, begPlayLoc );
    set_value( h, fragId, kEndPlayLocVarId, kInvalidId, endPlayLoc );
    set_value( h, fragId, kNoteVarId,       kInvalidId, note );

    // locate the new fragment record
    if((f = _find_frag(p, fragId )) == nullptr )
    {
      rc = cwLogError(rc,"Fragment record not found.");
      goto errLabel;
    }

    // for each preset
    for(unsigned i=0; i<presetN; ++i)
    {
      const object_t*   r      = presetL_obj->child_ele(i);
      unsigned    order        = 0;
      const char* alt_str      = nullptr;
      const char* preset_label = nullptr;
      unsigned    preset_idx   = kInvalidIdx;
      bool        playFl       = false;
      
      // parse the preset record
      if((rc = r->getv("order",        order,
                       "preset_label", preset_label,
                       "play_fl",    playFl)) != kOkRC )
      {
        rc = cwLogError(rc,"The fragment preset at index '%i' parse failed during restore.",i);
        goto errLabel;
      }

      if((rc = r->getv_opt("alt_str", alt_str )) != kOkRC )
      {
        rc = cwLogError(rc,"The fragment preset at index '%i' optional parse failed during restore.",i);
        goto errLabel;
      }

      // locate the preset index associated with the preset label
      if((preset_idx = _preset_label_to_index(p,preset_label)) == kInvalidIdx )
      {
        rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be restored.",cwStringNullGuard(preset_label));
        goto errLabel;
      }

      if( order > 0 || playFl )
        activePresetN += 1;

      f->presetA[ preset_idx ].order     = order;
      f->presetA[ preset_idx ].alt_str   = mem::duplStr(alt_str);
      f->presetA[ preset_idx ].playFl    = playFl;

      _set_alt_str( p, f, i, alt_str );

      if( playFl )
      {
        f->altPresetIdxA[0] = preset_idx;

        // if the dry preset is selected
        if( preset_idx == dryPresetIdx )
          f->drySelectedFl = true;
      }
      
    }

    // if only one preset is active and the dry preset is active
    f->dryOnlyFl    = activePresetN==1 && (f->presetA[dryPresetIdx].order>0 || f->presetA[dryPresetIdx].playFl);

  }
  

 errLabel:
  if(rc != kOkRC )
    cwLogError(rc,"Preset restore failed.");

  if( root != nullptr )
    root->free();
  
  return rc;
}

cw::rc_t cw::preset_sel::report( handle_t h )
{
  rc_t          rc = kOkRC;
  preset_sel_t* p  = _handleToPtr(h);
  unsigned      i  = 0;
  time::spec_t  t0;
  time::setZero(t0);
 
  for(frag_t* f=p->fragL; f!=nullptr; f=f->link,++i)
  {
    unsigned elapsedMs = time::elapsedMs(t0,f->endTimestamp);
    double mins = elapsedMs / 60000.0;
    
    cwLogInfo("%3i id:%3i end loc:%3i end min:%f dry-only:%i dry-sel:%i",i,f->fragId,f->endLoc, mins, f->dryOnlyFl, f->drySelectedFl);
  }

  return rc;
}

cw::rc_t cw::preset_sel::report_presets( handle_t h )
{
  rc_t          rc = kOkRC;
  preset_sel_t* p  = _handleToPtr(h);
  unsigned beg_loc = 1;
  frag_t* f = p->fragL;
  
  for(; f!=nullptr; f=f->link)
  {
    const char* dry_only_label = f->dryOnlyFl ? "only" : "";
    const char* dry_sel_label  = f->drySelectedFl ? "sel" : "";
    cwLogPrint("%5i %5i dry-(%4s %3s)",beg_loc,f->endLoc,dry_only_label,dry_sel_label);
    for(unsigned i=0; i<f->presetN; ++i)
      if( f->presetA[i].playFl || f->presetA[i].order!=0 )
        cwLogPrint("(%s-%i) ", p->presetLabelA[ f->presetA[i].preset_idx ].label, f->presetA[i].order);
    cwLogPrint("\n");
    beg_loc = f->endLoc+1;
  }
  
  
  return rc;
}

cw::rc_t cw::preset_sel::translate_frags( const object_t* cfg )
{
  return cwLogError(kNotImplementedRC,"translate_frags() is not implemented.");
}


#undef NOT_DEF
#ifdef NOT_DEF
cw::rc_t cw::preset_sel::translate_frags( const object_t* cfg )
{
  rc_t                  rc              = kOkRC;
  const char*           cur_frag_fname  = nullptr;
  const char*           cur_score_fname = nullptr;
  const char*           new_score_fname = nullptr;
  const char*           out_frag_fname  = nullptr;
  const char*           cur_rpt_fname   = nullptr;
  const char*           new_rpt_fname   = nullptr;
  const object_t*       presetsNode     = nullptr;
  const object_t*       dynTblNode      = nullptr;
  double                srate           = 0;
  handle_t              presetH;
  perf_score::handle_t  pianoScoreH;
  dyn_ref_tbl::handle_t dynTblH;
  score_parse::handle_t scoreParseH;
  sfscore::handle_t     scoreH;
  
  enum { kNoteN=3 };

  typedef struct
  {
    unsigned loc;
    unsigned opId; // see cwScoreParse k???TId
    uint8_t  pitch;
    unsigned barNumb;
    unsigned barPitchIdx;
    
    unsigned hash;
    
    uint8_t  preNote[kNoteN];
    uint8_t  postNote[kNoteN];
  } loc_t;
  
  typedef struct
  {
    frag_t* frag;
    loc_t begLoc;
    loc_t endLoc;
  } tfrag_t;

  unsigned      tfragN   = 0;
  tfrag_t*      tfragA   = nullptr;
  
  if((rc = cfg->getv("cur_frag_fname",  cur_frag_fname,
                     "cur_score_fname", cur_score_fname,
                     "new_score_fname", new_score_fname,
                     "out_frag_fname",  out_frag_fname,
                     "cur_score_rpt_fname",cur_rpt_fname,
                     "new_score_rpt_fname",new_rpt_fname,
                     "presets", presetsNode,
                     "srate",srate,
                     "dyn_ref", dynTblNode )) != kOkRC )
  {
    rc = cwLogError(rc,"Arg. parse failed on 'translate_frags'.");
    goto errLabel;
  }

  // Create the preset_sel object.
  if((rc = create(presetH,presetsNode)) != kOkRC )
  {
    rc = cwLogError(rc,"Object preset_sel create failed.");
    goto errLabel;    
  }

  // Read the current fragment file
  if((rc = read(presetH,cur_frag_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Object preset_sel read failed on '%s'.",cwStringNullGuard(cur_frag_fname));
    goto errLabel;        
  }

  // Create the piano score.
  if((rc = perf_score::create(pianoScoreH,cur_score_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Piano score failed on '%s'.",cwStringNullGuard(cur_score_fname));
    goto errLabel;        
  }
  else
  {
    preset_sel_t* p           = _handleToPtr(presetH);
    frag_t*       src_frag    = p->fragL;
    unsigned      src_beg_loc = 1;

    _report_with_piano_score( p, cur_rpt_fname, pianoScoreH );
    
    // Allocate the tranlate fragment array
    tfragN = fragment_count(presetH);
    tfragA = mem::allocZ<tfrag_t>(tfragN);

    // Get the locations of the current fragments
    for(unsigned i=0; i<tfragN; ++i, src_frag=src_frag->link)
    {
      tfragA[i].frag = src_frag;
      
      const perf_score::event_t* e;
      unsigned src_end_loc = src_frag->endLoc;
      
      // Get event for begin location.
      if((e = _loc_to_next_note_on(pianoScoreH, src_beg_loc )) == nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The beg-loc %i for fragment was not found in the score.",src_beg_loc);
        goto errLabel;
      }

      //loc_to_pitch_context(pianoScoreH,tfragA[i].begLoc.preNote,tfragA[i].begLoc.postNote,kNoteN);

      tfragA[i].begLoc.loc         = src_beg_loc;
      tfragA[i].begLoc.opId        = midi::isNoteOn(e->status,e->d1) ? score_parse::kNoteOnTId : score_parse::kBarTId;
      tfragA[i].begLoc.pitch       = e->d0;
      tfragA[i].begLoc.barNumb     = e->meas;
      tfragA[i].begLoc.barPitchIdx = e->barPitchIdx;
      tfragA[i].begLoc.hash        = score_parse::form_hash(tfragA[i].begLoc.opId, e->meas, e->d0, e->barPitchIdx );

      printf("Beg: loc:%i bar:%i st:0x%x bpi:%i p:%i : %s\n",src_beg_loc,e->meas,e->status,e->barPitchIdx,e->d0,midi::midiToSciPitch(e->d0));
      
      // Get event for end location.
      if((e = _loc_to_prev_note_on(pianoScoreH, src_end_loc )) == nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The end-loc %i for fragment was not found in the score.",src_end_loc);
        goto errLabel;
      }

      //loc_to_pitch_context(pianoScoreH,tfragA[i].endLoc.preNote,tfragA[i].endLoc.postNote,kNoteN);

      tfragA[i].endLoc.loc         = src_end_loc;
      tfragA[i].endLoc.opId        = midi::isNoteOn(e->status,e->d1) ? score_parse::kNoteOnTId : score_parse::kBarTId;
      tfragA[i].endLoc.pitch       = e->d0;
      tfragA[i].endLoc.barNumb     = e->meas;
      tfragA[i].endLoc.barPitchIdx = e->barPitchIdx;
      tfragA[i].endLoc.hash        = score_parse::form_hash(tfragA[i].endLoc.opId, e->meas, e->d0, e->barPitchIdx );

      printf("End: loc:%i bar:%i op:%i bpi:%i p:%i : %s\n",src_end_loc,e->meas,tfragA[i].endLoc.opId,e->barPitchIdx,e->d0,midi::midiToSciPitch(e->d0));

      src_beg_loc = src_end_loc + 1;
    }

    // Create dynamic table.
    if((rc = dyn_ref_tbl::create(dynTblH,dynTblNode)) != kOkRC )
    {
      rc = cwLogError(rc,"Dynamic table create failed.");
      goto errLabel;
    }

    // Create score parser.
    if((rc = score_parse::create( scoreParseH, new_score_fname, srate, dynTblH )) != kOkRC )
    {
      rc = cwLogError(rc,"Score parser create failed.");
      goto errLabel;
    }
    
    // Create sfscore.
    if((rc = sfscore::create(scoreH,scoreParseH)) != kOkRC )
    {
      rc = cwLogError(rc,"sfscore create failed.");
      goto errLabel;
    }

    //tfrag_t* t0 = nullptr;
    for(unsigned i=0; i<tfragN; ++i)
    {
      const sfscore::event_t* e = nullptr;
      tfrag_t*                t = tfragA + i;

      // bar's do not exist as events in sfscore so we have to ask for the first note
      // in the bar as a proxy to the bar line.
      if( t->endLoc.opId == score_parse::kBarTId )
      {
        if((e = bar_to_event( scoreH, t->endLoc.barNumb )) == nullptr )
        {
          cwLogError(kOpFailRC,"Beg loc %i bar event (meas:%i) not found.",t->begLoc.loc, t->begLoc.barNumb);
          goto errLabel;
        }
      }
      else
      {
        // Locate this event in sfscore
        if((e = hash_to_event( scoreH, t->endLoc.hash )) == nullptr )
        {        
          cwLogError(kOpFailRC,"Beg loc %i pitch event (meas:%i op:%i pitch:%i bpi:%i : hash:0x%x) not found.",t->begLoc.loc, t->begLoc.opId, t->begLoc.barNumb, t->begLoc.pitch,t->begLoc.barPitchIdx,t->begLoc.hash);
          goto errLabel;
        }

        if(0)
        {
          unsigned op0,bar0,bpi0;
          unsigned op1,bar1,bpi1;
          uint8_t pitch0,pitch1;
          
          score_parse::parse_hash( t->begLoc.hash, op0, bar0, pitch0, bpi0 );
          score_parse::parse_hash( e->hash, op1, bar1, pitch1, bpi1 );
          printf("%3i %2i %3i %3i %3i\n",i,op0,bar0,pitch0,bpi0);
          printf("%3i %2i %3i %3i %3i : %i\n",i,op1,bar1,pitch1,bpi1,e->pitch);
        }
      }

      
      //printf("%i\n",e->oLocId);
      //if( t0 != nullptr )
      //   t0->frag->endLoc = e->oLocId;
      //t0 = t;
      t->frag->endLoc = e->oLocId;
    }

    _report_with_sfscore(  p, new_rpt_fname, scoreH );

    // write the translated file
    if((rc = write(presetH,out_frag_fname)) != kOkRC )
    {
      rc = cwLogError(rc,"Translated preset fragment write failed. on '%s'.", cwStringNullGuard(out_frag_fname));
      goto errLabel;        
    }
    
  }
  
 errLabel:
  destroy(dynTblH);
  destroy(scoreParseH);
  destroy(scoreH);
  destroy(pianoScoreH);
  destroy(presetH);
  
  mem::release(tfragA);
  
  return rc;
}
#endif
