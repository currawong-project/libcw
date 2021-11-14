#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwPresetSel.h"

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
      
      double          defaultGain;
      double          defaultWetDryGain;
      double          defaultFadeOutMs;
      
      struct frag_str*         fragL;
    } preset_sel_t;

    preset_sel_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,preset_sel_t>(h); }

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
          mem::release(f1->presetA);
          mem::release(f1);
      
          return kOkRC;
        }
    
        f0 = f1;
      }
  
      return kOkRC;
    }
    
    rc_t _destroy( preset_sel_t* p )
    {
      while( p->fragL != nullptr )
        _delete_fragment(p, p->fragL->fragId );
      
      for(unsigned i=0; i<p->presetLabelN; ++i)
        mem::release( p->presetLabelA[i].label );
      
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

    rc_t _validate_preset_id( const frag_t* frag, unsigned preset_id )
    {
      bool fl =  (preset_id < frag->presetN) && (frag->presetA[ preset_id ].preset_idx == preset_id);

      return fl ? kOkRC : cwLogError(kInvalidIdRC,"The preset id '%i' is invalid on the fragment at loc:%i.",preset_id,frag->endLoc);
        
    }

    template< typename T >
    rc_t _set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, const T& value )
    {
      rc_t          rc = kOkRC;
      preset_sel_t* p  = _handleToPtr(h);
      frag_t*       f  = nullptr;

      // locate the requested fragment
      if((rc = _find_frag(p,fragId,f)) != kOkRC )
        goto errLabel;
  
      switch( varId )
      {
      case kDryFlVarId:
        f->dryFl = value;
        break;

      case kPresetSelectVarId:
        for(unsigned i=0; i<f->presetN; ++i)
          f->presetA[i].playFl = f->presetA[i].preset_idx == presetId ? value : false;
            
         break;
        
      case kPresetOrderVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          f->presetA[ presetId ].order = value;
        break;
        
      case kGainVarId:
        f->gain = value;
        break;
    
      case kFadeOutMsVarId:
        f->fadeOutMs = value;
        break;
    
      case kWetGainVarId:
        f->wetDryGain = value;
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

      // locate the requested fragment
      if((rc = _find_frag(p,fragId,f)) != kOkRC )
        goto errLabel;
  
      switch( varId )
      {
      case kEndLocVarId:
        valueRef = f->endLoc;
        break;
        
      case kDryFlVarId:
        valueRef = f->dryFl;
        break;

      case kPresetSelectVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          valueRef = f->presetA[ presetId ].playFl;
         break;
        
      case kPresetOrderVarId:
        if((rc = _validate_preset_id(f, presetId )) == kOkRC )
          valueRef = f->presetA[ presetId ].order;
        break;
        
      case kGainVarId:
        valueRef = f->gain;
        break;
    
      case kFadeOutMsVarId:
        valueRef = f->fadeOutMs;
        break;
    
      case kWetGainVarId:
        valueRef = f->wetDryGain;
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
  rc_t            rc     = kOkRC;
  preset_sel_t*   p      = nullptr;
  const object_t* labelL = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  p = mem::allocZ<preset_sel_t>();

  // parse the cfg
  if((rc = cfg->getv( "preset_labelL",        labelL,
                      "default_gain",         p->defaultGain,
                      "default_wet_dry_gain", p->defaultWetDryGain,
                      "default_fade_ms",      p->defaultFadeOutMs)) != kOkRC )
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
  return p->presetLabelA[ preset_idx].label;
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
    
cw::rc_t cw::preset_sel::create_fragment( handle_t h, unsigned fragId, unsigned end_loc )
{
  preset_sel_t* p = _handleToPtr(h);
  frag_t* f     = mem::allocZ<frag_t>();
  f->endLoc     = end_loc;
  f->fragId     = fragId;
  f->dryFl      = false;
  f->gain       = p->defaultGain;
  f->wetDryGain = p->defaultWetDryGain;
  f->fadeOutMs  = p->defaultFadeOutMs;
  f->presetA    = mem::allocZ<preset_t>(p->presetLabelN);
  f->presetN    = p->presetLabelN;

  for(unsigned i=0; i<p->presetLabelN; ++i)
    f->presetA[i].preset_idx = i;
  

  frag_t* f0;
  for(f0=p->fragL; f0!=nullptr; f0=f0->link)
    if( f0->link == nullptr )
      break;
  if( f0 == nullptr )
    p->fragL = f;
  else
    f0->link = f;
  
  return kOkRC;
}
  
cw::rc_t cw::preset_sel::delete_fragment( handle_t h, unsigned fragId )
{
  preset_sel_t* p  = _handleToPtr(h);
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
      mem::release(f1->presetA);
      mem::release(f1);
      
      return kOkRC;
    }
    
    f0 = f1;
  }
  
  return kOkRC;
}

bool cw::preset_sel::is_fragment_loc( handle_t h, unsigned loc )
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
    if( f->fragId == fragId )
      f->uiSelectFl = selectFl;
    else
      f->uiSelectFl = false;  
}


cw::rc_t cw::preset_sel::set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool value )
{ return _set_value(h,fragId,varId,presetId,value); }

cw::rc_t cw::preset_sel::set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned value )
{ return _set_value(h,fragId,varId,presetId,value); }

cw::rc_t cw::preset_sel::set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double value )
{ return _set_value(h,fragId,varId,presetId,value); }

cw::rc_t cw::preset_sel::get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool&     valueRef )
{ return _get_value(h,fragId,varId,presetId,valueRef); }

cw::rc_t cw::preset_sel::get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned& valueRef )
{ return _get_value(h,fragId,varId,presetId,valueRef); }

cw::rc_t cw::preset_sel::get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double&   valueRef )
{ return _get_value(h,fragId,varId,presetId,valueRef); }
    
cw::rc_t cw::preset_sel::write( handle_t h, const char* fn )
{
  rc_t rc = kOkRC;
  return rc;
}

cw::rc_t cw::preset_sel::read( handle_t h, const char* fn )
{
  rc_t rc = kOkRC;
  return rc;
}
