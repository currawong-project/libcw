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
          memcpy(abuf->buf,inst->ext_dev->u.a.abuf->buf, abuf->frameN*abuf->chN*sizeof(sample_t));
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

          unsigned n = src_abuf->frameN*src_abuf->chN;
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
        
        inst_t* inst = mem::allocZ<inst_t>();
        ctx->userPtr = inst;

        // Register variable and get their current value
        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kFnamePId, "fname", inst->filename,
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

        cwLogInfo("Audio '%s' srate:%f chs:%i frames:%i %f seconds.",inst->filename,info.srate,info.chCnt,info.frameCnt, info.frameCnt/info.srate );

        // create one output audio buffer
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
        return rc;
      }
      
      rc_t exec( instance_t* ctx )
      {
        rc_t     rc           = kOkRC;
        unsigned actualFrameN = 0;
        inst_t*  inst         = (inst_t*)ctx->userPtr;
        abuf_t*  abuf         = nullptr;


        // verify that a source buffer exists
        if((rc = var_get(ctx,kOutPId,kAnyChIdx,abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid audio output buffer.",ctx->label);
        }
        else
        {
          sample_t*   chBuf[ abuf->chN ];
        
          for(unsigned i=0; i<abuf->chN; ++i)
            chBuf[i] = abuf->buf + (i*abuf->frameN);
            
          rc  = readFloat(inst->afH, abuf->frameN, 0, abuf->chN, chBuf, &actualFrameN );
          
          if( inst->eofFl && actualFrameN == 0)            
            rc = kEofRC;
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
                                       kInPId,    "in",    src_abuf)) != kOkRC )
        {
          goto errLabel;
        }

        // create the audio file 
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
            printf("%5.1f min\n", inst->durSmpN/(src_abuf->srate*60));
          
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
            real_t  gain;
            real_t  hz;
            srate_t srate;
            
            var_get( ctx, kFreqHzPId, i, hz );
            var_get( ctx, kGainPId,   i, gain );
            var_get( ctx, kSratePId,  i, srate );
            
            sample_t* v = abuf->buf + (i*abuf->frameN);
            
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
      typedef struct dsp::pv_anl::obj_str<sample_t> pv_t;

      enum {
        kInPId,
        kHopSmpNPId,
        kWndSmpNPId,
        kHzFlPId,
        kOutPId
      };
      
      typedef struct
      {
        pv_t**   pvA;       // pvA[ srcBuf.chN ]
        unsigned pvN;
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

        if((rc = var_register_and_get( ctx, kAnyChIdx,
                                       kInPId,      "in",      srcBuf,
                                       kHopSmpNPId, "hopSmpN", inst->hopSmpN,
                                       kWndSmpNPId, "wndSmpN", inst->wndSmpN,
                                       kHzFlPId,    "hzFl",    inst->hzFl )) != kOkRC )
        {
          goto errLabel;
        }
        else
        {
          flags  = inst->hzFl ? dsp::pv_anl::kCalcHzPvaFl : dsp::pv_anl::kNoCalcHzPvaFl;
          inst->pvN = srcBuf->chN;
          inst->pvA = mem::allocZ<pv_t*>( inst->pvN );  // allocate pv channel array
          
          const sample_t* magV[ srcBuf->chN ];
          const sample_t* phsV[ srcBuf->chN ];
          const sample_t* hzV[  srcBuf->chN ];

          // create a pv anlaysis object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            if((rc = create( inst->pvA[i], ctx->ctx->framesPerCycle, srcBuf->srate, inst->wndSmpN, inst->hopSmpN, flags )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The PV analysis object create failed on the instance '%s'.",ctx->label);
              goto errLabel;
            }

            magV[i] = inst->pvA[i]->magV;
            phsV[i] = inst->pvA[i]->phsV;
            hzV[i]  = inst->pvA[i]->hzV;
          }

          if((rc = var_register( ctx, kAnyChIdx, kInPId, "in" )) != kOkRC )
            goto errLabel;
          
          // create the fbuf 'out'
          rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, inst->pvA[0]->binCnt, inst->pvA[0]->hopSmpCnt, magV, phsV, hzV );
        
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
            vop::mul(dstBuf->magV[i], dstBuf->binN/2, dstBuf->binN);
            
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
      typedef struct dsp::pv_syn::obj_str<sample_t> pv_t;

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
            unsigned wndSmpN = (srcBuf->binN-1)*2;
            
            if((rc = create( inst->pvA[i], ctx->ctx->framesPerCycle, srcBuf->srate, wndSmpN, srcBuf->hopSmpN )) != kOkRC )
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
      typedef struct dsp::spec_dist::obj_str<sample_t,sample_t> spec_dist_t;

      enum
      {
        kInPId,
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

          const sample_t* magV[ srcBuf->chN ];
          const sample_t* phsV[ srcBuf->chN ];
          const sample_t*  hzV[ srcBuf->chN ];

          //if((rc = var_register(ctx, kAnyChIdx, kInPId, "in")) != kOkRC )
          //  goto errLabel;
        
          // create a spec_dist object for each input channel
          for(unsigned i=0; i<srcBuf->chN; ++i)
          {
            if((rc = create( inst->sdA[i], srcBuf->binN )) != kOkRC )
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
          if((rc = var_register_and_set( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->binN, srcBuf->hopSmpN, magV, phsV, hzV )) != kOkRC )
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
          switch( var->vid )
          {
            case kCeilingPId:  var_get( var, inst->sdA[ var->chIdx ]->ceiling );  break;
            case kExpoPId:     var_get( var, inst->sdA[ var->chIdx ]->expo );     break;
            case kThreshPId:   var_get( var, inst->sdA[ var->chIdx ]->thresh );   break;
            case kUprSlopePId: var_get( var, inst->sdA[ var->chIdx ]->uprSlope ); break;
            case kLwrSlopePId: var_get( var, inst->sdA[ var->chIdx ]->lwrSlope ); break;
            case kMixPId:      var_get( var, inst->sdA[ var->chIdx ]->mix );      break;
            default:
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, ctx->label );
          }
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
            dsp::spec_dist::exec( inst->sdA[i], srcBuf->magV[i], srcBuf->phsV[i], srcBuf->binN );

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
            real_t igain, maxWnd_ms, wnd_ms, thresh, ratio, atk_ms, rls_ms, ogain, bypassFl;


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
          
          switch( var->vid )
          {
            case kBypassPId:   var_get( var, inst->cmpA[ var->chIdx ]->bypassFl );  break;
            case kInGainPId:   var_get( var, inst->cmpA[ var->chIdx ]->inGain );    break;
            case kOutGainPId:  var_get( var, inst->cmpA[ var->chIdx ]->outGain );   break;
            case kRatioPId:    var_get( var, inst->cmpA[ var->chIdx ]->ratio_num ); break;
            case kThreshPId:   var_get( var, inst->cmpA[ var->chIdx ]->threshDb );  break;
            case kAtkMsPId:    var_get( var, tmp ); set_attack_ms(inst->cmpA[ var->chIdx ],  tmp ); break;
            case kRlsMsPId:    var_get( var, tmp ); set_release_ms(inst->cmpA[ var->chIdx ], tmp ); break;
            case kWndMsPId:    var_get( var, tmp ); set_rms_wnd_ms(inst->cmpA[ var->chIdx ], tmp ); break;
            default:
              cwLogWarning("Unhandled variable id '%i' on instance: %s.", var->vid, ctx->label );
          }
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

    
    
  }
}


