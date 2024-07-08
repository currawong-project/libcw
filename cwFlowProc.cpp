#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"

#include "cwDspTypes.h" // srate_t, sample_t, coeff_t, ...

#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwMidiFile.h"

#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
#include "cwFlowNet.h"
#include "cwFlowProc.h"

#include "cwFile.h"
#include "cwMath.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"
#include "cwDspTransforms.h"
#include "cwMidiDecls.h"

#include "cwWaveTableBank.h"


namespace cw
{

  namespace flow
  {
    
    template< typename inst_t >
    rc_t std_destroy( proc_t* proc )
    {
      inst_t* p = (inst_t*)proc->userPtr;
      rc_t rc = _destroy(proc,p);
      mem::release(proc->userPtr);
      return rc;
    }
    
    template< typename inst_t >
    rc_t std_create( proc_t* proc )
    {
      rc_t rc = kOkRC;
      proc->userPtr = mem::allocZ<inst_t>();
      if((rc = _create(proc,(inst_t*)proc->userPtr)) != kOkRC )
        std_destroy<inst_t>(proc);
      return rc;        
    }

    template< typename inst_t >
    rc_t std_value( proc_t* proc, variable_t* var )
    { return _value(proc,(inst_t*)proc->userPtr, var); }
        
    template< typename inst_t >
    rc_t std_exec( proc_t* proc )
    { return _exec(proc,(inst_t*)proc->userPtr); }

    template< typename inst_t >
    rc_t std_report( proc_t* proc )
    { return _report(proc,(inst_t*)proc->userPtr); }

    
    //------------------------------------------------------------------------------------------------------------------
    //
    // Template
    //
    namespace template_proc
    {
      typedef struct
      {
        
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;        

        // Custom create code goes here

        // Notes:
        // 1. var_register_*() will automatically create any variables that don't already exist.
        //    therefore var_create() should never have to be called directly.
        // 2. The variable 'vid' value must be unique across all ('label','sfx-id') pairs but not across channels.

        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        // Custom clean-up code goes here

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // subnet
    //
    namespace subnet
    {
      typedef struct
      {
        network_t net;
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t            rc         = kOkRC;        
        const object_t* networkCfg = nullptr;
        
        if((rc = proc->class_desc->cfg->getv("network",networkCfg)) != kOkRC )
        {
          rc = cwLogError(rc,"The subnet 'network' cfg. was not found.");
          goto errLabel;
        }

        if((rc = network_create(proc->ctx,networkCfg,p->net,proc->varL)) != kOkRC )
        {
          rc = cwLogError(rc,"Creation failed on the subnet internal network.");
          goto errLabel;
        }

        // Set the internal net pointer in the base proc instance
        // so that network based utilities can scan it
        proc->internal_net = &p->net;

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        network_destroy(p->net);

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;

        if((rc = exec_cycle(p->net)) != kOkRC )
          rc = cwLogError(rc,"poly internal network exec failed.");
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // poly
    //
    namespace poly
    {
      enum
      {
        kCountPId,
        kOrderPId,
      };
      
      typedef struct
      {
        unsigned   count;
        network_t  net;
        network_order_id_t orderId;
      } inst_t;


      rc_t create( proc_t* proc )
      {
        rc_t            rc          = kOkRC;        
        inst_t*         inst        = mem::allocZ<inst_t>();
        const object_t* networkCfg  = nullptr;
        const char*     order_label = nullptr;
        variable_t*     proxyVarL   = nullptr;
        
        proc->userPtr = inst;
        
        if((rc  = var_register_and_get( proc, kAnyChIdx,
                                        kCountPId, "count", kBaseSfxId, inst->count,
                                        kOrderPId, "order", kBaseSfxId, order_label )) != kOkRC )
          goto errLabel;

        if( inst->count == 0 )
        {
          cwLogWarning("The 'poly' %s:%i was given a count of 0.",proc->label,proc->label_sfx_id);
          goto errLabel;
        }

        if((rc = proc->proc_cfg->getv("network",networkCfg)) != kOkRC )
        {
          rc = cwLogError(rc,"The 'network' cfg. was not found.");
          goto errLabel;
        }

        // get the network exec. order type
        if( textIsEqual(order_label,"net") )
          inst->orderId = kNetFirstPolyOrderId;
        else
        {
          if( textIsEqual(order_label,"proc") )
            inst->orderId = kProcFirstPolyOrderId;
          else
          {
            rc = cwLogError(kInvalidArgRC,"'%s' is not one of the valid order types (i.e. 'net','proc').",order_label);
            goto errLabel;
          }
        }
        
        if((rc = network_create(proc->ctx,networkCfg,inst->net,proxyVarL,inst->count)) != kOkRC )
        {
          rc = cwLogError(rc,"Creation failed on the internal network.");
          goto errLabel;
        }


        // Set the internal net pointer in the base proc instance
        // so that network based utilities can scan it
        proc->internal_net = &inst->net;
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        inst_t* p = (inst_t*)proc->userPtr;
        network_destroy(p->net);

        
        mem::release( proc->userPtr );
        return kOkRC;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( proc_t* proc )
      {
        inst_t* p = (inst_t*)proc->userPtr;
        rc_t   rc = kOkRC;

        if((rc = exec_cycle(p->net)) != kOkRC )
        {
          rc = cwLogError(rc,"poly internal network exec failed.");
        }
        
        return rc;
      }

      class_members_t members = {
        .create               = create,
        .destroy              = destroy,
        .value                = value,
        .exec                 = exec,
        .report               = nullptr
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // balance
    //
    namespace balance
    {
      enum
      {
        kInPId,
        kOutPId,
        kInvOutPId
      };
      
      typedef struct
      {
        coeff_t value; 
      } inst_t;


      rc_t create( proc_t* proc )
      {
        rc_t rc = kOkRC;        
        coeff_t in_value = 0.5;
        proc->userPtr = mem::allocZ<inst_t>();
        
        if((rc  = var_register_and_get( proc, kAnyChIdx, kInPId, "in", kBaseSfxId, in_value )) != kOkRC )
          goto errLabel;

        if((rc = var_register_and_set( proc, kAnyChIdx,
                                       kOutPId,    "out",     kBaseSfxId, in_value,
                                       kInvOutPId, "inv_out", kBaseSfxId, (coeff_t)(1.0-in_value) )) != kOkRC )
        {
          goto errLabel;
        }
           
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        mem::release( proc->userPtr );
        return kOkRC;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t   rc = kOkRC;
        inst_t* inst = (inst_t*)(proc->userPtr);
        
        coeff_t value = 1;
        
        var_get(proc, kInPId,     kAnyChIdx, value);
        var_set(proc, kOutPId,    kAnyChIdx, value);
        var_set(proc, kInvOutPId, kAnyChIdx, (coeff_t)(1.0 - value) );

        if( inst->value != value )
        {
          inst->value = value;
        }
        

        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = nullptr
      };
      
    }    


    //------------------------------------------------------------------------------------------------------------------
    //
    // midi_in
    //
    
    namespace midi_in
    {
      enum
      {
        kDevLabelPId,
        kPortLabelPId,
        kOutPId
      };
      
      typedef struct
      {
        midi::ch_msg_t*    buf;
        unsigned           bufN;
        bool               dev_filt_fl;
        bool               port_filt_fl;        
        external_device_t* ext_dev;
      } inst_t;
      
      rc_t create( proc_t* proc )
      {
        rc_t        rc         = kOkRC;
        const char* dev_label  = nullptr;
        const char* port_label = nullptr;        
        inst_t*     inst       = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // Register variable and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kDevLabelPId,  "dev_label",  kBaseSfxId, dev_label,
                                       kPortLabelPId, "port_label", kBaseSfxId, port_label )) != kOkRC )
          
        {
          goto errLabel;
        }

        if((rc = var_register( proc, kAnyChIdx, kOutPId, "out", kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }


        inst->dev_filt_fl = true;
        inst->port_filt_fl = true;
        
        if( textIsEqual(dev_label,"<all>") )
        {
          inst->dev_filt_fl = false;
          dev_label = nullptr;
        }
          
        if( textIsEqual(port_label,"<all>") )
        {
          inst->port_filt_fl = false;
          port_label = nullptr;
        }
                
        if((inst->ext_dev = external_device_find( proc->ctx, dev_label, kMidiDevTypeId, kInFl, port_label )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The MIDI input device '%s' port '%s' could not be found.", cwStringNullGuard(dev_label), cwStringNullGuard(port_label));
          goto errLabel;
        }

        // Allocate a buffer large enough to hold the max. number of messages arriving on a single call to exec().
        inst->bufN = inst->ext_dev->u.m.maxMsgCnt;
        inst->buf = mem::allocZ<midi::ch_msg_t>( inst->bufN );

        // create one output MIDI buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, 0  );


      errLabel: 
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        mem::release(inst->buf);

        mem::release(inst);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      { return kOkRC; }
      
      rc_t exec( proc_t* proc )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;
        mbuf_t* mbuf = nullptr;

        // get the output variable
        if((rc = var_get(proc,kOutPId,kAnyChIdx,mbuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The MIDI file instance '%s' does not have a valid MIDI output buffer.",proc->label);
        }
        else
        {
          mbuf->msgA = nullptr;
          mbuf->msgN = 0;
          
          // if the device filter is not set
          if( !inst->dev_filt_fl)
          {
            mbuf->msgA = inst->ext_dev->u.m.msgArray;
            mbuf->msgN = inst->ext_dev->u.m.msgCnt;
          }
          else // the device filter is set
          {
            const midi::ch_msg_t* m = inst->ext_dev->u.m.msgArray;
            unsigned j = 0;
            for(unsigned i=0; i<inst->ext_dev->u.m.msgCnt && j<inst->bufN; ++i)
              if( m->devIdx == inst->ext_dev->ioDevIdx && (!inst->port_filt_fl || m->portIdx == inst->ext_dev->ioPortIdx) )
                inst->buf[j++] = m[i];

            mbuf->msgN = j;
            mbuf->msgA = inst->buf;
          }

          
        }
        
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = nullptr
      };
      
    }


    //------------------------------------------------------------------------------------------------------------------
    //
    // midi_out
    //
    
    namespace midi_out
    {
      enum
      {
        kDevLabelPId,
        kPortLabelPId,
        kBufMsgCntPId,
        kInPId,
      };
      
      typedef struct
      {
        external_device_t* ext_dev;
        
        unsigned           inVarN;
        
        unsigned           msgN;
        midi::ch_msg_t*    msgA;
        unsigned           msg_idx;        
      } inst_t;
      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc         = kOkRC; //
        const char* dev_label  = nullptr;
        const char* port_label = nullptr;
        unsigned    inVarN     = var_mult_count(proc,"in");
        mbuf_t*     mbuf       = nullptr;
        unsigned    sfxIdA[ inVarN ];
          
        // get the the sfx_id's of the input audio variables 
        if((rc = var_mult_sfx_id_array(proc, "in", sfxIdA, inVarN, p->inVarN )) != kOkRC )
          goto errLabel;

        std::sort(sfxIdA, sfxIdA + p->inVarN, [](unsigned& a,unsigned& b){ return a<b; } );

        
        // Register variables and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kDevLabelPId, "dev_label",  kBaseSfxId, dev_label,
                                       kPortLabelPId,"port_label", kBaseSfxId, port_label,
                                       kInPId,       "in",         kBaseSfxId, mbuf,
                                       kBufMsgCntPId,"buf_cnt",    kBaseSfxId, p->msgN )) != kOkRC )
        {
          goto errLabel;
        }
        

        if((p->ext_dev = external_device_find( proc->ctx, dev_label, kMidiDevTypeId, kOutFl, port_label )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The audio output device description '%s' could not be found.", cwStringNullGuard(dev_label));
          goto errLabel;
        }

        p->msgA = mem::allocZ<midi::ch_msg_t>(p->msgN);

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        mem::release(p->msgA);
        return kOkRC;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        return kOkRC;
      }
      
      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t          rc       = kOkRC;
        const mbuf_t* src_mbuf = nullptr;

        if((rc = var_get(proc,kInPId,kAnyChIdx,src_mbuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The MIDI output instance '%s' does not have a valid input connection.",proc->label);
        else
        {
          for(unsigned i=0; i<src_mbuf->msgN; ++i)
          {            
            const midi::ch_msg_t* m = src_mbuf->msgA + i;
            p->ext_dev->u.m.sendTripleFunc( p->ext_dev, m->ch, m->status, m->d0, m->d1 );
          }
        }
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }

    
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_in
    //
    
    namespace audio_in
    {
      enum
      {
        kDevLabelPId,
        kOutPId
      };
      
      typedef struct
      {
        const char*        dev_label;
        external_device_t* ext_dev;
      } inst_t;
      
      rc_t create( proc_t* proc )
      {
        rc_t rc = kOkRC;
        
        inst_t*                  inst = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // Register variable and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx, kDevLabelPId, "dev_label", kBaseSfxId, inst->dev_label )) != kOkRC )
        {
          goto errLabel;
        }

        if((inst->ext_dev = external_device_find( proc->ctx, inst->dev_label, kAudioDevTypeId, kInFl )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The audio input device description '%s' could not be found.", cwStringNullGuard(inst->dev_label));
          goto errLabel;
        }
        

        // create one output audio buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, inst->ext_dev->u.a.abuf->srate, inst->ext_dev->u.a.abuf->chN, proc->ctx->framesPerCycle );

      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;

        mem::release(inst);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( proc_t* proc )
      {
        rc_t     rc           = kOkRC;
        inst_t*  inst         = (inst_t*)proc->userPtr;
        abuf_t*  abuf         = nullptr;


        // verify that a source buffer exists
        if((rc = var_get(proc,kOutPId,kAnyChIdx,abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The audio  input instance '%s' does not have a valid audio output buffer.",proc->label);
        }
        else
        {
          unsigned chN    = std::min(inst->ext_dev->u.a.abuf->chN, abuf->chN );
          unsigned frameN = std::min(inst->ext_dev->u.a.abuf->frameN, abuf->frameN );
          memcpy(abuf->buf,inst->ext_dev->u.a.abuf->buf, frameN*chN*sizeof(sample_t));
        }

        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }


    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_out
    //
    
    namespace audio_out
    {
      enum
      {
        kInPId,
        kDevLabelPId,
      };
      
      typedef struct
      {
        const char*        dev_label;
        external_device_t* ext_dev;

      } inst_t;
      
      rc_t create( proc_t* proc )
      {
        rc_t          rc            = kOkRC;                 //
        inst_t*       inst          = mem::allocZ<inst_t>(); //
        const abuf_t* src_abuf      = nullptr;
        proc->userPtr = inst;

        // Register variables and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kDevLabelPId, "dev_label", kBaseSfxId, inst->dev_label,
                                       kInPId,       "in",        kBaseSfxId, src_abuf)) != kOkRC )
        {
          goto errLabel;
        }

        if((inst->ext_dev = external_device_find( proc->ctx, inst->dev_label, kAudioDevTypeId, kOutFl )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The audio output device description '%s' could not be found.", cwStringNullGuard(inst->dev_label));
          goto errLabel;
        }        

      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;

        mem::release(inst);

        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const abuf_t* src_abuf = nullptr;

        if((rc = var_get(proc,kInPId,kAnyChIdx,src_abuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid input connection.",proc->label);
        else
        {
          unsigned  chN    = std::min(inst->ext_dev->u.a.abuf->chN,    src_abuf->chN);
          unsigned  frameN = std::min(inst->ext_dev->u.a.abuf->frameN, src_abuf->frameN);
          unsigned  n      = chN * frameN;
          for(unsigned i=0; i<n; ++i)
            inst->ext_dev->u.a.abuf->buf[i] += src_abuf->buf[i];
          
        }
        
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value = value,
        .exec = exec,
        .report = nullptr
      };
      
    }


    
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_file_in
    //
    
    namespace audio_file_in
    {
      enum
      {
        kFnamePId,
        kEofFlPId,
        kOnOffFlPId,
        kSeekSecsPId,
        kOutPId
      };
      
      typedef struct
      {
        audiofile::handle_t afH;
        bool                eofFl;
        char*               filename;
      } inst_t;
      
      rc_t create( proc_t* proc )
      {
        rc_t rc = kOkRC;
        audiofile::info_t info;
        ftime_t seekSecs;
        const char* fname = nullptr;
        inst_t* inst = mem::allocZ<inst_t>();
        proc->userPtr = inst;

        if((rc = var_register( proc, kAnyChIdx, kOnOffFlPId, "on_off", kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        // Register variable and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kFnamePId,    "fname",    kBaseSfxId, fname,
                                       kSeekSecsPId, "seekSecs", kBaseSfxId, seekSecs,
                                       kEofFlPId,    "eofFl",    kBaseSfxId, inst->eofFl )) != kOkRC )
        {
          goto errLabel;
        }

        if((inst->filename = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = cwLogError(kInvalidArgRC,"The audio output filename could not be formed.");
          goto errLabel;
        }
        

        // open the audio file
        if((rc = audiofile::open(inst->afH,inst->filename,&info)) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"The audio file '%s' could not be opened.",inst->filename);
          goto errLabel;
        }

        if((rc = seek( inst->afH, (unsigned)lround(seekSecs*info.srate) )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"The audio file '%s' could not seek to offset %f seconds.",seekSecs);
          goto errLabel;
        }
        

        cwLogInfo("Audio '%s' srate:%f chs:%i frames:%i %f seconds.",inst->filename,info.srate,info.chCnt,info.frameCnt, info.frameCnt/info.srate );

        // create one output audio buffer - with the same configuration as the source audio file
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, info.srate, info.chCnt, proc->ctx->framesPerCycle );

      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;

        if((rc = audiofile::close(inst->afH)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"The close failed on the audio file '%s'.", cwStringNullGuard(inst->filename) );
        }

        mem::release(inst->filename);
        mem::release(inst);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        ftime_t seekSecs = 0;
        inst_t* inst = (inst_t*)proc->userPtr;

        if((rc = var_get(proc,kSeekSecsPId,kAnyChIdx,seekSecs)) != kOkRC )
          goto errLabel;

        if((rc = seek( inst->afH, (unsigned)lround(seekSecs * audiofile::sampleRate(inst->afH) ) )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"The audio file '%s' could not seek to offset %f seconds.",seekSecs);
          goto errLabel;
        }

        
      errLabel:
        return kOkRC;
      }
      
      rc_t exec( proc_t* proc )
      {
        rc_t     rc           = kOkRC;
        unsigned actualFrameN = 0;
        inst_t*  inst         = (inst_t*)proc->userPtr;
        abuf_t*  abuf         = nullptr;
        bool     onOffFl      = false;

        // get the 'on-off; flag
        if((rc = var_get(proc,kOnOffFlPId,kAnyChIdx,onOffFl)) != kOkRC )
          goto errLabel;

        // verify that a source buffer exists
        if((rc = var_get(proc,kOutPId,kAnyChIdx,abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid audio output buffer.",proc->label);
        }
        else
        {
          sample_t*   chBuf[ abuf->chN ];
        
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            chBuf[i] = abuf->buf + (i*abuf->frameN);

            // if the on/off flag is not set - then fill the output buffer with zeros
            if( !onOffFl )
              vop::zero(chBuf[i],abuf->frameN);
          }

          // if the on/off flag is set then read from audio file
          if( onOffFl )
            rc  = readFloat(inst->afH, abuf->frameN, 0, abuf->chN, chBuf, &actualFrameN );
          
          if( inst->eofFl && actualFrameN == 0)            
            rc = kEofRC;
        }

      errLabel:
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }


    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_file_out
    //
    
    namespace audio_file_out
    {
      enum
      {
        kInPId,
        kFnamePId,
        kBitsPId
      };
      
      typedef struct
      {
        audiofile::handle_t afH;
        char*               filename;
        unsigned            durSmpN;
      } inst_t;
      
      rc_t create( proc_t* proc )
      {
        rc_t          rc            = kOkRC;                 //
        unsigned      audioFileBits = 0;                     // set audio file sample format to 'float32'.
        inst_t*       inst          = mem::allocZ<inst_t>(); //
        const abuf_t* src_abuf      = nullptr;
        const char*   fname         = nullptr;
        
        proc->userPtr = inst;

        // Register variables and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kFnamePId, "fname", kBaseSfxId, fname,
                                       kBitsPId,  "bits",  kBaseSfxId, audioFileBits,
                                       kInPId,    "in",    kBaseSfxId, src_abuf )) != kOkRC )
        {
          goto errLabel;
        }

        
        if((inst->filename = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = cwLogError(kInvalidArgRC,"The audio output filename could not be formed.");
          goto errLabel;
        }
          
        // create the audio file with the same channel count as the incoming signal
        if((rc = audiofile::create( inst->afH, inst->filename, src_abuf->srate, audioFileBits, src_abuf->chN)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"The audio file create failed on '%s'.",cwStringNullGuard(inst->filename));
          goto errLabel;
        }

      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;

        // close the audio file
        if((rc = audiofile::close( inst->afH )) != kOkRC )
        {
          rc = cwLogError(rc,"Close failed on the audio output file '%s'.",inst->filename);
          goto errLabel;
        }

        mem::release(inst->filename);
        mem::release(inst);

      errLabel:
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const abuf_t* src_abuf = nullptr;
        
        if((rc = var_get(proc,kInPId,kAnyChIdx,src_abuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid input connection.",proc->label);
        else
        {
          sample_t*     chBuf[ src_abuf->chN ];
        
          for(unsigned i=0; i<src_abuf->chN; ++i)
            chBuf[i] = src_abuf->buf + (i*src_abuf->frameN);
        
          if((rc = audiofile::writeFloat(inst->afH, src_abuf->frameN, src_abuf->chN, chBuf )) != kOkRC )
            rc = cwLogError(rc,"Audio file write failed on instance: '%s'.", proc->label );

          // print a minutes counter
          inst->durSmpN += src_abuf->frameN;          
          if( src_abuf->srate!=0 && inst->durSmpN % ((unsigned)src_abuf->srate*60) == 0 )
            printf("audio file out: %5.1f min\n", inst->durSmpN/(src_abuf->srate*60));

          //if( 48000 <= inst->durSmpN  && inst->durSmpN < 49000 )
          //  printf("break\n");
        }
        
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value = value,
        .exec = exec,
        .report = nullptr
      };
      
    }

    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_gain
    //
    namespace audio_gain
    {
      enum
      {
        kInPId,
        kGainPId,
        kOutPId
      };

      typedef struct inst_str
      {
        unsigned n;
        coeff_t vgain;
        coeff_t gain;
      } inst_t;

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* abuf    = nullptr; //
        proc->userPtr = mem::allocZ<inst_t>();

        // get the source audio buffer
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;

        // register the gain 
        for(unsigned i=0; i<abuf->chN; ++i)
          if((rc = var_register( proc, i, kGainPId, "gain", kBaseSfxId )) != kOkRC )
            goto errLabel;
          
        // create the output audio buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );


      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        inst_t* inst = (inst_t*)(proc->userPtr);
        mem::release(inst);
        return kOkRC;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        coeff_t value = 0;
        inst_t* inst = (inst_t*)proc->userPtr;
        var_get(proc,kGainPId,0,value);
        
        if( inst->vgain != value )
        {
          inst->vgain = value;
          //printf("VALUE GAIN: %s %s : %f\n", proc->label, var->label, value );
        }
        return kOkRC;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t     rc           = kOkRC;
        const abuf_t* ibuf = nullptr;
        abuf_t*       obuf = nullptr;
        inst_t*  inst = (inst_t*)(proc->userPtr);

        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        // for each channel
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          sample_t* isig = ibuf->buf + i*ibuf->frameN;
          sample_t* osig = obuf->buf + i*obuf->frameN;
          sample_t  gain = 1;
          
          var_get(proc,kGainPId,i,gain);

          // apply the gain
          for(unsigned j=0; j<ibuf->frameN; ++j)
            osig[j] = gain * isig[j];

          if( i==0 && gain != inst->gain )
          {
            inst->gain = gain;
            //printf("EXEC GAIN: %s %f\n",proc->label,gain);
            //proc_print(proc);
          }
        }  
        
      errLabel:
        return rc;
      }

      
      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value = value,
        .exec = exec,
        .report = nullptr
      };
      
    }


    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_split
    //
    namespace audio_split
    {

      enum {
        kInPId,
        kSelectPId,
        kInGainPId,
        kOutGainPId
        
      };

      typedef struct out_var_str
      {
        unsigned audioChN;
        coeff_t* ogainV;     // ogainV[ audioChN ]
        unsigned* iChIdxV;   // inChIdx[ audioChN ]        
      } out_var_t;
        
      typedef struct
      {

        unsigned   oVarN;       // count of output variables
        out_var_t* oVarA;       // oVarA[ oVarN ]

        unsigned   baseOutPId;

        unsigned  iChN;        // count of input audio channels
        coeff_t*  igainV;      // igainV[ inChN ] input ch. gain coeff's
          
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t            rc      = kOkRC;        
        const abuf_t*   abuf    = nullptr; //        
        const object_t* selList = nullptr;
        unsigned        selListN = 0;
        unsigned*       oVarSelMap = nullptr;
        
        
        // get the source audio buffer
        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kInPId,"in",kBaseSfxId, abuf,
                                      kSelectPId,"select",kBaseSfxId, selList )) != kOkRC )
        {
          goto errLabel;
        }

        // the input must not have 0 channels
        if( abuf->chN == 0 )
          goto errLabel;

        // validate the channel input->output map
        if( selList == nullptr || !selList->is_list() )
        {
          rc = cwLogError(kSyntaxErrorRC,"The 'audio_split' 'select' list has invalid syntax.");
          goto errLabel;
        }

        // there must be one in->out map for each input channel
        if(( selListN = selList->child_count()) != abuf->chN )
        {
          rc = cwLogError(kInvalidArgRC,"The 'audio_split' selection list must be the same length as the count of input channels:%i.",abuf->chN);
          goto errLabel;
        }

        // convert in->out map to an array
        oVarSelMap = mem::allocZ<unsigned>(selListN);
        
        // determine the count of output variables
        for(unsigned i = 0; i<selListN; ++i)
        {
          unsigned oVarIdx;
          const object_t* listEle = selList->child_ele(i);
          
          if( listEle == nullptr || (rc = listEle->value(oVarIdx)) != kOkRC )
          {
            rc = cwLogError(kInvalidArgRC,"The 'audio_split' selection list element at index %i is not a valid integer.",i);
            goto errLabel;
          }

          // count the number of outputs
          if( oVarIdx+1 > p->oVarN )
            p->oVarN = oVarIdx+1;

           oVarSelMap[i] = oVarIdx;
        }

        p->oVarA      = mem::allocZ<out_var_t>(p->oVarN);
        p->baseOutPId = kOutGainPId + p->oVarN;
        
        // fill p->oVar[].audioChN with the count of channels for each output variable
        for(unsigned i=0; i<selListN; ++i)
        {
          unsigned oVarIdx;
          selList->child_ele(i)->value(oVarIdx);
          p->oVarA[oVarIdx].audioChN += 1;          
        }

        // for each output variable
        for(unsigned i=0; i<p->oVarN; ++i)
        {
          out_var_t* ov = p->oVarA + i;

          if( ov->audioChN == 0 )
          {
            cwLogWarning("No channels have been assigned to 'audio_split' output index %i on '%s:%i'.",i,cwStringNullGuard(proc->label),proc->label_sfx_id);
            continue;
          }
          
          ov->ogainV  = mem::allocZ<coeff_t>( ov->audioChN );
          ov->iChIdxV = mem::allocZ<unsigned>(ov->audioChN );

          // create the output signal variable
          if((rc = var_register_and_set( proc, "out", kBaseSfxId + i, p->baseOutPId+i, kAnyChIdx, abuf->srate, ov->audioChN, abuf->frameN )) != kOkRC )
            goto errLabel;

          // register the ogain variables for this output and store the current gain values
          for(unsigned j=0; j<ov->audioChN; ++j)
            if((rc = var_register_and_get(proc, j, kOutGainPId + i,"ogain", kBaseSfxId + i, ov->ogainV[j])) != kOkRC )
              goto errLabel;

          // fill ov->iChIdV[] with the input channels that are mapped to this output var
          for(unsigned iChIdx=0,k=0; iChIdx<selListN; ++iChIdx)
            if( oVarSelMap[iChIdx] == i )
            {
              assert(k<ov->audioChN);
              ov->iChIdxV[k++] = iChIdx;
            }
          
        }
        

        p->iChN   = abuf->chN;
        p->igainV = mem::allocZ<coeff_t>(abuf->chN);
        
        // register the input gain variables and store the current gain values
        for(unsigned i=0; i<abuf->chN; ++i)
          if((rc = var_register_and_get( proc, i, kInGainPId, "igain", kBaseSfxId, p->igainV[i] )) != kOkRC )
            goto errLabel;


      errLabel:
        mem::release(oVarSelMap);
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        for(unsigned i=0; i<p->oVarN; ++i)
        {
          mem::release(p->oVarA[i].ogainV);
          mem::release(p->oVarA[i].iChIdxV);
        }
        mem::release(p->oVarA);
        mem::release(p->igainV);
        
        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        
        switch( var->vid )
        {
          case kInPId:
            break;
          case kSelectPId:
            break;
            
          case kInGainPId:
            // skip kAnyChIdx because individual channels exist will be sent
            if(var->chIdx != kAnyChIdx )
              rc = var_get(var,p->igainV[ var->chIdx ]);
            break;

            
            
          default:

            // skip kAnyChIdx because individual channels exist and will be sent
            if( kOutGainPId <= var->vid && var->vid < kOutGainPId + p->oVarN && var->chIdx != kAnyChIdx )
            {
              unsigned oVarIdx = var->label_sfx_id - kBaseSfxId;
              assert( oVarIdx < p->oVarN && var->chIdx < p->oVarA[oVarIdx].audioChN );
              rc = var_get(var,p->oVarA[ oVarIdx ].ogainV[var->chIdx]);
            }
            break;
            
            break;
        }

        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t          rc       = kOkRC;

        const abuf_t* ibuf = nullptr;
          
        // get the input audio buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        for(unsigned i=0; i<p->oVarN; ++i)
        {
          abuf_t*    obuf = nullptr;
          
          // get the ith output buffer
          if((rc = var_get(proc, p->baseOutPId +i, kAnyChIdx, obuf)) != kOkRC )
            goto errLabel;

          for( unsigned oChIdx=0; oChIdx<obuf->chN; ++oChIdx)
          {
            unsigned  iChIdx = p->oVarA[i].iChIdxV[oChIdx];
            coeff_t   ogain  = p->oVarA[i].ogainV[oChIdx];
            coeff_t   igain  = p->igainV[iChIdx];
            coeff_t   gain   = igain * ogain;
            sample_t* isig   = ibuf->buf + iChIdx * ibuf->frameN;
            sample_t* osig   = obuf->buf + oChIdx * obuf->frameN;            

            for(unsigned j=0; j<ibuf->frameN; ++j)
              osig[j] = gain * isig[j];
          }
          
        }
        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_duplicate
    //
    namespace audio_duplicate
    {
      enum {
        kInPId,
        kDuplicatePId,
        kGainPId,
        kOutPId,
        
      };
        
      typedef struct
      {
        unsigned* chDuplMap;  // [ inChN ] duplicate channel map
        unsigned  outChN;
      } inst_t;


      rc_t create( proc_t* proc )
      {
        rc_t          rc   = kOkRC;        
        const abuf_t* abuf = nullptr; //        
        inst_t*       inst = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // get the source audio buffer
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;

        if( abuf->chN )
        {
          inst->chDuplMap = mem::allocZ<unsigned>(abuf->chN);
        
          // register the gain 
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            if((rc = var_register_and_get( proc, i, kDuplicatePId, "duplicate", kBaseSfxId, inst->chDuplMap[i] )) != kOkRC )
              goto errLabel;

            if( inst->chDuplMap[i] )
            {
              // register an input gain control
              if((rc = var_register( proc, inst->outChN, kGainPId, "gain", kBaseSfxId)) != kOkRC )
                goto errLabel;

              // count the number of selected channels to determine the count of output channels
              inst->outChN += inst->chDuplMap[i];
            }
          }
          
          // create the output audio buffer
          if( inst->outChN == 0 )
            cwLogWarning("The audio split instance '%s' has no selected channels.",proc->label);
          else
            rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, inst->outChN, abuf->frameN );
        }

      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;

        mem::release(inst->chDuplMap);

        mem::release(inst);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc       = kOkRC;
        const abuf_t* ibuf     = nullptr;
        abuf_t*       obuf     = nullptr;
        inst_t*       inst     = (inst_t*)proc->userPtr;
        unsigned      outChIdx = 0;
        
        if( inst->outChN )
        {
          // get the src buffer
          if((rc = var_get(proc,kInPId, kAnyChIdx, ibuf )) != kOkRC )
            goto errLabel;

          // get the dst buffer
          if((rc = var_get(proc,kOutPId, kAnyChIdx, obuf)) != kOkRC )
            goto errLabel;

          // for each input channel          
          for(unsigned i=0; i<ibuf->chN  && outChIdx<obuf->chN; ++i)
          {
            sample_t* isig = ibuf->buf + i * ibuf->frameN;
            sample_t  gain = 1;
          
            var_get(proc,kGainPId,i,gain);
            
            for(unsigned j=0; j<inst->chDuplMap[i]; ++j )
            {            
              sample_t* osig = obuf->buf + j * obuf->frameN;

              // apply the gain
              for(unsigned k=0; k<ibuf->frameN; ++k)
                osig[k] = gain * isig[k];

              outChIdx += 1;
            }
          }
        }
        
      errLabel:
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }    


    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_mix
    //
    namespace audio_mix
    {
      enum {
        kOutPId,
        kOutGainPId,
        kInBasePId,
      };

      typedef struct audio_gain_str
      {
        unsigned sfx_id;   // sfx_id of both the audio and gain var's 
        unsigned audioChN; // count of audio channels
        unsigned aVId;     // (there can only be one audio var.)
        unsigned gVId;     // (there is one gain var. per audio channel)
        coeff_t* gainV;    // gainV[ audioChN ]
      } audio_gain_t;
      
      typedef struct
      {
        unsigned baseInGainPId;
        unsigned baseOutGainPId;
        
        unsigned inAudioVarCnt;

        audio_gain_t  oag;  
        audio_gain_t* iagV; // iagV[ inAudioVarCnt ]
        
      } inst_t;


      // Mix the the first N channels of the input audio signal from iag->aVId
      // into the first N channels of the output signal.
      rc_t _mix( proc_t* proc, audio_gain_t* iag, audio_gain_t* oag, abuf_t* obuf )
      {
        rc_t rc = kOkRC;
        const abuf_t* ibuf = nullptr;
        unsigned chN;
        
        // get the input audio buffer
        if((rc = var_get(proc, iag->aVId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        chN = std::min(ibuf->chN,obuf->chN);

        for(unsigned i=0; i<chN; ++i)
        {
            const sample_t* isig = ibuf->buf + i*ibuf->frameN;
            sample_t*       osig = obuf->buf + i*obuf->frameN;
            coeff_t         gain = iag->gainV[i] * oag->gainV[i];
          
            for(unsigned j=0; j<obuf->frameN; ++j)
              osig[j] += gain * isig[j];          
        }
        
      errLabel:
        return rc;
      }
      
      // Be sure that there is a gain channel instantiated for every audio channel
      // and fill ag->gainV[] with the current value of each pre-created gain variable.
      // or the class default gain value if no pre-created gain variable exists.
      rc_t _setup_gain( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned a_vid, unsigned g_vid, unsigned audioChN, audio_gain_t* ag )
      {
        rc_t rc = kOkRC;

        // setup the gain control record
        ag->audioChN = audioChN;
        ag->gainV    = mem::allocZ<coeff_t>(audioChN);
        ag->sfx_id   = sfx_id;
        ag->aVId     = a_vid;
        ag->gVId     = g_vid;
        
        vop::fill(ag->gainV,audioChN,1);

        // register audio gain variables for each channel of the audio signal represented by ag
        for(unsigned i=0; i<ag->audioChN; ++i)
          if((rc = var_register_and_get(proc, i, g_vid, var_label, sfx_id, ag->gainV[i])) != kOkRC )
            goto errLabel;
        
        
      errLabel:
        return rc;
      }
      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;        

        unsigned audioFrameN     = 0;
        unsigned maxInAudioChCnt = 0;
        srate_t  srate           = 0;
        unsigned aSfxIdAllocN     = var_mult_count(proc,"in");
        unsigned aSfxIdA[ aSfxIdAllocN ];
          
        // get the the sfx_id's of the input audio variables 
        if((rc = var_mult_sfx_id_array(proc, "in", aSfxIdA, aSfxIdAllocN, p->inAudioVarCnt )) != kOkRC )
          goto errLabel;

        p->iagV = mem::allocZ<audio_gain_t>(p->inAudioVarCnt);

        // set the baseInGainPId
        p->baseInGainPId = kInBasePId + p->inAudioVarCnt;

        // for each audio input var
        for(unsigned i=0; i<p->inAudioVarCnt; ++i)
        {
          abuf_t* abuf;

          // register the input audio variable
          if((rc = var_register_and_get(proc,kAnyChIdx,kInBasePId+i,"in",aSfxIdA[i],abuf)) != kOkRC )
            goto errLabel;
          
          // the sample rate of the input audio signals must be the same
          if( i != 0 && abuf->srate != srate )
          {
            rc = cwLogError(kInvalidArgRC,"All signals on a poly merge must have the same sample rate.");
            goto errLabel;
          }

          srate = abuf->srate;

          // the count of frames in all audio signals must be the same
          if( audioFrameN != 0 && abuf->frameN != audioFrameN )
          {
            rc = cwLogError(kInvalidArgRC,"All signals on a poly merge must have the same frame count.");
            goto errLabel;
          }

          audioFrameN = abuf->frameN;

          // track the max channel count among all audio input variables
          if( abuf->chN > maxInAudioChCnt )
            maxInAudioChCnt = abuf->chN;

          // setup the audio_gain record for this input audio variable
          if((rc= _setup_gain(proc, "igain", aSfxIdA[i], kInBasePId+i, p->baseInGainPId+i, abuf->chN, p->iagV + i )) != kOkRC )
            goto errLabel;
        }

        if((rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srate, maxInAudioChCnt, audioFrameN )) != kOkRC )
        {
          goto errLabel;
        }

        // setup the audio_gain record for the output gains
        if((rc= _setup_gain(proc, "ogain", kBaseSfxId, kOutPId, kOutGainPId, maxInAudioChCnt, &p->oag )) != kOkRC )
          goto errLabel;
        

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->oag.gainV);
        
        for(unsigned i=0; i<p->inAudioVarCnt; ++i)
          mem::release(p->iagV[i].gainV);
        
        mem::release(p->iagV);

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        switch( var->vid )
        {

          case kOutPId:
            break;
            
          case kOutGainPId:
            assert( var->chIdx == kAnyChIdx || var->chIdx < p->oag.audioChN);

            // (we skip kAnyChIdx because individual channels will follow)
            if( var->chIdx != kAnyChIdx )
              var_get(var,p->oag.gainV[var->chIdx] ); // ... update the associated gainV[] value            
            break;
            
          default:
            
            // if this is an in-gain value
            if( p->baseInGainPId  <= var->vid && var->vid < p->baseInGainPId + p->inAudioVarCnt  )
            {
              // determine which in-gain variable this var is associated with var->vid
              for(unsigned i=0; i<p->inAudioVarCnt; ++i)
                if( p->iagV[i].gVId == var->vid )
                {
                  assert( var->chIdx == kAnyChIdx || var->chIdx < p->iagV[i].audioChN);
                  
                  // ... and update the associated gainV[] value
                  // (we skip kAnyChIdx because individual channels will follow)
                  if( var->chIdx != kAnyChIdx )
                    var_get(var,p->iagV[i].gainV[var->chIdx]);
                  break;
                }
              
            }
            else
            {
              assert(kInBasePId <= var->vid && var->vid < kInBasePId + p->inAudioVarCnt);
            }
          
        }

             
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t          rc    = kOkRC;
        abuf_t*       obuf  = nullptr;

        // get the output audio buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;
        
        // zero the output buffer
        vop::zero(obuf->buf, obuf->frameN*obuf->chN );

        // mix each input channel into the output buffer
        for(unsigned i=0; i<p->inAudioVarCnt; ++i)
          if((rc =_mix(proc, p->iagV + i, &p->oag, obuf )) != kOkRC )
            goto errLabel;
        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_mix
    //
    namespace audio_mix_0
    {
      enum {
        kIn0PId,
        kIn1PId,
        kGain0PId,
        kGain1PId,
        kOutPId,
      };
      
      typedef struct
      {
      } inst_t;


      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* abuf0  = nullptr; //
        const abuf_t* abuf1  = nullptr;
        unsigned      outChN = 0;
        double dum;
        
        // get the source audio buffer
        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kIn0PId,"in0",kBaseSfxId,abuf0,
                                      kIn1PId,"in1",kBaseSfxId,abuf1 )) != kOkRC )
        {
          goto errLabel;
        }

        assert( abuf0->frameN == abuf1->frameN );

        outChN = std::max(abuf0->chN, abuf1->chN);

        // register the gain
        var_register_and_get( proc, kAnyChIdx, kGain0PId, "gain0", kBaseSfxId, dum );
        var_register_and_get( proc, kAnyChIdx, kGain1PId, "gain1", kBaseSfxId, dum );
          
        // create the output audio buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf0->srate, outChN, abuf0->frameN );

      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      { return kOkRC; }

      rc_t value( proc_t* proc, variable_t* var )
      { return kOkRC; }

      rc_t _mix( proc_t* proc, unsigned inPId, unsigned gainPId, abuf_t* obuf )
      {
        rc_t          rc   = kOkRC;
        const abuf_t* ibuf = nullptr;
        
        if((rc = var_get(proc, inPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;


        if(rc == kOkRC )
        {          
          unsigned chN = std::min(ibuf->chN, obuf->chN );
          
          for(unsigned i=0; i<chN; ++i)
          {
            const sample_t* isig = ibuf->buf + i*ibuf->frameN;
            sample_t*       osig = obuf->buf + i*obuf->frameN;
            coeff_t          gain = 1;

            if((rc = var_get(proc, gainPId, kAnyChIdx, gain)) != kOkRC )
              goto errLabel;
            
            for(unsigned j=0; j<obuf->frameN; ++j)
              osig[j] += gain * isig[j];
          }
        }

      errLabel:
        return rc;
        
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc    = kOkRC;
        abuf_t*       obuf  = nullptr;
        //const abuf_t* ibuf0 = nullptr;
        //const abuf_t* ibuf1 = nullptr;

        if((rc = var_get(proc,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        //if((rc = var_get(proc,kIn0PId, kAnyChIdx, ibuf0 )) != kOkRC )
        //  goto errLabel;
        
        //if((rc = var_get(proc,kIn1PId, kAnyChIdx, ibuf1 )) != kOkRC )
        //  goto errLabel;

        vop::zero(obuf->buf, obuf->frameN*obuf->chN );
        
        _mix( proc, kIn0PId, kGain0PId, obuf );
        _mix( proc, kIn1PId, kGain1PId, obuf );
        
      errLabel:
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_marker
    //
    namespace audio_marker
    {
      enum
      {
        kInPId,
        kTriggerPId,
        kOutPId
      };

      typedef struct inst_str
      {
        bool trig_fl;
      } inst_t;

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* abuf    = nullptr; //
        proc->userPtr = mem::allocZ<inst_t>();

        // get the source audio buffer
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;

        // register the marker input 
        if((rc = var_register( proc, kAnyChIdx, kTriggerPId, "trigger", kBaseSfxId )) != kOkRC )
          goto errLabel;
          
        // create the output audio buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        return kOkRC;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        if( var->vid == kTriggerPId )
          p->trig_fl = true;
        
        return kOkRC;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t          rc   = kOkRC;
        const abuf_t* ibuf = nullptr;
        abuf_t*       obuf = nullptr;
        sample_t      mark = p->trig_fl ? 1 : 0;

        p->trig_fl = false;
        
        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;          
        
        // for each channel
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          sample_t* isig = ibuf->buf + i*ibuf->frameN;
          sample_t* osig = obuf->buf + i*obuf->frameN;

          // apply the marker
          for(unsigned j=0; j<ibuf->frameN; ++j)
            osig[j] = mark + isig[j];
        }

      errLabel:
        return rc;
      }

      
      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // sine_tone
    //
    
    namespace sine_tone
    {
      enum
      {
        kSratePId,
        kChCntPid,
        kFreqHzPId,
        kPhasePId,
        kDcPId,
        kGainPId,
        kOutPId
      };
      
      typedef struct
      {
        double *phaseA;
      } inst_t;
      
      rc_t create( proc_t* proc )
      {
        rc_t     rc    = kOkRC;
        inst_t*  inst  = mem::allocZ<inst_t>();
        srate_t  srate = 0;
        unsigned chCnt = 0;
        coeff_t   gain;
        coeff_t   hz;
        coeff_t   phase;
        coeff_t   dc;
        
        proc->userPtr = inst;

        // Register variables and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kChCntPid, "chCnt", kBaseSfxId, chCnt,
                                       kSratePId, "srate", kBaseSfxId, srate)) != kOkRC )
        {
          goto errLabel;
        }

        // Sample rate logic:
        // The sample rate may be set directly, or sourced.
        // If the srate is 0 then this indicates that the system sample rate should be used.
        // if the sample rate is sourced and 0 it is a configuration error.

        // if no sample rate was given then use the system sample rate.
        if( srate == 0 )
          srate = proc->ctx->sample_rate;
        
        // register each oscillator variable
        for(unsigned i=0; i<chCnt; ++i)
        {
          unsigned ch_srate = 0;
          
          if((rc = var_register_and_get( proc, i,
                                         kSratePId,  "srate", kBaseSfxId, ch_srate,
                                         kFreqHzPId, "hz",    kBaseSfxId, hz,
                                         kPhasePId,  "phase", kBaseSfxId, phase,
                                         kDcPId,     "dc",    kBaseSfxId, dc,
                                         kGainPId,   "gain",  kBaseSfxId, gain)) != kOkRC )
          {
            goto errLabel;
          }

          // if no srate was set on this channel then use the default sample rate
          if( ch_srate == 0 )
            if((rc = var_set(proc,kSratePId,i,srate)) != kOkRC )
              goto errLabel;
        }

        //printf("%s: sr:%f hz:%f phs:%f dc:%f gain:%f\n",proc->label,srate,hz,phase,dc,gain);
        
        // create one output audio buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId,
                                   kOutPId, kAnyChIdx, srate, chCnt, proc->ctx->framesPerCycle );

        inst->phaseA = mem::allocZ<double>( chCnt );
        

      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;

        mem::release(inst->phaseA);
        mem::release(inst);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( proc_t* proc )
      {
        rc_t     rc           = kOkRC;
        inst_t*  inst         = (inst_t*)proc->userPtr;
        abuf_t*  abuf         = nullptr;

        // get the output signal buffer
        if((rc = var_get(proc,kOutPId,kAnyChIdx,abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The Sine Tone instance '%s' does not have a valid audio output buffer.",proc->label);
        }
        else
        {
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            coeff_t    gain  = val_get<coeff_t>( proc, kGainPId, i );
            coeff_t    hz    = val_get<coeff_t>( proc, kFreqHzPId, i );
            coeff_t    phase = val_get<coeff_t>( proc, kPhasePId, i );
            coeff_t    dc    = val_get<coeff_t>( proc, kDcPId, i );
            srate_t   srate = val_get<srate_t>(proc, kSratePId, i );                        
            sample_t* v     = abuf->buf + (i*abuf->frameN);
            
            for(unsigned j=0; j<abuf->frameN; ++j)
              v[j] = (sample_t)((gain * sin( inst->phaseA[i] + phase + (2.0 * M_PI * j * hz/srate)))+dc);

            inst->phaseA[i] += 2.0 * M_PI * abuf->frameN * hz/srate;

            //if( i==0 )
            //  printf("hz:%f gain:%f phs:%f : %f\n",hz,gain,inst->phaseA[i],v[0]);
          }
        }
        
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // Phase Vocoder (Analysis)
    //
    namespace pv_analysis
    {
      typedef struct dsp::pv_anl::obj_str<sample_t,fd_sample_t> pv_t;

      enum {
        kInPId,
        kMaxWndSmpNPId,
        kWndSmpNPId,
        kHopSmpNPId,
        kHzFlPId,
        kOutPId
      };
      
      typedef struct
      {
        pv_t**   pvA;       // pvA[ srcBuf.chN ]
        unsigned pvN;
        unsigned maxWndSmpN;
        unsigned wndSmpN;
        unsigned hopSmpN;
        bool     hzFl;
      } inst_t;
    

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        unsigned      flags  = 0;
        inst_t*       inst   = mem::allocZ<inst_t>();
        proc->userPtr = inst;

        if((rc = var_register_and_get( proc, kAnyChIdx,kInPId, "in", kBaseSfxId, srcBuf )) != kOkRC )
        {
          cwLogError(kInvalidArgRC,"Unable to access the 'src' buffer.");
        }
        else
        {
          flags  = inst->hzFl ? dsp::pv_anl::kCalcHzPvaFl : dsp::pv_anl::kNoCalcHzPvaFl;
          inst->pvN = srcBuf->chN;
          inst->pvA = mem::allocZ<pv_t*>( inst->pvN );  // allocate pv channel array
          
          const fd_sample_t* magV[ srcBuf->chN ];
          const fd_sample_t* phsV[ srcBuf->chN ];
          const fd_sample_t* hzV[  srcBuf->chN ];
          unsigned maxBinNV[ srcBuf->chN ];
          unsigned binNV[ srcBuf->chN ];
          unsigned hopNV[ srcBuf->chN ];
          
          // create a pv anlaysis object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            
            unsigned maxWndSmpN = 0;
            unsigned wndSmpN = 0;
            unsigned hopSmpN = 0;
            bool hzFl = false;
            
            if((rc = var_register_and_get( proc, i,
                                           kMaxWndSmpNPId, "maxWndSmpN", kBaseSfxId, maxWndSmpN,
                                           kWndSmpNPId, "wndSmpN",       kBaseSfxId, wndSmpN,
                                           kHopSmpNPId, "hopSmpN",       kBaseSfxId, hopSmpN,
                                           kHzFlPId,    "hzFl",          kBaseSfxId, hzFl )) != kOkRC )
            {
              goto errLabel;
            }
            
            if((rc = create( inst->pvA[i], proc->ctx->framesPerCycle, srcBuf->srate, maxWndSmpN, wndSmpN, hopSmpN, flags )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The PV analysis object create failed on the instance '%s'.",proc->label);
              goto errLabel;
            }

            maxBinNV[i] = inst->pvA[i]->maxBinCnt;
            binNV[i] = inst->pvA[i]->binCnt;
            hopNV[i] = hopSmpN;
            
            magV[i]  = inst->pvA[i]->magV;
            phsV[i]  = inst->pvA[i]->phsV;
            hzV[i]   = inst->pvA[i]->hzV;
          }

        
          // create the fbuf 'out'
          if((rc = var_register_and_set(proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, maxBinNV, binNV, hopNV, magV, phsV, hzV )) != kOkRC )
          {
            cwLogError(kOpFailRC,"The output freq. buffer could not be created.");
            goto errLabel;
          }
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        
        for(unsigned i=0; i<inst->pvN; ++i)
          destroy(inst->pvA[i]);
        
        mem::release(inst->pvA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;

        if( var->chIdx != kAnyChIdx && var->chIdx < inst->pvN )
        {
          unsigned val = 0;
          pv_t* pva = inst->pvA[ var->chIdx ];
          
          switch( var->vid )
          {
            case kWndSmpNPId:
              rc = var_get( var, val );
              dsp::pv_anl::set_window_length(pva,val);
              //printf("WL:%i %i\n",val,var->chIdx);
              break;              
          }
          
        }
        
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const abuf_t* srcBuf = nullptr;
        fbuf_t*       dstBuf = nullptr;

        // verify that a source buffer exists
        if((rc = var_get(proc,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",proc->label);
          goto errLabel;
        }

        // verify that the dst buffer exits
        if((rc = var_get(proc,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid output.",proc->label);
          goto errLabel;
        }

        // for each input channel
        for(unsigned i=0; i<srcBuf->chN; ++i)
        {
          dstBuf->readyFlV[i] = false;
          
          // call the PV analysis processor
          if( dsp::pv_anl::exec( inst->pvA[i], srcBuf->buf + i*srcBuf->frameN, srcBuf->frameN ) )
          {
            // rescale the frequency domain magnitude
            vop::mul(dstBuf->magV[i], dstBuf->binN_V[i]/2, dstBuf->binN_V[i]);
            
            dstBuf->readyFlV[i] = true;

          }
        }

      errLabel:
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = nullptr
      };  
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // Phase Vocoder (Synthesis)
    //
    namespace pv_synthesis
    {
      typedef struct dsp::pv_syn::obj_str<sample_t,fd_sample_t> pv_t;

      enum {
        kInPId,
        kOutPId
      };
      
      
      typedef struct
      {
        pv_t**   pvA;     // pvA[ srcBuf.chN ]
        unsigned pvN;
        unsigned wndSmpN; //  
        unsigned hopSmpN; //
        bool     hzFl;    //
      } inst_t;
    

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const fbuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        proc->userPtr = inst;

        if((rc = var_register_and_get( proc, kAnyChIdx,kInPId, "in", kBaseSfxId, srcBuf)) != kOkRC )
        {
          goto errLabel;
        }
        else
        {

          // allocate pv channel array
          inst->pvN = srcBuf->chN;
          inst->pvA = mem::allocZ<pv_t*>( inst->pvN );  

          // create a pv anlaysis object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            unsigned wndSmpN = (srcBuf->binN_V[i]-1)*2;
            
            if((rc = create( inst->pvA[i], proc->ctx->framesPerCycle, srcBuf->srate, wndSmpN, srcBuf->hopSmpN_V[i] )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The PV synthesis object create failed on the instance '%s'.",proc->label);
              goto errLabel;
            }
          }

          if((rc = var_register( proc, kAnyChIdx, kInPId, "in", kBaseSfxId)) != kOkRC )
            goto errLabel;

          // create the abuf 'out'
          rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, proc->ctx->framesPerCycle );
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->pvN; ++i)
          destroy(inst->pvA[i]);
        
        mem::release(inst->pvA);
        mem::release(inst);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const fbuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        
        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;
        
        for(unsigned i=0; i<srcBuf->chN; ++i)
        {
          if( srcBuf->readyFlV[i] )
            dsp::pv_syn::exec( inst->pvA[i], srcBuf->magV[i], srcBuf->phsV[i] );
          
          const sample_t* ola_out = dsp::ola::execOut(inst->pvA[i]->ola);
          if( ola_out != nullptr )
            abuf_set_channel( dstBuf, i, ola_out, inst->pvA[i]->ola->procSmpCnt );
          
          //abuf_set_channel( dstBuf, i, inst->pvA[i]->ola->outV, dstBuf->frameN );
        }
        
        
      errLabel:
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = nullptr
      };      
    }

    //------------------------------------------------------------------------------------------------------------------
    //
    // Spec Dist
    //
    namespace spec_dist
    {
      typedef struct dsp::spec_dist::obj_str<fd_sample_t,fd_sample_t> spec_dist_t;

      enum
      {
        kInPId,
        kBypassPId,
        kCeilingPId,
        kExpoPId,
        kThreshPId,
        kUprSlopePId,
        kLwrSlopePId,
        kMixPId,
        kOutPId,
      };


      typedef struct
      {
        spec_dist_t** sdA;
        unsigned sdN;
      } inst_t;
    

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const fbuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",proc->label);
          goto errLabel;
        }
        else
        {
          // allocate pv channel array
          inst->sdN = srcBuf->chN;
          inst->sdA = mem::allocZ<spec_dist_t*>( inst->sdN );  

          const fd_sample_t* magV[ srcBuf->chN ];
          const fd_sample_t* phsV[ srcBuf->chN ];
          const fd_sample_t*  hzV[ srcBuf->chN ];

          //if((rc = var_register(proc, kAnyChIdx, kInPId, "in")) != kOkRC )
          //  goto errLabel;
        
          // create a spec_dist object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            if((rc = create( inst->sdA[i], srcBuf->binN_V[i] )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The 'spec dist' object create failed on the instance '%s'.",proc->label);
              goto errLabel;
            }

            // setup the output buffer pointers
            magV[i] = inst->sdA[i]->outMagV;
            phsV[i] = inst->sdA[i]->outPhsV;
            hzV[i]  = nullptr;

            spec_dist_t* sd = inst->sdA[i];

            if((rc = var_register_and_get( proc, i,
                                           kBypassPId,   "bypass",   kBaseSfxId, sd->bypassFl,
                                           kCeilingPId,  "ceiling",  kBaseSfxId, sd->ceiling,
                                           kExpoPId,     "expo",     kBaseSfxId, sd->expo,
                                           kThreshPId,   "thresh",   kBaseSfxId, sd->thresh,
                                           kUprSlopePId, "upr",      kBaseSfxId, sd->uprSlope,
                                           kLwrSlopePId, "lwr",      kBaseSfxId, sd->lwrSlope,
                                           kMixPId,      "mix",      kBaseSfxId, sd->mix )) != kOkRC )
            {
              goto errLabel;
            }
                
          }
          
          // create the output buffer
          if((rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->maxBinN_V, srcBuf->binN_V, srcBuf->hopSmpN_V, magV, phsV, hzV )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->sdN; ++i)
          destroy(inst->sdA[i]);
        
        mem::release(inst->sdA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;

        if( var->chIdx != kAnyChIdx && var->chIdx < inst->sdN )
        {
          double val = 0;
          spec_dist_t* sd = inst->sdA[ var->chIdx ];
          
          
          switch( var->vid )
          {
            case kBypassPId:   rc = var_get( var, val ); sd->bypassFl = val; break;
            case kCeilingPId:  rc = var_get( var, val ); sd->ceiling = val; break;
            case kExpoPId:     rc = var_get( var, val ); sd->expo = val;    break;
            case kThreshPId:   rc = var_get( var, val ); sd->thresh = val;  break;
            case kUprSlopePId: rc = var_get( var, val ); sd->uprSlope = val; break;
            case kLwrSlopePId: rc = var_get( var, val ); sd->lwrSlope = val; break;
            case kMixPId:      rc = var_get( var, val ); sd->mix = val;     break;
            default:
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, proc->label );
          }

          //printf("%i sd: ceil:%f expo:%f thresh:%f upr:%f lwr:%f mix:%f : rc:%i val:%f var:%s \n",
          //       var->chIdx,sd->ceiling, sd->expo, sd->thresh, sd->uprSlope, sd->lwrSlope, sd->mix, rc, val, var->label );
        }
        
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const fbuf_t* srcBuf = nullptr;
        fbuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->sdN);
                
        for(unsigned i=0; i<chN; ++i)
        {
          dstBuf->readyFlV[i] = false;
          if( srcBuf->readyFlV[i] )
          {          
            dsp::spec_dist::exec( inst->sdA[i], srcBuf->magV[i], srcBuf->phsV[i], srcBuf->binN_V[i] );

            dstBuf->readyFlV[i] = true;
            //If == 0 )
            //  printf("%f %f\n", vop::sum(srcBuf->magV[i],srcBuf->binN), vop::sum(dstBuf->magV[i], dstBuf->binN) );
          }
        }

      errLabel:
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = nullptr
      };      
    }

    //------------------------------------------------------------------------------------------------------------------
    //
    // Compressor
    //
    namespace compressor
    {

      enum
      {       
        kInPId,
        kBypassPId,
        kInGainPId,
        kThreshPId,
        kRatioPId,
        kAtkMsPId,
        kRlsMsPId,
        kWndMsPId,
        kMaxWndMsPId,
        kOutGainPId,
        kOutPId,
        kEnvPId
      };


      typedef dsp::compressor::obj_t compressor_t;
      
      typedef struct
      {
        compressor_t** cmpA;
        unsigned       cmpN;
      } inst_t;
    

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",proc->label);
          goto errLabel;
        }
        else
        {
          // allocate pv channel array
          inst->cmpN = srcBuf->chN;
          inst->cmpA = mem::allocZ<compressor_t*>( inst->cmpN );  
        
          // create a compressor object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            coeff_t igain, thresh, ratio, ogain;
            ftime_t maxWnd_ms, wnd_ms, atk_ms, rls_ms;
            bool bypassFl;


            // get the compressor variable values
            if((rc = var_register_and_get( proc, i,
                                           kBypassPId,   "bypass",    kBaseSfxId, bypassFl,
                                           kInGainPId,   "igain",     kBaseSfxId, igain,
                                           kThreshPId,   "thresh",    kBaseSfxId, thresh,
                                           kRatioPId,    "ratio",     kBaseSfxId, ratio,
                                           kAtkMsPId,    "atk_ms",    kBaseSfxId, atk_ms,
                                           kRlsMsPId,    "rls_ms",    kBaseSfxId, rls_ms,
                                           kWndMsPId,    "wnd_ms",    kBaseSfxId, wnd_ms,
                                           kMaxWndMsPId, "maxWnd_ms", kBaseSfxId, maxWnd_ms,
                                           kOutGainPId,  "ogain",     kBaseSfxId, ogain )) != kOkRC )
            {
              goto errLabel;
            }

            // create the compressor instance
            if((rc = dsp::compressor::create( inst->cmpA[i], srcBuf->srate, srcBuf->frameN, igain, maxWnd_ms, wnd_ms, thresh, ratio, atk_ms, rls_ms, ogain, bypassFl)) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The 'compressor' object create failed on the instance '%s'.",proc->label);
              goto errLabel;
            }
                
          }
          
          // create the output audio buffer
          if((rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->cmpN; ++i)
          destroy(inst->cmpA[i]);
        
        mem::release(inst->cmpA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;
        ftime_t  tmp;

        if( var->chIdx != kAnyChIdx && var->chIdx < inst->cmpN )
        {
          compressor_t* c = inst->cmpA[ var->chIdx ];
          
          switch( var->vid )
          {
            case kBypassPId:   rc = var_get( var, tmp ); c->bypassFl=tmp; break;
            case kInGainPId:   rc = var_get( var, tmp ); c->inGain=tmp;   break;
            case kOutGainPId:  rc = var_get( var, tmp ); c->outGain=tmp;  break;
            case kRatioPId:    rc = var_get( var, tmp ); c->ratio_num=tmp; break;
            case kThreshPId:   rc = var_get( var, tmp ); c->threshDb=tmp; break;
            case kAtkMsPId:    rc = var_get( var, tmp ); dsp::compressor::set_attack_ms(c,  tmp ); break;
            case kRlsMsPId:    rc = var_get( var, tmp ); dsp::compressor::set_release_ms(c, tmp ); break;
            case kWndMsPId:    rc = var_get( var, tmp ); dsp::compressor::set_rms_wnd_ms(c, tmp ); break;
            case kMaxWndMsPId: break;
            default:
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, proc->label );
          }
          //printf("cmp byp:%i igain:%f ogain:%f rat:%f thresh:%f atk:%i rls:%i wnd:%i : rc:%i val:%f\n",
          //       c->bypassFl, c->inGain, c->outGain,c->ratio_num,c->threshDb,c->atkSmp,c->rlsSmp,c->rmsWndCnt,rc,tmp);
        }
        
        
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const abuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->cmpN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          dsp::compressor::exec( inst->cmpA[i], srcBuf->buf + i*srcBuf->frameN, dstBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
        }

        
        

      errLabel:
        return rc;
      }

      rc_t report( proc_t* proc )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->cmpN; ++i)
        {
          compressor_t* c = inst->cmpA[i];
          cwLogInfo("%s ch:%i : sr:%f bypass:%i procSmpN:%i igain:%f threshdb:%f ratio:%f atkSmp:%i rlsSmp:%i ogain:%f rmsWndN:%i maxRmsWndN%i",
                    proc->label,i,c->srate,c->bypassFl,c->procSmpCnt,c->inGain,c->threshDb,c->ratio_num,c->atkSmp,c->rlsSmp,c->outGain,c->rmsWndCnt,c->rmsWndAllocCnt
                    );
        }
        
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = report
      };      
    }


    //------------------------------------------------------------------------------------------------------------------
    //
    // Limiter
    //
    namespace limiter
    {

      enum
      {       
        kInPId,
        kBypassPId,
        kInGainPId,
        kThreshPId,
        kOutGainPId,
        kOutPId,
      };


      typedef dsp::limiter::obj_t limiter_t;
      
      typedef struct
      {
        limiter_t** limA;
        unsigned    limN;
      } inst_t;
    

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",proc->label);
          goto errLabel;
        }
        else
        {
          // allocate pv channel array
          inst->limN = srcBuf->chN;
          inst->limA = mem::allocZ<limiter_t*>( inst->limN );  
        
          // create a limiter object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            coeff_t igain, thresh, ogain;
            bool bypassFl;


            // get the limiter variable values
            if((rc = var_register_and_get( proc, i,
                                           kBypassPId,   "bypass",    kBaseSfxId, bypassFl,
                                           kInGainPId,   "igain",     kBaseSfxId, igain,
                                           kThreshPId,   "thresh",    kBaseSfxId, thresh,
                                           kOutGainPId,  "ogain",     kBaseSfxId, ogain )) != kOkRC )
            {
              goto errLabel;
            }

            // create the limiter instance
            if((rc = dsp::limiter::create( inst->limA[i], srcBuf->srate, srcBuf->frameN, igain, thresh, ogain, bypassFl)) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The 'limiter' object create failed on the instance '%s'.",proc->label);
              goto errLabel;
            }
                
          }
          
          // create the output audio buffer
          if((rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->limN; ++i)
          destroy(inst->limA[i]);
        
        mem::release(inst->limA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;
        coeff_t  rtmp;
        bool btmp;

        if( var->chIdx != kAnyChIdx && var->chIdx < inst->limN )
        {
          limiter_t* c = inst->limA[ var->chIdx ];
          
          switch( var->vid )
          {
            case kBypassPId:   rc = var_get( var, btmp ); c->bypassFl=btmp; break;
            case kInGainPId:   rc = var_get( var, rtmp ); c->igain=rtmp;   break;
            case kOutGainPId:  rc = var_get( var, rtmp ); c->ogain=rtmp;  break;
            case kThreshPId:   rc = var_get( var, rtmp ); c->thresh=rtmp; break;
            default:
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, proc->label );
          }
          //printf("lim byp:%i igain:%f ogain:%f rat:%f thresh:%f atk:%i rls:%i wnd:%i : rc:%i val:%f\n",
          //       c->bypassFl, c->inGain, c->outGain,c->ratio_num,c->threshDb,c->atkSmp,c->rlsSmp,c->rmsWndCnt,rc,tmp);
        }
        
        
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const abuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->limN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          dsp::limiter::exec( inst->limA[i], srcBuf->buf + i*srcBuf->frameN, dstBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
        }

        
        

      errLabel:
        return rc;
      }

      rc_t report( proc_t* proc )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->limN; ++i)
        {
          limiter_t* c = inst->limA[i];
          cwLogInfo("%s ch:%i : bypass:%i procSmpN:%i igain:%f threshdb:%f  ogain:%f",
                    proc->label,i,c->bypassFl,c->procSmpCnt,c->igain,c->thresh,c->ogain );
        }
        
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = report
      };      
    }
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_delay
    //
    namespace audio_delay
    {
      enum
      {
        kInPId,
        kMaxDelayMsPId,
        kDelayMsPId,
        kOutPId
      };

      typedef struct inst_str
      {
        abuf_t*   delayBuf;       // delayBuf->buf[ maxDelayFrameN ]
        unsigned  maxDelayFrameN; // length of the delay 
        unsigned* cntV;           // cntV[ chN ] per channel delay
        unsigned* idxV;           // idxV[ chN ] per channel i/o idx
      } inst_t;

      rc_t create( proc_t* proc )
      {
        rc_t          rc         = kOkRC;
        const abuf_t* abuf       = nullptr; //
        inst_t*       inst       = mem::allocZ<inst_t>();
        ftime_t        delayMs    = 0;
        ftime_t        maxDelayMs = 0;

        proc->userPtr = inst;

        // get the source audio buffer
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;


        inst->cntV = mem::allocZ<unsigned>(abuf->chN);
        inst->idxV = mem::allocZ<unsigned>(abuf->chN);
        
        // register the gain 
        for(unsigned i=0; i<abuf->chN; ++i)
        {
          if((rc = var_register_and_get( proc, i,
                                         kMaxDelayMsPId, "maxDelayMs", kBaseSfxId, maxDelayMs,
                                         kDelayMsPId,    "delayMs",    kBaseSfxId, delayMs)) != kOkRC )
          {
            goto errLabel;
          }

          if( delayMs > maxDelayMs )
          {
            cwLogWarning("'delayMs' (%i) is being reduced to 'maxDelayMs' (%i) on the delay instance:%s.",delayMs,maxDelayMs,proc->label);
            delayMs = maxDelayMs;
          }

          inst->maxDelayFrameN = std::max(inst->maxDelayFrameN, (unsigned)(fabs(maxDelayMs) * abuf->srate / 1000.0) );

          inst->cntV[i] = (unsigned)(fabs(delayMs) * abuf->srate / 1000.0);
                                  
        }

        inst->delayBuf = abuf_create( abuf->srate, abuf->chN, inst->maxDelayFrameN );
        
        // create the output audio buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );


      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        inst_t* inst = (inst_t*)proc->userPtr;

        mem::release(inst->cntV);
        mem::release(inst->idxV);
        abuf_destroy(inst->delayBuf);
        mem::release(inst);
        
        return kOkRC;
      }

      rc_t _update_delay( proc_t* proc, variable_t* var )
      {
        rc_t     rc          = kOkRC;
        inst_t*  inst        = (inst_t*)proc->userPtr;
        abuf_t*  ibuf        = nullptr;
        ftime_t   delayMs     = 0;
        unsigned delayFrameN = 0;
        
        if((rc = var_get(proc,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        if((rc = var_get( var, delayMs )) != kOkRC )
          goto errLabel;
        
        delayFrameN = (unsigned)(fabs(delayMs) * ibuf->srate / 1000.0);

        if( delayFrameN > inst->maxDelayFrameN )
        {
          delayFrameN = inst->maxDelayFrameN;
          cwLogWarning("The audio delay length is limited to %i milliseconds.", (int)((delayFrameN * 1000) / ibuf->srate));
        }
        
        vop::zero(inst->delayBuf->buf,inst->delayBuf->chN*inst->delayBuf->frameN);
        
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          inst->cntV[i] = delayFrameN;
          inst->idxV[i] = 0;          
        }

      errLabel:
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        
        switch( var->vid )
        {
          case kDelayMsPId:
            rc = _update_delay(proc,var);
            break;
        }

        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc   = kOkRC;
        inst_t*       inst = (inst_t*)proc->userPtr;
        const abuf_t* ibuf = nullptr;
        abuf_t*       obuf = nullptr;
        abuf_t*       dbuf = inst->delayBuf;

        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        // for each channel
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          sample_t* isig = ibuf->buf + i*ibuf->frameN;
          sample_t* osig = obuf->buf + i*obuf->frameN;
          sample_t* dsig = dbuf->buf + i*dbuf->frameN;
          unsigned    di = inst->idxV[i];

          // if the delay is set to zero samples
          if( inst->cntV[i] == 0 )
            memcpy(osig,isig,ibuf->frameN * sizeof(sample_t));
          else 
          {
            // otherwise the delay is non-zero positive sample count
            for(unsigned j=0; j<ibuf->frameN; ++j)
            {
              osig[j]  = dsig[di]; // read delay output
              dsig[di] = isig[j];  // set delay input
              di = (di+1) % inst->cntV[i];       // update the delay index
            }
          }

          // store the delay index for the next cycle
          inst->idxV[i] = di; 
        }  
        
      errLabel:
        return rc;
      }

      
      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = nullptr
      };
      
    }

    //------------------------------------------------------------------------------------------------------------------
    //
    // DC Filter
    //
    namespace dc_filter
    {

      enum
      {       
        kInPId,
        kBypassPId,
        kGainPId,
        kOutPId,
      };


      typedef dsp::dc_filter::obj_t dc_filter_t;
      
      typedef struct
      {
        dc_filter_t** dcfA;
        unsigned    dcfN;
      } inst_t;
    

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",proc->label);
          goto errLabel;
        }
        else
        {
          // allocate channel array
          inst->dcfN = srcBuf->chN;
          inst->dcfA = mem::allocZ<dc_filter_t*>( inst->dcfN );  
        
          // create a dc_filter object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            coeff_t gain;
            bool bypassFl;


            // get the dc_filter variable values
            if((rc = var_register_and_get( proc, i,
                                           kBypassPId,   "bypass",    kBaseSfxId, bypassFl,
                                           kGainPId,     "gain",      kBaseSfxId, gain )) != kOkRC )
            {
              goto errLabel;
            }

            // create the dc_filter instance
            if((rc = dsp::dc_filter::create( inst->dcfA[i], srcBuf->srate, srcBuf->frameN, gain, bypassFl)) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The 'dc_filter' object create failed on the instance '%s'.",proc->label);
              goto errLabel;
            }
                
          }
          
          // create the output audio buffer
          if((rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->dcfN; ++i)
          destroy(inst->dcfA[i]);
        
        mem::release(inst->dcfA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( proc_t* proc, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const abuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->dcfN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          coeff_t gain   = val_get<coeff_t>( proc, kGainPId,   i );
          bool bypassFl = val_get<bool>(   proc, kBypassPId, i );

          dsp::dc_filter::set( inst->dcfA[i], gain, bypassFl );
          
          dsp::dc_filter::exec( inst->dcfA[i], srcBuf->buf + i*srcBuf->frameN, dstBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
        }

      errLabel:
        return rc;
      }

      rc_t report( proc_t* proc )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->dcfN; ++i)
        {
          dc_filter_t* c = inst->dcfA[i];
          cwLogInfo("%s ch:%i : bypass:%i gain:%f",
                    proc->label,i,c->bypassFl,c->gain );
        }
        
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = report
      };      
    }
    
 
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_meter
    //
    namespace audio_meter
    {

      enum
      {       
        kInPId,
        kDbFlPId,
        kWndMsPId,
        kPeakDbPId,
        kOutPId,
        kPeakFlPId,
        kClipFlPId
      };


      typedef dsp::audio_meter::obj_t audio_meter_t;
      
      typedef struct
      {
        audio_meter_t** mtrA;
        unsigned    mtrN;
      } inst_t;
    

      rc_t create( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        proc->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(proc, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",proc->label);
          goto errLabel;
        }
        else
        {
          // allocate channel array
          inst->mtrN = srcBuf->chN;
          inst->mtrA = mem::allocZ<audio_meter_t*>( inst->mtrN );  
        
          // create a audio_meter object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            ftime_t wndMs;
            coeff_t peakThreshDb;
            bool dbFl;
	    
            // get the audio_meter variable values
            if((rc = var_register_and_get( proc, i,
                                           kDbFlPId,   "dbFl",    kBaseSfxId, dbFl,
                                           kWndMsPId, "wndMs",    kBaseSfxId, wndMs,
                                           kPeakDbPId, "peakDb",  kBaseSfxId, peakThreshDb )) != kOkRC )
            {
              goto errLabel;
            }

            // get the audio_meter variable values
            if((rc = var_register( proc, i,
                                   kOutPId,   "out",     kBaseSfxId,
                                   kPeakFlPId, "peakFl", kBaseSfxId, 
                                   kClipFlPId, "clipFl", kBaseSfxId )) != kOkRC )
            {
              goto errLabel;
            }
	    
            unsigned maxWndMs = std::max(wndMs,1000.0);
	    
            // create the audio_meter instance
            if((rc = dsp::audio_meter::create( inst->mtrA[i], srcBuf->srate, maxWndMs, wndMs, peakThreshDb)) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The 'audio_meter' object create failed on the instance '%s'.",proc->label);
              goto errLabel;
            }
                
          }
          
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->mtrN; ++i)
          destroy(inst->mtrA[i]);
        
        mem::release(inst->mtrA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( proc_t* proc, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)proc->userPtr;
        const abuf_t* srcBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->mtrN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          dsp::audio_meter::exec( inst->mtrA[i], srcBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
          var_set(proc, kOutPId,    i, inst->mtrA[i]->outDb  );
          var_set(proc, kPeakFlPId, i, inst->mtrA[i]->peakFl );
          var_set(proc, kClipFlPId, i, inst->mtrA[i]->clipFl );
        }

      errLabel:
        return rc;
      }

      rc_t report( proc_t* proc )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<inst->mtrN; ++i)
        {
          audio_meter_t* c = inst->mtrA[i];
          cwLogInfo("%s ch:%i : %f %f db : pk:%i %i clip:%i %i ",
                    proc->label,i,c->outLin,c->outDb,c->peakFl,c->peakCnt,c->clipFl,c->clipCnt );
        }
        
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec,
        .report  = report
      };      
    }

    //------------------------------------------------------------------------------------------------------------------
    //
    // xfade_ctl
    //
    namespace xfade_ctl
    {
      enum {
        kNetLabelPId,
        kNetLabelSfxPId,
        kSrateRefPId,
        kDurMsPId,
        kTriggerPId,
        kPresetPId,
        kGainPId,
      };

      typedef struct poly_ch_str
      {
        network_t net;
        coeff_t   target_gain;
        coeff_t   cur_gain;
      } poly_ch_t;
      
      typedef struct
      {
        unsigned    xfadeDurMs;       // crossfade duration in milliseconds
        proc_t*     net_proc;         // source 'poly' network
        poly_ch_t*  netA;             // netA[ poly_ch_cnt ] internal proxy network 
        unsigned    poly_ch_cnt;      // count of poly channels in net_proc
        unsigned    net_proc_cnt;     // count of proc's in a single poly-channel (net_proc->proc_arrayN/poly_cnt)
        unsigned    cur_poly_ch_idx;  // This is the active channel.
        unsigned    next_poly_ch_idx; // This is the next channel that will be active.
        srate_t     srate;            // Sample rate used for time base
        bool        preset_delta_fl;  // Preset change trigger flag.
        bool        trigFl;           // Cross-fade trigger flag.
      } inst_t;

      void _trigger_xfade( inst_t* p )
      {
        // begin fading out the cur channel
        p->netA[p->cur_poly_ch_idx].target_gain = 0;
        
        // the next poly-ch become the cur poly-ch
        p->cur_poly_ch_idx  = p->next_poly_ch_idx;

        // the next poly-ch advances
        p->next_poly_ch_idx = p->next_poly_ch_idx+1 >= p->poly_ch_cnt ? 0 : p->next_poly_ch_idx+1;
        
        // begin fading in the new cur channel
        p->netA[p->cur_poly_ch_idx].target_gain = 1;

        // if the next channel is not already at 0 send it in that direction
        p->netA[p->next_poly_ch_idx].target_gain = 0;

        //printf("xfad:%i %i : %i\n",p->cur_poly_ch_idx, p->next_poly_ch_idx,p->poly_ch_cnt);

      }

      rc_t create( proc_t* proc )
      {
        rc_t        rc            = kOkRC;
        const char* netLabel      = nullptr;
        const char* presetLabel   = nullptr;
        unsigned    netLabelSfxId = kBaseSfxId;
        abuf_t*     srateSrc      = nullptr;
        coeff_t     dum_dbl;

        inst_t* p = mem::allocZ<inst_t>();

        proc->userPtr = p;

        if((rc = var_register(proc,kAnyChIdx,kTriggerPId,"trigger", kBaseSfxId )) != kOkRC )
          goto errLabel;
                
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kNetLabelPId,    "net",       kBaseSfxId, netLabel,
                                      kNetLabelSfxPId, "netSfxId",  kBaseSfxId, netLabelSfxId,
                                      kSrateRefPId,    "srateSrc",  kBaseSfxId, srateSrc,
                                      kDurMsPId,       "durMs",     kBaseSfxId, p->xfadeDurMs,
                                      kPresetPId,      "preset",    kBaseSfxId, presetLabel,
                                      kGainPId,        "gain",      kBaseSfxId, dum_dbl)) != kOkRC )
        {
          goto errLabel; 
        }

        // locate the source poly-network for this xfad-ctl
        if((rc = proc_find(*proc->net,netLabel,netLabelSfxId,p->net_proc)) != kOkRC )
        {
          cwLogError(rc,"The xfade_ctl source network proc instance '%s:%i' was not found.",cwStringNullGuard(netLabel),netLabelSfxId);
          goto errLabel;
        }

        if( p->net_proc->internal_net->poly_cnt < 3 )
        {
          cwLogError(rc,"The xfade_ctl source network must have at least 3 poly channels. %i < 3",p->net_proc->internal_net->poly_cnt);
          goto errLabel;
        }

        p->poly_ch_cnt = p->net_proc->internal_net->poly_cnt;

        // create the gain output variables - one output for each poly-channel
        for(unsigned i=1; i<p->poly_ch_cnt; ++i)
        {
          variable_t* dum;
          if((rc = var_create(proc, "gain", i, kGainPId+i, kAnyChIdx, nullptr, kInvalidTFl, dum )) != kOkRC )
          {
            cwLogError(rc,"'gain:%i' create failed.",i);
            goto errLabel;
          }
        }

        // count of proc's in one poly-ch of the poly network
        p->net_proc_cnt = p->net_proc->internal_net->proc_arrayN / p->net_proc->internal_net->poly_cnt;

        p->netA = mem::allocZ<poly_ch_t>(p->poly_ch_cnt);
        
        // create the proxy network networks
        for(unsigned i=0; i<p->poly_ch_cnt; ++i)
        {
          p->netA[i].net.proc_arrayAllocN = p->net_proc_cnt;
          p->netA[i].net.proc_arrayN      = p->netA[i].net.proc_arrayAllocN;
          p->netA[i].net.proc_array       = mem::allocZ<proc_t*>(p->netA[i].net.proc_arrayAllocN);
          p->netA[i].net.presetsCfg       = p->net_proc->internal_net->presetsCfg;

          p->netA[i].net.presetA          = p->net_proc->internal_net->presetA;
          p->netA[i].net.presetN          = p->net_proc->internal_net->presetN;
          
          p->netA[i].net.preset_pairA     = p->net_proc->internal_net->preset_pairA;
          p->netA[i].net.preset_pairN     = p->net_proc->internal_net->preset_pairN;

          for(unsigned j=0,k=0; j<p->net_proc->internal_net->proc_arrayN; ++j)
            if( p->net_proc->internal_net->proc_array[j]->label_sfx_id == i )
            {
              assert( k < p->net_proc_cnt );
              p->netA[i].net.proc_array[k++] = p->net_proc->internal_net->proc_array[j];
            }          
        }

        if( srateSrc == nullptr )
          p->srate = proc->ctx->sample_rate;
        else
          p->srate = srateSrc->srate;


        // setup the channels such that the first active channel after _trigger_xfade()
        // will be channel 0
        p->cur_poly_ch_idx  = 1;
        p->next_poly_ch_idx = 2;
        _trigger_xfade(p);  // cur=2 nxt=0 initialize inst ptrs in range: p->net[0:net_proc_cnt]
        _trigger_xfade(p);  // cur=0 nxt=1 initialize inst ptrs in range: p->net[net_proc_cnt:2*net_proc_cnt]
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        inst_t* p = (inst_t*)proc->userPtr;
        for(unsigned i=0; i<p->poly_ch_cnt; ++i)
          mem::release(p->netA[i].net.proc_array);
        
        mem::release(p->netA);
        mem::release(proc->userPtr);
        
        return kOkRC;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t    rc = kOkRC;
        inst_t* p  = (inst_t*)proc->userPtr;

        switch( var->vid )
        {
          case kTriggerPId:
            p->trigFl = true;
            break;

          case kPresetPId:
            p->preset_delta_fl = true;
            break;
        }
        
        return rc;
      }

      // return sign of expression as a float
      float _signum( float v ) { return (0.0f < v) - (v < 0.0f); }
      
      rc_t exec( proc_t* proc )
      {
        rc_t    rc     = kOkRC;
        inst_t* p      = (inst_t*)proc->userPtr;

        // time in sample frames to complete a xfade
        double xfade_dur_smp        = p->xfadeDurMs * p->srate / 1000.0;

        // fraction of a xfade which will be completed in on exec() cycle
        float delta_gain_per_cycle =  (float)(proc->ctx->framesPerCycle / xfade_dur_smp);


        if( p->preset_delta_fl )
        {
          const char* preset_label = nullptr;
          
          p->preset_delta_fl = false;

          if((rc = var_get(proc,kPresetPId,kAnyChIdx,preset_label)) != kOkRC )
          {
            rc = cwLogError(rc,"Preset label access failed.");
            goto errLabel;
          }
          
          if((rc = network_apply_preset(p->netA[p->next_poly_ch_idx].net, preset_label,p->next_poly_ch_idx)) != kOkRC )
          {
            rc = cwLogError(rc,"Appy preset '%s' failed.",cwStringNullGuard(preset_label));
            goto errLabel;
          }
        }
        
        // check if a cross-fade has been triggered
        if(p->trigFl )
        {
          p->trigFl = false;
          _trigger_xfade(p);
        }

        // update the cross-fade gain outputs
        for(unsigned i=0; i<p->net_proc->internal_net->poly_cnt; ++i)
        {
          p->netA[i].cur_gain += _signum(p->netA[i].target_gain - p->netA[i].cur_gain) * delta_gain_per_cycle;
          
          p->netA[i].cur_gain = std::min(1.0f, std::max(0.0f, p->netA[i].cur_gain));
          
          var_set(proc,kGainPId+i,kAnyChIdx,p->netA[i].cur_gain);
        }
        
        

        
      errLabel:
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }    


    //------------------------------------------------------------------------------------------------------------------
    //
    // Poly Voice Control
    //
    namespace poly_voice_ctl
    {
      enum {
        kInPId,
        kVoiceCntPId,
        kBaseOutPId,
      };

      enum {
        kVoiceMsgN  = 16,
        kGlobalMsgN = 256,
      };
      
      typedef struct voice_str
      {
        bool            activeFl; // true if this voice is currently active
        unsigned        pitch;    // pitch associated with this voice
        unsigned        age;      // age of this voice in exec() cycles.
        
        midi::ch_msg_t* msgA;     // msgA[ msgN ] msg buffer for this voice 
        unsigned        msgN;     //
        unsigned        msg_idx;  // current count of msg's in msgA[]

        mbuf_t*         mbuf;      // cached mbuf for this output variable
        
      } voice_t;
      
      typedef struct
      {
        unsigned baseDoneFlPId;
        
        unsigned voiceN;   // voiceA[ voiceN ]
        voice_t* voiceA; 

        // sizeof of each voice msgA[] (same as voice_t.msgN)
        unsigned voiceMsgN;

      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc            = kOkRC;
        mbuf_t*     mbuf          = nullptr;

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kInPId,       "in",    kBaseSfxId, mbuf,
                                      kVoiceCntPId, "voice_cnt", kBaseSfxId, p->voiceN)) != kOkRC )
        {
          goto errLabel; 
        }

        if( p->voiceN == 0 )
        {
          rc = cwLogError(kInvalidArgRC,"The poly_voice_ctl '%s:%i' has 0 voices.",proc->label,proc->label_sfx_id );
          goto errLabel;
        }

        p->baseDoneFlPId = kBaseOutPId + p->voiceN;
        p->voiceMsgN     = kVoiceMsgN;
        p->voiceA        = mem::allocZ<voice_t>(p->voiceN);
        
        for(unsigned i=0; i<p->voiceN; ++i)
        {
          // create one output MIDI variable per voice
          if((rc = var_register_and_set( proc, "out", i, kBaseOutPId + i, kAnyChIdx, nullptr, 0  )) != kOkRC )
            goto errLabel;

          // create one 'done_fl' variable per voice
          if((rc = var_register_and_set( proc, kAnyChIdx, p->baseDoneFlPId + i, "done_fl", i, false  )) != kOkRC )
            goto errLabel;

          p->voiceA[i].msgA = mem::allocZ<midi::ch_msg_t>(p->voiceMsgN);
          p->voiceA[i].msgN = p->voiceMsgN;

          // cache a pointer to each output variables mbuf (because we know these won't change)
          if((rc = var_get(proc,kBaseOutPId+i, kAnyChIdx, p->voiceA[i].mbuf )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        for(unsigned i=0; i<p->voiceN; ++i)
          mem::release(p->voiceA[i].msgA);
        
        mem::release( p->voiceA );
        p->voiceN = 0;
        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        if( p->baseDoneFlPId <= var->vid && var->vid < p->baseDoneFlPId + p->voiceN )
        {          
          p->voiceA[ var->vid - p->baseDoneFlPId ].activeFl = false;
        }
        
        return rc;
      }

      unsigned _get_next_avail_voice( inst_t* p )
      {
        unsigned max_age_idx = 0;
        for(unsigned i=0; i<p->voiceN; ++i)
        {
          if( p->voiceA[i].activeFl == false )
            return i;
          
          if( p->voiceA[i].age > p->voiceA[ max_age_idx].age )
            max_age_idx = i;          
        }
        
        return max_age_idx;
      }

      unsigned _pitch_to_voice( inst_t* p, unsigned pitch )
      {
        for(unsigned i=0; i<p->voiceN; ++i)
          if( p->voiceA[i].activeFl && p->voiceA[i].pitch == pitch )
            return i;
        return kInvalidIdx;
      }

      rc_t _update_voice_msg( proc_t* proc, inst_t* p, unsigned voice_idx, const midi::ch_msg_t* m )
      {
        rc_t     rc   = kOkRC;
        voice_t* v    = p->voiceA + voice_idx;
        
        if( v->msg_idx >= v->msgN )
        {
          cwLogError(kBufTooSmallRC,"The voice MIDI buffer on ch:%i is full on '%s:%i'",voice_idx,cwStringNullGuard(proc->label),proc->label_sfx_id);
          goto errLabel;
        }
        else
        {
          v->msgA[ v->msg_idx++ ] = *m;
          v->mbuf->msgA = v->msgA;
          v->mbuf->msgN = v->msg_idx;

          //printf("vctl:%i : st:%i %i %i\n",voice_idx,m->status,m->d0,m->d1);
        }
        
      errLabel:
        return rc;
      }

      rc_t _on_note_on( proc_t* proc, inst_t* p, const midi::ch_msg_t* m  )
      {
        rc_t     rc         = kOkRC;
        unsigned voice_idx  = _get_next_avail_voice(p);

        assert( voice_idx <= p->voiceN);

        voice_t* v = p->voiceA + voice_idx;

        v->age      = 0;
        v->activeFl = true;
        v->pitch    = m->d0;

        rc = _update_voice_msg(proc,p,voice_idx,m);        
        
        return rc;
      }
            
      rc_t _on_note_off( proc_t* proc, inst_t* p, const midi::ch_msg_t* m )
      {
        rc_t     rc   = kOkRC;
        unsigned voice_idx;
        if((voice_idx = _pitch_to_voice(p,m->d0)) == kInvalidIdx )
        {
          cwLogWarning("Voice not found for note:%i.",m->d0);
          goto errLabel;
        }

        assert( voice_idx <= p->voiceN);

        rc = _update_voice_msg(proc,p,voice_idx,m);

      errLabel:
        return rc;
      }

      rc_t _send_to_all_voices( proc_t* proc, inst_t*p, const midi::ch_msg_t* m )
      {
        rc_t rc = kOkRC;

        if( midi::isChStatus( m->status ) )
          for(unsigned i=0; i<p->voiceN; ++i)
            if((rc = _update_voice_msg(proc,p,i,m)) != kOkRC )
              goto errLabel;

      errLabel:
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;
        mbuf_t* mbuf = nullptr;

        // update the voice array
        for(unsigned i=0; i<p->voiceN; ++i)
        { 
          if( p->voiceA[i].activeFl )
            p->voiceA[i].age    += 1;
          
          p->voiceA[i].msg_idx    = 0;
          p->voiceA[i].mbuf->msgN = 0;
          p->voiceA[i].mbuf->msgA = nullptr;
        }
        
        // get the input MIDI buffer
        if((rc = var_get(proc,kInPId,kAnyChIdx,mbuf)) != kOkRC )
          goto errLabel;
        
        // process the incoming MIDI messages
        for(unsigned i=0; i<mbuf->msgN; ++i)
        {
          const midi::ch_msg_t* m = mbuf->msgA + i;
          
          switch( m->status )
          {
            case midi::kNoteOnMdId:
              if( m->d1 == 0 )
                rc = _on_note_off(proc,p,m);
              else
                rc = _on_note_on(proc,p,m);                  
              break;

            case midi::kNoteOffMdId:
              rc = _on_note_off(proc,p,m);
              break;
              
            default:
              rc = _send_to_all_voices(proc,p,m);
              break;
          }
        }

        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // midi_voice
    //
    namespace midi_voice
    {
      enum {
        kInPId,
        kOutPId,
        kDoneFlPId
      };
      
      typedef struct
      {

        unsigned  wtAllocN;  // wtAlloc[ wtAllocN ]  
        sample_t* wtAllocA;  // total allocated WT space with extra leading and trailing samples
        
        unsigned  wtN;       // wtA[ wtA ] 
        sample_t* wtA;       // actual WT space which sits inside of wtAllocA[] 

        double    wtPhase;   // current WT phase

        unsigned cur_vel;    // current MIDI velocity 
        double   cur_hz;     // current fund. frequency
        double   cur_pbend;  // current pitch bend factor
        
        unsigned hzN;
        double*  hzA;        // hzA[128] - midi to Hz lookup table. 
        
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t           rc      = kOkRC;
        mbuf_t*        mbuf    = nullptr;
        srate_t        srate   = proc->ctx->sample_rate;
        const unsigned ch_cnt  = 1;
        bool           done_fl = false;
        
        // get the MIDI input variable
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kInPId,     "in",      kBaseSfxId, mbuf,
                                       kDoneFlPId, "done_fl", kBaseSfxId, done_fl)) != kOkRC )
          goto errLabel;

        // create one output audio buffer
        if((rc = var_register_and_set( proc, "out", kBaseSfxId,kOutPId, kAnyChIdx, srate, ch_cnt, proc->ctx->framesPerCycle )) != kOkRC )
          goto errLabel;
        
        // create the wave table
        p->wtN      = srate;
        p->wtAllocN = p->wtN + 2;
        p->wtAllocA = mem::allocZ<sample_t>(p->wtAllocN);
        p->wtA      = p->wtAllocA + 1;

        vop::sine( p->wtA, p->wtN, srate, 1);
        p->wtAllocA[0]             = p->wtA[p->wtN-1];
        p->wtAllocA[p->wtAllocN-1] = p->wtA[0];

        // create the MIDI pitch to hertz
        p->hzN = midi::kMidiNoteCnt;
        p->hzA = mem::allocZ<double>(p->hzN);
        
        for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
          p->hzA[i] = midi_to_hz(i);

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->wtAllocA);
        mem::release(p->hzA);

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;
        abuf_t* abuf = nullptr;
        mbuf_t* mbuf = nullptr;

        // get the input MIDI buffer
        if((rc = var_get(proc,kInPId,kAnyChIdx,mbuf)) != kOkRC )
          goto errLabel;
        
        // get the output audio buffer
        if((rc = var_get(proc,kOutPId,kAnyChIdx,abuf)) != kOkRC )
          goto errLabel;

        // if there are MIDI messages - update cur_hz and cur_vel
        for(unsigned i=0; i<mbuf->msgN; ++i)
        {
          const midi::ch_msg_t* m = mbuf->msgA + i;
          switch( m->status )
          {
            case midi::kNoteOnMdId:
              p->cur_hz  = p->hzA[ m->d0 ];
              p->cur_vel = m->d1;

              if( m->d1 == 0 )
                var_set(proc,kDoneFlPId,kAnyChIdx,true);              
              break;

            case midi::kNoteOffMdId:
              var_set(proc,kDoneFlPId,kAnyChIdx,true);              
              break;

            case midi::kPbendMdId:
              p->cur_pbend = midi::toPbend(m->d0,m->d1) / 8192.0;
              break;
              
            default:
              break;
          }
        }

        // if the voice is off then zero the audio buffer
        if( p->cur_vel == 0 )
        {
          vop::zero(abuf->buf,abuf->frameN);
        }
        else
        {

          // calculate the gain based on the cur_vel
          coeff_t gain = (coeff_t)p->cur_vel / 127;

          // fill in the audio buffer
          for(unsigned i=0; i<abuf->frameN; ++i)
          {
            unsigned j    = (unsigned)floor(p->wtPhase);
            double   frac = p->wtPhase - j;          
            sample_t smp  = p->wtA[j] + (p->wtA[j+1] - p->wtA[j]) * frac;
          
            abuf->buf[i] = gain*smp;

            p->wtPhase += p->cur_hz + (p->cur_hz * p->cur_pbend);
            if( p->wtPhase >= p->wtN )
              p->wtPhase -= p->wtN;
          }
        }
        
      errLabel:
            return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // piano_voice
    //
    namespace piano_voice
    {
      enum {
        kWtbDirPId,
        kWtbInstrPId,
        kInPId,
        kOutPId,
        kDoneFlPId,
        kTestPitchPId,
      };

      enum {
        kChCnt=2
      };

      typedef struct osc_state_str
      {
        wt_bank::ch_t* ch;
        unsigned seg_idx;   // current seg index in ch->segA[]
        unsigned smp_idx;   // current smp index in ch->segA[ seg_idx ].aV[]
        unsigned seg_cnt;   // count of segments played so far (increases with each segment loop traversail)
      } osc_state_t;
        
      typedef struct
      {
        wt_bank::handle_t*   wtbH_ptr;
        unsigned             wtb_instr_idx;
        const wt_bank::wt_t* wt;

        osc_state_t osc[ kChCnt ];

        coeff_t kGainThreshold;
        coeff_t kSustainGain;
        coeff_t kReleaseGain;
        
        coeff_t       gain;
        coeff_t       gain_coeff;
        bool          done_fl;
        
        unsigned      test_pitch;      // Base test pitch        
        unsigned      test_pitchN;     // Count of valid velocities for test_pitch 
        unsigned*     test_pitch_map;  // test_pitch_map[ test_pitch_N ]
        
      } inst_t;


      void _create_test_pitch_map( inst_t* p )
      {
        p->test_pitchN = 0;
        for(unsigned i=0; i<midi::kMidiVelCnt; ++i)
          if( get_wave_table( *p->wtbH_ptr, 0, p->test_pitch, i ) != nullptr )
            p->test_pitchN += 1;

        p->test_pitch_map = mem::allocZ<unsigned>(p->test_pitchN);
            
        for(unsigned i=0,j=0; i<midi::kMidiVelCnt; ++i)
          if( get_wave_table( *p->wtbH_ptr, p->wtb_instr_idx, p->test_pitch, i ) != nullptr )
          {
            assert( j < p->test_pitchN );
            p->test_pitch_map[j++] = i;
          }
          
      }
      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t               rc            = kOkRC;
        const char*        wtb_dir       = nullptr;
        char*              exp_wtb_dir   = nullptr;
        const char*        wtb_instr     = nullptr;
        mbuf_t*            mbuf          = nullptr;
        bool               done_fl       = false;
        srate_t            srate         = proc->ctx->sample_rate;
        unsigned           padSmpN       = 1;
        const char*        wtb_var_label = "wtb";

        // get the MIDI input variable
        if((rc = var_register_and_get( proc, kAnyChIdx,
                                       kWtbDirPId,    "wtb_dir",   kBaseSfxId, wtb_dir,
                                       kWtbInstrPId,  "wtb_instr", kBaseSfxId, wtb_instr,
                                       kInPId,        "in",        kBaseSfxId, mbuf,
                                       kDoneFlPId,    "done_fl",   kBaseSfxId, done_fl,
                                       kTestPitchPId, "test_pitch",kBaseSfxId, p->test_pitch)) != kOkRC )
        {
          goto errLabel;
        }

        // if the global wave table bank has not yet been created
        if((p->wtbH_ptr = (wt_bank::handle_t*)network_global_var(proc, wtb_var_label )) == nullptr )
        {
          wt_bank::handle_t wtbH;
          
          if((exp_wtb_dir = proc_expand_filename(proc,wtb_dir)) == nullptr )
          {
            rc = cwLogError(kOpFailRC,"The wave-table bank directory expansion failed.");
            goto errLabel;
          }
          
          // create the wave table bank
          if((rc = create( wtbH, exp_wtb_dir, padSmpN )) != kOkRC )
          {
            rc = cwLogError(rc,"The wave table bank global variable creation failed.");
            goto errLabel;
          }

          // store the wave table bank global var
          if((rc = network_global_var_alloc(proc, wtb_var_label, &wtbH, sizeof(wtbH) )) != kOkRC )
          {
            rc = cwLogError(rc,"The wave table bank global variable allocation failed.");
            goto errLabel;
          }

          if((p->wtbH_ptr = (wt_bank::handle_t*)network_global_var(proc, wtb_var_label )) == nullptr )
          {
            rc = cwLogError(rc,"The wave table bank global variable store failed.");
            goto errLabel;
          }

        }

        // create one output audio buffer
        if((rc = var_register_and_set( proc, "out", kBaseSfxId,kOutPId, kAnyChIdx, srate, kChCnt, proc->ctx->framesPerCycle )) != kOkRC )
        {
          goto errLabel;
        }

        if((p->wtb_instr_idx = wt_bank::instr_index( *p->wtbH_ptr, wtb_instr )) == kInvalidIdx )
        {
          rc = cwLogError(rc,"The wave table bank named '%s' could not be found.",cwStringNullGuard(wtb_instr));
          goto errLabel;
        }

        // if we are running in 'test-pitch' mode
        if( p->test_pitch != 0 )
        {
          cwLogInfo("%s is in test-pitch mode",proc->label);
          _create_test_pitch_map(p);
          
        }

        p->done_fl        = true;
        p->kGainThreshold = 0.01;
        p->kSustainGain   = 0.9995;
        p->kReleaseGain   = 0.9;
        

        assert(p->wtbH_ptr != nullptr );

      errLabel:
        mem::release(exp_wtb_dir);
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        if( p->wtbH_ptr )
          destroy(*p->wtbH_ptr);

        mem::release(p->test_pitch_map);

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;
        abuf_t* abuf = nullptr;
        mbuf_t* mbuf = nullptr;

        // get the input MIDI buffer
        if((rc = var_get(proc,kInPId,kAnyChIdx,mbuf)) != kOkRC )
          goto errLabel;
        
        // get the output audio buffer
        if((rc = var_get(proc,kOutPId,kAnyChIdx,abuf)) != kOkRC )
          goto errLabel;

        // if there are MIDI messages - update the wavetable oscillators
        for(unsigned i=0; i<mbuf->msgN; ++i)
        {
          const midi::ch_msg_t* m = mbuf->msgA + i;
          switch( m->status )
          {
            case midi::kNoteOnMdId:

              if( m->d1 > 0 )
              {
                unsigned d0 = m->d0;
                unsigned d1 = m->d1;

                // if in voice test mode
                if( p->test_pitch_map != nullptr )
                {
                  // if the the pitch is in side the test range
                  if( d0 < p->test_pitch || p->test_pitch + p->test_pitchN <= d1  )
                    goto errLabel;

                  // then the pitch is set to the test pitch ...
                  d0 = p->test_pitch;

                  // ... and the velocity is mapped to a vel for which there is a known vel in the wt-bank
                  // Performed pitches above the test pitch trigger increasing velocities.
                  d1 = p->test_pitch_map[ m->d0 - p->test_pitch ];
                }

                // get the wave-table associated with the pitch and velocity
                if((p->wt  = get_wave_table( *p->wtbH_ptr, p->wtb_instr_idx, d0,d1 )) == nullptr )
                {
                  cwLogWarning("No piano voice for pitch:%i vel:%i",m->d0,m->d1);
                  goto errLabel;
                }
                
                p->done_fl    = false;
                p->gain       = 1;
                p->gain_coeff = 1;
                
                for(unsigned i=0; i<kChCnt; ++i)
                {
                  p->osc[i].ch      = p->wt->chA + i;
                  p->osc[i].seg_idx = 0;
                  p->osc[i].smp_idx = 0;
                  p->osc[i].seg_cnt = 0;
                }

                //printf("%i non: %i %i (%i,%i)\n",proc->label_sfx_id,m->d0,m->d1,d0,d1);
                
              }
              else
              {
                p->gain_coeff = p->kReleaseGain;
                //printf("%i nof: %i %i\n",proc->label_sfx_id,m->d0,m->d1);
              }
              break;

            case midi::kNoteOffMdId:
              p->gain_coeff = p->kReleaseGain;
              //printf("%i nof: %i %i\n",proc->label_sfx_id,m->d0,m->d1);
              break;

            case midi::kPbendMdId:
              break;
              
            default:
              break;
          }
        }

        // if the voice is off then zero the audio buffer
        if( p->done_fl )
        {
          vop::zero(abuf->buf,abuf->frameN);
        }
        else
        {

          for(unsigned i=0; i<kChCnt; ++i)
          {
            sample_t*       yV = abuf->buf + i*abuf->frameN;
            unsigned        yi = 0;
            osc_state_t*    osc  = p->osc + i;

            while(yi < abuf->frameN)
            {
              wt_bank::seg_t* seg  = p->wt->chA[i].segA + osc->seg_idx;
              unsigned n;
              
              if( seg->aN - osc->smp_idx > abuf->frameN-yi )
                n = abuf->frameN-yi;
              else
                n = seg->aN - osc->smp_idx;

              vop::mul( yV + yi, seg->aV + seg->padN + osc->smp_idx, p->gain, n );

              yi           += n;
              osc->smp_idx += n;

              if( osc->smp_idx >= seg->aN )
              {
                osc->smp_idx = 0;

                osc->seg_cnt += 1;

                // if this is the second time through the loop segment
                // then begin applying the sustain decay
                if( osc->seg_cnt == 2 )
                  p->gain_coeff = p->kSustainGain;
                
                if( osc->seg_idx < osc->ch->segN-1 )
                  osc->seg_idx += 1;                  
              }            
            }

            p->gain *= p->gain_coeff;
            
            if( p->gain < p->kGainThreshold && !p->done_fl )
            {
              //printf("done\n");
              var_set(proc,kDoneFlPId,kAnyChIdx,true);
              p->done_fl = true;
            }
          }
        }
        
      errLabel:
            return rc;
        
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    
    //------------------------------------------------------------------------------------------------------------------
    //
    // audio_merge
    //

    namespace audio_merge
    {
      enum
      {
        kOutGainPId,
        kOutPId,
        kInBasePId,
      };
        
      typedef struct
      {
        unsigned inAudioVarCnt;
        unsigned gainVarCnt;
        unsigned baseGainPId;
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;        

        unsigned inAudioChCnt = 0;
        srate_t  srate        = 0;
        unsigned audioFrameN  = 0;
        unsigned sfxIdAllocN  = proc_var_count(proc);
        unsigned sfxIdA[ sfxIdAllocN ];
          

        // register the output gain variable
        if((rc = var_register(proc,kAnyChIdx,kOutGainPId,"out_gain",kBaseSfxId)) != kOkRC )
          goto errLabel;
          
        
        // get the the sfx_id's of the input audio variables 
        if((rc = var_mult_sfx_id_array(proc, "in", sfxIdA, sfxIdAllocN, p->inAudioVarCnt )) != kOkRC )
          goto errLabel;

        // for each input audio variable
        for(unsigned i=0; i<p->inAudioVarCnt; ++i)
        {
          abuf_t* abuf;

          // register the input audio variable
          if((rc = var_register_and_get(proc,kAnyChIdx,kInBasePId+i,"in",sfxIdA[i],abuf)) != kOkRC )
            goto errLabel;
            

          // the sample rate of off input audio signals must be the same
          if( i != 0 && abuf->srate != srate )
          {
            rc = cwLogError(kInvalidArgRC,"All signals on a poly merge must have the same sample rate.");
            goto errLabel;
          }

          srate = abuf->srate;

          // the count of frames in all audio signals must be the same
          if( audioFrameN != 0 && abuf->frameN != audioFrameN )
          {
            rc = cwLogError(kInvalidArgRC,"All signals on a poly merge must have the same frame count.");
            goto errLabel;
          }
          
          audioFrameN = abuf->frameN;
          
          inAudioChCnt += abuf->chN;
        }

        // Get the sfx-id's of the input gain variables
        if((rc = var_mult_sfx_id_array(proc, "gain", sfxIdA, sfxIdAllocN, p->gainVarCnt )) != kOkRC )
          goto errLabel;
        

        // There must be one gain variable for each audio input or exactly one gain variable 
        if( p->gainVarCnt != p->inAudioVarCnt && p->gainVarCnt != 1 )
        {
          rc = cwLogError(kInvalidArgRC,"The count of 'gain' variables must be the same as the count of audio variables or there must be exactly one gain variable.");
          goto errLabel;
        }

        // set the baseInGainPId
        p->baseGainPId = kInBasePId + p->inAudioVarCnt;

        // register each of the input gain variables
        for(unsigned i=0; i<p->gainVarCnt; ++i)
        {
          if((rc = var_register(proc,kAnyChIdx,p->baseGainPId + i,"gain",sfxIdA[i])) != kOkRC )
            goto errLabel;
          
        }

        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, srate, inAudioChCnt, audioFrameN );

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        return kOkRC;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        return kOkRC;
      }

      unsigned _merge_in_one_audio_var( proc_t* proc, const abuf_t* ibuf, abuf_t* obuf, unsigned outChIdx, coeff_t gain )
      {
        // for each channel          
        for(unsigned i=0; i<ibuf->chN  && outChIdx<obuf->chN; ++i)
        {            
          sample_t* isig = ibuf->buf + i        * ibuf->frameN;
          sample_t* osig = obuf->buf + outChIdx * obuf->frameN;

          // apply the gain
          for(unsigned j=0; j<ibuf->frameN; ++j)
            osig[j] = gain * isig[j];

          outChIdx += 1;
        }  

        return outChIdx;
      }
      
      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t          rc    = kOkRC;
        abuf_t*       obuf  = nullptr;
        unsigned      oChIdx = 0;
        coeff_t       igain   = 1;
        coeff_t       ogain   = 1;

        // get the output audio buffer
        if((rc = var_get(proc,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        // get the output audio gain
        if((rc = var_get(proc,kOutGainPId, kAnyChIdx, ogain)) != kOkRC )
          goto errLabel;

        // for each audio input variable
        for(unsigned i=0; i<p->inAudioVarCnt; ++i)
        {
          const abuf_t* ibuf = nullptr;

          // get the input audio buffer
          if((rc = var_get(proc,kInBasePId+i, kAnyChIdx, ibuf )) != kOkRC )
            goto errLabel;

          // get the input gain
          if( i < p->gainVarCnt )
            var_get(proc,p->baseGainPId+i,kAnyChIdx,igain);

          // merge the input audio signal into the output audio buffer
          oChIdx = _merge_in_one_audio_var( proc, ibuf, obuf, oChIdx, igain * ogain );
        }        

      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    


    //------------------------------------------------------------------------------------------------------------------
    //
    // sample_hold
    //
    namespace sample_hold
    {
      enum
      {
        kInPId,
        kPeriodMsPId,
        kOutPId,
        kMeanPId,
      };

      typedef struct inst_str
      {
        unsigned chN;            // count of audio input channels and output sample variables.
        unsigned bufAllocFrmN;   // count of sample frames allocated in the sample buffer
        unsigned periodFrmN;     // count of sample frames in the sample period
        unsigned ii;             // next buf[][] frame index to receive an incoming audio sample
        sample_t** buf;          // buf[chN][bufSmpAllocN]
      } inst_t;

      unsigned _period_ms_to_smp( srate_t srate, unsigned framesPerCycle, double periodMs )
      {
        unsigned frmN =  (unsigned)(srate * periodMs / 1000.0);
        return std::max(framesPerCycle,frmN);
      }

      unsigned _period_ms_to_smp( srate_t srate, unsigned framesPerCycle, unsigned bufSmpAllocN, double periodMs )
      {
        unsigned frmN = _period_ms_to_smp(srate,framesPerCycle, periodMs );
        
        // clip sample period to the max. buffer length.
        return std::min(bufSmpAllocN,frmN);
      }

      sample_t _mean( inst_t* p, unsigned chIdx, unsigned oi, unsigned n0, unsigned n1 )
      {
        sample_t sum = 0;
        
        for(unsigned i=0; i<n0; ++i)
          sum += p->buf[chIdx][oi + i ];
        
        for(unsigned i=0; i<n1; ++i)
          sum += p->buf[chIdx][i];
        
        return n0+n1==0 ? 0 : sum/(n0+n1);
      }

      void _destroy( inst_t* p )
      {
        for(unsigned i=0; i<p->chN; ++i)
          mem::release(p->buf[i]);
        mem::release(p->buf);
        mem::release(p);        
      }
      
      rc_t create( proc_t* proc )
      {
        rc_t          rc       = kOkRC;
        const abuf_t* abuf     = nullptr; //
        double        periodMs = 0;
        
        proc->userPtr = mem::allocZ<inst_t>();
        inst_t* p = (inst_t*)proc->userPtr;

        // get the source audio buffer
        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kInPId,       "in",       kBaseSfxId, abuf,
                                      kPeriodMsPId, "period_ms",kBaseSfxId, periodMs)) != kOkRC )
        {
          goto errLabel;
        }
        
        p->chN          = abuf->chN;
        p->bufAllocFrmN = _period_ms_to_smp( abuf->srate, proc->ctx->framesPerCycle, periodMs );
        p->periodFrmN   = p->bufAllocFrmN;
        p->buf          = mem::allocZ<sample_t*>(abuf->chN);

        for(unsigned i=0; i<abuf->chN; ++i)
        {
          p->buf[i] = mem::allocZ<sample_t>(p->bufAllocFrmN);
          if((rc = var_register_and_set(proc, i,
                                        kOutPId,  "out",  kBaseSfxId, 0.0f,
                                        kMeanPId, "mean", kBaseSfxId, 0.0f)) != kOkRC )
          {
            goto errLabel;
          }
        }

      errLabel:
        if(rc != kOkRC )
          _destroy(p);
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        inst_t* p = (inst_t*)(proc->userPtr);
        _destroy(p);
        return kOkRC;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        
        switch( var->vid )
        {
          case kPeriodMsPId:
            {
              double periodMs;
              const abuf_t* abuf;
              inst_t*  p = (inst_t*)(proc->userPtr);
                      
              var_get(proc,kInPId,kAnyChIdx,abuf);
              
              if((rc = var_get(var,periodMs)) == kOkRC )
              {
                p->periodFrmN = _period_ms_to_smp( abuf->srate, proc->ctx->framesPerCycle, p->bufAllocFrmN, periodMs );
              }
            }
            break;
            
          default:
            break;
            
        }
        
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t          rc   = kOkRC;
        const abuf_t* ibuf = nullptr;
        inst_t*       p    = (inst_t*)(proc->userPtr);
        unsigned      oi   = 0;
        unsigned      n0   = 0;
        unsigned      n1   = 0;
        //unsigned      chN  = 0;

        // get the src buffer
        if((rc = var_get(proc,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        //chN = std::min(ibuf->chN,p->chN);
        
        // Copy samples into buf.
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          sample_t* isig = ibuf->buf + i*ibuf->frameN;
          sample_t* obuf = p->buf[i];
          unsigned  k = p->ii;
          
          for(unsigned j=0; j<ibuf->frameN; ++j)
          {
            obuf[k++] = isig[j];
            if( k>= p->bufAllocFrmN )
              k -= p->bufAllocFrmN;
          }
        }

        // advance the input index
        p->ii += ibuf->frameN;
        if( p->ii >= p->bufAllocFrmN )
          p->ii -= p->bufAllocFrmN;

        
        // if the sampling buf is in range oi:ii
        if( p->ii >= p->periodFrmN )
        {
          oi = p->ii - p->periodFrmN;
          n0 = p->ii - oi;
          n1 = 0;
        }
        else // the sampling buf is in two parts: bufAllocN-ii:bufAllocN, 0:ii
        {
          oi = p->bufAllocFrmN - (p->periodFrmN - p->ii);
          n0 = p->bufAllocFrmN - oi;
          n1 = p->ii;
        }
        
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          // the output is the first sample in the buffer
          var_set(proc,kOutPId,i, p->buf[i][oi] );

          if( var_is_a_source(proc,kMeanPId,i) )
            var_set(proc,kMeanPId,i, _mean(p,i,oi,n0,n1));
        }
                
      errLabel:
        return rc;
      }

      
      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value = value,
        .exec = exec,
        .report = nullptr
      };
      
    }

    //------------------------------------------------------------------------------------------------------------------
    //
    // Number
    //
    namespace number
    {
      enum {
        kTriggerPId,
        kOutTypePId,
        kOutPId,
        kInPId,
      };

      typedef struct
      {
        unsigned inVarN;
        unsigned store_vid;
        bool     send_fl;
      } inst_t;

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc             = kOkRC;
        const char* out_type_label = nullptr;
        unsigned    out_type_fl    = kInvalidTFl;
        variable_t* dum            = nullptr;
        unsigned    inVarN         = var_mult_count(proc,"in");
        unsigned    inSfxIdA[ inVarN ];

        if((rc = var_register(proc,kAnyChIdx,kTriggerPId,"trigger",kBaseSfxId)) != kOkRC )
          goto errLabel;


        // if there are no inputs
        if( inVarN == 0 )
        {
          rc = cwLogError(rc,"The 'number' unit '%s' does not have any inputs.",cwStringNullGuard(proc->label));
          goto errLabel;
        }
        
        // get the the sfx_id's of the 'in' variables 
        if((rc = var_mult_sfx_id_array(proc, "in", inSfxIdA, inVarN, p->inVarN )) != kOkRC )
          goto errLabel;
        
        // sort the input id's in ascending order
        std::sort(inSfxIdA, inSfxIdA + p->inVarN, [](unsigned& a,unsigned& b){ return a<b; } );

        // register each of the input vars
        for(unsigned i=0; i<p->inVarN; ++i)
        {
          variable_t* dum;
          if((rc = var_register(proc, "in", inSfxIdA[i], kInPId+i, kAnyChIdx, nullptr, dum )) != kOkRC )
          {
            rc = cwLogError(rc,"Variable registration failed for the variable 'in:%i'.",inSfxIdA[i]);;
            goto errLabel;
          }
        }

        
        // Get the output type label as a string
        if((rc = var_register_and_get(proc,kAnyChIdx,kOutTypePId,"out_type",kBaseSfxId,out_type_label)) != kOkRC )
        {
          rc = cwLogError(rc,"Variable registration failed for the variable 'otype:0'.");;
          goto errLabel;          
        }


        // if an explicit output type was not given ...
        if( textIsEqual(out_type_label,"") )
        {
          // ... then get the type of the first input variable
          if((rc = var_find(proc, kInPId, kAnyChIdx, dum )) != kOkRC )
          {
            goto errLabel;
          }

          // if the first input variable's type  has a valid type is not included in the 
          if( dum->value != nullptr )
            out_type_fl = (dum->value->tflag & kTypeMask) ;
        }
        else
        {
          out_type_fl = value_type_label_to_flag( out_type_label );
        }

        if(out_type_fl == kInvalidTFl )
        {
          rc = cwLogError(kInvalidArgRC,"The output type '%s' is not a valid type.",cwStringNullGuard(out_type_label));
          goto errLabel;
        }
                  
        // Create the output variable
        if((rc = var_create( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, out_type_fl, dum )) != kOkRC )
        {
          goto errLabel;
        }
        

        p->store_vid = kInvalidId;
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      { return kOkRC; }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        if( var->vid == kTriggerPId )
        {
          if( proc->ctx->isInRuntimeFl )
            p->store_vid = kOutPId;
        }
        else
        {
          if( kInPId <= var->vid && var->vid < kInPId + p->inVarN )
          {
            if( proc->ctx->isInRuntimeFl )
            {
              p->store_vid = var->vid;
            }
            else
              var_set( proc, kOutPId, kAnyChIdx, var->value );
          }          
        }
        
        return kOkRC;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        if( p->store_vid != kInvalidIdx )
        {
          variable_t* var = nullptr;
          
          // Note that we set the 'value' directly from var->value so that
          // no extra type converersion is applied. In this case the value
          // 'store'  will be coerced to the type of 'value'
          if((rc = var_find(proc, p->store_vid, kAnyChIdx, var )) == kOkRC && var->value != nullptr /*&& is_connected_to_source(var)*/ )
          {
            rc = var_set(proc,kOutPId,kAnyChIdx,var->value);
          }

          p->store_vid = kInvalidIdx;
        }
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

#ifdef NOT_DEF    
    //------------------------------------------------------------------------------------------------------------------
    //
    // Number
    //
    namespace number
    {
      enum {
        kValuePId,
        kStorePId,
      };

      typedef struct
      {
        bool store_fl;
      } inst_t;

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        
        if((rc = var_register(proc,kAnyChIdx,
                              kValuePId,"value",kBaseSfxId,
                              kStorePId,"store",kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      { return kOkRC; }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        // skip the 'stored' value sent through prior to runtime.
        if( var->vid == kStorePId /*&& proc->ctx->isInRuntimeFl*/)
          p->store_fl = true;
        
        return kOkRC;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        if( p->store_fl )
        {
          variable_t*  var = nullptr;
          // Set 'value' from 'store'.
          // Note that we set the 'value' directly from var->value so that
          // no extra type converersion is applied. In this case the value
          // 'store'  will be coerced to the type of 'value'
          if((rc = var_find(proc, kStorePId, kAnyChIdx, var )) == kOkRC && var->value != nullptr && is_connected_to_source(var) )
          {
            rc = var_set(proc,kValuePId,kAnyChIdx,var->value);
          }
          p->store_fl = false;
        }
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    
#endif
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // Register
    //
    namespace reg
    {
      enum {
        kInPId,
        kStorePId,
        kOutPId,
      };

      typedef struct
      {
        value_t value;
        bool    store_fl;
      } inst_t;

      rc_t _set_stored_value( proc_t* proc, inst_t* p, const variable_t* var )
      {
        rc_t rc = kOkRC;
        
        if( var->value == nullptr )
        {
          rc = cwLogError(kInvalidStateRC,"The incoming register value is NULL.");
          goto errLabel;
        }

        value_duplicate(p->value,*var->value);
        p->store_fl = true;

      errLabel:
        return rc;
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t              rc  = kOkRC;
        const variable_t* in_var = nullptr;
        variable_t* out_var = nullptr;
        variable_t* store_var = nullptr;
        
        if((rc = var_register(proc, kAnyChIdx,
                              kInPId,    "in",    kBaseSfxId,
                              kStorePId, "store", kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_find(proc,"in",kBaseSfxId,kAnyChIdx,in_var )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _set_stored_value(proc,p,in_var)) != kOkRC )
        {
          goto errLabel;
        }
        
        // Create the output var
        if((rc = var_create( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, in_var->value->tflag, out_var )) != kOkRC )
        {          
          rc = cwLogError(rc,"The output variable create failed.");
          goto errLabel;
        }

        
        if((rc = var_find(proc,"store",kBaseSfxId,kAnyChIdx,store_var )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_set(store_var,&p->value)) != kOkRC )
          goto errLabel;
        
        if((rc = var_set(out_var,&p->value)) != kOkRC )
          goto errLabel;
        
        //store_var->value = &p->value;
        //out_var->value = &p->value;

        
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      { return kOkRC; }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        switch( var->vid )
        {
          case kInPId:
          case kStorePId:
            if( var->value != nullptr )
              _set_stored_value(proc,p,var);
            break;

          case kOutPId:
            break;
            
          default:
            assert(0);            
        }
                
        return kOkRC;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        if( p->store_fl )
        {
          rc = var_set(proc,kOutPId,kAnyChIdx,&p->value);
          p->store_fl = false;
        }
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // Timer
    //
    namespace timer
    {
      enum {
        kSratePId,
        kPeriodMsPId,
        kDelayMsPId,
        kOutPId,
      };
      
      typedef struct
      {
        bool     delayFl;
        unsigned delayFrmN;
        unsigned periodFrmN;
        unsigned periodPhase;
      } inst_t;

      unsigned _period_ms_to_frame_count( proc_t* proc, inst_t* p, srate_t srate, ftime_t periodMs )
      {
        return std::max((unsigned)(srate * periodMs / 1000.0), proc->ctx->framesPerCycle);
      }

      rc_t create( proc_t* proc )
      {
        rc_t    rc       = kOkRC;
        ftime_t  periodMs = 0;
        ftime_t  delayMs  = 0;
        srate_t srate    = 0;
        inst_t* p        = mem::allocZ<inst_t>();
        proc->userPtr    = p;
        

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kSratePId,    "srate",    kBaseSfxId,srate,
                                      kDelayMsPId,  "delay_ms", kBaseSfxId,delayMs,
                                      kPeriodMsPId, "period_ms",kBaseSfxId,periodMs)) != kOkRC )
        {
          goto errLabel;
        }

        if( srate == 0 )
          var_set(proc,kSratePId,kAnyChIdx,proc->ctx->sample_rate);

        if((rc = var_register_and_set(proc,kAnyChIdx,
                                      kOutPId,       "out",     kBaseSfxId,false)) != kOkRC )
        {
          goto errLabel;
        }
        
        p->periodFrmN = _period_ms_to_frame_count(proc,p,srate,periodMs);
        p->delayFrmN  = _period_ms_to_frame_count(proc,p,srate,delayMs);
        p->delayFl    = true;
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t    rc = kOkRC;
        inst_t* p  = (inst_t*)proc->userPtr;
        mem::release(p);        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        switch( var->vid )
        {
          case kPeriodMsPId:
            {
              double periodMs;
              srate_t srate;
              inst_t*  p = (inst_t*)(proc->userPtr);
                      
              var_get(proc,kSratePId,kAnyChIdx,srate);
              
              if((rc = var_get(var,periodMs)) == kOkRC )
              {
                p->periodFrmN = _period_ms_to_frame_count( proc, p, srate, periodMs );
              }
            }
            break;


          case kDelayMsPId:
            {
              double delayMs;
              srate_t srate;
              inst_t*  p = (inst_t*)(proc->userPtr);
                      
              var_get(proc,kSratePId,kAnyChIdx,srate);
              
              if((rc = var_get(var,delayMs)) == kOkRC )
                p->delayFrmN = _period_ms_to_frame_count( proc, p, srate, delayMs );
            }
            break;
            
          default:
            break;
        }
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t    rc = kOkRC;
        inst_t* p  = (inst_t*)proc->userPtr;

        p->periodPhase += proc->ctx->framesPerCycle;

        if( p->delayFl )
        {
          if( p->periodPhase >= p->delayFrmN )
          {
            p->periodPhase -= p->delayFrmN;
            p->delayFl = false;
          }            
        }

        //printf("%i %i\n",p->periodPhase,p->periodFrmN);
        
        if( p->delayFl==false && p->periodPhase >= p->periodFrmN )
        {
          p->periodPhase -= p->periodFrmN;
          
          bool val = false;
          var_get(proc,kOutPId,kAnyChIdx,val);

          //printf("%i %i %i\n",p->periodPhase,p->periodFrmN,val);
          
          var_set(proc,kOutPId,kAnyChIdx,!val);
        }
        
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }    
    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // Counter
    //
    namespace counter
    {
      enum {
        kTriggerPId,
        kResetPId,
        kInitPId,
        kMinPId,
        kMaxPId,
        kIncPId,
        kRepeatPId,
        kModePId,
        kOutTypePId,
        kUprLimPId,
        kLwrLimPId,
        kLimitPId,
        kOutPId
      };

      enum {        
        kModuloModeId,
        kReverseModeId,
        kClipModeId,
        kInvalidModeId
      };
      
      typedef struct
      {
        unsigned mode_id;
        
        bool trig_val;
        bool delta_fl;

        bool done_fl;
        unsigned iter_cnt;

        double dir;
        
      } inst_t;

      idLabelPair_t modeArray[] = {
        { kModuloModeId, "modulo" },
        { kReverseModeId, "reverse" },
        { kClipModeId,    "clip" },
        { kInvalidId, "<invalid>"}
      };


      unsigned _string_to_mode_id( const char* mode_label, unsigned& mode_id_ref )
      {
        mode_id_ref = kInvalidId;
        for(unsigned i=0; modeArray[i].id != kInvalidId; ++i)
          if( textIsEqual(modeArray[i].label,mode_label) )
          {
            mode_id_ref = modeArray[i].id;
            return kOkRC;            
          }
        
        return cwLogError(kInvalidArgRC,"'%s' is not a valid counter 'mode'.",cwStringNullGuard(mode_label));
      }

      rc_t create( proc_t* proc )
      {
        rc_t        rc  = kOkRC;        
        inst_t*     p   = mem::allocZ<inst_t>();
        proc->userPtr   = p;
        
        double      init_val;
        const char* mode_label;
        variable_t* dum = nullptr;
        const char* out_type_label;
        unsigned    out_type_fl;

        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kTriggerPId, "trigger", kBaseSfxId, p->trig_val,
                                      kInitPId,    "init",    kBaseSfxId, init_val,
                                      kModePId,    "mode",    kBaseSfxId, mode_label,
                                      kOutTypePId, "out_type",kBaseSfxId, out_type_label)) != kOkRC )
        {
          goto errLabel;
        }
                                      
        
        if((rc = var_register(proc, kAnyChIdx,
                              kMinPId,     "min",       kBaseSfxId,
                              kMaxPId,     "max",       kBaseSfxId,
                              kIncPId,     "inc",       kBaseSfxId,
                              kRepeatPId,  "repeat_fl", kBaseSfxId,
                              kResetPId,   "reset",     kBaseSfxId,
                              kUprLimPId,  "upr_lim",   kBaseSfxId,
                              kLwrLimPId,  "lwr_lim",   kBaseSfxId,
                              kLimitPId,   "limit",     kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        // get the type of the output
        if(out_type_label==nullptr || (out_type_fl = value_type_label_to_flag( out_type_label )) == kInvalidTFl )
        {
          rc = cwLogError(kInvalidArgRC,"The output type '%s' is not a valid type.",cwStringNullGuard(out_type_label));
          goto errLabel;
        }


        if((rc = var_create( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, out_type_fl, dum )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_set( proc, kOutPId, kAnyChIdx, init_val )) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to set the initial counter value to %f.",init_val);
          goto errLabel;
        }
                                                                    

        if((rc = _string_to_mode_id(mode_label,p->mode_id)) != kOkRC )
          goto errLabel;
        
        p->dir = 1.0;
        p->iter_cnt = 0;
        
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* p = (inst_t*)proc->userPtr;
        mem::release(p);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        inst_t* p = (inst_t*)proc->userPtr;

        switch( var->vid )
        {
          case kTriggerPId:
            {
              /*
              bool v;
              if((rc = var_get(var,v)) == kOkRC )
              {
                if( !p->delta_fl )
                  p->delta_fl = p->trig_val != v;
                
                p->trig_val = v;            
              }
              */
              p->delta_fl = true;
              
            }
            break;

          case kModePId:
            {
              const char* s;
              if((rc = var_get(var,s)) == kOkRC )
                rc = _string_to_mode_id(s,p->mode_id);
            }
            break;

          case kResetPId:
            p->iter_cnt = 0;
            p->dir      = 1.0;
            p->delta_fl = false;
            p->done_fl  = false;            
            break;
              
        }
        
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t rc      = kOkRC;
        inst_t* p = (inst_t*)proc->userPtr;
        bool trig_upr_fl = false;
        bool trig_lwr_fl = false;
        double cnt,inc,minv,maxv;        
        bool v;

        if( !p->delta_fl )
          return rc;
        
        p->delta_fl = false;
        if((rc = var_get(proc,kTriggerPId,kAnyChIdx,v)) != kOkRC )
        {
          cwLogError(rc,"Fail!");
          goto errLabel;
        }
         
        p->trig_val = v;

        p->iter_cnt += 1;
        /*
        if( p->iter_cnt == 1 )
        {
          var_get(proc,kInitPId,kAnyChIdx,cnt);          
        }
        else
        &*/
        if(1)
        {        
          var_get(proc,kOutPId,kAnyChIdx,cnt);
          var_get(proc,kIncPId,kAnyChIdx,inc);
          var_get(proc,kMinPId,kAnyChIdx,minv);
          var_get(proc,kMaxPId,kAnyChIdx,maxv);

          cnt += p->dir * inc;

          //printf("%f %f %f\n",minv,cnt,maxv);

          if( minv > cnt || cnt >= maxv )
          {
            bool repeat_fl;
            var_get(proc,kRepeatPId,kAnyChIdx,repeat_fl);

            trig_upr_fl = cnt >= maxv;
            trig_lwr_fl = cnt < minv;

            if( !repeat_fl )
              p->done_fl = true;
            else
            {              
              if( cnt >= maxv)
              {
                switch( p->mode_id )
                {
                  case kModuloModeId:
                    while(cnt >= maxv )
                      cnt = minv + (cnt-maxv);
                    break;

                  case kReverseModeId:
                    p->dir = -1 * p->dir;
                    while( cnt > maxv )
                      cnt = maxv - (cnt-maxv);
                    break;

                  case kClipModeId:
                    cnt = maxv;
                    break;

                  default:
                    assert(0);
                    
                }
              }

              if( cnt < minv)
              {
                switch( p->mode_id )
                {
                  case kModuloModeId:
                    while( cnt < minv )
                      cnt = maxv - (minv-cnt);
                    break;
                    
                  case kReverseModeId:
                    p->dir = -1 * p->dir;
                    while(cnt < minv )
                      cnt = minv + (minv-cnt);
                    break;
                    
                  case kClipModeId:
                    cnt = minv;
                    break;
                    
                  default:
                    assert(0);
                }                
              }                
            }
          }
        }
        
        // if the counter has not reached it's terminal state
        if( !p->done_fl )
          var_set(proc,kOutPId,kAnyChIdx,cnt);
        
        if( trig_upr_fl )
          if((rc = var_set(proc, kUprLimPId, kAnyChIdx, true )) != kOkRC )
            goto errLabel;

        if( trig_lwr_fl )
          if((rc = var_set(proc, kLwrLimPId, kAnyChIdx, true )) != kOkRC )
            goto errLabel;

        if( trig_upr_fl || trig_lwr_fl )
          if((rc = var_set(proc, kLimitPId, kAnyChIdx, true )) != kOkRC )
            goto errLabel;

      errLabel:
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // List
    //
    namespace list
    {
      enum
      {
        kInPId,
        kCfgFnamePId,
        kListPId,
        kOutPId,
        kValueBasePId
      };
      
      typedef struct
      {
        unsigned        listN;   // the length of the list
        const object_t* list;    // the list
        unsigned        typeFl;  // the output type
        unsigned        index;     // the last index referenced
        bool            deltaFl;
        object_t*       file_list;
      } inst_t;

      rc_t _determine_type( const object_t* list, unsigned& typeFl_ref )
      {
        rc_t rc = kOkRC;

        typeFl_ref = kInvalidTFl;
        
        enum { bool_idx, uint_idx, int_idx, float_idx, double_idx, string_idx, cfg_idx, typeN };
        typedef struct type_map_str
        {
          unsigned idx;
          unsigned typeFl;
          unsigned cnt;
        } type_map_t;

        type_map_t typeA[] = {
          { bool_idx,   kBoolTFl,   0 },
          { uint_idx,   kUIntTFl,   0 },
          { int_idx,    kIntTFl,    0 },
          { float_idx,  kFloatTFl,  0 },
          { double_idx, kDoubleTFl, 0 },
          { string_idx, kStringTFl, 0 },
          { cfg_idx,    kCfgTFl,    0 },          
        };


        // count the number of each type of element in the list.
        for(unsigned i=0; i<list->child_count(); ++i)
        {
          const object_t* c = list->child_ele(i);

          switch( c->type->id )
          {
            case kCharTId:   typeA[uint_idx].cnt+=1; break;
            case kInt8TId:   typeA[int_idx].cnt +=1; break;             
            case kUInt8TId:  typeA[uint_idx].cnt+=1; break;
            case kInt16TId:  typeA[int_idx].cnt +=1; break;
            case kUInt16TId: typeA[uint_idx].cnt+=1; break;
            case kInt32TId:  typeA[int_idx].cnt +=1; break;
            case kUInt32TId: typeA[uint_idx].cnt+=1; break;
            case kFloatTId:  typeA[float_idx].cnt+=1; break;
            case kDoubleTId: typeA[double_idx].cnt+=1; break;
            case kBoolTId:   typeA[bool_idx].cnt+=1; break;
            case kStringTId: typeA[string_idx].cnt+=1; break;
            case kCStringTId:typeA[string_idx].cnt+=1; break;
              break;
              
            default:
              switch( c->type->id )
              {
                case kVectTId:
                case kPairTId:
                case kListTId:
                case kDictTId:
                  typeA[cfg_idx].cnt +=1;
                  break;
                
                default:
                  rc = cwLogError(kSyntaxErrorRC,"The object type '0x%x' is not a valid list entry type. %i",c->type->flags,list->child_count());
                  goto errLabel;
              }
          }

          unsigned type_flag = kInvalidTFl; // type flag of one of the reference types
          unsigned type_cnt = 0;            // count of types

          for(unsigned i=0; i<typeN; ++i)
            if( typeA[i].cnt > 0 )
            {
              type_cnt += 1;
              type_flag = typeA[i].typeFl;
            }

          // it is an error if more than one type of element was included in the list - 
          // and one of those types was string or cfg - having multiple numeric types
          // is ok because they can be converted between each other - but string, and cfg's
          // cannot be converted to numbers, nor can the be converted between each other.
          if( type_cnt > 1 && (typeA[string_idx].cnt>0 || typeA[cfg_idx].cnt>0) )
          {
            rc = cwLogError(kInvalidArgRC,"The list types. The list must be all numerics, all strings, or all cfg. types.");
            for(unsigned i=0; i<typeN; ++i)
              if( typeA[i].cnt > 0 )
                cwLogInfo("%i %s",typeA[i].cnt, value_type_flag_to_label(typeA[i].typeFl));
            
            goto errLabel;
          }

          typeFl_ref = type_flag;
        }


      errLabel:
        return rc;
      }

      template< typename T >
      rc_t _set_out_tmpl( proc_t* proc, inst_t* p, unsigned idx, unsigned vid, T& v )
      {
        rc_t rc;
        const object_t* ele;        

        // get the list element to output
        if((ele = p->list->child_ele(idx)) == nullptr )
        {
          rc = cwLogError(kEleNotFoundRC,"The list element at index %i could not be accessed.",idx);
          goto errLabel;
        }

        // get the value of the list element
        if((rc = ele->value(v)) != kOkRC )
        {
          rc = cwLogError(rc,"List value access failed on index %i",idx);
          goto errLabel;
        }

        // set the output
        if((rc = var_set(proc,vid,kAnyChIdx,v)) != kOkRC )
        {
          rc = cwLogError(rc,"List output failed on index %i",idx);
          goto errLabel;          
        }

      errLabel:
        return rc;
      }

      rc_t _set_output( proc_t* proc, inst_t* p, unsigned idx, unsigned vid )
      {
        rc_t rc;

        switch( p->typeFl )
        {
          case kUIntTFl:
            {
              unsigned v;
              rc = _set_out_tmpl(proc,p,idx,vid,v);
            }
            break;
            
          case kIntTFl:
            {
              int v;
              rc = _set_out_tmpl(proc,p,idx,vid,v);
            }
            break;
            
          case kFloatTFl:
            {
              float v;
              rc = _set_out_tmpl(proc,p,idx,vid,v);
            }
            break;
            
          case kDoubleTFl:
            {
              double v;
              rc = _set_out_tmpl(proc,p,idx,vid,v); 
            }
            break;
            
          case kStringTFl:
            {
              const char* v;
              rc = _set_out_tmpl(proc,p,idx,vid,v);
            }
            break;
            
          case kCfgTFl:
            {
              const object_t* v;
              rc = _set_out_tmpl(proc,p,idx,vid,v);
            }
            break;
            
          default:
            rc = cwLogError(kInvalidArgRC,"The list type flag %s (0x%x) is not valid.",value_type_flag_to_label(p->typeFl),p->typeFl);
            goto errLabel;
            break;
            
        }

      errLabel:
        return rc;
      }

      rc_t _set_output( proc_t* proc, inst_t* p )
      {
        rc_t rc;
        unsigned idx;
        
        if((rc = var_get(proc,kInPId,kAnyChIdx,idx)) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to get the list index.");
          goto errLabel;
        }

        // if the index has not changed then there is nothing to do
        if( idx == p->index )
          goto errLabel;

        if((rc = _set_output(proc,p,idx, kOutPId )) != kOkRC )
          goto errLabel;

        p->index = idx;

      errLabel:
        return rc;
      }
        
      

      rc_t create( proc_t* proc )
      {
        rc_t            rc            = kOkRC;        
        const char*     cfg_fname     = nullptr;
        char*           exp_cfg_fname = nullptr;
        unsigned        index         = kInvalidIdx;
        const object_t* list_arg      = nullptr;
        
        inst_t* p = mem::allocZ<inst_t>();
        proc->userPtr = p;
        
        variable_t* dum = nullptr;

        p->index   = kInvalidIdx;
        p->typeFl  = kInvalidTFl;
        p->deltaFl = false;
        
        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kCfgFnamePId, "cfg_fname", kBaseSfxId, cfg_fname,
                                      kInPId,       "in",        kBaseSfxId, index,
                                      kListPId,     "list",      kBaseSfxId, list_arg)) != kOkRC )
        {
          goto errLabel;
        }

        if( cfg_fname != nullptr && textLength(cfg_fname)!=0 )
        {

          if((exp_cfg_fname = proc_expand_filename(proc,cfg_fname)) == nullptr )
          {
            rc = cwLogError(kInvalidArgRC,"The list cfg filename could not be formed.");
            goto errLabel;
 
          }
          
          if((rc = objectFromFile(exp_cfg_fname,p->file_list)) != kOkRC )
          {
            rc = cwLogError(rc,"The list configuration file '%s' could not be parsed.",cwStringNullGuard(exp_cfg_fname));
            goto errLabel;
          }

          if((rc = p->file_list->getv("list",p->list)) != kOkRC )
          {
            rc = cwLogError(rc,"The list configuration file '%s' does not have a 'list' field.",cwStringNullGuard(exp_cfg_fname));
            goto errLabel;
          }
          
        }
        else
        {
          p->list = list_arg;
        }

        if( !p->list->is_list() )
        {
          cwLogError(kSyntaxErrorRC,"The list cfg. value is not a list.");
          goto errLabel;
        }

        p->listN = p->list->child_count();

        // determine what type of element is in the list
        // (all elements in the this list must be of the same type: numeric,string,cfg)
        if((rc = _determine_type( p->list, p->typeFl )) != kOkRC )
          goto errLabel;

        // create the output variable
        if((rc = var_create( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, p->typeFl, dum )) != kOkRC )
        {
          rc = cwLogError(rc,"'out' var create failed.");
          goto errLabel;
        }

        // set the initial value of the output 
        if((rc = _set_output(proc,p)) != kOkRC )
          goto errLabel;
                
        // create the output variable
        for(unsigned i=0; i<p->listN; ++i)
        {
          if((rc = var_create( proc, "value", i, kValueBasePId+i, kAnyChIdx, nullptr, p->typeFl, dum )) != kOkRC )
          {
            rc = cwLogError(rc,"'value%i' var create failed.",i);
            goto errLabel;
          }

          if((rc = _set_output(proc, p, i, kValueBasePId+i )) != kOkRC )
          {
            rc = cwLogError(rc,"'value%i' output failed.",i);
            goto errLabel;            
          }

        }
       
        
      errLabel:
        mem::release(exp_cfg_fname);
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* p = (inst_t*)proc->userPtr;

        if(p->file_list != nullptr )
          p->file_list->free();
        
        mem::release(p);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        if( var->vid == kInPId )
        {
          inst_t*  p   = (inst_t*)proc->userPtr;
          unsigned idx;
          if( var_get(var,idx) == kOkRC && idx != p->index)
            p->deltaFl = true;
        }
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        rc_t rc = kOkRC;
        inst_t*  p   = (inst_t*)proc->userPtr;

        if( p->deltaFl )
        {
          rc = _set_output(proc, p );
          p->deltaFl = false;
        }
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // add
    //
    namespace add
    {
      enum {
        kOutPId,
        kOTypePId,
        kInPId
      };
      
      typedef struct
      {
        bool delta_fl;
        unsigned inN;
      } inst_t;


      template< typename T >
      rc_t _sum( proc_t* proc, variable_t* var )
      {
        rc_t rc      = kOkRC;
        inst_t*  p = (inst_t*)proc->userPtr;
        
        T sum = 0;
        
        // read and sum the inputs
        for(unsigned i=0; i<p->inN; ++i)
        {
          T val;
          if((rc = var_get(proc,kInPId+i,kAnyChIdx,val)) == kOkRC )            
            sum += val;
          else
          {
            rc = cwLogError(rc,"Operand index %i read failed.",i);
            goto errLabel;
          }
        }

        // set the output
        if((rc = var_set(var,sum)) != kOkRC )
        {
          rc = cwLogError(rc,"Result set failed.");
          goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t _exec( proc_t* proc, variable_t* out_var=nullptr )
      {
        rc_t rc = kOkRC;
        inst_t* p = (inst_t*)(proc->userPtr);
        
        if( !p->delta_fl )
          return rc;

        p->delta_fl = false;

        if( out_var == nullptr )
          if((rc = var_find(proc,kOutPId,kAnyChIdx,out_var)) != kOkRC )
          {
            rc = cwLogError(rc,"The output variable could not be found.");
            goto errLabel;
          }
        
        switch( out_var->varDesc->type )
        {
          case kBoolTFl:   rc = _sum<bool>(proc,out_var);     break;
          case kUIntTFl:   rc = _sum<unsigned>(proc,out_var); break; 
          case kIntTFl:    rc = _sum<int>(proc,out_var);      break;
          case kFloatTFl:  rc = _sum<float>(proc,out_var);    break;
          case kDoubleTFl: rc = _sum<double>(proc,out_var);   break;
          default:
            rc = cwLogError(kInvalidArgRC,"The output type %s (0x%x) is not valid.",value_type_flag_to_label(out_var->value->tflag),out_var->value->tflag);
            goto errLabel;
        }

        if(rc != kOkRC )
          rc = cwLogError(kOpFailRC,"Sum failed.");

      errLabel:
        return rc;

      }
      
      rc_t create( proc_t* proc )
      {
        rc_t    rc   = kOkRC;        
        inst_t* p = mem::allocZ<inst_t>();
        proc->userPtr = p;

        variable_t* out_var        = nullptr;
        const char* out_type_label = nullptr;
        unsigned    out_type_flag  = kInvalidTFl;
        unsigned    sfxIdAllocN    = proc_var_count(proc);
        unsigned    sfxIdA[ sfxIdAllocN ];
        p->inN = 0;

        // get a count of the number of input variables
        if((rc = var_mult_sfx_id_array(proc, "in", sfxIdA, sfxIdAllocN, p->inN )) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to obtain the array of mult label-sfx-id's for the variable 'in'.");
          goto errLabel;
        }

        // if the adder has no inputs
        if( p->inN == 0 )
        {
          rc = cwLogError(rc,"The 'add' unit '%s' appears to not have any inputs.",cwStringNullGuard(proc->label));
          goto errLabel;
        }

        // sort the input id's in ascending order
        std::sort(sfxIdA, sfxIdA + p->inN, [](unsigned& a,unsigned& b){ return a<b; } );

        // register each of the input vars
        for(unsigned i=0; i<p->inN; ++i)
        {
          variable_t* dum;
          if((rc = var_register(proc, "in", sfxIdA[i], kInPId+i, kAnyChIdx, nullptr, dum )) != kOkRC )
          {
            rc = cwLogError(rc,"Variable registration failed for the variable 'in:%i'.",sfxIdA[i]);;
            goto errLabel;
          }
        }

        // Get the output type label as a string
        if((rc = var_register_and_get(proc,kAnyChIdx,kOTypePId,"otype",kBaseSfxId,out_type_label)) != kOkRC )
        {
          rc = cwLogError(rc,"Variable registration failed for the variable 'otype:0'.");;
          goto errLabel;          
        }

        // Convert the output type label into a flag
        if((out_type_flag = value_type_label_to_flag(out_type_label)) == kInvalidTFl )
        {
          rc = cwLogError(rc,"The type label '%s' does not identify a valid type.",cwStringNullGuard(out_type_label));;
          goto errLabel;          
        }

        // Create the output var
        if((rc = var_create( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, out_type_flag, out_var )) != kOkRC )
        {          
          rc = cwLogError(rc,"The output variable create failed.");
          goto errLabel;
        }

        /*
        if((rc = var_set(proc,kOutPId,kAnyChIdx,0.0)) != kOkRC )
        {
          rc = cwLogError(rc,"Initial output variable set failed.");
          goto errLabel;          
        }
        */
        
        p->delta_fl=true;
        _exec(proc,out_var);
      errLabel:
        return rc;
      }

      rc_t destroy( proc_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* p = (inst_t*)proc->userPtr;

        mem::release(p);
        
        return rc;
      }

      rc_t value( proc_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        inst_t* p = (inst_t*)(proc->userPtr);

        // The check for 'isInRuntimeFl' prevents the adder from issuing an output
        // on cycle 0 - otherwise the delta flag will be set by the adder
        // receiving pre-runtime messages.
        if( kInPId <= var->vid && var->vid < kInPId+p->inN && proc->ctx->isInRuntimeFl )
          p->delta_fl = true;
        
        return rc;
      }

      rc_t exec( proc_t* proc )
      {
        return _exec(proc);
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
      };      
    }    


    //------------------------------------------------------------------------------------------------------------------
    //
    // preset
    //
    namespace preset
    {

      enum { kInPId };
      
      enum { kPresetLabelCharN=255 };
      
      typedef struct
      {
        char preset_label[ kPresetLabelCharN+1];
        bool delta_fl;
      } inst_t;


      rc_t _set_preset( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        unsigned presetLabelCharN = 0;
        const char* preset_label = nullptr;

        // get the preset label
        if((rc = var_get(proc, kInPId, kAnyChIdx, preset_label)) != kOkRC )
        {
          rc = cwLogError(rc,"The variable 'in read failed.");
          goto errLabel;
        }

        // at this point a valid preset-label must exist
        if( preset_label == nullptr || (presetLabelCharN=textLength(preset_label))==0 )
        {
          rc = cwLogError(kInvalidArgRC,"Preset application failed due to blank preset label.");
          goto errLabel;
        }

        // if the preset-label has not changed since the last preset application - then there is nothing to do
        if( textIsEqual(preset_label,p->preset_label) )
          goto errLabel;
          

        // verify the preset-label is not too long
        if( presetLabelCharN > kPresetLabelCharN )
        {
          rc = cwLogError(kBufTooSmallRC,"The preset label '%s' is to long.",cwStringNullGuard(preset_label));
          goto errLabel;
        }
        
        cwRuntimeCheck(proc->net != nullptr );
        
        // apply the preset
        if((rc = network_apply_preset(*proc->net, preset_label)) != kOkRC )
        {
          rc = cwLogError(rc,"Appy preset '%s' failed.",cwStringNullGuard(preset_label));
          goto errLabel;
        }

        // store the applied preset-label 
        textCopy(p->preset_label,kPresetLabelCharN,preset_label,presetLabelCharN);

      errLabel:
        return rc;
        
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;        

        // Custom create code goes here
        const char* label = nullptr;

        p->preset_label[0] = 0;
        p->delta_fl = true;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,kInPId,"in",kBaseSfxId,label)) != kOkRC )
          goto errLabel;

        // we can't apply a preset here because the network is not yet constructed
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      { return kOkRC; }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        if( var->vid == kInPId )
          p->delta_fl = true;
        
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        
        if( p->delta_fl )
          rc = _set_preset(proc,p);
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      
      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // Print
    //
    namespace print
    {
      enum {
        kTextPId,
        kBaseInPId
      };

      typedef struct
      {
        unsigned     eolPId;
        unsigned     inVarN;
        const char** labelA;
        unsigned     labelN;
      } inst_t;

      rc_t _parse_label_array( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        const object_t* textListCfg = nullptr;
        unsigned textListN = 0;
        
        // get the text list
        if((rc = var_get(proc,kTextPId,kAnyChIdx,textListCfg)) != kOkRC )
        {
          goto errLabel;
        }

        if(( textListN = textListCfg->child_count()) != p->labelN )
        {
          cwLogWarning("The count of labels does in print proc '%s' does not match the count of inputs plus one. %i != %i",proc->label,textListN,textListCfg->child_count());          
        }

        // for each string in the list
        for(unsigned i=0; i<textListN && i<p->labelN; ++i)
        {
          const object_t* textCfg = textListCfg->child_ele(i);
          
          if( textCfg==nullptr || !textCfg->is_string() )
            rc = cwLogError(kSyntaxErrorRC,"The print proc '%s' text list must be a list of strings.",proc->label);

          if((rc = textCfg->value(p->labelA[i])) != kOkRC )
            rc = cwLogError(kSyntaxErrorRC,"The print proc '%s' text label at index could not be read.");
        }

        // fill in any unspecified labels with blank strings
        for(unsigned i=textListN; i<p->labelN; ++i)
          p->labelA[i] = "";

      errLabel:
        return rc;
      }
      
      
      rc_t _print_field( proc_t* proc, inst_t* p, unsigned field_idx, const value_t* value )
      {
        if( field_idx >= p->inVarN )
        {
          assert( p->labelA[p->labelN-1] != nullptr );
          cwLogPrint("%s\n",p->labelA[p->labelN-1]);          
        }
        else
        {
          assert( field_idx<p->labelN && p->labelA[field_idx] != nullptr );
          cwLogPrint("%s ",p->labelA[field_idx]);
          value_print(value);
        }

        return kOkRC;
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t     rc           = kOkRC;        
        unsigned inVarN       = var_mult_count(proc, "in" );
        unsigned inVarSfxIdA[ inVarN ];

        if((rc = var_register(proc,kAnyChIdx,kTextPId,"text",kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

    
        if((rc = var_mult_sfx_id_array(proc, "in", inVarSfxIdA, inVarN, p->inVarN )) != kOkRC )
        {
          goto errLabel;
        }

        for(unsigned i=0; i<p->inVarN; ++i)
        {
          if((rc = var_register(proc,kAnyChIdx,kBaseInPId+i,"in",inVarSfxIdA[i])) != kOkRC )
          {
            goto errLabel;
          }
        }
        
        // There must be one label for each input plus an end of line label
        p->labelN = p->inVarN+1;
        p->labelA = mem::allocZ<const char*>( p->labelN );
        p->eolPId = kBaseInPId + p->inVarN;

        // Register the eol_fl with the highest variable id - so that it is called last during the later stage
        // of proc initialization where the value() function is called for each variable.
        // This way the EOL message will occur after all the 'in' values have been printed.
        if((rc = var_register(proc,kAnyChIdx,p->eolPId,"eol_fl",kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        for(unsigned i=0; i<p->labelN; ++i)
          p->labelA[i] = "";

        rc = _parse_label_array(proc,p);
        
      errLabel:

        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        mem::release(p->labelA);
        p->labelN=0;
        p->inVarN=0;
        return kOkRC;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        switch( var->vid )
        {
            
          case kTextPId:
            _parse_label_array(proc,p);            
            break;
            
          default:
            //printf("[%i %i] ",proc->ctx->cycleIndex,var->vid);

            /*
            if( var->vid == p->eolPId )
              _print_field(proc,p,p->inVarN,nullptr);
            else
            {
              if( kBaseInPId <= var->vid && var->vid <= kBaseInPId + p->inVarN )
              {
                _print_field(proc,p,var->vid - kBaseInPId,var->value);
              }
            }
            */

            if( var->vid == p->eolPId )
            {              
              for(unsigned vid = kBaseInPId; vid<kBaseInPId + p->inVarN; vid+=1 )
              {
                variable_t* v = nullptr;
                
                if(var_find(proc, vid, kAnyChIdx, v) != kOkRC )
                  continue;
                
                _print_field(proc, p, vid - kBaseInPId, v->value);
              }

              _print_field(proc,p,p->inVarN,nullptr);

            }
            
        }

        // always report success - don't let print() interrupt the network
        return kOkRC;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      { return kOkRC; }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    


    //------------------------------------------------------------------------------------------------------------------
    //
    // Halt
    //
    namespace halt
    {
      enum {
        kInPId
      };
      
      typedef struct
      {
        bool halt_fl;
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        return var_register(proc,kAnyChIdx,kInPId,"in",kBaseSfxId);
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        return kOkRC;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        if( proc->ctx->isInRuntimeFl && var->vid == kInPId )
        {
          p->halt_fl = true;
        }
        
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        return p->halt_fl ? kEofRC : kOkRC;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }


    //------------------------------------------------------------------------------------------------------------------
    //
    // midi_msg
    //
    namespace midi_msg
    {
      enum {
        kChPId,
        kStatusPId,
        kD0_PId,
        kD1_PId,
        kTriggerPId,
        kBufCntPId,
        kOutPId
      };
      
      typedef struct
      {
        midi::ch_msg_t* msgA;
        unsigned        msgN;
        unsigned        msg_idx;

        uint8_t ch;
        uint8_t status;
        uint8_t d0;
        uint8_t d1;
        
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;        

        if((rc = var_register(proc,kAnyChIdx,
                              kChPId,"ch",kBaseSfxId,
                              kStatusPId,"status",kBaseSfxId,
                              kD0_PId,"d0",kBaseSfxId,
                              kD1_PId,"d1",kBaseSfxId,
                              kTriggerPId,"trigger",kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register_and_get(proc,kAnyChIdx,kBufCntPId,"buf_cnt",kBaseSfxId,p->msgN)) != kOkRC )
        {
          goto errLabel;
        }

        p->msgA = mem::allocZ<midi::ch_msg_t>(p->msgN);

        // create one output MIDI buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, 0  );

        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        mem::release(p->msgA);
        return kOkRC;
      }

      rc_t _set_midi_byte_value( proc_t* proc, inst_t* p, unsigned vid, const char* label, uint8_t max_val, uint8_t& midi_byte_ref )
      {
        rc_t rc;
        unsigned v;

        if((rc = var_get(proc,vid,kAnyChIdx,v)) != kOkRC )
          goto errLabel;

        if( 0 <= v && v <= max_val )
          midi_byte_ref = (uint8_t)v;
        else
        {
          rc = cwLogError(kInvalidArgRC,"MIDI %s value (%i) is out of range 0-%i.",label,v,max_val);
          goto errLabel;
        }
        

      errLabel:
        return rc;
      }

      rc_t _store_msg( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        
        if( p->msg_idx >= p->msgN )
          rc = cwLogError(kBufTooSmallRC,"MIDI buffer overflow.");
        else
        {
          midi::ch_msg_t* m = p->msgA + p->msg_idx;
          time::now(m->timeStamp);
          m->devIdx  = kInvalidIdx;
          m->portIdx = 0;
          m->uid     = 0;
          m->ch      = p->ch;
          m->status  = p->status;
          m->d0      = p->d0;
          m->d1      = p->d1;
        
          p->msg_idx += 1;
        }
        
        return rc;
      }
      
      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        switch( var->vid )
        {
          case kChPId:
            rc = _set_midi_byte_value(proc,p,kChPId,"channel",midi::kMidiChCnt,p->ch);
            break;
            
          case kStatusPId:
            rc = _set_midi_byte_value(proc,p,kStatusPId,"status",255,p->status);
            break;
            
          case kD0_PId:
            rc = _set_midi_byte_value(proc,p,kD0_PId,"d0",127,p->d0);                        
            break;
            
          case kD1_PId:
            rc = _set_midi_byte_value(proc,p,kD1_PId,"d1",127,p->d1);                        
            break;
            
          case kTriggerPId:
            rc = _store_msg(proc,p);
            break;            
        }
        
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t     rc           = kOkRC;
        mbuf_t*  mbuf         = nullptr;

        // get the output variable
        if((rc = var_get(proc,kOutPId,kAnyChIdx,mbuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The MIDI msg. instance '%s' does not have a valid MIDI output buffer.",proc->label);
        else
        {
          mbuf->msgN = p->msg_idx;
          mbuf->msgA = p->msg_idx > 0 ? p->msgA : nullptr;
        }

        p->msg_idx = 0;
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // midi_split
    //
    namespace midi_split
    {
      enum {
        kInPId,
        kChPId,
        kStatusPId,
        kD0PId,
        kD1PId,
        kBufCntPId
      };
      
      typedef struct
      {
        midi::ch_msg_t* msgA;
        unsigned        msgN;
        unsigned        msg_idx;
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t     rc      = kOkRC;        
        
        if((rc = var_register(proc,kAnyChIdx,
                              kInPId,"in",kBaseSfxId,
                              kChPId,"ch",kBaseSfxId,
                              kStatusPId,"status",kBaseSfxId,
                              kD0PId,"d0",kBaseSfxId,
                              kD1PId,"d1",kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register_and_get(proc,kAnyChIdx,kBufCntPId,"buf_cnt",kBaseSfxId,p->msgN)) != kOkRC )
        {
          goto errLabel;
        }

        p->msgA = mem::allocZ<midi::ch_msg_t>(p->msgN);
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->msgA);

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;
        mbuf_t* mbuf = nullptr;
        
        if((rc = var_get(proc,kInPId,kAnyChIdx,mbuf)) != kOkRC )
          goto errLabel;

        
        for(unsigned i=0; i<mbuf->msgN; ++i)
        {
          var_set(proc, kChPId,     kAnyChIdx, mbuf->msgA[i].ch);
          var_set(proc, kStatusPId, kAnyChIdx, mbuf->msgA[i].status);
          var_set(proc, kD0PId,     kAnyChIdx, mbuf->msgA[i].d0);
          var_set(proc, kD1PId,     kAnyChIdx, mbuf->msgA[i].d1);
        }
        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // midi_file
    //
    namespace midi_file
    {
      enum {
        kMidiFileNamePId,
        kCsvFileNamePId,
        kDoneFlPId,
        kOutPId
      };
      
      typedef struct msg_str
      {
        unsigned       sample_idx;
        midi::ch_msg_t* m;
      } msg_t;
        
      typedef struct
      {
        midi::file::handle_t mfH;
        msg_t*               msgA;
        midi::ch_msg_t*      chMsgA;
        unsigned             msgN;
        unsigned             msg_idx;
        unsigned             sample_idx;
        
        char*                midi_fname;
        char*                csv_fname;
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t                           rc         = kOkRC;        
        const char*                    midi_fname = nullptr;
        const char*                    csv_fname  = nullptr;
        const midi::file::trackMsg_t** tmA        = nullptr;
        unsigned                       msgAllocN  = 0;
        bool                           done_fl    = false;
        time::spec_t                   asecs;

        time::setZero(asecs);
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kMidiFileNamePId, "fname",     kBaseSfxId, midi_fname,
                                      kCsvFileNamePId,  "csv_fname", kBaseSfxId, csv_fname,
                                      kDoneFlPId,       "done_fl",   kBaseSfxId, done_fl)) != kOkRC )
        {
          goto errLabel;
        }

        if( csv_fname != nullptr && textLength(csv_fname)>0 )
          if((p->csv_fname = proc_expand_filename(proc,csv_fname)) == nullptr )
          {
            rc = cwLogError(kInvalidArgRC,"The MIDI CSV filename could not be formed.");
            goto errLabel;
          }

        if( midi_fname != nullptr && textLength(midi_fname)>0 )
          if((p->midi_fname = proc_expand_filename(proc,midi_fname)) == nullptr )
          {
            rc = cwLogError(kInvalidArgRC,"The MIDI filename could not be formed.");
            goto errLabel;
          }

        
        if( p->midi_fname != nullptr && textLength(p->midi_fname) > 0 )
        {
          if((rc = midi::file::open(p->mfH,p->midi_fname)) != kOkRC )
            goto errLabel;
        }
        else
        {
          if( p->csv_fname != nullptr && textLength(p->csv_fname)>0 )
          {
            if((rc = midi::file::open_csv(p->mfH,p->csv_fname)) != kOkRC )
              goto errLabel;              
          }
          else
          {
            rc = cwLogError(kOpenFailRC,"No MIDI or CSV filename was given.");
          }
        }

        // create one output MIDI buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, 0  );

        tmA = msgArray(p->mfH);
        
        msgAllocN  = msgCount(p->mfH);        
        p->msgA    = mem::allocZ<msg_t>(msgAllocN);
        p->chMsgA  = mem::allocZ<midi::ch_msg_t>(msgAllocN);
        p->msg_idx = 0;


        for(unsigned i=0; i<msgAllocN; ++i)
        {
          const midi::file::trackMsg_t* tm = tmA[i];
          msg_t*                        m  = p->msgA + p->msg_idx;
          
          m->m = p->chMsgA + p->msg_idx;

          time::microsecondsToSpec( m->m->timeStamp, tmA[i]->amicro );

          m->sample_idx = (unsigned)(proc->ctx->sample_rate * time::specToSeconds(m->m->timeStamp));
          
          m->m->devIdx  = 0;
          m->m->portIdx = 0;
          m->m->uid     = tmA[i]->uid;
          
          if( midi::isChStatus(tm->status) )
          {
            m->m->status = tmA[i]->status & 0xf0; 
            m->m->ch     = tmA[i]->u.chMsgPtr->ch; 
            m->m->d0     = tmA[i]->u.chMsgPtr->d0; 
            m->m->d1     = tmA[i]->u.chMsgPtr->d1;

            //printf("%lli %f %f %i %i ch:%i st:%i d0:%i d1:%i\n",tmA[i]->amicro/1000,secs,m->sample_idx/proc->ctx->sample_rate, p->msg_idx, m->m->uid, m->m->ch, m->m->status, m->m->d0, m->m->d1);
                        
            p->msg_idx += 1;
          }
        }

        p->msgN    = p->msg_idx;
        p->msg_idx = 0;
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->midi_fname);
        mem::release(p->csv_fname);
        close(p->mfH);

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc      = kOkRC;
        mbuf_t* mbuf    = nullptr;
        bool    done_fl = false;

        p->sample_idx += proc->ctx->framesPerCycle;

        // get the output variable
        if((rc = var_get(proc,kOutPId,kAnyChIdx,mbuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The MIDI file instance '%s' does not have a valid MIDI output buffer.",proc->label);
        else
        {
          mbuf->msgA = nullptr;
          mbuf->msgN = 0;
                    
          while( p->msg_idx < p->msgN && p->sample_idx >= p->msgA[p->msg_idx].sample_idx  )
          {
            if( mbuf->msgA == nullptr )
              mbuf->msgA = p->msgA[p->msg_idx].m;
              
            mbuf->msgN += 1;

            p->msg_idx += 1;

            //printf("si:%i next:%i mi:%i\n",p->sample_idx,p->msgA[p->msg_idx].sample_idx,p->msg_idx);
            
            done_fl = p->msg_idx == p->msgN;

          }

          if( done_fl )
            var_set(proc, kDoneFlPId, kAnyChIdx, true );
          
        }
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // midi_merge
    //    
    namespace midi_merge
    {
      enum
      {
        kOutPId,
        kBufMsgCntPId,
        kBaseInPId,
      };
      
      typedef struct
      {
        external_device_t* ext_dev;
        
        unsigned           inVarN;
        
        unsigned           msgN;
        midi::ch_msg_t*    msgA;
        unsigned           msg_idx;        
      } inst_t;
      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc         = kOkRC; //
        unsigned    inVarN     = var_mult_count(proc,"in");
        
        unsigned    sfxIdA[ inVarN ];
          
        // get the the sfx_id's of the input audio variables 
        if((rc = var_mult_sfx_id_array(proc, "in", sfxIdA, inVarN, p->inVarN )) != kOkRC )
          goto errLabel;

        std::sort(sfxIdA, sfxIdA + p->inVarN, [](unsigned& a,unsigned& b){ return a<b; } );

        
        // Register variables and get their current value
        if((rc = var_register_and_get( proc, kAnyChIdx, kBufMsgCntPId,"buf_cnt", kBaseSfxId, p->msgN )) != kOkRC )
        {
          goto errLabel;
        }
        
        // Register each input var
        for(unsigned i=0; i<p->inVarN; ++i)
          if((rc = var_register( proc, kAnyChIdx, kBaseInPId+i, "in", sfxIdA[i] )) != kOkRC )
            goto errLabel;
        

        // create one output MIDI buffer
        if((rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, 0  )) != kOkRC )
        {
          goto errLabel;
        }
        
        p->msgA = mem::allocZ<midi::ch_msg_t>(p->msgN);
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        mem::release(p->msgA);
        return kOkRC;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t     rc       = kOkRC;
        mbuf_t*  out_mbuf = nullptr;
        unsigned mbufN    = 0;
        mbuf_t*  mbufA[ p->inVarN ];


        // get the output buffer
        if((rc = var_get(proc,kOutPId,kAnyChIdx,out_mbuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The MIDI merge instance '%s' does not have a valid input connection.",proc->label);
          goto errLabel;
        }
        
        // get the mbuf from each input 
        for(unsigned i=0; i<p->inVarN; ++i)
        {
          mbuf_t*  mbuf = nullptr;
          if((rc = var_get(proc, kBaseInPId+i, kAnyChIdx, mbuf)) != kOkRC )
            goto errLabel;

          // ... only store buffers that have events
          if( mbuf->msgN )
            mbufA[ mbufN++ ] = mbuf;

          assert( mbufN <= p->inVarN );
        }

        switch( mbufN )
        {
          case 0:
            // no midi events arrived
            out_mbuf->msgA = nullptr;
            out_mbuf->msgN = 0;
            break;
            
          case 1:
            // exactly one full midi buffer was found
            out_mbuf->msgA = mbufA[0]->msgA;
            out_mbuf->msgN = mbufA[0]->msgN;
            break;
            
          default:
            // multiple full midi buffers were found
            {
              unsigned i,j,k;
              for(i=0,j=0; i<mbufN && j<p->msgN; ++i)
                for(k=0; j<p->msgN && k<mbufA[i]->msgN; ++k)
                  p->msgA[j++] = mbufA[i]->msgA[k];

              std::sort(p->msgA, p->msgA + j, [](const midi::ch_msg_t& a, const midi::ch_msg_t& b){ return time::isLTE(a.timeStamp,b.timeStamp); } );
              
              out_mbuf->msgA = p->msg_idx > 0 ? p->msgA : nullptr;
              out_mbuf->msgN = p->msg_idx;          
            }
        }
        
        p->msg_idx = 0;
                
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }

    
    
  } // flow
} // cw


