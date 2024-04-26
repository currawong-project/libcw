#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"

#include "cwDspTypes.h" // srate_t, sample_t, coeff_t, ...

#include "cwTime.h"
#include "cwMidiDecls.h"


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

namespace cw
{

  namespace flow
  {
    //------------------------------------------------------------------------------------------------------------------
    //
    // Template
    //
    namespace template_proc
    {
      typedef struct
      {
        
      } inst_t;


      rc_t create( instance_t* proc )
      {
        rc_t    rc   = kOkRC;        
        inst_t* p = mem::allocZ<inst_t>();
        proc->userPtr = p;

        // Custom create code goes here

        return rc;
      }

      rc_t destroy( instance_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* p = (inst_t*)proc->userPtr;

        // Custom clean-up code goes here

        mem::release(p);
        
        return rc;
      }

      rc_t value( instance_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t exec( instance_t* proc )
      {
        rc_t rc      = kOkRC;
        //inst_t*  p = (inst_t*)proc->userPtr;
        
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


      rc_t create( instance_t* proc )
      {
        rc_t            rc          = kOkRC;        
        inst_t*         inst        = mem::allocZ<inst_t>();
        const object_t* networkCfg  = nullptr;
        const char*     order_label = nullptr;
        
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
        
        if((rc = network_create(proc->ctx,networkCfg,inst->net,inst->count )) != kOkRC )
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

      rc_t destroy( instance_t* proc )
      {
        inst_t* p = (inst_t*)proc->userPtr;
        network_destroy(p->net);

        
        mem::release( proc->userPtr );
        return kOkRC;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( instance_t* ctx )
      {
        inst_t* p = (inst_t*)ctx->userPtr;
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


      rc_t create( instance_t* ctx )
      {
        rc_t rc = kOkRC;        
        coeff_t in_value = 0.5;
        ctx->userPtr = mem::allocZ<inst_t>();
        
        if((rc  = var_register_and_get( ctx, kAnyChIdx, kInPId, "in", kBaseSfxId, in_value )) != kOkRC )
          goto errLabel;

        if((rc = var_register_and_set( ctx, kAnyChIdx,
                                       kOutPId,    "out",     kBaseSfxId, in_value,
                                       kInvOutPId, "inv_out", kBaseSfxId, (coeff_t)(1.0-in_value) )) != kOkRC )
        {
          goto errLabel;
        }
           
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        mem::release( ctx->userPtr );
        return kOkRC;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t   rc = kOkRC;
        inst_t* inst = (inst_t*)(ctx->userPtr);
        
        coeff_t value = 1;
        
        var_get(ctx, kInPId,     kAnyChIdx, value);
        var_set(ctx, kOutPId,    kAnyChIdx, value);
        var_set(ctx, kInvOutPId, kAnyChIdx, (coeff_t)(1.0 - value) );

        if( inst->value != value )
        {
          inst->value = value;
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
      
      rc_t create( instance_t* ctx )
      {
        rc_t        rc         = kOkRC;
        const char* dev_label  = nullptr;
        const char* port_label = nullptr;        
        inst_t*     inst       = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // Register variable and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kDevLabelPId,  "dev_label",  kBaseSfxId, dev_label,
                                       kPortLabelPId, "port_label", kBaseSfxId, port_label )) != kOkRC )
          
        {
          goto errLabel;
        }

        if((rc = var_register( ctx, kAnyChIdx, kOutPId, "out", kBaseSfxId)) != kOkRC )
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
          
        if( textIsEqual(dev_label,"<all>") )
        {
          inst->port_filt_fl = false;
          port_label = nullptr;
        }
        
        
        
        if((inst->ext_dev = external_device_find( ctx->ctx, dev_label, kMidiDevTypeId, kInFl, port_label )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The MIDI input device '%s' port '%s' could not be found.", cwStringNullGuard(dev_label), cwStringNullGuard(port_label));
          goto errLabel;
        }

        // Allocate a buffer large enough to hold the max. number of messages arriving on a single call to exec().
        inst->bufN = inst->ext_dev->u.m.maxMsgCnt;
        inst->buf = mem::allocZ<midi::ch_msg_t>( inst->bufN );

        // create one output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, 0  );


      errLabel: 
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        mem::release(inst->buf);

        mem::release(inst);
        
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      { return kOkRC; }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t     rc           = kOkRC;

        inst_t*  inst         = (inst_t*)ctx->userPtr;
        mbuf_t*  mbuf         = nullptr;


        // get the output variable
        if((rc = var_get(ctx,kOutPId,kAnyChIdx,mbuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The MIDI file instance '%s' does not have a valid MIDI output buffer.",ctx->label);
        }
        else
        {
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
        .create = create,
        .destroy = destroy,
        .value   = value,
        .exec = exec,
        .report = nullptr
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
        kInPId,
        kDevLabelPId,
        kPortLabelPId
      };
      
      typedef struct
      {
        external_device_t* ext_dev;
      } inst_t;
      
      rc_t create( instance_t* ctx )
      {
        rc_t        rc         = kOkRC; //
        inst_t*     inst       = mem::allocZ<inst_t>(); //
        const char* dev_label  = nullptr;
        const char* port_label = nullptr;
        mbuf_t*     mbuf       = nullptr;
        
        ctx->userPtr = inst;
        
        // Register variables and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kDevLabelPId, "dev_label",  kBaseSfxId, dev_label,
                                       kPortLabelPId,"port_label", kBaseSfxId, port_label,
                                       kInPId,       "in",         kBaseSfxId, mbuf)) != kOkRC )
        {
          goto errLabel;
        }

        if((inst->ext_dev = external_device_find( ctx->ctx, dev_label, kMidiDevTypeId, kOutFl, port_label )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The audio output device description '%s' could not be found.", cwStringNullGuard(dev_label));
          goto errLabel;
        }        

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;

        mem::release(inst);

        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const mbuf_t* src_mbuf = nullptr;

        if((rc = var_get(ctx,kInPId,kAnyChIdx,src_mbuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The MIDI output instance '%s' does not have a valid input connection.",ctx->label);
        else
        {
          for(unsigned i=0; i<src_mbuf->msgN; ++i)
          {            
            const midi::ch_msg_t* m = src_mbuf->msgA + i;
            inst->ext_dev->u.m.sendTripleFunc( inst->ext_dev, m->ch, m->status, m->d0, m->d1 );
          }
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
      
      rc_t create( instance_t* ctx )
      {
        rc_t rc = kOkRC;
        
        inst_t*                  inst = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // Register variable and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx, kDevLabelPId, "dev_label", kBaseSfxId, inst->dev_label )) != kOkRC )
        {
          goto errLabel;
        }

        if((inst->ext_dev = external_device_find( ctx->ctx, inst->dev_label, kAudioDevTypeId, kInFl )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The audio input device description '%s' could not be found.", cwStringNullGuard(inst->dev_label));
          goto errLabel;
        }
        

        // create one output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, inst->ext_dev->u.a.abuf->srate, inst->ext_dev->u.a.abuf->chN, ctx->ctx->framesPerCycle );

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;

        mem::release(inst);
        
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t     rc           = kOkRC;
        inst_t*  inst         = (inst_t*)ctx->userPtr;
        abuf_t*  abuf         = nullptr;


        // verify that a source buffer exists
        if((rc = var_get(ctx,kOutPId,kAnyChIdx,abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid audio output buffer.",ctx->label);
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
      
      rc_t create( instance_t* ctx )
      {
        rc_t          rc            = kOkRC;                 //
        inst_t*       inst          = mem::allocZ<inst_t>(); //
        const abuf_t* src_abuf      = nullptr;
        ctx->userPtr = inst;

        // Register variables and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kDevLabelPId, "dev_label", kBaseSfxId, inst->dev_label,
                                       kInPId,       "in",        kBaseSfxId, src_abuf)) != kOkRC )
        {
          goto errLabel;
        }

        if((inst->ext_dev = external_device_find( ctx->ctx, inst->dev_label, kAudioDevTypeId, kOutFl )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The audio output device description '%s' could not be found.", cwStringNullGuard(inst->dev_label));
          goto errLabel;
        }        

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;

        mem::release(inst);

        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* src_abuf = nullptr;

        if((rc = var_get(ctx,kInPId,kAnyChIdx,src_abuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid input connection.",ctx->label);
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
    // AudioFileIn
    //
    
    namespace audioFileIn
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
        const char*         filename;
      } inst_t;
      
      rc_t create( instance_t* ctx )
      {
        rc_t rc = kOkRC;
        audiofile::info_t info;
        ftime_t seekSecs;
        inst_t* inst = mem::allocZ<inst_t>();
        ctx->userPtr = inst;

        if((rc = var_register( ctx, kAnyChIdx, kOnOffFlPId, "on_off", kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        // Register variable and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kFnamePId,    "fname",    kBaseSfxId, inst->filename,
                                       kSeekSecsPId, "seekSecs", kBaseSfxId, seekSecs,
                                       kEofFlPId,    "eofFl",    kBaseSfxId, inst->eofFl )) != kOkRC )
        {
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
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, info.srate, info.chCnt, ctx->ctx->framesPerCycle );

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;

        if((rc = audiofile::close(inst->afH)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"The close failed on the audio file '%s'.", cwStringNullGuard(inst->filename) );
        }

        mem::release(inst);
        
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        ftime_t seekSecs = 0;
        inst_t* inst = (inst_t*)ctx->userPtr;

        if((rc = var_get(ctx,kSeekSecsPId,kAnyChIdx,seekSecs)) != kOkRC )
          goto errLabel;

        if((rc = seek( inst->afH, (unsigned)lround(seekSecs * audiofile::sampleRate(inst->afH) ) )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"The audio file '%s' could not seek to offset %f seconds.",seekSecs);
          goto errLabel;
        }

        
      errLabel:
        return kOkRC;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t     rc           = kOkRC;
        unsigned actualFrameN = 0;
        inst_t*  inst         = (inst_t*)ctx->userPtr;
        abuf_t*  abuf         = nullptr;
        bool     onOffFl      = false;

        // get the 'on-off; flag
        if((rc = var_get(ctx,kOnOffFlPId,kAnyChIdx,onOffFl)) != kOkRC )
          goto errLabel;

        // verify that a source buffer exists
        if((rc = var_get(ctx,kOutPId,kAnyChIdx,abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid audio output buffer.",ctx->label);
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
    // AudioFileOut
    //
    
    namespace audioFileOut
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
        const char*         filename;
        unsigned            durSmpN;
      } inst_t;
      
      rc_t create( instance_t* ctx )
      {
        rc_t          rc            = kOkRC;                 //
        unsigned      audioFileBits = 0;                     // set audio file sample format to 'float32'.
        inst_t*       inst          = mem::allocZ<inst_t>(); //
        const abuf_t* src_abuf      = nullptr;
        ctx->userPtr = inst;

        // Register variables and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kFnamePId, "fname", kBaseSfxId, inst->filename,
                                       kBitsPId,  "bits",  kBaseSfxId, audioFileBits,
                                       kInPId,    "in",    kBaseSfxId, src_abuf )) != kOkRC )
        {
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

      rc_t destroy( instance_t* ctx )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;

        // close the audio file
        if((rc = audiofile::close( inst->afH )) != kOkRC )
        {
          rc = cwLogError(rc,"Close failed on the audio output file '%s'.",inst->filename);
          goto errLabel;
        }

        mem::release(inst);

      errLabel:
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* src_abuf = nullptr;
        
        if((rc = var_get(ctx,kInPId,kAnyChIdx,src_abuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid input connection.",ctx->label);
        else
        {
          sample_t*     chBuf[ src_abuf->chN ];
        
          for(unsigned i=0; i<src_abuf->chN; ++i)
            chBuf[i] = src_abuf->buf + (i*src_abuf->frameN);
        
          if((rc = audiofile::writeFloat(inst->afH, src_abuf->frameN, src_abuf->chN, chBuf )) != kOkRC )
            rc = cwLogError(rc,"Audio file write failed on instance: '%s'.", ctx->label );

          // print a minutes counter
          inst->durSmpN += src_abuf->frameN;          
          if( inst->durSmpN % ((unsigned)src_abuf->srate*60) == 0 )
            printf("audio file out: %5.1f min\n", inst->durSmpN/(src_abuf->srate*60));
          
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

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* abuf    = nullptr; //
        ctx->userPtr = mem::allocZ<inst_t>();

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;

        // register the gain 
        for(unsigned i=0; i<abuf->chN; ++i)
          if((rc = var_register( ctx, i, kGainPId, "gain", kBaseSfxId )) != kOkRC )
            goto errLabel;
          
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );


      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        inst_t* inst = (inst_t*)(ctx->userPtr);
        mem::release(inst);
        return kOkRC;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        coeff_t value = 0;
        inst_t* inst = (inst_t*)ctx->userPtr;
        var_get(ctx,kGainPId,0,value);
        
        if( inst->vgain != value )
        {
          inst->vgain = value;
          //printf("VALUE GAIN: %s %s : %f\n", ctx->label, var->label, value );
        }
        return kOkRC;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t     rc           = kOkRC;
        const abuf_t* ibuf = nullptr;
        abuf_t*       obuf = nullptr;
        inst_t*  inst = (inst_t*)(ctx->userPtr);

        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        // for each channel
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          sample_t* isig = ibuf->buf + i*ibuf->frameN;
          sample_t* osig = obuf->buf + i*obuf->frameN;
          sample_t  gain = 1;
          
          var_get(ctx,kGainPId,i,gain);

          // apply the gain
          for(unsigned j=0; j<ibuf->frameN; ++j)
            osig[j] = gain * isig[j];

          if( i==0 && gain != inst->gain )
          {
            inst->gain = gain;
            //printf("EXEC GAIN: %s %f\n",ctx->label,gain);
            //instance_print(ctx);
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
        kGainPId,
        kOutPId,
        
      };
        
      typedef struct
      {
        bool* chSelMap;  // [ inChCnt ] selected channel map
        unsigned outChN;
      } inst_t;


      rc_t create( instance_t* ctx )
      {
        rc_t          rc   = kOkRC;        
        const abuf_t* abuf = nullptr; //        
        inst_t*       inst = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId, abuf )) != kOkRC )
          goto errLabel;

        if( abuf->chN )
        {
          unsigned selChN = 0;
          
          inst->chSelMap = mem::allocZ<bool>(abuf->chN);
          
          if((rc = var_channel_count(ctx,"select",kBaseSfxId,selChN)) != kOkRC )
            goto errLabel;
          
          // register the gain 
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            if( i < selChN )
              if((rc = var_register_and_get( ctx, i, kSelectPId, "select", kBaseSfxId, inst->chSelMap[i] )) != kOkRC )
                goto errLabel;

            if( inst->chSelMap[i] )
            {
              // register an output gain control
              if((rc = var_register( ctx, inst->outChN, kGainPId, "gain", kBaseSfxId)) != kOkRC )
                goto errLabel;

              // count the number of selected channels to determine the count of output channels
              inst->outChN += 1;
            }
          }
          
          // create the output audio buffer
          if( inst->outChN == 0 )
            cwLogWarning("The audio split instance '%s' has no selected channels.",ctx->label);
          else
            rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, inst->outChN, abuf->frameN );
        }

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;

        mem::release(inst->chSelMap);

        mem::release(inst);
        
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc       = kOkRC;
        const abuf_t* ibuf     = nullptr;
        abuf_t*       obuf     = nullptr;
        inst_t*       inst     = (inst_t*)ctx->userPtr;
        unsigned      outChIdx = 0;
        
        if( inst->outChN )
        {
          // get the src buffer
          if((rc = var_get(ctx,kInPId, kAnyChIdx, ibuf )) != kOkRC )
            goto errLabel;

          // get the dst buffer
          if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
            goto errLabel;

          // for each channel          
          for(unsigned i=0; i<ibuf->chN  && outChIdx<obuf->chN; ++i)
            if( inst->chSelMap[i] )
            {
            
              sample_t* isig = ibuf->buf + i        * ibuf->frameN;
              sample_t* osig = obuf->buf + outChIdx * obuf->frameN;
              sample_t  gain = 1;
          
              var_get(ctx,kGainPId,outChIdx,gain);

              // apply the gain
              for(unsigned j=0; j<ibuf->frameN; ++j)
                osig[j] = gain * isig[j];

              outChIdx += 1;
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


      rc_t create( instance_t* ctx )
      {
        rc_t          rc   = kOkRC;        
        const abuf_t* abuf = nullptr; //        
        inst_t*       inst = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;

        if( abuf->chN )
        {
          inst->chDuplMap = mem::allocZ<unsigned>(abuf->chN);
        
          // register the gain 
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            if((rc = var_register_and_get( ctx, i, kDuplicatePId, "duplicate", kBaseSfxId, inst->chDuplMap[i] )) != kOkRC )
              goto errLabel;

            if( inst->chDuplMap[i] )
            {
              // register an input gain control
              if((rc = var_register( ctx, inst->outChN, kGainPId, "gain", kBaseSfxId)) != kOkRC )
                goto errLabel;

              // count the number of selected channels to determine the count of output channels
              inst->outChN += inst->chDuplMap[i];
            }
          }
          
          // create the output audio buffer
          if( inst->outChN == 0 )
            cwLogWarning("The audio split instance '%s' has no selected channels.",ctx->label);
          else
            rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, inst->outChN, abuf->frameN );
        }

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;

        mem::release(inst->chDuplMap);

        mem::release(inst);
        
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc       = kOkRC;
        const abuf_t* ibuf     = nullptr;
        abuf_t*       obuf     = nullptr;
        inst_t*       inst     = (inst_t*)ctx->userPtr;
        unsigned      outChIdx = 0;
        
        if( inst->outChN )
        {
          // get the src buffer
          if((rc = var_get(ctx,kInPId, kAnyChIdx, ibuf )) != kOkRC )
            goto errLabel;

          // get the dst buffer
          if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
            goto errLabel;

          // for each input channel          
          for(unsigned i=0; i<ibuf->chN  && outChIdx<obuf->chN; ++i)
          {
            sample_t* isig = ibuf->buf + i * ibuf->frameN;
            sample_t  gain = 1;
          
            var_get(ctx,kGainPId,i,gain);
            
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
    // audio_merge
    //
    namespace audio_merge
    {
      enum {
        kGainPId,
        kOutPId,
        kInBasePId,
      };
      
      typedef struct
      {
        unsigned srcN;
      } inst_t;


      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        unsigned      outChN = 0;
        unsigned      frameN = 0;
        srate_t       srate  = 0;
        
        inst_t*       inst = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;
        
        for(unsigned i=0; 1; ++i)
        {
          const abuf_t* abuf  = nullptr; //
          
          char label[32];          
          snprintf(label,31,"in%i",i);
          label[31] = 0;

          // TODO: allow non-contiguous source labels
          
          // the source labels must be contiguous
          if( !var_has_value( ctx, label, kBaseSfxId, kAnyChIdx ) )
            break;
          
          // get the source audio buffer
          if((rc = var_register_and_get(ctx, kAnyChIdx,kInBasePId+i,label,kBaseSfxId, abuf )) != kOkRC )
          {
            goto errLabel;
          }
          
          if( i == 0 )
          {
            frameN = abuf->frameN;
            srate  = abuf->srate;
          }
          else
          {
            // TODO: check srate and frameN are same as first src
            assert( abuf->frameN == frameN );
            assert( abuf->srate  == srate );            
          }
          
          inst->srcN += 1;
          outChN += abuf->chN;
          
        }

        // register the gain 
        for(unsigned i=0; i<outChN; ++i)
          if((rc = var_register( ctx, i, kGainPId, "gain", kBaseSfxId )) != kOkRC )
            goto errLabel;
          
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srate, outChN, frameN );

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      { 
        inst_t* inst = (inst_t*)ctx->userPtr;
        
        mem::release(inst);

        return kOkRC;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      { return kOkRC; }

      unsigned _exec( instance_t* ctx, const abuf_t* ibuf, abuf_t* obuf, unsigned outChIdx )
      {
        // for each channel          
        for(unsigned i=0; i<ibuf->chN  && outChIdx<obuf->chN; ++i)
        {
            
          sample_t* isig = ibuf->buf + i        * ibuf->frameN;
          sample_t* osig = obuf->buf + outChIdx * obuf->frameN;
          sample_t  gain = 1;
          
          var_get(ctx,kGainPId,outChIdx,gain);

          // apply the gain
          for(unsigned j=0; j<ibuf->frameN; ++j)
            osig[j] = gain * isig[j];

          outChIdx += 1;
        }  

        return outChIdx;
      }

      /*
        rc_t exec( instance_t* ctx )
        {
        rc_t          rc    = kOkRC;
        const abuf_t* ibuf0 = nullptr;
        const abuf_t* ibuf1 = nullptr;
        abuf_t*       obuf  = nullptr;
        unsigned      oChIdx = 0;
        
        if((rc = var_get(ctx,kIn0PId, kAnyChIdx, ibuf0 )) != kOkRC )
        goto errLabel;

        if((rc = var_get(ctx,kIn1PId, kAnyChIdx, ibuf1 )) != kOkRC )
        goto errLabel;
        
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
        goto errLabel;

        oChIdx = _exec( ctx, ibuf0, obuf, oChIdx );
        oChIdx = _exec( ctx, ibuf1, obuf, oChIdx );

        assert( oChIdx == obuf->chN );

        errLabel:
        return rc;
        }
      */
      
      rc_t exec( instance_t* ctx )
      {
        rc_t          rc    = kOkRC;
        inst_t*       inst     = (inst_t*)ctx->userPtr;
        abuf_t*       obuf  = nullptr;
        unsigned      oChIdx = 0;
        
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        for(unsigned i=0; i<inst->srcN; ++i)
        {
          const abuf_t* ibuf = nullptr;

          if((rc = var_get(ctx,kInBasePId+i, kAnyChIdx, ibuf )) != kOkRC )
            goto errLabel;

          oChIdx = _exec( ctx, ibuf, obuf, oChIdx );
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
        kIn0PId,
        kIn1PId,
        kGain0PId,
        kGain1PId,
        kOutPId,
      };
      
      typedef struct
      {
      } inst_t;


      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* abuf0  = nullptr; //
        const abuf_t* abuf1  = nullptr;
        unsigned      outChN = 0;
        double dum;
        
        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,
                                      kIn0PId,"in0",kBaseSfxId,abuf0,
                                      kIn1PId,"in1",kBaseSfxId,abuf1 )) != kOkRC )
        {
          goto errLabel;
        }

        assert( abuf0->frameN == abuf1->frameN );

        outChN = std::max(abuf0->chN, abuf1->chN);

        // register the gain
        var_register_and_get( ctx, kAnyChIdx, kGain0PId, "gain0", kBaseSfxId, dum );
        var_register_and_get( ctx, kAnyChIdx, kGain1PId, "gain1", kBaseSfxId, dum );
          
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf0->srate, outChN, abuf0->frameN );

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      { return kOkRC; }

      rc_t value( instance_t* ctx, variable_t* var )
      { return kOkRC; }

      rc_t _mix( instance_t* ctx, unsigned inPId, unsigned gainPId, abuf_t* obuf )
      {
        rc_t          rc   = kOkRC;
        const abuf_t* ibuf = nullptr;
        
        if((rc = var_get(ctx, inPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;


        if(rc == kOkRC )
        {          
          unsigned chN = std::min(ibuf->chN, obuf->chN );
          
          for(unsigned i=0; i<chN; ++i)
          {
            const sample_t* isig = ibuf->buf + i*ibuf->frameN;
            sample_t*       osig = obuf->buf + i*obuf->frameN;
            coeff_t          gain = 1;

            if((rc = var_get(ctx, gainPId, kAnyChIdx, gain)) != kOkRC )
              goto errLabel;
            
            for(unsigned j=0; j<obuf->frameN; ++j)
              osig[j] += gain * isig[j];
          }
        }

      errLabel:
        return rc;
        
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc    = kOkRC;
        abuf_t*       obuf  = nullptr;
        //const abuf_t* ibuf0 = nullptr;
        //const abuf_t* ibuf1 = nullptr;

        if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        //if((rc = var_get(ctx,kIn0PId, kAnyChIdx, ibuf0 )) != kOkRC )
        //  goto errLabel;
        
        //if((rc = var_get(ctx,kIn1PId, kAnyChIdx, ibuf1 )) != kOkRC )
        //  goto errLabel;

        vop::zero(obuf->buf, obuf->frameN*obuf->chN );
        
        _mix( ctx, kIn0PId, kGain0PId, obuf );
        _mix( ctx, kIn1PId, kGain1PId, obuf );
        
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
      
      rc_t create( instance_t* ctx )
      {
        rc_t     rc    = kOkRC;
        inst_t*  inst  = mem::allocZ<inst_t>();
        srate_t  srate = 0;
        unsigned chCnt = 0;
        coeff_t   gain;
        coeff_t   hz;
        coeff_t   phase;
        coeff_t   dc;
        
        ctx->userPtr = inst;

        // Register variables and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx, kChCntPid, "chCnt", kBaseSfxId, chCnt)) != kOkRC )
        {
          goto errLabel;
        }

        // register each oscillator variable
        for(unsigned i=0; i<chCnt; ++i)
          if((rc = var_register_and_get( ctx, i,
                                         kSratePId,  "srate", kBaseSfxId, srate,
                                         kFreqHzPId, "hz",    kBaseSfxId, hz,
                                         kPhasePId,  "phase", kBaseSfxId, phase,
                                         kDcPId,     "dc",    kBaseSfxId, dc,
                                         kGainPId,   "gain",  kBaseSfxId, gain)) != kOkRC )
          {
            goto errLabel;
          }

        // create one output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srate, chCnt, ctx->ctx->framesPerCycle );

        inst->phaseA = mem::allocZ<double>( chCnt );
        

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;

        mem::release(inst);
        
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t     rc           = kOkRC;
        inst_t*  inst         = (inst_t*)ctx->userPtr;
        abuf_t*  abuf         = nullptr;

        // get the output signal buffer
        if((rc = var_get(ctx,kOutPId,kAnyChIdx,abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The Sine Tone instance '%s' does not have a valid audio output buffer.",ctx->label);
        }
        else
        {
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            coeff_t    gain  = val_get<coeff_t>( ctx, kGainPId, i );
            coeff_t    hz    = val_get<coeff_t>( ctx, kFreqHzPId, i );
            coeff_t    phase = val_get<coeff_t>( ctx, kPhasePId, i );
            coeff_t    dc    = val_get<coeff_t>( ctx, kDcPId, i );
            srate_t   srate = val_get<srate_t>(ctx, kSratePId, i );                        
            sample_t* v     = abuf->buf + (i*abuf->frameN);
            
            for(unsigned j=0; j<abuf->frameN; ++j)
              v[j] = (sample_t)((gain * sin( inst->phaseA[i] + phase + (2.0 * M_PI * j * hz/srate)))+dc);

            inst->phaseA[i] += 2.0 * M_PI * abuf->frameN * hz/srate;            
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
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        unsigned      flags  = 0;
        inst_t*       inst   = mem::allocZ<inst_t>();
        ctx->userPtr = inst;

        if((rc = var_register_and_get( ctx, kAnyChIdx,kInPId, "in", kBaseSfxId, srcBuf )) != kOkRC )
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
            
            if((rc = var_register_and_get( ctx, i,
                                           kMaxWndSmpNPId, "maxWndSmpN", kBaseSfxId, maxWndSmpN,
                                           kWndSmpNPId, "wndSmpN",       kBaseSfxId, wndSmpN,
                                           kHopSmpNPId, "hopSmpN",       kBaseSfxId, hopSmpN,
                                           kHzFlPId,    "hzFl",          kBaseSfxId, hzFl )) != kOkRC )
            {
              goto errLabel;
            }
            
            if((rc = create( inst->pvA[i], ctx->ctx->framesPerCycle, srcBuf->srate, maxWndSmpN, wndSmpN, hopSmpN, flags )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The PV analysis object create failed on the instance '%s'.",ctx->label);
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
          if((rc = var_register_and_set(ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, maxBinNV, binNV, hopNV, magV, phsV, hzV )) != kOkRC )
          {
            cwLogError(kOpFailRC,"The output freq. buffer could not be created.");
            goto errLabel;
          }
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        
        for(unsigned i=0; i<inst->pvN; ++i)
          destroy(inst->pvA[i]);
        
        mem::release(inst->pvA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;

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

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* srcBuf = nullptr;
        fbuf_t*       dstBuf = nullptr;

        // verify that a source buffer exists
        if((rc = var_get(ctx,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
          goto errLabel;
        }

        // verify that the dst buffer exits
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid output.",ctx->label);
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
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const fbuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        ctx->userPtr = inst;

        if((rc = var_register_and_get( ctx, kAnyChIdx,kInPId, "in", kBaseSfxId, srcBuf)) != kOkRC )
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
            
            if((rc = create( inst->pvA[i], ctx->ctx->framesPerCycle, srcBuf->srate, wndSmpN, srcBuf->hopSmpN_V[i] )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The PV synthesis object create failed on the instance '%s'.",ctx->label);
              goto errLabel;
            }
          }

          if((rc = var_register( ctx, kAnyChIdx, kInPId, "in", kBaseSfxId)) != kOkRC )
            goto errLabel;

          // create the abuf 'out'
          rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, ctx->ctx->framesPerCycle );
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->pvN; ++i)
          destroy(inst->pvA[i]);
        
        mem::release(inst->pvA);
        mem::release(inst);
        
        return rc;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const fbuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        
        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
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
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const fbuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
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

          //if((rc = var_register(ctx, kAnyChIdx, kInPId, "in")) != kOkRC )
          //  goto errLabel;
        
          // create a spec_dist object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            if((rc = create( inst->sdA[i], srcBuf->binN_V[i] )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The 'spec dist' object create failed on the instance '%s'.",ctx->label);
              goto errLabel;
            }

            // setup the output buffer pointers
            magV[i] = inst->sdA[i]->outMagV;
            phsV[i] = inst->sdA[i]->outPhsV;
            hzV[i]  = nullptr;

            spec_dist_t* sd = inst->sdA[i];

            if((rc = var_register_and_get( ctx, i,
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
          if((rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->maxBinN_V, srcBuf->binN_V, srcBuf->hopSmpN_V, magV, phsV, hzV )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->sdN; ++i)
          destroy(inst->sdA[i]);
        
        mem::release(inst->sdA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;

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
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, ctx->label );
          }

          //printf("%i sd: ceil:%f expo:%f thresh:%f upr:%f lwr:%f mix:%f : rc:%i val:%f var:%s \n",
          //       var->chIdx,sd->ceiling, sd->expo, sd->thresh, sd->uprSlope, sd->lwrSlope, sd->mix, rc, val, var->label );
        }
        
        return rc;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const fbuf_t* srcBuf = nullptr;
        fbuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
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
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
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
            if((rc = var_register_and_get( ctx, i,
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
              rc = cwLogError(kOpFailRC,"The 'compressor' object create failed on the instance '%s'.",ctx->label);
              goto errLabel;
            }
                
          }
          
          // create the output audio buffer
          if((rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->cmpN; ++i)
          destroy(inst->cmpA[i]);
        
        mem::release(inst->cmpA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;
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
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, ctx->label );
          }
          //printf("cmp byp:%i igain:%f ogain:%f rat:%f thresh:%f atk:%i rls:%i wnd:%i : rc:%i val:%f\n",
          //       c->bypassFl, c->inGain, c->outGain,c->ratio_num,c->threshDb,c->atkSmp,c->rlsSmp,c->rmsWndCnt,rc,tmp);
        }
        
        
        return rc;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->cmpN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          dsp::compressor::exec( inst->cmpA[i], srcBuf->buf + i*srcBuf->frameN, dstBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
        }

        
        

      errLabel:
        return rc;
      }

      rc_t report( instance_t* ctx )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->cmpN; ++i)
        {
          compressor_t* c = inst->cmpA[i];
          cwLogInfo("%s ch:%i : sr:%f bypass:%i procSmpN:%i igain:%f threshdb:%f ratio:%f atkSmp:%i rlsSmp:%i ogain:%f rmsWndN:%i maxRmsWndN%i",
                    ctx->label,i,c->srate,c->bypassFl,c->procSmpCnt,c->inGain,c->threshDb,c->ratio_num,c->atkSmp,c->rlsSmp,c->outGain,c->rmsWndCnt,c->rmsWndAllocCnt
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
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
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
            if((rc = var_register_and_get( ctx, i,
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
              rc = cwLogError(kOpFailRC,"The 'limiter' object create failed on the instance '%s'.",ctx->label);
              goto errLabel;
            }
                
          }
          
          // create the output audio buffer
          if((rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->limN; ++i)
          destroy(inst->limA[i]);
        
        mem::release(inst->limA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t    rc   = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;
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
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, ctx->label );
          }
          //printf("lim byp:%i igain:%f ogain:%f rat:%f thresh:%f atk:%i rls:%i wnd:%i : rc:%i val:%f\n",
          //       c->bypassFl, c->inGain, c->outGain,c->ratio_num,c->threshDb,c->atkSmp,c->rlsSmp,c->rmsWndCnt,rc,tmp);
        }
        
        
        return rc;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->limN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          dsp::limiter::exec( inst->limA[i], srcBuf->buf + i*srcBuf->frameN, dstBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
        }

        
        

      errLabel:
        return rc;
      }

      rc_t report( instance_t* ctx )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->limN; ++i)
        {
          limiter_t* c = inst->limA[i];
          cwLogInfo("%s ch:%i : bypass:%i procSmpN:%i igain:%f threshdb:%f  ogain:%f",
                    ctx->label,i,c->bypassFl,c->procSmpCnt,c->igain,c->thresh,c->ogain );
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

      rc_t create( instance_t* ctx )
      {
        rc_t          rc         = kOkRC;
        const abuf_t* abuf       = nullptr; //
        inst_t*       inst       = mem::allocZ<inst_t>();
        ftime_t        delayMs    = 0;
        ftime_t        maxDelayMs = 0;

        ctx->userPtr = inst;

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;


        inst->cntV = mem::allocZ<unsigned>(abuf->chN);
        inst->idxV = mem::allocZ<unsigned>(abuf->chN);
        
        // register the gain 
        for(unsigned i=0; i<abuf->chN; ++i)
        {
          if((rc = var_register_and_get( ctx, i,
                                         kMaxDelayMsPId, "maxDelayMs", kBaseSfxId, maxDelayMs,
                                         kDelayMsPId,    "delayMs",    kBaseSfxId, delayMs)) != kOkRC )
          {
            goto errLabel;
          }

          if( delayMs > maxDelayMs )
          {
            cwLogWarning("'delayMs' (%i) is being reduced to 'maxDelayMs' (%i) on the delay instance:%s.",delayMs,maxDelayMs,ctx->label);
            delayMs = maxDelayMs;
          }

          inst->maxDelayFrameN = std::max(inst->maxDelayFrameN, (unsigned)(fabs(maxDelayMs) * abuf->srate / 1000.0) );

          inst->cntV[i] = (unsigned)(fabs(delayMs) * abuf->srate / 1000.0);
                                  
        }

        inst->delayBuf = abuf_create( abuf->srate, abuf->chN, inst->maxDelayFrameN );
        
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );


      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        inst_t* inst = (inst_t*)ctx->userPtr;

        mem::release(inst->cntV);
        mem::release(inst->idxV);
        abuf_destroy(inst->delayBuf);
        mem::release(inst);
        
        return kOkRC;
      }

      rc_t _update_delay( instance_t* ctx, variable_t* var )
      {
        rc_t     rc          = kOkRC;
        inst_t*  inst        = (inst_t*)ctx->userPtr;
        abuf_t*  ibuf        = nullptr;
        ftime_t   delayMs     = 0;
        unsigned delayFrameN = 0;
        
        if((rc = var_get(ctx,kInPId, kAnyChIdx, ibuf )) != kOkRC )
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

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        
        switch( var->vid )
        {
          case kDelayMsPId:
            rc = _update_delay(ctx,var);
            break;
        }

        return rc;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc   = kOkRC;
        inst_t*       inst = (inst_t*)ctx->userPtr;
        const abuf_t* ibuf = nullptr;
        abuf_t*       obuf = nullptr;
        abuf_t*       dbuf = inst->delayBuf;

        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
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
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
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
            if((rc = var_register_and_get( ctx, i,
                                           kBypassPId,   "bypass",    kBaseSfxId, bypassFl,
                                           kGainPId,     "gain",      kBaseSfxId, gain )) != kOkRC )
            {
              goto errLabel;
            }

            // create the dc_filter instance
            if((rc = dsp::dc_filter::create( inst->dcfA[i], srcBuf->srate, srcBuf->frameN, gain, bypassFl)) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The 'dc_filter' object create failed on the instance '%s'.",ctx->label);
              goto errLabel;
            }
                
          }
          
          // create the output audio buffer
          if((rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
            goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->dcfN; ++i)
          destroy(inst->dcfA[i]);
        
        mem::release(inst->dcfA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( instance_t* ctx, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->dcfN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          coeff_t gain   = val_get<coeff_t>( ctx, kGainPId,   i );
          bool bypassFl = val_get<bool>(   ctx, kBypassPId, i );

          dsp::dc_filter::set( inst->dcfA[i], gain, bypassFl );
          
          dsp::dc_filter::exec( inst->dcfA[i], srcBuf->buf + i*srcBuf->frameN, dstBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
        }

      errLabel:
        return rc;
      }

      rc_t report( instance_t* ctx )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->dcfN; ++i)
        {
          dc_filter_t* c = inst->dcfA[i];
          cwLogInfo("%s ch:%i : bypass:%i gain:%f",
                    ctx->label,i,c->bypassFl,c->gain );
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
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
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
            if((rc = var_register_and_get( ctx, i,
                                           kDbFlPId,   "dbFl",    kBaseSfxId, dbFl,
                                           kWndMsPId, "wndMs",    kBaseSfxId, wndMs,
                                           kPeakDbPId, "peakDb",  kBaseSfxId, peakThreshDb )) != kOkRC )
            {
              goto errLabel;
            }

            // get the audio_meter variable values
            if((rc = var_register( ctx, i,
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
              rc = cwLogError(kOpFailRC,"The 'audio_meter' object create failed on the instance '%s'.",ctx->label);
              goto errLabel;
            }
                
          }
          
        }
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->mtrN; ++i)
          destroy(inst->mtrA[i]);
        
        mem::release(inst->mtrA);
        mem::release(inst);
        
        return rc;
      }
      
      rc_t value( instance_t* ctx, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* srcBuf = nullptr;
        unsigned      chN    = 0;
        
        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        chN = std::min(srcBuf->chN,inst->mtrN);
       
        for(unsigned i=0; i<chN; ++i)
        {
          dsp::audio_meter::exec( inst->mtrA[i], srcBuf->buf + i*srcBuf->frameN, srcBuf->frameN );
          var_set(ctx, kOutPId,    i, inst->mtrA[i]->outDb  );
          var_set(ctx, kPeakFlPId, i, inst->mtrA[i]->peakFl );
          var_set(ctx, kClipFlPId, i, inst->mtrA[i]->clipFl );
        }

      errLabel:
        return rc;
      }

      rc_t report( instance_t* ctx )
      {
        rc_t rc = kOkRC;
        inst_t* inst = (inst_t*)ctx->userPtr;
        for(unsigned i=0; i<inst->mtrN; ++i)
        {
          audio_meter_t* c = inst->mtrA[i];
          cwLogInfo("%s ch:%i : %f %f db : pk:%i %i clip:%i %i ",
                    ctx->label,i,c->outLin,c->outDb,c->peakFl,c->peakCnt,c->clipFl,c->clipCnt );
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
    // audio_marker
    //
    namespace audio_marker
    {
      enum
      {
        kInPId,
        kMarkPId,
        kOutPId
      };

      typedef struct inst_str
      {
        sample_t mark;
      } inst_t;

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* abuf    = nullptr; //
        ctx->userPtr = mem::allocZ<inst_t>();

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",kBaseSfxId,abuf )) != kOkRC )
          goto errLabel;

        // register the marker input 
        if((rc = var_register_and_set( ctx, kAnyChIdx, kMarkPId, "mark", kBaseSfxId, 0.0f )) != kOkRC )
          goto errLabel;
          
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        inst_t* inst = (inst_t*)(ctx->userPtr);
        mem::release(inst);
        return kOkRC;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        return kOkRC;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc   = kOkRC;
        const abuf_t* ibuf = nullptr;
        abuf_t*       obuf = nullptr;
        //inst_t*       inst = (inst_t*)(ctx->userPtr);
        sample_t      mark = 1;

        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

          
        var_get(ctx,kMarkPId,kAnyChIdx,mark);
        
        // for each channel
        for(unsigned i=0; i<ibuf->chN; ++i)
        {
          sample_t* isig = ibuf->buf + i*ibuf->frameN;
          sample_t* osig = obuf->buf + i*obuf->frameN;

          // apply the marker
          for(unsigned j=0; j<ibuf->frameN; ++j)
            osig[j] = mark + isig[j];
        }

        var_set(ctx,kMarkPId,kAnyChIdx,0.0f);
        
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
        kGainPId,
      };
      
      typedef struct
      {
        unsigned    xfadeDurMs;        // crossfade duration in milliseconds
        instance_t* net_proc;          // source 'poly' network
        network_t   net;               // internal proxy network 
        unsigned    poly_ch_cnt;      // set to 2 (one for 'cur' poly-ch., one for 'next' poly-ch.)
        unsigned    net_proc_cnt;      // count of proc's in a single poly-channel (net_proc->proc_arrayN/poly_cnt)
        unsigned    cur_poly_ch_idx;  // 
        unsigned    next_poly_ch_idx; //
        float*      target_gainA;     // target_gainA[net_proc->poly_cnt]
        float*      cur_gainA;        // cur_gainA[net_proc->poly_cnt]
        double      srate;
        
      } inst_t;

      void _trigger_xfade( inst_t* p )
      {
        // begin fading out the cur channel
        p->target_gainA[ p->cur_poly_ch_idx ] = 0;
        
        // the next poly-ch become the cur poly-ch
        p->cur_poly_ch_idx  = p->next_poly_ch_idx;

        // the next poly-ch advances
        p->next_poly_ch_idx = p->next_poly_ch_idx+1 >= p->poly_ch_cnt ? 0 : p->next_poly_ch_idx+1;

        // j selects a block of 'net_proc_cnt' slots in the proxy network which will become the 'next' channel
        unsigned j = p->next_poly_ch_idx * p->net_proc_cnt;

        // set the [j:j+poly_proc_cnt] pointers in the proxy net to the actual proc instances in the source net
        for(unsigned i=0; i<p->net_proc->internal_net->proc_arrayN; ++i)
          if( p->net_proc->internal_net->proc_array[i]->label_sfx_id == p->next_poly_ch_idx )
          {
            assert( p->next_poly_ch_idx * p->net_proc_cnt <= j
                    && j < p->next_poly_ch_idx * p->net_proc_cnt + p->net_proc_cnt
                    && j < p->net.proc_arrayN );
            
            p->net.proc_array[j++] = p->net_proc->internal_net->proc_array[i];
          }

        // begin fading in the new cur channel
        p->target_gainA[ p->cur_poly_ch_idx ] = 1;

        // if the next channel is not already at 0 send it in that direction
        p->target_gainA[ p->next_poly_ch_idx ] = 0; 

      }



      rc_t create( instance_t* ctx )
      {
        rc_t        rc            = kOkRC;
        const char* netLabel      = nullptr;
        unsigned    netLabelSfxId = kBaseSfxId;
        bool        trigFl        = false;
        variable_t* gainVar       = nullptr;
        abuf_t*     srateSrc      = nullptr;
        double      dum;

        inst_t* p = mem::allocZ<inst_t>();

        ctx->userPtr = p;
        
        p->poly_ch_cnt = 2;
        
        if((rc = var_register_and_get(ctx,kAnyChIdx,
                                      kNetLabelPId,       "net",       kBaseSfxId, netLabel,
                                      kNetLabelSfxPId,    "netSfxId",  kBaseSfxId, netLabelSfxId,
                                      kSrateRefPId,       "srateSrc",  kBaseSfxId, srateSrc,
                                      kDurMsPId,          "durMs",     kBaseSfxId, p->xfadeDurMs,
                                      kTriggerPId,         "trigger",  kBaseSfxId, trigFl,
                                      kGainPId,            "gain",     kBaseSfxId, dum)) != kOkRC )
        {
          goto errLabel; 
        }

        // locate the source poly-network for this xfad-ctl
        if((rc = instance_find(*ctx->net,netLabel,netLabelSfxId,p->net_proc)) != kOkRC )
        {
          cwLogError(rc,"The xfade_ctl source network proc instance '%s:%i' was not found.",cwStringNullGuard(netLabel),netLabelSfxId);
          goto errLabel;
        }

        if( p->net_proc->internal_net->poly_cnt < 3 )
        {
          cwLogError(rc,"The xfade_ctl source network must have at least 3 poly channels. %i < 3",p->net_proc->internal_net->poly_cnt);
          goto errLabel;
        }
          

        // create the gain output variables - one output for each poly-channel
        for(unsigned i=1; i<p->net_proc->internal_net->poly_cnt; ++i)
        {
          variable_t* dum;
          if((rc = var_create(ctx, "gain", i, kGainPId+i, kAnyChIdx, nullptr, dum )) != kOkRC )
          {
            cwLogError(rc,"'gain:%i' create failed.",i);
            goto errLabel;
          }
        }

        // count of proc's in one poly-ch of the poly network
        p->net_proc_cnt = p->net_proc->internal_net->proc_arrayN / p->net_proc->internal_net->poly_cnt;

        // create the proxy network
        p->net.proc_arrayAllocN = p->net_proc_cnt * p->poly_ch_cnt;
        p->net.proc_arrayN = p->net.proc_arrayAllocN;
        p->net.proc_array  = mem::allocZ<instance_t*>(p->net.proc_arrayAllocN);
        p->target_gainA    = mem::allocZ<float>(p->net_proc->internal_net->poly_cnt);
        p->cur_gainA       = mem::allocZ<float>(p->net_proc->internal_net->poly_cnt);
        p->srate           = srateSrc->srate;
        
        // make the proxy network public - xfad_ctl now looks like the source network
        // because it has the same proc instances
        ctx->internal_net = &p->net;

        // setup the channels such that the first active channel after _trigger_xfade()
        // will be channel 0
        p->cur_poly_ch_idx  = 1;
        p->next_poly_ch_idx = 2;
        _trigger_xfade(p);  // cur=2 nxt=0 initialize inst ptrs in range: p->net[0:net_proc_cnt]
        _trigger_xfade(p);  // cur=0 nxt=1 initialize inst ptrs in range: p->net[net_proc_cnt:2*net_proc_cnt]


        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        inst_t* p = (inst_t*)ctx->userPtr;
        mem::release(p->net.proc_array);
        mem::release(p->target_gainA);
        mem::release(p->cur_gainA);
        mem::release(ctx->userPtr);
        
        return kOkRC;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      { return kOkRC; }

      // return sign of expression as a float
      float _signum( float v ) { return (0.0f < v) - (v < 0.0f); }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t    rc     = kOkRC;
        inst_t* p      = (inst_t*)ctx->userPtr;
        bool    trigFl = false;

        // check if a cross-fade has been triggered
        if((rc = var_get(ctx,kTriggerPId,kAnyChIdx,trigFl)) == kOkRC )
        {
          _trigger_xfade(p);
          
          var_set(ctx,kTriggerPId,kAnyChIdx,false);
        }
        
        // time in sample frames to complete a xfade
        double xfade_dur_smp        = p->xfadeDurMs * p->srate / 1000.0;

        // fraction of a xfade which will be completed in on exec() cycle
        float delta_gain_per_cycle =  (float)(ctx->ctx->framesPerCycle / xfade_dur_smp);

        // update the cross-fade gain outputs
        for(unsigned i=0; i<p->net_proc->internal_net->poly_cnt; ++i)
        {
          p->cur_gainA[i] += _signum(p->target_gainA[i] - p->cur_gainA[i]) * delta_gain_per_cycle;
          
          p->cur_gainA[i] = std::min(1.0f, std::max(0.0f, p->cur_gainA[i]));
          
          var_set(ctx,kGainPId+i,kAnyChIdx,p->cur_gainA[i]);
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
    // poly_mixer
    //
    namespace poly_mixer
    {
      enum {
        kOutGainPId,
        kOutPId,
        
      };
      
      typedef struct
      {
        unsigned inBaseVId;
        unsigned gainBaseVId;
      } inst_t;


      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        /*
        const abuf_t* abuf0  = nullptr; //
        const abuf_t* abuf1  = nullptr;
        unsigned      outChN = 0;
        double dum;
        
        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,
                                      kIn0PId,"in0",kBaseSfxId,abuf0,
                                      kIn1PId,"in1",kBaseSfxId,abuf1 )) != kOkRC )
        {
          goto errLabel;
        }

        assert( abuf0->frameN == abuf1->frameN );

        outChN = std::max(abuf0->chN, abuf1->chN);

        // register the gain
        var_register_and_get( ctx, kAnyChIdx, kGain0PId, "gain0", kBaseSfxId, dum );
        var_register_and_get( ctx, kAnyChIdx, kGain1PId, "gain1", kBaseSfxId, dum );

        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kBaseSfxId, kOutPId, kAnyChIdx, abuf0->srate, outChN, abuf0->frameN );
        */
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* ctx )
      { return kOkRC; }

      rc_t value( instance_t* ctx, variable_t* var )
      { return kOkRC; }

      
      rc_t exec( instance_t* ctx )
      {
        rc_t          rc    = kOkRC;
        /*
        abuf_t*       obuf  = nullptr;
        //const abuf_t* ibuf0 = nullptr;
        //const abuf_t* ibuf1 = nullptr;

        if((rc = var_get(ctx,kOutPId, kAnyChIdx, obuf)) != kOkRC )
          goto errLabel;

        //if((rc = var_get(ctx,kIn0PId, kAnyChIdx, ibuf0 )) != kOkRC )
        //  goto errLabel;
        
        //if((rc = var_get(ctx,kIn1PId, kAnyChIdx, ibuf1 )) != kOkRC )
        //  goto errLabel;

        vop::zero(obuf->buf, obuf->frameN*obuf->chN );
        
        _mix( ctx, kIn0PId, kGain0PId, obuf );
        _mix( ctx, kIn1PId, kGain1PId, obuf );
        */
        
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
      
      rc_t create( instance_t* ctx )
      {
        rc_t          rc       = kOkRC;
        const abuf_t* abuf     = nullptr; //
        double        periodMs = 0;
        
        ctx->userPtr = mem::allocZ<inst_t>();
        inst_t* p = (inst_t*)ctx->userPtr;

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,
                                      kInPId,       "in",       kBaseSfxId, abuf,
                                      kPeriodMsPId, "period_ms",kBaseSfxId, periodMs)) != kOkRC )
        {
          goto errLabel;
        }
        
        p->chN          = abuf->chN;
        p->bufAllocFrmN = _period_ms_to_smp( abuf->srate, ctx->ctx->framesPerCycle, periodMs );
        p->periodFrmN   = p->bufAllocFrmN;
        p->buf          = mem::allocZ<sample_t*>(abuf->chN);

        for(unsigned i=0; i<abuf->chN; ++i)
        {
          p->buf[i] = mem::allocZ<sample_t>(p->bufAllocFrmN);
          if((rc = var_register_and_set(ctx, i,
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

      rc_t destroy( instance_t* ctx )
      {
        inst_t* p = (inst_t*)(ctx->userPtr);
        _destroy(p);
        return kOkRC;
      }

      rc_t value( instance_t* ctx, variable_t* var )
      {
        rc_t rc = kOkRC;
        
        switch( var->vid )
        {
          case kPeriodMsPId:
            {
              double periodMs;
              const abuf_t* abuf;
              inst_t*  p = (inst_t*)(ctx->userPtr);
                      
              var_get(ctx,kInPId,kAnyChIdx,abuf);
              
              if((rc = var_get(var,periodMs)) == kOkRC )
              {
                p->periodFrmN = _period_ms_to_smp( abuf->srate, ctx->ctx->framesPerCycle, p->bufAllocFrmN, periodMs );
              }
            }
            break;
            
          default:
            break;
            
        }
        
        return rc;
      }

      rc_t exec( instance_t* ctx )
      {
        rc_t          rc   = kOkRC;
        const abuf_t* ibuf = nullptr;
        inst_t*       p    = (inst_t*)(ctx->userPtr);
        unsigned      chN  = 0;
        unsigned      oi   = 0;
        unsigned      n0   = 0;
       unsigned       n1   = 0;

        // get the src buffer
        if((rc = var_get(ctx,kInPId, kAnyChIdx, ibuf )) != kOkRC )
          goto errLabel;

        chN = std::min(ibuf->chN,p->chN);
        
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
          var_set(ctx,kOutPId,i, p->buf[i][oi] );

          if( var_is_a_source(ctx,kMeanPId,i) )
            var_set(ctx,kMeanPId,i, _mean(p,i,oi,n0,n1));
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
        kInPId,
        kBoolPId,
        kUIntPId,
        kIntPId,
        kFloatPId,
        kOutPId
      };
      
      typedef struct
      {
        bool delta_fl;
        double value;
      } inst_t;


      rc_t create( instance_t* proc )
      {
        rc_t    rc   = kOkRC;        
        inst_t* p = mem::allocZ<inst_t>();
        proc->userPtr = p;

        if((rc  = var_register_and_get(proc,kAnyChIdx,
                                       kInPId,"in",kBaseSfxId,p->value)) != kOkRC )
        {
          goto errLabel;
        }
        
        if((rc = var_register_and_set(proc,kAnyChIdx,
                                      kBoolPId,"bool",kBaseSfxId,p->value != 0,
                                      kUIntPId,"uint",kBaseSfxId,(unsigned)p->value,
                                      kIntPId,"int",kBaseSfxId,(int)p->value,
                                      kFloatPId,"float",kBaseSfxId,(float)p->value,
                                      kOutPId,"out",kBaseSfxId,p->value )) != kOkRC )
        {
          goto errLabel;
        }
        
        p->delta_fl = true;

      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* p = (inst_t*)proc->userPtr;
        mem::release(p);        
        return rc;
      }

      rc_t value( instance_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        if( var->vid == kInPId )
        {
          double v;
          if((rc = var_get(var,v)) == kOkRC )
          {
            inst_t* p = (inst_t*)proc->userPtr;
            if( !p->delta_fl )
              p->delta_fl = v != p->value;
            p->value    = v;
            
          }
        }
        return rc;
      }

      rc_t exec( instance_t* proc )
      {
        rc_t rc      = kOkRC;
        inst_t*  p = (inst_t*)proc->userPtr;

        if( p->delta_fl )
        {
          p->delta_fl = false;
          var_set(proc,kBoolPId,kAnyChIdx,p->value!=0);
          var_set(proc,kUIntPId,kAnyChIdx,(unsigned)fabs(p->value));
          var_set(proc,kIntPId,kAnyChIdx,(int)p->value);
          var_set(proc,kFloatPId,kAnyChIdx,(float)p->value);
          var_set(proc,kOutPId,kAnyChIdx,p->value);
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
    // Timer
    //
    namespace timer
    {
      enum {
        kSratePId,
        kPeriodMsPId,
        kOutPId,
      };
      
      typedef struct
      {
        unsigned periodFrmN;
        unsigned periodPhase;
      } inst_t;

      unsigned _period_ms_to_frame_count( instance_t* proc, inst_t* p, srate_t srate, ftime_t periodMs )
      {
        return std::max((unsigned)(srate * periodMs / 1000.0), proc->ctx->framesPerCycle);
      }

      rc_t create( instance_t* proc )
      {
        rc_t    rc       = kOkRC;
        ftime_t  periodMs = 0;
        srate_t srate    = 0;
        inst_t* p        = mem::allocZ<inst_t>();
        proc->userPtr    = p;
        

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kSratePId,    "srate",    kBaseSfxId,srate,
                                      kPeriodMsPId, "period_ms",kBaseSfxId,periodMs)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register_and_set(proc,kAnyChIdx,
                                      kOutPId,       "out",     kBaseSfxId,false)) != kOkRC )
        {
          goto errLabel;
        }
        
        p->periodFrmN = _period_ms_to_frame_count(proc,p,srate,periodMs);
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* proc )
      {
        rc_t    rc = kOkRC;
        inst_t* p  = (inst_t*)proc->userPtr;
        mem::release(p);        
        return rc;
      }

      rc_t value( instance_t* proc, variable_t* var )
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
                p->periodFrmN = _period_ms_to_frame_count( proc, p, srate, periodMs );
            }
            break;
            
          default:
            break;
        }
        return rc;
      }

      rc_t exec( instance_t* proc )
      {
        rc_t    rc = kOkRC;
        inst_t* p  = (inst_t*)proc->userPtr;

        p->periodPhase += proc->ctx->framesPerCycle;

        
        if( p->periodPhase >= p->periodFrmN )
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

        bool reset_val;
        bool reset_fl;

        bool done_fl;

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
        rc_t rc = kOkRC;
        mode_id_ref = kInvalidId;
        for(unsigned i=0; modeArray[i].id != kInvalidId; ++i)
          if( textIsEqual(modeArray[i].label,mode_label) )
          {
            mode_id_ref = modeArray[i].id;
            return kOkRC;            
          }
        
        return cwLogError(kInvalidArgRC,"'%s' is not a valid counter 'mode'.",cwStringNullGuard(mode_label));
      }

      rc_t create( instance_t* proc )
      {
        rc_t    rc    = kOkRC;        
        inst_t* p     = mem::allocZ<inst_t>();
        proc->userPtr = p;
        double  init_val;
        const char* mode_label;

        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kTriggerPId, "trigger", kBaseSfxId, p->trig_val,
                                      kResetPId,   "reset",   kBaseSfxId, p->reset_val,
                                      kInitPId,    "init",    kBaseSfxId, init_val,
                                      kModePId,    "mode",    kBaseSfxId, mode_label)) != kOkRC )
        {
          goto errLabel;
        }
                                      
        
        if((rc = var_register(proc, kAnyChIdx,
                              kMinPId,     "min",     kBaseSfxId,
                              kMaxPId,     "max",     kBaseSfxId,
                              kIncPId,     "inc",     kBaseSfxId,
                              kRepeatPId,  "repeat_fl",  kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register_and_set(proc,kAnyChIdx,
                                      kOutPId,"out",kBaseSfxId,init_val)) != kOkRC )
        {
          goto errLabel;
        }
                                      

        if((rc = _string_to_mode_id(mode_label,p->mode_id)) != kOkRC )
          goto errLabel;
        
        p->dir = 1.0;
        
      errLabel:
        return rc;
      }

      rc_t destroy( instance_t* proc )
      {
        rc_t rc = kOkRC;

        inst_t* p = (inst_t*)proc->userPtr;
        mem::release(p);
        
        return rc;
      }

      rc_t value( instance_t* proc, variable_t* var )
      {
        rc_t rc = kOkRC;
        inst_t* p = (inst_t*)proc->userPtr;

        switch( var->vid )
        {
          case kTriggerPId:
            {
              bool v;
              
              if((rc = var_get(var,v)) == kOkRC )
              {
                if( !p->delta_fl )
                  p->delta_fl = p->trig_val != v;
                
                p->trig_val = v;            
              }
            }
            break;

          case kModePId:
            {
              const char* s;
              if((rc = var_get(var,s)) == kOkRC )
                rc = _string_to_mode_id(s,p->mode_id);
            }
            break;
              
        }
        
        return rc;
      }

      rc_t exec( instance_t* proc )
      {
        rc_t rc      = kOkRC;
        inst_t* p = (inst_t*)proc->userPtr;

        
        bool v;
        if((rc = var_get(proc,kTriggerPId,kAnyChIdx,v)) != kOkRC )
        {
          cwLogError(rc,"Fail!");
          goto errLabel;
        }

        p->delta_fl = v != p->trig_val;
        p->trig_val = v;

        if( p->delta_fl )
        {
          p->delta_fl = false;
          
          double cnt,inc,minv,maxv;
          var_get(proc,kOutPId,kAnyChIdx,cnt);
          var_get(proc,kIncPId,kAnyChIdx,inc);
          var_get(proc,kMinPId,kAnyChIdx,minv);
          var_get(proc,kMaxPId,kAnyChIdx,maxv);

          double incr = p->dir * inc;
          cnt += incr;

          if( minv > cnt || cnt > maxv )
          {
            bool repeat_fl;
            var_get(proc,kRepeatPId,kAnyChIdx,repeat_fl);

            if( !repeat_fl )
              p->done_fl = true;
            else
            {
              if( cnt > maxv)
              {
                switch( p->mode_id )
                {
                  case kModuloModeId:
                    while(cnt > maxv )
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

          if( !p->done_fl )
          {
            printf("cnt:%f\n",cnt);
            var_set(proc,kOutPId,kAnyChIdx,cnt);
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
    
  } // flow
} // cw


