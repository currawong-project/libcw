//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
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

namespace cw
{
  namespace afop
  {
    //------------------------------------------------------------------------------------------------
    // Template Process
    //
    namespace process
    {
      typedef struct process_str
      {
        int    foo;
        double blah;
      } process_t;

      rc_t open( proc_ctx_t* ctx )
      {
        rc_t       rc = kOkRC;
        process_t* p  = mem::allocZ<process_t>();
        ctx->userPtr = p;
        
        if((rc = ctx->args->getv( "foo",  p->foo,
                                   "blah", p->blah)) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing of 'template' args. failed.");
        }

        
        return rc;
      }
      
      rc_t close( proc_ctx_t* ctx )
      {
        rc_t       rc = kOkRC;
        process_t* p  = (process_t*)ctx->userPtr;
        mem::release(p);
        return rc;
      }
      
      rc_t process( proc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;
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

    //------------------------------------------------------------------------------------------------
    // Phase Vocoder Process
    //
    namespace pvoc
    {
      typedef struct process_str
      {
        dsp::pv_anl::fobj_t** anlA;      // anlA[chCnt]
        dsp::pv_syn::fobj_t** synA;      // synA[chCnt]
        unsigned              chCnt;     //
        unsigned              procSmpN;  //
        unsigned              wndSmpN;   //
        unsigned              hopSmpN;   //
        double                inGain;    //
        double                outGain;   //
      } process_t;

      rc_t open( proc_ctx_t* ctx )
      {
        rc_t rc = kOkRC;
        process_t* p = mem::allocZ<process_t>();
        ctx->userPtr = p;

        if((rc = ctx->args->getv( "procSmpN", p->procSmpN,
                                   "hopSmpN",  p->hopSmpN,
                                   "wndSmpN",  p->wndSmpN)) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing of 'pvoc' required arguments failed.");
        }

        
        p->chCnt = std::min( ctx->srcChN, ctx->dstChN );
        p->anlA  = mem::allocZ< dsp::pv_anl::fobj_t* >( p->chCnt );
        p->synA  = mem::allocZ< dsp::pv_syn::fobj_t* >( p->chCnt );

        for(unsigned i=0; i<p->chCnt; ++i)
        {
          if((rc = dsp::pv_anl::create( p->anlA[i], p->procSmpN, ctx->srcSrate, p->wndSmpN, p->wndSmpN, p->hopSmpN, dsp::pv_anl::kNoCalcHzPvaFl )) != kOkRC )
          {
            rc = cwLogError(rc,"PVOC analysis component create failed.");
            goto errLabel;
          }
        
          if((rc = dsp::pv_syn::create( p->synA[i], p->procSmpN, ctx->dstSrate, p->wndSmpN, p->hopSmpN )) != kOkRC )
          {
            rc = cwLogError(rc,"PVOC synthesis component create failed.");
            goto errLabel;
          }
        }
        
        

      errLabel:
        return rc;
      }
      
      rc_t close( proc_ctx_t* ctx )
      {
        rc_t       rc = kOkRC;
        process_t* p  = (process_t*)ctx->userPtr;

        for(unsigned i=0; i<p->chCnt; ++i)
        {
          if( p->anlA )
            dsp::pv_anl::destroy(p->anlA[i]);
          
          if( p->synA )
            dsp::pv_syn::destroy(p->synA[i]);
        }
        
        mem::release(p);
        return rc;
      }
      
      rc_t process( proc_ctx_t* ctx )
      {
        rc_t       rc = kOkRC;
        process_t* p  = (process_t*)ctx->userPtr;

        for(unsigned i=0; i<p->chCnt; ++i)
        {
          if( dsp::pv_anl::exec( p->anlA[i], ctx->srcChV[i], p->hopSmpN) )
          {

            float buf[ p->anlA[i]->binCnt ];
            vop::mul( buf, p->anlA[i]->magV, p->anlA[i]->binCnt/2, p->anlA[i]->binCnt );
            
            if((rc = dsp::pv_syn::exec( p->synA[i], buf, p->anlA[i]->phsV )) != kOkRC )
            {
              rc = cwLogError(rc,"Pvoc synthesis failed.");
              goto errLabel;
            }

            vop::copy( ctx->dstChV[i], p->synA[i]->ola->outV, p->hopSmpN );
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
    
    //------------------------------------------------------------------------------------------------
    // Tremelo Process
    //
    namespace tremelo
    {

      typedef struct tremelo_str
      {
        double hz;
        double depth;
        double phase;
      } tremelo_t;
      
      rc_t open( proc_ctx_t* ctx )
      {
        rc_t rc;

        tremelo_t* p = mem::allocZ<tremelo_t>();
        ctx->userPtr = p;
        
        if((rc = ctx->args->getv( "hz",    p->hz,
                                   "depth", p->depth)) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing of 'tremelo' ctx. failed.");
        }

        
        return rc;
      }

      rc_t close( proc_ctx_t* ctx )
      {
        tremelo_t* p = (tremelo_t*)ctx->userPtr;
        mem::release(p);
        return kOkRC;
      }
      
      rc_t process( proc_ctx_t* ctx )
      {
        tremelo_t* p       = (tremelo_t*)ctx->userPtr;
        unsigned   chCnt   = std::min( ctx->srcChN,   ctx->dstChN );
        unsigned   hopSmpN = std::min( ctx->srcHopSmpN, ctx->dstHopSmpN );

        
        for(unsigned i=0; i<hopSmpN; ++i)
        {
          float gain = p->depth * std::sin( p->phase );
          
          for(unsigned j=0; j<chCnt; ++j)
            ctx->dstChV[j][i] = gain * ctx->srcChV[j][i];
          
          
          p->phase += 2*M_PI*p->hz / ctx->srcSrate;
        }

        return kOkRC;
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


//------------------------------------------------------------------------------------------------
// Audio File Processor
//

cw::rc_t cw::afop::file_processor( const char* srcFn, const char* dstFn, proc_func_t func, unsigned wndSmpN, unsigned hopSmpN, void* userPtr, const object_t* args, const object_t* recorder_cfg, unsigned recordChN )
{
  rc_t rc = kOkRC;
  
  typedef float sample_t;
  
  audiofile::handle_t          srcAfH;
  audiofile::handle_t          dstAfH;
  audiofile::info_t            info;
  proc_ctx_t                   proc_ctx  = {};

  if( hopSmpN > wndSmpN )
    return cwLogError(kInvalidArgRC,"The hop sample count (%i) cannot exceed the window sample count (%i).", hopSmpN, wndSmpN );

  proc_ctx.userPtr = userPtr;
  proc_ctx.args    = args;

  // By default wndSmpN and hopSmpN set both the src and dst wndSmpN and hopSmpN parameters
  proc_ctx.srcWndSmpN = wndSmpN;
  proc_ctx.srcHopSmpN = hopSmpN;
  proc_ctx.dstWndSmpN = wndSmpN;
  proc_ctx.dstHopSmpN = hopSmpN;  

  // if a source audio file was given
  if( srcFn != nullptr )
  {
    // open the source audio file
    if((rc = audiofile::open(srcAfH,srcFn,&info)) != kOkRC )
    {
      rc = cwLogError(rc,"Unable to open the source file:%s", cwStringNullGuard(srcFn) );
      goto errLabel;
    }

    // Configure the input signal 
    proc_ctx.srcFn    = mem::duplStr(srcFn);
    proc_ctx.srcSrate = info.srate;
    proc_ctx.srcChN   = info.chCnt;
    proc_ctx.srcBits  = info.bits;

    // By default the output signal has the same configuration as the input file
    proc_ctx.dstSrate = info.srate;
    proc_ctx.dstChN   = info.chCnt;
    proc_ctx.dstBits  = info.bits; // TODO: allow setting output file bits as optional parameter (0=float)
                                      // Be sure if settting bits from input file with floating point sample format
                                      // that this case is correctly handled here.
  }

  // During the 'open' call the user defined function (UDF) can override the destination file configuration 
  proc_ctx.procId              = kOpenProcId;
  if((rc = func( &proc_ctx )) != kOkRC )
  {
    rc = cwLogError(rc,"Open processes failed.");
    goto errLabel;
  }

  // Create the output file
  if( dstFn != nullptr )
  {
    
    if((rc = audiofile::create(dstAfH,dstFn, proc_ctx.dstSrate, proc_ctx.dstBits, proc_ctx.dstChN )) != kOkRC )
    {
      rc = cwLogError(rc,"Unable to create the destination file:%s", cwStringNullGuard(dstFn) );
      goto errLabel;
    }

    proc_ctx.dstFn  = mem::duplStr(dstFn);
    
  }

  if( rc == kOkRC )
  {
    sample_t*       srcChFileBuf[ proc_ctx.srcChN ]; // srcChFileBuf[ srcChN ][ srcHopSmpCnt ]  - src file ch buffer
    sample_t*       dstChFileBuf[ proc_ctx.dstChN ]; // dstChFileBuf[ dstChN ][ dstHopSmpCnt ]  - dst file ch buffer
    const sample_t* iChBuf[       proc_ctx.srcChN ]; // iChBuf[       srcChN ][ srcWndSmpCnt ]  - src processing buffer
    sample_t*       oChBuf[       proc_ctx.dstChN ]; // oChBuf[       dstChN ][ dstWndSmpCnt ]  - dst processing buffer
    
    struct dsp::shift_buf::obj_str<sample_t>* srcShiftBufA[ proc_ctx.srcChN ]; // src shift buffer
    struct dsp::shift_buf::obj_str<sample_t>* dstShiftBufA[ proc_ctx.dstChN ]; // dst shift buffer

    // Allocate memory for the source/dest  file buffer 
    sample_t* srcFileBuf  = mem::allocZ<sample_t>( proc_ctx.srcChN * proc_ctx.srcHopSmpN );
    sample_t* dstFileBuf  = mem::allocZ<sample_t>( proc_ctx.dstChN * proc_ctx.dstHopSmpN );
    sample_t* procFileBuf = mem::allocZ<sample_t>( proc_ctx.dstChN * proc_ctx.dstHopSmpN );

    // Setup the input and output processing buffer
    proc_ctx.srcChV = iChBuf;  
    proc_ctx.dstChV = oChBuf;

    // For each source channel - setup the source file buffer and create the src shift buffer
    for(unsigned i = 0; i<proc_ctx.srcChN; ++i)
    {
      srcChFileBuf[i] = srcFileBuf + (i*proc_ctx.srcHopSmpN );
      dsp::shift_buf::create( srcShiftBufA[i], proc_ctx.srcHopSmpN, proc_ctx.srcWndSmpN, proc_ctx.srcWndSmpN, proc_ctx.srcHopSmpN );      
    }

    // For each dest. channel - setup the dest file buffer and create the dst shift buffer
    for(unsigned i = 0; i<proc_ctx.dstChN; ++i)
    {
      oChBuf[i]   = procFileBuf + (i*proc_ctx.dstHopSmpN );
      dsp::shift_buf::create( dstShiftBufA[i], proc_ctx.dstHopSmpN, proc_ctx.dstWndSmpN, proc_ctx.dstWndSmpN, proc_ctx.dstHopSmpN );
    }

    // create the data recorder
    if( recordChN )
    {
      proc_ctx.recordChA = mem::allocZ< dsp::data_recorder::fobj_t* >( recordChN );
      for(unsigned i = 0; i<recordChN; ++i)
      {
        if(dsp::data_recorder::create( proc_ctx.recordChA[i], recorder_cfg ) != kOkRC )
        {
          cwLogWarning( "Data recorder create failed." );
          break;
        }
      }

    }
    

    // 
    while( true )
    {
      unsigned rdFrmCnt = 0;

      // if a source file exists then read the next srcHopSmpN samples into srcChFileBuf[][]
      if( srcFn )
      {
        // Read the next srcHopSmpN samples
        rc = audiofile::readFloat(srcAfH, proc_ctx.srcHopSmpN, 0, proc_ctx.srcChN, srcChFileBuf, &rdFrmCnt );
        
        // if the end of the file was encountered or no samples were returned
        if( rc == kEofRC || rdFrmCnt == 0)
        {
          rc = kOkRC;
          break;
        }

        if( rc != kOkRC )
        {
          rc = cwLogError(rc,"Audio process source file '%s' read failed.", srcFn );
          break;
        }
        
      }


      do  // src shift-buffer processing loop      
      {

        if( srcFn )
        {
          // Shift the new source sample into the source shift buffer.
          // Note that the shift buffer must be called until they return false.
          bool rd_fl = false;
          
          for(unsigned j=0; j<proc_ctx.srcChN; ++j)
            if( dsp::shift_buf::exec( srcShiftBufA[j], srcChFileBuf[j], proc_ctx.srcHopSmpN ) )
            {
              rd_fl     = true;
              iChBuf[j] = srcShiftBufA[j]->outV;
            }
          
          if(!rd_fl)
            break; // src shift-buf iterations are done
        }
        
        // Call the processing function
        proc_ctx.procId = kProcProcId;    
        if((rc = func( &proc_ctx )) != kOkRC )
        {
          // kEof isn't an error 
          if( rc == kEofRC )
            rc = kOkRC;
          else
            rc = cwLogError(rc,"Audio file process reported an error.");
        
          goto doneLabel;
        }


        // if output samples exist - shift the new samples into the output shift buffer
        if( proc_ctx.dstChN )
        {

          do // destination shif-buffer iteration processing loop
          {
            bool wr_fl = false;
            
            // Update the dst shift buffers
            // Note that the shift buffer has to be called until it returns false therefore 
            // we must iterate over the file writing process writing hopSmpN frames on each call.
            for(unsigned j=0; j<proc_ctx.dstChN; ++j)
            {
              if(dsp::shift_buf::exec( dstShiftBufA[j], oChBuf[j], proc_ctx.dstHopSmpN ))
              {
                wr_fl = true;
              
                dstChFileBuf[j] = dstShiftBufA[j]->outV;
              }
            }

            // if no true's were returned by the shift-buffer (Note that all channels must return the same
            // value because they are all configured the same way.)
            if( !wr_fl )
              break; 
            
            // If a destination file was specified
            if( dstFn != nullptr )
            {
              if((rc = audiofile::writeFloat( dstAfH, proc_ctx.dstHopSmpN, proc_ctx.dstChN, dstChFileBuf )) != kOkRC )
              {
                rc = cwLogError(rc,"Audio process source file '%s' read failed.", srcFn );
                goto doneLabel;
              }
            }
            
          }while(1); // desination shift-buffer interation processing loop
        }
        

      }while(1); // source shift-buffer iteration processing loop
      

      proc_ctx.cycleIndex += 1;
    } // end while

  doneLabel:

    for(unsigned i=0; i<recordChN; ++i)
      dsp::data_recorder::destroy( proc_ctx.recordChA[i] );
    mem::release(proc_ctx.recordChA);
    
    for(unsigned i=0; i<proc_ctx.srcChN; ++i)
      dsp::shift_buf::destroy( srcShiftBufA[i] );
  
    for(unsigned i=0; i<proc_ctx.dstChN; ++i)
      dsp::shift_buf::destroy( dstShiftBufA[i] );

    mem::release(srcFileBuf);
    mem::release(dstFileBuf);
    mem::release(procFileBuf);
    
  }
  

 errLabel:

  proc_ctx.procId = kCloseProcId;    
  func( &proc_ctx );

  close(srcAfH);
  close(dstAfH);

  
  mem::release(proc_ctx.srcFn);
  mem::release(proc_ctx.dstFn);
  
  return rc;
}

cw::rc_t cw::afop::file_processor( const object_t* cfg )
{
  rc_t            rc           = kOkRC;
  const char*     srcFn        = nullptr;
  const char*     dstFn        = nullptr;
  const char*     cfgLabel     = nullptr;
  const object_t* pgm          = nullptr;
  unsigned        wndSmpN      = 0;
  unsigned        hopSmpN      = 0;
  const char*     program      = nullptr;
  const object_t* args         = nullptr;
  unsigned        recordChN    = 0;
  const object_t* recorder     = nullptr;
  proc_func_t     procFunc     = nullptr;

  typedef struct labelFunc_str
  {
    const char* label;
    proc_func_t func;
  } labelFunc_t;

  labelFunc_t labelFuncA[] =
  {
    { "tremelo",tremelo::main },
    { "pvoc",   pvoc::main },
    { nullptr, nullptr }
  };

  // parse the main audio file processor cfg record
  if((rc = cfg->getv("srcFn",  srcFn,
                      "dstFn", dstFn,
                      "cfg",   cfgLabel)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the main audio file proc configuration record.");
    goto errLabel;
  }

  // locate the cfg for the specific process function to run
  if((rc = cfg->getv(cfgLabel, pgm))  != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"The audio file proc. configuration '%s' was not found.",cwStringNullGuard(cfgLabel));
    goto errLabel;
  }

  // parse the specific process function configuration record
  if((rc = pgm->getv("wndSmpN", wndSmpN,
                     "hopSmpN", hopSmpN,
                     "program", program,
                     "args",    args)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"The audio file proc. configuration '%s' parse failed.",cwStringNullGuard(cfgLabel));
    goto errLabel;
  }

  // parse the recorder spec
  if((rc = cfg->getv_opt("recordChN",    recordChN,
                         "recorder",     recorder)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the main audio file proc optional configuration fields.");
    goto errLabel;
  }
  
  
  // TODO: add optional args: inGain,outGain,dstBits

  // locate the executable function associated with the specified process function
  for(unsigned i=0; true; ++i)
  {
    // if the function was not found
    if( labelFuncA[i].func == nullptr )
    {
      rc = cwLogError(kInvalidArgRC,"The audio processing program '%s' could not be found.", cwStringNullGuard(program));
      goto errLabel;
    }

    // if this is the specified function
    if( textCompare(labelFuncA[i].label,program ) == 0 )
    {
      procFunc = labelFuncA[i].func;
      break;
    }
  }

  // run the processr
  if((rc = file_processor( srcFn, dstFn, procFunc, wndSmpN, hopSmpN, nullptr, args, recorder, recordChN)) != kOkRC )
  {
    rc = cwLogError(rc,"The audio file proc. failed.");
    goto errLabel;
  }
  
  
 errLabel:
  return rc;
  
}

