#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"

#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDspTypes.h" // coeff_t, sample_t, srate_t ...
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
#include "cwFlowNet.h"
#include "cwFlowProc.h"

namespace cw
{
  namespace flow
  {
    typedef struct library_str
    {
      const char*      label;
      class_members_t* members;
    } library_t;
    
    library_t g_library[] = {
      { "subnet",          &subnet::members },
      { "poly",            &poly::members },
      { "midi_in",         &midi_in::members },
      { "midi_out",        &midi_out::members },
      { "audio_in",        &audio_in::members },
      { "audio_out",       &audio_out::members },
      { "audio_file_in",   &audio_file_in::members },
      { "audio_file_out",  &audio_file_out::members },
      { "audio_gain",      &audio_gain::members },
      { "audio_split",     &audio_split::members },
      { "audio_duplicate", &audio_duplicate::members },
      { "audio_merge",     &audio_merge::members },
      { "audio_mix",       &audio_mix::members },
      { "sine_tone",       &sine_tone::members },
      { "pv_analysis",     &pv_analysis::members },
      { "pv_synthesis",    &pv_synthesis::members },
      { "spec_dist",       &spec_dist::members },
      { "compressor",      &compressor::members },
      { "limiter",         &limiter::members },
      { "audio_delay",     &audio_delay::members },
      { "dc_filter",       &dc_filter::members },
      { "balance",         &balance::members },
      { "audio_meter",     &audio_meter::members },
      { "audio_marker",    &audio_marker::members },
      { "xfade_ctl",       &xfade_ctl::members },
      { "poly_merge",      &poly_merge::members },
      { "sample_hold",     &sample_hold::members },
      { "number",          &number::members },
      { "timer",           &timer::members },
      { "counter",         &counter::members },
      { "list",            &list::members },
      { "add",             &add::members },
      { "preset",          &preset::members },
      { nullptr, nullptr }
    };

    class_members_t* _find_library_record( const char* label )
    {
      for(library_t* l = g_library; l->label != nullptr; ++l)
        if( textCompare(l->label,label) == 0)
          return l->members;

      return nullptr;
    }
      
    flow_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,flow_t>(h); }


    rc_t _is_var_flag_set( const object_t* var_flags_obj, const char* flag_label, const char* classLabel, const char* varLabel, bool&is_set_flag_ref )
    {
      rc_t rc = kOkRC;
      
      is_set_flag_ref = false;

      if( var_flags_obj != nullptr )
      {
        for(unsigned k=0; k<var_flags_obj->child_count(); ++k)
        {
          const object_t* tag_obj = var_flags_obj->child_ele(k);
          const char* tag = nullptr;
          if( tag_obj != nullptr &&  tag_obj->is_string() && (rc=tag_obj->value(tag))==kOkRC && tag != nullptr )
          {
            if( strcmp(tag,flag_label) == 0 )
              is_set_flag_ref = true;
          }
          else                  
          {
            rc = cwLogError(kSyntaxErrorRC,"An invalid or non-string value was found in a flow class '%s' variable:'%s' 'flags' field.",classLabel,varLabel);
          }
        }
      }
      return rc;
    }
    
    rc_t  _parse_class_cfg(flow_t* p, const object_t* classCfg)
    {
      rc_t rc = kOkRC;

      if( !classCfg->is_dict() )
        return cwLogError(kSyntaxErrorRC,"The class description dictionary does not have dictionary syntax.");
              
      p->classDescN = classCfg->child_count();
      p->classDescA = mem::allocZ<class_desc_t>( p->classDescN );      

      // for each class description
      for(unsigned i=0; i<p->classDescN; ++i)
      {
        const object_t* class_obj = classCfg->child_ele(i);
        const object_t* varD      = nullptr;
        const object_t* presetD   = nullptr;
        class_desc_t*   cd        = p->classDescA + i;

        cd->cfg    = class_obj->pair_value();
        cd->label  = class_obj->pair_label();
        
        // get the variable description 
        if((rc = cd->cfg->getv_opt("vars",  varD,
                                   "presets", presetD,
                                   "poly_limit_cnt", cd->polyLimitN)) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing failed while parsing class desc:'%s'", cwStringNullGuard(cd->label) );
          goto errLabel;                      
        }

        // parse the preset dictionary
        if( presetD != nullptr )
        {

          if( !presetD->is_dict() )
          {
            rc = cwLogError(rc,"The preset dictionary is not a dictionary on class desc:'%s'", cwStringNullGuard(cd->label) );
            goto errLabel;                      
          }

          // for each preset in the class desc.
          for(unsigned j=0; j<presetD->child_count(); ++j)
          {
            const object_t* pair = presetD->child_ele(j);

            if( !pair->pair_value()->is_dict() )
            {
              rc = cwLogError(kSyntaxErrorRC,"The preset '%s' in class desc '%s' is not a dictionary.", cwStringNullGuard(pair->pair_label()), cwStringNullGuard(cd->label));
              goto errLabel;
            }

            preset_t* preset =  mem::allocZ< preset_t >();
              
            preset->label = pair->pair_label();
            preset->cfg   = pair->pair_value();
            preset->link  = cd->presetL;
            cd->presetL   = preset;
          }
        }
        
        // parse the variable dictionary
        if( varD != nullptr )
        {
          if( !varD->is_dict() )
          {
            rc = cwLogError(rc,"The value dictionary is not a dictionary on class desc:'%s'", cwStringNullGuard(cd->label) );
            goto errLabel;                      
          }
          
          // get the class member functions
          if((cd->members = _find_library_record(cd->label)) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The '%s' class member function record could not be found.", cd->label );
            goto errLabel;                    
          }

          // for each class value description
          for(unsigned j=0; j<varD->child_count(); ++j)
          {
            const object_t* var_obj   = varD->child_ele(j);
            const object_t* var_flags_obj = nullptr;
            const char*     type_str  = nullptr;
            unsigned        type_flag = 0;
            bool            srcVarFl  = false;
            bool            srcOptFl  = false;
            var_desc_t*     vd        = mem::allocZ<var_desc_t>();

            vd->label = var_obj->pair_label();
            vd->cfg   = var_obj->pair_value();

            // get the variable description 
            if((rc = vd->cfg->getv("type", type_str,
                                   "doc",  vd->docText)) != kOkRC )
            {
              rc = cwLogError(rc,"Parsing failed on class:%s variable: '%s'.", cd->label, vd->label );
              goto errLabel;
            }

            // convert the type string to a numeric type flag
            if( (type_flag = value_type_label_to_flag( type_str )) == kInvalidTId )
            {
              rc = cwLogError(kSyntaxErrorRC,"Invalid type flag: '%s' class:'%s' value:'%s'.", type_str, cd->label, vd->label );
              goto errLabel;            
            }

            // get the variable description 
            if((rc = vd->cfg->getv_opt("flags", var_flags_obj,
                                       "value",vd->val_cfg)) != kOkRC )
            {
              rc = cwLogError(rc,"Parsing optional fields failed on class:%s variable: '%s'.", cd->label, vd->label );
              goto errLabel;
            }

            // check for 'src' flag
            if((rc = _is_var_flag_set( var_flags_obj, "src", cd->label, vd->label, srcVarFl )) != kOkRC )
              goto errLabel;

            // check for 'src_opt' flag
            if((rc = _is_var_flag_set( var_flags_obj, "src_opt", cd->label, vd->label, srcOptFl )) != kOkRC )
              goto errLabel;
          
            vd->type |= type_flag;

            if( srcVarFl )
              vd->flags |= kSrcVarFl;

            if( srcOptFl )
              vd->flags |= kSrcOptVarFl;

            vd->link     = cd->varDescL;
            cd->varDescL = vd;
          }
        }

      }

    errLabel:
      return rc;
    }

    rc_t _find_subnet_proc_class_desc( flow_t* p, const object_t* subnetProcD, const char* procInstLabel, const class_desc_t*& class_desc_ref )
    {
      rc_t                rc          = kOkRC;
      const object_t*     procInstD   = nullptr;
      const object_t*     classStr    = nullptr;
      const class_desc_t* class_desc  = nullptr;
      const char*         class_label = nullptr;

      class_desc_ref = nullptr;
        
      // find the proc inst dict in the subnet
      if((procInstD = subnetProcD->find_child(procInstLabel)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance '%s' from the proxy var list could not be foud in the subnet.",cwStringNullGuard(procInstLabel));
        goto errLabel;
      }

      // find the proc class label of the proc inst
      if((classStr = procInstD->find_child("class")) == nullptr || (rc = classStr->value(class_label))!=kOkRC)
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'class' field could not be found in the '%s' proc instance record.", cwStringNullGuard(procInstLabel));
        goto errLabel;
      }

      // find the associated class desc record
      if((class_desc_ref = class_desc_find(p,class_label)) == nullptr)
      {
        rc = cwLogError(kEleNotFoundRC,"The class desc record '%s' for the proc instance '%s' could not be found.",class_label,cwStringNullGuard(procInstLabel));
        goto errLabel;
      }

    errLabel:
      
      return rc;
    }


    rc_t _parse_subnet_var_proxy_string( const char* proxyStr, char*& procLabel_ref, char*& varLabel_ref )
    {
      rc_t rc       = kOkRC;
      procLabel_ref = nullptr;
      varLabel_ref  = nullptr;

      const char* period;

      // find the separating period
      if((period = firstMatchChar(proxyStr,'.')) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The separating '.' could not be found in the proxy string '%s'.",cwStringNullGuard(proxyStr));
        goto errLabel;
      }

      // validate the length of the proc inst label
      if( period-proxyStr == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"No proxy proc instance was found in the proxy string '%s'.",cwStringNullGuard(proxyStr));
        goto errLabel;        
      }

      // validate the length of the var label
      if( textLength(period+1) == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"No proxy var was found in the proxy string '%s'.",cwStringNullGuard(proxyStr));
        goto errLabel;        
      }

      
      procLabel_ref = mem::duplStr(proxyStr,period-proxyStr);
      varLabel_ref  = mem::duplStr(period+1);

    errLabel:
      return rc;
    }

    void _var_desc_release( var_desc_t*& vd )
    {
      mem::release(vd->proxyProcLabel);
      mem::release(vd->proxyVarLabel);
      mem::release(vd);
    }

    rc_t _create_subnet_var_desc( flow_t* p, class_desc_t* wrapProcClassDesc, const object_t* subnetProcD, const object_t* varDescPair, var_desc_t*& vd_ref )
    {
      rc_t                rc               = kOkRC;
      const object_t*     flagsCfg         = nullptr;
      const char*         proxyStr         = nullptr;
      const char*         docStr           = nullptr;
      const char*         varLabel         = nullptr;
      char*               proxyProcLabel   = nullptr;
      char*               proxyVarLabel    = nullptr;
      unsigned            proxyVarFlags    = 0;
      const class_desc_t* proxy_class_desc = nullptr;
      const var_desc_t*   proxy_var_desc   = nullptr;
      bool                subnetOutVarFl   = false;
      var_desc_t*         var_desc         = mem::allocZ<var_desc_t>();
      
      vd_ref = nullptr;

      // verify that the var label is valid
      if(!varDescPair->is_pair() || (varLabel=varDescPair->pair_label())==nullptr || varDescPair->pair_value()==nullptr || !varDescPair->pair_value()->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proxy var entry is not valid.");
        goto errLabel;
      }

      // parse the var desc dictionary
      if((rc = varDescPair->pair_value()->getv("proxy",proxyStr,
                                               "doc",  docStr)) != kOkRC )
      {
        rc = cwLogError(rc,"Parsing failed on the proxy var dictionary.");
        goto errLabel;
      }

      // parse the var desc dictionary optional fields
      if((rc = varDescPair->pair_value()->getv_opt("flags", flagsCfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Optional attribute parsing failed on the proxy var dictionary.");
        goto errLabel;
      }
      
      // parse the proxy string into it's two parts: <proc>.<var>
      if((rc = _parse_subnet_var_proxy_string( proxyStr, proxyProcLabel, proxyVarLabel )) != kOkRC )
      {
        goto errLabel;
      }
      
      // locate the class desc associated with proxy proc
      if((rc = _find_subnet_proc_class_desc( p, subnetProcD, proxyProcLabel, proxy_class_desc )) != kOkRC )
      {
        goto errLabel;
      }

      // locate the var desc associated with the proxy proc var
      if((proxy_var_desc = var_desc_find( proxy_class_desc, proxyVarLabel)) == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The variable desc '%s.%s' could not be found.",cwStringNullGuard(proxyProcLabel),cwStringNullGuard(proxyVarLabel));
        goto errLabel;                        
      }

      // most of the fields for the proxy are taken directly from the the actual var ...
      *var_desc = *proxy_var_desc;
      
      var_desc->label   = varLabel;  // ... but the label and doc are overwritten from subnet desc.
      var_desc->docText = docStr;
      var_desc->proxyProcLabel = proxyProcLabel;
      var_desc->proxyVarLabel  = proxyVarLabel;
      var_desc->link    = nullptr;

      if((rc = _is_var_flag_set( flagsCfg, "out", wrapProcClassDesc->label, var_desc->label, subnetOutVarFl )) != kOkRC )
        goto errLabel;

      var_desc->flags = cwEnaFlag(var_desc->flags, kSubnetOutVarFl, subnetOutVarFl);
      
      vd_ref = var_desc;

    errLabel:

      if( rc != kOkRC )
      {        
        rc = cwLogError(rc,"The creation of proxy var '%s' failed.",cwStringNullGuard(varLabel));
        _var_desc_release(var_desc);
      }
      
      return rc;
    }
    
    rc_t _parse_subnet_vars( flow_t* p, class_desc_t* class_desc, const object_t* subnetProcD, const object_t* varD )
    {
      rc_t rc = kOkRC;

      unsigned varN = 0;
      
      if( !varD->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proxy variable dictionary is invalid.");
        goto errLabel;
      }

      varN = varD->child_count();

      // Fill the class_Desc.varDescL list from the subnet 'vars' dictioanry
      for(unsigned i=0; i<varN; ++i)
      {
        const object_t* child_pair = varD->child_ele(i);
        var_desc_t*     var_desc   = nullptr;

        if((rc = _create_subnet_var_desc( p, class_desc, subnetProcD, child_pair, var_desc )) != kOkRC )
          goto errLabel;


        var_desc->link = class_desc->varDescL;
        class_desc->varDescL = var_desc;

        //printf("Wrapper var-desc created: %i of %i : %s:%s proxy:%s:%s flags:%i.\n", i, varN, class_desc->label, var_desc->label, var_desc->proxyProcLabel,var_desc->proxyVarLabel,var_desc->flags);
        
      }

    errLabel:
      return rc;
    }

    rc_t _create_subnet_class_desc( flow_t* p, const object_t* class_obj, class_desc_t* class_desc )
    {
      rc_t            rc                  = kOkRC;
      const object_t* varD                = nullptr;
      const object_t* subnetD             = nullptr;
      const object_t* subnetProcD         = nullptr;
      const object_t* subnetPresetD       = nullptr;          
      const char*     subnetProcDescLabel = nullptr;

      // Validate the subnet proc desc label and value
      if( class_obj==nullptr || !class_obj->is_pair() || class_obj->pair_value()==nullptr || !class_obj->pair_value()->is_dict() || (subnetProcDescLabel = class_obj->pair_label()) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"An invalid subnet description '%s' was encountered.",cwStringNullGuard(subnetProcDescLabel));
        goto errLabel;
      }

      // verify that another subnet with the same name does not already exist
      if( class_desc_find(p,subnetProcDescLabel) != nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"A subnet named '%s' already exists.",subnetProcDescLabel);
        goto errLabel;
      }

      class_desc->cfg    = class_obj->pair_value();
      class_desc->label  = class_obj->pair_label();

      // get the 'subnet' members record
      if((class_desc->members = _find_library_record("subnet")) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'subnet' class member function record could not be found." );
        goto errLabel;                    
      }
                
      // get the variable description 
      if((rc = class_desc->cfg->getv_opt("vars",  varD,
                                         "network", subnetD)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed while parsing subnet desc:'%s'", cwStringNullGuard(class_desc->label) );
        goto errLabel;                      
      }

      // get the subnet proc and preset dictionaries
      if((rc = subnetD->getv("procs",   subnetProcD,
                             "presets", subnetPresetD)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed on the 'network' element.");
        goto errLabel;
      }

      // fill class_desc.varDescL from the subnet vars dictionary
      if((rc = _parse_subnet_vars( p, class_desc, subnetProcD, varD )) != kOkRC )
      {
        rc = cwLogError(rc,"Subnet 'vars' processing failed.");
        goto errLabel;          
      }

      
    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"'proc' class description creation failed for the subnet '%s'. ",cwStringNullGuard(subnetProcDescLabel));
        
      return rc;
    }
        
  
    rc_t  _parse_subnet_cfg(flow_t* p, const object_t* subnetCfg)
    {
      rc_t rc = kOkRC;
      
      if( !subnetCfg->is_dict() )
        return cwLogError(kSyntaxErrorRC,"The subnet class description dictionary does not have dictionary syntax.");
              
      unsigned subnetDescN = subnetCfg->child_count();
      p->subnetDescA = mem::allocZ<class_desc_t>( subnetDescN );      

      // for each subnet description
      for(unsigned i=0; i<subnetDescN; ++i)
      {
        const object_t* subnet_obj = subnetCfg->child_ele(i);

        if((rc = _create_subnet_class_desc(p, subnet_obj, p->subnetDescA + i )) != kOkRC )
        {
          rc = cwLogError(rc,"Subnet class description create failed on the subnet at index:%i.",i);
          goto errLabel;
        }

        // we have to update the size of the subnet class array
        // as we go because we may want to find subnets classes within
        // subnets which are definted later and so the length p->subnetA[]
        // must be correct 
        p->subnetDescN += 1;
      }

      assert( subnetDescN == p->subnetDescN );
    errLabel:

      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Subnet processing failed.");
        
      }
      
      return rc;

    }

    void _release_class_desc_array( class_desc_t*& classDescA, unsigned classDescN )
    {
      // release the class records
      for(unsigned i=0; i<classDescN; ++i)
      {
        class_desc_t* cd  = classDescA + i;

        // release the var desc list
        var_desc_t*   vd0 = cd->varDescL;
        var_desc_t*   vd1 = nullptr;        
        while( vd0 != nullptr )
        {
          vd1 = vd0->link;
          _var_desc_release(vd0);
          vd0 = vd1;
        }

        // release the preset list
        preset_t* pr0 = cd->presetL;
        preset_t* pr1 = nullptr;
        while( pr0 != nullptr )
        {
          pr1 = pr0->link;
          mem::release(pr0);
          pr0 = pr1;
        }
      }

      mem::release(classDescA);      
    }
    
    rc_t _destroy( flow_t* p)
    {
      rc_t rc = kOkRC;

      if( p == nullptr )
        return rc;

      network_destroy(p->net);
      
      _release_class_desc_array(p->classDescA,p->classDescN);
      _release_class_desc_array(p->subnetDescA,p->subnetDescN);
      
      mem::release(p);
      
      return rc;
    }

    
  }
}

void cw::flow::print_abuf( const abuf_t* abuf )
{
  printf("Abuf: sr:%7.1f chs:%3i frameN:%4i %p",abuf->srate,abuf->chN,abuf->frameN,abuf->buf);
}

void cw::flow::print_external_device( const external_device_t* dev )
{
  printf("Dev: %10s type:%3i fl:0x%x : ", cwStringNullGuard(dev->devLabel),dev->typeId,dev->flags);
  if( dev->typeId == kAudioDevTypeId )
    print_abuf(dev->u.a.abuf);
  printf("\n");
}



cw::rc_t cw::flow::create( handle_t&          hRef,
                           const object_t*    classCfg,
                           const object_t*    flowCfg,
                           const object_t*    subnetCfg,
                           external_device_t* deviceA,
                           unsigned           deviceN )
{
  rc_t            rc               = kOkRC;
  const object_t* networkCfg       = nullptr; 
  bool            printClassDictFl = false;
  bool            printNetworkFl   = false;
  variable_t*     proxyVarL        = nullptr;
  
  if(( rc = destroy(hRef)) != kOkRC )
    return rc;

  flow_t* p   = mem::allocZ<flow_t>();
  p->flowCfg    = flowCfg;   // TODO: duplicate cfg?
  p->deviceA    = deviceA;
  p->deviceN    = deviceN;

  // parse the class description array
  if((rc = _parse_class_cfg(p,classCfg)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the class description list.");
    goto errLabel;    
  }

  if( subnetCfg != nullptr )
    if((rc = _parse_subnet_cfg(p,subnetCfg)) != kOkRC )
    {
      rc = cwLogError(kSyntaxErrorRC,"Error parsing the subnet list.");
      goto errLabel;          
    }

  // parse the main audio file processor cfg record
  if((rc = flowCfg->getv("network", networkCfg)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the required flow configuration parameters.");
    goto errLabel;
  }

  p->framesPerCycle = kDefaultFramesPerCycle;
  p->sample_rate    = kDefaultSampleRate;

  // parse the optional args
  if((rc = flowCfg->getv_opt("framesPerCycle",      p->framesPerCycle,
                            "sample_rate",          p->sample_rate,
                            "maxCycleCount",        p->maxCycleCount,
                            "multiPriPresetProbFl", p->multiPriPresetProbFl,
                            "multiSecPresetProbFl", p->multiSecPresetProbFl,
                            "multiPresetInterpFl",  p->multiPresetInterpFl,
                            "printClassDictFl",     printClassDictFl,
                            "printNetworkFl",       printNetworkFl)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the optional flow configuration parameters.");
    goto errLabel;
  }

  for(unsigned i=0; i<deviceN; ++i)
    if( deviceA[i].typeId == kAudioDevTypeId )
    {
      if( deviceA[i].u.a.abuf == NULL )
      {
        rc = cwLogError(kInvalidArgRC,"The audio '%s' device does not have a valid audio buffer.",cwStringNullGuard(deviceA[i].devLabel));
        goto errLabel;
      }
      else
        if( deviceA[i].u.a.abuf->frameN != p->framesPerCycle )
          cwLogWarning("The audio frame count (%i) for audio device '%s' does not match the Flow framesPerCycle (%i).",deviceA[i].u.a.abuf->frameN,p->framesPerCycle);
    }
  
  // print the class dict
  if( printClassDictFl )
      class_dict_print( p );

  // instantiate the network
  if((rc = network_create(p,networkCfg,p->net,proxyVarL)) != kOkRC )
  {
    rc = cwLogError(rc,"Network creation failed.");
    goto errLabel;
  }

  if( printNetworkFl )
    network_print(p->net);

  hRef.set(p);
  
 errLabel:

  
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;  
}

cw::rc_t cw::flow::destroy( handle_t& hRef )
{
  rc_t    rc = kOkRC;
  flow_t* p  = nullptr;;
  
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);

  _destroy(p);

  hRef.clear();
  
  return rc;
}

unsigned cw::flow::preset_cfg_flags( handle_t h )
{
  flow_t*  p     = _handleToPtr(h);
  unsigned flags = 0;
  
  if( p->multiPriPresetProbFl )
    flags |= kPriPresetProbFl;
  
  if( p->multiSecPresetProbFl )
    flags |= kSecPresetProbFl;
  
  if( p->multiPresetInterpFl )
    flags |= kInterpPresetFl;

  return flags;
}


cw::rc_t cw::flow::exec_cycle( handle_t h )
{
  return exec_cycle(_handleToPtr(h)->net);
}

cw::rc_t cw::flow::exec(    handle_t h )
{
  rc_t    rc = kOkRC;
  flow_t* p  = _handleToPtr(h);

  while( true )
  {  
    rc = exec_cycle(p->net);

    if( rc == kEofRC )
    {
      rc = kOkRC;
      break;
    }    
    
    p->cycleIndex += 1;
    if( p->maxCycleCount > 0 && p->cycleIndex >= p->maxCycleCount )
    {
       cwLogInfo("'maxCycleCnt' reached: %i. Shutting down flow.",p->maxCycleCount);
      break;
    }
  }

  return rc;
}

cw::rc_t cw::flow::apply_preset( handle_t h, const char* presetLabel )
{
  flow_t* p  = _handleToPtr(h);
  return network_apply_preset(p->net,presetLabel);
}

cw::rc_t cw::flow::apply_dual_preset( handle_t h, const char* presetLabel_0, const char* presetLabel_1, double coeff )
{
  flow_t* p  = _handleToPtr(h);
  return network_apply_dual_preset(p->net,presetLabel_0, presetLabel_1, coeff );
}

cw::rc_t cw::flow::apply_preset( handle_t h, const multi_preset_selector_t& mps )
{
  flow_t* p  = _handleToPtr(h);
  return network_apply_preset(p->net,mps);
}



cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool value )
{ return set_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int value )
{ return set_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value )
{ return set_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float value )
{ return set_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double value )
{ return set_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool& valueRef )
{ return get_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int& valueRef )
{ return get_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned& valueRef )
{ return get_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float& valueRef )
{ return get_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double& valueRef )
{ return get_variable_value( _handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }



void cw::flow::print_class_list( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  class_dict_print(p);
}

void cw::flow::print_network( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->deviceN; ++i)
    print_external_device( p->deviceA + i );
  
  network_print(p->net);
}


cw::rc_t cw::flow::test(  const object_t* cfg, int argc, const char* argv[] )
{
  rc_t rc = kOkRC;
  handle_t flowH;

  const char*     proc_cfg_fname   = nullptr;
  const char*     subnet_cfg_fname = nullptr;
  const object_t* test_cases_cfg   = nullptr;
  object_t*       class_cfg        = nullptr;
  object_t*       subnet_cfg       = nullptr;
  const object_t* test_cfg         = nullptr;

  if( argc < 2 || textLength(argv[1]) == 0 )
  {
    rc = cwLogError(kInvalidArgRC,"No 'test-case' label was given on the command line.");
    goto errLabel;
  }
  
  if((rc                                                 = cfg->getv("proc_cfg_fname",proc_cfg_fname,
                     "test_cases",     test_cases_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The name of the flow_proc_dict file could not be parsed.");
    goto errLabel;
  }

  // get the subnet cfg filename
  if((rc = cfg->getv_opt("subnet_cfg_fname",subnet_cfg_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"The name of the subnet file could not be parsed.");
    goto errLabel;
  }

  // find the user requested test case
  if((test_cfg = test_cases_cfg->find_child(argv[1])) == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The test case named '%s' was not found.",argv[1]);
    goto errLabel;
  }  

  // parse the proc dict. file
  if((rc = objectFromFile(proc_cfg_fname,class_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The proc dictionary could not be read from '%s'.",cwStringNullGuard(proc_cfg_fname));
    goto errLabel;
  }

  // parse the subnet dict file
  if((rc = objectFromFile(subnet_cfg_fname,subnet_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The subnet dictionary could not be read from '%s'.",cwStringNullGuard(subnet_cfg_fname));
    goto errLabel;
  }
  
  // create the flow object
  if((rc = create( flowH, class_cfg, test_cfg, subnet_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Flow object create failed.");
    goto errLabel;
  }

  //print_network(flowH);
  
  // run the network
  if((rc = exec( flowH )) != kOkRC )
    rc = cwLogError(rc,"Execution failed.");
    

  // destroy the flow object
  if((rc = destroy(flowH)) != kOkRC )
  {
    rc = cwLogError(rc,"Close the flow object.");
    goto errLabel;
  }
  
 errLabel:
  if( class_cfg != nullptr )
    class_cfg->free();
  return rc;
}



