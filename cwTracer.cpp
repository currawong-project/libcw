//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwTracer.h"
#include "cwText.h"

namespace cw
{
  namespace tracer
  {
    handle_t _global_handle;
    
    typedef struct recd_str
    {
      struct timespec time;
      unsigned trace_id;
      unsigned event_id;
      unsigned user_data_0;
      unsigned user_data_1;
    } recd_t;

    typedef struct trace_str
    {
      char*    label;
      unsigned label_id;
      unsigned id;      
    } trace_t;
    
    typedef struct tracer_str
    {
      recd_t* recdA;
      unsigned recdN;
      std::atomic<unsigned> recd_idx;

      trace_t* traceA;
      unsigned traceN;
      unsigned trace_idx;

      char* out_fname;
      bool activate_fl;
      bool enable_fl;
      
    } tracer_t;

    tracer_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,tracer_t>(h); }

    rc_t _destroy( tracer_t* p )
    {
      rc_t rc = kOkRC;

      if( p != nullptr )
      {
        for(unsigned i=0; i<p->traceN; ++i)
          mem::release(p->traceA[i].label);
      
        mem::release(p->traceA);
        mem::release(p->recdA);
        mem::release(p->out_fname);
        mem::release(p);
      }
      
      return rc;
    }

    const trace_t* _find( tracer_t* p, const char* label, unsigned label_id )
    {
      for(unsigned i=0; i<p->traceN; ++i)
        if( textIsEqual(p->traceA[i].label,label) && p->traceA[i].label_id == label_id )
          return p->traceA + i;

      return nullptr;
    }

    rc_t _write_data( tracer_t* p )
    {
      rc_t           rc    = kOkRC;
      char*          fname = nullptr;
      file::handle_t fH;

      fname = mem::printf(fname,"%s_data.csv",p->out_fname);
      
      if((rc = file::open(fH,fname,file::kWriteFl)) != kOkRC )
      {
        goto errLabel;
      }

      file::printf(fH,"seconds,nseconds,trace_id,event_id,user0,user1\n");
      for(unsigned i=0; i<std::min(p->recdN,p->recd_idx.load()); ++i)
      {
        const recd_t* r = p->recdA + i;
        file::printf(fH,"%i,%i,%i,%i,%i,%i\n",r->time.tv_sec,r->time.tv_nsec,r->trace_id,r->event_id,r->user_data_0,r->user_data_1);
      }

    errLabel:
      file::close(fH);

      if( rc != kOkRC )
        rc = cwLogError(rc,"Tracer data write failed.");

      return rc;      
    }

    rc_t _write_ref( tracer_t* p )
    {
      rc_t           rc    = kOkRC;
      char*          fname = nullptr;
      file::handle_t fH;
      
      fname = mem::printf(fname,"%s_ref.csv",p->out_fname);

      if((rc = file::open(fH,fname,file::kWriteFl)) != kOkRC )
      {
        goto errLabel;
      }

      file::printf(fH,"id,label,label_id\n");
      for(unsigned i=0; i<p->trace_idx; ++i)
      {
        const trace_t* t = p->traceA + i;
        file::printf(fH,"%i,%s,%i\n",t->id,t->label,t->label_id);
      }

    errLabel:
      file::close(fH);

      if( rc != kOkRC )
        rc = cwLogError(rc,"Tracer reference write failed.");


      return rc;
      
    }
    
  }  
}

cw::rc_t cw::tracer::create( handle_t& hRef, const object_t* cfg )
{
  rc_t     rc            = kOkRC;
  unsigned max_trace_cnt = 0;
  unsigned max_msg_cnt   = 0;
  bool     enable_fl     = false;
  bool     activate_fl   = false;
  char*    out_fname     = nullptr;
  
  if((rc = cfg->getv("trace_cnt",max_trace_cnt,
                     "msg_cnt",max_msg_cnt,
                     "enable_fl",enable_fl,
                     "activate_fl",activate_fl,
                     "out_fname",out_fname)) != kOkRC )
  {
    goto errLabel;
  }

  rc = create(hRef,max_trace_cnt,max_msg_cnt,enable_fl,activate_fl,out_fname);

errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"Tracer create failed.");

  return rc;
}


cw::rc_t cw::tracer::create( handle_t& hRef, unsigned max_trace_cnt, unsigned max_msg_cnt, bool enable_fl, bool activate_fl, const char* out_fname )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  tracer_t* p = mem::allocZ<tracer_t>();
  
#ifndef cwTRACER
  enable_fl = false;
#else  
  p->recdA = mem::allocZ<recd_t>( max_msg_cnt );
  p->recdN = max_msg_cnt;
  p->recd_idx.store(0);
  
  p->traceA = mem::allocZ<trace_t>(max_trace_cnt );
  p->traceN = max_trace_cnt;
  p->trace_idx= 0;
#endif

  cwLogInfo("The tracer is %s.", enable_fl ? "ENABLED" : "DISABLED");
  
  p->out_fname   = mem::duplStr(out_fname);
  p->activate_fl = activate_fl;
  p->enable_fl   = enable_fl;
  
  hRef.set(p);
  
  return rc;
}

cw::rc_t  cw::tracer::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  tracer_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}

cw::rc_t cw::tracer::activate( handle_t h, bool activate_fl )
{
  tracer_t* p = _handleToPtr(h);
  p->activate_fl = activate_fl;
  return kOkRC;
}

cw::rc_t cw::tracer::register_trace( handle_t h, const char* label, unsigned label_id, unsigned& trace_id_ref )
{
  rc_t          rc = kOkRC;
  tracer_t*     p  = _handleToPtr(h);
  const trace_t* t  = nullptr;
    
  trace_id_ref = kInvalidId;

  if( p->trace_idx >= p->traceN )
  {
    rc = cwLogError(kBufTooSmallRC,"The trace registry is full (%i>=%i). Trace '%s:%i' was not registered.", p->trace_idx,p->traceN, cwStringNullGuard(label), label_id);
    goto errLabel;
  }

  if((t = _find( p, label, label_id )) != nullptr )
  {
    cwLogWarning("The trace '%s:%i' already exists.",cwStringNullGuard(label),label_id);
    trace_id_ref = t->id;
  }
  else
  {
    p->traceA[ p->trace_idx ].label    = mem::duplStr(label);
    p->traceA[ p->trace_idx ].label_id = label_id;
    p->traceA[ p->trace_idx ].id       = p->trace_idx;
    
    trace_id_ref = p->trace_idx;
  
    p->trace_idx += 1;
  }
  
errLabel:
  return rc;
}

cw::rc_t cw::tracer::log_trace_time( handle_t h, unsigned trace_id, unsigned event_id, unsigned user_data_0, unsigned user_data_1 )
{
  rc_t      rc = kOkRC;
  tracer_t* p  = _handleToPtr(h);

  if( p->enable_fl && p->activate_fl )
  {
    unsigned idx = p->recd_idx.fetch_add(1);
    
    if( idx < p->recdN )
    {
      recd_t* r = p->recdA + idx;
      clock_gettime(CLOCK_MONOTONIC,&r->time);
      r->trace_id    = trace_id;
      r->event_id    = event_id;
      r->user_data_0 = user_data_0;
      r->user_data_1 = user_data_1;
    }
  }
  
  return rc;
}

cw::rc_t cw::tracer::log_trace_data( handle_t h, unsigned trace_id, unsigned event_id, unsigned user_data_0, unsigned user_data_1  )
{
  rc_t      rc = kOkRC;
  tracer_t* p  = _handleToPtr(h);

  if( p->enable_fl && p->activate_fl )
  {
    unsigned idx = p->recd_idx.fetch_add(1);
    
    if( idx < p->recdN )
    {
      recd_t* r = p->recdA + idx;
      r->trace_id    = trace_id;
      r->event_id    = event_id;
      r->user_data_0 = user_data_0;
      r->user_data_1 = user_data_1;
    }
  }
  
  return rc;
}

cw::rc_t cw::tracer::write( handle_t h )
{
  rc_t           rc = kOkRC;
  tracer_t*      p  = _handleToPtr(h);

  if( p->enable_fl)
  {
    if((rc = _write_ref(p)) != kOkRC )
      goto errLabel;
    
    if((rc = _write_data(p)) != kOkRC )
      goto errLabel;
  }
  
errLabel:
  return rc;
  
}


void cw::tracer::set_global_handle( handle_t h ) { cw::tracer::_global_handle = h; }
cw::tracer::handle_t cw::tracer::global_handle() { return cw::tracer::_global_handle; }
