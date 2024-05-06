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
#include "cwDspTypes.h" // real_t, sample_t
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwFlowDecl.h"
#include "cwFlowTypes.h"
#include "cwFlowNet.h"
#include "cwFlowProc.h"

namespace cw
{
  namespace flow
  {


    rc_t _network_destroy( network_t& net )
    {
      rc_t rc = kOkRC;

      for(unsigned i=0; i<net.proc_arrayN; ++i)
        proc_destroy(net.proc_array[i]);

      mem::release(net.proc_array);
      net.proc_arrayAllocN = 0;
      net.proc_arrayN = 0;
      
      return rc;
    }

    rc_t  _var_map_id_to_index(  proc_t* proc, unsigned vid, unsigned chIdx, unsigned& idxRef );

    rc_t _create_proc_var_map( proc_t* proc )
    {
      rc_t        rc        = kOkRC;
      unsigned    max_vid   = kInvalidId;
      unsigned    max_chIdx = 0;
      variable_t* var       = proc->varL;
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
        proc->varMapChN = max_chIdx + 1;
        proc->varMapIdN = max_vid + 1;
        proc->varMapN   = proc->varMapIdN * proc->varMapChN;
        proc->varMapA   = mem::allocZ<variable_t*>( proc->varMapN );

        // assign each variable to a location in the map
        for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
          if( var->vid != kInvalidId )
          {
            unsigned idx = kInvalidIdx;

            if((rc = _var_map_id_to_index( proc, var->vid, var->chIdx, idx )) != kOkRC )
              goto errLabel;

          
            // verify that there are not multiple variables per map position          
            if( proc->varMapA[ idx ] != nullptr )
            {
              variable_t* v0 = proc->varMapA[idx];
              rc = cwLogError(kInvalidStateRC,"The variable '%s' id:%i ch:%i and '%s' id:%i ch:%i share the same variable map position on proc instance: %s. This is usually cased by duplicate variable id's.",
                              v0->label,v0->vid,v0->chIdx, var->label,var->vid,var->chIdx,proc->label);

              goto errLabel;
            }

            // assign this variable to a map position
            proc->varMapA[ idx ] = var;

            if( var->chIdx != kAnyChIdx && var->value == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The value of the variable '%s' ch:%i on proc instance:'%s' has not been set.",var->label,var->chIdx,proc->label);
              goto errLabel;
            }

          }
        
      }

    errLabel:
      return rc;
      
    }

    /*
    void _complete_input_connections( proc_t* proc )
    {
      for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
        if(var->chIdx == kAnyChIdx && is_connected_to_source_proc(var) )
        {

          variable_t* base_src_var = var->src_var;

          // since 'var' is on the 'any' channel the 'src' var must also be on the 'any' channel
          assert( base_src_var->chIdx == kAnyChIdx );
          
          //printf("%s %s\n",proc->label,var->label);
          
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
    */
    
    rc_t _set_log_flags(proc_t* proc, const object_t* log_labels)
    {
      rc_t rc = kOkRC;
      
      if( log_labels == nullptr )
        return rc;

      if( !log_labels->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The log spec on '%s:%i' is not a dictionary.",cwStringNullGuard(proc->label),proc->label_sfx_id);
        goto errLabel;
      }

      for(unsigned i=0; i<log_labels->child_count(); ++i)
      {
        const object_t* pair;
        unsigned sfx_id;
        
        if((pair = log_labels->child_ele(i)) == nullptr || pair->pair_label()==nullptr || pair->pair_value()==nullptr || (rc=pair->pair_value()->value(sfx_id))!=kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"Syntax error on log var identifier.");
          goto errLabel;
        }

        if((rc = var_set_flags( proc, kAnyChIdx, pair->pair_label(), sfx_id, kLogVarFl )) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to set var flags on '%s:%i' var:'%s:%i'.",cwStringNullGuard(proc->label),proc->label_sfx_id,pair->pair_label(),sfx_id);
          goto errLabel;          
        }
      }

    errLabel:
      return rc;
    }

    rc_t _call_value_func_on_all_variables( proc_t* proc )
    {
      rc_t rc  = kOkRC;
      rc_t rc1 = kOkRC;
      
      for(unsigned i=0; i<proc->varMapN; ++i)
        if( proc->varMapA[i] != nullptr && proc->varMapA[i]->vid != kInvalidId )
        {
          variable_t* var = proc->varMapA[i];

          if((rc = var_call_custom_value_func( var )) != kOkRC )
            rc1 = cwLogError(rc,"The proc proc instance '%s:%i' reported an invalid valid on variable:%s chIdx:%i.", var->proc->label, var->proc->label_sfx_id, var->label, var->chIdx );
        }
      
      return rc1;
    }

    rc_t _var_channelize( proc_t* proc, const char* preset_label,  const char* type_src_label, const char* var_label, const object_t* value )
    {
      rc_t rc = kOkRC;
      
      variable_t* dummy = nullptr;
      var_desc_t* vd    = nullptr;

      // verify that a valid value exists
      if( value == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"Unexpected missig value on %s preset '%s' proc instance '%s' variable '%s'.", type_src_label, preset_label, proc->label, cwStringNullGuard(var_label) );
        goto errLabel;
      }
      else
      {
        bool is_var_cfg_type_fl = (vd = var_desc_find( proc->class_desc, var_label ))!=nullptr && cwIsFlag(vd->type,kCfgTFl);
        bool is_list_fl         = value->is_list();
        bool is_list_of_list_fl = is_list_fl && value->child_count() > 0 && value->child_ele(0)->is_list();
        bool parse_list_fl      = (is_list_fl && !is_var_cfg_type_fl) || (is_list_of_list_fl && is_var_cfg_type_fl);

        // if a list of values was given and the var type is not a 'cfg' type or if a list of lists was given
        if( parse_list_fl )
        {
          // then each value in the list is assigned to the associated channel
          for(unsigned chIdx=0; chIdx<value->child_count(); ++chIdx)
            if((rc = var_channelize( proc, var_label, kBaseSfxId, chIdx, value->child_ele(chIdx), kInvalidId, dummy )) != kOkRC )
              goto errLabel;
        }
        else // otherwise a single value was given
        {          
          if((rc = var_channelize( proc, var_label, kBaseSfxId, kAnyChIdx, value, kInvalidId, dummy )) != kOkRC )
            goto errLabel;
        }
      }
        
    errLabel:
      return rc;
    }
    
    rc_t _preset_channelize_vars( proc_t* proc, const char* type_src_label, const char* preset_label, const object_t* preset_cfg )
    {
      rc_t rc = kOkRC;

      //cwLogInfo("Channelizing '%s' preset %i vars for '%s'.",type_src_label, preset_cfg==nullptr ? 0 : preset_cfg->child_count(), proc->label );
      
      // validate the syntax of the preset record
      if( !preset_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The preset record '%s' on %s '%s' is not a dictionary.", preset_label, type_src_label, proc->class_desc->label );
        goto errLabel;
      }


      // for each preset variable
      for(unsigned i=0; i<preset_cfg->child_count(); ++i)
      {
        const object_t* value     = preset_cfg->child_ele(i)->pair_value();
        const char*     var_label = preset_cfg->child_ele(i)->pair_label();

        //cwLogInfo("variable:%s",value_label);
        
        if((rc = _var_channelize( proc, preset_label, type_src_label, var_label, value )) != kOkRC )
          goto errLabel;
        
        
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Apply %s preset failed on proc instance:%s class:%s preset:%s.", type_src_label, proc->label, proc->class_desc->label, preset_label );

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

    rc_t _set_var_from_dual_preset_scalar_scalar( proc_t* proc, const char* var_label, const object_t* scalar_0, const object_t* scalar_1, double coeff, unsigned chIdx )
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

      printf("%s:%s :",proc->label,var_label);
      
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

      
      if((rc = var_channelize( proc, var_label, kBaseSfxId, chIdx, &interped_value, kInvalidId, dummy )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Dual value preset application failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _set_var_from_dual_preset_list_list( proc_t* proc, const char* var_label, const object_t* list_0, const object_t* list_1, double coeff )
    {
      rc_t rc = kOkRC;
      
      if( list_0->child_count() != list_1->child_count() )
        return cwLogError(kInvalidArgRC,"If two lists are to be applied as a dual preset they must be the same length.");

      for(unsigned chIdx=0; chIdx<list_0->child_count(); ++chIdx)
        if((rc = _set_var_from_dual_preset_scalar_scalar(proc,var_label,list_0->child_ele(chIdx),list_1->child_ele(chIdx),coeff,chIdx)) != kOkRC )
          goto errLabel;

    errLabel:
      return rc;
    }

    rc_t _set_var_from_dual_preset_scalar_list( proc_t* proc, const char* var_label, const object_t* scalar, const object_t* list, double coeff )
    {
      rc_t rc = kOkRC;
      for(unsigned chIdx=0; chIdx<list->child_count(); ++chIdx)
        if((rc = _set_var_from_dual_preset_scalar_scalar(proc,var_label,scalar,list->child_ele(chIdx),coeff,chIdx)) != kOkRC )
          goto errLabel;
      
    errLabel:
      return rc;
    }

    rc_t _set_var_from_dual_preset_list_scalar( proc_t* proc, const char* var_label, const object_t* list, const object_t* scalar, double coeff )
    {
      rc_t rc = kOkRC;
      for(unsigned chIdx=0; chIdx<list->child_count(); ++chIdx)
        if((rc = _set_var_from_dual_preset_scalar_scalar(proc,var_label,list->child_ele(chIdx),scalar,coeff,chIdx)) != kOkRC )
          goto errLabel;
      
    errLabel:
      return rc;
    }
    
    rc_t _set_var_from_dual_preset_scalar_scalar( proc_t* proc, const char* var_label, const object_t* scalar_0, const object_t* scalar_1, double coeff )
    {
      return _set_var_from_dual_preset_scalar_scalar(proc,var_label,scalar_0,scalar_1,coeff,kAnyChIdx);
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
    
    rc_t _set_var_from_dual_preset( proc_t* proc, const char* var_label, const object_t* value_0, const object_t* value_1, double coeff )
    {
      rc_t rc = kOkRC;

      // dual values must be either numeric scalars or lists
      if((rc = _is_legal_dual_value(value_0)) != kOkRC || (rc = _is_legal_dual_value(value_1)) != kOkRC)
         goto errLabel;
              
      
      // if both values are lists then they must be the same length
      if( value_0->is_list() && value_1->is_list() )
      {
        rc = _set_var_from_dual_preset_list_list( proc, var_label, value_0, value_1, coeff );
        goto errLabel;
      }
      else
      {
        // if value_0 is a list and value_1 is a scalar
        if( value_0->is_list() )
        {
          rc = _set_var_from_dual_preset_list_scalar( proc, var_label, value_0, value_1, coeff );
          goto errLabel;
        }
        else
        {
          // if value_1 is a list and value_0 is a scalar
          if( value_1->is_list() )
          {
            rc = _set_var_from_dual_preset_scalar_list( proc, var_label, value_0, value_1, coeff );
            goto errLabel;
          }
          else // both values are scalars
          {
            rc = _set_var_from_dual_preset_scalar_scalar( proc, var_label, value_0, value_1, coeff );
            goto errLabel;
          }
        }
      }

    errLabel:
      return rc;
    }
    
    rc_t _multi_preset_channelize_vars( proc_t* proc, const char* type_src_label, const char** presetLabelA, const object_t** preset_cfgA, unsigned presetN, double coeff )
    {
      rc_t rc = kOkRC;

      const char* preset_label_0 = "<None>";
      const char* preset_label_1 = "<None>";

      //cwLogInfo("Channelizing '%s' preset %i vars for '%s'.",type_src_label, preset_cfg==nullptr ? 0 : preset_cfg->child_count(), proc->label );

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
          rc = cwLogError(kSyntaxErrorRC,"The preset record '%s' on %s '%s' is not a dictionary.", presetLabelA[i], type_src_label, proc->class_desc->label );
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
          rc = cwLogError(kSyntaxErrorRC,"Unexpected missig values on %s preset '%s' proc instance '%s' variable '%s'.", type_src_label, presetLabelA[0], proc->label, cwStringNullGuard(var_label) );
          goto errLabel;
        }

        if( value_0 == nullptr )
        {
          cwLogWarning("The preset variable '%s' was not found for the preset: '%s'. Falling back to single value assign.",cwStringNullGuard(var_label),cwStringNullGuard(presetLabelA[0]));

          rc = _var_channelize( proc, preset_label_1, "dual class", var_label, value_1 );
          goto errLabel;
        }
        
        if( value_1 == nullptr )
        {
          cwLogWarning("The preset variable '%s' was not found for the preset: '%s'. Falling back to single value assign.",cwStringNullGuard(var_label),cwStringNullGuard(presetLabelA[1]));
          
          rc = _var_channelize( proc, preset_label_0, "dual class", var_label, value_0 );
          goto errLabel;
        }


        if((rc = _set_var_from_dual_preset( proc, var_label, value_0, value_1, coeff )) != kOkRC )
        {
          rc = cwLogError(rc,"Multi preset application failed on variable:%s.",cwStringNullGuard(var_label));
          goto errLabel;
        }
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Apply %s multi-preset failed on proc instance:%s class:%s presetA:%s presetB:%s.", type_src_label, proc->label, proc->class_desc->label, preset_label_0, preset_label_1 );

      return rc;
    }


    rc_t _class_multi_preset_channelize_vars(proc_t* proc, const char** class_preset_labelA, unsigned presetN, double coeff )
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
          if((pr = class_preset_find(proc->class_desc, class_preset_labelA[i])) == nullptr )
          {
            rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be found for the proc instance '%s'.", class_preset_labelA[i], proc->label);
            goto errLabel;
          }

          if( pr->cfg == nullptr )
          {
            rc = cwLogError(kInvalidIdRC,"The value of preset '%s' was empty in proc instance '%s'.", class_preset_labelA[i], proc->label);
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
          rc = _preset_channelize_vars( proc, "class", presetLabelA[0], presetCfgA[0]);
          break;
          
        default:
          // more than one preset was located - apply it's interpolated values
          rc = _multi_preset_channelize_vars( proc, "class", presetLabelA, presetCfgA, presetCfgN, coeff);
      }
      
      
    errLabel:                  
      return rc;
      
    }
    
    rc_t _class_preset_channelize_vars( proc_t* proc, const char* preset_label )
    {
      rc_t            rc = kOkRC;
      const preset_t* pr;

      if( preset_label == nullptr )
        return kOkRC;
      
      // locate the requestd preset record
      if((pr = class_preset_find(proc->class_desc, preset_label)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be found for the proc instance '%s'.", preset_label, proc->label);
        goto errLabel;
      }
      
      rc = _preset_channelize_vars( proc, "class", preset_label, pr->cfg);
      
    errLabel:                  
      return rc;
    }


    rc_t _class_apply_presets( proc_t* proc, const object_t* preset_labels )
    {
      rc_t        rc = kOkRC;
      const char* s  = nullptr;
      
      // if preset_labels is a string
      if( preset_labels->is_string() && preset_labels->value(s)==kOkRC )
        return _class_preset_channelize_vars(proc,s);

      // if the preset_labels is not a list
      if( !preset_labels->is_list() )
        rc = cwLogError(kSyntaxErrorRC,"The preset list on proc instance '%s' is neither a list nor a string.",proc->label);
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
            rc = cwLogError(kSyntaxErrorRC,"The preset list does not contain string on proc instance '%s'.",proc->label);
            goto errLabel;
          }

          // apply a preset label
          if((rc = _class_preset_channelize_vars( proc, s)) != kOkRC )
            goto errLabel;          
        }
      }
      
    errLabel:
      return rc;
    }
                               
                                 
    

    rc_t _proc_inst_args_channelize_vars( proc_t* proc, const char* arg_label, const object_t* arg_cfg )
    {
      rc_t rc = kOkRC;
      
      if( arg_cfg == nullptr )
        return rc;

      return _preset_channelize_vars( proc, "proc instance", arg_label, arg_cfg );
      
    }


    //=======================================================================================================
    //
    // network creation
    //
    
    enum {
      kInVarTypeId   = 0x01,
      kSrcProcTypeId = 0x02,
      kSrcVarTypeId  = 0x04
    };
    
    typedef struct in_ele_str
    {
      unsigned typeId;          // See k???_InFl above
      char*    label;           // label of in or src id
      unsigned base_sfx_id;     // literal base_sfx_id or kInvalidId if the base_sfx_id was not given or 'is_iter_fl' is false
      unsigned sfx_id_count;    // literal sfx_id_count or kInvalidCnt if not given
      unsigned is_iter_fl;      // this id included an '_'
    } in_ele_t;
    
    typedef struct in_stmt_str
    {
      in_ele_t    in_var_ele;       // in-var element
      char*       src_net_label;    // src-net label  (null=in-var net, '_'=root net, string=named net)
      network_t*  src_net;          // network containing the src-proc
      in_ele_t    src_proc_ele;     // src-proc element
      in_ele_t    src_var_ele;      // src-var element
      var_desc_t* in_var_desc;      // Pointer to the in-var var_desc.
      bool        create_in_fl;     // True if the in_var needs to be created with an sfx_id, false create the var by the default process (w/o sfx_id)
      in_ele_t*   iter_cnt_ctl_ele; // Pointer to the ele which is controlling the iteration count (or null if in-var is non-iterating)
      unsigned    iter_cnt;         // Count of iterations or 0 if in-var is non-iterating.
    } in_stmt_t;

    typedef struct proc_inst_parse_statestr
    {
      const char*     proc_label;       // 
      const char*     proc_clas_label;  //
      const char*     arg_label;        //
      const object_t* preset_labels;    //
      const object_t* arg_cfg;          //
      const object_t* log_labels;       // 
      const object_t* in_dict;          // cfg. node to the in-list
      in_stmt_t*      in_array;         // in_array[ in_arrayN ] in-stmt array
      unsigned        in_arrayN;        // count of in-stmt's in the in-list.
    } proc_inst_parse_state_t;

    bool _is_non_null_pair( const object_t* cfg )
    { return cfg != nullptr && cfg->is_pair() && cfg->pair_label()!=nullptr && cfg->pair_value()!=nullptr; }

    
    rc_t _parse_in_ele( const char* id_str, in_ele_t& r  )
    {
      rc_t rc = kOkRC;
      unsigned bufN;

      r.base_sfx_id  = kInvalidId;
      r.sfx_id_count = kInvalidCnt;
      
      if((bufN = textLength(id_str)) == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"A blank connection id string was encountered.");
        goto errLabel;
      }
      else
      {
        char* underscore = nullptr;
        char* digit      = nullptr;
        char  buf[ bufN+1 ];

        // copy the id string into a non-const scratch buffer 
        textCopy(buf,bufN+1,id_str);

        // locate the last underscore
        if((underscore = lastMatchChar(buf,'_')) != nullptr )
        {
          *underscore  = 0;   // terminate the string prior to the underscore
          
          for(digit  = underscore + 1; *digit; digit++)
            if( !isdigit(*digit) )
              break;

          // if the underscore was followed by a number
          // or if the underscore was the last char
          // in the string - then digit will point to
          // the terminating zero - otherwise the
          // underscore did not indicate an iterating id
          if( *digit != 0 )
          {
            *underscore = '_';  // replace the underscore - its part of the label
            underscore = nullptr;
          }
          else
          {
            r.is_iter_fl = true;

            // if there is a number following the underscore then this is the secInt
            if( textLength(underscore + 1) )
            {
              // a literal iteration count was given - parse it into an integer
              if((rc = string_to_number(underscore + 1,r.sfx_id_count)) != kOkRC )
              {
                rc = cwLogError(rc,"Unable to parse the secondary integer in the connection label '%s'.",cwStringNullGuard(id_str));
                goto errLabel;
              }
            }              
          }
        }

        // verify that some content remains in the id string
        if( textLength(buf) == 0 )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unable to parse the connection id string '%s'.",cwStringNullGuard(id_str));
          goto errLabel;
        }

        // go backward from the last char until the begin-of-string or a non-digit is found
        for(digit=buf + textLength(buf)-1; digit>buf; --digit)
          if(!isdigit(*digit) )
          {
            ++digit; // advance to the first digit in the number
            break;
          }

        // if a digit was found then this is the 'priInt'
        if( textLength(digit) )
        {
          assert( buf <= digit-1 && digit-1 <= buf + bufN );
          
          // a literal base-sfx-id was given - parse it into an integer
          if((rc = string_to_number(digit,r.base_sfx_id)) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to parse the primary integer in the connection label '%s'.",cwStringNullGuard(id_str));
            goto errLabel;            
          }

          *digit = 0; // zero terminate the label

        }

        // verify that some content remains in the id string
        if( textLength(buf) == 0 )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unexpected invalid connection id string '%s'.",cwStringNullGuard(id_str));
          goto errLabel;

        }
        else
        {
          // store the label
          r.label = mem::duplStr(buf);
        }
      }
      

    errLabel:
      return rc;
    }

    rc_t _calc_src_proc_ele_count(network_t& net, in_ele_t& src_proc_ele, unsigned& cnt_ref)
    {
      rc_t rc = kOkRC;
      cnt_ref = 0;
      
      // if a literal proc sfx_id was given then use it otherwise use the default base-sfx-id (0)
      unsigned sfx_id = src_proc_ele.base_sfx_id==kInvalidCnt ? kBaseSfxId : src_proc_ele.base_sfx_id;
      unsigned n;
      for(n=0; proc_find(net, src_proc_ele.label, sfx_id ) != nullptr; ++n )
        sfx_id += 1;

      if( n == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"The src-proc '%s:%i' was not found.",cwStringNullGuard(src_proc_ele.label),sfx_id);
        goto errLabel;        
      }

      cnt_ref = n;
    errLabel:
      return rc;
    }
    
    rc_t _calc_src_var_ele_count(network_t& net, const in_ele_t& src_proc_ele, const in_ele_t& src_var_ele, unsigned& cnt_ref)
    {
      rc_t              rc          = kOkRC;
      proc_t*       src_proc    = nullptr;
      unsigned          proc_sfx_id = src_proc_ele.base_sfx_id==kInvalidCnt ? kBaseSfxId : src_proc_ele.base_sfx_id;

      cnt_ref = 0;

      // locate the parent proc of this var
      if((src_proc = proc_find(net,src_proc_ele.label,proc_sfx_id)) == nullptr )
      {
        cwLogError(kSyntaxErrorRC,"The src-proc proc instance '%s:%i' could not be found.",cwStringNullGuard(src_proc_ele.label),proc_sfx_id);
        goto errLabel;
      }
      else
      {
        // if a starting var sfx_id was given by the id then use it otherwise use the default base-sfx-id (0)
        unsigned sfx_id = src_var_ele.base_sfx_id==kInvalidCnt ? kBaseSfxId : src_var_ele.base_sfx_id;
        unsigned n;
        for(n=0; var_exists(src_proc,src_var_ele.label, sfx_id, kAnyChIdx ); ++n )
          sfx_id += 1;


        if( n == 0 )
        {
          cwLogError(kSyntaxErrorRC,"The src-var '%s:%i' was not found.",cwStringNullGuard(src_var_ele.label),sfx_id);
          goto errLabel;
        }

        cnt_ref = n;

      }

    errLabel:
      return rc;
    }

    // If the in-var is iterating then the count of iterations must be controlled by exactly one
    // of the 3 parts of the in-stmt: in-var,src_proc, or src_var.  This function determines
    // which element is used to determine the iteration count.
    rc_t _determine_in_stmt_iter_count_ctl_ele(network_t& net, proc_t* proc, in_stmt_t& in_stmt )
    {
      assert( in_stmt.in_var_ele.is_iter_fl );
      rc_t rc = kOkRC;

      in_ele_t* iter_cnt_ctl_ele = nullptr;

      // if the in-var gives a literal count - then it determines the count
      if( in_stmt.in_var_ele.sfx_id_count != kInvalidCnt )
      {
        // if the in-var gives a literal count then the src-proc cannot give one
        if( in_stmt.src_proc_ele.sfx_id_count != kInvalidCnt )
        {
          rc = cwLogError(kSyntaxErrorRC,"The in-var provided a literal iteration count therefore the src-proc cannot.");
          goto errLabel;
        }
          
        // if the in-var gives a literal count then the src-var cannot give one
        if( in_stmt.src_var_ele.sfx_id_count != kInvalidCnt )
        {
          rc = cwLogError(kSyntaxErrorRC,"The in-var provided a literal iteration count therefore the src-var cannot.");
          goto errLabel;
        }

        iter_cnt_ctl_ele = &in_stmt.in_var_ele;
          
      }
      else // the src-proc or src-var must control the iter count
      {
        // if the src-proc gives a literal count - then it determines th count
        if( in_stmt.src_proc_ele.sfx_id_count != kInvalidCnt )
        {
          // then the src-var cannot give a literal count
          if( in_stmt.src_var_ele.sfx_id_count != kInvalidCnt )
          {
            rc = cwLogError(kSyntaxErrorRC,"The src-proc provided a literal iteration count therefore the src-var cannot.");
            goto errLabel;
          }

          iter_cnt_ctl_ele = &in_stmt.src_proc_ele;
            
        }
        else
        {
          // if the src-var gives a literal count - then it determines the count
          if( in_stmt.src_var_ele.sfx_id_count != kInvalidCnt )
          {
            iter_cnt_ctl_ele = &in_stmt.src_var_ele;
          }
          else // no literal count was given - we need to get the implied count
          {
            // if the src-proc is iterating then it will provide the count
            if( in_stmt.src_proc_ele.is_iter_fl )
            {
              // the src-var cannot be iterating if the src-proc is iterating
              if( in_stmt.src_var_ele.is_iter_fl )
              {
                rc = cwLogError(kSyntaxErrorRC,"The src-proc is iterating therefore the src-var cannot.");
                goto errLabel;
              }
                
              iter_cnt_ctl_ele = &in_stmt.src_proc_ele;
            }
            else // the src-proc isn't iterating check the src-var
            {
              if( in_stmt.src_var_ele.is_iter_fl )
              {
                iter_cnt_ctl_ele = &in_stmt.src_var_ele;
              }
              else // no iteration count control was found
              {
                rc = cwLogError(kSyntaxErrorRC,"No iteration count control was specified.");
                goto errLabel;                  
              }
            }
          }
        }
      }               
      
    errLabel:
      
      if( rc == kOkRC )
        in_stmt.iter_cnt_ctl_ele = iter_cnt_ctl_ele;
      
      return rc;
    }
    
    rc_t _determine_in_stmt_iter_count( network_t& net,proc_t* proc, in_stmt_t& in_stmt )
    {
      rc_t rc = kOkRC;

      // it has already been determined that this an iterating in-stmt
      // and a iteration count control element has been identified.
      assert( in_stmt.in_var_ele.is_iter_fl );
      assert( in_stmt.iter_cnt_ctl_ele != nullptr );

      switch( in_stmt.iter_cnt_ctl_ele->typeId )
      {
        case kInVarTypeId:
            
          assert( in_stmt.iter_cnt_ctl_ele->sfx_id_count != kInvalidCnt );
            
          if((in_stmt.iter_cnt = in_stmt.iter_cnt_ctl_ele->sfx_id_count) == 0 )
            rc = cwLogError(rc,"The literal in-var iteration count on '%s:%i' must be greater than zero.", cwStringNullGuard(in_stmt.iter_cnt_ctl_ele->label),in_stmt.iter_cnt_ctl_ele->base_sfx_id);            
          break;
            
        case kSrcProcTypeId:
          if((rc = _calc_src_proc_ele_count( *in_stmt.src_net, in_stmt.src_proc_ele, in_stmt.iter_cnt )) != kOkRC )
            rc = cwLogError(rc,"Unable to determine the in-stmt iteration count based on the iteration control src-proc '%s'.",cwStringNullGuard(in_stmt.src_proc_ele.label));
          break;
            
        case kSrcVarTypeId:
          if((rc = _calc_src_var_ele_count( *in_stmt.src_net, in_stmt.src_proc_ele, in_stmt.src_var_ele, in_stmt.iter_cnt )) != kOkRC )
            rc = cwLogError(rc,"Unable to determine the in-stmt iteration count based on the iteration control src-var '%s'.",cwStringNullGuard(in_stmt.src_var_ele.label));

          break;
            
        default:
          rc = cwLogError(kInvalidStateRC,"An unknown in-stmt element type was encountered.");
      }
      
      return rc;
    }
    
    void _destroy_in_stmt( in_stmt_t& s )
    {
      mem::release(s.in_var_ele.label);
      mem::release(s.src_net_label);
      mem::release(s.src_proc_ele.label);
      mem::release(s.src_var_ele.label);
    }

    rc_t _parse_in_stmt_src_net_proc_var_string( char* str, char*& src_net_label, const char*& src_proc_label, const char*& src_var_label )
    {
      rc_t rc = kOkRC;
      char* period0 = nullptr;
      char* period1 = nullptr;
      
      // locate the separator period on the src proc/var id
      if((period0 = firstMatchChar(str,'.')) == nullptr )
      {
        cwLogError(kSyntaxErrorRC,"No period separator was found in the src net/proc/var for the src specifier:%s.",str);
        goto errLabel;
      }

      *period0 = 0;
      
      if((period1 = firstMatchChar(period0+1,'.')) != nullptr )
      {
        *period1 = 0;
        src_var_label = period1 + 1;  // Set a pointer to the src var label
        src_proc_label = period0 + 1;
        src_net_label = mem::duplStr(str);
      }
      else
      {
        src_var_label = period0 + 1;
        src_proc_label = str;
        src_net_label = nullptr;
      }

      if( textLength(src_var_label) == 0 )
        rc = cwLogError(kSyntaxErrorRC,"The 'src-var' label has length 0.");

      if( textLength(src_proc_label) == 0 )
        rc = cwLogError(kSyntaxErrorRC,"The 'src-proc' label has length 0.");


    errLabel:
      return rc;
    }


    // Recursively search the tree of networks rooted on 'net' for the
    // network named 'net_proc_label'.
    network_t*  _find_labeled_network( network_t& net, const char* net_proc_label )
    {
      network_t* labeled_net = nullptr;

      // for each proc instance in the network
      for(unsigned i=0; i<net.proc_arrayN && labeled_net==nullptr; ++i)
      {
        proc_t* proc =  net.proc_array[i];
          
        // if this proc instance has an  internal network
        if( proc->internal_net != nullptr )
        {
          // if the name of the network matches the key ...
          if( textIsEqual(proc->label,net_proc_label) )            
            labeled_net = proc->internal_net; // .. we are done
          else
          {
            // ... otherwise recurse
            labeled_net = _find_labeled_network(*proc->internal_net,net_proc_label);
          }
        }
        
      }
      return labeled_net;
    }

    // Set  'in_stmt.src_net' based on 'in_stmt.src_net_label'
    rc_t _locate_src_net(network_t& net,proc_t* proc, in_stmt_t& in_stmt)
    {
      rc_t rc = kOkRC;
      network_t* src_net = nullptr;

      in_stmt.src_net = nullptr;
      
      if( in_stmt.src_net_label == nullptr )
        src_net = &net;
      else
      {        
        if( textIsEqual(in_stmt.src_net_label,"_") )
          src_net = &proc->ctx->net;
        else
        {
          if((src_net = _find_labeled_network(proc->ctx->net,in_stmt.src_net_label)) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The source net '%s' was not found.",cwStringNullGuard(in_stmt.src_net_label));
            goto errLabel;
          }
        } 
      }
    errLabel:
      in_stmt.src_net = src_net;

      if( in_stmt.src_net == nullptr )
        rc = cwLogError(kSyntaxErrorRC,"No source net was found.");
      
      return rc;
    }


    rc_t _create_in_stmt( network_t& net, proc_t* proc, in_stmt_t& in_stmt, const char* in_var_str, const char* src_proc_var_str )
    {
      rc_t     rc          = kOkRC;
      unsigned src_char_cnt = 0;

      in_stmt.in_var_ele.typeId   = kInVarTypeId;
      in_stmt.src_proc_ele.typeId = kSrcProcTypeId;
      in_stmt.src_var_ele.typeId  = kSrcVarTypeId;
      
      // verify the src proc/var string is valid
      if( (src_char_cnt = textLength(src_proc_var_str)) == 0 )
      {
        cwLogError(kSyntaxErrorRC,"No source variable was found for the input variable '%s'.",cwStringNullGuard(in_var_str));
        goto errLabel;
      }
      else
      {
        const char* src_proc_label = nullptr;
        const char* src_var_label = nullptr;
        
        char  str[ src_char_cnt+1 ];

        // put the src proc/var string into a non-const scratch buffer
        textCopy(str,src_char_cnt+1,src_proc_var_str);

        // parse the src part into it's 3 parts
        if((rc = _parse_in_stmt_src_net_proc_var_string(str, in_stmt.src_net_label, src_proc_label, src_var_label )) != kOkRC )          
        {
          cwLogError(rc,"Unable to parse the 'src' part of an 'in-stmt'.");
          goto errLabel;
        }

        // parse the in-var
        if((rc = _parse_in_ele( in_var_str, in_stmt.in_var_ele  )) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to parse the in-var from '%s'.",cwStringNullGuard(in_var_str));
          goto errLabel;
        }

        // parse the src-proc
        if((rc = _parse_in_ele( src_proc_label, in_stmt.src_proc_ele  )) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to parse the in-var from '%s'.",cwStringNullGuard(in_var_str));
          goto errLabel;
        }

        // parse the src-var
        if((rc = _parse_in_ele( src_var_label, in_stmt.src_var_ele )) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to parse the in-var from '%s'.",cwStringNullGuard(in_var_str));
          goto errLabel;
        }

        // get the var class desc. for the in-var
        if(( in_stmt.in_var_desc = var_desc_find(proc->class_desc,in_stmt.in_var_ele.label)) == nullptr )
        {
          rc = cwLogError(kEleNotFoundRC,"Unable to locate the var class desc for the in-var from '%s'.",cwStringNullGuard(in_stmt.in_var_ele.label));
          goto errLabel;
        }

        // get the src net
        if((rc = _locate_src_net(net,proc,in_stmt)) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to locate the src-net '%s'.",cwStringNullGuard(in_stmt.src_net_label));
          goto errLabel;
        }

        // if the in-var has an sfx_id, or is iterating, then the var needs to be created (the dflt creation process assumes no sfx id)
        if( in_stmt.in_var_ele.base_sfx_id != kInvalidId || in_stmt.in_var_ele.is_iter_fl )
        {
          in_stmt.create_in_fl = true;
          if( in_stmt.in_var_ele.base_sfx_id == kInvalidId )
            in_stmt.in_var_ele.base_sfx_id = kBaseSfxId;
        }

        // if the src-proc is not iterating and the src-proc was not given a literal sfx-id
        if( in_stmt.src_proc_ele.is_iter_fl==false && in_stmt.src_proc_ele.base_sfx_id==kInvalidId && in_stmt.src_net==&net)
          in_stmt.src_proc_ele.base_sfx_id = proc->label_sfx_id;

        // if this is not an iterating in-stmt ... 
        if( !in_stmt.in_var_ele.is_iter_fl )
        {
          in_stmt.iter_cnt = 1;  // ... then it must be a simple 1:1 connection
        }
        else
        {
          // if the in-stmt is iterating then determine the in-stmt element which controls the iteration count
          if((rc = _determine_in_stmt_iter_count_ctl_ele(net,proc,in_stmt)) != kOkRC || in_stmt.iter_cnt_ctl_ele==nullptr)
          {
            rc = cwLogError(rc,"Unable to determine the iter count control ele.");
            goto errLabel;
          }

          // if the in-stmt is iterating then determine the iteration count
          if((rc = _determine_in_stmt_iter_count(net,proc,in_stmt)) != kOkRC )
          {
            cwLogError(rc,"Unable to determine the in-stmt iteration count.");
            goto errLabel;
          }
        }
      }
      
    errLabel:
      if( rc != kOkRC )
        _destroy_in_stmt(in_stmt);
      
      return rc;      
    }

    const variable_t* _find_proxy_var( const char* procProcLabel, const char* varLabel, const variable_t* proxyVarL )
    {
      for(const variable_t* proxyVar=proxyVarL; proxyVar!=nullptr; proxyVar=proxyVar->var_link)
        if( textIsEqual(proxyVar->varDesc->proxyProcLabel,procProcLabel) && textIsEqual(proxyVar->varDesc->proxyVarLabel,varLabel) )
          return proxyVar;
      return nullptr;
    }
    
    rc_t _parse_in_list( network_t& net, proc_t* proc, variable_t* proxyVarL, proc_inst_parse_state_t& pstate )
    {
      rc_t            rc     = kOkRC;
      
      if( pstate.in_dict == nullptr )
        goto errLabel;
      
      if( !pstate.in_dict->is_dict() )
      {
        cwLogError(kSyntaxErrorRC,"The 'in' dict in proc instance '%s' is not a valid dictionary.",proc->label);
        goto errLabel;
      }

      if( pstate.in_dict->child_count() == 0 )
        goto errLabel;

      pstate.in_arrayN = pstate.in_dict->child_count();
      pstate.in_array  = mem::allocZ<in_stmt_t>(pstate.in_arrayN);

      // for each input variable in the 'in' set
      for(unsigned i=0; i<pstate.in_arrayN; ++i)
      {
        in_stmt_t&      in_stmt          = pstate.in_array[i];
        const object_t* in_pair          = pstate.in_dict->child_ele(i); // in:src pair
        const char*     in_var_str       = nullptr;
        const char*     src_proc_var_str = nullptr;

        // validate the basic syntax
        if( in_pair == nullptr || (in_var_str = in_pair->pair_label()) == nullptr || in_pair->pair_value()==nullptr )
        {
          cwLogError(rc,"Malformed dictionary encountered in 'in' statement.");
          goto errLabel;
        }

        // get the src net/proc/var string
        if((rc = in_pair->pair_value()->value(src_proc_var_str)) != kOkRC )
        {
          cwLogError(rc,"Unable to access the source proc/var string for the input var '%s'.",cwStringNullGuard(in_var_str));
          goto errLabel;
        }

        // 
        if((rc= _create_in_stmt(net, proc, in_stmt, in_var_str, src_proc_var_str )) != kOkRC )
        {
          cwLogError(rc,"Parse failed on the in-connection '%s:%s'.",cwStringNullGuard(in_var_str),cwStringNullGuard(src_proc_var_str));
          goto errLabel;
        }

        // create the var
        if( in_stmt.create_in_fl )
        {
          const variable_t* proxy_var;
          
          // a variable cannot be in the 'in' list if it is used a a subnet variable
          if((proxy_var = _find_proxy_var(proc->label, in_stmt.in_var_ele.label, proxyVarL)) != nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The variable:'%s' cannot be used as the in-var of an 'in' statement if it is a subnet variable: '%s'.",cwStringNullGuard(in_stmt.in_var_ele.label),cwStringNullGuard(proxy_var->label));
            goto errLabel;
          }

          
          for(unsigned i=0; i<in_stmt.iter_cnt; ++i)
          {
            variable_t* dum = nullptr;        

            if((rc = var_create( proc, in_stmt.in_var_desc->label,
                                 in_stmt.in_var_ele.base_sfx_id + i,
                                 kInvalidId,
                                 kAnyChIdx,
                                 in_stmt.in_var_desc->val_cfg,
                                 kInvalidTFl,
                                 dum )) != kOkRC )
            {
              rc = cwLogError(rc,"in-stmt var create failed on '%s:%s'.",cwStringNullGuard(in_var_str),cwStringNullGuard(src_proc_var_str));
              goto errLabel;
            }
          }
        }        
      }

    errLabel:
      
      return rc;      
    }

    // This function is used to create the variables on subnet procs
    // which are represented by interface variables on the subnet proxy (wrapper) proc.
    // 'proc' is a proc on the subnet's internal proc list
    // 'wrap_varL' is a list of all the variables on the wrapper proc.
    // These wrapper variables mirror variables on the internal subnet proc's.
    // This function finds the variables in wrap_varL that mirror
    // variables in 'proc' and instantiates them.  
    rc_t  _create_proxied_vars( proc_t* proc, variable_t* wrap_varL )
    {
      rc_t rc = kOkRC;

      // for each proxy var
      for(variable_t* wrap_var=wrap_varL; wrap_var!=nullptr; wrap_var=wrap_var->var_link )
      {
        // if this proxy var is on this internal proc (proc->label)
        if( textIsEqual(wrap_var->varDesc->proxyProcLabel,proc->label) )
        {
          
          variable_t* var;
          
          // create the proxied var
          if((rc = var_create( proc, wrap_var->varDesc->proxyVarLabel, wrap_var->label_sfx_id, kInvalidId, wrap_var->chIdx, nullptr, kInvalidTFl, var )) != kOkRC )
          {
            rc = cwLogError(rc,"Subnet variable creation failed for %s:%s on wrapper variable:%s:%s.",cwStringNullGuard(wrap_var->varDesc->proxyProcLabel),cwStringNullGuard(wrap_var->varDesc->proxyVarLabel),cwStringNullGuard(wrap_var->proc->label),cwStringNullGuard(wrap_var->label));
            goto errLabel;
          }

          //printf("Proxy matched: %s %s %s : flags:%i.\n",proc->label, wrap_var->varDesc->proxyVarLabel, wrap_var->label,wrap_var->varDesc->flags );

          var->flags |= kProxiedVarFl;
          
          if( cwIsFlag(wrap_var->varDesc->flags,kSubnetOutVarDescFl) )
            var->flags |= kProxiedOutVarFl;
        }
      }

    errLabel:
      return rc;
    }

    variable_t* _find_proxy_var( variable_t* wrap_varL, variable_t* var )
    {
      for(variable_t* wrap_var=wrap_varL; wrap_var!=nullptr; wrap_var=wrap_var->var_link)
        if( textIsEqual(wrap_var->varDesc->proxyProcLabel,var->proc->label) && textIsEqual(wrap_var->varDesc->proxyVarLabel,var->label) && (wrap_var->label_sfx_id==var->label_sfx_id) )
          return wrap_var;
        
      return nullptr;
    }
    
    rc_t _connect_proxy_vars( proc_t* proc, variable_t* wrap_varL )
    {
      rc_t rc = kOkRC;
      for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
      {
        if( cwIsFlag(var->flags,kProxiedVarFl) )
        {
          variable_t* wrap_var;
          if((wrap_var = _find_proxy_var(wrap_varL,var)) == nullptr )
          {
            rc = cwLogError(kEleNotFoundRC,"The wrapped variable '%s:%i' not found on '%s:%i'.",var->label,var->label_sfx_id,proc->label,proc->label_sfx_id);
            goto errLabel;
          }
          
          if( cwIsFlag(var->flags,kProxiedOutVarFl) )
          {
            //printf("Proxy connection: %i %s:%i-%s:%i -> %s:%i-%s:%i\n",var->flags,
            //       var->proc->label,var->proc->label_sfx_id,var->label,var->label_sfx_id,
            //       wrap_var->proc->label,wrap_var->proc->label_sfx_id,wrap_var->label,wrap_var->label_sfx_id );
            
            var_connect(var,wrap_var);
          }
          else
          {
            //printf("Proxy connection: %i %s:%i-%s:%i -> %s:%i-%s:%i\n",var->flags,
            //       wrap_var->proc->label,wrap_var->proc->label_sfx_id,wrap_var->label,wrap_var->label_sfx_id,
            //       var->proc->label,var->proc->label_sfx_id,var->label,var->label_sfx_id );
            
            var_connect(wrap_var,var);
          }          
        }
      }

      errLabel:
        return rc;
    }

    // Check if the var named 'label' already exists in 'proc->varL'.
    bool _is_var_proc_already_created( proc_t* proc, const char* var_label, const proc_inst_parse_state_t& pstate )
    {
      for(unsigned i=0; i<pstate.in_arrayN; ++i)
        if( textIsEqual(pstate.in_array[i].in_var_ele.label,var_label) && pstate.in_array[i].create_in_fl )
          return true;

      for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
        if( textIsEqual(var->label,var_label) )
          return true;
      
      return false;
    }

    rc_t _connect_in_vars(network_t& net, proc_t* proc, const proc_inst_parse_state_t& pstate)
    {
      rc_t rc = kOkRC;
      
      for(unsigned i=0; i<pstate.in_arrayN; ++i)
      {
        in_stmt_t& in_stmt = pstate.in_array[i];

        for(unsigned j=0; j<in_stmt.iter_cnt; ++j)
        {
          variable_t* in_var    = nullptr;
          network_t*  src_net   = in_stmt.src_net;
          proc_t* src_proc  = nullptr;
          variable_t* src_var   = nullptr;

          const char* in_var_label   = in_stmt.in_var_ele.label;
          const char* src_proc_label = in_stmt.src_proc_ele.label;
          const char* src_var_label  = in_stmt.src_var_ele.label;

          unsigned in_var_sfx_id   = (in_stmt.in_var_ele.base_sfx_id == kInvalidId ? kBaseSfxId : in_stmt.in_var_ele.base_sfx_id) + j;
          unsigned src_proc_sfx_id = kInvalidId;
          unsigned src_var_sfx_id  = kInvalidId;

          // if a literal in-var sfx id was no given ...
          if( in_stmt.in_var_ele.base_sfx_id == kInvalidId )
            in_var_sfx_id = kBaseSfxId;
          else
            in_var_sfx_id = in_stmt.in_var_ele.base_sfx_id;

          if( in_stmt.in_var_ele.is_iter_fl )
            in_var_sfx_id += j;

          // if a literal src-proc sfx id was not given ...
          if( in_stmt.src_proc_ele.base_sfx_id == kInvalidId )
            src_proc_sfx_id = kBaseSfxId; // ... then use the sfx_id of the in-var proc
          else
            src_proc_sfx_id = in_stmt.src_proc_ele.base_sfx_id; // ... otherwise use the given literal

          if( in_stmt.src_proc_ele.is_iter_fl ) // if this is an iterating src
            src_proc_sfx_id += j;

          // if a literal src-var sfx id was not given ...
          if( in_stmt.src_var_ele.base_sfx_id == kInvalidId )
            src_var_sfx_id = kBaseSfxId; // ... then use the base-sfx-id
          else
            src_var_sfx_id = in_stmt.src_var_ele.base_sfx_id; // ... otherwise use the given literal

          if( in_stmt.src_var_ele.is_iter_fl )  // if this is an iterating src
            src_var_sfx_id += j;
          
          // locate input value
          if((rc = var_find( proc, in_var_label, in_var_sfx_id, kAnyChIdx, in_var )) != kOkRC )
          {
            rc = cwLogError(rc,"The in-var '%s:%i' was not found.", in_stmt.in_var_ele.label, in_stmt.in_var_ele.base_sfx_id + j);
            goto errLabel;        
          }
          
          // locate source proc instance 
          if((src_proc = proc_find(*src_net, src_proc_label, src_proc_sfx_id )) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The src-proc '%s:%i' was not found.", in_stmt.src_proc_ele.label, src_proc_sfx_id );
            goto errLabel;
          }

          // locate source variable
          if((rc = var_find( src_proc, src_var_label, src_var_sfx_id, kAnyChIdx, src_var)) != kOkRC )
          {
            rc = cwLogError(rc,"The src-var '%s:i' was not found.", in_stmt.src_var_ele.label, src_var_sfx_id);
            goto errLabel;
          }

          // verify that the src_value type is included in the in_value type flags
          if( cwIsNotFlag(in_var->varDesc->type, src_var->varDesc->type) )
          {
            rc = cwLogError(kSyntaxErrorRC,"The type flags don't match on input:%s:%i source:%s:%i.%s:%i .", in_var_label, in_var_sfx_id, src_proc_label, src_proc_sfx_id, src_var_label, src_var_sfx_id);        
            goto errLabel;                
          }

          // verify that the source exists
          if( src_var->value == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The source value is null on the connection input::%s:%i source:%s:%i.%s:%i .", in_var_label, in_var_sfx_id, src_proc_label, src_proc_sfx_id, src_var_label, src_var_sfx_id);        
            goto errLabel;
          }

          //
          var_connect( src_var, in_var );
        }                
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Connection failed on proc '%s:%i'.",proc->label,proc->label_sfx_id);
      return rc;
    }
      
    
    rc_t _parse_proc_inst_cfg( network_t& net, const object_t* proc_inst_cfg, unsigned sfx_id, proc_inst_parse_state_t& pstate )
    {
      rc_t            rc       = kOkRC;
      const object_t* arg_dict = nullptr;

      // validate the syntax of the proc_inst_cfg pair
      if( !_is_non_null_pair(proc_inst_cfg))
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance cfg. is not a valid pair. No proc instance label could be parsed.");
        goto errLabel;
      }
      
      pstate.proc_label = proc_inst_cfg->pair_label();

      // verify that the proc instance label is unique
      if( proc_find(net,pstate.proc_label,sfx_id) != nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc proc instance label '%s:%i' has already been used.",pstate.proc_label,sfx_id);
        goto errLabel;
      }
      
      // get the proc instance class label
      if((rc = proc_inst_cfg->pair_value()->getv("class",pstate.proc_clas_label)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance cfg. %s is missing: 'type'.",pstate.proc_label);
        goto errLabel;        
      }
      
      // parse the optional args
      if((rc = proc_inst_cfg->pair_value()->getv_opt("args",     arg_dict,
                                                     "in",       pstate.in_dict,
                                                     "argLabel", pstate.arg_label,
                                                     "preset",   pstate.preset_labels,
                                                     "log",      pstate.log_labels )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance cfg. '%s' missing: 'type'.",pstate.proc_label);
        goto errLabel;        
      }

      // if an argument dict was given in the proc instance cfg
      if( arg_dict != nullptr  )
      {
        bool rptErrFl = true;

        // verify the arg. dict is actually a dict.
        if( !arg_dict->is_dict() )
        {
          cwLogError(kSyntaxErrorRC,"The proc instance argument dictionary on proc instance '%s' is not a dictionary.",pstate.proc_label);
          goto errLabel;
        }
        
        // if no label was given then try 'default'
        if( pstate.arg_label == nullptr)
        {
          pstate.arg_label = "default";
          rptErrFl = false;
        }

        // locate the specified argument record
        if((pstate.arg_cfg = arg_dict->find_child(pstate.arg_label)) == nullptr )
        {

          // if an explicit arg. label was given but it was not found
          if( rptErrFl )
          {
            rc = cwLogError(kSyntaxErrorRC,"The argument cfg. '%s' was not found on proc instance cfg. '%s'.",pstate.arg_label,pstate.proc_label);
            goto errLabel;
          }

          // no explicit arg. label was given - make arg_dict the proc instance arg cfg.
          pstate.arg_cfg = arg_dict;
          pstate.arg_label = nullptr;
        }        
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(kSyntaxErrorRC,"Configuration parsing failed on proc instance: '%s'.", cwStringNullGuard(pstate.proc_label) );
      
      return rc;
    }

    void _destroy_pstate( proc_inst_parse_state_t pstate )
    {
      for(unsigned i=0; i<pstate.in_arrayN; ++i)
        _destroy_in_stmt(pstate.in_array[i]);
      mem::release(pstate.in_array);
    }

    // Count of proc proc instances which exist in the network with a given class.
    unsigned _poly_copy_count( const network_t& net, const char* proc_clas_label )
    {
      unsigned n = 0;
      
      for(unsigned i=0; i<net.proc_arrayN; ++i)
        if( textIsEqual(net.proc_array[i]->class_desc->label,proc_clas_label) )
          ++n;
      return n;
    }

    rc_t _create_proc_instance( flow_t*         p,
                                const object_t* proc_inst_cfg,
                                unsigned        sfx_id,
                                network_t&      net,
                                variable_t*     proxyVarL,
                                proc_t*&    proc_ref )
    {
      rc_t                    rc         = kOkRC;
      proc_inst_parse_state_t pstate     = {};
      proc_t*             proc       = nullptr;
      class_desc_t*           class_desc = nullptr;

      proc_ref = nullptr;

      // parse the proc instance configuration 
      if((rc = _parse_proc_inst_cfg( net, proc_inst_cfg, sfx_id, pstate )) != kOkRC )
        goto errLabel;
      
      // locate the proc class desc
      if(( class_desc = class_desc_find(p,pstate.proc_clas_label)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The flow class '%s' was not found.",cwStringNullGuard(pstate.proc_clas_label));
        goto errLabel;
      }

      // if the poly proc instance count has been exceeded for this proc proc class ...
      if(class_desc->polyLimitN > 0 && _poly_copy_count(net,pstate.proc_clas_label) >= class_desc->polyLimitN )
      {
        // ... then silently skip this instantiation
        cwLogDebug("The poly class copy count has been exceeded for '%s' - skipping instantiation of sfx_id:%i.",pstate.proc_label,sfx_id);
        goto errLabel;
      }
      
      // instantiate the proc instance
      proc = mem::allocZ<proc_t>();

      proc->ctx           = p;
      proc->label         = mem::duplStr(pstate.proc_label);
      proc->label_sfx_id  = sfx_id;
      proc->proc_cfg      = proc_inst_cfg->pair_value();
      proc->arg_label     = pstate.arg_label;
      proc->arg_cfg       = pstate.arg_cfg;
      proc->class_desc    = class_desc;
      proc->net           = &net;

      // parse the in-list ,fill in pstate.in_array, and create var proc instances for var's referenced by in-list
      if((rc = _parse_in_list( net, proc, proxyVarL, pstate )) != kOkRC )
      {
        rc = cwLogError(rc,"in-list parse failed on proc proc instance '%s:%i'.",cwStringNullGuard(proc->label),sfx_id);
        goto errLabel;
      }

      // create the vars that are connected to the proxy vars
      if((rc = _create_proxied_vars( proc, proxyVarL )) != kOkRC )
      {
        rc = cwLogError(rc,"Proxy vars create failed on proc proc instance '%s:%i'.",cwStringNullGuard(proc->label),sfx_id);
        goto errLabel;
      }

      // Instantiate all the variables in the class description - that were not already created in _parse_in_list()
      for(var_desc_t* vd=class_desc->varDescL; vd!=nullptr; vd=vd->link)
        if( !_is_var_proc_already_created( proc, vd->label, pstate ) && cwIsNotFlag(vd->type,kRuntimeTFl) )
        {
          variable_t* var = nullptr;        
          if((rc = var_create( proc, vd->label, kBaseSfxId, kInvalidId, kAnyChIdx, vd->val_cfg, kInvalidTFl, var )) != kOkRC )
            goto errLabel;
        }

      // All the variables that can be used by this proc instance have now been created
      // and the chIdx of each variable is set to 'any'.
      
      // If a 'preset' field was included in the class cfg then apply the specified class preset
      if( pstate.preset_labels != nullptr )      
        if((rc = _class_apply_presets(proc, pstate.preset_labels )) != kOkRC )
          goto errLabel;

      // All the class presets values have now been set and those variables
      // that were expressed with a list have numeric channel indexes assigned.

      // Apply the proc proc instance 'args:{}' values.
      if( pstate.arg_cfg != nullptr )
      {
        if((rc = _proc_inst_args_channelize_vars( proc, pstate.arg_label, pstate.arg_cfg )) != kOkRC )
          goto errLabel;
      }
      
      // All the proc instance arg values have now been set and those variables
      // that were expressed with a list have numeric channel indexes assigned.


      // TODO: Should the 'all' variable be removed for variables that have numeric channel indexes?

      // Connect the in-list variables to their sources.
      if((rc = _connect_in_vars(net, proc, pstate)) != kOkRC )
      {
        rc = cwLogError(rc,"Input connection processing failed.");
        goto errLabel;
      }

      // Connect the proxied vars in this subnet proc
      if((rc = _connect_proxy_vars( proc, proxyVarL )) != kOkRC )
      {
        rc = cwLogError(rc,"Proxy connection processing failed.");
        goto errLabel;
      }

      
      // Complete the instantiation of the proc proc instance by calling the custom proc instance creation function.

      // Call the custom proc instance create() function.
      if((rc = class_desc->members->create( proc )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Custom instantiation failed." );
        goto errLabel;
      }

      // Create the proc instance->varMap[] lookup array
      if((rc =_create_proc_var_map( proc )) != kOkRC )
      {
        rc = cwLogError(rc,"Variable map creation failed.");
        goto errLabel;
      }

      // the custom creation function may have added channels to in-list vars fix up those connections here.
      //_complete_input_connections(proc);

      // set the log flags again so that vars created by the proc instance can be included in the log output
      if((rc = _set_log_flags(proc,pstate.log_labels)) != kOkRC )
        goto errLabel;
      
      // call the 'value()' function to inform the proc instance of the current value of all of it's variables.
      if((rc = _call_value_func_on_all_variables( proc )) != kOkRC )
        goto errLabel;

      if((rc = proc_validate(proc)) != kOkRC )
      {
        rc = cwLogError(rc,"Proc proc instance validation failed.");
        goto errLabel;
      }
      
      proc_ref = proc;
      
    errLabel:
      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Proc instantiation failed on '%s:%i'.",cwStringNullGuard(pstate.proc_label),sfx_id);
        proc_destroy(proc);
      }
      
      _destroy_pstate(pstate);
      
      return rc;      
    }

    //=======================================================================================================
    //
    // Apply presets
    //

    
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
    
    rc_t _find_network_preset_proc_pair( network_t& net, const char* preset_label, const char* proc_label, const object_t*& preset_val_ref )
    {
      rc_t rc = kOkRC;
      const object_t* net_preset_pair = nullptr;
      
      preset_val_ref = nullptr;
  
      // locate the cfg of the requested preset
      if((net_preset_pair = find_network_preset(net, preset_label )) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", cwStringNullGuard(preset_label) );
        goto errLabel;
      }

      // locate the proc instance matching 'proc_label'.
      for(unsigned i=0; i<net_preset_pair->child_count(); ++i)
      {
        const object_t* proc_pair;
        if((proc_pair = net_preset_pair->child_ele(i)) != nullptr && proc_pair->is_pair() && textIsEqual(proc_pair->pair_label(),proc_label) )
        {      

          preset_val_ref = proc_pair->pair_value();

          goto errLabel;
        }
      }
  
      rc = cwLogError(kInvalidArgRC,"The preset proc instance label '%s' was not found.",cwStringNullGuard(preset_label));
  
    errLabel:
      return rc;
    }

    
  }
}

cw::rc_t cw::flow::network_create( flow_t*            p,
                                   const object_t*    networkCfg,
                                   network_t&         net,
                                   variable_t*        proxyVarL,
                                   unsigned           polyCnt,
                                   network_order_id_t orderId )
{
  rc_t     rc     = kOkRC;

  // default to kNetFirstPolyOrderId
  unsigned outerN        = polyCnt;
  unsigned innerN        = 1;

  if((rc = networkCfg->getv("procs",net.procsCfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Failed on parsing required network cfg. elements.");
    goto errLabel;
  }

  if((rc = networkCfg->getv_opt("presets",net.presetsCfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Failed on parsing optional network cfg. elements.");
    goto errLabel;
  }


  if( orderId == kProcFirstPolyOrderId )
  {
    outerN = 1;
    innerN = polyCnt;
  }

  net.proc_arrayAllocN = polyCnt * net.procsCfg->child_count();
  net.proc_array       = mem::allocZ<proc_t*>(net.proc_arrayAllocN);
  net.proc_arrayN      = 0;

  for(unsigned i=0; i<outerN; ++i)
  {
    // for each proc in the network
    for(unsigned j=0; j<net.procsCfg->child_count(); ++j)
    {
      const object_t* proc_cfg = net.procsCfg->child_ele(j);

      for(unsigned k=0; k<innerN; ++k)
      {
        unsigned sfx_id = orderId == kNetFirstPolyOrderId ? i : k;

        assert(net.proc_arrayN < net.proc_arrayAllocN );
        
        // create the proc proc instance
        if( (rc= _create_proc_instance( p, proc_cfg, sfx_id, net, proxyVarL, net.proc_array[net.proc_arrayN] ) ) != kOkRC )
        {
          //rc = cwLogError(rc,"The instantiation at proc index %i is invalid.",net.proc_arrayN);
          goto errLabel;
        }

        net.proc_arrayN += 1;
      }
    }
  }

  net.poly_cnt = polyCnt;
  
errLabel:
  if( rc != kOkRC )
    _network_destroy(net);
  
  return rc;
}

cw::rc_t cw::flow::network_destroy( network_t& net )
{
  return _network_destroy(net);
}

const cw::object_t* cw::flow::find_network_preset( const network_t& net, const char* presetLabel )
{
  const object_t* preset_value = nullptr;
      
  if( net.presetsCfg != nullptr )
  {
    rc_t rc;
        
    if((rc = net.presetsCfg->getv_opt( presetLabel, preset_value )) != kOkRC )
      cwLogError(rc,"Search for network preset named '%s' failed.", cwStringNullGuard(presetLabel));
  }

  return preset_value;
      
}

cw::rc_t cw::flow::exec_cycle( network_t& net )
{
  rc_t rc = kOkRC;

  for(unsigned i=0; i<net.proc_arrayN; ++i)
  {
    if((rc = net.proc_array[i]->class_desc->members->exec(net.proc_array[i])) != kOkRC )
    {
      rc = cwLogError(rc,"Execution failed on the proc:%s:%i.",cwStringNullGuard(net.proc_array[i]->label),net.proc_array[i]->label_sfx_id);
      break;
    }
  }
      
  return rc;
}

cw::rc_t cw::flow::get_variable( network_t& net, const char* proc_label, const char* var_label, unsigned chIdx, proc_t*& procPtrRef, variable_t*& varPtrRef )
{
  rc_t        rc   = kOkRC;
  proc_t* proc = nullptr;
  variable_t* var  = nullptr;

  varPtrRef = nullptr;
  procPtrRef = nullptr;

  // locate the proc proc instance
  if((proc = proc_find(net,proc_label,kBaseSfxId)) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"Unknown proc proc instance label '%s'.", cwStringNullGuard(proc_label));
    goto errLabel;
  }

  // locate the variable
  if((rc = var_find( proc, var_label, kBaseSfxId, chIdx, var)) != kOkRC )
  {
    rc = cwLogError(kInvalidArgRC,"The variable '%s' could not be found on the proc proc instance '%s'.",cwStringNullGuard(var_label),cwStringNullGuard(proc_label));
    goto errLabel;
  }

  procPtrRef = proc;
  varPtrRef = var;
      
errLabel:
  return rc;
}


cw::rc_t cw::flow::network_apply_preset( network_t& net, const char* presetLabel, unsigned proc_label_sfx_id )
{
  rc_t    rc = kOkRC;
  const object_t* net_preset_value;
  const object_t* preset_pair;

  // locate the cfg of the requested preset
  if((net_preset_value = find_network_preset(net, presetLabel )) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", presetLabel );
    goto errLabel;
  }

  // for each proc instance in the preset
  for(unsigned i=0; i<net_preset_value->child_count(); ++i)
  {
    // get the proc instance label/value pair
    if((preset_pair = net_preset_value->child_ele(i)) != nullptr && preset_pair->is_pair() )
    {
      const char* proc_label = preset_pair->pair_label();
      const object_t* preset_value_cfg = preset_pair->pair_value();
      proc_t* proc;

      // locate the proc instance
      if((proc = proc_find(net,proc_label,proc_label_sfx_id)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"The network proc instance '%s' refered to in network preset '%s' could not be found.",proc_label,presetLabel);
        goto errLabel;
      }

      // if the preset value is a string then look it up in the class preset dictionary
      if( preset_value_cfg->is_string() )
      {
        const char* class_preset_label;
        preset_value_cfg->value(class_preset_label);
        _class_preset_channelize_vars(proc, class_preset_label );
      }
      else
      {
        // if the preset value is a dict then apply it directly
        if( preset_value_cfg->is_dict() )
        {
          if((rc =  _preset_channelize_vars( proc, "network", presetLabel, preset_value_cfg )) != kOkRC )
          {
            rc = cwLogError(rc,"The preset  '%s' application failed on proc instance '%s'.", presetLabel, proc_label );
            goto errLabel;
          }
          
        }
        else
        {
          rc = cwLogError(kSyntaxErrorRC,"The network preset '%s' proc instance '%s' does not have a string or dictionary value.", presetLabel, proc_label );
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

cw::rc_t cw::flow::network_apply_dual_preset( network_t& net, const char* presetLabel_0, const char* presetLabel_1, double coeff )
{
  rc_t    rc = kOkRC;
  
  const object_t* net_preset_value_0;

  cwLogInfo("*** Applying dual: %s %s : %f",presetLabel_0, presetLabel_1, coeff );
  
  // locate the cfg of the requested preset
  if((net_preset_value_0 = find_network_preset(net, presetLabel_0 )) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", presetLabel_0 );
    goto errLabel;
  }

  // for each proc instance in the preset
  for(unsigned i=0; i<net_preset_value_0->child_count(); ++i)
  {
    const object_t* preset_pair_0      = net_preset_value_0->child_ele(i);
    const char*     proc_label         = preset_pair_0->pair_label(); 
    const object_t* preset_value_cfg_0 = preset_pair_0->pair_value();
    proc_t*     proc               = nullptr;
    const object_t* preset_value_cfg_1 = nullptr;
    const int two = 2;
    const char* class_preset_labelA[two];
    
    // get the proc instance label/value pair
    if((preset_pair_0 = net_preset_value_0->child_ele(i)) == nullptr || !preset_pair_0->is_pair() )
    {
      rc = cwLogError(kSyntaxErrorRC,"An invalid preset value pair was encountered in '%s'.",presetLabel_0);
      goto errLabel;
    }

    // verify that the preset value is a string or dict
    if( preset_pair_0->pair_value()==nullptr || (!preset_value_cfg_0->is_dict() && !preset_value_cfg_0->is_string() ))
    {
      rc = cwLogError(kSyntaxErrorRC,"The preset value pair for proc instance '%s' in '%s' is not a 'dict' or 'string'.",proc_label,presetLabel_0);
      goto errLabel;
    }

    // locate the proc instance associated with the primary and secondary preset
    if((proc = proc_find(net,proc_label,kBaseSfxId)) == nullptr )
    {
      rc = cwLogError(kInvalidIdRC,"The network proc instance '%s' refered to in network preset '%s' could not be found.",cwStringNullGuard(proc_label),cwStringNullGuard(presetLabel_0));
      goto errLabel;
    }
            
    // locate the second proc instance/preset value pair 
    if((rc = _find_network_preset_proc_pair(net, presetLabel_1, proc_label, preset_value_cfg_1 )) != kOkRC )
    {
      rc = cwLogError(kInvalidIdRC,"The second network proc instance '%s' refered to in network preset '%s' could not be found.",proc_label,presetLabel_1);
      goto errLabel;
    }
    
    // TODO: We require that the proc instance presets both be of the same type: string or dict.
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
      rc = _class_multi_preset_channelize_vars(proc, class_preset_labelA, two, coeff );        
    }
    else
    {
      assert( preset_value_cfg_1->is_dict() );
        
      const object_t* preset_value_cfgA[] = { preset_value_cfg_0, preset_value_cfg_1};
                  
      if((rc =  _multi_preset_channelize_vars( proc, "network", class_preset_labelA, preset_value_cfgA, two, coeff )) != kOkRC )
      {
        rc = cwLogError(rc,"The dual preset  '%s':'%s' application failed on proc instance '%s'.", cwStringNullGuard(class_preset_labelA[0]), cwStringNullGuard(class_preset_labelA[1]), proc_label );
        goto errLabel;
      }
    }
  }

  
errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"The dual preset  '%s':'%s' application failed.", cwStringNullGuard(presetLabel_0), cwStringNullGuard(presetLabel_1) );

  return rc;
}

cw::rc_t cw::flow::network_apply_preset( network_t& net, const multi_preset_selector_t& mps )
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
    rc = network_apply_preset( net, label0 );
  }
  else
  {
    double coeff = _calc_multi_preset_dual_coeff(mps);
    rc = network_apply_dual_preset( net, label0, label1, coeff );
  }
  

errLabel:
  return rc;
}
