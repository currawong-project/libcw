#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwFile.h"
#include "cwTime.h"
#include "cwVectOps.h"
#include "cwMtx.h"

#include "cwDspTypes.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
#include "cwFlowCross.h"

#include "cwIo.h"

#include "cwIoFlow.h"



namespace cw
{
  namespace io_flow
  {
    // An audio_dev_t record exists for each possible input or output device.
    typedef struct audio_dev_str
    {
      unsigned       ioDevIdx;  // device index in the io:: API
      unsigned       ioDevId;   // device id in the io:: API
      flow::abuf_t   abuf;      // src/dst buffer for incoming/outgoing (record/play) samples used by flow proc 'audio_in' and 'audio_out'.
    } audio_dev_t;

    typedef struct audio_group_str
    {
      double       srate;
      unsigned     dspFrameCnt;
      unsigned     ioGroupIdx;
      
      audio_dev_t* iDeviceA;
      unsigned     iDeviceN;
      
      audio_dev_t* oDeviceA;
      unsigned     oDeviceN;
      
    } audio_group_t;
    
    typedef struct io_flow_str
    {
      io::handle_t             ioH;         //
      
      flow::external_device_t* deviceA;     // Array of generic device descriptions used by the ioFlow controller
      unsigned                 deviceN;     // (This array must exist for the life of ioFlow controller)
      
      audio_group_t*           audioGroupA; // Array of real time audio device control records.
      unsigned                 audioGroupN; //
      
      flow_cross::handle_t     crossFlowH;  //
      
    } io_flow_t;

    io_flow_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,io_flow_t>(h); }

    rc_t _destroy( io_flow_t* p )
    {

      flow_cross::destroy( p->crossFlowH );
      
      mem::release(p->deviceA);
      p->deviceN = 0;

      for(unsigned gi=0; gi<p->audioGroupN; ++gi)
      {
        audio_group_t* ag = p->audioGroupA + gi;
        for(unsigned di=0; di<ag->iDeviceN; ++di)
          mem::release( ag->iDeviceA[di].abuf.buf );

        for(unsigned di=0; di<ag->oDeviceN; ++di)
          mem::release( ag->oDeviceA[di].abuf.buf );

        mem::release( ag->iDeviceA);
        mem::release( ag->oDeviceA);
      }

      mem::release(p->audioGroupA);
      mem::release(p);
      
      return kOkRC;
    }

    unsigned _calc_device_count(io_flow_t* p)
    {
      unsigned devN = 0;
      
      //devN += midiDeviceCount(p->ioH);
      devN += socketCount(p->ioH);
      devN += serialDeviceCount(p->ioH);

      for(unsigned i=0; i<p->audioGroupN; ++i)
        devN +=  p->audioGroupA[i].iDeviceN + p->audioGroupA[i].oDeviceN;
      
      return devN;
    }

    void _setup_audio_device( io_flow_t* p,audio_dev_t* dev, unsigned inOrOutFl, unsigned ioDevIdx, unsigned dspFrameCnt )
    {
      dev->ioDevIdx    = ioDevIdx;
      dev->ioDevId     = audioDeviceUserId( p->ioH, ioDevIdx );
      dev->abuf.base   = nullptr;
      dev->abuf.srate  = audioDeviceSampleRate(   p->ioH, ioDevIdx );
      dev->abuf.chN    = audioDeviceChannelCount( p->ioH, ioDevIdx, inOrOutFl );
      dev->abuf.frameN = dspFrameCnt;
      dev->abuf.buf    = mem::allocZ< flow::sample_t >( dev->abuf.chN * dev->abuf.frameN );

      //printf("%i %s\n", dev->abuf.chN, audioDeviceLabel( p->ioH, ioDevIdx ) );

    }

    void _setup_audio_groups( io_flow_t* p )
    {
      p->audioGroupN = audioGroupCount( p->ioH );
      p->audioGroupA = mem::allocZ<audio_group_t>( p->audioGroupN );
      
      for(unsigned gi=0; gi<audioGroupCount(p->ioH); ++gi)
      {
        audio_group_t* ag = p->audioGroupA + gi;
        ag->srate       = audioGroupSampleRate(    p->ioH, gi );
        ag->dspFrameCnt = audioGroupDspFrameCount( p->ioH, gi );
        ag->ioGroupIdx  = gi;

        ag->iDeviceN = audioGroupDeviceCount( p->ioH, gi, io::kInFl );
        ag->iDeviceA = mem::allocZ< audio_dev_t >( ag->iDeviceN );

        for(unsigned gdi=0; gdi<ag->iDeviceN; ++gdi)
          _setup_audio_device( p, ag->iDeviceA + gdi, io::kInFl, audioGroupDeviceIndex( p->ioH, gi, io::kInFl, gdi), ag->dspFrameCnt  );
      
        ag->oDeviceN = audioGroupDeviceCount( p->ioH, gi, io::kOutFl );
        ag->oDeviceA = mem::allocZ< audio_dev_t >( ag->oDeviceN );

        for(unsigned gdi=0; gdi<ag->oDeviceN; ++gdi)
          _setup_audio_device( p, ag->oDeviceA + gdi, io::kOutFl, audioGroupDeviceIndex( p->ioH, gi, io::kOutFl, gdi), ag->dspFrameCnt  );
        
      }
    }


    void _setup_device_cfg( flow::external_device_t* d, const char* devLabel, unsigned ioDevId, unsigned typeId, unsigned flags )
    {
      d->label   = devLabel;
      d->ioDevId = ioDevId;
      d->typeId  = typeId;
      d->flags   = flags;
    }

    void _setup_audio_device_cfg( io_flow_t* p, flow::external_device_t* d, audio_group_t* ag, audio_dev_t* ad, unsigned flags )
    {
      _setup_device_cfg( d, io::audioDeviceLabel(p->ioH,ad->ioDevIdx), ad->ioDevId, flow::kAudioDevTypeId, flags );

      // Each audio device is given a flow::abuf to hold incoming or outgoing audio.
      // This buffer also allows the 'audio_in' and 'audio_out' flow procs to configure themselves.
      d->u.a.abuf = &ad->abuf;
    }

    void _setup_generic_device_array( io_flow_t* p )
    {
      unsigned i = 0;

      // allocate the generic device control records
      p->deviceN = _calc_device_count(p);
      p->deviceA = mem::allocZ<flow::external_device_t>( p->deviceN );
      

      // get serial devices
      for(unsigned di=0; i<p->deviceN && di<serialDeviceCount(p->ioH); ++di,++i)
        _setup_device_cfg( p->deviceA + i, io::serialDeviceLabel(p->ioH,di), io::serialDeviceId(p->ioH,di), flow::kSerialDevTypeId, flow::kInFl | flow::kOutFl );

      // get midi devices
      //for(unsigned di=0; i<p->deviceN && di<midiDeviceCount(p->ioH); ++di,++i)
      //  _setup_device_cfg( p->deviceA + i, io::midiDeviceLabel(p->ioH,di), di, flow::kMidiDevTypeId, flow::kInFl | flow::kOutFl );

      // get sockets
      for(unsigned di=0; i<p->deviceN && di<socketCount(p->ioH); ++di,++i)
        _setup_device_cfg( p->deviceA + i, io::socketLabel(p->ioH,di), io::socketUserId(p->ioH,di), flow::kSocketDevTypeId, flow::kInFl | flow::kOutFl );
        

      // get the audio devices
      for(unsigned gi=0; gi<p->audioGroupN; ++gi)
      {
        audio_group_t* ag = p->audioGroupA + gi;
        
        for(unsigned di=0; i<p->deviceN && di<ag->iDeviceN; ++di,++i)
          _setup_audio_device_cfg( p, p->deviceA + i, ag, ag->iDeviceA + di, flow::kInFl );
        
        for(unsigned di=0; i<p->deviceN && di<ag->oDeviceN; ++di,++i)
          _setup_audio_device_cfg( p, p->deviceA + i, ag, ag->oDeviceA + di, flow::kOutFl );
      }

    }

    rc_t _device_index_to_abuf( io_flow_t* p, unsigned ioGroupIdx, unsigned ioDevIdx, unsigned inOrOutFl, flow::abuf_t*& abuf_ref )
    {

      rc_t rc = kOkRC;

      for(unsigned gi=0; gi<p->audioGroupN; ++gi)
        if( p->audioGroupA[gi].ioGroupIdx == ioGroupIdx )
        {
          audio_dev_t* adA = inOrOutFl == flow::kInFl ? p->audioGroupA[gi].iDeviceA : p->audioGroupA[gi].oDeviceA;
          unsigned     adN = inOrOutFl == flow::kInFl ? p->audioGroupA[gi].iDeviceN : p->audioGroupA[gi].oDeviceN;

          for(unsigned di=0; di<adN; ++di)
            if( adA[di].ioDevIdx == ioDevIdx )
            {
              abuf_ref = &adA[di].abuf;
              return rc;
            }
        
        }

      const char* dir = inOrOutFl==flow::kInFl ? "in" : "out";      
      return cwLogError(kOpFailRC,"The '%s' audio group index:%i ,device index '%i' was not found.", dir, ioGroupIdx, ioDevIdx);
    }

    void _fill_input_buffer( flow::sample_t** bufChArray, unsigned bufChArrayN, flow::abuf_t* dst_abuf )
    {
      for(unsigned i=0; i<bufChArrayN; ++i)
      {
        const flow::sample_t* src = bufChArray[i];
        flow::sample_t*       dst = dst_abuf->buf + (i*dst_abuf->frameN);
        memcpy(dst,src,dst_abuf->frameN*sizeof(flow::sample_t));
      }
    }

    void _zero_output_buffer( flow::abuf_t* dst_abuf )
    {
      memset(dst_abuf->buf,0, dst_abuf->chN*dst_abuf->frameN*sizeof(flow::sample_t));
    }

    void _fill_output_buffer( const flow::abuf_t* src_abuf, flow::sample_t** bufChArray, unsigned bufChArrayN )
    {
      for(unsigned i=0; i<src_abuf->chN; ++i)
      {
        const flow::sample_t* src = src_abuf->buf + (i*src_abuf->frameN);
        flow::sample_t*       dst = bufChArray[i];
        memcpy(dst,src,src_abuf->frameN*sizeof(flow::sample_t));
      }
    }

    rc_t _audio_callback( io_flow_t* p, io::audio_msg_t& m )
    {      
      rc_t rc = kOkRC;
      flow::abuf_t* abuf = nullptr;

      // if there is incoming (recorded) audio 
      if( m.iBufChCnt > 0 )
      {
        unsigned chIdx = 0;

        // for each input device in this group
        for(io::audio_group_dev_t* agd = m.iDevL; agd!=nullptr; agd=agd->link)
        {
          // get the abuf associated with each device in this group
          if((rc = _device_index_to_abuf( p, m.groupIndex, agd->devIdx, flow::kInFl, abuf )) != kOkRC )            
            goto errLabel;

          // fill the input audio buf from the the external audio device
          _fill_input_buffer( m.iBufArray + chIdx, agd->chCnt, abuf );

          chIdx += agd->chCnt;
        }

      }

      // if there are empty output (playback) buffers
      if( m.oBufChCnt > 0 )
      {

        // for each output device in this group
        for(io::audio_group_dev_t* agd=m.oDevL; agd!=nullptr; agd=agd->link)
        {
          // get the output audio buf associated with this external audio device
          if((rc = _device_index_to_abuf( p, m.groupIndex, agd->devIdx, flow::kOutFl, abuf )) != kOkRC )
            goto errLabel;

          // zerot the output buffer
          _zero_output_buffer( abuf );
        }
      }


      // update the flow network - this will generate audio into the output audio buffers
      flow_cross::exec_cycle(p->crossFlowH);
      

      // if there are empty output (playback) buffers
      if( m.oBufChCnt > 0 )
      {

        unsigned chIdx = 0;
        
        // for each output device in this group
        for(io::audio_group_dev_t* agd=m.oDevL; agd!=nullptr; agd=agd->link)
        {
          // get the output audio buf associated with this external audio device
          if((rc = _device_index_to_abuf( p, m.groupIndex, agd->devIdx, flow::kOutFl, abuf )) != kOkRC )
            goto errLabel;

          // copy the samples from the flow 'audio_out' buffers to the outgoing buffer passed from the device driver
          _fill_output_buffer( abuf, m.oBufArray + chIdx, agd->chCnt );
          
          chIdx += agd->chCnt;
            
        }
        
      }
        
    errLabel:
      return rc;
    }

    
  }
}


cw::rc_t cw::io_flow::create( handle_t&       hRef,
                              io::handle_t    ioH,
                              double          srate,
                              unsigned        crossFadeCnt,
                              const object_t& flow_class_dict,
                              const object_t& network_cfg )
{
  rc_t rc;

  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  io_flow_t* p = mem::allocZ<io_flow_t>();

  p->ioH     = ioH;

  // allocate p->audioGroupA[] and create the audio input/output buffers associated with each audio device
  _setup_audio_groups(p);

  // setup the control record for each external device known to the IO interface
  _setup_generic_device_array(p);

  // create the flow object
  if((rc = flow_cross::create( p->crossFlowH, flow_class_dict, network_cfg, srate, crossFadeCnt, p->deviceA, p->deviceN )) != kOkRC )
  {
    cwLogError(rc,"The 'flow' object create failed.");
    goto errLabel;
  }

  //flow_cross::print(p->crossFlowH);
  

  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
  {
    rc = cwLogError(rc,"io_flow create failed.");
    _destroy(p);
  }
  
  
  return rc;
}

cw::rc_t cw::io_flow::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;;
  
  if( !hRef.isValid() )
    return rc;

  io_flow_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}

unsigned cw::io_flow::preset_cfg_flags( handle_t h )
{
  io_flow_t* p  = _handleToPtr(h);
  return preset_cfg_flags(p->crossFlowH);
}


cw::rc_t cw::io_flow::exec( handle_t h, const io::msg_t& msg )
{
  rc_t       rc = kOkRC;
  io_flow_t* p  = _handleToPtr(h);
  
  switch( msg.tid )
  {
  case io::kAudioTId:
    if( msg.u.audio != nullptr )
      _audio_callback(p,*msg.u.audio);
    break;

  default:
    rc = kOkRC;
  
  }

  return rc;
}

cw::rc_t cw::io_flow::apply_preset( handle_t h, unsigned crossFadeMs, const char* presetLabel )
{
  rc_t rc;
  if((rc = apply_preset( h, flow_cross::kNextDestId, presetLabel )) == kOkRC )
    rc = begin_cross_fade( h, crossFadeMs );
  
  return rc;
}


cw::rc_t cw::io_flow::apply_preset( handle_t h, flow_cross::destId_t destId, const char* presetLabel )
{ return apply_preset( _handleToPtr(h)->crossFlowH, destId, presetLabel ); }

cw::rc_t cw::io_flow::apply_preset( handle_t h, flow_cross::destId_t destId, const flow::multi_preset_selector_t& multi_preset_sel )
{ return apply_preset( _handleToPtr(h)->crossFlowH, destId, multi_preset_sel ); }

cw::rc_t cw::io_flow::set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, bool value )
{ return flow_cross::set_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::io_flow::set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, int value )
{ return flow_cross::set_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::io_flow::set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value )
{ return flow_cross::set_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::io_flow::set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, float value )
{ return flow_cross::set_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::io_flow::set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, double value )
{ return flow_cross::set_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::io_flow::begin_cross_fade( handle_t h, unsigned crossFadeMs )
{ return flow_cross::begin_cross_fade( _handleToPtr(h)->crossFlowH, crossFadeMs ); }


cw::rc_t cw::io_flow::get_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, bool& valueRef )
{ return flow_cross::get_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::io_flow::get_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, int& valueRef )
{ return flow_cross::get_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::io_flow::get_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, unsigned& valueRef )
{ return flow_cross::get_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::io_flow::get_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, float& valueRef )
{ return flow_cross::get_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::io_flow::get_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, double& valueRef )
{ return flow_cross::get_variable_value( _handleToPtr(h)->crossFlowH, destId, inst_label, var_label, chIdx, valueRef ); }


void cw::io_flow::print( handle_t h )
{ return flow_cross::print( _handleToPtr(h)->crossFlowH ); }

void cw::io_flow::print_network( handle_t h, flow_cross::destId_t destId )
{  return flow_cross::print_network(  _handleToPtr(h)->crossFlowH, destId );  }

void cw::io_flow::report( handle_t h )
{ flow_cross::report( _handleToPtr(h)->crossFlowH ); }
