#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwPresetSel.h"
#include "cwFile.h"

namespace cw
{
  namespace preset_sel
  {
    typedef struct preset_label_str
    {
      char* label;
    } preset_label_t;
    
    typedef struct preset_sel_str
    {
      preset_label_t*  presetLabelA;
      unsigned         presetLabelN;
      
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
    
    rc_t _delete_fragment( preset_sel_t*  p, unsigned fragId )
    {
      frag_t*       f0 = nullptr;
      frag_t*       f1 = p->fragL;
  
      for(; f1!=nullptr; f1=f1->link)
      {
        if( f1->fragId == fragId )
        {
          if( f0 == nullptr )
            p->fragL = f1->link;
          else
            f0->link = f1->link;

          // release the fragment
          mem::release(f1->note);
          mem::release(f1->presetA);
          mem::release(f1);
      
          return kOkRC;
        }
    
        f0 = f1;
      }
  
      return kOkRC;
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
      p->presetLabelN = 0;
      mem::release(p);

      return kOkRC;
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
      bool fl =  (preset_id < frag->presetN) && (frag->presetA[ preset_id ].preset_idx == preset_id);

      return fl ? kOkRC : cwLogError(kInvalidIdRC,"The preset id '%i' is invalid on the fragment at loc:%i.",preset_id,frag->endLoc);
        
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
        if((rc = _find_frag(p,fragId,f)) != kOkRC )
          goto errLabel;
  
      switch( varId )
      {
      case kGuiUuIdVarId:
        f->guiUuId = value;
        break;
        
      case kPresetSelectVarId:
        for(unsigned i=0; i<f->presetN; ++i)
          f->presetA[i].playFl = f->presetA[i].preset_idx == presetId ? value : false;
         break;

      case kPresetSeqSelectVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          f->presetA[ presetId ].seqFl = value;
        break;

      case kPresetOrderVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          f->presetA[ presetId ].order = value;
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
    
  }
}


cw::rc_t cw::preset_sel::create(  handle_t& hRef, const object_t* cfg  )
{
  rc_t            rc                   = kOkRC;
  preset_sel_t*   p                    = nullptr;
  const object_t* labelL               = nullptr;
  const char*     default_preset_label = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  p = mem::allocZ<preset_sel_t>();

  // parse the cfg
  if((rc = cfg->getv( "preset_labelL",                labelL,
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
  p->presetLabelN = labelL->child_count();
  p->presetLabelA = mem::allocZ<preset_label_t>(p->presetLabelN);

  // get the preset labels
  for(unsigned i=0; i<p->presetLabelN; ++i)
  {
    const char*     label     = nullptr;
    const object_t* labelNode = labelL->child_ele(i);

    if( labelNode!=nullptr )
      rc = labelNode->value(label);
    
    if( rc != kOkRC || label == nullptr || textLength(label) == 0 )
    {
      rc = cwLogError(kInvalidStateRC,"A empty preset label was encountered while reading the preset label list.");
      goto errLabel;
    }
    
    p->presetLabelA[i].label = mem::duplStr(label);
  }

  p->defaultPresetIdx = kInvalidIdx;
  if( default_preset_label != nullptr )
    if((p->defaultPresetIdx = _preset_label_to_index(p,default_preset_label)) ==kInvalidIdx )
      cwLogError(kInvalidIdRC,"The default preset label '%s' could not be found.",cwStringNullGuard(default_preset_label));
    
  if( p->defaultPresetIdx == kInvalidIdx )
    cwLogError(kInvalidStateRC,"No default preset was set.");

  
  
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
  f->fragId        = _generate_unique_frag_id(p);
  f->begPlayLoc    = 0;
  f->endPlayLoc    = end_loc;
  f->note          = mem::duplStr("");
  
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
  frag_t*       f = p->fragL;
  
  for(; f!=nullptr; f=f->link)
    if( f->fragId == fragId )
    {
      if( f->prev == nullptr )
        p->fragL = f->link;
      else
      {
        // the previous fragment's end-loc become the
        // endloc of the deleted fragment
        f->prev->endLoc = f->endLoc;
        f->prev->link = f->link;
      }

      if( f->link != nullptr )
        f->link->prev = f->prev;

      // release the fragment
      mem::release(f->presetA);
      mem::release(f);
      
      return kOkRC;
    }
  
  return cwLogError(kInvalidArgRC,"The fragment '%i' could not be found to delete.",fragId);
}

bool cw::preset_sel::is_fragment_end_loc( handle_t h, unsigned loc )
{
  preset_sel_t* p  = _handleToPtr(h);
  return _loc_to_frag(p,loc) != nullptr;
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



unsigned cw::preset_sel::fragment_play_preset_index( const frag_t* frag, unsigned preset_seq_idx )
{
  unsigned n = 0;
  // for each preset
  for(unsigned i=0; i<frag->presetN; ++i)
  {
    // if 'preset_seq_idx' is not valid ...
    if( preset_seq_idx==kInvalidIdx )
    {
      // ...then select the first preset whose 'playFl' is set.
      if( frag->presetA[i].playFl  )
        return frag->presetA[i].preset_idx;
    }
    else
    {
      // ... otherwise select the 'nth' preset whose 'seqFl' is set      
      if( frag->presetA[i].seqFl || frag->seqAllFl )
      {
        if( n == preset_seq_idx )
          return frag->presetA[i].preset_idx;
        ++n;
      }
    }
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
      newPairObject("preset_label", _preset_label(p, f->presetA[i].preset_idx ), presetD_obj );
      newPairObject("play_fl",                       f->presetA[i].playFl,       presetD_obj );

      presetL_obj->append_child( presetD_obj );
    }
  }

  newPairObject("fragL",             fragL_obj,              root);
  newPairObject("fragN",             fragN,                  root);
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
  rc_t            rc    = kOkRC;
  preset_sel_t*   p     = _handleToPtr(h);  
  object_t*       root  = nullptr;
  unsigned        fragN = 0;
  const object_t* fragL_obj = nullptr;

  // parse the preset  file
  if((rc = objectFromFile(fn,root)) != kOkRC )
  {
    rc = cwLogError(rc,"The preset select file parse failed on '%s'.", cwStringNullGuard(fn));
    goto errLabel;
  }

  // remove all existing fragments
  _destroy_all_frags(p);

  // parse the root level
  if((rc = root->getv( "fragN",            fragN,
                       "fragL",            fragL_obj,
                       "masterWetInGain",  p->master_wet_in_gain,
                       "masterWetOutGain", p->master_wet_out_gain,
                       "masterDryGain",    p->master_dry_gain,
                       "masterSyncDelayMs",p->master_sync_delay_ms)) != kOkRC )
  {
    rc = cwLogError(rc,"Root preset select parse failed on '%s'.", cwStringNullGuard(fn));
    goto errLabel;
  }

  // for each fragment
  for(unsigned i=0; i<fragN; ++i)
  {
    frag_t* f = nullptr;
    const object_t* r = fragL_obj->child_ele(i);

    unsigned fragId=kInvalidId,endLoc=0,presetN=0,begPlayLoc=0,endPlayLoc=0;
    double igain=0,ogain=0,wetDryGain=0,fadeOutMs=0;
    const char* note = nullptr;
    const object_t* presetL_obj = nullptr;
    time::spec_t end_ts;

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

      // locate the preset index associated with the preset label
      if((preset_idx = _preset_label_to_index(p,preset_label)) == kInvalidIdx )
      {
        rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be restored.",cwStringNullGuard(preset_label));
        goto errLabel;
      }

      f->presetA[ preset_idx ].order  = order;
      f->presetA[ preset_idx ].playFl = playFl;
    }

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
    
    cwLogInfo("%3i id:%3i end loc:%3i end min:%f",i,f->fragId,f->endLoc, mins);
  }

  return rc;
}
