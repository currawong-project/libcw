#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"

#include "cwDspTypes.h" // real_t, sample_t
#include "cwFlow.h"
#include "cwFlowTypes.h"
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


      rc_t create( instance_t* ctx )
      {
        rc_t    rc   = kOkRC;        
        inst_t* inst = mem::allocZ<inst_t>();
        ctx->userPtr = inst;

        // Custom create code goes here

        return rc;
      }

      rc_t destroy( instance_t* ctx )
      {
        rc_t rc = kOkRC;

        inst_t* inst = (inst_t*)ctx->userPtr;

        // Custom clean-up code goes here

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
        //inst_t*  inst         = (inst_t*)ctx->userPtr;
        
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
        real_t value; 
      } inst_t;


      rc_t create( instance_t* ctx )
      {
        rc_t rc = kOkRC;        
        real_t in_value = 0.5;
        ctx->userPtr = mem::allocZ<inst_t>();
        
        if((rc  = var_register_and_get( ctx, kAnyChIdx, kInPId, "in", in_value )) != kOkRC )
          goto errLabel;

        if((rc = var_register_and_set( ctx, kAnyChIdx,
                                       kOutPId,    "out", in_value,
                                       kInvOutPId, "inv_out", (real_t)(1.0-in_value) )) != kOkRC )
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
        
        real_t value = 1;
        
        var_get(ctx, kInPId,     kAnyChIdx, value);
        var_set(ctx, kOutPId,    kAnyChIdx, value);
        var_set(ctx, kInvOutPId, kAnyChIdx, (real_t)(1.0 - value) );

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
        if((rc = var_register_and_get( ctx, kAnyChIdx, kDevLabelPId, "dev_label", inst->dev_label )) != kOkRC )
        {
          goto errLabel;
        }

        if((inst->ext_dev = external_device_find( ctx->ctx, inst->dev_label, kAudioDevTypeId, kInFl )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The audio input device description '%s' could not be found.", cwStringNullGuard(inst->dev_label));
          goto errLabel;
        }
        

        // create one output audio buffer
        rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, inst->ext_dev->u.a.abuf->srate, inst->ext_dev->u.a.abuf->chN, ctx->ctx->framesPerCycle );

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
                                       kDevLabelPId, "dev_label", inst->dev_label,
                                       kInPId,       "in",        src_abuf)) != kOkRC )
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
          unsigned n = chN * frameN;
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
        real_t seekSecs;
        inst_t* inst = mem::allocZ<inst_t>();
        ctx->userPtr = inst;

        if((rc = var_register( ctx, kAnyChIdx, kOnOffFlPId, "on_off" )) != kOkRC )
        {
          goto errLabel;
        }

        // Register variable and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kFnamePId, "fname", inst->filename,
                                       kSeekSecsPId, "seekSecs", seekSecs,
                                       kEofFlPId, "eofFl", inst->eofFl )) != kOkRC )
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
        rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, info.srate, info.chCnt, ctx->ctx->framesPerCycle );

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
        real_t seekSecs = 0;
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
                                       kFnamePId, "fname", inst->filename,
                                       kBitsPId,  "bits",  audioFileBits,
                                       kInPId,    "in",    src_abuf )) != kOkRC )
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
        real_t vgain;
        real_t gain;
      } inst_t;

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const abuf_t* abuf    = nullptr; //
        ctx->userPtr = mem::allocZ<inst_t>();

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",abuf )) != kOkRC )
          goto errLabel;

        // register the gain 
        for(unsigned i=0; i<abuf->chN; ++i)
          if((rc = var_register( ctx, i, kGainPId, "gain" )) != kOkRC )
            goto errLabel;
          
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );


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
        real_t value = 0;
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
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",abuf )) != kOkRC )
          goto errLabel;

        if( abuf->chN )
        {
          unsigned selChN = 0;
          
          inst->chSelMap = mem::allocZ<bool>(abuf->chN);
          
          if((rc = var_channel_count(ctx,"select",selChN)) != kOkRC )
            goto errLabel;
          
          // register the gain 
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            if( i < selChN )
              if((rc = var_register_and_get( ctx, i, kSelectPId, "select", inst->chSelMap[i] )) != kOkRC )
                goto errLabel;

            if( inst->chSelMap[i] )
            {
              // register an output gain control
              if((rc = var_register( ctx, inst->outChN, kGainPId, "gain")) != kOkRC )
                goto errLabel;

              // count the number of selected channels to determine the count of output channels
              inst->outChN += 1;
            }
          }
          
          // create the output audio buffer
          if( inst->outChN == 0 )
            cwLogWarning("The audio split instance '%s' has no selected channels.",ctx->label);
          else
            rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, abuf->srate, inst->outChN, abuf->frameN );
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
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",abuf )) != kOkRC )
          goto errLabel;

        if( abuf->chN )
        {
          inst->chDuplMap = mem::allocZ<unsigned>(abuf->chN);
        
          // register the gain 
          for(unsigned i=0; i<abuf->chN; ++i)
          {
            if((rc = var_register_and_get( ctx, i, kDuplicatePId, "duplicate", inst->chDuplMap[i] )) != kOkRC )
              goto errLabel;

            if( inst->chDuplMap[i] )
            {
              // register an input gain control
              if((rc = var_register( ctx, inst->outChN, kGainPId, "gain")) != kOkRC )
                goto errLabel;

              // count the number of selected channels to determine the count of output channels
              inst->outChN += inst->chDuplMap[i];
            }
          }
          
          // create the output audio buffer
          if( inst->outChN == 0 )
            cwLogWarning("The audio split instance '%s' has no selected channels.",ctx->label);
          else
            rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, abuf->srate, inst->outChN, abuf->frameN );
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
          if( !var_has_value( ctx, label, kAnyChIdx ) )
            break;
          
          // get the source audio buffer
          if((rc = var_register_and_get(ctx, kAnyChIdx,kInBasePId+i,label,abuf )) != kOkRC )
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
          }
          
          inst->srcN += 1;
          outChN += abuf->chN;
          
        }

        //outChN = abuf0->chN + abuf1->chN;

        // register the gain 
        for(unsigned i=0; i<outChN; ++i)
          if((rc = var_register( ctx, i, kGainPId, "gain" )) != kOkRC )
            goto errLabel;
          
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srate, outChN, frameN );

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
                                      kIn0PId,"in0",abuf0,
                                      kIn1PId,"in1",abuf1 )) != kOkRC )
        {
          goto errLabel;
        }

        assert( abuf0->frameN == abuf1->frameN );

        outChN = std::max(abuf0->chN, abuf1->chN);

        // register the gain
        var_register_and_get( ctx, kAnyChIdx, kGain0PId, "gain0", dum );
        var_register_and_get( ctx, kAnyChIdx, kGain1PId, "gain1", dum );
          
        // create the output audio buffer
        rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, abuf0->srate, outChN, abuf0->frameN );

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
            real_t          gain = 1;

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
        kGainPId,
        kOutPId
      };
      
      typedef struct
      {
        real_t *phaseA;
      } inst_t;
      
      rc_t create( instance_t* ctx )
      {
        rc_t     rc    = kOkRC;
        inst_t*  inst  = mem::allocZ<inst_t>();
        srate_t  srate = 0;
        unsigned chCnt = 0;
        real_t   gain;
        real_t   hz;
        
        ctx->userPtr = inst;

        // Register variables and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx, kChCntPid, "chCnt", chCnt)) != kOkRC )
        {
          goto errLabel;
        }

        // register each oscillator variable
        for(unsigned i=0; i<chCnt; ++i)
          if((rc = var_register_and_get( ctx, i,
                                         kSratePId,  "srate", srate,
                                         kFreqHzPId, "hz",    hz,
                                         kGainPId,   "gain",  gain)) != kOkRC )
          {
            goto errLabel;
          }

        // create one output audio buffer
        rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srate, chCnt, ctx->ctx->framesPerCycle );

        inst->phaseA = mem::allocZ<real_t>( chCnt );
        

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
            real_t    gain  = val_get<real_t>( ctx, kGainPId, i );
            real_t    hz    = val_get<real_t>( ctx, kFreqHzPId, i );
            srate_t   srate = val_get<srate_t>( ctx, kSratePId, i );                        
            sample_t* v     = abuf->buf + (i*abuf->frameN);
            
            for(unsigned j=0; j<abuf->frameN; ++j)
              v[j] = (sample_t)(gain * sin( inst->phaseA[i] + (2.0 * M_PI * j * hz/srate)));

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
      typedef struct dsp::pv_anl::obj_str<sample_t,fd_real_t> pv_t;

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

        if((rc = var_register_and_get( ctx, kAnyChIdx,kInPId, "in", srcBuf )) != kOkRC )
        {
          cwLogError(kInvalidArgRC,"Unable to access the 'src' buffer.");
        }
        else
        {
          flags  = inst->hzFl ? dsp::pv_anl::kCalcHzPvaFl : dsp::pv_anl::kNoCalcHzPvaFl;
          inst->pvN = srcBuf->chN;
          inst->pvA = mem::allocZ<pv_t*>( inst->pvN );  // allocate pv channel array
          
          const fd_real_t* magV[ srcBuf->chN ];
          const fd_real_t* phsV[ srcBuf->chN ];
          const fd_real_t* hzV[  srcBuf->chN ];
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
                                           kMaxWndSmpNPId, "maxWndSmpN", maxWndSmpN,
                                           kWndSmpNPId, "wndSmpN", wndSmpN,
                                           kHopSmpNPId, "hopSmpN", hopSmpN,
                                           kHzFlPId,    "hzFl",    hzFl )) != kOkRC )
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
          if((rc = var_register_and_set(ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, maxBinNV, binNV, hopNV, magV, phsV, hzV )) != kOkRC )
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
      typedef struct dsp::pv_syn::obj_str<sample_t,fd_real_t> pv_t;

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

        if((rc = var_register_and_get( ctx, kAnyChIdx,kInPId, "in", srcBuf)) != kOkRC )
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

          if((rc = var_register( ctx, kAnyChIdx, kInPId, "in" )) != kOkRC )
            goto errLabel;

          // create the abuf 'out'
          rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, ctx->ctx->framesPerCycle );
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
      typedef struct dsp::spec_dist::obj_str<fd_real_t,fd_real_t> spec_dist_t;

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
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
          goto errLabel;
        }
        else
        {
          // allocate pv channel array
          inst->sdN = srcBuf->chN;
          inst->sdA = mem::allocZ<spec_dist_t*>( inst->sdN );  

          const fd_real_t* magV[ srcBuf->chN ];
          const fd_real_t* phsV[ srcBuf->chN ];
          const fd_real_t*  hzV[ srcBuf->chN ];

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
                                           kBypassPId,   "bypass",   sd->bypassFl,
                                           kCeilingPId,  "ceiling",  sd->ceiling,
                                           kExpoPId,     "expo",     sd->expo,
                                           kThreshPId,   "thresh",   sd->thresh,
                                           kUprSlopePId, "upr",      sd->uprSlope,
                                           kLwrSlopePId, "lwr",      sd->lwrSlope,
                                           kMixPId,      "mix",      sd->mix )) != kOkRC )
            {
              goto errLabel;
            }
                
          }
          
          // create the output buffer
          if((rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->maxBinN_V, srcBuf->binN_V, srcBuf->hopSmpN_V, magV, phsV, hzV )) != kOkRC )
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
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",srcBuf )) != kOkRC )
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
            real_t igain, maxWnd_ms, wnd_ms, thresh, ratio, atk_ms, rls_ms, ogain;
            bool bypassFl;


            // get the compressor variable values
            if((rc = var_register_and_get( ctx, i,
                                           kBypassPId,   "bypass",    bypassFl,
                                           kInGainPId,   "igain",     igain,
                                           kThreshPId,   "thresh",    thresh,
                                           kRatioPId,    "ratio",     ratio,
                                           kAtkMsPId,    "atk_ms",    atk_ms,
                                           kRlsMsPId,    "rls_ms",    rls_ms,
                                           kWndMsPId,    "wnd_ms",    wnd_ms,
                                           kMaxWndMsPId, "maxWnd_ms", maxWnd_ms,
                                           kOutGainPId,  "ogain",     ogain )) != kOkRC )
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
          if((rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
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
        real_t  tmp;

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
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",srcBuf )) != kOkRC )
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
            real_t igain, thresh, ogain;
            bool bypassFl;


            // get the limiter variable values
            if((rc = var_register_and_get( ctx, i,
                                           kBypassPId,   "bypass",    bypassFl,
                                           kInGainPId,   "igain",     igain,
                                           kThreshPId,   "thresh",    thresh,
                                           kOutGainPId,  "ogain",     ogain )) != kOkRC )
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
          if((rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
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
        real_t  rtmp;
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
        real_t        delayMs    = 0;
        real_t        maxDelayMs = 0;

        ctx->userPtr = inst;

        // get the source audio buffer
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",abuf )) != kOkRC )
          goto errLabel;


        inst->cntV = mem::allocZ<unsigned>(abuf->chN);
        inst->idxV = mem::allocZ<unsigned>(abuf->chN);
        
        // register the gain 
        for(unsigned i=0; i<abuf->chN; ++i)
        {
          if((rc = var_register_and_get( ctx, i,
                                         kMaxDelayMsPId, "maxDelayMs", maxDelayMs,
                                         kDelayMsPId,    "delayMs",    delayMs)) != kOkRC )
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
        rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, abuf->srate, abuf->chN, abuf->frameN );


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

      rc_t value( instance_t* ctx, variable_t* var )
      { return kOkRC; }

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
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",srcBuf )) != kOkRC )
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
            real_t gain;
            bool bypassFl;


            // get the dc_filter variable values
            if((rc = var_register_and_get( ctx, i,
                                           kBypassPId,   "bypass",    bypassFl,
                                           kGainPId,     "gain",      gain )) != kOkRC )
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
          if((rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->frameN )) != kOkRC )
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
          real_t gain   = val_get<real_t>( ctx, kGainPId,   i );
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
        if((rc = var_register_and_get(ctx, kAnyChIdx,kInPId,"in",srcBuf )) != kOkRC )
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
	    real_t wndMs, peakThreshDb;
	    bool dbFl;
	    
            // get the audio_meter variable values
            if((rc = var_register_and_get( ctx, i,
                                           kDbFlPId,   "dbFl",    dbFl,
                                           kWndMsPId, "wndMs",    wndMs,
					   kPeakDbPId, "peakDb",  peakThreshDb )) != kOkRC )
            {
              goto errLabel;
            }

            // get the audio_meter variable values
            if((rc = var_register( ctx, i,
				   kOutPId,   "out",
				   kPeakFlPId, "peakFl",
				   kClipFlPId, "clipFl" )) != kOkRC )
            {
              goto errLabel;
            }
	    
	    unsigned maxWndMs = std::max(wndMs,1000.0f);
	    
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
    
    
  } // flow
} // cw


