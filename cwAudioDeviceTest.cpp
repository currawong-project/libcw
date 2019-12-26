#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwTextBuf.h"
#include "cwAudioDevice.h"
#include "cwAudioBuf.h"
#include "cwAudioDeviceAlsa.h"
#include "cwAudioDeviceTest.h"

namespace  cw
{
  namespace audio
  {
    namespace device
    {
      /// [cmAudioPortExample]

      // See test() below for the main point of entry.

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

      // Get a command line option. Note that if 'boolFl' is set to 'true' then the function simply
      // returns '1'.  This is used to handle arguments whose presense indicates a positive boolean
      // flag. For example -h (help) indicates that the usage data should be printed - it needs no other argument.
      int _cmApGetOpt( int argc, const char* argv[], const char* label, int defaultVal, bool boolFl=false )
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
        for(unsigned i=0; i<inPktCnt; ++i)
          static_cast<cmApPortTestRecd*>(inPktArray[i].cbArg)->cbCnt++;

        for(unsigned i=0; i<outPktCnt; ++i)
          static_cast<cmApPortTestRecd*>(outPktArray[i].cbArg)->cbCnt++;
        
        buf::inputToOutput( _cmGlobalInDevIdx, _cmGlobalOutDevIdx );

        buf::update( inPktArray, inPktCnt, outPktArray, outPktCnt );
      }
    }
  }
}

// Audio Port testing function
cw::rc_t cw::audio::device::test( int argc, const char** argv )
{
  cmApPortTestRecd  r;
  unsigned          i;
  rc_t              rc;
  driver_t*         drv   = nullptr;
  handle_t          h;
  alsa::handle_t    alsaH;
  bool              runFl = true;

  if( _cmApGetOpt(argc,argv,"-h",0,true) )
    _cmApPrintUsage();


  runFl            = _cmApGetOpt(argc,argv,"-p",0,true)?false:true;
  r.srate          = _cmApGetOpt(argc,argv,"-r",44100);
  r.chIdx          = _cmApGetOpt(argc,argv,"-a",0);
  r.chCnt          = _cmApGetOpt(argc,argv,"-c",2);
  r.bufCnt         = _cmApGetOpt(argc,argv,"-b",3);
  r.framesPerCycle = _cmApGetOpt(argc,argv,"-f",512);
  r.bufFrmCnt      = (r.bufCnt*r.framesPerCycle);
  r.bufSmpCnt      = (r.chCnt  * r.bufFrmCnt);
  r.logCnt         = 100; 
  r.meterMs        = 50;

  sample_t     buf[r.bufSmpCnt];
  char         log[r.logCnt];
  unsigned    ilog[r.logCnt];
  
  r.inDevIdx   = _cmGlobalInDevIdx  = _cmApGetOpt(argc,argv,"-i",0);   
  r.outDevIdx  = _cmGlobalOutDevIdx = _cmApGetOpt(argc,argv,"-o",2); 
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

  cwLogInfo("Program cfg: %s in:%i out:%i chidx:%i chs:%i bufs=%i frm=%i rate=%f",runFl?"exec":"rpt",r.inDevIdx,r.outDevIdx,r.chIdx,r.chCnt,r.bufCnt,r.framesPerCycle,r.srate);

  
  // initialize the audio device interface  
  if((rc = create(h)) != kOkRC )
  {
    cwLogInfo("Initialize failed.");
    goto errLabel;
  }

  // initialize the ALSA device driver interface
  if((rc = alsa::create(alsaH, drv )) != kOkRC )
  {
    cwLogInfo("ALSA initialize failed.");
    goto errLabel;
  }

  // register the ALSA device driver with the audio interface
  if((rc = registerDriver( h, drv )) != kOkRC )
  {
    cwLogInfo("ALSA driver registration failed.");
    goto errLabel;
  }
  
  // report the current audio device configuration
  for(i=0; i<deviceCount(h); ++i)
  {
    cwLogInfo("%i [in: chs=%i frames=%i] [out: chs=%i frames=%i] srate:%f %s",i,deviceChannelCount(h,i,true),deviceFramesPerCycle(h,i,true),deviceChannelCount(h,i,false),deviceFramesPerCycle(h,i,false),deviceSampleRate(h,i),deviceLabel(h,i));
  }
  
  // report the current audio devices using the audio port interface function
  report(h);

  if( runFl )
  {
    // initialize the audio bufer
    buf::initialize( deviceCount(h), r.meterMs );

    // setup the buffer for the output device
    buf::setup( r.outDevIdx, r.srate, r.framesPerCycle, r.bufCnt, deviceChannelCount(h,r.outDevIdx,true), r.framesPerCycle, deviceChannelCount(h,r.outDevIdx,false), r.framesPerCycle );

    // setup the buffer for the input device
    if( r.inDevIdx != r.outDevIdx )
      buf::setup( r.inDevIdx, r.srate, r.framesPerCycle, r.bufCnt, deviceChannelCount(h,r.inDevIdx,true), r.framesPerCycle, deviceChannelCount(h,r.inDevIdx,false), r.framesPerCycle ); 

    // setup an output device
    if(deviceSetup(h, r.outDevIdx,r.srate,r.framesPerCycle,_cmApPortCb2,&r) != kOkRC )
      cwLogInfo("Out device setup failed.");
    else
      // setup an input device
      if( deviceSetup(h, r.inDevIdx,r.srate,r.framesPerCycle,_cmApPortCb2,&r) != kOkRC )
        cwLogInfo("In device setup failed.");
      else
        // start the input device
        if( deviceStart(h, r.inDevIdx) != kOkRC )
          cwLogInfo("In device start failed.");
        else
          // start the output device
          if( deviceStart(h, r.outDevIdx) != kOkRC )
            cwLogInfo("Out Device start failed.");
          else
            cwLogInfo("Setup complete!");
    
    
    cwLogInfo("q=quit O/o output tone, I/i input tone P/p pass s=buf report");
    char c;
    while((c=getchar()) != 'q')
    {
      deviceRealTimeReport(h, r.outDevIdx );

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
          buf::report();
          break;
      }

    }

    // stop the input device
    if( deviceIsStarted(h,r.inDevIdx) )
      if( deviceStop(h,r.inDevIdx) != kOkRC )
        cwLogInfo("In device stop failed.");

    // stop the output device
    if( deviceIsStarted(h,r.outDevIdx) )
      if( deviceStop(h,r.outDevIdx) != kOkRC )
        cwLogInfo("Out device stop failed.");
  }

 errLabel:

  // release the ALSA driver
  rc_t rc0 = alsa::destroy(alsaH);
  
  // release any resources held by the audio port interface
  rc_t rc1 = destroy(h);
  
  rc_t rc2 = buf::finalize();

  //cmApNrtFree();
  //cmApFileFree();

  // report the count of audio buffer callbacks
  cwLogInfo("cb count:%i", r.cbCnt );

  return rcSelect(rc,rc0,rc1,rc2);
}

/// [cmAudioPortExample]


