#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwUtility.h"
#include "cwFileSys.h"
#include "cwAudioFileOps.h"
#include "cwMath.h"
#include "cwVectOps.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"
#include "cwAudioFileProc.h"
#include "cwPvAudioFileProc.h"

//------------------------------------------------------------------------------------------------
// Phase Vocoder File Processor
//
namespace cw
{
  namespace afop
  {
    // PV Template
    namespace pvoc_template
    {
      typedef struct process_str
      {
        int foo;
        int blah;
      } process_t;
      
      rc_t open( pvoc_ctx_t* ctx )
      {
        rc_t       rc = kOkRC;
        process_t* p  = mem::allocZ<process_t>();
        
        if((rc = ctx->args->getv( "foo",  p->foo,
                                  "blah", p->blah)) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing of 'pvoc_template' args. failed.");
        }

        ctx->userPtr = p;
        
        return rc;
      }
      
      rc_t close( pvoc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;
        process_t* p = (process_t*)ctx->userPtr;
        if( p != nullptr )
        {
          mem::release(ctx->userPtr);
        }
        return rc;
      }
      
      rc_t process( pvoc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;

        unsigned chCnt = std::min(ctx->srcChN,ctx->dstChN);
        unsigned binN  = ctx->binN;

        for(unsigned i=0; i<chCnt; ++i)
          for(unsigned j=0; j<binN; ++j)
          {
            ctx->dstMagChA[i][j] = ctx->srcMagChA[i][j];
            ctx->dstPhsChA[i][j] = ctx->srcPhsChA[i][j];
          }
        
        return rc;
      }
      
      rc_t main( pvoc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;
        
        switch( ctx->procId )
        {
          case kOpenProcId:
            rc = open(ctx);
            break;
            
          case kCloseProcId:
            rc = close(ctx);
            break;
            
          case kProcProcId:
            rc = process(ctx);
            break;            
        }
        return rc;
      }      
    }


    // PV Spec Dist
    namespace pvoc_spec_dist
    {
      typedef struct process_str
      {
        dsp::spec_dist::fobj_t** sdChA;
      } process_t;
      
      rc_t open( pvoc_ctx_t* ctx )
      {
        rc_t       rc  = kOkRC;

        process_t* p   = mem::allocZ<process_t>();
        ctx->userPtr = p;
        
        float ceiling  = 30;
        float expo     = 2;
        float thresh   = 60;
        float uprSlope = -0.7;
        float lwrSlope = 2;
        float mix      = 0;


        
        if((rc = ctx->args->getv( "ceiling",   ceiling,
                                   "expo",     expo,
                                   "thresh",   thresh,
                                   "uprSlope", uprSlope,
                                   "lwrSlope", lwrSlope,
                                   "mix",      mix)) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing of 'pvoc_template' args. failed.");
          goto errLabel;
        }

        p->sdChA = mem::allocZ< dsp::spec_dist::fobj_t* >( ctx->srcChN );

        for(unsigned i=0; i<ctx->srcChN; ++i)
          if((rc = dsp::spec_dist::create( p->sdChA[i], ctx->binN, ceiling, expo, thresh, uprSlope, lwrSlope, mix )) != kOkRC )
          {
            rc = cwLogError(rc,"Spec Dist processor channel create failed.");
            goto errLabel;
          }
        

      errLabel:
        return rc;
      }
      
      rc_t close( pvoc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;
        process_t* p = (process_t*)ctx->userPtr;

        if( p != nullptr )
        {
          if( p->sdChA )
          {
            for(unsigned i=0; i<ctx->srcChN; ++i)
              dsp::spec_dist::destroy( p->sdChA[i] );
            mem::release(p->sdChA);
          }

          mem::release( p );
        }
        return rc;
      }
      
      rc_t process( pvoc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;

        process_t* p     = (process_t*)ctx->userPtr;
        unsigned   chCnt = std::min(ctx->srcChN,ctx->dstChN);

        for(unsigned i=0; i<chCnt; ++i)
        {
          dsp::spec_dist::exec( p->sdChA[i], ctx->srcMagChA[i], ctx->srcPhsChA[i], ctx->binN );
                    
          for(unsigned j=0; j<ctx->binN; ++j)
          {           
            ctx->dstMagChA[i][j] = p->sdChA[i]->outMagV[j];
            ctx->dstPhsChA[i][j] = p->sdChA[i]->outPhsV[j];
           }
        }
        
        return rc;
      }
      
      rc_t main( pvoc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;
        
        switch( ctx->procId )
        {
          case kOpenProcId:
            rc = open(ctx);
            break;
            
          case kCloseProcId:
            rc = close(ctx);
            break;
            
          case kProcProcId:
            rc = process(ctx);
            break;            
        }
        return rc;
      }      
    }

    
    namespace pv_file_proc
    {
      typedef struct process_str
      {
        dsp::pv_anl::fobj_t** anlA;       // anlA[chCnt]
        dsp::pv_syn::fobj_t** synA;       // synA[chCnt]
        pvoc_ctx_t            pvoc_ctx;   //
        const char*           functionLabel;;   //
        pvoc_func_t           function;  //
        float*                dstBuf;
        float*                srcBuf;
      } process_t;

      typedef struct labelFunc_str
      {
        const char* label;
        pvoc_func_t func;
      } labelFunc_t;

      labelFunc_t labelFuncA[] =
      {
        { "pvoc_template",pvoc_template::main },
        { "spec_dist",    pvoc_spec_dist::main },
        { nullptr, nullptr }
      };
      
      rc_t open( proc_ctx_t* ctx )
      {
        rc_t        rc        = kOkRC;
        process_t*  p         = mem::allocZ<process_t>();
        ctx->userPtr = p;
        

        // parse the specific process function configuration record
        if((rc = ctx->args->getv( "wndSmpN",  p->pvoc_ctx.wndSmpN,
                                  "hopSmpN",  p->pvoc_ctx.hopSmpN,
                                  "procSmpN", p->pvoc_ctx.procSmpN,
                                  "function", p->functionLabel,
                                  "args",     p->pvoc_ctx.args)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"The pvoc file proc. configuration parse failed.");
          goto errLabel;
        }

        if((rc = ctx->args->getv_opt( "inGain",  p->pvoc_ctx.inGain,
                                       "outGain", p->pvoc_ctx.outGain )) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing of pvoc file optional arguments failed.");
          goto errLabel;
        }
        

        // locate the executable function associated with the specified process function
        for(unsigned i=0; true; ++i)
        {
          // if the function was not found
          if( labelFuncA[i].func == nullptr )
          {
            rc = cwLogError(kInvalidArgRC,"The audio processing program '%s' could not be found.", cwStringNullGuard(p->functionLabel));
            goto errLabel;
          }

          // if this is the specified function
          if( textCompare(labelFuncA[i].label,p->functionLabel ) == 0 )
          {
            p->function = labelFuncA[i].func;
            break;
          }
        }

        p->pvoc_ctx.td_ctx    = ctx;
        p->pvoc_ctx.srcChN    = ctx->srcChN;
        p->pvoc_ctx.dstChN    = ctx->dstChN;

        
        p->pvoc_ctx.srcMagChA = mem::allocZ< const float * >( p->pvoc_ctx.srcChN );
        p->pvoc_ctx.srcPhsChA = mem::allocZ< const float * >( p->pvoc_ctx.srcChN );
        
        p->pvoc_ctx.dstMagChA = mem::allocZ< float* >( p->pvoc_ctx.dstChN );
        p->pvoc_ctx.dstPhsChA = mem::allocZ< float* >( p->pvoc_ctx.dstChN );

        p->anlA  = mem::allocZ< dsp::pv_anl::fobj_t* >( p->pvoc_ctx.srcChN );
        p->synA  = mem::allocZ< dsp::pv_syn::fobj_t* >( p->pvoc_ctx.dstChN );

        
        for(unsigned i=0; i<p->pvoc_ctx.srcChN; ++i)
        {
          if((rc = dsp::pv_anl::create( p->anlA[i], p->pvoc_ctx.procSmpN, ctx->srcSrate, p->pvoc_ctx.wndSmpN, p->pvoc_ctx.hopSmpN, dsp::pv_anl::kNoCalcHzPvaFl )) != kOkRC )
          {
            rc = cwLogError(rc,"PVOC analysis component create failed.");
            goto errLabel;
          }

          p->pvoc_ctx.binN = p->anlA[i]->binCnt;  // All input and ouput frames have the same bin count
        }

        // Call the open function 
        p->pvoc_ctx.procId = kOpenProcId;
        if((rc = p->function( &p->pvoc_ctx )) != kOkRC )
          goto errLabel;

        
        // Allocate the vector memory for the src/dst buffer
        p->srcBuf = mem::allocZ< float >(    p->pvoc_ctx.srcChN * p->pvoc_ctx.binN );
        p->dstBuf = mem::allocZ< float >( 2* p->pvoc_ctx.dstChN * p->pvoc_ctx.binN );
        
        for(unsigned i=0; i<p->pvoc_ctx.dstChN; ++i)
        {
          if((rc = dsp::pv_syn::create( p->synA[i], p->pvoc_ctx.procSmpN, ctx->dstSrate, p->pvoc_ctx.wndSmpN, p->pvoc_ctx.hopSmpN )) != kOkRC )
          {
            rc = cwLogError(rc,"PVOC synthesis component create failed.");
            goto errLabel;
          }

          p->pvoc_ctx.dstMagChA[i] = p->dstBuf + (2*i*p->pvoc_ctx.binN);
          p->pvoc_ctx.dstPhsChA[i] = p->pvoc_ctx.dstMagChA[i] + p->pvoc_ctx.binN;
        }
        
      errLabel:
        return rc;
      }
      
      rc_t close( proc_ctx_t* ctx )
      {
        rc_t       rc = kOkRC;
        process_t* p  = (process_t*)ctx->userPtr;

        if( p != nullptr )
        {

          p->pvoc_ctx.procId = kCloseProcId;
          p->function( &p->pvoc_ctx );
          
          if( p->anlA )
            for(unsigned i=0; i<p->pvoc_ctx.srcChN; ++i)
              dsp::pv_anl::destroy(p->anlA[i]);

          if( p->synA )
            for(unsigned i=0; i<p->pvoc_ctx.dstChN; ++i)
              dsp::pv_syn::destroy(p->synA[i]);

          mem::release( p->anlA );
          mem::release( p->synA );
          mem::release( p->pvoc_ctx.srcMagChA );
          mem::release( p->pvoc_ctx.srcPhsChA );
          mem::release( p->pvoc_ctx.dstMagChA );
          mem::release( p->pvoc_ctx.dstPhsChA );
          mem::release( p->dstBuf );
          mem::release( p->srcBuf );
          mem::release(p);
        }
        
        return rc;
      }
      
      rc_t process( proc_ctx_t* ctx )
      {
        rc_t       rc = kOkRC;
        process_t* p  = (process_t*)ctx->userPtr;
        bool       fl = false;
          
        // Setup the source spectral data
        for(unsigned i=0; i<p->pvoc_ctx.srcChN; ++i)
        {
          p->pvoc_ctx.srcMagChA[i] = nullptr;
          p->pvoc_ctx.srcPhsChA[i] = nullptr;
          
          if( dsp::pv_anl::exec( p->anlA[i], ctx->srcChV[i], p->pvoc_ctx.hopSmpN) )
          {
            float* srcChV = p->srcBuf + (i*p->pvoc_ctx.binN);

            // apply input gain
            vop::mul( srcChV, p->anlA[i]->magV, p->pvoc_ctx.inGain * p->pvoc_ctx.binN, p->pvoc_ctx.binN );
            
            p->pvoc_ctx.srcMagChA[i] = (const float*)srcChV;
            p->pvoc_ctx.srcPhsChA[i] = (const float*)p->anlA[i]->phsV;

            fl = true;
          }
        }

        if( fl )
        {
          p->pvoc_ctx.procId = kProcProcId;
          p->function( &p->pvoc_ctx );

          // Get the dest. spectral data.
          for(unsigned i=0; i<p->pvoc_ctx.dstChN; ++ i)
          {
            if((rc = dsp::pv_syn::exec( p->synA[i], p->pvoc_ctx.dstMagChA[i], p->pvoc_ctx.dstPhsChA[i] )) != kOkRC )
            {
              rc = cwLogError(rc,"Pvoc synthesis failed.");
              goto errLabel;
            }

            // apply output gain
            vop::mul( ctx->dstChV[i], p->synA[i]->ola->outV, p->pvoc_ctx.outGain, p->pvoc_ctx.hopSmpN );
            //vop::copy( ctx->dstChV[i], p->synA[i]->ola->outV, p->pvoc_ctx.hopSmpN );
          }
        }
        

      errLabel:
        return rc;
      }
      
      rc_t main( proc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;
        
        switch( ctx->procId )
        {
          case kOpenProcId:
            rc = open(ctx);
            break;
            
          case kCloseProcId:
            rc = close(ctx);
            break;
            
          case kProcProcId:
            rc = process(ctx);
            break;            
        }
        return rc;
      }      
    }
  }
}

cw::rc_t cw::afop::pvoc_file_processor( const object_t* cfg )
{
  rc_t            rc           = kOkRC;
  const char*     srcFn        = nullptr;
  const char*     dstFn        = nullptr;
  const char*     pgmLabel     = nullptr;
  const object_t* pgm          = nullptr;
  unsigned        hopSmpN      = 0;
  unsigned        recordChN    = 0;
  const object_t* recorder     = nullptr;
  
  // parse the main audio file processor cfg record
  if((rc = cfg->getv("srcFn",  srcFn,
                      "dstFn", dstFn,
                      "program",   pgmLabel)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the main audio file proc configuration record.");
    goto errLabel;
  }

  // parse the recorder spec
  if((rc = cfg->getv_opt("recordChN",      recordChN,
                         "recorder",       recorder)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the main audio file proc optional configuration fields.");
    goto errLabel;
  }

  // locate the cfg for the specific process function to run
  if((rc = cfg->getv(pgmLabel, pgm))  != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"The audio file proc. configuration '%s' was not found.",cwStringNullGuard(pgmLabel));
    goto errLabel;
  }
  
  // parse the specific process function configuration record
  if((rc = pgm->getv("hopSmpN", hopSmpN)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"The audio file proc. configuration '%s' parse failed.",cwStringNullGuard(pgmLabel));
    goto errLabel;
  }

  // run the processr
  if((rc = file_processor( srcFn, dstFn, pv_file_proc::main, hopSmpN, hopSmpN, nullptr, pgm, recorder, recordChN)) != kOkRC )
  {
    rc = cwLogError(rc,"The audio file proc. failed.");
    goto errLabel;
  }

 errLabel:
  return rc;
}
