#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDspTypes.h" // real_t, sample_t
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwFlowDecl.h"
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
    
    library_t g_library[] = {
      { "midi_in",         &midi_in::members },
      { "midi_out",        &midi_out::members },
      { "audio_in",        &audio_in::members },
      { "audio_out",       &audio_out::members },
      { "audioFileIn",     &audioFileIn::members },
      { "audioFileOut",    &audioFileOut::members },
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
            rc = cwLogError(kSyntaxErrorRC,"The '%s' class member function record could not be found..", cd->label );
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
              rc = cwLogError(rc,"Invalid type flag: '%s' class:'%s' value:'%s'.", type_str, cd->label, vd->label );
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

    void _connect_vars( variable_t* src_var, variable_t* in_var )
    {
      // connect in_var into src_var's outgoing var chain
      in_var->connect_link  = src_var->connect_link;
      src_var->connect_link = in_var;

      assert( src_var->value != nullptr );
          
      in_var->value    = src_var->value;
      in_var->src_var = src_var;
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
      if((rc = var_find( src_inst, suffix, kAnyChIdx, src_var)) != kOkRC )
      {
        rc = cwLogError(rc,"The source var '%s' was not found on the source instance '%s'.", cwStringNullGuard(suffix), cwStringNullGuard(sbuf));
        goto errLabel;
      }

      // locate input value
      if((rc = var_find( in_inst, in_var_label, kAnyChIdx, in_var )) != kOkRC )
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

      if( src_var->value == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The source value is null on the connection input:'%s' %s source:'%s' '%s' .", in_inst->label, in_var_label, src_inst->label, suffix);
        goto errLabel;
      }

      _connect_vars( src_var, in_var );

      //cwLogInfo("'%s:%s' connected to source '%s:%s' %p.", in_inst->label, in_var_label, src_inst->label, suffix, in_var->value );
      
    errLabel:
      return rc;
    }


    void _destroy_inst( instance_t* inst )
    {
      if( inst == nullptr )
        return;
      
      if( inst->class_desc->members->destroy != nullptr && inst->userPtr != nullptr )
        inst->class_desc->members->destroy( inst );

      // destroy the instance variables
      variable_t* var0 = inst->varL;
      variable_t* var1 = nullptr;      
      while( var0 != nullptr )
      {
        var1 = var0->var_link;
        _var_destroy(var0);
        var0 = var1;
      }

      
      mem::release(inst->varMapA);
      mem::release(inst);
    }

    rc_t  _var_map_id_to_index(  instance_t* inst, unsigned vid, unsigned chIdx, unsigned& idxRef );

    rc_t _create_instance_var_map( instance_t* inst )
    {
      rc_t        rc        = kOkRC;
      unsigned    max_vid   = kInvalidId;
      unsigned    max_chIdx = 0;
      variable_t* var       = inst->varL;
      //variable_t* v0        = nullptr;
      
      // determine the max variable vid and max channel index value among all variables
      for(; var!=nullptr; var = var->var_link )
      {
        if( var->vid != kInvalidId )
        {
          if( max_vid == kInvalidId || var->vid > max_vid )
            max_vid = var->vid;

          if( var->chIdx != kAnyChIdx && (var->chIdx+1) > max_chIdx )
            max_chIdx = (var->chIdx + 1);

        }
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
        for(variable_t* var=inst->varL; var!=nullptr; var=var->var_link)
          if( var->vid != kInvalidId )
          {
            unsigned idx = kInvalidIdx;

            if((rc = _var_map_id_to_index( inst, var->vid, var->chIdx, idx )) != kOkRC )
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

            if( var->chIdx != kAnyChIdx && var->value == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The value of the variable '%s' ch:%i on instance:'%s' has not been set.",var->label,var->chIdx,inst->label);
              goto errLabel;
            }

          }
        
      }

    errLabel:
      return rc;
      
    }

    void _complete_input_connections( instance_t* inst )
    {
      for(variable_t* var=inst->varL; var!=nullptr; var=var->var_link)
        if(var->chIdx == kAnyChIdx && is_connected_to_external_proc(var) )
        {

          variable_t* base_src_var = var->src_var;

          // since 'var' is on the 'any' channel the 'src' var must also be on the 'any' channel
          assert( base_src_var->chIdx == kAnyChIdx );
          
          //printf("%s %s\n",inst->label,var->label);
          
          // for each var channel in the input var
          for(variable_t* in_var = var->ch_link; in_var != nullptr; in_var=in_var->ch_link)
          {
            // locate the matching channel on the 'src' var
            variable_t* svar = base_src_var;
            for(; svar!=nullptr; svar=svar->ch_link)
              if( svar->chIdx == in_var->chIdx )
                break;

            // connect the src->input var
            _connect_vars( svar==nullptr ? base_src_var : svar, in_var);
          }
        }
    }
    
    rc_t _call_value_func_on_all_variables( instance_t* inst )
    {
      rc_t rc  = kOkRC;
      rc_t rc1 = kOkRC;
      
      for(unsigned i=0; i<inst->varMapN; ++i)
        if( inst->varMapA[i] != nullptr && inst->varMapA[i]->vid != kInvalidId )
        {
          variable_t* var = inst->varMapA[i];
          
          if((rc = var->inst->class_desc->members->value( var->inst, var )) != kOkRC )
            rc1 = cwLogError(rc,"The proc instance '%s' reported an invalid valid on variable:%s chIdx:%i.", var->inst->label, var->label, var->chIdx );
        }
      
      return rc1;
    }

    rc_t _var_channelize( instance_t* inst, const char* preset_label,  const char* type_src_label, const char* value_label, const object_t* value )
    {
      rc_t rc = kOkRC;
      
      variable_t*     dummy       = nullptr;

      // verify that a valid value exists
      if( value == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"Unexpected missig value on %s preset '%s' instance '%s' variable '%s'.", type_src_label, preset_label, inst->label, cwStringNullGuard(value_label) );
        goto errLabel;
      }

      // if a list of values was given
      if( value->is_list() )
      {
        for(unsigned chIdx=0; chIdx<value->child_count(); ++chIdx)
          if((rc = var_channelize( inst, value_label, chIdx, value->child_ele(chIdx), kInvalidId, dummy )) != kOkRC )
            goto errLabel;
      }
      else // otherwise a single value was given
      {          
        if((rc = var_channelize( inst, value_label, kAnyChIdx, value, kInvalidId, dummy )) != kOkRC )
          goto errLabel;
      }

    errLabel:
      return rc;
    }
    
    rc_t _preset_channelize_vars( instance_t* inst, const char* type_src_label, const char* preset_label, const object_t* preset_cfg )
    {
      rc_t rc = kOkRC;

      //cwLogInfo("Channelizing '%s' preset %i vars for '%s'.",type_src_label, preset_cfg==nullptr ? 0 : preset_cfg->child_count(), inst->label );
      
      // validate the syntax of the preset record
      if( !preset_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The preset record '%s' on %s '%s' is not a dictionary.", preset_label, type_src_label, inst->class_desc->label );
        goto errLabel;
      }


      // for each preset variable
      for(unsigned i=0; i<preset_cfg->child_count(); ++i)
      {
        const object_t* value       = preset_cfg->child_ele(i)->pair_value();
        const char*     value_label = preset_cfg->child_ele(i)->pair_label();
        if((rc = _var_channelize( inst, preset_label, type_src_label, value_label, value )) != kOkRC )
          goto errLabel;
        
        
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Apply %s preset failed on instance:%s class:%s preset:%s.", type_src_label, inst->label, inst->class_desc->label, preset_label );

      return rc;
    }


    template< typename T >
    T _interp_dual_value( T v0, T v1, double coeff )
    {
      T y;
      if( v0 == v1 )
        y = v0;
      else
        y = (T)(v0 + (v1-v0)*coeff );

      //printf("%f %f -> %f\n",(double)v0,(double)v1,(double)y);
      return y;
    }

    rc_t _set_var_from_dual_preset_scalar_scalar( instance_t* inst, const char* var_label, const object_t* scalar_0, const object_t* scalar_1, double coeff, unsigned chIdx )
    {
      rc_t rc = kOkRC;
      object_t interped_value;
      variable_t* dummy = nullptr;

      // one of the input values must exist
      if( scalar_0==nullptr && scalar_1==nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The numeric types of both operands of a dual value are null.");
        goto errLabel;
      }

      // It's possible that one or the other input value does not exist
      if( scalar_0 == nullptr )
        scalar_0 = scalar_1;
      else
      {
        if( scalar_1 == nullptr )
          scalar_1 = scalar_0;
      }

      // verify that the input values are the same type
      if( scalar_0->type->id != scalar_1->type->id )
      {
        rc = cwLogError(kInvalidArgRC,"The numeric types of both operands of a dual value preset must match. (%s != %s).",cwStringNullGuard(scalar_0->type->label),cwStringNullGuard(scalar_1->type->label));
        goto errLabel;
      }

      printf("%s:%s :",inst->label,var_label);
      
      switch( scalar_0->type->id )
      {
        case kInt32TId:
          interped_value.set_value( _interp_dual_value(scalar_0->u.i32,scalar_1->u.i32,coeff) );
          break;
        case kUInt32TId:
          interped_value.set_value( _interp_dual_value(scalar_0->u.u32,scalar_1->u.u32,coeff) );
          break;
        case kInt64TId:
          assert(0);
          //interped_value.set_value( _interp_dual_value(scalar_0->u.i64,scalar_1->u.i64,coeff) );          
          break;
        case kUInt64TId:
          assert(0);
          //interped_value.set_value( _interp_dual_value(scalar_0->u.u64,scalar_1->u.u64,coeff) );          
          break;
        case kFloatTId:
          interped_value.set_value( _interp_dual_value(scalar_0->u.f,scalar_1->u.f,coeff) );          
          break;
        case kDoubleTId:
          interped_value.set_value( _interp_dual_value(scalar_0->u.d,scalar_1->u.d,coeff) );          
          break;
          
        default:
          rc = cwLogError(kInvalidStateRC,"Preset dual values of type '%s' cannot be interpolated.",cwStringNullGuard(scalar_0->type->label));
          goto errLabel;
      }

      
      if((rc = var_channelize( inst, var_label, chIdx, &interped_value, kInvalidId, dummy )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Dual value preset application failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _set_var_from_dual_preset_list_list( instance_t* inst, const char* var_label, const object_t* list_0, const object_t* list_1, double coeff )
    {
      rc_t rc = kOkRC;
      
      if( list_0->child_count() != list_1->child_count() )
        return cwLogError(kInvalidArgRC,"If two lists are to be applied as a dual preset they must be the same length.");

      for(unsigned chIdx=0; chIdx<list_0->child_count(); ++chIdx)
        if((rc = _set_var_from_dual_preset_scalar_scalar(inst,var_label,list_0->child_ele(chIdx),list_1->child_ele(chIdx),coeff,chIdx)) != kOkRC )
          goto errLabel;

    errLabel:
      return rc;
    }

    rc_t _set_var_from_dual_preset_scalar_list( instance_t* inst, const char* var_label, const object_t* scalar, const object_t* list, double coeff )
    {
      rc_t rc = kOkRC;
      for(unsigned chIdx=0; chIdx<list->child_count(); ++chIdx)
        if((rc = _set_var_from_dual_preset_scalar_scalar(inst,var_label,scalar,list->child_ele(chIdx),coeff,chIdx)) != kOkRC )
          goto errLabel;
      
    errLabel:
      return rc;
    }

    rc_t _set_var_from_dual_preset_list_scalar( instance_t* inst, const char* var_label, const object_t* list, const object_t* scalar, double coeff )
    {
      rc_t rc = kOkRC;
      for(unsigned chIdx=0; chIdx<list->child_count(); ++chIdx)
        if((rc = _set_var_from_dual_preset_scalar_scalar(inst,var_label,list->child_ele(chIdx),scalar,coeff,chIdx)) != kOkRC )
          goto errLabel;
      
    errLabel:
      return rc;
    }
    
    rc_t _set_var_from_dual_preset_scalar_scalar( instance_t* inst, const char* var_label, const object_t* scalar_0, const object_t* scalar_1, double coeff )
    {
      return _set_var_from_dual_preset_scalar_scalar(inst,var_label,scalar_0,scalar_1,coeff,kAnyChIdx);
    }
    

    rc_t _is_legal_dual_value( const object_t* value )
    {
      rc_t rc = kOkRC;
      
      if( value->is_list() )
      {
        if( value->child_count() == 0 )
        {
          rc = cwLogError(kInvalidArgRC,"Empty lists values cannot be applied as part of a dual value preset.");
          goto errLabel;
        }

      }
      else
      {
        switch( value->type->id )
        {
          case kInt32TId:
          case kUInt32TId:
          case kInt64TId:
          case kUInt64TId:
          case kFloatTId:
          case kDoubleTId:
            break;
          default:
            rc = cwLogError(kInvalidArgRC,"Objects of type '%s' cannot be applied as part of a dual value preset.",cwStringNullGuard(value->type->label));
        }
      }
      
    errLabel:
      return rc;
      
    }
    
    rc_t _set_var_from_dual_preset( instance_t* inst, const char* var_label, const object_t* value_0, const object_t* value_1, double coeff )
    {
      rc_t rc = kOkRC;

      // dual values must be either numeric scalars or lists
      if((rc = _is_legal_dual_value(value_0)) != kOkRC || (rc = _is_legal_dual_value(value_1)) != kOkRC)
         goto errLabel;
              
      
      // if both values are lists then they must be the same length
      if( value_0->is_list() && value_1->is_list() )
      {
        rc = _set_var_from_dual_preset_list_list( inst, var_label, value_0, value_1, coeff );
        goto errLabel;
      }
      else
      {
        // if value_0 is a list and value_1 is a scalar
        if( value_0->is_list() )
        {
          rc = _set_var_from_dual_preset_list_scalar( inst, var_label, value_0, value_1, coeff );
          goto errLabel;
        }
        else
        {
          // if value_1 is a list and value_0 is a scalar
          if( value_1->is_list() )
          {
            rc = _set_var_from_dual_preset_scalar_list( inst, var_label, value_0, value_1, coeff );
            goto errLabel;
          }
          else // both values are scalars
          {
            rc = _set_var_from_dual_preset_scalar_scalar( inst, var_label, value_0, value_1, coeff );
            goto errLabel;
          }
        }
      }

    errLabel:
      return rc;
    }
    
    rc_t _multi_preset_channelize_vars( instance_t* inst, const char* type_src_label, const char** presetLabelA, const object_t** preset_cfgA, unsigned presetN, double coeff )
    {
      rc_t rc = kOkRC;

      const char* preset_label_0 = "<None>";
      const char* preset_label_1 = "<None>";

      //cwLogInfo("Channelizing '%s' preset %i vars for '%s'.",type_src_label, preset_cfg==nullptr ? 0 : preset_cfg->child_count(), inst->label );

      if( presetN < 2 )
      {
        rc = cwLogError(kInvalidArgRC,"There must be at least 2 presets selected to interpolate between preset variable dictionaries.");
        goto errLabel;
      }

      if( presetN > 2 )
      {
        cwLogWarning("More than two presets dictionaries were specified for interpolation. Only the first two will be used.");
        goto errLabel;
      }

      preset_label_0 = presetLabelA[0];
      preset_label_1 = presetLabelA[1];
      
      // validate each of the preset records is a dict
      for(unsigned i=0; i<presetN; ++i)
        if( !preset_cfgA[i]->is_dict() )
        {
          rc = cwLogError(kSyntaxErrorRC,"The preset record '%s' on %s '%s' is not a dictionary.", presetLabelA[i], type_src_label, inst->class_desc->label );
          goto errLabel;
        }


      // for each preset variable in the first preset var dict
      for(unsigned i=0; i<preset_cfgA[0]->child_count(); ++i)
      {
        const char*     var_label   = preset_cfgA[0]->child_ele(i)->pair_label();
        const object_t* value_0     = preset_cfgA[0]->child_ele(i)->pair_value();

        const object_t* value_1     = preset_cfgA[1]->find_child(var_label);

        if( value_0 == nullptr && value_1 == nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unexpected missig values on %s preset '%s' instance '%s' variable '%s'.", type_src_label, presetLabelA[0], inst->label, cwStringNullGuard(var_label) );
          goto errLabel;
        }

        if( value_0 == nullptr )
        {
          cwLogWarning("The preset variable '%s' was not found for the preset: '%s'. Falling back to single value assign.",cwStringNullGuard(var_label),cwStringNullGuard(presetLabelA[0]));

          rc = _var_channelize( inst, preset_label_1, "dual class", var_label, value_1 );
          goto errLabel;
        }
        
        if( value_1 == nullptr )
        {
          cwLogWarning("The preset variable '%s' was not found for the preset: '%s'. Falling back to single value assign.",cwStringNullGuard(var_label),cwStringNullGuard(presetLabelA[1]));
          
          rc = _var_channelize( inst, preset_label_0, "dual class", var_label, value_0 );
          goto errLabel;
        }


        if((rc = _set_var_from_dual_preset( inst, var_label, value_0, value_1, coeff )) != kOkRC )
        {
          rc = cwLogError(rc,"Multi preset application failed on variable:%s.",cwStringNullGuard(var_label));
          goto errLabel;
        }
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Apply %s multi-preset failed on instance:%s class:%s presetA:%s presetB:%s.", type_src_label, inst->label, inst->class_desc->label, preset_label_0, preset_label_1 );

      return rc;
    }


    rc_t _class_multi_preset_channelize_vars(instance_t* inst, const char** class_preset_labelA, unsigned presetN, double coeff )
    {
      rc_t            rc = kOkRC;
      const object_t* presetCfgA[ presetN ];
      const char*     presetLabelA[ presetN ];
      unsigned        presetCfgN = 0;
      
      for(unsigned i=0; i<presetN; ++i)
      {
        if( class_preset_labelA[i] != nullptr )
        {
          const preset_t* pr;
          
          // locate the requestd preset record
          if((pr = class_preset_find(inst->class_desc, class_preset_labelA[i])) == nullptr )
          {
            rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be found for the instance '%s'.", class_preset_labelA[i], inst->label);
            goto errLabel;
          }

          if( pr->cfg == nullptr )
          {
            rc = cwLogError(kInvalidIdRC,"The value of preset '%s' was empty in instance '%s'.", class_preset_labelA[i], inst->label);
            goto errLabel;            
          }

          presetCfgA[  presetCfgN] = pr->cfg;
          presetLabelA[presetCfgN] = class_preset_labelA[i];
          presetCfgN++;
        }
      }

      // dispatch based on the count of presets located
      switch( presetCfgN )
      {
        case 0:
          rc = cwLogError(kInvalidArgRC,"No valid class preset records were found while attempting apply a multi-preset.");
          break;
          
        case 1:
          // only one valid preset was located - apply it directly
          rc = _preset_channelize_vars( inst, "class", presetLabelA[0], presetCfgA[0]);
          break;
          
        default:
          // more than one preset was located - apply it's interpolated values
          rc = _multi_preset_channelize_vars( inst, "class", presetLabelA, presetCfgA, presetCfgN, coeff);
      }
      
      
    errLabel:                  
      return rc;
      
    }
    
    rc_t _class_preset_channelize_vars( instance_t* inst, const char* preset_label )
    {
      rc_t            rc = kOkRC;
      const preset_t* pr;

      if( preset_label == nullptr )
        return kOkRC;
      
      // locate the requestd preset record
      if((pr = class_preset_find(inst->class_desc, preset_label)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be found for the instance '%s'.", preset_label, inst->label);
        goto errLabel;
      }
      
      rc = _preset_channelize_vars( inst, "class", preset_label, pr->cfg);
      
    errLabel:                  
      return rc;
    }


    rc_t _class_apply_presets( instance_t* inst, const object_t* preset_labels )
    {
      rc_t        rc = kOkRC;
      const char* s  = nullptr;
      
      // if preset_labels is a string
      if( preset_labels->is_string() && preset_labels->value(s)==kOkRC )
        return _class_preset_channelize_vars(inst,s);

      // if the preset_labels is not a list
      if( !preset_labels->is_list() )
        rc = cwLogError(kSyntaxErrorRC,"The preset list on instance '%s' is neither a list nor a string.",inst->label);
      else        
      {
        // preset_labels is a list.
        
        // for each label listed in the preset label list
        for(unsigned i=0; i<preset_labels->child_count(); ++i)
        {
          const object_t* label_obj = preset_labels->child_ele(i);

          // verify that the label is a strng
          if( !label_obj->is_string() || label_obj->value(s) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"The preset list does not contain string on instance '%s'.",inst->label);
            goto errLabel;
          }

          // apply a preset label
          if((rc = _class_preset_channelize_vars( inst, s)) != kOkRC )
            goto errLabel;          
        }
      }
      
    errLabel:
      return rc;
    }
                               
                                 
    

    rc_t _inst_args_channelize_vars( instance_t* inst, const char* arg_label, const object_t* arg_cfg )
    {
      rc_t rc = kOkRC;
      
      if( arg_cfg == nullptr )
        return rc;

      return _preset_channelize_vars( inst, "instance", arg_label, arg_cfg );
      
    }

    typedef struct inst_parse_vars_str
    {
      const char*     inst_label;
      const char*     inst_clas_label;
      const object_t* in_dict;
      const char*     arg_label;
      const object_t* preset_labels;
      const object_t* arg_cfg;
    } inst_parse_vars_t;

    rc_t _parse_instance_cfg( flow_t* p, const object_t* inst_cfg, inst_parse_vars_t& pvars )
    {
      rc_t            rc       = kOkRC;
      const object_t* arg_dict = nullptr;
      
      // validate the syntax of the inst_cfg pair
      if( inst_cfg == nullptr || !inst_cfg->is_pair() || inst_cfg->pair_label()==nullptr || inst_cfg->pair_value()==nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance cfg. is not a valid pair. No instance label could be parsed.");
        goto errLabel;
      }
      
      pvars.inst_label = inst_cfg->pair_label();

      // verify that the instance label is unique
      if( instance_find(p,pvars.inst_label) != nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance label '%s' has already been used.",pvars.inst_label);
        goto errLabel;
      }
      
      // get the instance class label
      if((rc = inst_cfg->pair_value()->getv("class",pvars.inst_clas_label)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance cfg. %s is missing: 'type'.",pvars.inst_label);
        goto errLabel;        
      }
      
      // parse the optional args
      if((rc = inst_cfg->pair_value()->getv_opt("args",     arg_dict,
                                                "in",       pvars.in_dict,
                                                "argLabel", pvars.arg_label,
                                                "preset",   pvars.preset_labels)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The instance cfg. '%s' missing: 'type'.",pvars.inst_label);
        goto errLabel;        
      }

      // if an argument dict was given in the instanec cfg
      if( arg_dict != nullptr  )
      {
        bool rptErrFl = true;

        // verify the arg. dict is actually a dict.
        if( !arg_dict->is_dict() )
        {
          cwLogError(kSyntaxErrorRC,"The instance argument dictionary on instance '%s' is not a dictionary.",pvars.inst_label);
          goto errLabel;
        }
        
        // if no label was given then try 'default'
        if( pvars.arg_label == nullptr)
        {
          pvars.arg_label = "default";
          rptErrFl = false;
        }

        // locate the specified argument record
        if((pvars.arg_cfg = arg_dict->find_child(pvars.arg_label)) == nullptr )
        {

          // if an explicit arg. label was given but it was not found
          if( rptErrFl )
          {
            rc = cwLogError(kSyntaxErrorRC,"The argument cfg. '%s' was not found on instance cfg. '%s'.",pvars.arg_label,pvars.inst_label);
            goto errLabel;
          }

          // no explicit arg. label was given - make arg_dict the instance arg cfg.
          pvars.arg_cfg = arg_dict;
          pvars.arg_label = nullptr;
        }        
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(kSyntaxErrorRC,"Configuration parsing failed on instance: '%s'.", cwStringNullGuard(pvars.inst_label) );
      
      return rc;
    }
    
    rc_t _create_instance( flow_t* p, const object_t* inst_cfg )
    {
      rc_t              rc         = kOkRC;
      inst_parse_vars_t pvars      = {};
      instance_t*       inst       = nullptr;
      class_desc_t*     class_desc = nullptr;      

      // parse the instance configuration 
      if((rc = _parse_instance_cfg( p, inst_cfg, pvars )) != kOkRC )
        goto errLabel;
        
      // locate the class desc
      if(( class_desc = class_desc_find(p,pvars.inst_clas_label)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The flow class '%s' was not found.",cwStringNullGuard(pvars.inst_clas_label));
        goto errLabel;
      }

      // instantiate the instance
      inst = mem::allocZ<instance_t>();

      inst->ctx          = p;
      inst->label        = pvars.inst_label;
      inst->inst_cfg     = inst_cfg;
      inst->arg_label    = pvars.arg_label;
      inst->arg_cfg      = pvars.arg_cfg;
      inst->class_desc   = class_desc;
      
      // Instantiate all the variables in the class description
      for(var_desc_t* vd=class_desc->varDescL; vd!=nullptr; vd=vd->link)
      {
        variable_t* var = nullptr;        
        if((rc = var_create( inst, vd->label, kInvalidId, kAnyChIdx, vd->val_cfg, var )) != kOkRC )
          goto errLabel;
      }

      // All the variables that can be used by this instance have now been created
      // and the chIdx of each variable is set to 'any'.

      // If a 'preset' field was included in the instance cfg then apply the specified class preset
      if( pvars.preset_labels != nullptr )      
        if((rc = _class_apply_presets(inst, pvars.preset_labels )) != kOkRC )
          goto errLabel;

      // All the class presets values have now been set and those variables
      // that were expressed with a list have numeric channel indexes assigned.

      // Apply the instance preset values.
      if( pvars.arg_cfg != nullptr )
        if((rc = _inst_args_channelize_vars( inst, pvars.arg_label, pvars.arg_cfg )) != kOkRC )
          goto errLabel;

      // All the instance arg values have now been set and those variables
      // that were expressed with a list have numeric channel indexes assigned.


      // TODO: Should the 'all' variable be removed for variables that have numeric channel indexes?

      // connect the variable lists in the instance 'in' dictionary
      if( pvars.in_dict != nullptr )
      {
        if( !pvars.in_dict->is_dict() )
        {
          cwLogError(kSyntaxErrorRC,"The 'in' dict in instance '%s' is not a valid dictionary.",inst->label);
          goto errLabel;
        }
        
        // for each input variable in the 'in' set
        for(unsigned i=0; i<pvars.in_dict->child_count(); ++i)
        {
          const object_t*   in_pair      = pvars.in_dict->child_ele(i);
          const char*       in_var_label = in_pair->pair_label();
          const char*       src_label    = nullptr;
          const var_desc_t* vd           = nullptr;

          // locate the var desc of the associated variable
          if((vd = var_desc_find( class_desc, in_var_label)) == nullptr )
          {
            cwLogError(kSyntaxErrorRC,"The value description for the 'in' value '%s' was not found on instance '%s'. Maybe '%s' is not marked as a 'src' attribute in the class variable descripiton.",in_var_label,inst->label,in_var_label);
            goto errLabel;
          }

          // Note that all variable's found by the above call to var_desc_find() should be 'src' variables.
          //assert( cwIsFlag(vd->flags,kSrcVarFl) );

          // if this value is a 'src' value then it must be setup prior to the instance being instantiated
          //if( cwIsFlag(vd->flags,kSrcVarFl) )
          //{
            in_pair->pair_value()->value(src_label);

            // locate the pointer to the referenced output abuf and store it in inst->srcABuf[i]
            if((rc = _setup_input( p, inst, in_var_label, src_label )) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"The 'in' variable at index %i is not valid on instance '%s'.", i, inst->label );
              goto errLabel;
            }
            //}
        }
      }

      // Complete the instantiation

      // Call the custom instance create() function.
      if((rc = class_desc->members->create( inst )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Instantiation failed on instance '%s'.", inst->label );
        goto errLabel;
      }

      // Create the instance->varMap[] lookup array
      if((rc =_create_instance_var_map( inst )) != kOkRC )
        goto errLabel;

      // 
      _complete_input_connections(inst);

      // call the 'value()' function to inform the instance of the current value of all of it's variables.
      if((rc = _call_value_func_on_all_variables( inst )) != kOkRC )
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

      if( p == nullptr )
        return rc;

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

    const object_t* _find_network_preset( flow_t* p, const char* presetLabel )
    {
      const object_t* preset_value = nullptr;
      
      if( p->presetCfg != nullptr )
      {
        rc_t rc;
        
        if((rc = p->presetCfg->getv_opt( presetLabel, preset_value )) != kOkRC )
          cwLogError(rc,"Search for network preset named '%s' failed.", cwStringNullGuard(presetLabel));
      }

      return preset_value;
      
    }

    rc_t _exec_cycle( flow_t* p )
    {
      rc_t rc = kOkRC;
      
      for(instance_t* inst = p->network_head; inst!=nullptr; inst=inst->link)
      {
        if((rc = inst->class_desc->members->exec(inst)) != kOkRC )
        {          
          break;
        }
      }
      
      return rc;
    }

    rc_t _get_variable( flow_t* p, const char* inst_label, const char* var_label, unsigned chIdx, instance_t*& instPtrRef, variable_t*& varPtrRef )
    {
      rc_t        rc   = kOkRC;
      instance_t* inst = nullptr;
      variable_t* var  = nullptr;

      varPtrRef = nullptr;
      instPtrRef = nullptr;

      // locate the proc instance
      if((inst = instance_find(p,inst_label)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"Unknown proc instance label '%s'.", cwStringNullGuard(inst_label));
        goto errLabel;
      }

      // locate the variable
      if((rc = var_find( inst, var_label, chIdx, var)) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"The variable '%s' could not be found on the proc instance '%s'.",cwStringNullGuard(var_label),cwStringNullGuard(inst_label));
        goto errLabel;
      }

      instPtrRef = inst;
      varPtrRef = var;
      
    errLabel:
      return rc;
    }
    
    template< typename T >
    rc_t _set_variable_value( flow_t* p, const char* inst_label, const char* var_label, unsigned chIdx, T value )
    {
      rc_t rc = kOkRC;
      instance_t* inst = nullptr;
      variable_t* var = nullptr;

      // get the variable
      if((rc = _get_variable(p,inst_label,var_label,chIdx,inst,var)) != kOkRC )
        goto errLabel;
      
      // set the variable value
      if((rc = var_set( inst, var->vid, chIdx, value )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The variable set failed on instance:'%s' variable:'%s'.",cwStringNullGuard(inst_label),cwStringNullGuard(var_label));
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    template< typename T >
    rc_t _get_variable_value( flow_t* p, const char* inst_label, const char* var_label, unsigned chIdx, T& valueRef )
    {
      rc_t rc = kOkRC;
      instance_t* inst = nullptr;
      variable_t* var = nullptr;

      // get the variable 
      if((rc = _get_variable(p,inst_label,var_label,chIdx,inst,var)) != kOkRC )
        goto errLabel;
      
      // get the variable value
      if((rc = var_get( inst, var->vid, chIdx, valueRef )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The variable get failed on instance:'%s' variable:'%s'.",cwStringNullGuard(inst_label),cwStringNullGuard(var_label));
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    unsigned _select_ranked_ele_by_rank_prob( const preset_order_t* presetA, const bool* selV , unsigned presetN )
    {

      // get a count of the candidate presets
      unsigned rankN = selV==nullptr ? presetN : std::count_if(selV,selV+presetN,[](const bool& x){ return x; });

      if( rankN == 0 )
      {
        cwLogWarning("All preset candidates have been eliminated.");
        return kInvalidIdx;
      }

      unsigned rankV[  rankN ];
      unsigned idxMapA[ rankN ];

      // fill rankV[] with candidates 'order' value
      for(unsigned i=0,j=0; i<presetN; ++i)
        if( selV==nullptr || selV[i] )
        {
          assert( j < rankN );
          rankV[j]   = presetA[i].order;
          idxMapA[j] = i;
          ++j;
        }

      // if only one element remains to be selected
      if( rankN == 1 )
        return idxMapA[0];

      assert( rankN > 1 );
      
      unsigned threshV[ rankN ];
      unsigned uniqueRankV[ rankN ];      
      unsigned uniqueRankN = 0;
      unsigned sel_idx = rankN - 1; //

      // for each possible rank value
      for(unsigned i=0; i<rankN; ++i)
      {
        // locate the rank in the uniqueRankV[]
        unsigned j=0;
        for(; j<uniqueRankN; ++j)
          if( uniqueRankV[j]==rankV[i] )
            break;

        // if the rank was not found then include it here
        if( j == uniqueRankN )
          uniqueRankV[uniqueRankN++] = rankV[i];

      }

      // uniqueRankV[] now includes the set of possible rank values
      
      // Take the product of all possible values.
      // (this will be evenly divisible by all values)
      unsigned prod = vop::prod(uniqueRankV,uniqueRankN);

      unsigned thresh = 0;
      for(unsigned i=0; i<rankN; ++i)
        threshV[i] = (thresh += rankV[i] * prod);

      // Thresh is now set to the max possible random value.
      
      // Generate a random number between 0 and thresh
      double   fval = (double)std::rand() * thresh / RAND_MAX;

      unsigned thresh0 = 0;
      for(unsigned i=0; i<rankN; ++i)
      {
        if( thresh0 <= fval && fval < threshV[i] )
        {
          sel_idx = i;
          break;
        }
      }

      assert( sel_idx < rankN );
      
      return idxMapA[sel_idx];
    }

    /*
    unsigned _select_ranked_ele_by_rank_prob( const preset_order_t* rankV, unsigned rankN )
    {     
      unsigned threshV[ rankN ];
      unsigned uniqueRankV[ rankN ];      
      unsigned uniqueRankN = 0;
      unsigned sel_idx = rankN - 1; //

      if( rankN == 0 )
        return kInvalidIdx;

      if( rankN == 1 )
        return 0;

      // for each possible rank value
      for(unsigned i=0; i<rankN; ++i)
      {
        // locate the rank in the uniqueRankV[]
        unsigned j=0;
        for(; j<uniqueRankN; ++j)
          if( uniqueRankV[j]==rankV[i].order )
            break;

        // if the rank was not found then include it here
        if( j == uniqueRankN )
          uniqueRankV[uniqueRankN++] = rankV[i].order;

      }

      // uniqueRankV[] now includes the set of possible rank values
      
      // Take the product of all possible values.
      // (this will be evenly divisible by all values)
      unsigned prod = vop::prod(uniqueRankV,uniqueRankN);

      unsigned thresh = 0;
      for(unsigned i=0; i<rankN; ++i)
        threshV[i] = (thresh += rankV[i].order * prod);

      // Thresh is now set to the max possible random value.
      
      // Generate a random number between 0 and thresh
      double   fval = (double)std::rand() * thresh / RAND_MAX;

      unsigned thresh0 = 0;
      for(unsigned i=0; i<rankN; ++i)
      {
        if( thresh0 <= fval && fval < threshV[i] )
        {
          sel_idx = i;
          break;
        }
      }

      return sel_idx;
    }
    */
    
    const char* _select_ranked_ele_label_by_rank_prob( const preset_order_t* rankV, const bool* selA, unsigned rankN )
    {
      unsigned sel_idx;

      if((sel_idx = _select_ranked_ele_by_rank_prob( rankV, selA, rankN )) == kInvalidIdx )
      {
        cwLogWarning("The multi-preset select function failed. Selecting preset 0.");
        sel_idx = 0;
      }

      return rankV[sel_idx].preset_label;

    }
    

    double _calc_multi_preset_dual_coeff( const multi_preset_selector_t& mps )
    {
      double result = 0;
      unsigned resultN = 0;
      
      if( mps.coeffN == 0 )
      {
        result = 0.5;
      }
      else
      {  
        for(unsigned i=0; i<mps.coeffN; ++i)
        {
          /*

            Temporarily commented out because coeffV[] values
            have already been normalized.
            
          double norm_factor = (mps.coeffMaxV[i] - mps.coeffMinV[i]);
          
          if( norm_factor <= 0 )
            cwLogWarning("Invalid normalization factor in aggregated distance measurement.");
          else
            norm_factor = 1;
          
          
          result += std::max( mps.coeffMinV[i], std::min( mps.coeffMaxV[i], mps.coeffV[i] ) ) / norm_factor;
          */

          // WOULD DISTANCE BE BETTER THAN AVERAGE????
          
          if( mps.coeffV[i] != 0 )
          {
            result += mps.coeffV[i];
            resultN += 1;
          }
        }

        if( resultN <= 0 )
            cwLogWarning("Invalid normalization factor in aggregated distance measurement.");
        else
          result = std::min(1.0,std::max(0.0,result/mps.coeffN));
      }
      
      
      return result;
    }
    
    rc_t _find_network_preset_instance_pair( flow_t* p, const char* preset_label, const char* instance_label, const object_t*& preset_val_ref )
    {
      rc_t rc = kOkRC;
      const object_t* net_preset_pair = nullptr;
      
      preset_val_ref = nullptr;
  
      // locate the cfg of the requested preset
      if((net_preset_pair = _find_network_preset(p, preset_label )) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", cwStringNullGuard(preset_label) );
        goto errLabel;
      }

      // locate the instance matching 'instance_label'.
      for(unsigned i=0; i<net_preset_pair->child_count(); ++i)
      {
        const object_t* inst_pair;
        if((inst_pair = net_preset_pair->child_ele(i)) != nullptr && inst_pair->is_pair() && textIsEqual(inst_pair->pair_label(),instance_label) )
        {      

          preset_val_ref = inst_pair->pair_value();

          goto errLabel;
        }
      }
  
      rc = cwLogError(kInvalidArgRC,"The preset instance label '%s' was not found.",cwStringNullGuard(preset_label));
  
    errLabel:
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
                           const object_t&    classCfg,
                           const object_t&    networkCfg,
                           external_device_t* deviceA,
                           unsigned           deviceN )
{
  rc_t            rc               = kOkRC;
  const object_t* network          = nullptr; 
  bool            printClassDictFl = false;
  bool            printNetworkFl   = false;
  
  if(( rc = destroy(hRef)) != kOkRC )
    return rc;

  flow_t* p   = mem::allocZ<flow_t>();
  p->networkCfg = &networkCfg;   // TODO: duplicate cfg?
  p->deviceA    = deviceA;
  p->deviceN    = deviceN;

  // parse the class description array
  if((rc = _parse_class_cfg(p,&classCfg)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the class description list.");
    goto errLabel;    
  }

  // parse the main audio file processor cfg record
  if((rc = networkCfg.getv("framesPerCycle",      p->framesPerCycle,
                           "multiPriPresetProbFl", p->multiPriPresetProbFl,
                           "multiSecPresetProbFl", p->multiSecPresetProbFl,
                           "multiPresetInterpFl", p->multiPresetInterpFl,
                           "network",             network)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the required flow configuration parameters.");
    goto errLabel;
  }

  // parse the optional args
  if((rc = networkCfg.getv_opt("maxCycleCount",    p->maxCycleCount,
                               "printClassDictFl", printClassDictFl,
                               "printNetworkFl",   printNetworkFl,
                               "presets",          p->presetCfg)) != kOkRC )
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

  // build the network
  for(unsigned i=0; i<network->child_count(); ++i)
  {
    const object_t* inst_cfg = network->child_ele(i);

    // create the instance
    if( (rc= _create_instance( p, inst_cfg ) ) != kOkRC )
    {
      rc = cwLogError(rc,"The instantiation at proc index %i is invalid.",i);
      goto errLabel;
      
    }
  }

  if( printNetworkFl )
    network_print(p);

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
  return _exec_cycle(_handleToPtr(h));
}

cw::rc_t cw::flow::exec(    handle_t h )
{
  rc_t    rc = kOkRC;
  flow_t* p  = _handleToPtr(h);

  while( true )
  {  
    rc = _exec_cycle(p);

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
  rc_t    rc = kOkRC;
  flow_t* p  = _handleToPtr(h);
  const object_t* net_preset_value;
  const object_t* preset_pair;

  // locate the cfg of the requested preset
  if((net_preset_value = _find_network_preset(p, presetLabel )) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", presetLabel );
    goto errLabel;
  }

  // for each instance in the preset
  for(unsigned i=0; i<net_preset_value->child_count(); ++i)
  {
    // get the instance label/value pair
    if((preset_pair = net_preset_value->child_ele(i)) != nullptr && preset_pair->is_pair() )
    {
      const char* inst_label = preset_pair->pair_label();
      const object_t* preset_value_cfg = preset_pair->pair_value();
      instance_t* inst;

      // locate the instance
      if((inst = instance_find(p,inst_label)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"The network instance '%s' refered to in network preset '%s' could not be found.",inst_label,presetLabel);
        goto errLabel;
      }

      // if the preset value is a string then look it up in the class dictionary
      if( preset_value_cfg->is_string() )
      {
        const char* class_preset_label;
        preset_value_cfg->value(class_preset_label);
        _class_preset_channelize_vars(inst, class_preset_label );
      }
      else
      {
        // if the preset value is a dict then apply it directly
        if( preset_value_cfg->is_dict() )
        {
          if((rc =  _preset_channelize_vars( inst, "network", presetLabel, preset_value_cfg )) != kOkRC )
          {
            rc = cwLogError(rc,"The preset  '%s' application failed on instance '%s'.", presetLabel, inst_label );
            goto errLabel;
          }
          
        }
        else
        {
          rc = cwLogError(kSyntaxErrorRC,"The network preset '%s' instance '%s' does not have a string or dictionary value.", presetLabel, inst_label );
          goto errLabel;
        }
      }
    }
    else
    {
      rc = cwLogError(kSyntaxErrorRC,"The network preset '%s' is malformed.",presetLabel);
      goto errLabel;        
    }      
  }

  cwLogInfo("Activated preset:%s",presetLabel);
 errLabel:
  return rc;
}

cw::rc_t cw::flow::apply_dual_preset( handle_t h, const char* presetLabel_0, const char* presetLabel_1, double coeff )
{
  rc_t    rc = kOkRC;
  flow_t* p  = _handleToPtr(h);
  const object_t* net_preset_value_0;

  cwLogInfo("*** Applying dual: %s %s : %f",presetLabel_0, presetLabel_1, coeff );
  
  // locate the cfg of the requested preset
  if((net_preset_value_0 = _find_network_preset(p, presetLabel_0 )) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", presetLabel_0 );
    goto errLabel;
  }

  // for each instance in the preset
  for(unsigned i=0; i<net_preset_value_0->child_count(); ++i)
  {
    const object_t* preset_pair_0      = net_preset_value_0->child_ele(i);
    const char*     inst_label         = preset_pair_0->pair_label(); 
    const object_t* preset_value_cfg_0 = preset_pair_0->pair_value();
    instance_t*     inst               = nullptr;
    const object_t* preset_value_cfg_1 = nullptr;
    const int two = 2;
    const char* class_preset_labelA[two];
    
    // get the instance label/value pair
    if((preset_pair_0 = net_preset_value_0->child_ele(i)) == nullptr || !preset_pair_0->is_pair() )
    {
      rc = cwLogError(kSyntaxErrorRC,"An invalid preset value pair was encountered in '%s'.",presetLabel_0);
      goto errLabel;
    }

    // verify that the preset value is a string or dict
    if( preset_pair_0->pair_value()==nullptr || (!preset_value_cfg_0->is_dict() && !preset_value_cfg_0->is_string() ))
    {
      rc = cwLogError(kSyntaxErrorRC,"The preset value pair for instance '%s' in '%s' is not a 'dict' or 'string'.",inst_label,presetLabel_0);
      goto errLabel;
    }

    // locate the instance associated with the primary and secondary preset
    if((inst = instance_find(p,inst_label)) == nullptr )
    {
      rc = cwLogError(kInvalidIdRC,"The network instance '%s' refered to in network preset '%s' could not be found.",cwStringNullGuard(inst_label),cwStringNullGuard(presetLabel_0));
      goto errLabel;
    }
            
    // locate the second instance/preset value pair 
    if((rc = _find_network_preset_instance_pair( p, presetLabel_1, inst_label, preset_value_cfg_1 )) != kOkRC )
    {
      rc = cwLogError(kInvalidIdRC,"The second network instance '%s' refered to in network preset '%s' could not be found.",inst_label,presetLabel_1);
      goto errLabel;
    }
    
    // TODO: We require that the instance presets both be of the same type: string or dict.
    // There's no good reason for this, as string's resolve to class dict presets anyway.
    // Fix this!
    if( !(preset_value_cfg_0->is_dict() == preset_value_cfg_1->is_dict() && preset_value_cfg_0->is_string() == preset_value_cfg_1->is_string()) )
    {
      rc = cwLogError(kInvalidIdRC,"The value type (string or dict) of dual network presets must match. (%s != %s)",preset_value_cfg_0->type->label,preset_value_cfg_1->type->label);
      goto errLabel;
    }

    preset_value_cfg_0->value(class_preset_labelA[0]);
    preset_value_cfg_1->value(class_preset_labelA[1]);
    
    
    // if the preset value is a string then look it up in the class dictionary
    if( preset_value_cfg_0->is_string() )
    {
      rc = _class_multi_preset_channelize_vars(inst, class_preset_labelA, two, coeff );        
    }
    else
    {
      assert( preset_value_cfg_1->is_dict() );
        
      const object_t* preset_value_cfgA[] = { preset_value_cfg_0, preset_value_cfg_1};
                  
      if((rc =  _multi_preset_channelize_vars( inst, "network", class_preset_labelA, preset_value_cfgA, two, coeff )) != kOkRC )
      {
        rc = cwLogError(rc,"The dual preset  '%s':'%s' application failed on instance '%s'.", cwStringNullGuard(class_preset_labelA[0]), cwStringNullGuard(class_preset_labelA[1]), inst_label );
        goto errLabel;
      }
    }
  }

  
 errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"The dual preset  '%s':'%s' application failed.", cwStringNullGuard(presetLabel_0), cwStringNullGuard(presetLabel_1) );

  return rc;
}

cw::rc_t cw::flow::apply_preset( handle_t h, const multi_preset_selector_t& mps )
{
  rc_t        rc        = kOkRC;
  const char* label0    = nullptr;
  const char* label1    = nullptr;
  bool        priProbFl = cwIsFlag(mps.flags, kPriPresetProbFl );
  bool        secProbFl = cwIsFlag(mps.flags, kSecPresetProbFl );
  bool        interpFl  = cwIsFlag(mps.flags, kInterpPresetFl );

  //printf("preset flags: pri:%i sec:%i interp:%i\n",priProbFl,secProbFl,interpFl);
  
 // verify that the set of candidate presets is not empty
  if( mps.presetN == 0 )
  {
    cwLogError(kInvalidArgRC,"A multi-preset application was requested but no presets were provided.");
    goto errLabel;    
  }

  // if only a single candidate preset exists or needs to be selected
  if( interpFl==false || mps.presetN==1 )
  {
    // if only a single candidate preset is available or pri. probablity is not enabled 
    if( mps.presetN == 1 || priProbFl==false )
      label0 = mps.presetA[0].preset_label;
    else
    {
      if( priProbFl )
        label0 = _select_ranked_ele_label_by_rank_prob( mps.presetA, nullptr, mps.presetN );
      else
        label0 = mps.presetA[0].preset_label;
    }
  }
  else  // interpolation has been selected and at least 2 presets exist
  {    
    unsigned pri_sel_idx = 0;
        
    // select the primary preset
    if( priProbFl )
      pri_sel_idx = _select_ranked_ele_by_rank_prob( mps.presetA, nullptr, mps.presetN );
    else
    {
      // select all presets assigned to order == 1
      bool selA[ mps.presetN ];
      for(unsigned i=0; i<mps.presetN; ++i)
        selA[i]= mps.presetA[i].order==1;

      // select the preset among all presets marked as 1
      pri_sel_idx = _select_ranked_ele_by_rank_prob( mps.presetA, selA, mps.presetN );
    }

    if( pri_sel_idx == kInvalidIdx )
      pri_sel_idx    = 0;
    
    // the primary preset has now been selected

    // if there is only one candidate secondary preset
    if( mps.presetN == 2)
    {
      assert( pri_sel_idx <= 1 );
      label1  = mps.presetA[ pri_sel_idx == 0 ? 1 : 0 ].preset_label;
    }
    else                        // at least two remaining presets exist to select between
    {
      // mark the selected primary preset as not-available
      bool selA[ mps.presetN ];
      vop::fill(selA,mps.presetN,true);
      selA[pri_sel_idx] = false;

      // if the second preset should be selected probabilistically
      if( secProbFl )
        label1 = _select_ranked_ele_label_by_rank_prob( mps.presetA, selA, mps.presetN );
      else 
      {
        // select the best preset that is not the primary preset
        for(unsigned i=0; i<mps.presetN; ++i)
          if( i != pri_sel_idx )
          {
            label1 = mps.presetA[i].preset_label;
            break;
          }
        
      }
    }
    
    assert( pri_sel_idx != kInvalidIdx );
    label0               = mps.presetA[ pri_sel_idx ].preset_label;
  }
    
  assert(label0 != nullptr );
  
  if( label1 == nullptr )
  {
    rc = apply_preset( h, label0 );
  }
  else
  {
    double coeff = _calc_multi_preset_dual_coeff(mps);
    rc = apply_dual_preset( h, label0, label1, coeff );
  }
  

errLabel:
  return rc;
}


/*
cw::rc_t cw::flow::apply_preset( handle_t h, const multi_preset_selector_t& multi_preset_sel )
{
  rc_t        rc                   = kOkRC;
  const char* label0               = nullptr;
  const char* label1               = nullptr;
  const char* prob_label           = nullptr;
  bool        multiPriPresetProbFl = cwIsFlag(multi_preset_sel.flags, kPriPresetProbFl );
  bool        multiSecPresetProbFl = cwIsFlag(multi_preset_sel.flags, kSecPresetProbFl );
  bool        multiPresetInterpFl  = cwIsFlag(multi_preset_sel.flags, kInterpPresetFl );

  // verify that the set of presets to select from is not empty
  if( multi_preset_sel.presetN == 0 )
  {
    cwLogError(kInvalidArgRC,"A multi-preset application was requested but no presets were provided.");
    goto errLabel;    
  }

  // if probabistic selection was requested and is possible
  if( multiPresetProbFl && multi_preset_sel.presetN > 1 )
  {    
    auto presetA  = multi_preset_sel.presetA;
    auto presetN = multi_preset_sel.presetN;

    // if we are interpolating then the base preset is always the first one in presetA[]
    // so do not include it as a candidate for probabilistic selection
    if( multiPresetInterpFl  )
    {
      presetA += 1;
      presetN -= 1;
      
      // if only one preset remains in the list then prob. selection is not possible
      if( presetN == 1 )
        prob_label = presetA[0].preset_label;
    }

    // select a preset based using the ranked-prob. algorithm.
    if( prob_label == nullptr )
    {
      unsigned prob_sel_idx;
      
      if((prob_sel_idx = _select_ranked_ele_by_rank_prob( presetA, presetN )) == kInvalidIdx )
        rc = cwLogWarning("The multi-preset select function failed. Selecting preset 0.");
      else
      {
        prob_label = presetA[prob_sel_idx].preset_label;
        
        cwLogInfo("Multi-preset prob. select:%s :  %i from %i",
                  cwStringNullGuard(prob_label),
                  prob_sel_idx,
                  multi_preset_sel.presetN );
        
      }
    }
  }

  // prob_label now holds a probablistically selected preset label
  // or null if prob. sel. was not requested or failed
  
  switch( multi_preset_sel.presetN )
  {
    case 0:
      assert(0); // we avoided this case at the top of the function
      break;
      
    case 1:
      // if there is only one preset to select from 
      label0 = multi_preset_sel.presetA[0].preset_label;
      break;

    default:
      // There are at least two presets ...
      // ... and prob. select was not requested or failed
      if( prob_label  == nullptr )
      {
        label0 = multi_preset_sel.presetA[0].preset_label;
        label1 = multiPresetInterpFl ? multi_preset_sel.presetA[1].preset_label : nullptr;
      }
      else // ... and a prob. selection exists
      {
        // if we need two presets 
        if( multiPresetInterpFl )
        {
          label0 = multi_preset_sel.presetA[0].preset_label; 
          label1 = prob_label;
        }          
        else // otherwise we need only one
        {
          label0 = prob_label;
          label1 = nullptr;
        }        
      }      
  }
  
  if( label0 == nullptr )
  {
    rc = cwLogError(kInvalidStateRC,"The selected multi-preset label is empty.");
    goto errLabel;
  }

  if( label1 == nullptr )
  {
    rc = apply_preset( h, label0 );
  }
  else
  {
    double coeff = _calc_multi_preset_dual_coeff(multi_preset_sel);
    rc = apply_dual_preset( h, label0, label1, coeff );
  }
  
errLabel:  
  return rc;
}
*/

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool value )
{ return _set_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int value )
{ return _set_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value )
{ return _set_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float value )
{ return _set_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double value )
{ return _set_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool& valueRef )
{ return _get_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int& valueRef )
{ return _get_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned& valueRef )
{ return _get_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float& valueRef )
{ return _get_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double& valueRef )
{ return _get_variable_value( _handleToPtr(h), inst_label, var_label, chIdx, valueRef ); }



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
  
  network_print(p);
}


cw::rc_t cw::flow::test(  const object_t* cfg )
{
  rc_t rc = kOkRC;
  handle_t flowH;

  object_t* class_cfg = nullptr;
  const char* flow_proc_fname;
  
  if((rc = cfg->getv("flow_proc_fname",flow_proc_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"The name of the flow_proc_dict file could not be parsed.");
    goto errLabel;
  }

  if((rc = objectFromFile(flow_proc_fname,class_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The flow proc dict could not be read from '%s'.",cwStringNullGuard(flow_proc_fname));
    goto errLabel;
  }

  // create the flow object
  if((rc = create( flowH, *class_cfg, *cfg)) != kOkRC )
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



