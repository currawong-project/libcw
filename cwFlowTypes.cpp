#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDspTypes.h" // real_t, sample_t
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"


namespace cw
{
  namespace flow
  {
    idLabelPair_t typeLabelFlagsA[] = {
      
      { kBoolTFl, "bool" },
      { kUIntTFl, "uint" },
      { kIntTFl,  "int", },
      { kFloatTFl,  "float"},
      { kRealTFl,   "real"},
      { kDoubleTFl,  "double"},
      
      { kBoolMtxTFl, "bool_mtx" },
      { kUIntMtxTFl, "uint_mtx" },
      { kIntMtxTFl,  "int_mtx"  },
      { kFloatMtxTFl,  "float_mtx" },
      { kDoubleMtxTFl,  "double_mtx" },
      
      { kABufTFl,   "audio" },
      { kFBufTFl,   "spectrum" },
      { kStringTFl, "string" },
      { kTimeTFl,   "time" },
      { kInvalidTFl, nullptr }
    };

    const char* _typeFlagToLabel( unsigned flag )
    {
      for(unsigned i=0; typeLabelFlagsA[i].id != kInvalidTFl; ++i)
        if( typeLabelFlagsA[i].id == flag )
          return typeLabelFlagsA[i].label;

      return "<unknown-type>";
    }

      
    void _value_release( value_t* v )
    {
      if( v == nullptr )
        return;
        
      switch( v->flags & kTypeMask )
      {
        case kInvalidTFl:
          break;
          
        case kBoolTFl:
        case kUIntTFl:
        case kIntTFl:
        case kFloatTFl:
        case kDoubleTFl:
          break;
          
        case kABufTFl:
          abuf_destroy( v->u.abuf );
          break;
          
        case kFBufTFl:
          fbuf_destroy( v->u.fbuf );
          break;

          
        case kBoolMtxTFl:
        case kUIntMtxTFl:
        case kIntMtxTFl:
        case kFloatMtxTFl:
        case kDoubleMtxTFl:
          assert(0); // not implemeneted
          break;
          
        case kStringTFl:
          mem::release( v->u.s );
          break;
                    
        case kTimeTFl:
          assert(0);
          break;

        default:
          assert(0);
          break;
      }

      v->flags = kInvalidTFl;
    }

    void _value_duplicate( value_t& dst, const value_t& src )
    {
        
      switch( src.flags & kTypeMask )
      {
        case kInvalidTFl:
          break;
          
        case kBoolTFl:
        case kUIntTFl:
        case kIntTFl:
        case kFloatTFl:
        case kDoubleTFl:
          dst = src;
          break;
          
        case kABufTFl:
          
          dst.u.abuf = src.u.abuf == nullptr ? nullptr : abuf_duplicate(src.u.abuf);
          dst.flags = src.flags;
          break;
          
        case kFBufTFl:
          dst.u.fbuf = src.u.fbuf == nullptr ? nullptr : fbuf_duplicate(src.u.fbuf);
          dst.flags = src.flags;
          break;

          
        case kBoolMtxTFl:
        case kUIntMtxTFl:
        case kIntMtxTFl:
        case kFloatMtxTFl:
        case kDoubleMtxTFl:
          assert(0); // not implemeneted
          break;
          
        case kStringTFl:
          dst.u.s = mem::duplStr( dst.u.s );
          dst.flags = src.flags;
          break;
                    
        case kTimeTFl:
          assert(0);
          break;

        default:
          assert(0);
          break;
      }

    }
    
    void _value_print( const value_t* v )
    {
      if( v == nullptr )
        return;
        
      switch( v->flags & kTypeMask )
      {
        case kInvalidTFl:
          break;
          
        case kBoolTFl:  printf("%s ", v->u.b ? "True" : "False" ); break;          
        case kUIntTFl:  printf("%i ", v->u.u ); break;
        case kIntTFl:   printf("%i ", v->u.i ); break;          
        case kFloatTFl: printf("%f ", v->u.f ); break;
        case kDoubleTFl:printf("%f ", v->u.d ); break;
        case kABufTFl:
          if( v->u.abuf == nullptr )
            printf("abuf: <null>");
          else
            printf("abuf: chN:%i frameN:%i srate:%8.1f ", v->u.abuf->chN, v->u.abuf->frameN, v->u.abuf->srate ); 
          break;
          
        case kFBufTFl:
          if( v->u.fbuf == nullptr )
            printf("fbuf: <null>");
          else
          {
            printf("fbuf: chN:%i srate:%8.1f ", v->u.fbuf->chN, v->u.fbuf->srate );
            for(unsigned i=0; i<v->u.fbuf->chN; ++i)
              printf("(binN:%i hopSmpN:%i) ", v->u.fbuf->binN_V[i], v->u.fbuf->hopSmpN_V[i] );
          }
          break;
          
        case kBoolMtxTFl:
        case kUIntMtxTFl:
        case kIntMtxTFl:
        case kFloatMtxTFl:
        case kDoubleMtxTFl:
          assert(0); // not implemeneted
          break;
          
        case kStringTFl:
          printf("%s ", v->u.s);
          break;
           
        case kTimeTFl:
          assert(0);
          break;

        default:
          assert(0);
          break;
      }

    }
    

    rc_t _val_get( const value_t* val, bool& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->flags & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b; break;
        case kUIntTFl:   valRef = val->u.u!=0; break;
        case kIntTFl:    valRef = val->u.i!=0; break;
        case kFloatTFl:  valRef = val->u.f!=0; break;
        case kDoubleTFl: valRef = val->u.d!=0; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to a bool.",val->flags);
      }
      return rc;
    }
    
    rc_t _val_get( const value_t* val, uint_t& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->flags & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = val->u.u; break;
        case kIntTFl:    valRef = val->u.i; break;
        case kFloatTFl:  valRef = (uint_t)val->u.f; break;
        case kDoubleTFl: valRef = (uint_t)val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to a uint_t.",val->flags);
      }
      return rc;
    }
      
    rc_t _val_get( const value_t* val, int_t& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->flags & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = (int_t)val->u.u; break;
        case kIntTFl:    valRef = val->u.i; break;
        case kFloatTFl:  valRef = (int_t)val->u.f; break;
        case kDoubleTFl: valRef = (int_t)val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to a int_t.",val->flags);
      }
      return rc;
    }

    rc_t _val_get( const value_t* val, float& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->flags & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = (float)val->u.u; break;
        case kIntTFl:    valRef = (float)val->u.i; break;
        case kFloatTFl:  valRef = (float)val->u.f; break;
        case kDoubleTFl: valRef = (float)val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to a float.", val->flags);
      }
      return rc;
    }

    rc_t _val_get( const value_t* val, double& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->flags & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = (double)val->u.u; break;
        case kIntTFl:    valRef = (double)val->u.i; break;
        case kFloatTFl:  valRef = (double)val->u.f; break;
        case kDoubleTFl: valRef =         val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to a double.",val->flags);
      }
      return rc;
    }

    rc_t _val_get( const value_t* val, const char*& valRef )
    {
      rc_t rc = kOkRC;
      if( cwIsFlag(val->flags & kTypeMask, kStringTFl) )
        valRef = val->u.s;
      else
      {
        rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to a string.",val->flags);
        valRef = nullptr;
      }
      
      return rc;
    }

    rc_t _val_get( value_t* val, abuf_t*& valRef )
    {
      rc_t rc = kOkRC;
      if( cwIsFlag(val->flags & kTypeMask, kABufTFl) )
        valRef = val->u.abuf;
      else
      {
        rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to an abuf_t.",val->flags);
        valRef = nullptr;
      }
      return rc;
    }

    rc_t _val_get( value_t* val, const abuf_t*& valRef )
    {
      abuf_t* non_const_val;
      rc_t rc = kOkRC;
      if((rc = _val_get(val,non_const_val)) == kOkRC )
        valRef = non_const_val;
      return rc;        
    }

    rc_t _val_get( value_t* val, fbuf_t*& valRef )
    {
      rc_t rc = kOkRC;
      if( cwIsFlag(val->flags & kTypeMask, kFBufTFl) )
        valRef = val->u.fbuf;
      else
      {
        valRef = nullptr;
        rc = cwLogError(kTypeMismatchRC,"The type 0x%x could not be converted to an fbuf_t.",val->flags);
      }
      return rc;
    }

    rc_t _val_get( value_t* val, const fbuf_t*& valRef )
    {
      fbuf_t* non_const_val;
      rc_t rc = kOkRC;
      if((rc = _val_get(val,non_const_val)) == kOkRC )
        valRef = non_const_val;
      return rc;        
    }


    template< typename T >
    rc_t _val_get_driver( const variable_t* var, T& valRef )
    {
      if( var == nullptr )
        return cwLogError(kInvalidArgRC,"Cannnot get the value of a non-existent variable.");
      
      if( var->value == nullptr )
        return cwLogError(kInvalidStateRC,"No value has been assigned to the variable: %s.%s ch:%i.",cwStringNullGuard(var->inst->label),cwStringNullGuard(var->label),var->chIdx);

      return _val_get(var->value,valRef);
    }

    rc_t _var_find_to_set( instance_t* inst, unsigned vid, unsigned chIdx, unsigned typeFl, variable_t*& varRef )
    {
      rc_t rc = kOkRC;
      varRef = nullptr;

      // 
      if((rc = var_find(inst,vid,chIdx,varRef)) == kOkRC )
      {
        // validate the type of the variable against the description
        if( !cwIsFlag(varRef->varDesc->type,typeFl ) )
          rc = cwLogError(kTypeMismatchRC,"Type mismatch. Instance:%s variable:%s with type 0x%x does not match requested type:0x%x.",varRef->inst->label,varRef->label,varRef->varDesc->type,typeFl);
    
      }

      return rc;
    }
   

    // Variable lookup: Exact match on vid and chIdx
    rc_t _var_find_on_vid_and_ch( instance_t* inst, unsigned vid, unsigned chIdx, variable_t*& varRef )
    {
      varRef = nullptr;
      
      for(variable_t* var = inst->varL; var!=nullptr; var=var->var_link)
      {
        // the variable vid and chIdx should form a unique pair
        if( var->vid==vid && var->chIdx == chIdx )
        {
          varRef = var;
          return kOkRC;
        }
      }
      return cwLogError(kInvalidIdRC,"The variable matching id:%i ch:%i on instance '%s' could not be found.", vid, chIdx, inst->label);
    }

    // Variable lookup: Exact match on label and chIdx
    variable_t* _var_find_on_label_and_ch( instance_t* inst, const char* var_label, unsigned chIdx )
    {
      for(variable_t* var = inst->varL; var!=nullptr; var=var->var_link)
      {
        // the variable vid and chIdx should form a unique pair
        if( textCompare(var->label,var_label)==0 && var->chIdx == chIdx )
          return var;
      }
      
      return nullptr;
    }
    
    
    rc_t _validate_var_assignment( variable_t* var, unsigned typeFl )
    {
      if( cwIsFlag(var->varDesc->flags, kSrcVarFl ) )
        return cwLogError(kInvalidStateRC, "The variable '%s' on instance '%s' cannot be set because it is a 'src' variable.", var->label, var->inst->label);
      
      if( !cwIsFlag(var->varDesc->type, typeFl ) )
        return cwLogError(kTypeMismatchRC, "The variable '%s' on instance '%s' is not a  '%s'.", var->label, var->inst->label, _typeFlagToLabel( typeFl ));

      return kOkRC;
    }

    rc_t _var_broadcast_new_value( variable_t* var )
    {
      rc_t rc = kOkRC;
      /*
      // notify each connected var that the value has changed
      for(variable_t* con_var = var->connect_link; con_var!=nullptr; con_var=con_var->connect_link)
        if((rc = con_var->inst->class_desc->members->value( con_var->inst, con_var )) != kOkRC )
          break;
      */
      return rc;
    }

    template< typename T >
    void _var_setter( variable_t* var, unsigned local_value_idx, T val )
    {
      cwLogError(kAssertFailRC,"Unimplemented variable setter.");
      assert(0);
    }

    template<>
    void _var_setter<bool>( variable_t* var, unsigned local_value_idx, bool val )
    {
      var->local_value[ local_value_idx ].u.b   = val;
      var->local_value[ local_value_idx ].flags = kBoolTFl;
      cwLogMod("%s.%s ch:%i %i (bool).",var->inst->label,var->label,var->chIdx,val);
    }

    template<>
    void _var_setter<unsigned>( variable_t* var, unsigned local_value_idx, unsigned val )
    {
      var->local_value[ local_value_idx ].u.u   = val;
      var->local_value[ local_value_idx ].flags = kUIntTFl;
      cwLogMod("%s.%s ch:%i %i (uint).",var->inst->label,var->label,var->chIdx,val);
    }

    template<>
    void _var_setter<int>( variable_t* var, unsigned local_value_idx, int val )
    {
      var->local_value[ local_value_idx ].u.i   = val;
      var->local_value[ local_value_idx ].flags = kIntTFl;
      cwLogMod("%s.%s ch:%i %i (int).",var->inst->label,var->label,var->chIdx,val);
    }

    template<>
    void _var_setter<float>( variable_t* var, unsigned local_value_idx, float val )
    {
      var->local_value[ local_value_idx ].u.f   = val;
      var->local_value[ local_value_idx ].flags = kFloatTFl;
      cwLogMod("%s.%s ch:%i %f (float).",var->inst->label,var->label,var->chIdx,val);
    }

    template<>
    void _var_setter<double>( variable_t* var, unsigned local_value_idx, double val )
    {
      var->local_value[ local_value_idx ].u.d   = val;
      var->local_value[ local_value_idx ].flags = kDoubleTFl;
      cwLogMod("%s.%s ch:%i %f (double).",var->inst->label,var->label,var->chIdx,val);
    }

    template<>
    void _var_setter<const char*>( variable_t* var, unsigned local_value_idx, const char* val )
    {      
      var->local_value[ local_value_idx ].u.s   = mem::duplStr(val);
      var->local_value[ local_value_idx ].flags = kStringTFl;
      cwLogMod("%s.%s ch:%i %s (string).",var->inst->label,var->label,var->chIdx,val);
    }

    template<>
    void _var_setter<abuf_t*>( variable_t* var, unsigned local_value_idx, abuf_t* val )
    {
      var->local_value[ local_value_idx ].u.abuf   = val;
      var->local_value[ local_value_idx ].flags = kABufTFl;
      cwLogMod("%s.%s ch:%i %s (abuf).",var->inst->label,var->label,var->chIdx,abuf==nullptr ? "null" : "valid");
    }
    
    template<>
    void _var_setter<fbuf_t*>( variable_t* var, unsigned local_value_idx, fbuf_t* val )
    {
      var->local_value[ local_value_idx ].u.fbuf = val;
      var->local_value[ local_value_idx ].flags  = kFBufTFl;
      cwLogMod("%s.%s ch:%i %s (fbuf).",var->inst->label,var->label,var->chIdx,fbuf==nullptr ? "null" : "valid");
    }
    
    template< typename T >
    rc_t _var_set_template( variable_t* var, unsigned typeFlag, T val )
    {
      rc_t rc;
      
      unsigned next_local_value_idx = (var->local_value_idx + 1) % kLocalValueN;
      
      // store the pointer to the current value of this variable
      value_t* original_value     = var->value;
      unsigned original_value_idx = var->local_value_idx;

      // verify that this is a legal assignment
      if((rc = _validate_var_assignment( var, typeFlag )) != kOkRC )
      {        
        goto errLabel;
      }
      
      // release the previous value in the next slot
      _value_release(&var->local_value[next_local_value_idx]);

      // set the new local value
      _var_setter(var,next_local_value_idx,val);

      // make the new local value current
      var->value           = var->local_value + next_local_value_idx;
      var->local_value_idx = next_local_value_idx;
      
      // If the instance is fully initialized ...
      if( var->inst->varMapA != nullptr )
      {
        // ... then inform the proc. that the value changed
        // Note 1: We don't want to this call to occur if we are inside or prior to 'proc.create()' 
        // call because calls' to 'proc.value()' will see the instance in a incomplete state)
        // Note 2: If this call returns an error then the value assignment is cancelled
        // and the value does not change.
        rc = var->inst->class_desc->members->value( var->inst, var );        
      }

      if( rc == kOkRC )
      {
        // send the value to connected downstream proc's
        rc = _var_broadcast_new_value( var );
      }
      else
      {
        // cancel the assignment and restore the original value
        var->value           = original_value;
        var->local_value_idx = original_value_idx;
      }
      
    errLabel:
      return rc;
    }
    
    
    bool is_connected_to_external_proc( const variable_t* var )
    {
      // if this var does not have a 'src_ptr' then it can't be connected to an external proc
      if( var->src_var == nullptr || var->value == nullptr )
        return false;

      // if this var is using a local value then it can't be connected to an external proc
      for(unsigned i=0; i<kLocalValueN; ++i)
        if( var->value == var->local_value + i )
          return false;

      return true;
    }

    template< typename T >
    rc_t _var_set_driver( variable_t* var, unsigned typeFlag, T value )
    {
      rc_t rc;

      // if this variable is fed from the output of an external proc - then it's local value cannot be set
      if(is_connected_to_external_proc(var)   )
        return kOkRC;
      

      // if this assignment targets a specific channel ...
      if( var->chIdx != kAnyChIdx )
      {
        rc = _var_set_template( var, typeFlag, value ); // ...  then set it alone
      }
      else // ... otherwise set all channels.
      {
        for(; var!=nullptr; var=var->ch_link)
          if((rc = _var_set_template( var, typeFlag, value )) != kOkRC)
            break;
      }

      return rc;
    }

    
    rc_t  _var_register_and_set( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, abuf_t* abuf )
    {
      rc_t rc;
      variable_t* var = nullptr;
      if((rc = var_register_and_set( inst, var_label, vid, chIdx, var)) != kOkRC )
        return rc;

      if( var != nullptr )
        _var_set_driver( var, kABufTFl, abuf );

      return rc;
    }

    rc_t  _var_register_and_set( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, fbuf_t* fbuf )
    {
      rc_t rc;
      variable_t* var = nullptr;
      if((rc = var_register_and_set( inst, var_label, vid, chIdx, var)) != kOkRC )
        return rc;

      if( var != nullptr )
        _var_set_driver( var, kFBufTFl, fbuf );

      return rc;
    }

    rc_t _set_var_value_from_cfg( variable_t* var, const object_t* value )
    {
      rc_t rc = kOkRC;

      unsigned typeFlag = var->varDesc->type & kTypeMask;
      
      switch( typeFlag )
      {
        case kBoolTFl:
          {
            bool v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set_driver( var, typeFlag, v );
          }
          break;
          
        case kUIntTFl:
          {
            unsigned v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set_driver( var, typeFlag, v );
          }
          break;
          
        case kIntTFl:
          {
            int v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set_driver( var, typeFlag, v );
          }
          break;
          
        case kFloatTFl:
          {
            float v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set_driver( var, typeFlag, v );
          }
          break;

        case kDoubleTFl:
          {
            double v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set_driver( var, typeFlag, v );
          }
          break;
          
        case kStringTFl:
          {
            const char* v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set_driver( var, typeFlag, v );
          }
          break;

        default:
          rc = cwLogError(kOpFailRC,"The variable type 0x%x cannot yet be set via a preset.", var->varDesc->type );
          goto errLabel;
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(kSyntaxErrorRC,"The %s.%s could not extract a type:%s from a configuration value.",var->inst->label,var->label,_typeFlagToLabel(var->varDesc->type & kTypeMask));
      return rc;
      
    }

    rc_t  _var_map_id_to_index(  instance_t* inst, unsigned vid, unsigned chIdx, unsigned& idxRef )
    {
      unsigned idx = vid * inst->varMapChN + (chIdx == kAnyChIdx ? 0 : (chIdx+1));

      // verify that the map idx is valid
      if( idx >= inst->varMapN )
        return cwLogError(kAssertFailRC,"The variable map positioning location %i is out of the range %i on instance '%s' vid:%i ch:%i.", idx, inst->varMapN, inst->label,vid,chIdx);

      idxRef = idx;
  
      return kOkRC;
    }

    rc_t  _var_map_label_to_index(  instance_t* inst, const char* var_label, unsigned chIdx, unsigned& idxRef )
    {
      rc_t        rc  = kOkRC;
      variable_t* var = nullptr;
  
      idxRef = kInvalidIdx;
      if((rc = var_find(inst, var_label, chIdx, var )) == kOkRC)
        rc = _var_map_id_to_index( inst, var->vid, chIdx, idxRef );
     
      return rc;
    }

    rc_t _var_add_to_ch_list( instance_t* inst, variable_t* new_var )
    {
      rc_t rc = kOkRC;
      
      variable_t* base_var = nullptr;
      variable_t* v0 = nullptr;
      variable_t* v1 = nullptr;
      
      if( new_var->chIdx == kAnyChIdx )
        return kOkRC;
      
      if((base_var = _var_find_on_label_and_ch( inst, new_var->label, kAnyChIdx )) == nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The base channel variable does not exist for '%s.%s'. This is an illegal state.", inst->label, new_var->label );
        goto errLabel;
      }

      // insert v0 in order by channel number
      for(v0=base_var,v1=base_var->ch_link; v1!=nullptr; v1=v1->ch_link)
      {
        if( v1->chIdx > new_var->chIdx )
          break;
        v0 = v1;
      }

      // the new var channel index should never match the previous or next channel index
      assert( v0->chIdx != new_var->chIdx && (v1==nullptr || v1->chIdx != new_var->chIdx ) );

      new_var->ch_link = v1;
      v0->ch_link      = new_var;
      
      
    errLabel:
      return rc;
      
    }

    // Create a variable and set it's value from 'value_cfg'.
    // If 'value_cfg' is null then use the value from var->varDesc->val_cfg.
    rc_t _var_create( instance_t* inst, const char* var_label, unsigned id, unsigned chIdx, const object_t* value_cfg, variable_t*& varRef )
    {
      rc_t        rc  = kOkRC;
      variable_t* var = nullptr;
      var_desc_t* vd  = nullptr;
      
      varRef = nullptr;

      // if this var already exists - it can't be created again
      if((var = _var_find_on_label_and_ch(inst,var_label,chIdx)) != nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The variable '%s' ch:%i has already been created on the instance: '%s'.",var_label,chIdx,inst->label);
        goto errLabel;
      }

      // locate the var desc
      if((vd = var_desc_find( inst->class_desc, var_label)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"Unable to locate the variable '%s' in class '%s'.", var_label, inst->class_desc->label );
        goto errLabel;
      }

      // create the var
      var = mem::allocZ<variable_t>();

      var->varDesc = vd;
      var->inst    = inst;
      var->label   = mem::duplStr(var_label);
      var->vid     = id;
      var->chIdx   = chIdx;
      var->value   = nullptr;

      // if no value was given then set the value to the value given in the class
      if( value_cfg == nullptr )
        value_cfg = var->varDesc->val_cfg;

      // if value_cfg is valid set the variable value
      if( value_cfg != nullptr )
        if((rc = _set_var_value_from_cfg( var, value_cfg )) != kOkRC )
          goto errLabel;

      var->var_link  = inst->varL;
      inst->varL = var;

      // link the new var into the ch_link list
      if((rc = _var_add_to_ch_list(inst, var )) != kOkRC )
        goto errLabel;


    errLabel:
      if( rc != kOkRC )
      {
        _var_destroy(var);
        cwLogError(kOpFailRC,"Variable creation failed on '%s.%s' ch:%i.", inst->label, var_label, chIdx );
      }
      else
      {
        varRef = var;
        cwLogMod("Created var: %s.%s ch:%i.", inst->label, var_label, chIdx );
      }
      
      return rc;
    }

    void _var_print( const variable_t* var )
    {
      const char* conn_label  = is_connected_to_external_proc(var) ? "extern" : "      ";
    
      printf("  %20s id:%4i ch:%3i : %s  : ", var->label, var->vid, var->chIdx, conn_label );
    
      if( var->value == nullptr )
        _value_print( &var->local_value[0] );
      else
        _value_print( var->value );

      printf("\n");    
    }
    
    
    rc_t _preset_set_var_value( instance_t* inst, const char* var_label, unsigned chIdx, const object_t* value )
    {
      rc_t rc = kOkRC;
      variable_t* var = nullptr;

      // get the variable
      if((rc = var_find( inst, var_label, chIdx, var )) != kOkRC )
        goto errLabel;
        
      rc = _set_var_value_from_cfg( var, value );
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"The value of instance:%s variable:%s could not be set via a preset.", inst->label, var_label );

      return rc;
    }


    
  }
}


cw::flow::abuf_t* cw::flow::abuf_create( srate_t srate, unsigned chN, unsigned frameN )
{
  abuf_t* a  = mem::allocZ<abuf_t>();
  a->srate   = srate;
  a->chN     = chN;
  a->frameN  = frameN;
  a->buf     = mem::allocZ<sample_t>( chN*frameN );
  
  return a;
}

void  cw::flow::abuf_destroy( abuf_t*& abuf )
{
  if( abuf == nullptr )
    return;
  
  mem::release(abuf->buf);
  mem::release(abuf);
}

cw::flow::abuf_t*  cw::flow::abuf_duplicate( const abuf_t* src )
{
  return abuf_create( src->srate, src->chN, src->frameN );
}


cw::rc_t  cw::flow::abuf_set_channel( abuf_t* abuf, unsigned chIdx, const sample_t* v, unsigned vN )
{
  rc_t rc = kOkRC;
  
  if( vN > abuf->frameN )
    rc = cwLogError(kInvalidArgRC,"Cannot copy source vector of length %i into an abuf of length %i.", vN, abuf->frameN);
  else
    if( chIdx > abuf->chN )
      rc = cwLogError(kInvalidArgRC,"The abuf destination channel %i is out of range.", chIdx);
    else
      vop::copy( abuf->buf + (chIdx*abuf->frameN), v, vN);
  
  return rc;
}

const cw::flow::sample_t*   cw::flow::abuf_get_channel( abuf_t* abuf, unsigned chIdx )
{
  assert( abuf->buf != nullptr );
  return abuf->buf + (chIdx*abuf->frameN);
}

cw::flow::fbuf_t* cw::flow::fbuf_create( srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_real_t** magV, const fd_real_t** phsV, const fd_real_t** hzV )
{
  for(unsigned i=0; i<chN; ++i)
    if( binN_V[i] > maxBinN_V[i] )
    {
      cwLogError(kInvalidArgRC,"A channel bin count (%i) execeeds the max bin count (%i).",binN_V[i],maxBinN_V[i]);
      return nullptr;;
    }
  
  fbuf_t* f = mem::allocZ<fbuf_t>();
  
  f->srate     = srate;
  f->chN       = chN;
  f->maxBinN_V = mem::allocZ<unsigned>(chN);
  f->binN_V    = mem::allocZ<unsigned>(chN);
  f->hopSmpN_V = mem::allocZ<unsigned>(chN); 
  f->magV      = mem::allocZ<fd_real_t*>(chN);
  f->phsV      = mem::allocZ<fd_real_t*>(chN);
  f->hzV       = mem::allocZ<fd_real_t*>(chN);
  f->readyFlV  = mem::allocZ<bool>(chN);

  vop::copy( f->binN_V, binN_V, chN );
  vop::copy( f->maxBinN_V, maxBinN_V, chN );
  vop::copy( f->hopSmpN_V, hopSmpN_V, chN );  
  
  if( magV != nullptr || phsV != nullptr || hzV != nullptr )
  {
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {      
      f->magV[ chIdx ] = (fd_real_t*)magV[chIdx];
      f->phsV[ chIdx ] = (fd_real_t*)phsV[chIdx];
      f->hzV[  chIdx ] = (fd_real_t*)hzV[chIdx];
    }
  }
  else
  {
    unsigned maxTotalBinsN = vop::sum( maxBinN_V, chN );
        
    fd_real_t* buf       = mem::allocZ<fd_real_t>( kFbufVectN * maxTotalBinsN );
    fd_real_t* m         = buf;
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {   
      f->magV[chIdx] = m + 0 * f->binN_V[chIdx];
      f->phsV[chIdx] = m + 1 * f->binN_V[chIdx];
      f->hzV[ chIdx] = m + 2 * f->binN_V[chIdx];
      m += f->maxBinN_V[chIdx];
      assert( m <= buf + kFbufVectN * maxTotalBinsN );
    }

    f->buf = buf;
      
  }

  return f;  
}


cw::flow::fbuf_t*  cw::flow::fbuf_create( srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_real_t** magV, const fd_real_t** phsV, const fd_real_t** hzV )
{
  unsigned maxBinN_V[ chN ];
  unsigned binN_V[ chN ];
  unsigned hopSmpN_V[ chN ];

  vop::fill( maxBinN_V, chN, maxBinN );
  vop::fill( binN_V, chN, binN );
  vop::fill( hopSmpN_V, chN, binN );
  return fbuf_create( srate, chN, maxBinN_V, binN_V, hopSmpN_V, magV, phsV, hzV );

}



void cw::flow::fbuf_destroy( fbuf_t*& fbuf )
{
  if( fbuf == nullptr )
    return;

  mem::release( fbuf->maxBinN_V );
  mem::release( fbuf->binN_V );
  mem::release( fbuf->hopSmpN_V);
  mem::release( fbuf->magV);
  mem::release( fbuf->phsV);
  mem::release( fbuf->hzV);
  mem::release( fbuf->buf);
  mem::release( fbuf->readyFlV);
  mem::release( fbuf);
}

cw::flow::fbuf_t*  cw::flow::fbuf_duplicate( const fbuf_t* src )
{
  fbuf_t* fbuf = fbuf_create( src->srate, src->chN, src->maxBinN_V, src->binN_V, src->hopSmpN_V );
  
  for(unsigned i=0; i<fbuf->chN; ++i)
  {
    fbuf->maxBinN_V[i] = src->maxBinN_V[i];
    fbuf->binN_V[i]    = src->binN_V[i];
    fbuf->hopSmpN_V[i] = src->hopSmpN_V[i]; 

    vop::copy( fbuf->magV[i], src->magV[i], fbuf->binN_V[i] );
    vop::copy( fbuf->phsV[i], src->phsV[i], fbuf->binN_V[i] );
    vop::copy( fbuf->hzV[i],  src->hzV[i],  fbuf->binN_V[i] );    
  }
  return fbuf;
}


unsigned cw::flow::value_type_label_to_flag( const char* s )
{
  unsigned flags = labelToId(typeLabelFlagsA,s,kInvalidTFl);
  if( flags == kInvalidTFl )
    cwLogError(kInvalidArgRC,"Invalid type flag: '%s'",cwStringNullGuard(s));
    
  return flags;
}


cw::flow::class_desc_t* cw::flow::class_desc_find( flow_t* p, const char* label )
{
  for(unsigned i=0; i<p->classDescN; ++i)
    if( textCompare(p->classDescA[i].label,label) == 0 )
      return p->classDescA + i;
  return nullptr;
}

cw::flow::var_desc_t* cw::flow::var_desc_find( class_desc_t* cd, const char* label )
{
  var_desc_t* vd = cd->varDescL;
      
  for(; vd != nullptr; vd=vd->link )
    if( textCompare(vd->label,label) == 0 )
      return vd;
  return nullptr;
}

cw::rc_t cw::flow::var_desc_find( class_desc_t* cd, const char* label, var_desc_t*& vdRef )
{
  if((vdRef = var_desc_find(cd,label)) == nullptr )
    return cwLogError(kInvalidArgRC,"The variable desc. named '%s' could not be found on the class '%s'.",label,cd->label);
  return kOkRC;
}


void cw::flow::class_dict_print( flow_t* p )
{
  for(unsigned i=0; i<p->classDescN; ++i)
  {
    class_desc_t* cd = p->classDescA + i;
    var_desc_t*   vd = cd->varDescL;
    printf("%s\n",cwStringNullGuard(cd->label));
        
    for(; vd!=nullptr; vd=vd->link)
    {
      const char* srcFlStr    = vd->flags & kSrcVarFl    ? "src"    : "   ";
      const char* srcOptFlStr = vd->flags & kSrcOptVarFl ? "srcOpt" : "      ";
          
      printf("  %10s 0x%08x %s %s %s\n", cwStringNullGuard(vd->label), vd->type, srcFlStr, srcOptFlStr, cwStringNullGuard(vd->docText) );
    }
  }
}

void cw::flow::network_print( flow_t* p )
{
  for(instance_t* inst = p->network_head; inst!=nullptr; inst=inst->link)
    instance_print(inst);
}

cw::flow::instance_t* cw::flow::instance_find( flow_t* p, const char* inst_label )
{
  for(instance_t* inst = p->network_head; inst!=nullptr; inst=inst->link )
    if( textCompare(inst_label,inst->label) == 0 )
      return inst;

  return nullptr;
}

cw::rc_t cw::flow::instance_find( flow_t* p, const char* inst_label, instance_t*& instPtrRef )
{
  rc_t rc = kOkRC;
      
  if((instPtrRef = instance_find(p,inst_label)) != nullptr )
    return rc;
      
  return cwLogError(kInvalidArgRC,"The instance '%s' was not found.", inst_label );
}

cw::flow::external_device_t* cw::flow::external_device_find( flow_t* p, const char* device_label, unsigned typeId, unsigned inOrOutFl )
{
  for(unsigned i=0; i<p->deviceN; ++i)
    if( cw::textIsEqual(p->deviceA[i].label,device_label) && p->deviceA[i].typeId==typeId && cwIsFlag(p->deviceA[i].flags,inOrOutFl ))
      return p->deviceA + i;
  
  cwLogError(kInvalidArgRC,"The %s device named '%s' could not be found.", cwIsFlag(inOrOutFl,kInFl) ? "in" : "out", device_label );
  
  return nullptr;
}

void cw::flow::instance_print( instance_t* inst )
{
  printf("%s\n", inst->label);
  for(variable_t* var = inst->varL; var!=nullptr; var=var->var_link)
    if( var->chIdx == kAnyChIdx )
      for(variable_t* v0 = var; v0!=nullptr; v0=v0->ch_link)
        _var_print(v0);      
  
  if( inst->class_desc->members->report )
    inst->class_desc->members->report( inst );
}



void cw::flow::_var_destroy( variable_t* var )
{
  if( var != nullptr )
  {
    for(unsigned i=0; i<kLocalValueN; ++i)
      _value_release(var->local_value+i);
    mem::release(var->label);
    mem::release(var);
  }
}


cw::rc_t cw::flow::var_create( instance_t* inst, const char* var_label, unsigned id, unsigned chIdx, const object_t* value_cfg, variable_t*& varRef )
{
  rc_t rc = kOkRC;
 
  rc = _var_create( inst, var_label, id, chIdx, value_cfg, varRef );

  return rc;
}

cw::rc_t  cw::flow::var_channelize( instance_t* inst, const char* var_label, unsigned chIdx, const object_t* value_cfg, unsigned vid, variable_t*& varRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  variable_t* base_var = nullptr;
  varRef = nullptr;

  if((base_var = _var_find_on_label_and_ch( inst, var_label, kAnyChIdx)) == nullptr)
  {
    rc = cwLogError(kInvalidStateRC,"The base ('any') channel variable could not be located on '%s.%s'.",inst->label,var_label);
    goto errLabel;
  }

  
  // locate the variable with the stated chIdx
  var = _var_find_on_label_and_ch( inst, var_label, chIdx );
  

  // 'src' variables cannot be channelized
  if( cwIsFlag(base_var->varDesc->flags,kSrcVarFl) )
  {
    rc = cwLogError(rc,"'src' variables cannot be channelized.");
    goto errLabel;
  }

  // if the requested var was not found then create a new variable with the requested channel index
  if( var == nullptr && chIdx != kAnyChIdx )
  {
    // create the channelized var
    if((rc = _var_create( inst, var_label, vid, chIdx, value_cfg, var )) != kOkRC )
      goto errLabel;

    // if no value was set then set the value from the 'any' channel
    if( value_cfg == nullptr )
    {
      // Set the value of the new variable to the value of the 'any' channel
      _value_duplicate( var->local_value[ var->local_value_idx], base_var->local_value[ base_var->local_value_idx ] );

      // If the 'any' channel value was set to point to it's local value then do same with this value
      if( base_var->local_value + base_var->local_value_idx == base_var->value )
        var->value = var->local_value + var->local_value_idx;
    }
    
  }
  else
  {
    
    // a correctly channelized var was found - but we still may need to set the value
    if( value_cfg != nullptr )
    {
      rc = _set_var_value_from_cfg( var, value_cfg );
    }
    else
    {
      cwLogWarning("An existing var (%s.%s ch:%i) was specified for channelizing but no value was provided.", inst->label, var_label, chIdx );
    }
  }

  assert( var != nullptr );
  varRef = var;
  
 errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Channelize failed for variable '%s' on instance '%s' ch:%i.", var_label, inst->label, chIdx );
  
  return rc;
}

bool cw::flow::var_exists( instance_t* inst, const char* label, unsigned chIdx )
{ return _var_find_on_label_and_ch(inst,label,chIdx) != nullptr; }

bool cw::flow::var_has_value( instance_t* inst, const char* label, unsigned chIdx )
{
  variable_t* varPtr = nullptr;
  rc_t rc;
  
  if((rc = var_find( inst, label, chIdx, varPtr )) != kOkRC )
    return false;

  return varPtr->value != nullptr;
}


cw::rc_t cw::flow::var_find( instance_t* inst, unsigned vid, unsigned chIdx, variable_t*& varRef )
{
  rc_t        rc  = kOkRC;
  unsigned    idx = kInvalidIdx;
  variable_t* var = nullptr;
      
  varRef = nullptr;

  // if the varMapA[] has not yet been formed (we are inside the instance constructor) then do a slow lookup of the variable
  if( inst->varMapA == nullptr )
  {
    if((rc = _var_find_on_vid_and_ch(inst,vid,chIdx,var)) != kOkRC )
      goto errLabel;
  }
  else
  {
    // otherwise do a fast lookup using inst->varMapA[]
    if((rc = _var_map_id_to_index(inst, vid, chIdx, idx )) == kOkRC && (idx != kInvalidIdx ))
      var = inst->varMapA[idx];
    else
    {
      rc = cwLogError(kInvalidIdRC,"The index of variable vid:%i chIdx:%i on instance '%s' could not be calculated and the variable value could not be retrieved.", vid, chIdx, inst->label);
      goto errLabel;
    }
  }

  // if we get here var must be non-null
  assert( var != nullptr && rc == kOkRC );
  varRef = var;
  
 errLabel:
      
  return rc;
}



cw::rc_t cw::flow::var_find( instance_t* inst, const char* label, unsigned chIdx, variable_t*& vRef )
{
  variable_t* var;
  vRef = nullptr;
  
  if((var = _var_find_on_label_and_ch(inst,label,chIdx)) != nullptr )
  {
    vRef = var;
    return kOkRC;
  }

  return cwLogError(kInvalidIdRC,"The instance '%s' does not have a variable named '%s'.", inst->label, label );  
}

cw::rc_t cw::flow::var_find( instance_t* inst, const char* label, unsigned chIdx, const variable_t*& vRef )
{
  variable_t* v = nullptr;
  rc_t        rc = var_find(inst,label,chIdx,v);
  vRef = v;
  return rc;
}

cw::rc_t  cw::flow::var_channel_count( instance_t* inst, const char* label, unsigned& chCntRef )
{
  rc_t rc = kOkRC;
  const variable_t* var= nullptr;
  if((rc = var_find(inst,label,kAnyChIdx,var)) != kOkRC )
    return cwLogError(rc,"Channel count was not available because the variable '%s.%s' does not exist.",cwStringNullGuard(inst->label),cwStringNullGuard(label));

  return var_channel_count(var,chCntRef);
}

cw::rc_t  cw::flow::var_channel_count( const variable_t* var, unsigned& chCntRef )
{
  rc_t rc = kOkRC;
  const variable_t* v;
  
  chCntRef = 0;
  
  if((rc = var_find( var->inst, var->label, kAnyChIdx, v )) != kOkRC )
  {
    rc = cwLogError(kInvalidStateRC,"The base channel variable instance could not be found for the variable '%s.%s'.",var->inst->label,var->label);
    goto errLabel;
  }

  for(v = v->ch_link; v!=nullptr; v=v->ch_link)
    chCntRef += 1;

 errLabel:
  return rc;
}



cw::rc_t cw::flow::var_register( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, const object_t* value_cfg, variable_t*& varRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;

  varRef = nullptr;

  // TODO: check for duplicate 'vid'-'chIdx' pairs on this instance
  // The concatenation of 'vid' and 'chIdx' should be unique 

  // if an exact match to label/chIdx was found
  if((var = _var_find_on_label_and_ch(inst,var_label,chIdx)) != nullptr )
  {
    // if a value was given - then update the value
    if( value_cfg != nullptr )
      if((rc = _set_var_value_from_cfg( var, value_cfg )) != kOkRC )
        goto errLabel;    
  }
  else // an exact match was not found - channelize the variable
  {
    if((rc = var_channelize(inst,var_label,chIdx,value_cfg,vid,var)) != kOkRC )
      goto errLabel;
  }

  var->vid = vid;
  varRef   = var;

  if((var = _var_find_on_label_and_ch(inst,var_label,kAnyChIdx)) != nullptr )
    var->vid = vid;
  else
    rc = cwLogError(kInvalidStateRC,"The variable '%s' instance '%s' has no base channel.", var_label, inst->label, chIdx);
  
 errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Registration failed on variable '%s' instance '%s' ch: %i.", var_label, inst->label, chIdx);
  
  return rc;
}


cw::rc_t cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, variable_t*& varRef )
{
  return var_register( inst, var_label, vid, chIdx, nullptr, varRef );
}

cw::rc_t        cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned frameN )
{
  rc_t rc = kOkRC;
  abuf_t* abuf;
  
  if((abuf = abuf_create( srate, chN, frameN )) == nullptr )
    return cwLogError(kOpFailRC,"abuf create failed on instance:'%s' variable:'%s'.", inst->label, var_label);

  if((rc = _var_register_and_set( inst, var_label, vid, chIdx, abuf )) != kOkRC )
    abuf_destroy(abuf);

  return rc;
}

cw::rc_t cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_real_t** magV, const fd_real_t** phsV, const fd_real_t** hzV )
{
  rc_t rc = kOkRC;
  fbuf_t* fbuf;
  if((fbuf = fbuf_create( srate, chN, maxBinN_V, binN_V, hopSmpN_V, magV, phsV, hzV )) == nullptr )
    return cwLogError(kOpFailRC,"fbuf create failed on instance:'%s' variable:'%s'.", inst->label, var_label);

  if((rc = _var_register_and_set( inst, var_label, vid, chIdx, fbuf )) != kOkRC )
    fbuf_destroy(fbuf);

  return rc;
}

cw::rc_t cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_real_t** magV, const fd_real_t** phsV, const fd_real_t** hzV )
{
  unsigned maxBinN_V[ chN ];
  unsigned binN_V[ chN ];
  unsigned hopSmpN_V[ chN ];
  vop::fill(maxBinN_V,chN,maxBinN);
  vop::fill(binN_V,chN,binN);
  vop::fill(hopSmpN_V,chN, hopSmpN );
  return var_register_and_set(inst,var_label,vid,chIdx,srate, chN, maxBinN_V, binN_V, hopSmpN_V, magV, phsV, hzV);
}


cw::rc_t  cw::flow::var_get( const variable_t* var, bool& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, uint_t& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, int_t& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, float& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, double& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, const char*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, const abuf_t*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( variable_t* var, abuf_t*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, const fbuf_t*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( variable_t* var, fbuf_t*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, bool val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_to_set(inst, vid, chIdx, kBoolTFl, var )) == kOkRC )
    _var_set_driver( var, kBoolTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, uint_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_to_set(inst, vid, chIdx, kUIntTFl, var )) == kOkRC )
    _var_set_driver( var, kUIntTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, int_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_to_set(inst, vid, chIdx, kIntTFl, var )) == kOkRC )
    _var_set_driver( var, kIntTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, float val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_to_set(inst, vid, chIdx, kFloatTFl, var )) == kOkRC )
    _var_set_driver( var, kFloatTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, double val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_to_set(inst, vid, chIdx, kDoubleTFl, var )) == kOkRC )
    _var_set_driver( var, kDoubleTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, const char* val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_to_set(inst, vid, chIdx, kStringTFl, var )) == kOkRC )
    _var_set_driver( var, kStringTFl, val );
  
  return rc;    
}

const cw::flow::preset_t* cw::flow::class_preset_find( class_desc_t* cd, const char* preset_label )
{
  const preset_t* pr;
  for(pr=cd->presetL; pr!=nullptr; pr=pr->link)
    if( textCompare(pr->label,preset_label) == 0 )
      return pr;
  
  return nullptr;
}

