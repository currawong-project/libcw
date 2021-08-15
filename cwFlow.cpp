#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
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
    
    library_t library[] = {
      { "audioFileIn",  &audioFileIn::members },
      { "audioFileOut", &audioFileOut::members },
      { "pv_analysis",  &pv_analysis::members },
      { "pv_synthesis", &pv_synthesis::members },
      { "spec_dist",    &spec_dist::members },
      { nullptr, nullptr }
    };

    class_members_t* _find_library_record( const char* label )
    {
      for(library_t* l = library; l->label != nullptr; ++l)
        if( textCompare(l->label,label) == 0)
          return l->members;

      return nullptr;
    }
      
    flow_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,flow_t>(h); }


    rc_t  _parse_class_cfg(flow_t* p, const library_t* library, const object_t* classCfg)
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
                                   "presets", presetD )) != kOkRC )
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
        
        // parse the value dictionary
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
            rc = cwLogError(kSyntaxErrorRC,"The '%s' class member function record could not be found..", cd->label );
            goto errLabel;                    
          }

          // for each class value description
          for(unsigned j=0; j<varD->child_count(); ++j)
          {
            const object_t* var_obj   = varD->child_ele(j);
            const char*     type_str  = nullptr;
            unsigned        type_flag = 0;
            bool            srcVarFl  = false;            
            var_desc_t*     vd        = mem::allocZ<var_desc_t>();

            vd->label = var_obj->pair_label();
            vd->cfg   = var_obj->pair_value();

            // get the variable description 
            if((rc = var_obj->getv("type", type_str,
                                   "doc",  vd->docText)) != kOkRC )
            {
              rc = cwLogError(rc,"Parsing failed on class:%s variable: '%s'.", cd->label, vd->label );
              goto errLabel;
            }

            // convert the type string to a numeric type flag
            if( (type_flag = value_type_label_to_flag( type_str )) == kInvalidTId )
            {
              rc = cwLogError(rc,"Invalid type flag: '%s' class:'%s' value:'%s'.", type_str, cd->label, vd->label );
              goto errLabel;            
            }

            // get the variable description 
            if((rc = var_obj->getv_opt("srcFl", srcVarFl)) != kOkRC )
            {
              rc = cwLogError(rc,"Parsing optional fields failed on class:%s variable: '%s'.", cd->label, vd->label );
              goto errLabel;
            }
            
          
            vd->type |= type_flag;

            if( srcVarFl )
            {
              vd->flags |= kSrcVarFl;
            }

            vd->link     = cd->varDescL;
            cd->varDescL = vd;
          }
        }

      }

    errLabel:
      return rc;
    }

    

    rc_t _setup_input( flow_t* p, instance_t* in_inst, const char* in_var_label, const char* src_label_arg )
    {
      rc_t        rc        = kOkRC;
      unsigned    src_charN = textLength(src_label_arg);
      variable_t* src_var   = nullptr;
      instance_t* src_inst  = nullptr;
      variable_t* in_var    = nullptr;
      
      char        sbuf[ src_charN+1 ];
        
      // copy the id into the buf
      strncpy(sbuf,src_label_arg,src_charN+1);

      // advance suffix to the '.'
      char* suffix = sbuf;
      while( *suffix && *suffix != '.')
        ++suffix;

      // if a '.' suffix was found
      if( *suffix )
      {
        *suffix = 0;
        ++suffix;
      }

      // locate source instance
      if((rc = instance_find(p, sbuf, src_inst )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The source instance '%s' was not found.", cwStringNullGuard(sbuf) );
        goto errLabel;
      }

      // locate source value
      if((rc = var_get( src_inst, suffix, kAnyChIdx, src_var)) != kOkRC )
      {
        rc = cwLogError(rc,"The source var '%s' was not found on the source instance '%s'.", cwStringNullGuard(suffix), cwStringNullGuard(sbuf));
        goto errLabel;
      }

      // locate input value
      if((rc = var_get( in_inst, in_var_label, kAnyChIdx, in_var )) != kOkRC )
      {
        rc = cwLogError(rc,"The input value '%s' was not found on the instance '%s'.", cwStringNullGuard(in_var_label), cwStringNullGuard(in_inst->label));
        goto errLabel;        
      }

      // verify that the src_value type is included in the in_value type flags
      if( cwIsNotFlag(in_var->varDesc->type, src_var->varDesc->type) )
      {
        rc = cwLogError(kSyntaxErrorRC,"The type flags don't match on input:%s %s source:%s %s .", in_inst->label, in_var_label, src_inst->label, suffix);        
        goto errLabel;                
      }


      // connect in_var into src_var's outgoing var chain
      in_var->connect_link = src_var->connect_link;
      src_var->connect_link = in_var;
      

      in_var->value   = src_var->value;
      

      //cwLogInfo("'%s:%s' connected to source '%s:%s'.", in_inst->label, in_var_label, src_inst->label, suffix );
      
    errLabel:
      return rc;
    }


    void _destroy_inst( instance_t* inst )
    {
      if( inst->class_desc->members->destroy != nullptr && inst->userPtr != nullptr )
        inst->class_desc->members->destroy( inst );

      // destroy the instance variables
      variable_t* var0 = inst->varL;
      variable_t* var1 = nullptr;      
      while( var0 != nullptr )
      {
        var1 = var0->link;
        _var_destroy(var0);
        var0 = var1;
      }

      
      mem::release(inst->varMapA);
      mem::release(inst);
    }

    rc_t _create_instance_var_map( instance_t* inst )
    {
      rc_t rc = kOkRC;
      unsigned max_vid = kInvalidId;
      unsigned max_chIdx = 0;

      // determine the max variable vid and max channel index value among all variables
      for(variable_t* var=inst->varL; var!=nullptr; var=var->link)
      {
        
        if( var->vid == kInvalidId )
        {
          rc = cwLogError(kInvalidStateRC,"The variable '%s' on instance '%s' was not assigned an id.",var->label,inst->label);
          goto errLabel;
        }
        
        if( max_vid == kInvalidId || var->vid > max_vid )
          max_vid = var->vid;

        if( var->chIdx != kAnyChIdx && var->chIdx > max_chIdx )
          max_chIdx = var->chIdx;
      }

      // If there are any variables
      if( max_vid != kInvalidId )
      {
        // create the variable map array
        inst->varMapChN = max_chIdx + 1;
        inst->varMapIdN = max_vid + 1;
        inst->varMapN   = inst->varMapIdN * inst->varMapChN;
        inst->varMapA   = mem::allocZ<variable_t*>( inst->varMapN );

        // assign each variable to a location in the map
        for(variable_t* var=inst->varL; var!=nullptr; var=var->link)
        {
          unsigned idx = kInvalidIdx;

          if((rc = var_map_id_to_index( inst, var->vid, var->chIdx, idx )) != kOkRC )
            goto errLabel;

          
          // verify that there are not multiple variables per map position          
          if( inst->varMapA[ idx ] != nullptr )
          {
            variable_t* v0 = inst->varMapA[idx];
            rc = cwLogError(kInvalidStateRC,"The variable '%s' id:%i ch:%i and '%s' id:%i ch:%i share the same variable map position on instance: %s. This is usually cased by duplicate variable id's.",
                            v0->label,v0->vid,v0->chIdx, var->label,var->vid,var->chIdx,inst->label);
            goto errLabel;
          }

          // assign this variable to a map position
          inst->varMapA[ idx ] = var;
        }
        
      }

    errLabel:
      return rc;
      
    }
    
    rc_t _create_instance( flow_t* p, const object_t* inst_cfg )
    {
      rc_t            rc              = kOkRC;
      const char*     inst_label      = nullptr;
      const char*     inst_clas_label = nullptr;
      const object_t* in_dict         = nullptr;
      const char*     arg_label       = nullptr;
      const char*     preset_label    = nullptr;
      const object_t* arg_dict        = nullptr;
      const object_t* arg_cfg         = nullptr;
      instance_t*     inst            = nullptr;
      class_desc_t*   class_desc      = nullptr;
      
      // validate the syntax of the inst_cfg pair
      if( inst_cfg == nullptr || !inst_cfg->is_pair() || inst_cfg->pair_label()==nullptr || inst_cfg->pair_value()==nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance cfg. is not a valid pair.");
        goto errLabel;
      }
      
      inst_label = inst_cfg->pair_label();

      // verify that the instance label is unique
      if( instance_find(p,inst_label) != nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance label '%s' has already been used.",inst_label);
        goto errLabel;
      }
      
      // get the instance class label
      if((rc = inst_cfg->getv("class",inst_clas_label)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance cfg. %s is missing: 'type'.",inst_label);
        goto errLabel;        
      }
      
      // parse the optional args
      if((rc = inst_cfg->getv_opt("args",     arg_dict,
                                  "in",       in_dict,
                                  "argLabel", arg_label,
                                  "preset",   preset_label)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance cfg. '%s' missing: 'type'.",inst_label);
        goto errLabel;        
      }

      // if an argument dict was given
      if( arg_dict != nullptr  )
      {
        bool rptErrFl = true;
        
        // if no label was given then try 'default'
        if( arg_label == nullptr)
        {
          arg_label = "default";
          rptErrFl = false;
        }
        
        if((arg_cfg = arg_dict->find_child(arg_label)) == nullptr )
        {

          // if an explicit arg. label was given but it was not found
          if( rptErrFl )
          {
            rc = cwLogError(kSyntaxErrorRC,"The argument cfg. '%s' was not found on instance cfg. '%s'.",arg_label,inst_label);
            goto errLabel;
          }

          // no explicit arg. label was given - make arg_dict the instance arg cff.
          arg_cfg = arg_dict;
          arg_label = nullptr;
        }        
      }

      // locate the class desc
      if(( class_desc = class_desc_find(p,inst_clas_label)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The flow class '%s' was not found.",cwStringNullGuard(inst_clas_label));
        goto errLabel;
      }

      // instantiate the instance
      inst = mem::allocZ<instance_t>();

      inst->ctx          = p;
      inst->label        = inst_label;
      inst->inst_cfg     = inst_cfg;
      inst->arg_label    = arg_label;
      inst->arg_cfg      = arg_cfg;
      inst->class_desc   = class_desc;
      inst->preset_label = preset_label;
      
      // Instantiate the variables which have the 'src' attribute. We need these variables
      // to exist so that they can be connected to their source prior to the instance 
      // custom constructorbeing exected
      for(var_desc_t* vd=class_desc->varDescL; vd!=nullptr; vd=vd->link)
      {
        variable_t* var = nullptr;
        
        if( cwIsFlag(vd->flags,kSrcVarFl) )
        {
          if((rc = var_create( inst, vd->label, kInvalidId, kAnyChIdx, var )) != kOkRC )
            goto errLabel;
        }
      } 

      // connect the variable lists in the instance 'in' dictionary
      if( in_dict != nullptr && in_dict->is_dict() )
      {
        // for each input
        for(unsigned i=0; i<in_dict->child_count(); ++i)
        {
          const object_t*   in_pair      = in_dict->child_ele(i);
          const char*       in_var_label = in_pair->pair_label();
          const char*       src_label    = nullptr;
          const var_desc_t* vd           = nullptr;

          // note
          if((vd = var_desc_find( class_desc, in_var_label)) == nullptr )
          {
            cwLogError(kSyntaxErrorRC,"The value description for the 'in' value '%s' was not found on instance '%s'. Maybe '%s' is not marked as a 'src' attribute in the class variable descripiton.",in_var_label,inst->label,in_var_label);
            goto errLabel;
          }

          // Note that all variable's found by the above call to var_desc_find() should be 'src'
          // variables because they are the only ones that have been created so far.
          assert( cwIsFlag(vd->flags,kSrcVarFl) );

          // if this value is a 'src' value then it must be setup prior to the instance being instantiated
          if( cwIsFlag(vd->flags,kSrcVarFl) )
          {
            in_pair->pair_value()->value(src_label);

            // locate the pointer to the referenced output abuf and store it in inst->srcABuf[i]
            if((rc = _setup_input( p, inst, in_var_label, src_label )) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"The 'in' buffer at index %i is not valid on instance '%s'.", i, inst_label );
              goto errLabel;
            }
          }
        }
      }

      // complete the instantiation 
      if((rc = class_desc->members->create( inst )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Instantiation failed on instance '%s'.", inst_label );
        goto errLabel;
      }

      if((rc =_create_instance_var_map( inst )) != kOkRC )
        goto errLabel;
      

      // insert an instance in the network
      if( p->network_tail == nullptr )
      {
        p->network_head = inst;
        p->network_tail = inst;
      }
      else
      {
        p->network_tail->link = inst;
        p->network_tail       = inst;
      }      

      
    errLabel:
      if( rc != kOkRC )
        _destroy_inst(inst);
      
      return rc;      
    }


    rc_t _destroy( flow_t* p)
    {
      rc_t rc = kOkRC;

      instance_t* i0=p->network_head;
      instance_t* i1=nullptr;

      // destroy the instances
      while(i0!=nullptr)
      {
        i1 = i0->link;
        _destroy_inst(i0);
        i0 = i1;
      }

      // release the class records
      for(unsigned i=0; i<p->classDescN; ++i)
      {
        class_desc_t* cd  = p->classDescA + i;

        // release the var desc list
        var_desc_t*   vd0 = cd->varDescL;
        var_desc_t*   vd1 = nullptr;        
        while( vd0 != nullptr )
        {
          vd1 = vd0->link;
          mem::release(vd0);
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

      mem::release(p->classDescA);
      mem::release(p);
      
      return rc;
    }
    
  }
}

cw::rc_t cw::flow::create( handle_t& hRef, const object_t& classCfg, const object_t& cfg )
{
  rc_t rc = kOkRC;
  const object_t* network;
  
  if(( rc = destroy(hRef)) != kOkRC )
    return rc;

  flow_t* p   = mem::allocZ<flow_t>();
  p->cfg = &cfg;   // TODO: duplicate cfg?

  // parse the class description array
  if((rc = _parse_class_cfg(p,library,&classCfg)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the class description list.");
    goto errLabel;    
  }

  // parse the main audio file processor cfg record
  if((rc = cfg.getv("framesPerCycle",  p->framesPerCycle,
                    "network",         network)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the required flow configuration parameters.");
    goto errLabel;
  }

  if((rc = cfg.getv_opt("maxCycleCount", p->maxCycleCount)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the optional flow configuration parameters.");
    goto errLabel;
  }

  // for each instance in the network
  for(unsigned i=0; i<network->child_count(); ++i)
  {
    const object_t* inst_cfg = network->child_ele(i);

    // create the instance
    if( (rc= _create_instance( p, inst_cfg ) ) != kOkRC )
    {
      rc = cwLogError(rc,"The instantiation at network index %i is invalid.",i);
      goto errLabel;
    }
  }

  // apply preset
  for(instance_t* inst=p->network_head; inst!=nullptr; inst=inst->link)
    if( inst->preset_label != nullptr )
      if((rc = apply_preset( inst, inst->preset_label )) != kOkRC )
        goto errLabel;    
        
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;  
}

cw::rc_t cw::flow::exec(    handle_t& hRef )
{
  rc_t    rc = kOkRC;
  flow_t* p  = _handleToPtr(hRef);

  while( true )
  {  
    for(instance_t* inst = p->network_head; inst!=nullptr; inst=inst->link)    
      if((rc = inst->class_desc->members->exec(inst)) != kOkRC )
        break;

    p->cycleIndex += 1;
    if( p->maxCycleCount > 0 && p->cycleIndex >= p->maxCycleCount )
      break;
  }
  
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

void cw::flow::print_class_list( handle_t& hRef )
{
  class_desc_print(_handleToPtr(hRef));
}

void cw::flow::print_network( handle_t& hRef )
{
  network_print(_handleToPtr(hRef));
}


cw::rc_t cw::flow::test( const object_t* class_cfg, const object_t* cfg )
{
  rc_t rc = kOkRC;
  handle_t flowH;

  // create the flow object
  if((rc = create( flowH, *class_cfg, *cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Flow object create failed.");
    goto errLabel;
  }

  print_network(flowH);
  
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
  return rc;
}



