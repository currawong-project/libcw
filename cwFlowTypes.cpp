#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwFlowTypes.h"


namespace cw
{
  namespace flow
  {
    idLabelPair_t typeLabelFlagsA[] = {
      
      { kBoolTFl, "bool" },
      { kUIntTFl, "uint" },
      { kIntTFl,  "int", },
      { kRealTFl, "real" },
      { kF32TFl,  "float"},
      { kF64TFl,  "double"},
      
      { kBoolMtxTFl, "bool_mtx" },
      { kUIntMtxTFl, "uint_mtx" },
      { kIntMtxTFl,  "int_mtx"  },
      { kRealMtxTFl, "real_mtx" },
      { kF32MtxTFl,  "float_mtx" },
      { kF64MtxTFl,  "double_mtx" },
      
      { kABufTFl,   "audio" },
      { kFBufTFl,   "spectrum" },
      { kStringTFl, "string" },
      { kFNameTFl,  "fname" },
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
        case kRealTFl:
        case kF32TFl:
        case kF64TFl:
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
        case kRealMtxTFl:
        case kF32MtxTFl:
        case kF64MtxTFl:
          assert(0); // not implemeneted
          break;
          
        case kStringTFl:
          mem::release( v->u.s );
          break;
          
        case kFNameTFl:
          mem::release( v->u.fname );
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
        case kRealTFl:  printf("%f ", v->u.r ); break;
        case kF32TFl:   printf("%f ", v->u.f ); break;
        case kF64TFl:   printf("%f ", v->u.d ); break;
        case kABufTFl:  printf("abuf: chN:%i frameN:%i srate:%8.1f ", v->u.abuf->chN, v->u.abuf->frameN, v->u.abuf->srate ); break;
        case kFBufTFl:  printf("fbuf: chN:%i binN:%i hopSmpN:%i srate:%8.1f", v->u.fbuf->chN, v->u.fbuf->binN, v->u.fbuf->hopSmpN, v->u.fbuf->srate ); break;

          
        case kBoolMtxTFl:
        case kUIntMtxTFl:
        case kIntMtxTFl:
        case kRealMtxTFl:
        case kF32MtxTFl:
        case kF64MtxTFl:
          assert(0); // not implemeneted
          break;
          
        case kStringTFl: printf("%s ", v->u.s); break;
        case kFNameTFl:  printf("%s ", v->u.fname); break;
           
        case kTimeTFl:
          assert(0);
          break;

        default:
          assert(0);
          break;
      }

    }

    rc_t _var_get( instance_t* inst, unsigned vid, unsigned chIdx, unsigned typeFl, const char* typeLabel, variable_t*& varRef )
    {
      rc_t     rc  = kOkRC;
      unsigned idx = kInvalidIdx;

      varRef = nullptr;

      if((rc = var_map_id_to_index(inst, vid, chIdx, idx )) == kOkRC || idx == kInvalidIdx )
      {
        variable_t* var = inst->varMapA[idx];
  
        if( !cwIsFlag(var->varDesc->type,typeFl ) )
        {
          rc = cwLogError(kTypeMismatchRC,"Instance:%s variable:%s is not a %s.",inst->label,var->label,typeLabel);
          goto errLabel;
        }

        varRef = var;
      }
      
    errLabel:
      return rc;
    }

    
    variable_t* _var_find( instance_t* inst, const char* label, unsigned chIdx )
    {
      variable_t* var  = inst->varL;
  
      for(; var!=nullptr; var=var->link)
        if( textCompare(var->label,label) == 0 && (var->chIdx==chIdx || var->chIdx==kAnyChIdx || (chIdx==kAnyChIdx && var->chIdx==0)))
          return var;
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

      // notify each connected var that the value has changed
      for(variable_t* con_var = var->connect_link; con_var!=nullptr; con_var=con_var->connect_link)
        if((rc = con_var->inst->class_desc->members->value( con_var->inst, con_var )) != kOkRC )
          break;
      
      return rc;
    }
    
    rc_t _var_set( variable_t* var, unsigned val )
    {
      rc_t rc;
      if((rc = _validate_var_assignment( var, kUIntTFl )) != kOkRC )
        return rc;
      
      _value_release(&var->local_value);
      var->local_value.u.u = val;
      var->local_value.flags  = kUIntTFl;
      var->value = &var->local_value;

      return _var_broadcast_new_value( var );
        
    }
    
    rc_t _var_set( variable_t* var, int val )
    {
      rc_t rc;
      if((rc = _validate_var_assignment( var, kIntTFl )) != kOkRC )
        return rc;
      
      _value_release(&var->local_value);
      var->local_value.u.i = val;
      var->local_value.flags  = kIntTFl;
      var->value = &var->local_value;

      return _var_broadcast_new_value( var );      
    }
    
    rc_t _var_set( variable_t* var, real_t val )
    {
      rc_t rc;
      if((rc = _validate_var_assignment( var, kRealTFl )) != kOkRC )
        return rc;
      
      _value_release(&var->local_value);
      var->local_value.u.r = val;
      var->local_value.flags  = kRealTFl;
      var->value = &var->local_value;

      return _var_broadcast_new_value( var );      
    }
    
    rc_t _var_set( variable_t* var, abuf_t* abuf )
    {
      rc_t rc;
      if((rc = _validate_var_assignment( var, kABufTFl )) != kOkRC )
        return rc;
      
      _value_release(&var->local_value);
      var->local_value.u.abuf = abuf;
      var->local_value.flags  = kABufTFl;
      var->value = &var->local_value;

      return _var_broadcast_new_value( var );
      
    }

    rc_t _var_set( variable_t* var, fbuf_t* fbuf )
    {
      rc_t rc;
      if((rc = _validate_var_assignment( var, kFBufTFl )) != kOkRC )
        return rc;
      
      _value_release(&var->local_value);
      var->local_value.u.fbuf = fbuf;
      var->local_value.flags  = kFBufTFl;
      var->value = &var->local_value;
      
      return _var_broadcast_new_value( var );
    }

    const preset_t* _preset_find( class_desc_t* cd, const char* preset_label )
    {
      const preset_t* pr;
      for(pr=cd->presetL; pr!=nullptr; pr=pr->link)
        if( textCompare(pr->label,preset_label) == 0 )
          return pr;

      return nullptr;
    }
    
    rc_t _preset_set_var_value( instance_t* inst, const char* var_label, unsigned chIdx, const object_t* value )
    {
      rc_t rc = kOkRC;
      variable_t* var = nullptr;

      // get the variable
      if((rc = var_get( inst, var_label, chIdx, var )) != kOkRC )
        goto errLabel;
        
      switch( var->varDesc->type & kTypeMask )
      {
        case kUIntTFl:
          {
            unsigned v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set( var, v );
          }
          break;
          
        case kIntTFl:
          {
            int v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set( var, v );
          }
          break;
          
        case kRealTFl:
          {
            real_t v;
            if((rc = value->value(v)) == kOkRC )
              rc = _var_set( var, v );
          }
          break;
          
        default:
          rc = cwLogError(kOpFailRC,"The variable type 0x%x cannot yet be set via a preset.", var->varDesc->type );
          goto errLabel;
      }

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

void  cw::flow::abuf_destroy( abuf_t* abuf )
{
  if( abuf == nullptr )
    return;
  
  mem::release(abuf->buf);
  mem::release(abuf);
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



cw::flow::fbuf_t*  cw::flow::fbuf_create( srate_t srate, unsigned chN, unsigned binN, unsigned hopSmpN, const sample_t** magV, const sample_t** phsV, const sample_t** hzV )
{
  fbuf_t* f = mem::allocZ<fbuf_t>();
  
  f->srate   = srate;
  f->chN     = chN;
  f->binN    = binN;
  f->hopSmpN = hopSmpN;
  f->magV    = mem::allocZ<sample_t*>(chN);
  f->phsV    = mem::allocZ<sample_t*>(chN);
  f->hzV     = mem::allocZ<sample_t*>(chN);

  if( magV != nullptr || phsV != nullptr || hzV != nullptr )
  {
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {
      f->magV[ chIdx ] = (sample_t*)magV[chIdx];
      f->phsV[ chIdx ] = (sample_t*)phsV[chIdx];
      f->hzV[  chIdx ] = (sample_t*)hzV[chIdx];
    }
  }
  else
  {
    sample_t* buf  = mem::allocZ<sample_t>( chN * kFbufVectN * binN );
  
    for(unsigned chIdx=0,j=0; chIdx<chN; ++chIdx,j+=kFbufVectN*binN)
    {   
      f->magV[chIdx] = buf + j + 0 * binN;
      f->phsV[chIdx] = buf + j + 1 * binN;
      f->hzV[ chIdx] = buf + j + 2 * binN;
    }
  
    f->buf = buf;
      
  }

  return f;
}

void cw::flow::fbuf_destroy( fbuf_t* fbuf )
{
  if( fbuf == nullptr )
    return;
  
  mem::release( fbuf->magV);
  mem::release( fbuf->phsV);
  mem::release( fbuf->hzV);
  mem::release( fbuf->buf);
  mem::release( fbuf);
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


void cw::flow::class_desc_print( flow_t* p )
{
  for(unsigned i=0; i<p->classDescN; ++i)
  {
    class_desc_t* cd = p->classDescA + i;
    var_desc_t*   vd = cd->varDescL;
    printf("%s\n",cwStringNullGuard(cd->label));
        
    for(; vd!=nullptr; vd=vd->link)
    {
      const char* srcFlStr = vd->flags&kSrcVarFl ? "src" : "   ";
          
      printf("  %10s 0x%08x %s %s\n", cwStringNullGuard(vd->label), vd->type, srcFlStr, cwStringNullGuard(vd->docText) );
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

void cw::flow::instance_print( instance_t* inst )
{
  printf("%s\n", inst->label);
  for(variable_t* var = inst->varL; var!=nullptr; var=var->link)
  {
    printf("  %20s id:%4i ch:%3i : ", var->label, var->vid, var->chIdx );
    _value_print( var->value );
    printf("\n");
  }
}


cw::rc_t cw::flow::var_create( instance_t* inst, const char* var_label, unsigned id, unsigned chIdx, variable_t*& varRef )
{
  rc_t rc = kOkRC;
  
  varRef = nullptr;
  
  variable_t* var = nullptr;

  // if this var already exists then just update its id
  if( var_exists(inst,var_label,chIdx) )
  {
    rc = cwLogError(kInvalidStateRC,"The variable '%s' has already been created on the instance: '%s'.",var_label,inst->label);
    goto errLabel;
  }
  
  var = mem::allocZ<variable_t>();
  
  if((var->varDesc = var_desc_find( inst->class_desc, var_label)) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"Unable to locate the variable '%s' in class '%s'.", var_label, inst->class_desc->label );
    goto errLabel;
  }

  var->inst  = inst;
  var->label = mem::duplStr(var_label);
  var->vid   = id;
  var->chIdx = chIdx;
  var->value = nullptr;
  var->link  = inst->varL;
  inst->varL = var;
  
  
 errLabel:

  if(rc != kOkRC )
    rc = cwLogError(kOpFailRC,"Creation failed for variable '%s' on instance '%s'.", var_label, inst->label );
  else
    varRef = var;
  
  return kOkRC;
}

void cw::flow::_var_destroy( variable_t* var )
{
  _value_release(&var->local_value);
  mem::release(var->label);
  mem::release(var);
}

bool cw::flow::var_exists( instance_t* inst, const char* label, unsigned chIdx )
{ return _var_find(inst,label,chIdx) != nullptr; }

cw::rc_t cw::flow::var_get( instance_t* inst, const char* label, unsigned chIdx, variable_t*& vRef )
{
  variable_t* var;

  if((var = _var_find(inst,label,chIdx)) != nullptr )
  {
    vRef = var;
    return kOkRC;
  }

  return cwLogError(kInvalidIdRC,"The instance '%s' does not have a variable named '%s'.", inst->label, label );  
}

cw::rc_t cw::flow::var_get( instance_t* inst, const char* label, unsigned chIdx, const variable_t*& vRef )
{
  variable_t* v = nullptr;
  rc_t rc = var_get(inst,label,chIdx,v);
  vRef = v;
  return rc;
}

cw::rc_t cw::flow::value_get(    instance_t* inst, const char* label, unsigned chIdx, value_t*& vRef )
{
  variable_t* var = nullptr;
  rc_t rc = kOkRC;
  if((rc = var_get( inst, label, chIdx, var )) != kOkRC )
    return rc;

  vRef = var->value;
  return rc;
}

cw::rc_t cw::flow::value_get(    instance_t* inst, const char* label, unsigned chIdx, const value_t*& vRef )
{
  value_t* v = nullptr;
  rc_t rc = value_get( inst, label, chIdx, v );
  vRef = v;
  return rc;
}

cw::rc_t        cw::flow::var_abuf_create( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned frameN )
{
  rc_t        rc;
  abuf_t*     abuf = nullptr;
  variable_t* var = nullptr;
  
  if((rc = var_create( inst, var_label, vid, chIdx, var)) != kOkRC )
    return rc;
      
  if((abuf = abuf_create( srate, chN, frameN )) == nullptr )
    return cwLogError(kOpFailRC,"abuf create failed on instance:'%s' variable:'%s'.", inst->label, var_label);

  rc = _var_set( var, abuf );
  
  return rc;
}

cw::rc_t cw::flow::var_fbuf_create( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned binN, unsigned hopSmpN, const sample_t** magV, const sample_t** phsV, const sample_t** hzV )
{
  rc_t        rc;
  fbuf_t*     fbuf = nullptr;
  variable_t* var = nullptr;
  
  if((rc = var_create( inst, var_label, vid, chIdx, var)) != kOkRC )
    return rc;
      
  if((fbuf = fbuf_create( srate, chN, binN, hopSmpN, magV, phsV, hzV )) == nullptr )
    return cwLogError(kOpFailRC,"fbuf create failed on instance:'%s' variable:'%s'.", inst->label, var_label);

  rc = _var_set( var, fbuf );
  
  return rc;
}

cw::rc_t cw::flow::var_abuf_get( instance_t* inst, const char* var_label, unsigned chIdx, abuf_t*& abufRef )
{
  rc_t rc = kOkRC;
  value_t* val;
  if((rc = value_get(inst,var_label,chIdx,val)) != kOkRC )
    goto errLabel;

  if( !value_is_abuf(val))
  {
    rc = cwLogError(kTypeMismatchRC,"The variable '%' on instance '%s' is not an abuf.");
    goto errLabel;
  }
  
 errLabel:
  if( rc == kOkRC )
    abufRef = val->u.abuf;
  else
    rc = cwLogError(rc,"No abuf was retrieved from variable: '%s' instance: '%s'", var_label, inst->label );
  
  return rc;  
}

cw::rc_t cw::flow::var_abuf_get( instance_t* inst, const char* var_label, unsigned chIdx, const abuf_t*& abufRef )
{
  rc_t rc;
  abuf_t* abuf;
  abufRef = (rc = var_abuf_get( inst, var_label, chIdx, abuf)) == kOkRC ? abuf : nullptr;
  return rc;
}

cw::rc_t cw::flow::var_fbuf_get( instance_t* inst, const char* var_label, unsigned chIdx, fbuf_t*& fbufRef )
{
  rc_t rc = kOkRC;
  value_t* val;
  if((rc = value_get(inst,var_label,chIdx,val)) != kOkRC )
    goto errLabel;

  if( !value_is_fbuf(val))
  {
    rc = cwLogError(kTypeMismatchRC,"The variable '%' on instance '%s' is not an fbuf.");
    goto errLabel;
  }
  
 errLabel:
  if( rc == kOkRC )
    fbufRef = val->u.fbuf;
  else
    rc = cwLogError(rc,"No fbuf was retrieved from variable: '%s' instance: '%s'", var_label, inst->label );
  
  return rc;    
}

cw::rc_t cw::flow::var_fbuf_get( instance_t* inst, const char* var_label, unsigned chIdx, const fbuf_t*& fbufRef )
{
  rc_t rc;
  fbuf_t* fbuf;
  fbufRef = (rc = var_fbuf_get( inst, var_label, chIdx, fbuf)) == kOkRC ? fbuf : nullptr;
  return rc;
}

cw::rc_t cw::flow::var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, variable_t*& varRef )
{
  variable_t* var = nullptr;

  varRef = nullptr;

  // if the variable already exists then update the vid, and set the returned variable to null.
  if((var = _var_find(inst,var_label,chIdx)) != nullptr)
  {
    var->vid = vid;
    return kOkRC;  // NOTE: when returning here 'varRef' should be NULL
  }
  
  return var_create( inst, var_label, vid, chIdx, varRef);
}

cw::rc_t  cw::flow::var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, unsigned value )
{
  rc_t rc;
  variable_t* var = nullptr;
  if((rc = var_init( inst, var_label, vid, chIdx, var)) != kOkRC )
    return rc;

  if( var != nullptr )
    _var_set( var, value );

  return rc;
}

cw::rc_t  cw::flow::var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, int value )
{
  rc_t rc;
  variable_t* var = nullptr;
  if((rc = var_init( inst, var_label, vid, chIdx, var)) != kOkRC )
    return rc;

  if( var != nullptr )
    _var_set( var, value );

  return rc;
}

cw::rc_t  cw::flow::var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, real_t value )
{
  rc_t rc;
  variable_t* var = nullptr;
  if((rc = var_init( inst, var_label, vid, chIdx, var)) != kOkRC )
    return rc;

  if( var != nullptr )
    _var_set( var, value );

  return rc;
}

cw::rc_t  cw::flow::var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, const abuf_t* abuf )
{
  rc_t rc = kOkRC;
  variable_t* var;

  if((var =  _var_find(inst,var_label,chIdx)) != nullptr )
    var->vid = vid;
  else
    rc = var_abuf_create(inst, var_label, vid, chIdx, abuf->srate, abuf->chN, abuf->frameN );

  return rc;
}

cw::rc_t  cw::flow::var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, const fbuf_t* fbuf )
{
  rc_t        rc = kOkRC;
  variable_t* var;
  if((var    = _var_find(inst,var_label,chIdx)) != nullptr )
    var->vid = vid;
  else
    rc = var_fbuf_create(inst, var_label, vid, chIdx, fbuf->srate, fbuf->chN, fbuf->binN, fbuf->hopSmpN );

  return rc;
}

cw::rc_t  cw::flow::var_map_id_to_index(  instance_t* inst, unsigned vid, unsigned chIdx, unsigned& idxRef )
{
  unsigned idx = vid * inst->varMapChN + (chIdx == kAnyChIdx ? 0 : chIdx);

  // verify that the map idx is valid
  if( idx >= inst->varMapN )
    return cwLogError(kAssertFailRC,"The variable map positioning location %i is out of the range % on instance '%s' vid:%i ch:%i.", idx, inst->varMapN, inst->label,vid,chIdx);

  idxRef = idx;
  
  return kOkRC;
}

cw::rc_t  cw::flow::var_map_label_to_index(  instance_t* inst, const char* var_label, unsigned chIdx, unsigned& idxRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  idxRef = kInvalidIdx;
  if((rc = var_get(inst, var_label, chIdx, var )) == kOkRC)
     rc = var_map_id_to_index( inst, var->vid, chIdx, idxRef );
     
  return rc;
}


cw::rc_t cw::flow::var_get( instance_t* inst, unsigned vid, unsigned chIdx, uint_t& valRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kUIntTFl, "uint_t", var )) != kOkRC )
    return rc;
  
  valRef = var->value->u.u;

  return rc;
}

cw::rc_t cw::flow::var_get( instance_t* inst, unsigned vid, unsigned chIdx, int_t& valRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kIntTFl, "int_t", var )) == kOkRC )
    valRef = var->value->u.i;

  return rc;
} 

cw::rc_t cw::flow::var_get( instance_t* inst, unsigned vid, unsigned chIdx, real_t& valRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kRealTFl, "real_t", var )) == kOkRC )
    valRef = var->value->u.r;
  
  return rc;
}

cw::rc_t cw::flow::var_get( instance_t* inst, unsigned vid, unsigned chIdx, abuf_t*& valRef )    
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kABufTFl, "abuf_t", var )) == kOkRC )
    valRef = var->value->u.abuf;
  
  return rc;
}

cw::rc_t cw::flow::var_get( instance_t* inst, unsigned vid, unsigned chIdx, fbuf_t*& valRef )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kFBufTFl, "fbuf_t", var )) == kOkRC )
    valRef = var->value->u.fbuf;
  
  return rc;  
}


cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, uint_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kUIntTFl, "uint_t", var )) == kOkRC )
    _var_set( var, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, int_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kIntTFl, "int_t", var )) == kOkRC )
    _var_set( var, val );
  
  return rc;    
}

cw::rc_t cw::flow::var_set( instance_t* inst, unsigned vid, unsigned chIdx, real_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;
  
  if((rc = _var_get(inst, vid, chIdx, kRealTFl, "real_t", var )) == kOkRC )
    _var_set( var, val );
  
  return rc;    
}


cw::rc_t   cw::flow::var_set( instance_t* inst, const char* var_label, unsigned chIdx, uint_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;

  if((rc = var_get(inst, var_label, chIdx, var)) == kOkRC )
    rc = var_set( inst, var->vid, chIdx, val );
  return rc;
}

cw::rc_t   cw::flow::var_set( instance_t* inst, const char* var_label, unsigned chIdx, int_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;

  if((rc = var_get(inst, var_label, chIdx, var)) == kOkRC )
    rc = var_set( inst, var->vid, chIdx, val );
  return rc;
}

cw::rc_t   cw::flow::var_set( instance_t* inst, const char* var_label, unsigned chIdx, real_t val )
{
  rc_t        rc  = kOkRC;
  variable_t* var = nullptr;

  if((rc = var_get(inst, var_label, chIdx, var)) == kOkRC )
    rc = var_set( inst, var->vid, chIdx, val );
  return rc;
}


cw::rc_t  cw::flow::apply_preset( instance_t* inst, const char* preset_label )
{
  rc_t            rc = kOkRC;
  const preset_t* pr;

  // locate the requestd preset record
  if((pr = _preset_find(inst->class_desc, preset_label)) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be found for the instance '%s'.", preset_label, inst->label);
    goto errLabel;
  }

  // validate the syntax of the preset record
  if( !pr->cfg->is_dict() )
  {
    rc = cwLogError(kSyntaxErrorRC,"The preset record '%s' on class '%s' is not a dictionary.", preset_label, inst->class_desc->label );
    goto errLabel;
  }

  // for each variable
  for(unsigned i=0; i<pr->cfg->child_count(); ++i)
  {
    const object_t* value_list  = pr->cfg->child_ele(i)->pair_value();
    const char*     value_label = pr->cfg->child_ele(i)->pair_label();

    unsigned preset_valueN = value_list->child_count();
    unsigned inst_chN      = inst->varMapChN;

    if( preset_valueN == 0 )
      continue;

    enum { kOneToOneAlgoId, kOneToManyAlgoId, kModuloAlgoId, kRepeatLastAlgoId };

    unsigned algo_id = kModuloAlgoId;

    if( preset_valueN == inst_chN )
      algo_id = kOneToOneAlgoId;
    else      
      if( preset_valueN == 1 && inst_chN >= 1 )
        algo_id = kOneToManyAlgoId;
      else
        algo_id = kModuloAlgoId;

    // for each value in this variables value list/ each var channel on the inst
    for(unsigned chIdx=0; chIdx<inst->varMapChN; ++chIdx)
    {
      if( var_exists( inst, value_label, chIdx ) )
      {
        unsigned value_list_idx = 0;
        
        switch( algo_id )
        {
          case kOneToOneAlgoId:
            value_list_idx = chIdx;
            break;
            
          case kOneToManyAlgoId:
            value_list_idx = 0;
            break;
            
          case kModuloAlgoId:
            value_list_idx = chIdx % value_list->child_count();
            break;
            
          default:
            assert(0);
        }

        // set the value of the preset
        if((rc = _preset_set_var_value( inst, value_label, chIdx, value_list->child_ele(value_list_idx) )) != kOkRC)
          break;
        
      } // if exitst
    } // for varMapN
  } // for value_list

 errLabel:
  
  return rc;
}
