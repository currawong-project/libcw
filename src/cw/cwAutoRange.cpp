#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwAutoRange.h"
#include "cwVectOps.h"

namespace cw
{
  namespace auto_range
  {
    typedef struct in_var_str
    {
      char*    label;       // input variable label
      unsigned id;          // input variable id - same as index into auto_range_t.i_varA[]
      double*  bufA;        // bufA[ bufAllocN ]  - circular buffer
      unsigned bufAllocN;   // 
      unsigned bufN;        // bufN - count of filled elements in bufA[]
      unsigned i_buf_idx;   // next input element in bufA[]

      bool  init_fl; // Set if at least one valid input value has been received.

      
      double sum;      // sum of values in bufA[]
      double mean;     // mean of values in bufA[] 
      double std_dev;  // std-dev of values in bufA[]

      double i_value;  // last input value
    } i_var_t;
    
    typedef struct var_str
    {
      char*          label;
      unsigned       id;        // out variable id - same as index in to auto_range_t.o_varA[]
      
      cfg_t          cfg;       // variable config. record
      
      const i_var_t* ivar;      // input variable
      
      double         o_value;   // last output value
      
    } o_var_t;
    
    typedef struct auto_range_str
    {
      i_var_t* i_varA;
      unsigned i_varAllocN;
      unsigned i_varN;

      o_var_t* o_varA;
      unsigned o_varAllocN;
      unsigned o_varN;
      
    } auto_range_t;

    auto_range_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,auto_range_t>(h); }
  
    rc_t _destroy( auto_range_t* p )
    {
      for(unsigned i=0; i<p->i_varN; ++i)
      {
        mem::release(p->i_varA[i].bufA);
        mem::release(p->i_varA[i].label);
      }

      for(unsigned i=0; i<p->o_varN; ++i)
      {
        mem::release(p->o_varA[i].label);
      }
      
      mem::release(p->i_varA);
      mem::release(p->o_varA);
      mem::release(p);
      return kOkRC;
    }

    const i_var_t* _i_label_to_var( const auto_range_t* p, const char* label )
    {
      for(unsigned i=0; i<p->i_varN; ++i)
        if( textIsEqual(p->i_varA[i].label,label))
          return p->i_varA + i;
      return nullptr;
    }

    const o_var_t* _o_label_to_var( const auto_range_t* p, const char* label )
    {
      for(unsigned i=0; i<p->o_varN; ++i)
        if( textIsEqual(p->o_varA[i].label,label))
          return p->o_varA + i;
      return nullptr;
    }
    
    
  }
}

cw::rc_t cw::auto_range::create( handle_t& hRef, const char* fname )
{
  rc_t      rc  = kOkRC;
  object_t* cfg = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  if((rc = objectFromFile( fname, cfg )) != kOkRC )
  {
    goto errLabel;
  }
  
  if((rc = create(hRef,cfg)) != kOkRC )
    goto errLabel;
  
errLabel:
  if( cfg != nullptr )
    cfg->free();
  
  if( rc != kOkRC )
    rc = cwLogError(rc,"Auto-range instanitation from '%s' failed.",cwStringNullGuard(fname));
  
  return rc;
}

cw::rc_t cw::auto_range::create( handle_t& hRef, const object_t* cfg )
{
  rc_t            rc          = kOkRC;
  unsigned        history_cnt = 0;
  unsigned        var_cnt     = 0;
  const object_t* varL        = nullptr;

  if((rc = cfg->getv("history_cnt",history_cnt,
                     "varL",varL)) != kOkRC )
  {
    rc = cwLogError(rc,"Cfg. syntax error.");
    goto errLabel;
  }

  if( !varL->is_list() )
  {
    rc = cwLogError(kSyntaxErrorRC,"Auto-range var list cfg. syntax error.");
    goto errLabel;
  }

  var_cnt = varL->child_count();

  if((rc = create(hRef,var_cnt,var_cnt,history_cnt)) != kOkRC )
  {
    goto errLabel;
  }

  for(unsigned i=0; i<var_cnt; ++i)
  {    
    unsigned        in_var_id  = kInvalidId;
    unsigned        out_var_id = kInvalidId;
    const object_t* var_cfg = varL->child_ele(i);
    cfg_t cfg{};

    if(!var_cfg->is_dict())
    {
      rc = cwLogError(kSyntaxErrorRC,"The variable cfg at index %i is not a dictionary.",i);
      goto errLabel;
    }

    if((rc = var_cfg->getv("i_label",cfg.i_label,
                           "o_label",cfg.o_label,
                           "default_out_value",cfg.default_out_value,
                           "dev_mult",cfg.dev_mult,
                           "min_out_value",cfg.min_out_value,
                           "max_out_value",cfg.max_out_value,
                           "out_filter_coeff",cfg.out_filter_coeff,
                           "bool_threshold",cfg.bool_threshold)) != kOkRC )
    {
      rc = cwLogError(rc,"Syntax error on variable cfg. at index %i.",i);
      goto errLabel;
    }

    if((rc = register_variable(hRef,cfg,in_var_id,out_var_id)) != kOkRC )
    {
      rc = cwLogError(rc,"'%s' variable registration failed.",cwStringNullGuard(cfg.o_label));
      goto errLabel;
    }    
  }
  
errLabel:
  if(rc != kOkRC )
  {
    rc = cwLogError(rc,"Auto-range instantiation failed.");
    destroy(hRef);
  }
  
  return rc;
}


cw::rc_t cw::auto_range::create( handle_t& hRef, unsigned in_var_cnt, unsigned out_var_cnt, unsigned history_cnt )
{
  rc_t rc = kOkRC;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  auto_range_t* p = mem::allocZ<auto_range_t>();

  p->i_varAllocN = in_var_cnt;
  p->o_varAllocN = out_var_cnt;
  p->i_varA = mem::allocZ<i_var_t>(p->i_varAllocN);
  p->o_varA = mem::allocZ<o_var_t>(p->o_varAllocN);
  for(unsigned i=0; i<p->i_varAllocN; ++i)
  {
    i_var_t* var = p->i_varA + i;

    var->bufAllocN = history_cnt;
    var->bufA = mem::allocZ<double>(var->bufAllocN);    
  }

  hRef.set(p);
errLabel:
  if( rc != kOkRC )
    _destroy(p);
  return rc;
}

cw::rc_t cw::auto_range::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if(!hRef.isValid())
    return rc;

  auto_range_t* p = _handleToPtr(hRef);
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;
}

cw::rc_t cw::auto_range::reset( handle_t h )
{
  rc_t rc = kOkRC;
  auto_range_t* p = _handleToPtr(h);

  for(unsigned i=0; i<p->i_varN; ++i)
  {
    vop::zero(p->i_varA[i].bufA,p->i_varA[i].bufAllocN);
    
    p->i_varA[i].init_fl   = false;
    p->i_varA[i].i_buf_idx = 0;
    p->i_varA[i].bufN      = 0;
    p->i_varA[i].sum       = 0;
    p->i_varA[i].mean      = 0;
    p->i_varA[i].std_dev   = 0;
    p->i_varA[i].i_value   = 0;
  }

  for(unsigned i=0; i<p->o_varN; ++i)
    p->o_varA[i].o_value = 0;
  
  return rc;
}

cw::rc_t cw::auto_range::register_variable( handle_t h, const cfg_t& cfg, unsigned& in_var_id_ref, unsigned& out_var_id_ref  )
{
  rc_t           rc  = kOkRC;
  auto_range_t*  p   = _handleToPtr(h);
  const i_var_t* i_var = nullptr;
  o_var_t* o_var = nullptr;
  
  if( p->o_varN >= p->o_varAllocN )
  {
    rc = cwLogError(kBufTooSmallRC,"All output variables in use. Variable registration failed.");
    goto errLabel;
  }

  if( _o_label_to_var(p,cfg.o_label) != nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The output variable label '%s' is already in use.",cwStringNullGuard(cfg.o_label));
    goto errLabel;
  }

  // if this input variable has not yet been allocated ...
  if(( i_var = _i_label_to_var(p,cfg.i_label)) == nullptr )
  {
    // ... and there is an available slot in p->i_varA[]
    if( p->i_varN >= p->i_varAllocN )
    {
      rc = cwLogError(kBufTooSmallRC,"All input variables in use. Variable registration failed.");
      goto errLabel;
    }

    // then allocate the input variable
    p->i_varA[ p->i_varN ].label = mem::duplStr(cfg.i_label);
    p->i_varA[ p->i_varN ].id    = p->i_varN;
    
    i_var = p->i_varA + p->i_varN;
    
    p->i_varN += 1;
  }
  
  o_var = p->o_varA + p->o_varN;

  out_var_id_ref = p->o_varN;
  in_var_id_ref  = i_var->id;
  
  p->o_varN += 1;

  o_var->id          = out_var_id_ref;
  o_var->label       = mem::duplStr(cfg.o_label);
  o_var->ivar        = i_var;
  o_var->cfg         = cfg;
  o_var->cfg.o_label = o_var->label;


  if( o_var->cfg.dev_mult == 0 )
    o_var->cfg.dev_mult = 1.0;
  
  if( o_var->cfg.bool_threshold == 0 )
    o_var->cfg.bool_threshold = 0.5;

errLabel:
  return rc;
}

unsigned cw::auto_range::in_variable_label_to_id( handle_t h, const char* label )
{
  auto_range_t* p = _handleToPtr(h);
  const i_var_t* var = nullptr;

  if((var = _i_label_to_var( p, label )) == nullptr )
    return kInvalidId;
    
  return var->id;
}

cw::rc_t cw::auto_range::in_variable_label_to_id( handle_t h, const char* label, unsigned& id_ref )
{
  rc_t     rc = kOkRC;
  unsigned id = kInvalidId;
  
  id_ref = kInvalidId;
  
  if((id = in_variable_label_to_id(h,label)) == kInvalidId )
  {
    rc = cwLogError(kInvalidArgRC,"The auto-range input variable '%s' was not found.",cwStringNullGuard(label));
    goto errLabel;
  }

  id_ref = id;
errLabel:
  return rc;
}

unsigned cw::auto_range::out_variable_label_to_id( handle_t h, const char* label )
{
  auto_range_t* p = _handleToPtr(h);
  const o_var_t* var = nullptr;

  if((var = _o_label_to_var( p, label )) == nullptr )
    return kInvalidId;
    
  return var->id;
}

cw::rc_t cw::auto_range::out_variable_label_to_id( handle_t h, const char* label, unsigned& id_ref )
{
  rc_t     rc = kOkRC;
  unsigned id = kInvalidId;
  
  id_ref = kInvalidId;
  
  if((id = out_variable_label_to_id(h,label)) == kInvalidId )
  {
    rc = cwLogError(kInvalidArgRC,"The auto-range input variable '%s' was not found.",cwStringNullGuard(label));
    goto errLabel;
  }

  id_ref = id;
errLabel:
  return rc;
}

unsigned cw::auto_range::out_variable_count( handle_t h )
{
  auto_range_t* p = _handleToPtr(h);
  return p->o_varN;
}

    
const cw::auto_range::cfg_t* cw::auto_range::out_variable_index_to_cfg( handle_t h, unsigned index )
{
  auto_range_t* p = _handleToPtr(h);
  if( index >= p->o_varN )
  {    
    return nullptr;
  }

  return &p->o_varA[index].cfg;  
}

const cw::auto_range::cfg_t* cw::auto_range::out_variable_id_to_cfg( handle_t h, unsigned variable_id )
{
  auto_range_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->o_varN; ++i)
    if( p->o_varA[i].id == variable_id )
      return &p->o_varA[i].cfg;
  
  return nullptr;
}

cw::rc_t cw::auto_range::_update_variable( handle_t h, double sec, unsigned i_var_id, unsigned value )
{
  double v = value;
  return _update_variable(h,sec, i_var_id,v);
}

cw::rc_t cw::auto_range::_update_variable( handle_t h, double sec, unsigned i_var_id, float    value )
{
  double v = value;
  return _update_variable(h,sec, i_var_id,v);
}

cw::rc_t cw::auto_range::_update_variable( handle_t h, double sec, unsigned i_var_id, double   value )
{
  rc_t          rc      = kOkRC;
  auto_range_t* p       = _handleToPtr(h);
  i_var_t*      var     = nullptr;
  double        dev_sum = 0;
  
  if( i_var_id >= p->i_varN )
  {
    rc = cwLogError(kInvalidArgRC,"The variable id %i is out of variable id range %i.",i_var_id, p->i_varN);
    goto errLabel;
  }

  var = p->i_varA + i_var_id;

  var->sum -= var->bufA[ var->i_buf_idx ];
  var->sum += value;
  
  var->bufA[ var->i_buf_idx ] = value;
  var->i_buf_idx = (var->i_buf_idx + 1) % var->bufAllocN;
  var->bufN    = std::min(var->bufN+1,var->bufAllocN);

  var->mean = var->sum / var->bufN;
  for(unsigned i=0; i<var->bufN; ++i)
  {
    double dev = var->bufA[i] - var->mean;
    dev_sum += dev*dev;
  }

  var->std_dev = std::sqrt(dev_sum/var->bufN);

  var->i_value = value;

  var->init_fl = true;

errLabel:
  return rc;
}

cw::rc_t cw::auto_range::_get_value( handle_t h, double sec, unsigned o_var_id, bool& value_ref )
{
  rc_t rc = kOkRC;
  auto_range_t* p = _handleToPtr(h);
  double o_value;
      
  if((rc = get_value(h,sec,o_var_id,o_value)) != kOkRC )
    return rc;

  value_ref = o_value > p->o_varA[o_var_id].cfg.bool_threshold;
  
  return rc;
}

cw::rc_t cw::auto_range::_get_value( handle_t h, double sec, unsigned o_var_id, int& value_ref )
{
  rc_t rc = kOkRC;
  double o_value;
  if((rc = get_value(h,sec,o_var_id,o_value)) != kOkRC )
    return rc;

  value_ref = (int)std::round(o_value);
  
  return rc;
}

cw::rc_t cw::auto_range::_get_value( handle_t h, double sec, unsigned o_var_id, unsigned& value_ref )
{
  rc_t rc = kOkRC;
  double o_value;
  if((rc = get_value(h,sec,o_var_id,o_value)) != kOkRC )
    return rc;

  value_ref = (unsigned)std::round(o_value);
  
  return rc;
}

cw::rc_t cw::auto_range::_get_value( handle_t h, double sec, unsigned o_var_id, float& value_ref )
{
  rc_t rc = kOkRC;
  double o_value;
  if((rc = get_value(h,sec,o_var_id,o_value)) != kOkRC )
    return rc;

  value_ref = (float)std::round(o_value);
  
  return rc;
}

cw::rc_t cw::auto_range::_get_value( handle_t h, double sec, unsigned o_var_id, double&   value_ref )
{
  rc_t          rc      = kOkRC;
  auto_range_t* p       = _handleToPtr(h);

  if( o_var_id >= p->o_varN )
  {
    return cwLogError(kInvalidArgRC,"The variable id %i is out of variable id range %i.",o_var_id, p->o_varN);
  }
  
  o_var_t*        var     = p->o_varA + o_var_id;

  if( !var->ivar->init_fl )
  {
    var->o_value = var->cfg.default_out_value;
    value_ref = var->cfg.default_out_value;
    return rc;
  }
  
  double        min_val = var->ivar->mean - (var->ivar->std_dev*var->cfg.dev_mult);
  double        max_val = var->ivar->mean + (var->ivar->std_dev*var->cfg.dev_mult);
  double        i_val   = var->ivar->i_value;
  
  if( i_val < min_val )
    i_val = min_val;
  
  if( i_val > max_val )
    i_val = max_val;

  // convert i_val to unit range
  i_val = min_val + (i_val - min_val)/(max_val - min_val);

  // calculate the pre-filter output value
  double o_val = var->cfg.min_out_value + (i_val * (var->cfg.max_out_value - var->cfg.min_out_value));

  // calculate the filter output value
  var->o_value = (1.0-var->cfg.out_filter_coeff) * o_val + (var->cfg.out_filter_coeff * var->o_value);

  // assign the return value
  value_ref = var->o_value;
  
  return rc;
}


void cw::auto_range::report( handle_t h )
{
  auto_range_t* p = _handleToPtr(h);

  for(unsigned i=0; i<p->o_varN; ++i)
  {
    o_var_t* var = p->o_varA + i;
    cwLogPrint("%i %16s u:%f sd:%f : i:%f o:%f\n",i,var->label,var->ivar->mean,var->ivar->std_dev,var->ivar->i_value,var->o_value);
  }
  
}
