//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwFile.h"
#include "cwTime.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwTime.h"
#include "cwMidiDecls.h"

#include "cwTime.h"
#include "cwMidiDecls.h"

#include "cwDspTypes.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowValue.h"
#include "cwFlowTypes.h"

#include "cwIo.h"

#include "cwIoFlowCtl.h"


namespace cw
{
  namespace io_flow_ctl
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
    
    typedef struct pgm_str
    {
      const char*     label;
      const object_t* cfg;
    } pgm_t;
    
    typedef struct io_flow_ctl_str
    {
      const char*     base_dir;
      object_t*       proc_class_dict_cfg;
      object_t*       udp_dict_cfg;
      
      io::handle_t    ioH;

      flow::external_device_t* deviceA;     // Array of generic device descriptions used by the ioFlow controller
      unsigned                 deviceN;     // (This array must exist for the life of ioFlow controller)
      
      audio_group_t*           audioGroupA; // Array of real time audio device control records.
      unsigned                 audioGroupN; //
      
      pgm_t*          pgmA; // pgmA[ pgmN ]
      unsigned        pgmN;

      unsigned        pgm_idx;   // current program index
      flow::handle_t  flowH;     //
      char*           proj_dir;  // current project directory

      bool            init_fl; 
      bool            done_fl;
    } io_flow_ctl_t;

    io_flow_ctl_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,io_flow_ctl_t>(h); }    

    void _destroy_device_setup( io_flow_ctl_t* p )
    {
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
      p->audioGroupN = 0;
    }
    
    rc_t _program_unload( io_flow_ctl_t* p )
    {
      rc_t rc;
      if((rc = destroy(p->flowH)) != kOkRC )
      {
        rc = cwLogError(rc,"Program unload failed.");
        goto errLabel;
      }

      _destroy_device_setup(p);
      
      mem::release(p->proj_dir);
      p->pgm_idx = kInvalidIdx;
      p->done_fl = true;
      p->init_fl = false;
      
    errLabel:
      return rc;
    }

    
    rc_t _destroy( io_flow_ctl_t* p )
    {
      rc_t rc = kOkRC;

      destroy( p->flowH );

      
      if( p->proc_class_dict_cfg != nullptr )
        p->proc_class_dict_cfg->free();

      if( p->udp_dict_cfg != nullptr )
        p->udp_dict_cfg->free();
      
      _program_unload(p);
      p->pgmN = 0;
      mem::release(p->pgmA);
      mem::release(p);
      return rc;
    }

    rc_t _validate_pgm_idx( io_flow_ctl_t* p, unsigned pgm_idx )
    {
      rc_t rc = kOkRC;
      
      if( pgm_idx == kInvalidIdx || pgm_idx >= p->pgmN )
      {
        rc = cwLogError(kInvalidArgRC,"The program index '%i' is invalid. Program count=%i.",pgm_idx,p->pgmN);
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _parse_cfg( io_flow_ctl_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;
      const char*     proc_cfg_fname   = nullptr;
      const char*     udp_cfg_fname    = nullptr;
      const char*     io_cfg_fname     = nullptr;
      const object_t* pgmL             = nullptr;
      const object_t* tracer_cfg       = nullptr;  // tracer_cfg is parsed just to satify readv() it is not used by cwIoFlowCtl
      
      // parse the cfg parameters
      if((rc = cfg->readv("base_dir",    kReqFl,   p->base_dir,
                          "proc_dict",   kReqFl,   proc_cfg_fname,
                          "udp_dict",    kReqFl,   udp_cfg_fname,
                          "io_dict",     kOptFl,   io_cfg_fname,
                          "tracer",      kOptFl,   tracer_cfg,
                          "programs",    kDictTId, pgmL)) != kOkRC )
      {
        rc = cwLogError(rc,"'caw' system parameter processing failed.");
        goto errLabel;
      }

      // parse the proc dict. file
      if((rc = objectFromFile(proc_cfg_fname,p->proc_class_dict_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"The flow proc dictionary could not be read from '%s'.",cwStringNullGuard(proc_cfg_fname));
        goto errLabel;
      }

      // parse the udp dict file
      if((rc = objectFromFile(udp_cfg_fname,p->udp_dict_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"The flow user-defined-proc dictionary could not be read from '%s'.",cwStringNullGuard(udp_cfg_fname));
        goto errLabel;
      }

      p->pgmN = pgmL->child_count();
      p->pgmA = mem::allocZ<pgm_t>(p->pgmN);
      
      // find the parameters for the requested program
      for(unsigned i=0; i<p->pgmN; i++)      
      {
        const object_t* pgm = pgmL->child_ele(i);

        if( pgm->pair_label()==nullptr || pgm->pair_value()==nullptr || !pgm->pair_value()->is_dict() )
        {
          rc = cwLogError(kSyntaxErrorRC,"The program at index %i has a syntax error.",i);
          goto errLabel;
        }

        p->pgmA[i].label = pgm->pair_label();
        p->pgmA[i].cfg   = pgm->pair_value();

        
      }
      
    errLabel:
      return rc;
    }

    unsigned _calc_device_count(io_flow_ctl_t* p)
    {
      unsigned devN = 0;
      
      devN += socketCount(p->ioH);
      devN += serialDeviceCount(p->ioH);

      unsigned midiDevN = midiDeviceCount(p->ioH);
      for(unsigned i=0; i<midiDevN; ++i)
        devN += midiDevicePortCount(p->ioH,i,true) + midiDevicePortCount(p->ioH,i,false);
      
      for(unsigned i=0; i<p->audioGroupN; ++i)
        devN +=  p->audioGroupA[i].iDeviceN + p->audioGroupA[i].oDeviceN;
      
      return devN;
    }
    
    void _setup_audio_device( io_flow_ctl_t* p,audio_dev_t* dev, unsigned inOrOutFl, unsigned ioDevIdx, unsigned dspFrameCnt )
    {
      dev->ioDevIdx    = ioDevIdx;
      dev->ioDevId     = audioDeviceUserId( p->ioH, ioDevIdx );
      dev->abuf.srate  = audioDeviceSampleRate(   p->ioH, ioDevIdx );
      dev->abuf.chN    = audioDeviceChannelCount( p->ioH, ioDevIdx, inOrOutFl );
      dev->abuf.frameN = dspFrameCnt;
      dev->abuf.buf    = mem::allocZ< flow::sample_t >( dev->abuf.chN * dev->abuf.frameN );

      //printf("%i %s\n", dev->abuf.chN, audioDeviceLabel( p->ioH, ioDevIdx ) );

    }

    rc_t _setup_audio_groups( io_flow_ctl_t* p, double srate, unsigned dspFrameN )
    {
      rc_t rc = kOkRC;
      p->audioGroupN = audioGroupCount( p->ioH );
      p->audioGroupA = mem::allocZ<audio_group_t>( p->audioGroupN );
      
      for(unsigned gi=0; gi<audioGroupCount(p->ioH); ++gi)
      {
        audio_group_t* ag = p->audioGroupA + gi;

        if((rc = audioGroupReconfigure(p->ioH, gi, srate, dspFrameN )) != kOkRC )
        {
          rc = cwLogError(rc,"Audio group reconfiguration to srate=%f dspFrameN:%i failed.",srate,dspFrameN);
          goto errLabel;
        }

        
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

    errLabel:
      return rc;
    }

    rc_t _send_midi_triple( flow::external_device_t* dev, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      return midiDeviceSend(((io_flow_ctl_t*)dev->reserved)->ioH, dev->ioDevIdx, dev->ioPortIdx, status |= ch, d0, d1);
    }

    void _setup_device_cfg( io_flow_ctl_t* p, flow::external_device_t* d, const char* devLabel, unsigned ioDevIdx, unsigned typeId, unsigned flags, const char* midiPortLabel=nullptr, unsigned midiPortIdx=kInvalidIdx )
    {
      d->reserved  = p;
      d->devLabel  = devLabel;
      d->portLabel = midiPortLabel;
      d->typeId    = typeId;
      d->flags     = flags;
      d->ioDevIdx  = ioDevIdx;
      d->ioPortIdx = midiPortIdx;
    }

    void _setup_midi_device_cfg( io_flow_ctl_t* p, flow::external_device_t* d, const char* devLabel, unsigned ioDevIdx, unsigned flags, unsigned ioMidiPortIdx  )
    {
      const char* midiPortLabel = io::midiDevicePortName(p->ioH,ioDevIdx, flags & flow::kInFl ? true : false,ioMidiPortIdx);
      _setup_device_cfg( p, d, devLabel, ioDevIdx, flow::kMidiDevTypeId, flags, midiPortLabel, ioMidiPortIdx );
      d->u.m.maxMsgCnt = io::midiDeviceMaxBufferMsgCount(p->ioH);
      d->u.m.sendTripleFunc = _send_midi_triple; 
    }
    
    void _setup_audio_device_cfg( io_flow_ctl_t* p, flow::external_device_t* d, audio_group_t* ag, audio_dev_t* ad, unsigned flags )
    {
      _setup_device_cfg( p, d, io::audioDeviceLabel(p->ioH,ad->ioDevIdx), ad->ioDevIdx, flow::kAudioDevTypeId, flags );

      // Each audio device is given a flow::abuf to hold incoming or outgoing audio.
      // This buffer also allows the 'audio_in' and 'audio_out' flow procs to configure themselves.
      d->u.a.abuf = &ad->abuf;
    }    

    void _setup_generic_device_array( io_flow_ctl_t* p )
    {
      unsigned i = 0;

      // allocate the generic device control records
      p->deviceN = _calc_device_count(p);
      p->deviceA = mem::allocZ<flow::external_device_t>( p->deviceN );
      

      // get serial devices
      for(unsigned di=0; i<p->deviceN && di<serialDeviceCount(p->ioH); ++di,++i)
        _setup_device_cfg( p, p->deviceA + i, io::serialDeviceLabel(p->ioH,di), di, flow::kSerialDevTypeId, flow::kInFl | flow::kOutFl );

      // get sockets
      for(unsigned di=0; i<p->deviceN && di<socketCount(p->ioH); ++di,++i)
        _setup_device_cfg( p, p->deviceA + i, io::socketLabel(p->ioH,di), di, flow::kSocketDevTypeId, flow::kInFl | flow::kOutFl );
        

      // get midi devices
      for(unsigned di=0; i<p->deviceN && di<midiDeviceCount(p->ioH); ++di)
      {
        // input port setup
        for(unsigned pi=0; pi<midiDevicePortCount(p->ioH,di,true); ++pi,++i)
          _setup_midi_device_cfg( p, p->deviceA + i, io::midiDeviceName(p->ioH,di), di, flow::kInFl, pi);

        // output port setup
        for(unsigned pi=0; pi<midiDevicePortCount(p->ioH,di,false); ++pi,++i)
          _setup_midi_device_cfg( p, p->deviceA + i, io::midiDeviceName(p->ioH,di), di, flow::kOutFl, pi);
        
      }
      
      
      // get the audio devices
      for(unsigned gi=0; gi<p->audioGroupN; ++gi)
      {
        audio_group_t* ag = p->audioGroupA + gi;
        
        for(unsigned di=0; i<p->deviceN && di<ag->iDeviceN; ++di,++i)
          _setup_audio_device_cfg( p, p->deviceA + i, ag, ag->iDeviceA + di, flow::kInFl );
        
        for(unsigned di=0; i<p->deviceN && di<ag->oDeviceN; ++di,++i)
          _setup_audio_device_cfg( p, p->deviceA + i, ag, ag->oDeviceA + di, flow::kOutFl );
      }


      assert( i == p->deviceN );

    }

    rc_t _device_index_to_abuf( io_flow_ctl_t* p, unsigned ioGroupIdx, unsigned ioDevIdx, unsigned inOrOutFl, flow::abuf_t*& abuf_ref )
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

    rc_t _audio_callback( io_flow_ctl_t* p, io::audio_msg_t& m )
    {      
      rc_t rc = kOkRC;
      flow::abuf_t* abuf = nullptr;

      // Get an array of incoming MIDI events which have occurred since the last call to 'io::midiDeviceBuffer()'
      unsigned midiBufMsgCnt        = 0;
      const midi::ch_msg_t* midiBuf = midiDeviceBuffer(p->ioH,midiBufMsgCnt);
      
      if( p->done_fl )
      {
        rc = cwLogError(kInvalidStateRC,"Cannot execute an already completed program.");
        goto errLabel;
      }


      // Give each MIDI input device a pointer to the incoming MIDI msgs
      for(unsigned i=0; i<p->deviceN; ++i)
        if( p->deviceA[i].typeId == flow::kMidiDevTypeId && cwIsFlag(p->deviceA[i].flags,flow::kInFl) )
        {
          p->deviceA[i].u.m.msgArray = midiBuf;
          p->deviceA[i].u.m.msgCnt   = midiBufMsgCnt;
        }

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
      if((rc = flow::exec_cycle(p->flowH)) != kOkRC )
      {
        if( rc == kEofRC )
        {
          p->done_fl = true;
          p->init_fl = false;
          rc = kOkRC;
        }
      }
      

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
      // Drop the MIDI messages that were processed on this call.
      midiDeviceClearBuffer(p->ioH,midiBufMsgCnt);
        
      return rc;
    }

    unsigned _ui_var_to_widget_uuid( flow::ui_var_t* ui_var )
    {
      return ui_var != nullptr && ui_var->user_arg != nullptr ? ((const io_var_arg_t*)ui_var->user_arg)->widget_uuid : kInvalidId;    
    }

    unsigned _ui_var_to_container_uuid( flow::ui_var_t* ui_var )
    {
      return ui_var != nullptr && ui_var->user_arg != nullptr ? ((const io_var_arg_t*)ui_var->user_arg)->container_uuid : kInvalidId;    
    }
    
    unsigned _ui_var_to_label_uuid( flow::ui_var_t* ui_var )
    {
      return ui_var != nullptr && ui_var->user_arg != nullptr ? ((const io_var_arg_t*)ui_var->user_arg)->label_uuid : kInvalidId;    
    }

    template< typename T >
    rc_t _ui_callback_tpl( io_flow_ctl_t* p, flow::ui_var_t* ui_var )
    {
      rc_t rc;
      
      T value;
      if((rc = get_variable_value(p->flowH,ui_var,value)) != kOkRC )
      {
        rc = cwLogError(rc,"The variable value could not be read.");
        goto errLabel;
      }

      if((rc = uiSendValue(p->ioH,_ui_var_to_widget_uuid(ui_var),value)) != kOkRC )
      {
        rc = cwLogError(rc,"UI element data transmission failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    // This function is called with messages for the UI from the flow proc instances 
    rc_t _ui_callback( void* arg, flow::ui_var_t* ui_var )
    {
      rc_t rc = kOkRC;
      
      io_flow_ctl_t* p = (io_flow_ctl_t*)arg;

      if( ui_var->user_arg == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The user_id (uuid) of the variable was not set.");
        goto errLabel;
      }

      if( ui_var->new_disable_fl != ui_var->disable_fl )
      {

        uiSetEnable( p->ioH, _ui_var_to_label_uuid(ui_var), !ui_var->new_disable_fl );
        
        if((rc = uiSetEnable( p->ioH, _ui_var_to_widget_uuid(ui_var), !ui_var->new_disable_fl )) != kOkRC )
        {
          rc = cwLogError(rc,"UI enable/disable update failed.");
          goto errLabel;
        }

        ui_var->disable_fl = ui_var->new_disable_fl;
      }

      if( ui_var->new_hide_fl != ui_var->hide_fl )
      {
        if((rc = uiSetVisible( p->ioH, _ui_var_to_container_uuid(ui_var), !ui_var->new_hide_fl )) != kOkRC )
        {
          rc = cwLogError(rc,"UI hide/show update failed.");
          goto errLabel;
        }

        ui_var->hide_fl = ui_var->new_hide_fl;
      }
      
      switch( ui_var->value_tid & flow::kTypeMask )
      {
        case flow::kBoolTFl:
          rc = _ui_callback_tpl<bool>(p,ui_var);
          break;

        case flow::kIntTFl:
          rc = _ui_callback_tpl<int>(p,ui_var);
          break;

        case flow::kUIntTFl:
          rc = _ui_callback_tpl<unsigned>(p,ui_var);
          break;

        case flow::kFloatTFl:
          rc = _ui_callback_tpl<float>(p,ui_var);
          break;

        case flow::kDoubleTFl:
          rc = _ui_callback_tpl<double>(p,ui_var);
          break;

        case flow::kStringTFl:
          rc = _ui_callback_tpl<const char*>(p,ui_var);
          break;
          
      }

    errLabel:
      if( rc != kOkRC )  
        rc = cwLogError(rc,"Update of UI element  of '%s:%i-%s:%i' failed.",cwStringNullGuard(ui_var->ui_proc->label),ui_var->ui_proc->label_sfx_id,cwStringNullGuard(ui_var->label),ui_var->label_sfx_id);
      
      return rc;
      
    }
    
  }
}


cw::rc_t cw::io_flow_ctl::create(  handle_t& hRef, io::handle_t ioH, const object_t* flow_cfg )
{
  rc_t rc = kOkRC;

  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  io_flow_ctl_t* p = mem::allocZ<io_flow_ctl_t>();
  p->pgm_idx = kInvalidIdx;
  p->ioH     = ioH;

  if((rc = _parse_cfg(p,flow_cfg)) != kOkRC )
    goto errLabel;
  
  hRef.set(p);
  
errLabel:
  if( rc != kOkRC )
  {
    rc = cwLogError(rc,"io_flow create failed.");
    _destroy(p);    
  }
  return rc;  
}

cw::rc_t cw::io_flow_ctl::destroy( handle_t& hRef )
{
  rc_t           rc = kOkRC;
  io_flow_ctl_t* p  = nullptr;
  
  if(!hRef.isValid())
    return rc;

  p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    goto errLabel;
  

  hRef.clear();
  
errLabel:
  return rc;
}

unsigned    cw::io_flow_ctl::program_count(handle_t h)
{
  io_flow_ctl_t* p = _handleToPtr(h);
  return p->pgmN;  
}

const char* cw::io_flow_ctl::program_title( handle_t h, unsigned pgm_idx )
{
  
  io_flow_ctl_t* p = _handleToPtr(h);
  const char* pgm_title = nullptr;

  if(_validate_pgm_idx(p,pgm_idx) != kOkRC )
    goto errLabel;

  pgm_title = p->pgmA[pgm_idx].label;
  
errLabel:
  return pgm_title;
}


unsigned cw::io_flow_ctl::program_index( handle_t h, const char* pgm_title )
{
  io_flow_ctl_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->pgmN; ++i)
    if( textIsEqual(pgm_title,p->pgmA[i].label) )
      return i;

  return kInvalidIdx;
}

cw::rc_t    cw::io_flow_ctl::program_load(  handle_t h, unsigned pgm_idx )
{
  rc_t           rc = kOkRC;
  io_flow_ctl_t* p  = _handleToPtr(h);
  
  if((rc = _validate_pgm_idx(p,pgm_idx)) != kOkRC )
    goto errLabel;

  if((rc = _program_unload(p)) != kOkRC )
    goto errLabel;

  // form the program project directory
  if((p->proj_dir = filesys::makeFn(p->base_dir,nullptr,nullptr,p->pgmA[pgm_idx].label,nullptr)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"The project directory formation failed.");
    goto errLabel;
  }

  // create the project directory if it doesn't already exist
  if( !filesys::isDir(p->proj_dir) )
    if((rc = filesys::makeDir(p->proj_dir)) != kOkRC )
      goto errLabel;

  // configure the flow network
  if((rc = create( p->flowH,
                   p->proc_class_dict_cfg,
                   p->pgmA[ pgm_idx ].cfg,
                   p->udp_dict_cfg,
                   p->proj_dir,
                   _ui_callback,
                   p)) != kOkRC )
  {
    rc = cwLogError(rc,"Network configuration failed.");
    goto errLabel;
  }
                               
  // allocate p->audioGroupA[] and create the audio input/output buffers associated with each audio device
  _setup_audio_groups(p, sample_rate(p->flowH), frames_per_cycle(p->flowH) );

  // setup the control record for each external device known to the IO interface
  _setup_generic_device_array(p);
  
  p->pgm_idx = pgm_idx;
  p->done_fl = false;
  p->init_fl = false;

errLabel:
  return rc;
}

unsigned    cw::io_flow_ctl::program_current_index( handle_t h )
{
  io_flow_ctl_t* p = _handleToPtr(h);
  return p->pgm_idx;
}

bool        cw::io_flow_ctl::is_program_nrt( handle_t h )
{
  io_flow_ctl_t* p      = _handleToPtr(h);
  bool           nrt_fl = false;

  if( !p->flowH.isValid() )
  {
    cwLogWarning("No program is loaded.");
    goto errLabel;
  }
  
  if(_validate_pgm_idx(p,p->pgm_idx) != kOkRC )
    goto errLabel;

  nrt_fl = is_non_real_time(p->flowH);
  
errLabel:
  return nrt_fl;
}

unsigned    cw::io_flow_ctl::program_preset_count( handle_t h )
{
  io_flow_ctl_t* p      = _handleToPtr(h);
  
  if(!p->flowH.isValid() || _validate_pgm_idx(p,p->pgm_idx) != kOkRC )
    return 0;
  
  return preset_count(p->flowH);
}

const char* cw::io_flow_ctl::program_preset_title( handle_t h, unsigned preset_idx )
{
  io_flow_ctl_t* p      = _handleToPtr(h);
  
  if(!p->flowH.isValid() || _validate_pgm_idx(p,p->pgm_idx) != kOkRC )
    return nullptr;
  return preset_label(p->flowH,preset_idx);
}

cw::rc_t cw::io_flow_ctl::program_initialize( handle_t h, unsigned preset_idx )
{
  rc_t rc = kOkRC;
  io_flow_ctl_t* p = _handleToPtr(h);

  if( p->pgm_idx == kInvalidIdx || p->init_fl || p->done_fl || !p->flowH.isValid()  )
  {
    cwLogError(kInvalidStateRC,"A valid pre-initialized program is not loaded.");
    goto errLabel;
  }
  
  // create the flow network
  if((rc = initialize( p->flowH,
                       p->deviceA,
                       p->deviceN,
                       preset_idx )) != kOkRC )
  {
    rc = cwLogError(rc,"Network create failed.");
    goto errLabel;
  }

  p->init_fl = true;
errLabel:
  return rc;
}

bool cw::io_flow_ctl::program_is_initialized( handle_t h )
{
  io_flow_ctl_t* p  = _handleToPtr(h);
  return p->init_fl;
}

cw::rc_t cw::io_flow_ctl::program_apply_preset( handle_t h, unsigned preset_idx )
{
  rc_t rc = kOkRC;
  const char* preset_label = nullptr;
  io_flow_ctl_t* p  = _handleToPtr(h);
  if( !p->init_fl )
  {
    rc = cwLogError(kInvalidStateRC,"The preset cannot be applied because the program is not initialized.");
    goto errLabel;
  }

  if((preset_label = flow::preset_label(p->flowH,preset_idx)) == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The preset index '%i' is invalid.",preset_idx);
    goto errLabel;
  }

  if((rc = apply_preset(p->flowH,preset_label)) != kOkRC )
  {
    rc = cwLogError(rc,"Application of the preset '%s' failed.",preset_label);
    goto errLabel;
    
  }
  
errLabel:
  return rc;
  
}

const cw::flow::ui_net_t* cw::io_flow_ctl::program_ui_net( handle_t h )
{
  io_flow_ctl_t* p  = _handleToPtr(h);  
  return ui_net(p->flowH);
}


cw::rc_t    cw::io_flow_ctl::exec_nrt( handle_t h )
{
  rc_t           rc = kOkRC;
  io_flow_ctl_t* p  = _handleToPtr(h);

  if( p->pgm_idx == kInvalidIdx )
  {
    rc = cwLogWarning("No program is loaded.");
    goto errLabel;
  }

  if( p->done_fl )
  {
    rc = cwLogError(kInvalidStateRC,"Cannot execute an already completed program.");
    goto errLabel;
  }
  
  if((rc = exec( p->flowH )) != kOkRC )
  {
    if(rc == kEofRC )
    {
      p->done_fl = true;
      rc = kOkRC;
    }
    else
    {
      rc = cwLogError(rc,"%s execution failed.", cwStringNullGuard(p->pgmA[p->pgm_idx].label));
      goto errLabel;
    }
  }
  
errLabel:
  return rc;
}

cw::rc_t cw::io_flow_ctl::exec( handle_t h, const io::msg_t& msg )
{
  rc_t           rc = kOkRC;
  io_flow_ctl_t* p  = _handleToPtr(h);
  
  switch( msg.tid )
  {
  case io::kAudioTId:
    if( msg.u.audio != nullptr )
      rc = _audio_callback(p,*msg.u.audio);
    break;

  default:
    rc = kOkRC;
  
  }

  return rc;
}

bool cw::io_flow_ctl::is_executable( handle_t h )
{
  io_flow_ctl_t* p  = _handleToPtr(h);

  // A program must be loaded, initialized and execution cannot be complete
  return p->pgm_idx != kInvalidIdx && p->init_fl && p->done_fl==false;
}

bool cw::io_flow_ctl::is_exec_complete( handle_t h )
{
  io_flow_ctl_t* p  = _handleToPtr(h);

  return p->done_fl;
}

cw::rc_t cw::io_flow_ctl::send_ui_updates( handle_t h )
{
  rc_t rc = kOkRC;
  io_flow_ctl_t* p  = _handleToPtr(h);
  if( program_is_initialized(h) )
    rc = send_ui_updates(p->flowH);

  return rc;
}



cw::rc_t cw::io_flow_ctl::get_variable_value(   handle_t h, const flow::ui_var_t* ui_var, bool& value_ref )
{ return get_variable_value( _handleToPtr(h)->flowH, ui_var, value_ref ); }
cw::rc_t cw::io_flow_ctl::get_variable_value(    handle_t h, const flow::ui_var_t* ui_var, int& value_ref )
{ return get_variable_value( _handleToPtr(h)->flowH, ui_var, value_ref ); }
cw::rc_t cw::io_flow_ctl::get_variable_value(   handle_t h, const flow::ui_var_t* ui_var, unsigned& value_ref )
{ return get_variable_value( _handleToPtr(h)->flowH, ui_var, value_ref ); }
cw::rc_t cw::io_flow_ctl::get_variable_value(  handle_t h, const flow::ui_var_t* ui_var, float& value_ref )
{ return get_variable_value( _handleToPtr(h)->flowH, ui_var, value_ref ); }
cw::rc_t cw::io_flow_ctl::get_variable_value( handle_t h, const flow::ui_var_t* ui_var, double& value_ref )
{ return get_variable_value( _handleToPtr(h)->flowH, ui_var, value_ref ); }
cw::rc_t cw::io_flow_ctl::get_variable_value( handle_t h, const flow::ui_var_t* ui_var, const char*& value_ref )
{ return get_variable_value( _handleToPtr(h)->flowH, ui_var, value_ref ); }

cw::rc_t cw::io_flow_ctl::set_variable_user_arg( handle_t h, const flow::ui_var_t* ui_var, io_var_arg_t* arg )
{ return set_variable_user_arg( _handleToPtr(h)->flowH, ui_var, arg ); }

cw::rc_t cw::io_flow_ctl::set_variable_value( handle_t h, const flow::ui_var_t* ui_var, bool value )
{ return set_variable_value( _handleToPtr(h)->flowH, ui_var, value ); }
cw::rc_t cw::io_flow_ctl::set_variable_value( handle_t h, const flow::ui_var_t* ui_var, int value )
{ return set_variable_value( _handleToPtr(h)->flowH, ui_var, value ); }
cw::rc_t cw::io_flow_ctl::set_variable_value( handle_t h, const flow::ui_var_t* ui_var, unsigned value )
{ return set_variable_value( _handleToPtr(h)->flowH, ui_var, value ); }
cw::rc_t cw::io_flow_ctl::set_variable_value( handle_t h, const flow::ui_var_t* ui_var, float value )
{ return set_variable_value( _handleToPtr(h)->flowH, ui_var, value ); }
cw::rc_t cw::io_flow_ctl::set_variable_value( handle_t h, const flow::ui_var_t* ui_var, double value )
{ return set_variable_value( _handleToPtr(h)->flowH, ui_var, value ); }
cw::rc_t cw::io_flow_ctl::set_variable_value( handle_t h, const flow::ui_var_t* ui_var, const char* value )
{ return set_variable_value( _handleToPtr(h)->flowH, ui_var, value ); }


void cw::io_flow_ctl::report( handle_t h )
{  
  io_flow_ctl_t* p = _handleToPtr(h);
  if( p->flowH.isValid() )
    print_network(p->flowH);
}

void cw::io_flow_ctl::print_network( handle_t h )
{
  io_flow_ctl_t* p = _handleToPtr(h);
  if( p->flowH.isValid() )
    print_network(p->flowH);  
}




