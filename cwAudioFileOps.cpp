#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwUtility.h"
#include "cwFileSys.h"
#include "cwAudioFileOps.h"
#include "cwVectOps.h"
#include "cwDsp.h"

cw::rc_t     cw::afop::sine( const char* fn, double srate, unsigned bits, double hz, double gain, double secs )
{
  rc_t     rc    = kOkRC;
  unsigned bN    = srate * secs;
  float*   b     = mem::alloc<float>(bN);
  unsigned chCnt = 1;

  unsigned        i;
  for(i=0; i<bN; ++i)
    b[i] = gain * sin(2.0*M_PI*hz*i/srate);

  if((rc = audiofile::writeFileFloat(fn, srate, bits, bN, chCnt, &b)) != kOkRC)
    return rc;

  return rc;
}

cw::rc_t cw::afop::sine( const object_t* cfg )
{
  rc_t        rc;
  double      srate, hz, gain, secs;
  unsigned    bits;
  const char* fn = nullptr;
  
  if((rc = cfg->getv("fn",fn,"srate",srate,"bits",bits,"hz",hz,"gain",gain,"secs",secs)) != kOkRC )
    return cwLogError(kSyntaxErrorRC,"Invalid parameter to audio file sine test.");
  
  char* afn = filesys::expandPath(fn);
  rc        = sine( afn, srate, bits, hz, gain, secs );
  mem::release(afn);
  
  return rc;
}

cw::rc_t      cw::afop::mix( const char* fnV[], const float* gainV, unsigned srcN, const char* outFn, unsigned outBits )
{
  rc_t rc = kOkRC;
  
  if( srcN == 0 )
    return rc;

  unsigned            maxFrmN = 0;
  unsigned            maxChN  = 0;
  double              srate   = 0;
  audiofile::handle_t hV[ srcN ];
  audiofile::handle_t oH;

  // open each source file and determine the output file audio format
  for(unsigned i=0; i<srcN; ++i)
  {
    audiofile::info_t info;

    if((rc = audiofile::open( hV[i], fnV[i], &info )) != kOkRC )
    {
      rc = cwLogError(kOpFailRC,"Unable to open the audio mix source file '%s'.", fnV[i] );
      goto errLabel;
    }

    if( srate == 0 )
      srate    = info.srate;
    
    if( srate != info.srate )
    {
      rc = cwLogError(kInvalidArgRC,"The sample rate (%f) of '%s' does not match the sample rate (%f) of '%s'.", info.srate, fnV[i], srate, fnV[0] );
      goto errLabel;
    }
        
    if( maxFrmN < info.frameCnt )
      maxFrmN = info.frameCnt;

    if( maxChN < info.chCnt )
      maxChN = info.chCnt;
  }

  // create the output file
  if((rc = audiofile::create( oH, outFn, srate, outBits, maxChN)) != kOkRC )
    goto errLabel;
  else
  {
    const unsigned kBufFrmN = 1024;
    float          ibuf[   maxChN * kBufFrmN ];
    float          obuf[   maxChN * kBufFrmN ];
    float*         iChBuf[ maxChN ];
    float*         oChBuf[ maxChN ];

    // setup the in/out channel buffers
    for(unsigned i=0; i<maxChN; ++i)
    {
      iChBuf[i] = ibuf + (i*kBufFrmN);
      oChBuf[i] = obuf + (i*kBufFrmN);
    }

    // for each frame
    for(unsigned frmIdx=0; frmIdx < maxFrmN; frmIdx += kBufFrmN )
    {
      // zero the mix buf
      memset(obuf,0,sizeof(obuf));

      unsigned maxActualFrmN = 0;

      // for each source
      for(unsigned i=0; i<srcN; ++i)
      {      
        unsigned actualFrmN = 0;
        
        // read a buffer of audio from the ith source.
        if((rc = audiofile::readFloat( hV[i], kBufFrmN, 0, channelCount(hV[i]), iChBuf, &actualFrmN)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"Read failed on source '%s'.", name(hV[i]));
          goto errLabel;
        }

        // mix the input buffer into the output buffer
        for(unsigned j=0; j<channelCount(hV[i]); ++j)
          for(unsigned k=0; k<actualFrmN; ++k)
            oChBuf[j][k] += gainV[i] * iChBuf[j][k];

        // track the max. count of samples actually read for this buffer cycle
        if( actualFrmN > maxActualFrmN )
          maxActualFrmN = actualFrmN;

      }

      // write the mixed output buffer
      if((rc = audiofile::writeFloat(oH, maxActualFrmN, maxChN, oChBuf )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Write failed on output file '%s'.", outFn );
        goto errLabel;
      }
    }
  }
  
 errLabel:
  if( rc != kOkRC )
    cwLogError(kOpFailRC,"Mix failed.");

  // close the source audio files
  for(unsigned i=0; i<srcN; ++i)
    audiofile::close(hV[i]);

  audiofile::close(oH);         // close the output file
  
  return rc;
}

cw::rc_t       cw::afop::mix( const object_t* cfg )
{
  rc_t            rc      = kOkRC;
  const object_t* srcL    = nullptr;
  const char*     oFn     = nullptr;
  unsigned        outBits = 16;

  // read the top level cfg record
  if((rc = cfg->getv("outFn",oFn,"outBits",outBits,"srcL",srcL)) != kOkRC )
    goto errLabel;
  else
  {
    char*       outFn = filesys::expandPath(oFn);
    unsigned    srcN  = srcL->child_count();
    const char* fnV[ srcN ];
    float       gainV[ srcN ];

    memset(fnV,0,sizeof(fnV));

    // read each source record
    for(unsigned i=0; i<srcN; ++i)
    {
      const char* fn                                                = nullptr;
      if((rc = srcL->child_ele(i)->getv("gain",gainV[i],"src",fn)) != kOkRC )
        rc                                                          = cwLogError(kSyntaxErrorRC,"Mix source index %i syntax error.");
      else
        fnV[i]                                                      = filesys::expandPath(fn);      
    }

    if( rc == kOkRC )
      rc = mix( fnV, gainV, srcN, outFn, outBits);

    mem::free(outFn);
    for(unsigned i=0; i<srcN; ++i)
      mem::free((char*)fnV[i]);

  }
  
 errLabel:
  return rc;
}

cw::rc_t  cw::afop::selectToFile( const char* srcFn, double begSec, double endSec, unsigned outBits, const char* outDir, const char* outFn )
{
  rc_t                rc     = kOkRC;
  char*               iFn    = filesys::expandPath(srcFn);
  char*               dstDir = filesys::expandPath(outDir);
  char*               oFn    = filesys::makeFn( dstDir, outFn, nullptr, nullptr );
  audiofile::info_t   info;
  audiofile::handle_t iH;
  audiofile::handle_t oH;
  
  // open the source file
  if((rc = audiofile::open( iH, iFn, &info )) != kOkRC )
    goto errLabel;

  // 
  if( begSec >= endSec )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid time selection. Begin time (%f) is greater than end time (%f). ", begSec, endSec );
    goto errLabel;
  }
  
  // create the output file
  if((rc = audiofile::create( oH, oFn, info.srate, outBits, info.chCnt)) != kOkRC )
    goto errLabel;
  else
  {
    unsigned       begFrmIdx     = (unsigned)floor(begSec * info.srate);
    unsigned       endFrmIdx     = endSec==-1 ? info.frameCnt : (unsigned)floor(endSec * info.srate);
    unsigned       ttlFrmN       = endFrmIdx - begFrmIdx;
    unsigned       actualBufFrmN = 0;
    const unsigned bufFrmN       = 8196; // read/write buffer size
    float          buf[ bufFrmN*info.chCnt ];
    float*         chBuf[ info.chCnt ];

    // set up the read/write channel buffer
    for(unsigned i = 0; i<info.chCnt; ++i)
      chBuf[i]     = buf + i*bufFrmN;

    // seek to first frame
    if((rc = audiofile::seek(iH,begFrmIdx)) != kOkRC )
      goto errLabel;

    // for each read/write buffer in the selected region
    for(unsigned curFrmN=0; curFrmN<ttlFrmN; curFrmN += actualBufFrmN )
    {

      // read a buffer of audio from the source.
      if((rc = audiofile::readFloat( iH, bufFrmN, 0, info.chCnt, chBuf, &actualBufFrmN)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Read failed on source '%s'.", iFn );
        goto errLabel;
      }

      // if this buffer would write more frames than the total frame count
      // then decrease the count of samples in chBuf[]
      if( curFrmN + actualBufFrmN > ttlFrmN )
        actualBufFrmN -= ttlFrmN - curFrmN;

      // write the buffer to the output file
      if((rc = audiofile::writeFloat(oH, actualBufFrmN, info.chCnt, chBuf )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Write failed on output file '%s'.", oFn );
        goto errLabel;
      }
    
    }    
  }
    
 errLabel:
  if( rc != kOkRC )
    cwLogError(rc,"selectToFile failed.");

  audiofile::close(oH);
  audiofile::close(iH);
  mem::release(iFn);
  mem::release(dstDir);
  mem::release(oFn);
  return rc;
}

cw::rc_t       cw::afop::selectToFile( const object_t* cfg )
{
  rc_t            rc      = kOkRC;
  const object_t* selectL = nullptr;
  const char*     oDir    = nullptr;
  unsigned        outBits = 16;

  // read the top level cfg record
  if((rc = cfg->getv("outDir",oDir,"outBits",outBits,"selectL",selectL)) != kOkRC )
    goto errLabel;
  else
  {
    unsigned selN = selectL->child_count();

    for(unsigned i=0; i<selN; ++i)
    {
      double      begSec = 0;
      double      endSec = -1;
      const char* dstFn = nullptr;
      const char* srcFn  = nullptr;

      if((rc = selectL->child_ele(i)->getv("begSec",begSec,"endSec",endSec,"dst",dstFn, "src",srcFn)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"'Select to file' index %i syntax error.");
        goto errLabel;
      }
      
      if((rc = selectToFile( srcFn, begSec, endSec, outBits, oDir, dstFn )) != kOkRC )
        goto errLabel;
      
    }
  }
  
 errLabel:
  return rc;
}

namespace cw
{
  namespace afop
  {
    typedef struct _cutMixArg_str
    {
      const cutMixArg_t*  arg;
      char*               srcFn;
      unsigned            srcFrmIdx;
      unsigned            srcFrmN;
      unsigned            srcFadeInFrmN;
      unsigned            srcFadeOutFrmN;
      unsigned            dstFrmIdx;
      audiofile::handle_t afH;
      audiofile::info_t   afInfo;
    } _cutMixArg_t;
    
    rc_t _cutAndMixOpen( const char* srcDir, const cutMixArg_t* argL, _cutMixArg_t* xArgL, unsigned argN, unsigned& chN_Ref, double& srate_Ref, unsigned& dstFrmN_Ref, unsigned& maxSrcFrmN_Ref )
    {
      rc_t rc = kOkRC;

      maxSrcFrmN_Ref  = 0;
      chN_Ref         = 0;
      
      for(unsigned i = 0; i<argN; ++i)
      {
        // create the source audio file name
        xArgL[i].srcFn = filesys::makeFn(srcDir, argL[i].srcFn, NULL, NULL);


        // get the audio file info
        if((rc = audiofile::open( xArgL[i].afH, xArgL[i].srcFn, &xArgL[i].afInfo )) != kOkRC )
        {
          rc = cwLogError(rc, "Unable to obtain info for the source audio file: '%s'.", cwStringNullGuard(xArgL[i].srcFn));
          goto errLabel;
        }

        // get the system sample rate from the first file
        if( i == 0 )
          srate_Ref = xArgL[i].afInfo.srate;

        // if the file sample rate does not match the system sample rate
        if( srate_Ref != xArgL[i].afInfo.srate )
        {
          rc = cwLogError(kInvalidArgRC,"'%s' sample rate %f does not match the system sample rate %f.", xArgL[i].srcFn, xArgL[i].afInfo.srate, srate_Ref );
          goto errLabel;
        }

        // verify the source file begin/end time 
        if( argL[i].srcBegSec > argL[i].srcEndSec )
        {
          rc = cwLogError(kInvalidArgRC,"The end time is prior to the begin time on source file '%s'.", xArgL[i].srcFn);
          goto errLabel;
        }

        xArgL[i].arg            = argL + i;
        xArgL[i].srcFrmIdx      = floor(argL[i].srcBegSec     * srate_Ref);
        xArgL[i].srcFrmN        = floor(argL[i].srcEndSec     * srate_Ref) - xArgL[i].srcFrmIdx;
        xArgL[i].srcFadeInFrmN  = floor(argL[i].srcBegFadeSec * srate_Ref);
        xArgL[i].srcFadeOutFrmN = floor(argL[i].srcEndFadeSec * srate_Ref);
        xArgL[i].dstFrmIdx      = floor(argL[i].dstBegSec     * srate_Ref);

        //printf("cm beg:%f end:%f dst:%f gain:%f %s\n", argL[i].srcBegSec, argL[i].srcEndSec, argL[i].dstBegSec, argL[i].gain, argL[i].srcFn );
      
        
        chN_Ref        = std::max( chN_Ref,        xArgL[i].afInfo.chCnt );
        maxSrcFrmN_Ref = std::max( maxSrcFrmN_Ref, xArgL[i].srcFrmN );

        dstFrmN_Ref = std::max( dstFrmN_Ref, xArgL[i].dstFrmIdx + xArgL[i].srcFrmN );
        
      }

    errLabel:
      return rc;
    }

    rc_t _cutAndMixClose( _cutMixArg_t* xArgL, unsigned argN )
    {
      rc_t rc = kOkRC;
      
      for(unsigned i = 0; i<argN; ++i)
      {

        if((rc = audiofile::close( xArgL[i].afH )) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"'%s' file closed.", xArgL[i].srcFn );
          goto errLabel;
            
        }
          
        mem::release( xArgL[i].srcFn );
      }
      
    errLabel:
      return rc;
    }

    enum { kLinearFadeFl = 0x01, kEqualPowerFadeFl=0x02, kFadeInFl=0x04, kFadeOutFl=0x08 };

    void _fadeOneChannel( float* xV, unsigned frmN, unsigned fadeFrmN, unsigned flags )
    {
      int i0,d,offs;
      
      if( cwIsFlag(flags,kFadeInFl ) )
      {
        // count forward
        i0   = 0;
        d    = 1;
        offs = 0;
      }
      else
      {
        // count backward
        i0 = (int)fadeFrmN;
        d    = -1;
        offs = frmN-fadeFrmN;
      }

      // do a linear fade
      if( cwIsFlag(flags,kLinearFadeFl) )
      {
        for(int i = i0,j=0; j<(int)fadeFrmN; i+=d,++j )
        {
          assert(0 <= offs+j && offs+j < (int)frmN );
          xV[offs+j] *= ((float)i) / fadeFrmN;
        }
      }
      else                      // do an equal power fade
      {
        for(int i = i0,j=0; j<(int)fadeFrmN; i+=d,++j )
        {
          assert(0 <= offs+j && offs+j < (int)frmN );
          xV[offs+j] *= std::sqrt(((float)i) / fadeFrmN);
        }
      } 
    }

    
    void _fadeAllChannels( float* chBufL[], unsigned chN, unsigned frmN, unsigned fadeFrmN, unsigned flags )
    {
      fadeFrmN = std::min(frmN,fadeFrmN);

      for(unsigned i=0; i<chN; ++i)        
        _fadeOneChannel(chBufL[i],frmN,fadeFrmN,flags);
    }
  }
}


cw::rc_t cw::afop::cutAndMix( const char* dstFn, unsigned dstBits, const char* srcDir, const cutMixArg_t* argL, unsigned argN )
{
  rc_t         rc         = kOkRC;
  unsigned     dstChN     = 0;
  double       dstSrate   = 0;
  unsigned     dstFrmN    = 0;
  unsigned     maxSrcFrmN = 0;
  float*       dstV       = nullptr;
  float*       srcV       = nullptr;
  _cutMixArg_t xArgL[ argN ];
  memset( &xArgL, 0, sizeof(xArgL));

  // open each of the source files
  if((rc = _cutAndMixOpen( srcDir, argL, xArgL, argN, dstChN, dstSrate, dstFrmN, maxSrcFrmN )) != kOkRC )
    goto errLabel;
  else
  {
    float* dstChBufL[ dstChN ];
    float* srcChBufL[ dstChN ];
    
    dstV = mem::allocZ<float>(dstFrmN*dstChN); // output signal buffer
    srcV  = mem::alloc<float>(maxSrcFrmN*dstChN); // source signal buffer

    // create the src read/ dst write buffer
    for(unsigned i=0; i<dstChN; ++i)
    {
      dstChBufL[i] = dstV + (i*dstFrmN);
      srcChBufL[i] = srcV + (i*maxSrcFrmN);
    }
  
    // for each source file
    for(unsigned i = 0; i<argN; ++i)
    {
      unsigned chIdx         = 0;
      unsigned actualFrmN    = 0;
      unsigned srcFrmN       = xArgL[i].srcFrmN;
      unsigned srcChN        = xArgL[i].afInfo.chCnt;


      // read the source segment
      if((rc = audiofile::getFloat( xArgL[i].srcFn, xArgL[i].srcFrmIdx, srcFrmN, chIdx, srcChN, srcChBufL, &actualFrmN, nullptr)) != kOkRC )
      {
        cwLogError(rc,"Error reading source audio file '%s'.", xArgL[i].srcFn );
        goto errLabel;
      }

      srcFrmN     = std::min( srcFrmN, actualFrmN ); // Track the true size of the source buffer.
      
      // Apply the fade in and out functions.
      _fadeAllChannels(srcChBufL, srcChN, srcFrmN, xArgL[i].srcFadeInFrmN,  kFadeInFl  | kLinearFadeFl );
      _fadeAllChannels(srcChBufL, srcChN, srcFrmN, xArgL[i].srcFadeOutFrmN, kFadeOutFl | kLinearFadeFl );
      
      // sum into the source signal into the output buffer
      for(unsigned j   = 0; j<srcChN; ++j)
        for(unsigned k = 0; k<srcFrmN; ++k)
        {
          assert( xArgL[i].dstFrmIdx + k < dstFrmN );
          dstChBufL[j][ xArgL[i].dstFrmIdx + k ] += xArgL[i].arg->gain * srcChBufL[j][k];
        }
      
    }


    // write the output file
    if((rc = audiofile::writeFileFloat(  dstFn, dstSrate, dstBits, dstFrmN, dstChN, dstChBufL)) != kOkRC )
    {
      cwLogError(rc,"Output file ('%s') write failed.", cwStringNullGuard(dstFn));
      goto errLabel;
    }
  }
  

 errLabel:
  _cutAndMixClose( xArgL, argN );

  mem::release(dstV);
  mem::release(srcV);
  if( rc != kOkRC )
    cwLogError(rc,"Cross-fade failed.");
  
  return rc;
}

cw::rc_t cw::afop::cutAndMix( const object_t* cfg )
{
  rc_t            rc       = kOkRC;
  const char*     srcDir   = nullptr;
  const char*     dstFn    = nullptr;
  unsigned        dstBits  = 16;
  char*           afSrcDir = nullptr;
  char*           afDstFn     = nullptr;  
  double          crossFadeSec = 0;
  const object_t* argNodeL    = nullptr;
  unsigned        i;
  
  // read the top level cfg record
  if((rc = cfg->getv("dstFn",dstFn,"dstBits",dstBits,"srcDir",srcDir,"crossFadeSec",crossFadeSec,"argL",argNodeL)) != kOkRC )
    goto errLabel;

  if( argNodeL == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"No crossfades were specified.");
  }
  else
  {
    unsigned argN = argNodeL->child_count(); 
    cutMixArg_t argL[ argN ];
        
    // for each source file
    for(i=0; i<argNodeL->child_count(); ++i)
    {
      const object_t* o = argNodeL->child_ele(i);

      // parse the non-optional parameters
      if((rc = o->getv("srcBegSec", argL[i].srcBegSec, "srcEndSec", argL[i].srcEndSec, "srcFn", argL[i].srcFn  )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Invalid crossfade argument at argument index %i.",i);
      }
      else
      {
        
        argL[i].dstBegSec     = argL[i].srcBegSec; // By default the src is moved to the same location 
        argL[i].srcBegFadeSec = crossFadeSec; // By default the beg/end fade is the global fade time. 
        argL[i].srcEndFadeSec = crossFadeSec;
        argL[i].gain          = 1;

        // parse the optional parameters
        if((rc = o->getv_opt("dstBegSec", argL[i].dstBegSec, "srcBegFadeSec", argL[i].srcBegFadeSec, "srcEndFadeSec", argL[i].srcEndFadeSec, "gain", argL[i].gain  )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"Invalid crossfade optional argument at argument index %i.",i);
        }
      }

      //printf("cm beg:%f end:%f dst:%f gain:%f %s\n", argL[i].srcBegSec, argL[i].srcEndSec, argL[i].dstBegSec, argL[i].gain, argL[i].srcFn );
      
    }

    afSrcDir = filesys::expandPath(srcDir);
    afDstFn  = filesys::expandPath(dstFn);

    // call cross-fader
    if((rc = cutAndMix( afDstFn, dstBits, afSrcDir, argL, argN )) != kOkRC )
    {
      rc = cwLogError(rc,"");
      goto errLabel;
    }

  }

 errLabel:
  mem::release(afSrcDir);
  mem::release(afDstFn);
  if( rc != kOkRC )
    rc = cwLogError(rc,"Cut and mix failed.");
  return rc;
}

cw::rc_t cw::afop::parallelMix( const char* dstFn, unsigned dstBits, const char* srcDir, const parallelMixArg_t* argL, unsigned argN )
{
  
  double fadeInSec = 0;
  cutMixArg_t cmArgL[ argN ];
  double dstBegSec = 0;
  memset(&cmArgL,0,sizeof(cmArgL));
  
  for(unsigned i=0; i<argN; ++i)
  {
    cmArgL[i].srcFn          = argL[i].srcFn;
    cmArgL[i].srcBegSec      = argL[i].srcBegSec;
    cmArgL[i].srcEndSec      = argL[i].srcEndSec + argL[i].fadeOutSec;
    cmArgL[i].srcBegFadeSec  = fadeInSec;
    cmArgL[i].srcEndFadeSec  = argL[i].fadeOutSec;
    cmArgL[i].dstBegSec      = dstBegSec;
    cmArgL[i].gain           = argL[i].gain;

    dstBegSec               += argL[i].srcEndSec - argL[i].srcBegSec;
    fadeInSec                = argL[i].fadeOutSec;
  }

  return cutAndMix( dstFn, dstBits, srcDir, cmArgL, argN );
}

cw::rc_t cw::afop::parallelMix( const object_t* cfg )
{
  rc_t            rc       = kOkRC;
  const char*     srcDir   = nullptr;
  const char*     dstFn    = nullptr;
  unsigned        dstBits  = 16;
  char*           afSrcDir = nullptr;
  char*           afDstFn     = nullptr;  
  const object_t* argNodeL    = nullptr;
  unsigned        i;

  // read the top level cfg record
  if((rc = cfg->getv("dstFn",dstFn,"dstBits",dstBits,"srcDir",srcDir,"argL",argNodeL)) != kOkRC )
    goto errLabel;

  if( argNodeL == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"No crossfades were specified.");
  }
  else
  {
    unsigned argN = argNodeL->child_count(); 
    parallelMixArg_t argL[ argN ];
        
    // for each source file
    for(i=0; i<argNodeL->child_count(); ++i)
    {
      const object_t* o = argNodeL->child_ele(i);

      // parse the non-optional parameters
      if((rc = o->getv("srcBegSec", argL[i].srcBegSec, "srcEndSec", argL[i].srcEndSec, "fadeOutSec", argL[i].fadeOutSec, "srcFn", argL[i].srcFn  )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Invalid xform app argument at argument index %i.",i);
      }
      else
      {
        argL[i].gain = 1;
        
        // parse the optional parameters
        if((rc = o->getv_opt("gain", argL[i].gain  )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"Invalid xform app optional argument at argument index %i.",i);
        }
      }

      //printf("beg:%f end:%f fade:%f %s\n", argL[i].srcBegSec, argL[i].srcEndSec, argL[i].fadeOutSec, argL[i].srcFn );
      
    }

    afSrcDir = filesys::expandPath(srcDir);
    afDstFn  = filesys::expandPath(dstFn);

    // call cross-fader
    if((rc = parallelMix( afDstFn, dstBits, afSrcDir, argL, argN )) != kOkRC )
      goto errLabel;

  }

 errLabel:
  mem::release(afSrcDir);
  mem::release(afDstFn);
  if( rc != kOkRC )
    rc = cwLogError(rc,"Parallel-mix failed.");
  return rc;
}

cw::rc_t cw::afop::transformApp( const object_t* cfg )
{
  rc_t            rc        = kOkRC;
  const char*     srcDir    = nullptr;
  const char*     dryFn     = nullptr;
  const char*     dstPreFn  = nullptr;
  const char*     dstRevFn  = nullptr;
  unsigned        dstBits   = 16;
  const object_t* argNodeL  = nullptr;
  const char*     irFn      = nullptr;
  double          irScale   = 1;

  char* expSrcDir   = nullptr;
  char* expDryFn    = nullptr;
  char* expDstPreFn = nullptr;  
  char* expDstRevFn = nullptr;  
  char* expIrFn     = nullptr;
  
  unsigned        i;

  // read the top level cfg record
  if((rc = cfg->getv("dstPreFn",dstPreFn,"dstRevFn",dstRevFn,"dstBits",dstBits,"srcDir",srcDir,"argL",argNodeL,"dryFn",dryFn,"irFn",irFn,"irScale",irScale)) != kOkRC )
    goto errLabel;


  expSrcDir   = filesys::expandPath(srcDir);
  expDryFn    = filesys::expandPath(dryFn);
  expDstPreFn = filesys::expandPath(dstPreFn);
  expDstRevFn = filesys::expandPath(dstRevFn);
  expIrFn     = filesys::expandPath(irFn);


  if( argNodeL == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"No crossfades were specified.");
  }
  else
  {
    unsigned argN = argNodeL->child_count() * 2; 
    parallelMixArg_t argL[ argN ];
        
    // for each source file
    for(i=0; i<argNodeL->child_count(); ++i)
    {
      const object_t* o = argNodeL->child_ele(i);

      unsigned j = i*2;

      // parse the non-optional parameters
      if((rc = o->getv("srcBegSec", argL[j].srcBegSec, "srcEndSec", argL[j].srcEndSec, "fadeOutSec", argL[j].fadeOutSec, "srcFn", argL[j].srcFn  )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Invalid xform app argument at argument index %i.",i);
      }
      else
      {
        argL[j].gain = 1;
        
        // parse the optional parameters
        if((rc = o->getv_opt("wetGain", argL[j].gain  )) != kOkRC )
        {
          rc = cwLogError(kInvalidArgRC,"Invalid xform app optional argument at argument index %i.",i);
        }
      }

      // expand the source file name
      argL[j].srcFn  = filesys::makeFn(expSrcDir, argL[j].srcFn, NULL, NULL);

      // form the dry file name
      argL[j+1]       = argL[j];
      argL[j+1].srcFn = expDryFn;
      argL[j+1].gain  = 1.0 - argL[j].gain;
    }

    // call cross-fader
    rc = parallelMix( expDstPreFn, dstBits, "", argL, argN );

    for(unsigned i=0; i<argN; i+=2)
      mem::release( const_cast<char*&>(argL[i].srcFn));

    if( rc == kOkRC )
      rc = convolve( expDstRevFn, dstBits, expDstPreFn, expIrFn, irScale );        

  }

 errLabel:

  
  
  mem::release(expSrcDir);
  mem::release(expDryFn);
  mem::release(expDstPreFn);
  mem::release(expDstRevFn);
  mem::release(expIrFn);
  
  if( rc != kOkRC )
    rc = cwLogError(rc,"Parallel-mix failed.");
  return rc;
  
}



cw::rc_t cw::afop::convolve( const char* dstFn, unsigned dstBits, const char* srcFn, const char* impulseResponseFn, float irScale )
{
  rc_t              rc     = kOkRC;
  float**           hChBuf = nullptr;
  unsigned          hChN   = 0;
  unsigned          hFrmN  = 0;
  double            hSrate = 0;
  float**           xChBuf = nullptr;
  unsigned          xChN   = 0;
  unsigned          xFrmN  = 0;
  audiofile::info_t info;

  audiofile::reportInfo(impulseResponseFn);
  audiofile::reportInfo(srcFn);

  // read the impulse response audio file
  if((rc = audiofile::allocFloatBuf(impulseResponseFn, hChBuf, hChN, hFrmN, info)) != kOkRC )
  {
    rc = cwLogError(rc,"The impulse audio file read failed on '%s'.", cwStringNullGuard(impulseResponseFn));
    goto errLabel;
  }

  hSrate = info.srate;

  // read the source audio file
  if((rc = audiofile::allocFloatBuf(srcFn, xChBuf, xChN, xFrmN, info)) != kOkRC )
  {
    rc = cwLogError(rc,"The source audio file read failed on '%s'.", cwStringNullGuard(srcFn));
    goto errLabel;
  }

  // the sample rate of impulse response and src audio signals must be the same
  if( hSrate != info.srate )
  {
    rc = cwLogError(kInvalidArgRC,"The soure file sample rate %f does not match the impulse response sample rate %f.",info.srate,hSrate);    
  }
  else
  {
    // allocate the output buffer
    float*   yChBuf[ xChN ];
    unsigned yFrmN = xFrmN + hFrmN;
    for(unsigned i=0; i<xChN; ++i)
      yChBuf[i] = mem::allocZ<float>( yFrmN );

    //printf("xFrmN:%i xChN:%i hFrmN:%i hChN:%i yFrmN:%i\n",xFrmN,xChN,hFrmN,hChN,yFrmN);

    // scale the impulse response
    for(unsigned i=0; i<hChN; ++i)
      vop::mul( hChBuf[i], irScale, hFrmN );

    // for each source channel
    for(unsigned i=0; i<xChN  && rc == kOkRC; ++i)
    {
      unsigned j = i >= hChN ? 0 : i; // select the impulse response channel

      // convolve this channel with the impulse response and store into the output buffer
      rc = dsp::convolve::apply( xChBuf[i], xFrmN, hChBuf[j], hFrmN, yChBuf[i], yFrmN );
    }

    // write the output file.
    if( rc == kOkRC )
      rc = audiofile::writeFileFloat( dstFn, info.srate, dstBits, yFrmN, xChN, yChBuf);
       

    // release the output buffer
    for(unsigned i=0; i<xChN; ++i)
      mem::release( yChBuf[i] );    
  }
  
 errLabel:
  if(rc != kOkRC )
    cwLogError(rc,"Audio file convolve failed.");
  
  audiofile::freeFloatBuf(hChBuf, hChN );
  audiofile::freeFloatBuf(xChBuf, xChN );

  return rc;
}

cw::rc_t cw::afop::convolve( const object_t* cfg )
{
  rc_t            rc       = kOkRC;
  const char*     srcFn    = nullptr;
  const char*     dstFn    = nullptr;
  const char*     irFn     = nullptr;
  float           irScale   = 1.0;
  unsigned        dstBits  = 16;
  

  // read the top level cfg record
  if((rc = cfg->getv("dstFn",dstFn,"dstBits",dstBits,"srcFn",srcFn,"irFn",irFn,"irScale",irScale)) != kOkRC )
  {
    cwLogError(rc,"convolve() arg. parse failed.");
  }
  else
  {
    char* sFn = filesys::expandPath(srcFn);
    char* dFn = filesys::expandPath(dstFn);
    char* iFn = filesys::expandPath(irFn);

    rc = convolve( dFn, dstBits, sFn, iFn, irScale );
    
    mem::release(sFn);
    mem::release(dFn);
    mem::release(iFn);
  }

  
  return rc;
}



cw::rc_t cw::afop::test( const object_t* cfg )
{
  rc_t rc = kOkRC;
  const object_t* o;
  
  if((o = cfg->find("sine")) != nullptr )
    rc = sine(o);

  return rc;
}
