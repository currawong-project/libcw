#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwFlow.h"
#include "cwFlowTypes.h"
#include "cwFlowProc.h"

#include "cwFile.h"
#include "cwMath.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"

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
    // AudioFileIn
    //
    
    namespace audioFileIn
    {
      enum
      {
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

        // get the audio filename
        if((rc = ctx->arg_cfg->getv("fn",inst->filename)) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"The audio input file has no 'fn' argument.");
          goto errLabel;
        }

        // get the 'eof' flag
        if((rc = ctx->arg_cfg->getv_opt("eof",inst->eofFl)) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"The audio input file has no 'fn' argument.");
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
        rc = var_abuf_create( ctx, "out", kOutPId, kAnyChIdx, info.srate, info.chCnt, ctx->ctx->framesPerCycle );

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
        if((rc = var_abuf_get(ctx,"out",kAnyChIdx,abuf)) != kOkRC )
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
        .exec = exec
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
        kInPId
      };
      
      typedef struct
      {
        audiofile::handle_t afH;
        const char*         filename;        
      } inst_t;
      
      rc_t create( instance_t* ctx )
      {
        rc_t          rc            = kOkRC;                 //
        unsigned      audioFileBits = 0;                     // set audio file sample format to 'float32'.
        inst_t*       inst          = mem::allocZ<inst_t>(); //
        abuf_t*       src_abuf      = nullptr;
        ctx->userPtr = inst;

        // get the audio filename
        if((rc = ctx->arg_cfg->getv("fn",inst->filename)) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"The audio input file has no 'fn' argument.");
          goto errLabel;
        }
        
        // verify that a source buffer exists
        if((rc = var_abuf_get(ctx,"in",kAnyChIdx,src_abuf)) != kOkRC )
        {
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid input connection.",ctx->label);
          goto errLabel;
        }

        // create the audio file 
        if((rc = audiofile::create( inst->afH, inst->filename, src_abuf->srate, audioFileBits, src_abuf->chN)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"The audio file create failed on '%s'.",cwStringNullGuard(inst->filename));
          goto errLabel;
        }

        rc = var_init( ctx, kAnyChIdx, kInPId, "in", src_abuf);
        
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

        if((rc = var_abuf_get(ctx,"in",kAnyChIdx,src_abuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The audio file instance '%s' does not have a valid input connection.",ctx->label);
        else
        {
          sample_t*     chBuf[ src_abuf->chN ];
        
          for(unsigned i=0; i<src_abuf->chN; ++i)
            chBuf[i] = src_abuf->buf + (i*src_abuf->frameN);
        
          if((rc = audiofile::writeFloat(inst->afH, src_abuf->frameN, src_abuf->chN, chBuf )) != kOkRC )
            rc = cwLogError(rc,"Audio file write failed on instance: '%s'.", ctx->label );
        }
        
        return rc;
      }

      class_members_t members = {
        .create = create,
        .destroy = destroy,
        .value = value,
        .exec = exec
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
        kOutPId
      };
      
      typedef struct
      {
        pv_t**   pvA;       // pvA[ srcBuf.chN ]
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

        // get the wnd/hop sample count
        if((rc = ctx->arg_cfg->getv("wndSmpCnt",inst->wndSmpN,
                                    "hopSmpCnt",inst->hopSmpN )) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"PV Analysis required parameters parse failed on instance '%s'.",ctx->label);
          goto errLabel;
        }

        // get the optional arg's.
        if((rc = ctx->arg_cfg->getv_opt("hzFl",inst->hzFl)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"PV Analysis optional parameters parse failed on instance '%s'.",ctx->label);
          goto errLabel;          
        }
        
        // verify that a source buffer exists
        if((rc = var_abuf_get(ctx,"in", kAnyChIdx, srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
          goto errLabel;
        }
        else
        {

          flags  = inst->hzFl ? dsp::pv_anl::kCalcHzPvaFl : dsp::pv_anl::kNoCalcHzPvaFl;
          inst->pvA = mem::allocZ<pv_t*>( srcBuf->chN );  // allocate pv channel array
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

          if((rc = var_init( ctx, kAnyChIdx, kInPId, "in", srcBuf )) != kOkRC )
            goto errLabel;
          
          // create the fbuf 'out'
          rc = var_fbuf_create( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, inst->pvA[0]->binCnt, inst->pvA[0]->hopSmpCnt, magV, phsV, hzV );
        }
        
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
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const abuf_t* srcBuf = nullptr;
        fbuf_t*       dstBuf = nullptr;

        // verify that a source buffer exists
        if((rc = var_abuf_get(ctx,"in", kAnyChIdx, srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
          goto errLabel;
        }

        // verify that the dst buffer exits
        if((rc = var_fbuf_get(ctx,"out", kAnyChIdx, dstBuf)) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid output.",ctx->label);
          goto errLabel;
        }

        // for each input channel
        for(unsigned i=0; i<srcBuf->chN; ++i)
        {
          // call the PV analysis processor
          dsp::pv_anl::exec( inst->pvA[i], srcBuf->buf + i*srcBuf->frameN, srcBuf->frameN );

          // rescale the frequency domain magnitude
          vop::mul(dstBuf->magV[i], dstBuf->binN/2, dstBuf->binN);
        }

      errLabel:
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec
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

        
        // verify that a source buffer exists
        if((rc = var_fbuf_get(ctx,"in", kAnyChIdx, srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
          goto errLabel;
        }
        else
        {

          // allocate pv channel array
          inst->pvA = mem::allocZ<pv_t*>( srcBuf->chN );  

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

          if((rc = var_init( ctx, kAnyChIdx, kInPId, "in", srcBuf )) != kOkRC )
            goto errLabel;

          // create the abuf 'out'
          rc = var_abuf_create( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, ctx->ctx->framesPerCycle );
        }
        
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
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const fbuf_t* srcBuf = nullptr;
        abuf_t*       dstBuf = nullptr;
        
        // get the src buffer
        if((rc = var_fbuf_get(ctx,"in", kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_abuf_get(ctx,"out", kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;
        
        for(unsigned i=0; i<srcBuf->chN; ++i)
        {
          dsp::pv_syn::exec( inst->pvA[i], srcBuf->magV[i], srcBuf->phsV[i] );

          abuf_set_channel( dstBuf, i, inst->pvA[i]->ola->outV, inst->pvA[i]->ola->hopSmpCnt );
        }
        

      errLabel:
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec
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
      } inst_t;
    

      rc_t create( instance_t* ctx )
      {
        rc_t          rc     = kOkRC;
        const fbuf_t* srcBuf = nullptr; //
        inst_t*       inst   = mem::allocZ<inst_t>();
        
        ctx->userPtr = inst;

        // verify that a source buffer exists
        if((rc = var_fbuf_get(ctx,"in", kAnyChIdx, srcBuf )) != kOkRC )
        {
          rc = cwLogError(rc,"The instance '%s' does not have a valid input connection.",ctx->label);
          goto errLabel;
        }
        else
        {
          // allocate pv channel array
          inst->sdA = mem::allocZ<spec_dist_t*>( srcBuf->chN );  

          const sample_t* magV[ srcBuf->chN ];
          const sample_t* phsV[ srcBuf->chN ];
          const sample_t*  hzV[ srcBuf->chN ];
          
        
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

            if((rc = var_init( ctx, i,
                               kInPId,      "in",       srcBuf,
                               kCeilingPId, "ceiling",  30.0f,
                               kExpoPId,    "expo",      3.0f,
                               kThreshPId,  "thresh",   54.0f,
                               kUprSlopePId,"uprSlope", -0.7f,
                               kLwrSlopePId,"lwrSlope",  2.0f,
                               kMixPId,     "mix",       0.0f )) != kOkRC )
            {
              goto errLabel;
            }
                
          }
          
          // create the output buffer
          if((rc = var_fbuf_create( ctx, "out", kOutPId, kAnyChIdx, srcBuf->srate, srcBuf->chN, srcBuf->binN, srcBuf->hopSmpN, magV, phsV, hzV )) != kOkRC )
            goto errLabel;
        }
        
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
        rc_t          rc     = kOkRC;
        inst_t*       inst   = (inst_t*)ctx->userPtr;
        const fbuf_t* srcBuf = nullptr;
        fbuf_t*       dstBuf = nullptr;
        
        // get the src buffer
        if((rc = var_fbuf_get(ctx,"in", kAnyChIdx, srcBuf )) != kOkRC )
          goto errLabel;

        // get the dst buffer
        if((rc = var_fbuf_get(ctx,"out", kAnyChIdx, dstBuf)) != kOkRC )
          goto errLabel;
        
        for(unsigned i=0; i<srcBuf->chN; ++i)
        {
          dsp::spec_dist::exec( inst->sdA[i], srcBuf->magV[i], srcBuf->phsV[i], srcBuf->binN );

          //if( i == 0 )
          //  printf("%f %f\n", vop::sum(srcBuf->magV[i],srcBuf->binN), vop::sum(dstBuf->magV[i], dstBuf->binN) );
        }
        

      errLabel:
        return rc;
      }

      class_members_t members = {
        .create  = create,
        .destroy = destroy,
        .value   = value,
        .exec    = exec
      };      
    }

    
  }
}


