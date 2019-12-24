#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTextBuf.h"
#include "cwTime.h"
#include "cwAudioPort.h"
#include "cwAudioBuf.h"


#ifdef cwLINUX
#include "cwAudioPortAlsa.h"
#endif

#ifdef cwOSX
#include "cmAudioPortOsx.h"
#endif

namespace cw
{
  namespace audio
  {
    namespace device
    {

      typedef struct
      {
        unsigned      begDevIdx;
        unsigned      endDevIdx;

        rc_t        (*initialize)( unsigned baseApDevIdx );
        rc_t        (*finalize)();
        rc_t        (*deviceCount)();
        const char* (*deviceLabel)(          unsigned devIdx );
        unsigned    (*deviceChannelCount)(   unsigned devIdx, bool inputFl );
        double      (*deviceSampleRate)(    unsigned devIdx );
        unsigned    (*deviceFramesPerCycle)( unsigned devIdx, bool inputFl );
        rc_t        (*deviceSetup)( unsigned devIdx, double sr, unsigned frmPerCycle, cbFunc_t cb, void* cbData );
        rc_t        (*deviceStart)( unsigned devIdx );
        rc_t        (*deviceStop)(  unsigned devIdx );
        bool        (*deviceIsStarted)( unsigned devIdx );

      } cmApDriver_t;

      typedef struct
      {
        cmApDriver_t*  drvArray;
        unsigned       drvCnt;
        unsigned       devCnt;
      } cmAp_t;

      cmAp_t* _ap = NULL;

      rc_t      _cmApIndexToDev( unsigned devIdx, cmApDriver_t** drvPtrPtr, unsigned* devIdxPtr )
      {
        assert( drvPtrPtr != NULL && devIdxPtr != NULL );
        *drvPtrPtr = NULL;
        *devIdxPtr = kInvalidIdx;

        unsigned i;
        for(i=0; i<_ap->drvCnt; ++i)
          if( _ap->drvArray[i].begDevIdx != kInvalidIdx )
            if( (_ap->drvArray[i].begDevIdx <= devIdx) && (devIdx <= _ap->drvArray[i].endDevIdx) )
            {
              *drvPtrPtr = _ap->drvArray + i;
              *devIdxPtr = devIdx - _ap->drvArray[i].begDevIdx;
              return kOkRC;
            }
  
        return cwLogError(kInvalidIdRC,"The audio port device index %i is not valid.",devIdx);
      }
    }
  }
}

cw::rc_t      cw::audio::device::initialize()
{
  rc_t rc = kOkRC;
  if((rc = finalize()) != kOkRC )
    return rc;

  _ap = memAllocZ<cmAp_t>(1);

  _ap->drvCnt = 1;
  _ap->drvArray = memAllocZ<cmApDriver_t>(_ap->drvCnt);
  cmApDriver_t* dp = _ap->drvArray;
  
#ifdef cwOSX
  dp->initialize           = cmApOsxInitialize;
  dp->finalize             = cmApOsxFinalize;
  dp->deviceCount          = cmApOsxDeviceCount;
  dp->deviceLabel          = cmApOsxDeviceLabel;
  dp->deviceChannelCount   = cmApOsxDeviceChannelCount;
  dp->deviceSampleRate     = cmApOsxDeviceSampleRate;
  dp->deviceFramesPerCycle = cmApOsxDeviceFramesPerCycle;
  dp->deviceSetup          = cmApOsxDeviceSetup;
  dp->deviceStart          = cmApOsxDeviceStart;
  dp->deviceStop           = cmApOsxDeviceStop;
  dp->deviceIsStarted      = cmApOsxDeviceIsStarted;  
#endif

#ifdef cwLINUX
  dp->initialize           = alsa::initialize;
  dp->finalize             = alsa::finalize;
  dp->deviceCount          = alsa::deviceCount;
  dp->deviceLabel          = alsa::deviceLabel;
  dp->deviceChannelCount   = alsa::deviceChannelCount;
  dp->deviceSampleRate     = alsa::deviceSampleRate;
  dp->deviceFramesPerCycle = alsa::deviceFramesPerCycle;
  dp->deviceSetup          = alsa::deviceSetup;
  dp->deviceStart          = alsa::deviceStart;
  dp->deviceStop           = alsa::deviceStop;
  dp->deviceIsStarted      = alsa::deviceIsStarted;  
#endif

  /*
  dp = _ap->drvArray + 1;

  dp->initialize           = cmApFileInitialize;
  dp->finalize             = cmApFileFinalize;
  dp->deviceCount          = cmApFileDeviceCount;
  dp->deviceLabel          = cmApFileDeviceLabel;
  dp->deviceChannelCount   = cmApFileDeviceChannelCount;
  dp->deviceSampleRate     = cmApFileDeviceSampleRate;
  dp->deviceFramesPerCycle = cmApFileDeviceFramesPerCycle;
  dp->deviceSetup          = cmApFileDeviceSetup;
  dp->deviceStart          = cmApFileDeviceStart;
  dp->deviceStop           = cmApFileDeviceStop;
  dp->deviceIsStarted      = cmApFileDeviceIsStarted;  

  dp = _ap->drvArray + 2;

  dp->initialize           = cmApAggInitialize;
  dp->finalize             = cmApAggFinalize;
  dp->deviceCount          = cmApAggDeviceCount;
  dp->deviceLabel          = cmApAggDeviceLabel;
  dp->deviceChannelCount   = cmApAggDeviceChannelCount;
  dp->deviceSampleRate     = cmApAggDeviceSampleRate;
  dp->deviceFramesPerCycle = cmApAggDeviceFramesPerCycle;
  dp->deviceSetup          = cmApAggDeviceSetup;
  dp->deviceStart          = cmApAggDeviceStart;
  dp->deviceStop           = cmApAggDeviceStop;
  dp->deviceIsStarted      = cmApAggDeviceIsStarted;  

  dp = _ap->drvArray + 3;

  dp->initialize           = cmApNrtInitialize;
  dp->finalize             = cmApNrtFinalize;
  dp->deviceCount          = cmApNrtDeviceCount;
  dp->deviceLabel          = cmApNrtDeviceLabel;
  dp->deviceChannelCount   = cmApNrtDeviceChannelCount;
  dp->deviceSampleRate     = cmApNrtDeviceSampleRate;
  dp->deviceFramesPerCycle = cmApNrtDeviceFramesPerCycle;
  dp->deviceSetup          = cmApNrtDeviceSetup;
  dp->deviceStart          = cmApNrtDeviceStart;
  dp->deviceStop           = cmApNrtDeviceStop;
  dp->deviceIsStarted      = cmApNrtDeviceIsStarted;  
  */
  
  _ap->devCnt = 0;

  unsigned i;
  for(i=0; i<_ap->drvCnt; ++i)
  {
    unsigned dn; 
    rc_t rc0;

    _ap->drvArray[i].begDevIdx = kInvalidIdx;
    _ap->drvArray[i].endDevIdx = kInvalidIdx;

    if((rc0 = _ap->drvArray[i].initialize(_ap->devCnt)) != kOkRC )
    {
      rc = rc0;
      continue;
    }

    if((dn = _ap->drvArray[i].deviceCount()) > 0)
    {
      _ap->drvArray[i].begDevIdx = _ap->devCnt;
      _ap->drvArray[i].endDevIdx = _ap->devCnt + dn - 1;
      _ap->devCnt += dn;
    }
  }

  if( rc != kOkRC )
    finalize();

  return rc;
}

cw::rc_t      cw::audio::device::finalize()
{
  rc_t rc=kOkRC;
  rc_t rc0 = kOkRC;
  unsigned i;

  if( _ap == NULL )
    return kOkRC;

  for(i=0; i<_ap->drvCnt; ++i)
  {
    if((rc0 = _ap->drvArray[i].finalize()) != kOkRC )
      rc = rc0;
  }

  memRelease(_ap->drvArray);
  memRelease(_ap);
  return rc;
}


unsigned cw::audio::device::deviceCount()
{ return _ap->devCnt; }

const char*   cw::audio::device::deviceLabel( unsigned devIdx )
{
  cmApDriver_t* dp = NULL;
  unsigned      di = kInvalidIdx;
  rc_t      rc;

  if( devIdx == kInvalidIdx )
    return NULL;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return cwStringNullGuard(NULL);

  return dp->deviceLabel(di);
}

unsigned      cw::audio::device::deviceLabelToIndex( const char* label )
{
  unsigned n = deviceCount();
  unsigned i;
  for(i=0; i<n; ++i)
  {
    const char* s = deviceLabel(i);
    if( s!=NULL && strcmp(s,label)==0)
      return i;
  }
  return kInvalidIdx;
}


unsigned      cw::audio::device::deviceChannelCount(   unsigned devIdx, bool inputFl )
{
  cmApDriver_t* dp = NULL;
  unsigned      di = kInvalidIdx;
  rc_t      rc;

  if( devIdx == kInvalidIdx )
    return 0;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return rc;

  return dp->deviceChannelCount(di,inputFl);
}

double        cw::audio::device::deviceSampleRate(     unsigned devIdx )
{
  cmApDriver_t* dp = NULL;
  unsigned      di = kInvalidIdx;
  rc_t      rc;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return rc;

  return dp->deviceSampleRate(di);
}

unsigned      cw::audio::device::deviceFramesPerCycle( unsigned devIdx, bool inputFl )
{
  cmApDriver_t* dp = NULL;
  unsigned      di = kInvalidIdx;
  rc_t      rc;

  if( devIdx == kInvalidIdx )
    return 0;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return rc;

  return dp->deviceFramesPerCycle(di,inputFl);
}

cw::rc_t      cw::audio::device::deviceSetup(          
  unsigned          devIdx, 
  double            srate, 
  unsigned          framesPerCycle, 
  cbFunc_t          cbFunc,
  void*             cbArg )
{
  cmApDriver_t* dp;
  unsigned      di;
  rc_t      rc;

  if( devIdx == kInvalidIdx )
    return kOkRC;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return rc;

  return dp->deviceSetup(di,srate,framesPerCycle,cbFunc,cbArg);
}

cw::rc_t      cw::audio::device::deviceStart( unsigned devIdx )
{
  cmApDriver_t* dp;
  unsigned      di;
  rc_t      rc;

  if( devIdx == kInvalidIdx )
    return kOkRC;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return rc;

  return dp->deviceStart(di);
}

cw::rc_t      cw::audio::device::deviceStop(  unsigned devIdx )
{
  cmApDriver_t* dp;
  unsigned      di;
  rc_t      rc;

  if( devIdx == kInvalidIdx )
    return kOkRC;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return rc;

  return dp->deviceStop(di);
}

bool          cw::audio::device::deviceIsStarted( unsigned devIdx )
{
  cmApDriver_t* dp;
  unsigned      di;
  rc_t      rc;

  if( devIdx == kInvalidIdx )
    return false;

  if((rc = _cmApIndexToDev(devIdx,&dp,&di)) != kOkRC )
    return rc;

  return dp->deviceIsStarted(di);
}



void cw::audio::device::report()
{
  unsigned i,j,k;
  for(j=0,k=0; j<_ap->drvCnt; ++j)
  {
    cmApDriver_t* drvPtr = _ap->drvArray + j;
    unsigned      n      = drvPtr->deviceCount(); 

    for(i=0; i<n; ++i,++k)
    {
      cwLogInfo( "%i %f in:%i (%i) out:%i (%i) %s",
        k, drvPtr->deviceSampleRate(i),
        drvPtr->deviceChannelCount(i,true),  drvPtr->deviceFramesPerCycle(i,true),
        drvPtr->deviceChannelCount(i,false), drvPtr->deviceFramesPerCycle(i,false),
        drvPtr->deviceLabel(i));
  
    }
  }

  //cmApAlsaDeviceReport(rpt);
}

namespace  cw
{
  namespace audio
  {
    namespace device
    {
      /// [cmAudioPortExample]

      // See cmApPortTest() below for the main point of entry.

      // Data structure used to hold the parameters for cpApPortTest()
      // and the user defined data record passed to the host from the
      // audio port callback functions.
      typedef struct
      {
        unsigned      bufCnt;         // 2=double buffering 3=triple buffering
        unsigned      chIdx;          // first test channel
        unsigned      chCnt;          // count of channels to test
        unsigned      framesPerCycle; // DSP frames per cycle
        unsigned      bufFrmCnt;      // count of DSP frames used by the audio buffer  (bufCnt * framesPerCycle)
        unsigned      bufSmpCnt;      // count of samples used by the audio buffer     (chCnt  * bufFrmCnt)
        unsigned      inDevIdx;       // input device index
        unsigned      outDevIdx;      // output device index
        double        srate;          // audio sample rate
        unsigned      meterMs;        // audio meter buffer length

        // param's and state for cmApSynthSine()
        unsigned      phase;          // sine synth phase
        double        frqHz;          // sine synth frequency in Hz

        // buffer and state for cmApCopyIn/Out()
        sample_t*     buf;            // buf[bufSmpCnt] - circular interleaved audio buffer
        unsigned      bufInIdx;       // next input buffer index
        unsigned      bufOutIdx;      // next output buffer index
        unsigned      bufFullCnt;     // count of full samples

        // debugging log data arrays 
        unsigned      logCnt;        // count of elements in log[] and ilong[]
        char*         log;           // log[logCnt]
        unsigned*     ilog;          // ilog[logCnt]
        unsigned      logIdx;        // current log index

        unsigned      cbCnt;         // count the callback
      } cmApPortTestRecd;


#ifdef NOT_DEF
      // The application can request any block of channels from the device. The packets are provided with the starting
      // device channel and channel count.  This function converts device channels and channel counts to buffer
      // channel indexes and counts.  
      //
      //  Example:
      //      input                            output
      //       i,n                              i n
      //  App: 0,4   0 1 2 3                ->  2 2
      //  Pkt  2,8       2 3 4 5 6 7 8      ->  0 2
      //
      // The return value is the count of application requested channels located in this packet.
      //
      // input: *appChIdxPtr and appChCnt describe a block of device channels requested by the application.
      //        *pktChIdxPtr and pktChCnt describe a block of device channels provided to the application
      //
      // output:*appChIdxPtr and <return value> describe a block of app buffer channels which will send/recv samples.
      //        *pktChIdxPtr and <return value>  describe a block of pkt buffer channels which will send/recv samples
      //
      unsigned _cmApDeviceToBuffer( unsigned* appChIdxPtr, unsigned appChCnt, unsigned* pktChIdxPtr, unsigned pktChCnt )
      {
        unsigned abi = *appChIdxPtr;
        unsigned aei = abi+appChCnt-1;

        unsigned pbi = *pktChIdxPtr;
        unsigned pei = pbi+pktChCnt-1;

        // if the ch's rqstd by the app do not overlap with this packet - return false.
        if( aei < pbi || abi > pei )
          return 0;

        // if the ch's rqstd by the app overlap with the beginning of the pkt channel block
        if( abi < pbi )
        {
          appChCnt     -= pbi - abi;
          *appChIdxPtr  = pbi - abi;
          *pktChIdxPtr  = 0;
        }
        else
        {
          // the rqstd ch's begin inside the pkt channel block
          pktChCnt     -= abi - pbi;
          *pktChIdxPtr  = abi - pbi;
          *appChIdxPtr  = 0;
        }

        // if the pkt channels extend beyond the rqstd ch block
        if( aei < pei )
          pktChCnt -= pei - aei;
        else 
          appChCnt -= aei - pei; // the rqstd ch's extend beyond or coincide with the pkt block

        // the returned channel count must always be the same for both the rqstd and pkt 
        return cmMin(appChCnt,pktChCnt);

      }


      // synthesize a sine signal into an interleaved audio buffer
      unsigned _cmApSynthSine( cmApPortTestRecd* r, float* p, unsigned chIdx, unsigned chCnt, unsigned frmCnt, unsigned phs, double hz )
      {
        long     ph = 0;
        unsigned i;
        unsigned bufIdx    = r->chIdx;
        unsigned bufChCnt;

        if( (bufChCnt =  _cmApDeviceToBuffer( &bufIdx, r->chCnt, &chIdx, chCnt )) == 0)
          return phs;

  
        //if( r->cbCnt < 50 )
        //  printf("ch:%i cnt:%i  ch:%i cnt:%i  bi:%i bcn:%i\n",r->chIdx,r->chCnt,chIdx,chCnt,bufIdx,bufChCnt);
 

        for(i=bufIdx; i<bufIdx+bufChCnt; ++i)
        {
          unsigned j;
          float*   op = p + i;

          ph = phs;
          for(j=0; j<frmCnt; j++, op+=chCnt, ph++)
          {
            *op = (float)(0.9 * sin( 2.0 * M_PI * hz * ph / r->srate ));
          }
        }
  
        return ph;
      }

      // Copy the audio samples in the interleaved audio buffer sp[srcChCnt*srcFrameCnt]
      // to the internal record buffer.
      void _cmApCopyIn( cmApPortTestRecd* r, const sample_t* sp, unsigned srcChIdx, unsigned srcChCnt, unsigned srcFrameCnt  )
      {
        unsigned i,j;

        unsigned chCnt = cmMin(r->chCnt,srcChCnt);

        for(i=0; i<srcFrameCnt; ++i)
        {
          for(j=0; j<chCnt; ++j)
            r->buf[ r->bufInIdx + j ] = sp[ (i*srcChCnt) + j ];

          for(; j<r->chCnt; ++j)
            r->buf[ r->bufInIdx + j ] = 0;

          r->bufInIdx = (r->bufInIdx+r->chCnt) % r->bufFrmCnt;
        }

        //r->bufFullCnt = (r->bufFullCnt + srcFrameCnt) % r->bufFrmCnt;
        r->bufFullCnt += srcFrameCnt;
      }

      // Copy audio samples out of the internal record buffer into dp[dstChCnt*dstFrameCnt].
      void _cmApCopyOut( cmApPortTestRecd* r, sample_t* dp, unsigned dstChIdx, unsigned dstChCnt, unsigned dstFrameCnt )
      {
        // if there are not enough samples available to fill the destination buffer then zero the dst buf.
        if( r->bufFullCnt < dstFrameCnt )
        {
          printf("Empty Output Buffer\n");
          memset( dp, 0, dstFrameCnt*dstChCnt*sizeof(sample_t) );
        }
        else
        {
          unsigned i,j;
          unsigned chCnt = cmMin(dstChCnt, r->chCnt);

          // for each output frame
          for(i=0; i<dstFrameCnt; ++i)
          {
            // copy the first chCnt samples from the internal buf to the output buf
            for(j=0; j<chCnt; ++j)
              dp[ (i*dstChCnt) + j ] = r->buf[ r->bufOutIdx + j ];

            // zero any output ch's for which there is no internal buf channel
            for(; j<dstChCnt; ++j)
              dp[ (i*dstChCnt) + j ] = 0;

            // advance the internal buffer
            r->bufOutIdx = (r->bufOutIdx + r->chCnt) % r->bufFrmCnt;
          }

          r->bufFullCnt -= dstFrameCnt;
        }
      }

      // Audio port callback function called from the audio device thread.
      void _cmApPortCb( cmApAudioPacket_t* inPktArray, unsigned inPktCnt, cmApAudioPacket_t* outPktArray, unsigned outPktCnt )
      {
        unsigned i;

        // for each incoming audio packet
        for(i=0; i<inPktCnt; ++i)
        {
          cmApPortTestRecd* r = (cmApPortTestRecd*)inPktArray[i].userCbPtr; 

          if( inPktArray[i].devIdx == r->inDevIdx )
          {
            // copy the incoming audio into an internal buffer where it can be picked up by _cpApCopyOut().
            _cmApCopyIn( r, (sample_t*)inPktArray[i].audioBytesPtr, inPktArray[i].begChIdx, inPktArray[i].chCnt, inPktArray[i].audioFramesCnt );
          }
          ++r->cbCnt;

          //printf("i %4i in:%4i out:%4i\n",r->bufFullCnt,r->bufInIdx,r->bufOutIdx);
        }

        unsigned hold_phase = 0;

        // for each outgoing audio packet
        for(i=0; i<outPktCnt; ++i)
        {
          cmApPortTestRecd* r = (cmApPortTestRecd*)outPktArray[i].userCbPtr; 

          if( outPktArray[i].devIdx == r->outDevIdx )
          {
            // zero the output buffer
            memset(outPktArray[i].audioBytesPtr,0,outPktArray[i].chCnt * outPktArray[i].audioFramesCnt * sizeof(sample_t) );
      
            // if the synth is enabled
            if( r->synthFl )
            {
              unsigned tmp_phase  = _cmApSynthSine( r, outPktArray[i].audioBytesPtr, outPktArray[i].begChIdx, outPktArray[i].chCnt, outPktArray[i].audioFramesCnt, r->phase, r->frqHz );  

              // the phase will only change on packets that are actually used
              if( tmp_phase != r->phase )
                hold_phase = tmp_phase;
            }
            else
            {
              // copy the any audio in the internal record buffer to the playback device 
              _cmApCopyOut( r, (sample_t*)outPktArray[i].audioBytesPtr, outPktArray[i].begChIdx, outPktArray[i].chCnt, outPktArray[i].audioFramesCnt );   
            }
          }

          r->phase = hold_phase;

          //printf("o %4i in:%4i out:%4i\n",r->bufFullCnt,r->bufInIdx,r->bufOutIdx);
          // count callbacks
          ++r->cbCnt;
        }
      }
#endif

      // print the usage message for cmAudioPortTest.c
      void _cmApPrintUsage()
      {
        char msg[] =
          "cmApPortTest() command switches\n"
          "-r <srate> -c <chcnt> -b <bufcnt> -f <frmcnt> -i <idevidx> -o <odevidx> -t -p -h \n"
          "\n"
          "-r <srate> = sample rate\n"
          "-a <chidx> = first channel\n"
          "-c <chcnt> = audio channels\n"
          "-b <bufcnt> = count of buffers\n"
          "-f <frmcnt> = count of samples per buffer\n"
          "-i <idevidx> = input device index\n"
          "-o <odevidx> = output device index\n"
          "-p = print report but do not start audio devices\n"
          "-h = print this usage message\n";

        cwLogInfo(msg);
      }

      // Get a command line option.
      int _cmApGetOpt( int argc, const char* argv[], const char* label, int defaultVal, bool boolFl )
      {
        int i = 0;
        for(; i<argc; ++i)
          if( strcmp(label,argv[i]) == 0 )
          {
            if(boolFl)
              return 1;

            if( i == (argc-1) )
              return defaultVal;

            return atoi(argv[i+1]);
          }
  
        return defaultVal;
      }

      unsigned _cmGlobalInDevIdx  = 0;
      unsigned _cmGlobalOutDevIdx = 0;

      void _cmApPortCb2( audioPacket_t* inPktArray, unsigned inPktCnt, audioPacket_t* outPktArray, unsigned outPktCnt )
      {
        buf::inputToOutput( _cmGlobalInDevIdx, _cmGlobalOutDevIdx );

        buf::update( inPktArray, inPktCnt, outPktArray, outPktCnt );
      }
    }
  }
}

// Audio Port testing function
cw::rc_t cw::audio::device::test( bool runFl,  int argc, const char** argv )
{
  cmApPortTestRecd  r;
  unsigned          i;
  int               result = 0;
  textBuf::handle_t tbH = textBuf::create();

  if( _cmApGetOpt(argc,argv,"-h",0,true) )
    _cmApPrintUsage();


  runFl            = _cmApGetOpt(argc,argv,"-p",!runFl,true)?false:true;
  r.srate          = _cmApGetOpt(argc,argv,"-r",44100,false);
  r.chIdx          = _cmApGetOpt(argc,argv,"-a",0,false);
  r.chCnt          = _cmApGetOpt(argc,argv,"-c",2,false);
  r.bufCnt         = _cmApGetOpt(argc,argv,"-b",3,false);
  r.framesPerCycle = _cmApGetOpt(argc,argv,"-f",512,false);
  r.bufFrmCnt      = (r.bufCnt*r.framesPerCycle);
  r.bufSmpCnt      = (r.chCnt  * r.bufFrmCnt);
  r.logCnt         = 100; 
  r.meterMs        = 50;

  sample_t     buf[r.bufSmpCnt];
  char         log[r.logCnt];
  unsigned    ilog[r.logCnt];
  
  r.inDevIdx   = _cmGlobalInDevIdx  = _cmApGetOpt(argc,argv,"-i",0,false);   
  r.outDevIdx  = _cmGlobalOutDevIdx = _cmApGetOpt(argc,argv,"-o",2,false); 
  r.phase      = 0;
  r.frqHz      = 2000;
  r.bufInIdx   = 0;
  r.bufOutIdx  = 0;
  r.bufFullCnt = 0;
  r.logIdx     = 0;

  r.buf        = buf;
  r.log        = log;
  r.ilog       = ilog;
  r.cbCnt      = 0;

  cwLogInfo("%s in:%i out:%i chidx:%i chs:%i bufs=%i frm=%i rate=%f",runFl?"exec":"rpt",r.inDevIdx,r.outDevIdx,r.chIdx,r.chCnt,r.bufCnt,r.framesPerCycle,r.srate);

  /*
  if( cmApFileAllocate(rpt) != kOkRC )
  {
    cwLogInfo("Audio port file allocation failed.");
    result = -1;
    goto errLabel;
  }

  // allocate the non-real-time port
  if( cmApNrtAllocate(rpt) != kOkRC )
  {
    cwLogInfo("Non-real-time system allocation failed.");
    result = 1;
    goto errLabel;
  }
  */
  
  // initialize the audio device interface
  if( initialize() != kOkRC )
  {
    cwLogInfo("Initialize failed.");
    result = 1;
    goto errLabel;
  }
  
  // report the current audio device configuration
  for(i=0; i<deviceCount(); ++i)
  {
    cwLogInfo("%i [in: chs=%i frames=%i] [out: chs=%i frames=%i] srate:%f %s",i,deviceChannelCount(i,true),deviceFramesPerCycle(i,true),deviceChannelCount(i,false),deviceFramesPerCycle(i,false),deviceSampleRate(i),deviceLabel(i));
  }
  // report the current audio devices using the audio port interface function
  report();

  if( runFl )
  {
    // initialize the audio bufer
    buf::initialize( deviceCount(), r.meterMs );

    // setup the buffer for the output device
    buf::setup( r.outDevIdx, r.srate, r.framesPerCycle, r.bufCnt, deviceChannelCount(r.outDevIdx,true), r.framesPerCycle, deviceChannelCount(r.outDevIdx,false), r.framesPerCycle );

    // setup the buffer for the input device
    if( r.inDevIdx != r.outDevIdx )
      buf::setup( r.inDevIdx, r.srate, r.framesPerCycle, r.bufCnt, deviceChannelCount(r.inDevIdx,true), r.framesPerCycle, deviceChannelCount(r.inDevIdx,false), r.framesPerCycle ); 

    // setup an output device
    if(deviceSetup(r.outDevIdx,r.srate,r.framesPerCycle,_cmApPortCb2,&r) != kOkRC )
      cwLogInfo("Out device setup failed.");
    else
      // setup an input device
      if( deviceSetup(r.inDevIdx,r.srate,r.framesPerCycle,_cmApPortCb2,&r) != kOkRC )
        cwLogInfo("In device setup failed.");
      else
        // start the input device
        if( deviceStart(r.inDevIdx) != kOkRC )
          cwLogInfo("In device start failed.");
        else
          // start the output device
          if( deviceStart(r.outDevIdx) != kOkRC )
            cwLogInfo("Out Device start failed.");

    
    cwLogInfo("q=quit O/o output tone, I/i input tone P/p pass");
    char c;
    while((c=getchar()) != 'q')
    {
      //cmApAlsaDeviceRtReport(rpt,r.outDevIdx);

      switch(c)
      {
        case 'i':
        case 'I':
          buf::enableTone(r.inDevIdx,-1,buf::kInFl | (c=='I'?buf::kEnableFl:0));
          break;

        case 'o':
        case 'O':
          buf::enableTone(r.outDevIdx,-1,buf::kOutFl | (c=='O'?buf::kEnableFl:0));
          break;

        case 'p':
        case 'P':
          buf::enablePass(r.outDevIdx,-1,buf::kOutFl | (c=='P'?buf::kEnableFl:0));
          break;
          
        case 's':
          buf::report(tbH);
          cwLogInfo("%s",textBuf::text(tbH));
          textBuf::clear(tbH);
          break;
      }

    }

    // stop the input device
    if( deviceIsStarted(r.inDevIdx) )
      if( deviceStop(r.inDevIdx) != kOkRC )
        cwLogInfo("In device stop failed.");

    // stop the output device
    if( deviceIsStarted(r.outDevIdx) )
      if( deviceStop(r.outDevIdx) != kOkRC )
        cwLogInfo("Out device stop failed.");
  }

 errLabel:

  // release any resources held by the audio port interface
  if( finalize() != kOkRC )
    cwLogInfo("Finalize failed.");

  buf::finalize();

  //cmApNrtFree();
  //cmApFileFree();

  // report the count of audio buffer callbacks
  cwLogInfo("cb count:%i", r.cbCnt );
  //for(i=0; i<_logCnt; ++i)
  //  cwLogInfo("%c(%i)",_log[i],_ilog[i]);
  //cwLogInfo("\n");

  textBuf::destroy(tbH);

  return result;
}

/// [cmAudioPortExample]



