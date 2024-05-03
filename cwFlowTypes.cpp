#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwMath.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDspTypes.h" // real_t, sample_t
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"


namespace cw
{
  namespace flow
  {
    idLabelPair_t typeLabelFlagsA[] = {
      
      { kBoolTFl,  "bool" },
      { kUIntTFl,  "uint" },
      { kIntTFl,   "int", },
      { kFloatTFl, "float"},
      { kDoubleTFl,"double"},
      
      { kBoolMtxTFl,  "bool_mtx" },
      { kUIntMtxTFl,  "uint_mtx" },
      { kIntMtxTFl,   "int_mtx"  },
      { kFloatMtxTFl, "float_mtx" },
      { kDoubleMtxTFl,"double_mtx" },
      
      { kABufTFl,   "audio" },
      { kFBufTFl,   "spectrum" },
      { kMBufTFl,   "midi" },
      { kStringTFl, "string" },
      { kTimeTFl,   "time" },
      { kCfgTFl,    "cfg" },

      // alias types to map to cwDspTypes.h
      { kFloatTFl, "srate"},
      { kFloatTFl, "sample"},
      { kFloatTFl, "coeff"},
      { kDoubleTFl, "ftime" },

      { kNumericTFl, "numeric" },
      { kAllTFl,     "all" },

      { kRuntimeTFl, "runtime" },

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
        
      switch( v->tflag & kTypeMask )
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

        case kMBufTFl:
          mbuf_destroy( v->u.mbuf );
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

        case kCfgTFl:
          break;

        default:
          assert(0);
          break;
      }

      v->tflag = kInvalidTFl;
    }

    void _value_duplicate( value_t& dst, const value_t& src )
    {        
      switch( src.tflag & kTypeMask )
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
          dst.tflag = src.tflag;
          break;
          
        case kFBufTFl:
          dst.u.fbuf = src.u.fbuf == nullptr ? nullptr : fbuf_duplicate(src.u.fbuf);
          dst.tflag = src.tflag;
          break;

        case kMBufTFl:
          dst.u.mbuf = src.u.mbuf == nullptr ? nullptr : mbuf_duplicate(src.u.mbuf);
          dst.tflag = src.tflag;
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
          dst.tflag = src.tflag;
          break;
                    
        case kTimeTFl:
          assert(0);
          break;

        case kCfgTFl:
          dst = src;
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
        
      switch( v->tflag & kTypeMask )
      {
        case kInvalidTFl:
          break;
          
        case kBoolTFl:  printf("b:%s ", v->u.b ? "true" : "false" ); break;          
        case kUIntTFl:  printf("u:%i ", v->u.u ); break;
        case kIntTFl:   printf("i:%i ", v->u.i ); break;          
        case kFloatTFl: printf("f:%f ", v->u.f ); break;
        case kDoubleTFl:printf("d:%f ", v->u.d ); break;
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

        case kMBufTFl:
          if( v->u.mbuf == nullptr )
            printf("mbuf: <null>");
          else
          {
            printf("mbuf: cnt: %i", v->u.mbuf->msgN );
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

        case kCfgTFl:
          if( v->u.cfg != nullptr )
            v->u.cfg->print();
          break;
          
        default:
          assert(0);
          break;
      }

    }
    

    rc_t _val_get( const value_t* val, bool& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b; break;
        case kUIntTFl:   valRef = val->u.u!=0; break;
        case kIntTFl:    valRef = val->u.i!=0; break;
        case kFloatTFl:  valRef = val->u.f!=0; break;
        case kDoubleTFl: valRef = val->u.d!=0; break;        
        default:
          rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a bool.",_typeFlagToLabel(val->tflag),val->tflag);
      }
      return rc;
    }

    rc_t _val_set( value_t* val, bool v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   val->u.b=v; break;
        case kUIntTFl:   val->u.u=v; break;
        case kIntTFl:    val->u.i=v; break;
        case kFloatTFl:  val->u.f=v; break;
        case kDoubleTFl: val->u.d=v; break;
        case kInvalidTFl:
          val->u.b   = v;
          val->tflag = kBoolTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A bool could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
    
    rc_t _val_get( const value_t* val, uint_t& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = val->u.u; break;
        case kIntTFl:    valRef = val->u.i; break;
        case kFloatTFl:  valRef = (uint_t)val->u.f; break;
        case kDoubleTFl: valRef = (uint_t)val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a uint.",_typeFlagToLabel(val->tflag),val->tflag);
      }
      return rc;
    }

    rc_t _val_set( value_t* val, uint_t v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   val->u.b=v!=0; break;
        case kUIntTFl:   val->u.u=v; break;
        case kIntTFl:    val->u.i=v; break;
        case kFloatTFl:  val->u.f=v; break;
        case kDoubleTFl: val->u.d=v; break;
        case kInvalidTFl:
          val->u.u  = v;
          val->tflag = kUIntTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A uint could not be converted to a  %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
    
    rc_t _val_get( const value_t* val, int_t& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = (int_t)val->u.u; break;
        case kIntTFl:    valRef = val->u.i; break;
        case kFloatTFl:  valRef = (int_t)val->u.f; break;
        case kDoubleTFl: valRef = (int_t)val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an int.",_typeFlagToLabel(val->tflag),val->tflag);
          
      }
      return rc;
    }

    rc_t _val_set( value_t* val, int_t v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   val->u.b=v!=0; break;
        case kUIntTFl:   val->u.u=v; break;
        case kIntTFl:    val->u.i=v; break;
        case kFloatTFl:  val->u.f=v; break;
        case kDoubleTFl: val->u.d=v; break;
        case kInvalidTFl:
          val->u.i   = v;
          val->tflag = kIntTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"An int could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
    

    rc_t _val_get( const value_t* val, float& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = (float)val->u.u; break;
        case kIntTFl:    valRef = (float)val->u.i; break;
        case kFloatTFl:  valRef = (float)val->u.f; break;
        case kDoubleTFl: valRef = (float)val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a float.",_typeFlagToLabel(val->tflag),val->tflag);
      }
      return rc;
    }

    rc_t _val_set( value_t* val, float v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   val->u.b=v!=0; break;
        case kUIntTFl:   val->u.u=(unsigned)v; break;
        case kIntTFl:    val->u.i=(int)v; break;
        case kFloatTFl:  val->u.f=v; break;
        case kDoubleTFl: val->u.d=v; break;
        case kInvalidTFl:
          val->u.f   = v;
          val->tflag = kFloatTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A float could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
    
    rc_t _val_get( const value_t* val, double& valRef )
    {
      rc_t rc = kOkRC;
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
        case kUIntTFl:   valRef = (double)val->u.u; break;
        case kIntTFl:    valRef = (double)val->u.i; break;
        case kFloatTFl:  valRef = (double)val->u.f; break;
        case kDoubleTFl: valRef =         val->u.d; break;
        default:
          rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a double.",_typeFlagToLabel(val->tflag),val->tflag);
      }
      return rc;
    }

    rc_t _val_set( value_t* val, double v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kBoolTFl:   val->u.b=v!=0; break;
        case kUIntTFl:   val->u.u=(unsigned)v; break;
        case kIntTFl:    val->u.i=(int)v; break;
        case kFloatTFl:  val->u.f=(float)v; break;
        case kDoubleTFl: val->u.d=v; break;
        case kInvalidTFl:
          val->u.d   = v;
          val->tflag = kDoubleTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A double could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
    
    rc_t _val_get( const value_t* val, const char*& valRef )
    {
      rc_t rc = kOkRC;
      if( cwIsFlag(val->tflag & kTypeMask, kStringTFl) )
        valRef = val->u.s;
      else
      {
        rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a string.",_typeFlagToLabel(val->tflag),val->tflag);        
        valRef = nullptr;
      }
      
      return rc;
    }

    rc_t _val_set( value_t* val, const char* v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kStringTFl:
          val->u.s=mem::duplStr(v); break;
          
        case kInvalidTFl:
          val->u.s   = mem::duplStr(v);
          val->tflag = kStringTFl;
          break;
        default:
          rc = cwLogError(kTypeMismatchRC,"A string could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
    
    rc_t _val_get( value_t* val, abuf_t*& valRef )
    {
      rc_t rc = kOkRC;
      if( cwIsFlag(val->tflag & kTypeMask, kABufTFl) )
        valRef = val->u.abuf;
      else
      {
        rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an abuf.",_typeFlagToLabel(val->tflag),val->tflag);        
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

    rc_t _val_set( value_t* val, abuf_t* v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kABufTFl:
          val->u.abuf=v;
          break;
          
        case kInvalidTFl:
          val->u.abuf=v;
          val->tflag = kABufTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A audio signal could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
        
    rc_t _val_get( value_t* val, fbuf_t*& valRef )
    {
      rc_t rc = kOkRC;
      if( cwIsFlag(val->tflag & kTypeMask, kFBufTFl) )
        valRef = val->u.fbuf;
      else
      {
        valRef = nullptr;
        rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an fbuf.",_typeFlagToLabel(val->tflag),val->tflag);        
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

    rc_t _val_set( value_t* val, fbuf_t* v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kFBufTFl:
          val->u.fbuf=v;
          break;
          
        case kInvalidTFl:
          val->u.fbuf=v;
          val->tflag = kFBufTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A spectrum signal could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
        
    rc_t _val_get( value_t* val, mbuf_t*& valRef )
    {
      rc_t rc = kOkRC;
      if( cwIsFlag(val->tflag & kTypeMask, kMBufTFl) )
        valRef = val->u.mbuf;
      else
      {
        valRef = nullptr;
        rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an mbuf.",_typeFlagToLabel(val->tflag),val->tflag);        
      }
      return rc;
    }

    rc_t _val_set( value_t* val, mbuf_t* v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kMBufTFl:
          val->u.mbuf=v;
          break;
          
        case kInvalidTFl:
          val->u.mbuf=v;
          val->tflag = kMBufTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A MIDI signal could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }
    
    
    rc_t _val_get( value_t* val, const mbuf_t*& valRef )
    {
      mbuf_t* non_const_val;
      rc_t rc = kOkRC;
      if((rc = _val_get(val,non_const_val)) == kOkRC )
        valRef = non_const_val;
      return rc;        
    }

    rc_t _val_get( value_t* val, const object_t*& valRef )
    {
      rc_t rc = kOkRC;
      
      if( cwIsFlag(val->tflag & kTypeMask, kCfgTFl) )
        valRef = val->u.cfg;
      else
      {
        valRef = nullptr;
        rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a cfg.",_typeFlagToLabel(val->tflag),val->tflag);        
        
      }
      return rc;        
    }
    
    rc_t _val_set( value_t* val, const object_t* v )
    {
      rc_t rc = kOkRC;
      
      switch( val->tflag & kTypeMask )
      {
        case kCfgTFl:
          val->u.cfg=v;
          break;
          
        case kInvalidTFl:
          val->u.cfg=v;
          val->tflag = kCfgTFl;
          break;

        default:
          rc = cwLogError(kTypeMismatchRC,"A cfg. could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
      }
      
      return rc;
    }

    template< typename T >
    rc_t _val_get_driver( const variable_t* var, T& valRef )
    {
      if( var == nullptr )
        return cwLogError(kInvalidArgRC,"Cannnot get the value of a non-existent variable.");
      
      if( var->value == nullptr )
        return cwLogError(kInvalidStateRC,"No value has been assigned to the variable: %s:%i.%s:%i ch:%i.",cwStringNullGuard(var->inst->label),var->inst->label_sfx_id,cwStringNullGuard(var->label),var->label_sfx_id,var->chIdx);

      return _val_get(var->value,valRef);
    
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
      return cwLogError(kInvalidIdRC,"The variable matching id:%i ch:%i on instance '%s:%i' could not be found.", vid, chIdx, inst->label, inst->label_sfx_id);
    }

    // Variable lookup: Exact match on label and chIdx
    variable_t* _var_find_on_label_and_ch( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned chIdx )
    {
      for(variable_t* var = inst->varL; var!=nullptr; var=var->var_link)
      {
        // the variable vid and chIdx should form a unique pair
        if( var->label_sfx_id==sfx_id && textCompare(var->label,var_label)==0 && var->chIdx == chIdx )
          return var;
      }
      
      return nullptr;
    }
    
    rc_t _var_find_on_label_and_ch( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned chIdx, variable_t*& var_ref )
    {
      rc_t rc = kOkRC;
      if((var_ref = _var_find_on_label_and_ch(inst,var_label,sfx_id,chIdx)) == nullptr )
        rc = cwLogError(kEleNotFoundRC,"The variable '%s:%i' cannot be found on the proc:%s.",cwStringNullGuard(var_label),sfx_id,cwStringNullGuard(inst->label));
      return rc;
    }
    
    rc_t _validate_var_assignment( variable_t* var, unsigned typeFl )
    {
      if( cwIsFlag(var->varDesc->flags, kSrcVarFl ) )
        return cwLogError(kInvalidStateRC, "The variable '%s:%i' on instance '%s:%i' cannot be set because it is a 'src' variable.", var->label, var->label_sfx_id, var->inst->label,var->inst->label_sfx_id);
      /*
      if( !cwIsFlag(var->varDesc->type, typeFl ) )
        return cwLogError(kTypeMismatchRC, "The variable '%s:%i' on instance '%s:%i' is not a  '%s'.", var->label, var->label_sfx_id, var->inst->label, var->inst->label_sfx_id, _typeFlagToLabel( typeFl ));
      */
      
      return kOkRC;
    }

    void _var_print_addr( const char* title, const variable_t* v )
    { printf("%s:%s:%i.%s:%i ",title,v->inst->label,v->inst->label_sfx_id,v->label,v->label_sfx_id); }

    void _var_print( const variable_t* var )
    {
      const char* conn_label  = is_connected_to_source_proc(var) ? "extern" : "      ";
    
      printf("  %20s:%5i id:%4i ch:%3i : %s  : ", var->label, var->label_sfx_id, var->vid, var->chIdx, conn_label );
    
      if( var->value == nullptr )
        _value_print( &var->local_value[0] );
      else
        _value_print( var->value );

      if( var->src_var != nullptr )
        printf(" src:%s:%i.%s:%i",var->src_var->inst->label,var->src_var->inst->label_sfx_id,var->src_var->label,var->src_var->label_sfx_id);

      if( var->dst_head != nullptr )
      {
        for(variable_t* v = var->dst_head; v!=nullptr; v=v->dst_link)
          printf(" dst:%s:%i.%s:%i",v->inst->label,v->inst->label_sfx_id,v->label,v->label_sfx_id);
      }

      printf("\n");    
    }
    
    
    rc_t _var_broadcast_new_value( variable_t* var )
    {
      rc_t rc = kOkRC;
      
      // notify each connected var that the value has changed
      for(variable_t* con_var = var->dst_head; con_var!=nullptr; con_var=con_var->dst_link)
      {
        // the var->local_value[] slot used by the source variable may have changed - update the destination variable
        // so that it points to the correct value.
        con_var->value = var->value;
        
        cwLogMod("%s:%i %s:%i -> %s:%i %s:%i",
                 var->inst->label,var->inst->label_sfx_id,
                 var->label,var->label_sfx_id,
                 con_var->inst->label,con_var->inst->label_sfx_id,
                 con_var->label,con_var->label_sfx_id );

        // Call the value() function on the connected variable
        if((rc = var_call_custom_value_func(con_var)) != kOkRC )
          break;
               
      }
      return rc;
    }


    
    // 'argTypeFlag' is the type (tflag) of 'val'.
    template< typename T >    
    rc_t _var_set_template( variable_t* var, unsigned argTypeFlag, T val )
    {
      rc_t rc = kOkRC;
      
      // it is not legal to set the value of a variable that is connected to a 'source' variable.
      if( var->src_var != nullptr )
        return cwLogError(kInvalidStateRC, "The variable '%s:%i %s:%i' cannot be set because it is connected to a source variable.", var->inst->label,var->inst->label_sfx_id, var->label, var->label_sfx_id);      

      
      // var->type is the allowable type for this var's value.
      // It may be set to kInvalidTFl if the type has not yet been determined.
      unsigned value_type_flag    = var->type;

      // Pick the slot in local_value[] that we will use to try out this new value.
      unsigned next_local_value_idx = (var->local_value_idx + 1) % kLocalValueN;
      
      // store the pointer to the current value of this variable
      value_t* original_value     = var->value;
      unsigned original_value_idx = var->local_value_idx;
      
      
      // release the previous value in the next slot
      _value_release(&var->local_value[next_local_value_idx]);

      // if the value type of this variable has not been established
      if( value_type_flag == kInvalidTFl  )
      {
        // if the var desc is a single type then use that ....
        if( math::isPowerOfTwo(var->varDesc->type) )
        {
          value_type_flag = var->varDesc->type;
        }
        else // ... Otherwise select a type from the one of the possible flags given by the var desc
        {
          value_type_flag = var->varDesc->type & argTypeFlag;
        }

        // if the incoming type is not in the set of allowable types then it is an error
        if( value_type_flag == 0  )
        {
          rc = cwLogError(kTypeMismatchRC,"The type 0x%x is not valid for the variable: %s:%i %s:%i type:0x%x.",
                          argTypeFlag,var->inst->label,var->inst->label_sfx_id,var->label,var->label_sfx_id,var->varDesc->type);
          goto errLabel;
        }
      }
      
      // set the type of the LHS to force the incoming value to be coerced to this type
      var->local_value[ next_local_value_idx ].tflag = value_type_flag;

      // set the new local value in var->local_value[next_local_value_idx]
      if((rc = _val_set(var->local_value + next_local_value_idx, val )) != kOkRC )
      {
        rc = cwLogError(rc,"Value set failed on '%s:%i %s:%i",var->inst->label,var->inst->label_sfx_id,var->label,var->label_sfx_id);
        goto errLabel;
      }

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
        rc = var_call_custom_value_func( var );
      }

      //printf("%p set: %s:%s  0x%x\n",var->value, var->inst->label,var->label,var->value->tflag);

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
    
    
    

    // 'valueTypeFlag' is the type (tflag) of 'value'
    template< typename T >
    rc_t _var_set_driver( variable_t* var, unsigned valueTypeFlag, T value )
    {
      rc_t rc;

      // if this variable is fed from the output of an external proc - then it's local value cannot be set
      if(is_connected_to_source_proc(var)   )
      {
        return cwLogError(kInvalidStateRC,"Cannot set the value on a connected variables.");
      }

      // if this assignment targets a specific channel ...
      if( var->chIdx != kAnyChIdx )
      {
        rc = _var_set_template( var, valueTypeFlag, value ); // ...  then set it alone
      }
      else // ... otherwise set all channels.
      {
        for(; var!=nullptr; var=var->ch_link)
          if((rc = _var_set_template( var, valueTypeFlag, value )) != kOkRC)
            break;
      }

      if(rc != kOkRC )
        rc = cwLogError(rc,"Variable value set failed on '%s:%i %s:%i",cwStringNullGuard(var->inst->label),var->inst->label_sfx_id,cwStringNullGuard(var->label),var->label_sfx_id);
      
      return rc;
    }

    
    rc_t  _var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, abuf_t* abuf )
    {
      rc_t rc;
      variable_t* var = nullptr;
      if((rc = var_register_and_set( inst, var_label, sfx_id, vid, chIdx, var)) != kOkRC )
        return rc;

      if( var != nullptr )
        _var_set_driver( var, kABufTFl, abuf );

      return rc;
    }

    rc_t  _var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, mbuf_t* mbuf )
    {
      rc_t rc;
      variable_t* var = nullptr;
      if((rc = var_register_and_set( inst, var_label, sfx_id, vid, chIdx, var)) != kOkRC )
        return rc;

      if( var != nullptr )
        _var_set_driver( var, kMBufTFl, mbuf );

      return rc;
    }
    
    rc_t  _var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, fbuf_t* fbuf )
    {
      rc_t rc;
      variable_t* var = nullptr;
      if((rc = var_register_and_set( inst, var_label, sfx_id, vid, chIdx, var)) != kOkRC )
        return rc;

      if( var != nullptr )
        _var_set_driver( var, kFBufTFl, fbuf );

      return rc;
    }


    rc_t  _var_map_id_to_index(  instance_t* inst, unsigned vid, unsigned chIdx, unsigned& idxRef )
    {
      unsigned idx = vid * inst->varMapChN + (chIdx == kAnyChIdx ? 0 : (chIdx+1));

      // verify that the map idx is valid
      if( idx >= inst->varMapN )
        return cwLogError(kAssertFailRC,"The variable map positioning location %i is out of the range %i on instance '%s:%i' vid:%i ch:%i.", idx, inst->varMapN, inst->label,inst->label_sfx_id,vid,chIdx);

      idxRef = idx;
  
      return kOkRC;
    }

    rc_t  _var_map_label_to_index(  instance_t* inst, const char* var_label, unsigned sfx_id, unsigned chIdx, unsigned& idxRef )
    {
      rc_t        rc  = kOkRC;
      variable_t* var = nullptr;
  
      idxRef = kInvalidIdx;
      if((rc = var_find(inst, var_label, sfx_id, chIdx, var )) == kOkRC)
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
      
      if((base_var = _var_find_on_label_and_ch( inst, new_var->label, new_var->label_sfx_id, kAnyChIdx )) == nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The base channel variable does not exist for '%s:%i.%s:%i'. This is an illegal state.", inst->label, inst->label_sfx_id, new_var->label, new_var->label_sfx_id );
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

    rc_t _var_set_type( variable_t* var, unsigned type_flag )
    {
      rc_t rc = kOkRC;

      if( cwIsNotFlag(var->classVarDesc->type,kRuntimeTFl) )
      {
        rc = cwLogError(kOpFailRC,"It is invalid to change the type of a statically (non-runtime) type variable.");
        goto errLabel;
      }

      // Duplicate the varDesc with the 'type' field set to type_flag      
      if( var->localVarDesc == nullptr )
      {
        var->localVarDesc    = mem::allocZ<var_desc_t>();
        *(var->localVarDesc) = *(var->classVarDesc);
        var->localVarDesc->link = nullptr;    
      }
  
      var->localVarDesc->type = type_flag;
      var->varDesc            = var->localVarDesc;
      
    errLabel:
      return rc;
    }
    
    // Create a variable and set it's value from 'value_cfg'.
    // If 'value_cfg' is null then use the value from var->varDesc->val_cfg.
    rc_t _var_create( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned id, unsigned chIdx, const object_t* value_cfg, unsigned altTypeFl, variable_t*& varRef )
    {
      rc_t        rc  = kOkRC;
      variable_t* var = nullptr;
      var_desc_t* vd  = nullptr;
      
      varRef = nullptr;

      // if this var already exists - it can't be created again
      if((var = _var_find_on_label_and_ch(inst,var_label, sfx_id, chIdx)) != nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The variable '%s:%i' ch:%i has already been created on the instance: '%s:%i'.",var_label,sfx_id,chIdx,inst->label,inst->label_sfx_id);
        goto errLabel;
      }

      // locate the var desc
      if((vd = var_desc_find( inst->class_desc, var_label)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"Unable to locate the variable '%s:%i' in class '%s'.", var_label, sfx_id, inst->class_desc->label );
        goto errLabel;
      }

      // create the var
      var = mem::allocZ<variable_t>();

      var->varDesc      = vd;
      var->classVarDesc = vd;
      var->inst         = inst;
      var->label        = mem::duplStr(var_label);
      var->label_sfx_id = sfx_id;
      var->vid          = id;
      var->chIdx        = chIdx;
      var->value        = nullptr;
      var->type         = kInvalidTFl;

      if( altTypeFl != kInvalidTFl )
        _var_set_type(var,altTypeFl);
      
      // if no value was given then set the value to the value given in the class
      if( value_cfg == nullptr )
        value_cfg = var->varDesc->val_cfg;
      
      // if value_cfg is valid set the variable value
      if( value_cfg != nullptr && cwIsNotFlag(vd->type,kRuntimeTFl))
        if((rc = var_set_from_preset( var, value_cfg )) != kOkRC )
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
        cwLogError(kOpFailRC,"Variable creation failed on '%s:%i.%s:%i' ch:%i.", inst->label, inst->label_sfx_id, var_label, sfx_id, chIdx );
      }
      else
      {
        varRef = var;
        cwLogMod("Created var: %s:%i.%s:%i ch:%i.", inst->label, inst->label_sfx_id, var_label, sfx_id, chIdx );
      }
      
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

cw::flow::fbuf_t* cw::flow::fbuf_create( srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV, const fd_sample_t** phsV, const fd_sample_t** hzV )
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
  f->magV      = mem::allocZ<fd_sample_t*>(chN);
  f->phsV      = mem::allocZ<fd_sample_t*>(chN);
  f->hzV       = mem::allocZ<fd_sample_t*>(chN);
  f->readyFlV  = mem::allocZ<bool>(chN);

  vop::copy( f->binN_V, binN_V, chN );
  vop::copy( f->maxBinN_V, maxBinN_V, chN );
  vop::copy( f->hopSmpN_V, hopSmpN_V, chN );  
  
  if( magV != nullptr || phsV != nullptr || hzV != nullptr )
  {
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {      
      f->magV[ chIdx ] = (fd_sample_t*)magV[chIdx];
      f->phsV[ chIdx ] = (fd_sample_t*)phsV[chIdx];
      f->hzV[  chIdx ] = (fd_sample_t*)hzV[chIdx];
    }
  }
  else
  {
    unsigned maxTotalBinsN = vop::sum( maxBinN_V, chN );
        
    fd_sample_t* buf       = mem::allocZ<fd_sample_t>( kFbufVectN * maxTotalBinsN );
    fd_sample_t* m         = buf;
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

cw::flow::fbuf_t*  cw::flow::fbuf_create( srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV, const fd_sample_t** phsV, const fd_sample_t** hzV )
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


cw::flow::mbuf_t* cw::flow::mbuf_create( const midi::ch_msg_t* msgA, unsigned msgN )
{
  mbuf_t* m = mem::allocZ<mbuf_t>();
  m->msgA = msgA;
  m->msgN = msgN;
  return m;
}

void cw::flow::mbuf_destroy( mbuf_t*& buf )
{
  mem::release(buf);
}

cw::flow::mbuf_t* cw::flow::mbuf_duplicate( const mbuf_t* src )
{
  return mbuf_create(src->msgA,src->msgN);
}


unsigned cw::flow::value_type_label_to_flag( const char* s )
{
  unsigned flags = labelToId(typeLabelFlagsA,s,kInvalidTFl);
  if( flags == kInvalidTFl )
    cwLogError(kInvalidArgRC,"Invalid type flag: '%s'",cwStringNullGuard(s));
    
  return flags;
}

const char* cw::flow::value_type_flag_to_label( unsigned flag )
{  return _typeFlagToLabel(flag); }
  

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

const cw::flow::preset_t* cw::flow::class_preset_find( class_desc_t* cd, const char* preset_label )
{
  const preset_t* pr;
  for(pr=cd->presetL; pr!=nullptr; pr=pr->link)
    if( textCompare(pr->label,preset_label) == 0 )
      return pr;
  
  return nullptr;
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
      const char* srcFlStr    = vd->flags & kSrcVarFl    ? "src"      : "   ";
      const char* srcOptFlStr = vd->flags & kSrcOptVarFl ? "srcOpt"   : "      ";
      const char* plyMltFlStr = vd->flags & kMultVarFl   ? "mult"     : "    ";
          
      printf("  %10s 0x%08x %s %s %s %s\n", cwStringNullGuard(vd->label), vd->type, srcFlStr, srcOptFlStr, plyMltFlStr, cwStringNullGuard(vd->docText) );
    }
  }
}


void cw::flow::network_print( const network_t& net )
{
  // for each instance in the network
  for(unsigned i=0; i<net.proc_arrayN; ++i)
  {
    instance_t* inst = net.proc_array[i];
    instance_print(inst);

    // if this instance has an  internal network
    if( inst->internal_net != nullptr )
    {
      printf("------- Begin Nested Network: %s -------\n",cwStringNullGuard(inst->label));
      network_print(*(inst->internal_net));
      printf("------- End Nested Network: %s -------\n",cwStringNullGuard(inst->label));
    }
        
  }
}

cw::rc_t cw::flow::instance_validate( instance_t* inst )
{
  rc_t rc = kOkRC;

  for(variable_t* var=inst->varL; var!=nullptr; var=var->var_link)
  {
    if( var->label == nullptr )
    {
      rc = cwLogError(kInvalidStateRC,"A var with no label was encountered.");
      continue;      
    }
    
    if( var->value == nullptr )
    {
      rc = cwLogError(kInvalidStateRC,"The var '%s:%i' has no value.",var->label,var->label_sfx_id);
      continue;      
    }

    // the assigned value must have exactly one type
    if(!math::isPowerOfTwo( var->value->tflag ) )
    {
      rc = cwLogError(kInvalidStateRC,"The var '%s:%i' has the invalid type flag:0x%x",var->label,var->label_sfx_id,var->value->tflag);
      continue;
    }

    // if var is using a local value (not connected to a source variable) then the type of the value must be valid with the variable class
    if( !is_connected_to_source_proc(var) && !(var->varDesc->type & var->value->tflag) )
    {
      rc = cwLogError(kInvalidStateRC, "The value type flag '%s' (0x%x) of '%s:%i-%s:%i' is not found in the variable class type flags: '%s' (0x%x)",
                      _typeFlagToLabel(var->value->tflag),var->value->tflag,
                      var->inst->label,var->inst->label_sfx_id,
                      var->label,var->label_sfx_id,
                      _typeFlagToLabel(var->varDesc->type),var->varDesc->type);
      continue;
    }

    // By setting the var->type field all future assignments to this variable
    // must be coercible to this type.  See _var_set_template()
    var->type = var->value->tflag;
  }
  
  return rc;
}


cw::flow::instance_t* cw::flow::instance_find( network_t& net, const char* inst_label, unsigned sfx_id )
{
  for(unsigned i=0; i<net.proc_arrayN; ++i)
    if( net.proc_array[i]->label_sfx_id==sfx_id && textIsEqual(inst_label,net.proc_array[i]->label) )
      return net.proc_array[i];

  return nullptr;
}

cw::rc_t cw::flow::instance_find( network_t& net, const char* inst_label, unsigned sfx_id, instance_t*& instPtrRef )
{
  rc_t rc = kOkRC;
      
  if((instPtrRef = instance_find(net,inst_label,sfx_id)) != nullptr )
    return rc;
      
  return cwLogError(kInvalidArgRC,"The instance '%s:%i' was not found.", inst_label, sfx_id );
}

cw::flow::external_device_t* cw::flow::external_device_find( flow_t* p, const char* device_label, unsigned typeId, unsigned inOrOutFl, const char* midiPortLabel )
{
  for(unsigned i=0; i<p->deviceN; ++i)
    if( (device_label==nullptr || cw::textIsEqual(p->deviceA[i].devLabel,device_label))
        && p->deviceA[i].typeId==typeId
        && cwIsFlag(p->deviceA[i].flags,inOrOutFl)
        && (midiPortLabel==nullptr || cw::textIsEqual(p->deviceA[i].portLabel,midiPortLabel)) )
      return p->deviceA + i;
  
  cwLogError(kInvalidArgRC,"The %s device named '%s' could not be found.", cwIsFlag(inOrOutFl,kInFl) ? "in" : "out", device_label );
  
  return nullptr;
}

void cw::flow::instance_print( instance_t* inst )
{
  printf("%s:%i\n", inst->label,inst->label_sfx_id);
  for(variable_t* var = inst->varL; var!=nullptr; var=var->var_link)
    if( var->chIdx == kAnyChIdx )
      for(variable_t* v0 = var; v0!=nullptr; v0=v0->ch_link)
        _var_print(v0);      
  
  if( inst->class_desc->members->report )
    inst->class_desc->members->report( inst );
}

unsigned cw::flow::instance_var_count( instance_t* inst )
{
  unsigned n = 0;
  for(variable_t* var=inst->varL; var!=nullptr; var=var->var_link)
    ++n;

  return n;
}


void cw::flow::_var_destroy( variable_t* var )
{
  if( var != nullptr )
  {
    for(unsigned i=0; i<kLocalValueN; ++i)
      _value_release(var->local_value+i);

    if( var->localVarDesc != nullptr )
      mem::release(var->localVarDesc);
    
    mem::release(var->label);
    mem::release(var);
  }
}


cw::rc_t cw::flow::var_create( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned id, unsigned chIdx, const object_t* value_cfg, unsigned altTypeFl, variable_t*& varRef )
{
  rc_t rc = kOkRC;

  rc = _var_create( inst, var_label, sfx_id, id, chIdx, value_cfg, altTypeFl, varRef );

  return rc;
}

cw::rc_t  cw::flow::var_channelize( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned chIdx, const object_t* value_cfg, unsigned vid, variable_t*& varRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  variable_t* base_var = nullptr;
  varRef = nullptr;

  if((base_var = _var_find_on_label_and_ch( inst, var_label, sfx_id, kAnyChIdx)) == nullptr)
  {
    rc = cwLogError(kInvalidStateRC,"The base ('any') channel variable could not be located on '%s:%i.%s:%i'.",inst->label,inst->label_sfx_id,var_label,sfx_id);
    goto errLabel;
  }
 
  // locate the variable with the stated chIdx
  var = _var_find_on_label_and_ch( inst, var_label, sfx_id, chIdx );
  
  // 'src' variables cannot be channelized
  /*
  if( cwIsFlag(base_var->varDesc->flags,kSrcVarFl) )
  {
    rc = cwLogError(rc,"'src' variables cannot be channelized.");
    goto errLabel;
  }
  */
  
  // if the requested var was not found then create a new variable with the requested channel index
  if( var == nullptr && chIdx != kAnyChIdx )
  {
    // create the channelized var
    if((rc = _var_create( inst, var_label, sfx_id, vid, chIdx, value_cfg, kInvalidTFl, var )) != kOkRC )
      goto errLabel;

    // if no value was set then set the value from the 'any' channel
    if( value_cfg == nullptr )
    {
      // if the base-var is connected to a source ...
      if( is_connected_to_source_proc(base_var) )
      {
        // ... then connect the new var to a source also
        
        // Attempt to find a matching channel on the source
        variable_t* src_ch_var      = base_var->src_var;
        variable_t* last_src_ch_var = base_var->src_var;
        unsigned src_ch_cnt = 0;
        
        for(; src_ch_var!=nullptr; src_ch_var=src_ch_var->ch_link)
        {
          last_src_ch_var = src_ch_var;
          if( src_ch_var->chIdx == var->chIdx )
            break;

          src_ch_cnt += 1;
        }

        // if there is more than one channel available and the src and dst var's do not have matching ch indexes
        // then there is a possibility that this is an unexpected connection between different channels.
        if( src_ch_var == nullptr && src_ch_cnt>1 && last_src_ch_var->chIdx != var->chIdx )
          cwLogWarning("A connection is being made where channel src and dst. channels don't match and more than one src channel is available.");

        // if no matching channel is found connect to the last valid source channel
        // (Connecting to the last valid source is better than connecting to base_var->src_var
        //  because if a var has more than a base var it is unlikely to update the base_var.)
        var_connect( last_src_ch_var, var );
            
      }
      else
      {
      
        // Set the value of the new variable to the value of the 'any' channel
        _value_duplicate( var->local_value[ var->local_value_idx], base_var->local_value[ base_var->local_value_idx ] );

        // If the 'any' channel value was set to point to it's local value then do same with this value
        if( base_var->local_value + base_var->local_value_idx == base_var->value )
          var->value = var->local_value + var->local_value_idx;
      }
    }
    
  }
  else
  {
    
    // a correctly channelized var was found - but we still may need to set the value
    if( value_cfg != nullptr )
    {
      //cwLogInfo("%s ch:%i",var_label,chIdx);
      rc = var_set_from_preset( var, value_cfg );
    }
    else
    {
      cwLogWarning("An existing var (%s:i.%s:i ch:%i) was specified for channelizing but no value was provided.", inst->label, inst->label_sfx_id, var_label, sfx_id, chIdx );
    }
  }

  assert( var != nullptr );
  varRef = var;
  
 errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Channelize failed for variable '%s:%i' on instance '%s:%i' ch:%i.", var_label, sfx_id, inst->label, inst->label_sfx_id, chIdx );
  
  return rc;
}

cw::rc_t cw::flow::var_call_custom_value_func( variable_t* var )
{
  rc_t rc;
  if((rc = var->inst->class_desc->members->value( var->inst, var )) != kOkRC )
    goto errLabel;
  
  if( var->flags & kLogVarFl )
  {    
    printf("%10s:%5i", var->inst->label,var->inst->label_sfx_id);
    
    if( var->chIdx == kAnyChIdx )
      _var_print(var);
    else
    {
      printf("\n");
      for(variable_t* ch_var = var; ch_var!=nullptr; ch_var=ch_var->ch_link)
        _var_print(ch_var);
      
    }
  }
  
errLabel:
  return rc;
  
}

cw::rc_t cw::flow::var_flags( instance_t* inst, unsigned chIdx, const char* var_label, unsigned sfx_id, unsigned& flags_ref )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  flags_ref = 0;
  
  if((rc = _var_find_on_label_and_ch(inst,var_label,sfx_id,chIdx,var)) != kOkRC )
    goto errLabel;

  flags_ref = var->flags;

errLabel:
  return rc;
}
    
cw::rc_t cw::flow::var_set_flags( instance_t* inst, unsigned chIdx, const char* var_label, unsigned sfx_id, unsigned flag )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_on_label_and_ch(inst,var_label,sfx_id,chIdx,var)) != kOkRC )
    goto errLabel;
  

  var->flags |= flag;

errLabel:
  return rc;
}
    
cw::rc_t cw::flow::var_clr_flags( instance_t* inst, unsigned chIdx, const char* var_label, unsigned sfx_id, unsigned flag )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_find_on_label_and_ch(inst,var_label,sfx_id,chIdx,var)) != kOkRC )
    goto errLabel;

  var->flags = cwClrFlag(var->flags,flag);
errLabel:
  return rc;
}

bool cw::flow::var_exists( instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx )
{ return _var_find_on_label_and_ch(inst,label,sfx_id,chIdx) != nullptr; }

bool cw::flow::var_has_value( instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx )
{
  variable_t* varPtr = nullptr;
  rc_t rc;
  
  if((rc = var_find( inst, label, sfx_id, chIdx, varPtr )) != kOkRC )
    return false;

  return varPtr->value != nullptr;
}

bool  cw::flow::var_is_a_source( instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx )
{
  rc_t rc;
  variable_t* varPtr = nullptr;
  if((rc = var_find( inst, label, sfx_id, chIdx, varPtr)) != kOkRC )
  {
    cwLogError(kEleNotFoundRC,"The variable '%s:%i' was not found on proc:'%s:%i'. 'source' state query is invalid.",cwStringNullGuard(label),sfx_id,cwStringNullGuard(inst->label),inst->label_sfx_id);
    return false;
  }
  
  return is_a_source_var(varPtr);
}

bool  cw::flow::var_is_a_source( instance_t* inst, unsigned vid, unsigned chIdx )
{
  rc_t rc;
  variable_t* varPtr = nullptr;
  if((rc = var_find( inst, vid, chIdx, varPtr)) != kOkRC )
  {
    cwLogError(kEleNotFoundRC,"The variable with vid '%i' was not found on proc:'%s:%i'. 'source' state query is invalid.",vid,cwStringNullGuard(inst->label),inst->label_sfx_id);
    return false;
  }
  
  return is_a_source_var(varPtr);
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
      rc = cwLogError(kInvalidIdRC,"The index of variable vid:%i chIdx:%i on instance '%s:%i' could not be calculated and the variable value could not be retrieved.", vid, chIdx, inst->label,inst->label_sfx_id);
      goto errLabel;
    }
  }

  // if we get here var must be non-null - (was the var registered?)
  assert( var != nullptr && rc == kOkRC );
  varRef = var;
  
 errLabel:
      
  return rc;
}



cw::rc_t cw::flow::var_find( instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx, variable_t*& vRef )
{
  variable_t* var;
  vRef = nullptr;
  
  if((var = _var_find_on_label_and_ch(inst,label,sfx_id,chIdx)) != nullptr )
  {
    vRef = var;
    return kOkRC;
  }

  return cwLogError(kInvalidIdRC,"The instance '%s:%i' does not have a variable named '%s:%i'.", inst->label, inst->label_sfx_id, label, sfx_id );  
}

cw::rc_t cw::flow::var_find( instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx, const variable_t*& vRef )
{
  variable_t* v = nullptr;
  rc_t        rc = var_find(inst,label,sfx_id,chIdx,v);
  vRef = v;
  return rc;
}


cw::rc_t  cw::flow::var_channel_count( instance_t* inst, const char* label, unsigned sfx_id, unsigned& chCntRef )
{
  rc_t rc = kOkRC;
  const variable_t* var= nullptr;
  if((rc = var_find(inst,label,sfx_id,kAnyChIdx,var)) != kOkRC )
    return cwLogError(rc,"Channel count was not available because the variable '%s:%i.%s:%i' does not exist.",cwStringNullGuard(inst->label),inst->label_sfx_id,cwStringNullGuard(label),sfx_id);

  return var_channel_count(var,chCntRef);
}

cw::rc_t  cw::flow::var_channel_count( const variable_t* var, unsigned& chCntRef )
{
  rc_t rc = kOkRC;
  const variable_t* v;
  
  chCntRef = 0;
  
  if((rc = var_find( var->inst, var->label, var->label_sfx_id, kAnyChIdx, v )) != kOkRC )
  {
    rc = cwLogError(kInvalidStateRC,"The base channel variable instance could not be found for the variable '%s:%i.%s:%i'.",var->inst->label,var->inst->label_sfx_id,var->label,var->label_sfx_id);
    goto errLabel;
  }

  for(v = v->ch_link; v!=nullptr; v=v->ch_link)
    chCntRef += 1;

 errLabel:
  return rc;
}



cw::rc_t cw::flow::var_register( instance_t* inst, const char* var_label, unsigned sfx_id,  unsigned vid, unsigned chIdx, const object_t* value_cfg, variable_t*& varRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;

  varRef = nullptr;

  // TODO: check for duplicate 'vid'-'chIdx' pairs on this instance
  // The concatenation of 'vid' and 'chIdx' should be unique 

  // if an exact match to label/chIdx was found
  if((var = _var_find_on_label_and_ch(inst,var_label,sfx_id,chIdx)) != nullptr )
  {
    // if a value was given - then update the value
    if( value_cfg != nullptr )
      if((rc = var_set_from_preset( var, value_cfg )) != kOkRC )
        goto errLabel;    
  }
  else // an exact match was not found - channelize the variable
  {
    if((rc = var_channelize(inst,var_label,sfx_id,chIdx,value_cfg,vid,var)) != kOkRC )
      goto errLabel;
  }

  var->vid = vid;
  varRef   = var;

  if((var = _var_find_on_label_and_ch(inst,var_label,sfx_id,kAnyChIdx)) != nullptr )
    var->vid = vid;
  else
    rc = cwLogError(kInvalidStateRC,"The variable '%s:%i' instance '%s:%i' has no base channel.", var_label, sfx_id, inst->label, inst->label_sfx_id, chIdx);
  
 errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Registration failed on variable '%s:%i' instance '%s:%i' ch: %i.", var_label, sfx_id, inst->label, inst->label_sfx_id, chIdx);
  
  return rc;
}

bool cw::flow::is_connected_to_source_proc( const variable_t* var )
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

bool cw::flow::is_a_source_var( const variable_t* var )
{ return var->dst_head != nullptr; }


void cw::flow::var_connect( variable_t* src_var, variable_t* in_var )
{
  assert( in_var->dst_link == nullptr );

  // connect in_var into src_var's outgoing var chain   
  if( src_var->dst_head == nullptr )
    src_var->dst_head = in_var;
  else
    src_var->dst_tail->dst_link = in_var;

  src_var->dst_tail = in_var;
  
  in_var->value    = src_var->value;
  in_var->src_var = src_var;

  //printf("Connect: ");
  //_var_print_addr("src",src_var);
  //_var_print_addr("dst",in_var);
  //_var_print_addr("HEAD",src_var->dst_head);

  //if( src_var->dst_head->dst_link != nullptr )
  //  _var_print_addr("LINK",src_var->dst_head->dst_link);
  
  //_var_print_addr("TAIL",src_var->dst_tail);
  //printf("\n");

  
}


cw::rc_t  cw::flow::var_mult_sfx_id_array( instance_t* inst, const char* var_label, unsigned* idA, unsigned idAllocN, unsigned& idN_ref )
{
  rc_t rc = kOkRC;

  idN_ref = 0;

  // for each variable whose 'label' is 'var_label'
  for(variable_t* var=inst->varL; var!=nullptr; var=var->var_link)
    if( textIsEqual(var->label,var_label) )
    {
      // scan idA[] for a matching sfx_id
      unsigned i=0;
      for(; i<idN_ref; ++i)
        if( idA[i] == var->label_sfx_id )
          break;

      // if the sfx_id of this var has not yet been included in idA[]
      if( i == idN_ref )
      {
        // ... and we still have space left in the output arrau
        if( idN_ref >= idAllocN )
        {
          rc = cwLogError(kBufTooSmallRC,"The mult-sfx-id result array is too small for the var:'%s'.",cwStringNullGuard(var_label));
          goto errLabel;
        }

        // store this sfx_id in idA[]
        idA[idN_ref++] = var->label_sfx_id;
      }
    }
  
errLabel:
  if( rc != kOkRC )
    idN_ref = 0;
  
  return rc;
}

cw::rc_t cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, variable_t*& varRef )
{
  return var_register( inst, var_label, sfx_id, vid, chIdx, nullptr, varRef );
}

cw::rc_t        cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned frameN )
{
  rc_t rc = kOkRC;
  abuf_t* abuf;
  
  if((abuf = abuf_create( srate, chN, frameN )) == nullptr )
    return cwLogError(kOpFailRC,"abuf create failed on instance:'%s:%i' variable:'%s:%i'.", inst->label, inst->label_sfx_id, var_label,sfx_id);

  if((rc = _var_register_and_set( inst, var_label, sfx_id, vid, chIdx, abuf )) != kOkRC )
    abuf_destroy(abuf);

  return rc;
}

cw::rc_t cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV, const fd_sample_t** phsV, const fd_sample_t** hzV )
{
  rc_t rc = kOkRC;
  fbuf_t* fbuf;
  if((fbuf = fbuf_create( srate, chN, maxBinN_V, binN_V, hopSmpN_V, magV, phsV, hzV )) == nullptr )
    return cwLogError(kOpFailRC,"fbuf create failed on instance:'%s:%i' variable:'%s:%i'.", inst->label, inst->label_sfx_id, var_label,sfx_id);

  if((rc = _var_register_and_set( inst, var_label, sfx_id, vid, chIdx, fbuf )) != kOkRC )
    fbuf_destroy(fbuf);

  return rc;
}

cw::rc_t        cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, midi::ch_msg_t* msgA, unsigned msgN  )
{
  rc_t rc = kOkRC;
  mbuf_t* mbuf;
  
  if((mbuf = mbuf_create(msgA,msgN)) == nullptr )
    return cwLogError(kOpFailRC,"mbuf create failed on instance:'%s:%i' variable:'%s:%i'.", inst->label, inst->label_sfx_id, var_label, sfx_id);

  if((rc = _var_register_and_set( inst, var_label, sfx_id, vid, chIdx, mbuf )) != kOkRC )
    mbuf_destroy(mbuf);

  return rc;
}


cw::rc_t cw::flow::var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV, const fd_sample_t** phsV, const fd_sample_t** hzV )
{
  unsigned maxBinN_V[ chN ];
  unsigned binN_V[ chN ];
  unsigned hopSmpN_V[ chN ];
  vop::fill(maxBinN_V,chN,maxBinN);
  vop::fill(binN_V,chN,binN);
  vop::fill(hopSmpN_V,chN, hopSmpN );
  return var_register_and_set(inst,var_label,sfx_id,vid,chIdx,srate, chN, maxBinN_V, binN_V, hopSmpN_V, magV, phsV, hzV);
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

cw::rc_t  cw::flow::var_get( const variable_t* var, const mbuf_t*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( variable_t* var, mbuf_t*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t  cw::flow::var_get( const variable_t* var, const object_t*& valRef )
{ return _val_get_driver(var,valRef); }

cw::rc_t cw::flow::var_set_from_preset( variable_t* var, const object_t* value )
{
  rc_t rc = kOkRC;

  //
  // Determine the flow variable type of cfg. argument 'value'.
  // 
  unsigned value_flag = 0;
      
  switch( value->type->id )
  {
    case kCharTId:  
    case kUInt8TId:
    case kUInt16TId:
    case kUInt32TId:
      value_flag = kUIntTFl;
      break;
          
    case kInt8TId:
    case kInt16TId:
    case kInt32TId:
      value_flag = kIntTFl;
      break;
          
    case kInt64TId:
    case kUInt64TId:
      rc = cwLogError(kInvalidArgRC,"The flow system does not currently implement 64bit integers.");
      goto errLabel;
      break;
          
    case kFloatTId:
      value_flag = kFloatTFl;
      break;
          
    case kDoubleTId:
      value_flag = kDoubleTFl;
      break;
          
    case kBoolTId:
      value_flag = kBoolTFl;
      break;
          
    case kStringTId:
    case kCStringTId:
      value_flag = kStringTFl;
      break;
          
    default:
      value_flag = kCfgTFl;
        
  }

  //
  // Convert the cfg value to a c++ typed value and then call _var_set_driver() with the c++ value.
  //
  switch( value_flag )
  {
    case kBoolTFl:
      {
        bool v;
        // assign the value of 'value' to to 'v' (do type conversion if necessary)
        if((rc = value->value(v)) == kOkRC )
          // set the value of 'var' where 'v' is the new value and 'value_flag' is the type of 'v'.
          rc = _var_set_driver( var, value_flag, v );
      }
      break;
          
    case kUIntTFl:
      {
        unsigned v;
        if((rc = value->value(v)) == kOkRC )
          rc = _var_set_driver( var, value_flag, v );
      }
      break;
          
    case kIntTFl:
      {
        int v;
        if((rc = value->value(v)) == kOkRC )
          rc = _var_set_driver( var, value_flag, v );
      }
      break;
          
    case kFloatTFl:
      {
        float v;
        if((rc = value->value(v)) == kOkRC )
          rc = _var_set_driver( var, value_flag, v );
      }
      break;

    case kDoubleTFl:
      {
        double v;
        if((rc = value->value(v)) == kOkRC )
          rc = _var_set_driver( var, value_flag, v );
      }
      break;
          
    case kStringTFl:
      {
        const char* v;
        if((rc = value->value(v)) == kOkRC )
          rc = _var_set_driver( var, value_flag, v );
      }
      break;

    case kCfgTFl:
      {
        rc = _var_set_driver( var, value_flag, value );
      }
      break;
          
    default:
      rc = cwLogError(kOpFailRC,"The variable type 0x%x cannot yet be set via a cfg.", var->varDesc->type );
      goto errLabel;
  }

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(kSyntaxErrorRC,"The %s:%i.%s:%i could not extract a type:%s from a configuration value.",var->inst->label,var->inst->label_sfx_id,var->label,var->label_sfx_id,_typeFlagToLabel(var->varDesc->type & kTypeMask));
      
  return rc;
      
}

cw::rc_t cw::flow::var_set( variable_t* var, const value_t* val )
{
  rc_t rc = kOkRC;
  
  switch( val->tflag )
  {
    case kBoolTFl:   rc = _var_set_driver(var,val->tflag,val->u.b); break;
    case kUIntTFl:   rc = _var_set_driver(var,val->tflag,val->u.u); break;
    case kIntTFl:    rc = _var_set_driver(var,val->tflag,val->u.i); break;
    case kFloatTFl:  rc = _var_set_driver(var,val->tflag,val->u.f); break;
    case kDoubleTFl: rc = _var_set_driver(var,val->tflag,val->u.d); break;
    case kStringTFl: rc = _var_set_driver(var,val->tflag,val->u.s); break;
    case kCfgTFl:    rc = _var_set_driver(var,val->tflag,val->u.cfg); break;
    case kABufTFl:   rc = _var_set_driver(var,val->tflag,val->u.abuf); break;
    case kFBufTFl:   rc = _var_set_driver(var,val->tflag,val->u.fbuf); break;
    case kMBufTFl:   rc = _var_set_driver(var,val->tflag,val->u.mbuf); break;
    default:
      rc = cwLogError(kNotImplementedRC,"The var_set() from value_t has not been implemented for the type 0x%x.",val->tflag);
  }

  return rc;
}


cw::rc_t cw::flow::var_set( variable_t* var, bool val )            { return _var_set_driver(var,kBoolTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, uint_t val )          { return _var_set_driver(var,kUIntTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, int_t val )           { return _var_set_driver(var,kIntTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, float val )           { return _var_set_driver(var,kFloatTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, double val )          { return _var_set_driver(var,kDoubleTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, const char* val )     { return _var_set_driver(var,kStringTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, abuf_t* val )         { return _var_set_driver(var,kABufTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, fbuf_t* val )         { return _var_set_driver(var,kFBufTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, mbuf_t* val )         { return _var_set_driver(var,kMBufTFl,val); }
cw::rc_t cw::flow::var_set( variable_t* var, const object_t* val ) { return _var_set_driver(var,kCfgTFl,val); }

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, const value_t* val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = var_set(var,val);
  
  return rc;
}    

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, bool val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = _var_set_driver( var, kBoolTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, uint_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = _var_set_driver( var, kUIntTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, int_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = _var_set_driver( var, kIntTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, float val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = _var_set_driver( var, kFloatTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, double val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = _var_set_driver( var, kDoubleTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, const char* val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = _var_set_driver( var, kStringTFl, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, const object_t* val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
    rc = _var_set_driver( var, kCfgTFl, val );
  
  return rc;    
}



