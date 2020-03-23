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
        unsigned      framesPerCycle; // DSP frames per cycle
        unsigned      inDevIdx;       // input device index
        unsigned      outDevIdx;      // output device index
        double        srate;          // audio sample rate
        unsigned      meterMs;        // audio meter buffer length
        
        unsigned      iCbCnt;         // count the callback
        unsigned      oCbCnt;

        buf::handle_t audioBufH;
      } cmApPortTestRecd;



      // print the usage message for cmAudioPortTest.c
      void _cmApPrintUsage()
      {
        char msg[] =
          "cmApPortTest() command switches\n"
          "-r <srate> -c <chcnt> -b <bufcnt> -f <frmcnt> -i <idevidx> -o <odevidx> -t -p -h \n"
          "\n"
          "-r <srate> = sample rate\n"
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

      void _cmApPortCb2( void* arg, audioPacket_t* inPktArray, unsigned inPktCnt, audioPacket_t* outPktArray, unsigned outPktCnt )
      {
        cmApPortTestRecd* p = static_cast<cmApPortTestRecd*>(arg);
        
        for(unsigned i=0; i<inPktCnt; ++i)
          static_cast<cmApPortTestRecd*>(inPktArray[i].cbArg)->iCbCnt++;

        for(unsigned i=0; i<outPktCnt; ++i)
          static_cast<cmApPortTestRecd*>(outPktArray[i].cbArg)->oCbCnt++;
        
        buf::inputToOutput( p->audioBufH, _cmGlobalInDevIdx, _cmGlobalOutDevIdx );

        buf::update( p->audioBufH, inPktArray, inPktCnt, outPktArray, outPktCnt );
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

  runFl            = _cmApGetOpt(argc,argv,"-p",0,true) ? false : true;
  r.srate          = _cmApGetOpt(argc,argv,"-r",44100);
  r.bufCnt         = _cmApGetOpt(argc,argv,"-b",3);
  r.framesPerCycle = _cmApGetOpt(argc,argv,"-f",512);
  r.meterMs        = 50;

  r.inDevIdx   = _cmGlobalInDevIdx  = _cmApGetOpt(argc,argv,"-i",0);   
  r.outDevIdx  = _cmGlobalOutDevIdx = _cmApGetOpt(argc,argv,"-o",0); 
  r.iCbCnt     = 0;
  r.oCbCnt     = 0;
  
  //cwLogInfo("Program cfg: %s in:%i out:%i chidx:%i chs:%i bufs=%i frm=%i rate=%f",runFl?"exec":"rpt",r.inDevIdx,r.outDevIdx,r.chIdx,r.chCnt,r.bufCnt,r.framesPerCycle,r.srate);

  
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
  for(i=0; i<device::count(h); ++i)
  {
    cwLogInfo("%i [in: chs=%i frames=%i] [out: chs=%i frames=%i] srate:%8.1f %s",i,device::channelCount(h,i,true),framesPerCycle(h,i,true),channelCount(h,i,false),framesPerCycle(h,i,false),sampleRate(h,i),label(h,i));
  }
  
  // report the current audio devices using the audio port interface function
  //report(h);

  if( runFl )
  {
    // initialize the audio bufer
    buf::create( r.audioBufH, device::count(h), r.meterMs );

    // setup the buffer for the output device
    buf::setup( r.audioBufH, r.outDevIdx, r.srate, r.framesPerCycle, r.bufCnt, channelCount(h,r.outDevIdx,true), r.framesPerCycle, channelCount(h,r.outDevIdx,false), r.framesPerCycle );

    // setup the buffer for the input device
    //if( r.inDevIdx != r.outDevIdx )
    buf::setup( r.audioBufH, r.inDevIdx, r.srate, r.framesPerCycle, r.bufCnt, channelCount(h,r.inDevIdx,true), r.framesPerCycle, channelCount(h,r.inDevIdx,false), r.framesPerCycle ); 

    // setup an output device
    if(setup(h, r.outDevIdx,r.srate,r.framesPerCycle,_cmApPortCb2,&r) != kOkRC )
      cwLogInfo("Out device setup failed.");
    else
      // setup an input device
      if( setup(h, r.inDevIdx,r.srate,r.framesPerCycle,_cmApPortCb2,&r) != kOkRC )
        cwLogInfo("In device setup failed.");
      else
        // start the input device
        if( start(h, r.inDevIdx) != kOkRC )
          cwLogInfo("In device start failed.");
        else
          // start the output device
          if( start(h, r.outDevIdx) != kOkRC )
            cwLogInfo("Out Device start failed.");
          else
            cwLogInfo("Setup complete!");
    
    
    cwLogInfo("q=quit O/o output tone, I/i input tone, P/p pass M/m meter s=buf report");

    // turn on the meters
    buf::enableMeter(r.audioBufH, r.outDevIdx,-1,buf::kOutFl | buf::kEnableFl);
    
    char c;
    while((c=getchar()) != 'q')
    {
      realTimeReport(h, r.outDevIdx );

      switch(c)
      {
        case 'i':
        case 'I':
          buf::enableTone(r.audioBufH, r.inDevIdx,-1,buf::kInFl | (c=='I'?buf::kEnableFl:0));
          break;

        case 'o':
        case 'O':
          buf::enableTone(r.audioBufH, r.outDevIdx,-1,buf::kOutFl | (c=='O'?buf::kEnableFl:0));
          break;

        case 'p':
        case 'P':
          buf::enablePass(r.audioBufH, r.outDevIdx,-1,buf::kOutFl | (c=='P'?buf::kEnableFl:0));
          break;

        case 'M':
        case 'm':
          buf::enableMeter( r.audioBufH, r.inDevIdx,  -1,  buf::kInFl | (c=='M'?buf::kEnableFl:0));
          buf::enableMeter( r.audioBufH, r.outDevIdx, -1, buf::kOutFl | (c=='M'?buf::kEnableFl:0));
          break;
          
        case 's':
          buf::report(r.audioBufH);
          break;
      }

    }

    // stop the input device
    if( isStarted(h,r.inDevIdx) )
      if( stop(h,r.inDevIdx) != kOkRC )
        cwLogInfo("In device stop failed.");

    // stop the output device
    if( isStarted(h,r.outDevIdx) )
      if( stop(h,r.outDevIdx) != kOkRC )
        cwLogInfo("Out device stop failed.");
  }

 errLabel:

  // release the ALSA driver
  rc_t rc0 = alsa::destroy(alsaH);
  
  // release any resources held by the audio port interface
  rc_t rc1 = destroy(h);
  
  rc_t rc2 = buf::destroy(r.audioBufH);

  //cmApNrtFree();
  //cmApFileFree();

  // report the count of audio buffer callbacks
  cwLogInfo("cb count: i:%i o:%i", r.iCbCnt, r.oCbCnt );

  return rcSelect(rc,rc0,rc1,rc2);
}

/// [cmAudioPortExample]

cw::rc_t cw::audio::device::report()
{
  const char* argv[] = { "-p" };
  return test(0,argv);   
}



