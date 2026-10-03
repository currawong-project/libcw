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
    typedef struct var_str
    {
      char*    label;       // variable label
      unsigned id;          // variable id - same as index in to auto_range_t.varA[]
      cfg_t    cfg;         // variable config. record
      double*  bufA;        // bufA[ bufAllocN ]  - circular buffer
      unsigned bufAllocN;   // 
      unsigned bufN;        // bufN - count of filled elements in bufA[]
      unsigned i_buf_idx;   // next input element in bufA[]

      bool  init_fl; // Set if at least one valid input value has been received.

      
      double sum;      // sum of values in bufA[]
      double mean;     // mean of values in bufA[] 
      double std_dev;  // std-dev of values in bufA[]

      double i_value;  // last input value
      double o_value;  // last output value
      
    } var_t;
    
    typedef struct auto_range_str
    {
      var_t*   varA;
      unsigned varAllocN;
      unsigned varN;
      
    } auto_range_t;

    auto_range_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,auto_range_t>(h); }
  
    rc_t _destroy( auto_range_t* p )
    {
      for(unsigned i=0; i<p->varN; ++i)
      {
        mem::release(p->varA[i].bufA);
        mem::release(p->varA[i].label);
      }
      
      mem::release(p->varA);
      mem::release(p);
      return kOkRC;
    }

    const var_t* _label_to_var( const auto_range_t* p, const char* label )
    {
      for(unsigned i=0; i<p->varN; ++i)
        if( textIsEqual(p->varA[i].label,label))
          return p->varA + i;
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

  if((rc = create(hRef,var_cnt,history_cnt)) != kOkRC )
  {
    goto errLabel;
  }

  for(unsigned i=0; i<var_cnt; ++i)
  {    
    unsigned        var_id  = kInvalidId;
    const object_t* var_cfg = varL->child_ele(i);
    cfg_t cfg{};

    if(!var_cfg->is_dict())
    {
      rc = cwLogError(kSyntaxErrorRC,"The variable cfg at index %i is not a dictionary.",i);
      goto errLabel;
    }

    if((rc = var_cfg->getv("label",cfg.label,
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

    if((rc = register_variable(hRef,cfg,var_id)) != kOkRC )
    {
      rc = cwLogError(rc,"'%s' variable registration failed.",cwStringNullGuard(cfg.label));
      goto errLabel;
    }    
  }
  
errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"Auto-range instantiation failed.");
  
  return rc;
}


cw::rc_t cw::auto_range::create( handle_t& hRef, unsigned value_cnt, unsigned history_cnt )
{
  rc_t rc = kOkRC;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  auto_range_t* p = mem::allocZ<auto_range_t>();

  p->varA = mem::allocZ<var_t>(value_cnt);
  for(unsigned i=0; i<value_cnt; ++i)
  {
    var_t* var = p->varA + i;

    var->bufAllocN = history_cnt;
    var->bufA = mem::allocZ<double>(var->bufAllocN);    
  }
    
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

  for(unsigned i=0; i<p->varN; ++i)
  {
    vop::zero(p->varA[i].bufA,p->varA[i].bufAllocN);
    
    p->varA[i].init_fl   = false;
    p->varA[i].i_buf_idx = 0;
    p->varA[i].bufN      = 0;
    p->varA[i].sum       = 0;
    p->varA[i].mean      = 0;
    p->varA[i].std_dev   = 0;
    p->varA[i].i_value   = 0;
    p->varA[i].o_value   = 0;
  } 
  return rc;
}

cw::rc_t cw::auto_range::register_variable( handle_t h, const cfg_t& cfg, unsigned var_id_ref )
{
  rc_t          rc  = kOkRC;
  auto_range_t* p   = _handleToPtr(h);
  var_t*        var = nullptr;
  
  if( p->varN >= p->varAllocN )
  {
    rc = cwLogError(kBufTooSmallRC,"All variables in use. Variable registration failed.");
    goto errLabel;
  }

  if( _label_to_var(p,cfg.label) != nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The variable label '%s' is already in use.",cwStringNullGuard(cfg.label));
    goto errLabel;
  }

  
  var = p->varA + p->varN;

  var_id_ref = p->varN;
  
  p->varN += 1;

  var->id        = var_id_ref;
  var->label     = mem::duplStr(cfg.label);
  var->cfg       = cfg;
  var->cfg.label = var->label;


  if( var->cfg.dev_mult == 0 )
    var->cfg.dev_mult = 1.0;
  
  if( var->cfg.bool_threshold == 0 )
    var->cfg.bool_threshold = 0.5;

errLabel:
  return rc;
}


unsigned cw::auto_range::variable_label_to_id( handle_t h, const char* label )
{
  auto_range_t* p = _handleToPtr(h);
  const var_t* var = nullptr;

  if((var = _label_to_var( p, label )) == nullptr )
    return kInvalidId;
    
  return var->id;
}

unsigned cw::auto_range::variable_count( handle_t h )
{
  auto_range_t* p = _handleToPtr(h);
  return p->varN;
}

    
const cw::auto_range::cfg_t* cw::auto_range::index_to_cfg( handle_t h, unsigned index )
{
  auto_range_t* p = _handleToPtr(h);
  if( index >= p->varN )
  {    
    return nullptr;
  }

  return &p->varA[index].cfg;  
}

const cw::auto_range::cfg_t* cw::auto_range::id_to_cfg( handle_t h, unsigned variable_id )
{
  auto_range_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->varN; ++i)
    if( p->varA[i].id == variable_id )
      return &p->varA[i].cfg;
  
  return nullptr;
}




cw::rc_t cw::auto_range::update_variable( handle_t h, unsigned var_id, const unsigned& value )
{
  double v = value;
  return update_variable(h,var_id,v);
}

cw::rc_t cw::auto_range::update_variable( handle_t h, unsigned var_id, const double&   value )
{
  rc_t          rc  = kOkRC;
  auto_range_t* p   = _handleToPtr(h);
  var_t*        var = nullptr;
  double        dev_sum = 0;
  
  if( var_id >= p->varN )
  {
    rc = cwLogError(kInvalidArgRC,"The variable id %i is out of variable id range %i.",var_id, p->varN);
    goto errLabel;
  }

  var = p->varA + var_id;

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

cw::rc_t cw::auto_range::get_value( handle_t h, double sec, unsigned var_id, bool& value_ref )
{
  rc_t rc = kOkRC;
  auto_range_t* p = _handleToPtr(h);
  double o_value;
      
  if((rc = get_value(h,sec,var_id,o_value)) != kOkRC )
    return rc;

  value_ref = o_value > p->varA[var_id].cfg.bool_threshold;
  
  return rc;
}

cw::rc_t cw::auto_range::get_value( handle_t h, double sec, unsigned var_id, int& value_ref )
{
  rc_t rc = kOkRC;
  double o_value;
  if((rc = get_value(h,sec,var_id,o_value)) != kOkRC )
    return rc;

  value_ref = (int)std::round(o_value);
  
  return rc;
}

cw::rc_t cw::auto_range::get_value( handle_t h, double sec, unsigned var_id, unsigned& value_ref )
{
  rc_t rc = kOkRC;
  double o_value;
  if((rc = get_value(h,sec,var_id,o_value)) != kOkRC )
    return rc;

  value_ref = (unsigned)std::round(o_value);
  
  return rc;
}

cw::rc_t cw::auto_range::get_value( handle_t h, double sec, unsigned var_id, double&   value_ref )
{
  rc_t          rc      = kOkRC;
  auto_range_t* p       = _handleToPtr(h);

  if( var_id >= p->varN )
  {
    return cwLogError(kInvalidArgRC,"The variable id %i is out of variable id range %i.",var_id, p->varN);
  }
  
  var_t*        var     = p->varA + var_id;

  if( !var->init_fl )
  {
    var->o_value = var->cfg.default_out_value;
    value_ref = var->cfg.default_out_value;
    return rc;
  }
  
  double        min_val = var->mean - (var->std_dev*var->cfg.dev_mult);
  double        max_val = var->mean + (var->std_dev*var->cfg.dev_mult);
  double        i_val   = var->i_value;
  
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

  for(unsigned i=0; i<p->varN; ++i)
  {
    var_t* var = p->varA + i;
    cwLogPrint("%i %16s u:%f sd:%f : i:%f o:%f\n",i,var->label,var->mean,var->std_dev,var->i_value,var->o_value);
  }
  
}
