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
#include "cwDspTransforms.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
#include "cwFlowProc.h"
#include "cwFlowCross.h"


namespace cw
{
  namespace flow_cross
  {
    enum
    {
      kInactiveStateId,
      kActiveStateId,
      kFadeInStateId,
      kFadeOutStateId,
    };

    typedef struct flow_network_str
    {
      dsp::recorder::obj_t*     recorder;
      
      flow::external_device_t* deviceA;
      unsigned                 deviceN;
      flow::handle_t           flowH;
      
      unsigned                 stateId;   // inactive, fade-in, fade-out
      double                   fadeGain;  //    0        0->1     1->0
      unsigned                 fadeSmpN;  //

      unsigned                 net_idx;
      
    } flow_network_t;
    
    typedef struct flow_cross_str
    {
      unsigned cur_idx;
      double   srate;
      
      unsigned        netN;
      flow_network_t* netA;
      
      flow::external_device_t* deviceA;
      unsigned                 deviceN;
      
    } flow_cross_t;
    
    flow_cross_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,flow_cross_t>(h); }

    void _destroy_network( flow_network_t* net )
    {
      for(unsigned i=0; i<net->deviceN; ++i)
        if( net->deviceA[i].typeId == flow::kAudioDevTypeId )
        {
          mem::release(net->deviceA[i].u.a.abuf);
        }
      mem::release(net->deviceA);
      flow::destroy(net->flowH);

      char fname[256];

      snprintf(fname,256,"/home/kevin/temp/temp_%i.json",net->net_idx);
      
      dsp::recorder::write(net->recorder,fname);
      dsp::recorder::destroy( net->recorder );
    }

    rc_t _destroy( flow_cross_t* p )
    {
      rc_t rc = kOkRC;

      for(unsigned i=0; i<p->netN; ++i)
        _destroy_network( p->netA + i );
      
      mem::release(p->netA);
      mem::release(p);
      return rc;
    }

    flow::abuf_t* _clone_abuf( const flow::abuf_t* src )
    {
      flow::abuf_t* b = mem::allocZ<flow::abuf_t>();
      memcpy(b,src,sizeof(flow::abuf_t));
      b->buf          = mem::allocZ<flow::sample_t>( b->frameN * b->chN );
      return b;
    }
    
    flow::external_device_t* _clone_external_device_array( const flow::external_device_t* srcDevA, unsigned devN )
    {
      flow::external_device_t* devA = mem::allocZ<flow::external_device_t>(devN);
      memcpy(devA,srcDevA,devN * sizeof(flow::external_device_t));
      
      for(unsigned i=0; i<devN; ++i)
        if( devA[i].typeId == flow::kAudioDevTypeId )
          devA[i].u.a.abuf = _clone_abuf( srcDevA[i].u.a.abuf );
      
      return devA;
    }

    rc_t _create_network( flow_cross_t*            p,
                          unsigned                 net_idx,
                          double                   srate,
                          const object_t&          classCfg,
                          const object_t&          networkCfg,
                          flow::external_device_t* deviceA,
                          unsigned                 deviceN )
    {
      rc_t            rc;
      flow_network_t* net = p->netA + net_idx;

      dsp::recorder::create( net->recorder, srate, 10.0, 1 );
      
      net->deviceA = _clone_external_device_array( deviceA, deviceN );
      net->deviceN = 0;
      net->stateId = net_idx == 0 ? kActiveStateId : kInactiveStateId;
      net->net_idx = net_idx;
      
      if((rc = flow::create( net->flowH, classCfg, networkCfg, net->deviceA, deviceN )) == kOkRC )
        net->deviceN += 1;
      else
      {
        cwLogError(rc,"Flow cross index %i network created failed.",net_idx);
        goto errLabel;
      }

    errLabel:
      if(rc != kOkRC )
        _destroy_network(net);
      
      return rc;
    }

    void _fade_audio( const flow::abuf_t* src, flow::abuf_t* dst, flow_network_t* net )
    {
      unsigned frameN = std::min(dst->frameN,src->frameN);
      unsigned chN    = std::min(dst->chN,src->chN);
      
      double   dd     = (double)frameN / net->fadeSmpN; // change in gain over frameN samples
      double   bf     = net->fadeGain; // starting gain
      double   ef;
      
      switch( net->stateId )
      {
        case kFadeInStateId:    ef = bf + dd; break;
        case kFadeOutStateId:   ef = bf - dd; break;
        case kActiveStateId:    ef = 1;       break;
        case kInactiveStateId:  ef = 0;       break;
        default:
          assert(0);
          
      }

      // pin the ending gain in the range 0.0 to 1.0
      ef = std::min(1.0,std::max(0.0,ef));

      dd = ef - bf;             // gain change

      flow::sample_t BUF[ chN * frameN ];
      
      //memcpy(dst->buf,src->buf,frameN*chN*sizeof(flow::sample_t));


      // apply gain slope to the src signal and sum the result into the output buffer
      for(unsigned j = 0; j<chN; ++j)
      {
        flow::sample_t* dbuf = dst->buf + (j*frameN);
        flow::sample_t* sbuf = src->buf + (j*frameN);
        flow::sample_t* rbuf = BUF + (j*frameN);
        
        for(unsigned i = 0; i<frameN; ++i)
        {
          flow::sample_t g = bf + (dd*i/frameN);
          dbuf[i] += (flow::sample_t)(g * sbuf[i]);

          rbuf[i] = g;
          //dbuf[i] = sbuf[i];
        }
      }


      dsp::recorder::exec( net->recorder, BUF, chN, frameN );
      
      
      net->fadeGain += dd; 

      if( net->stateId == kFadeInStateId && ef == 1.0 )
        net->stateId  = kActiveStateId;
      
      if( net->stateId == kFadeOutStateId && ef == 0.0 )
        net->stateId = kInactiveStateId;
      
    }

    void _update_audio_input( flow_cross_t* p, flow_network_t* net, unsigned devIdx )
    {
      flow::abuf_t* src = p->deviceA[devIdx].u.a.abuf;
      flow::abuf_t* dst = net->deviceA[devIdx].u.a.abuf;

      memset( dst->buf, 0, dst->chN * dst->frameN * sizeof(flow::sample_t));
      
      _fade_audio( src, dst, net );
    }

    void _zero_audio_output( flow_cross_t* p, flow_network_t* net, unsigned devIdx )
    {
      flow::abuf_t* dst = net->deviceA[devIdx].u.a.abuf;
      memset(dst->buf,0,dst->chN*dst->frameN*sizeof(flow::sample_t));
    }
    
    void _update_audio_output( flow_cross_t* p, flow_network_t* net, unsigned devIdx )
    {
      flow::abuf_t* src = net->deviceA[devIdx].u.a.abuf;
      flow::abuf_t* dst = p->deviceA[devIdx].u.a.abuf;

      assert( net->deviceA[ devIdx ].flags == flow::kOutFl  && net->deviceA[ devIdx ].typeId==flow::kAudioDevTypeId);

      _fade_audio( src, dst, net );
    }

    unsigned _get_flow_index( flow_cross_t* p, destId_t destId )
    {
      unsigned flow_idx = kInvalidIdx;
  
      switch( destId )
      {
        case kCurDestId:
          flow_idx = p->cur_idx;
          break;
          
        case kNextDestId:
          flow_idx = (p->cur_idx + 1) % p->netN;
          break;
          
        default:
        assert(0);
    
      }  
      return flow_idx;
    }

    template< typename T >
    rc_t _set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, const T& value )
    {
      //rc_t          rc       = kOkRC;
      flow_cross_t* p        = _handleToPtr(h);
      unsigned      flow_idx = _get_flow_index(p, destId );
      return set_variable_value( p->netA[ flow_idx ].flowH, inst_label, var_label, chIdx, value );
  
    }

    
  }
}



cw::rc_t cw::flow_cross::create( handle_t&                hRef,                 
                                 const object_t&          classCfg,
                                 const object_t&          networkCfg,
                                 double                   srate,
                                 unsigned                 crossN,
                                 flow::external_device_t* deviceA,
                                 unsigned                 deviceN)
{
  rc_t rc;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  // There must be at least 2 networks to cross fade between
  if( crossN < 2 )
    crossN = 2;

  flow_cross_t* p = mem::allocZ<flow_cross_t>();
  p->netA         = mem::allocZ<flow_network_t>( crossN );
  p->netN         = 0;
  p->srate        = srate;
  p->deviceA = deviceA;
  p->deviceN = deviceN;

  for(unsigned i=0; i<crossN; ++i)
    if((rc = _create_network(p,i,srate,classCfg,networkCfg,deviceA,deviceN)) == kOkRC )
      p->netN += 1;
    else
    {
      rc = cwLogError(rc,"Sub-network index '%i' create failed.",i);
      goto errLabel;
    }

  hRef.set(p);
    
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
  
}

cw::rc_t cw::flow_cross::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  flow_cross_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;  
}


cw::rc_t cw::flow_cross::exec_cycle( handle_t h )
{
  rc_t          rc = kOkRC;
  flow_cross_t* p = _handleToPtr(h);

  for(unsigned i=0; i<p->netN; ++i)
  {
    flow_network_t* net = p->netA + i;

    if( net->stateId != kInactiveStateId )
    {

      for(unsigned j=0; j<p->deviceN; ++j)
        if( p->deviceA[j].typeId == flow::kAudioDevTypeId && cwIsFlag(p->deviceA[j].flags, flow::kInFl ) )
          _update_audio_input( p, p->netA + i, j );

      for(unsigned j=0; j<p->deviceN; ++j)
        if( p->deviceA[j].typeId == flow::kAudioDevTypeId && cwIsFlag(p->deviceA[j].flags, flow::kOutFl ) )
        _zero_audio_output( p, net, j );
        

      flow::exec_cycle( net->flowH );

      for(unsigned j=0; j<p->deviceN; ++j)
        if( p->deviceA[j].typeId == flow::kAudioDevTypeId && cwIsFlag(p->deviceA[j].flags, flow::kOutFl ) )
          _update_audio_output( p, net, j );
    }
  }
  
  return rc;
}

/*
cw::rc_t cw::flow_cross::exec_cycle( handle_t h )
{
  rc_t          rc = kOkRC;
  flow_cross_t* p = _handleToPtr(h);
  flow_network_t* net = p->netA;

  
  flow::exec_cycle( net->flowH );
  
  for(unsigned j=0; j<p->deviceN; ++j)
    if( p->deviceA[j].typeId == flow::kAudioDevTypeId && cwIsFlag(p->deviceA[j].flags, flow::kOutFl ) )
      _update_audio_output( p, net, j );
  
  return rc;
}
*/



cw::rc_t cw::flow_cross::apply_preset( handle_t h, destId_t destId, const char* presetLabel )
{
  rc_t          rc       = kOkRC;
  flow_cross_t* p        = _handleToPtr(h);
  unsigned      flow_idx = _get_flow_index(p, destId );
      
  if((rc = flow::apply_preset( p->netA[flow_idx].flowH, presetLabel )) != kOkRC )
    rc = cwLogError(rc,"Preset application '%s' failed.",cwStringNullGuard(presetLabel));

  return rc;  
}

cw::rc_t cw::flow_cross::set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, bool value )
{  return _set_variable_value(h,destId,inst_label,var_label,chIdx,value); }

cw::rc_t cw::flow_cross::set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, int value )
{  return _set_variable_value(h,destId,inst_label,var_label,chIdx,value); }

cw::rc_t cw::flow_cross::set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value )
{  return _set_variable_value(h,destId,inst_label,var_label,chIdx,value); }

cw::rc_t cw::flow_cross::set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, float value )
{  return _set_variable_value(h,destId,inst_label,var_label,chIdx,value); }

cw::rc_t cw::flow_cross::set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, double value )
{  return _set_variable_value(h,destId,inst_label,var_label,chIdx,value); }

cw::rc_t cw::flow_cross::begin_cross_fade( handle_t h, unsigned crossFadeMs )
{
  rc_t          rc = kOkRC;
  flow_cross_t* p = _handleToPtr(h);
  
  p->netA[ p->cur_idx ].stateId = kFadeOutStateId;
  
  p->cur_idx                    = _get_flow_index( p, kNextDestId );
  
  p->netA[ p->cur_idx ].stateId = kFadeInStateId;
  p->netA[ p->cur_idx ].fadeSmpN = (unsigned)(p->srate * crossFadeMs / 1000.0);

  return rc;
}



void cw::flow_cross::print( handle_t h )
{
  flow_cross_t* p = _handleToPtr(h);

  printf("flow_cross: sr:%7.1f\n", p->srate );

  printf("master devices:\n");
  for(unsigned i=0; i<p->deviceN; ++i)
    flow::print_external_device( p->deviceA + i );

  for(unsigned i=0; i<p->netN; ++i)
  {
    printf("cross network:%i \n",i);
    flow::print_network( p->netA[i].flowH );
  }
}
