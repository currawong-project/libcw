//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwTextBuf.h"
#include "cwAudioDevice.h"
#include "cwAudioBuf.h"
#include "cwAudioDeviceAlsa.h"
#include "cwObject.h"
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
        const char*   inDevLabel;     // Input audio device label
        const char*   outDevLabel;    // Output audio device label
        unsigned      bufCnt;         // 2=double buffering 3=triple buffering
        unsigned      framesPerCycle; // DSP frames per cycle
        double        srate;          // audio sample rate
        unsigned      meterMs;        // audio meter buffer length
        
        unsigned      inDevIdx;       // input device index
        unsigned      outDevIdx;      // output device index
        
        unsigned      iCbCnt;         // count the callback
        unsigned      oCbCnt;

        double        amHz;      // ampl. modulation frequency
        double        amPhs;     //                  phase
        double        amMaxGain; //                  max gain.

        buf::handle_t audioBufH;
      } cmApPortTestRecd;


      rc_t _cmApGetCfg( cmApPortTestRecd* r, const object_t* cfg )
      {
        rc_t rc;

        r->bufCnt         = 3;
        r->srate          = 48000;
        r->framesPerCycle = 512;
        r->meterMs        = 50;
        r->amHz           = 0;
        r->amMaxGain      = 0.8;
        
        if((rc = cfg->getv_opt("inDev",r->inDevLabel,"outDev",r->outDevLabel,"srate",r->srate,"bufN",r->bufCnt,"framesPerCycle",r->framesPerCycle,"meterMs",r->meterMs,"amHz",r->amHz,"amMaxGain",r->amMaxGain)) != kOkRC )
          return cwLogError(rc,"The audio device configuration is invalid.");

        return rc;
      }

      

      void _cmApPortCb2( void* arg, audioPacket_t* inPktArray, unsigned inPktCnt, audioPacket_t* outPktArray, unsigned outPktCnt )
      {
        cmApPortTestRecd* p = reinterpret_cast<cmApPortTestRecd*>(arg);
        
        for(unsigned i=0; i<inPktCnt; ++i)
          reinterpret_cast<cmApPortTestRecd*>(inPktArray[i].cbArg)->iCbCnt++;

        for(unsigned i=0; i<outPktCnt; ++i)
          reinterpret_cast<cmApPortTestRecd*>(outPktArray[i].cbArg)->oCbCnt++;

        if( p->amHz > 0 && outPktCnt > 0 )
        {
          unsigned sampleFrameN = outPktArray[0].audioFramesCnt;
          
          double amGain = p->amMaxGain * (cos( p->amPhs ) + 1.0) / 2;
          p->amPhs += p->amHz * sampleFrameN *  M_PI / p->srate;
          buf::setGain( p->audioBufH, p->outDevIdx, -1, buf::kOutFl, amGain);
        }
        
        buf::inputToOutput( p->audioBufH, p->inDevIdx, p->outDevIdx );

        buf::update( p->audioBufH, inPktArray, inPktCnt, outPktArray, outPktCnt );
      }
    }
  }
}

// Audio Port testing function
cw::rc_t cw::audio::device::test( const object_t* cfg )
{
  cmApPortTestRecd  r;
  unsigned          i;
  rc_t              rc;
  driver_t*         drv   = nullptr;
  handle_t          h;
  alsa::handle_t    alsaH;
  bool              runFl = true;

  r.oCbCnt = 0;
  r.iCbCnt = 0;
  r.amPhs  = 0;
  
  
  // initialize the audio device interface  
  if((rc = create(h)) != kOkRC )
  {
    cwLogError(rc,"Initialize failed.");
    goto errLabel;
  }

  // initialize the ALSA device driver interface
  if((rc = alsa::create(alsaH, drv )) != kOkRC )
  {
    cwLogError(rc,"ALSA initialize failed.");
    goto errLabel;
  }

  // register the ALSA device driver with the audio interface
  if((rc = registerDriver( h, drv )) != kOkRC )
  {
    cwLogError(rc,"ALSA driver registration failed.");
    goto errLabel;
  }

  // report the current audio device configuration
  for(i=0; i<device::count(h); ++i)
  {
    cwLogInfo("%i [in: chs=%i frames=%i] [out: chs=%i frames=%i] srate:%8.1f %s",i,device::channelCount(h,i,true),framesPerCycle(h,i,true),channelCount(h,i,false),framesPerCycle(h,i,false),sampleRate(h,i),label(h,i));
  }

  if( cfg == nullptr )
    goto errLabel;
  
  if((rc = _cmApGetCfg(&r, cfg )) != kOkRC )
    goto errLabel;

  // get the input device index
  if((r.inDevIdx = labelToIndex(h,r.inDevLabel)) == kInvalidIdx )
  {
    rc = cwLogError(kInvalidIdRC,"The input audio device '%s' could not be found.", r.inDevLabel );
    goto errLabel;
  }

  // get the output device index
  if((r.outDevIdx = labelToIndex(h,r.outDevLabel)) == kInvalidIdx )
  {
    rc = cwLogError(kInvalidIdRC,"The output audio device '%s' could not be found.", r.outDevLabel );
    goto errLabel;
  }

  cwLogInfo("In:%i %s Out:%i %s iCh:%i oCh:%i sr:%f bufN:%i FpC:%i meterMs:%i",r.inDevIdx,r.inDevLabel,r.outDevIdx,r.outDevLabel,channelCount(h,r.inDevIdx,true),channelCount(h,r.outDevIdx,false),r.srate,r.bufCnt,r.framesPerCycle,r.meterMs);
  
  // report the current audio devices using the audio port interface function
  //report(h);

  if( runFl )
  {
    // initialize the audio bufer
    buf::create( r.audioBufH, device::count(h), r.meterMs );

    // setup the buffer for the output device
    buf::setup( r.audioBufH, r.outDevIdx, r.srate, r.framesPerCycle, r.bufCnt, channelCount(h,r.outDevIdx,true), r.framesPerCycle, channelCount(h,r.outDevIdx,false), r.framesPerCycle );

    // setup the buffer for the input device
    if( r.inDevIdx != r.outDevIdx )
      buf::setup( r.audioBufH, r.inDevIdx, r.srate, r.framesPerCycle, r.bufCnt, channelCount(h,r.inDevIdx,true), r.framesPerCycle, channelCount(h,r.inDevIdx,false), r.framesPerCycle ); 


    buf::report( r.audioBufH );
    
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
          if( r.outDevIdx != r.inDevIdx )            
            if( start(h, r.outDevIdx) != kOkRC )
              cwLogInfo("Out Device start failed.");

    
    
    
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


cw::rc_t cw::audio::device::test_tone( const object_t* cfg )
{
  rc_t rc = kOkRC;
  return rc;
}

cw::rc_t cw::audio::device::report()
{
  return test(nullptr);
}



