//( {file_desc: "Thread safe audio buffer class." kw:[rt audio]}
//
// This file defines an audio buffer class which handles
// buffering incoming (recording) and outgoing (playback)
// samples in a thread-safe manner. 
//
// Usage example and testing code:
// See audio::device::test()
// \snippet cwAudioBuf.c cwAudioBufExample
//
// Notes on channel flags:
// Disabled channels:  kChFl is cleared
//   get()     
//      in  - return NULL buffer pointers  
//      out - return NULL buffer points
//
//   update()
//      in  - incoming samples are set to 0. 
//      out - outgoing samples are set to 0.
//
// Muted channels: kMuteFl is set 
//   update()
//      in  - incoming samples are set to 0. 
//      out - outgoing samples are set to 0.
//
// Tone channels: kToneFl is set 
//   update()
//      in  - incoming samples are filled with a 1k sine tone
//      out - outgoing samples are filled with a 1k sine tone
//)

#ifndef cmApBuf_H
#define cmApBuf_H

namespace cw
{
  namespace audio
  {
    namespace buf
    {
  
      //(

      typedef device::sample_t            sample_t;
      typedef handle<struct audioBuf_str> handle_t;
      

      // Allocate and initialize an audio buffer.
      // devCnt - count of devices this buffer will handle.
      // meterMs - length of the meter buffers in milliseconds (automatically limit to the range:10 to 1000)
      rc_t create( handle_t& hRef, unsigned devCnt, unsigned meterMs );

      // Deallocate and release any resource held by an audio buffer allocated via initialize().
      rc_t destroy( handle_t& hRef );

      // Configure a buffer for a given device.  
      rc_t setup(
        handle_t h,
        unsigned devIdx,              //< device to setup
        double   srate,               //< device sample rate (only required for synthesizing the correct test-tone frequency)
        unsigned dspFrameCnt,         // dspFrameCnt - count of samples in channel buffers returned via get() 
        unsigned cycleCnt,            //< number of audio port cycles to store 
        unsigned inChCnt,             //< input channel count on this device
        unsigned inFramesPerCycle,    //< maximum number of incoming sample frames on an audio port cycle
        unsigned outChCnt,            //< output channel count on this device
        unsigned outFramesPerCycle    //< maximum number of outgoing sample frames in an audio port cycle
                             );

      // Prime the buffer with 'audioCycleCnt' * outFramesPerCycle samples ready to be played
      rc_t primeOutput( handle_t h, unsigned devIdx, unsigned audioCycleCnt );

      // Notify the audio buffer that a device is being enabled or disabled.
      void onPortEnable( handle_t h, unsigned devIdx, bool enabelFl );

      // This function is called asynchronously by the audio device driver to transfer incoming samples to the
      // the buffer and to send outgoing samples to the DAC. This function is 
      // intended to be called from the audio port callback function (\see auido::device::cbFunc_t).
      // This function is thread-safe under the condition where the audio device uses
      // different threads for input and output.
      //
      // Enable Flag: 
      // Input: If an input channel is disabled then the incoming samples are replaced with zeros.
      // Output: If an output channel is disabled then the packet samples are set to zeros.
      //
      // Tone Flag:
      // Input: If the tone flag is set on an input channel then the incoming samples are set to a sine tone.
      // Output: If the tone flag is set on an output channel then the packet samples are set to a sine tone.
      //
      // The enable flag has higher precedence than the tone flag therefore disabled channels
      // will be set to zero even if the tone flag is set.
      rc_t update(
        handle_t               h, 
        device::audioPacket_t* inPktArray,  //< full audio packets from incoming audio (from ADC)
        unsigned               inPktCnt,    //< count of incoming audio packets
        device::audioPacket_t* outPktArray, //< empty audio packet for outgoing audio (to DAC)  
        unsigned               outPktCnt);  //< count of outgoing audio packets
                             
      // Channel flags
      enum
      {
       kInFl     = 0x01,  //< Identify an input channel
       kOutFl    = 0x02,  //< Identify an output channel
       kEnableFl = 0x04,  //< Set to enable a channel, Clear to disable. 

       kChFl     = 0x08,  //< Used to enable/disable a channel
       kMuteFl   = 0x10,  //< Mute this channel
       kToneFl   = 0x20,  //< Generate a tone on this channel
       kMeterFl  = 0x40,  //< Turn meter's on/off
       kPassFl   = 0x80   //< Pass input channels throught to the output. Must use getIO() to implement this functionality.
  
      };

      // Return the meter window period as set by initialize()
      unsigned meterMs(handle_t h);
  
      // Set the meter update period. THis function limits the value to between 10 and 1000.
      void     setMeterMs( handle_t h, unsigned meterMs );

      // Returns the channel count set via setup().
      unsigned channelCount( handle_t h, unsigned devIdx, unsigned flags );

      // Set chIdx to -1 to enable all channels on this device.
      // Set flags to {kInFl | kOutFl} | {kChFl | kToneFl | kMeterFl} | { kEnableFl=on | 0=off }  
      void setFlag( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );
  
      // Return true if the the flags is set.
      bool isFlag( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Set chIdx to -1 to enable all channels on this device.
      void  enableChannel(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Returns true if an input/output channel is enabled on the specified device.
      bool  isChannelEnabled(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Set the state of the tone generator on the specified channel.
      // Set chIdx to -1 to apply the change to all channels on this device.
      // Set flags to {kInFl | kOutFl} | { kEnableFl=on | 0=off }
      void  enableTone(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Returns true if an input/output tone is enabled on the specified device.
      bool  isToneEnabled(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Mute a specified channel.
      // Set chIdx to -1 to apply the change to all channels on this device.
      // Set flags to {kInFl | kOutFl} | { kEnableFl=on | 0=off }
      void  enableMute(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Returns true if an input/output channel is muted on the specified device.
      bool  isMuteEnabled(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Set the specified channel to pass through.
      // Set chIdx to -1 to apply the change to all channels on this device.
      // Set flags to {kInFl | kOutFl} | { kEnableFl=on | 0=off }
      void  enablePass(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Returns true if pass through is enabled on the specified channel.
      bool  isPassEnabled(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Turn meter data collection on and off.
      // Set chIdx to -1 to apply the change to all channels on this device.
      // Set flags to {kInFl | kOutFl} | { kEnableFl=on | 0=off }
      void  enableMeter(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Returns true if an input/output tone is enabled on the specified device.
      bool  isMeterEnabled(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Return the meter value for the requested channel.
      // Set flags to kInFl | kOutFl.
      sample_t meter(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags );

      // Set chIdx to -1 to apply the gain to all channels on the specified device.
      void setGain( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags, double gain );

      // Return the current gain seting for the specified channel.
      double gain( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags ); 

      // Get the meter and fault status of the channel input or output channel array of a device.
      // Set 'flags' to { kInFl | kOutFl }.
      // The returns value is the count of channels actually written to meterArray.
      // If 'faultCntPtr' is non-NULL then it is set to the faultCnt of the associated devices input or output buffer.
      unsigned getStatus( handle_t h, unsigned devIdx, unsigned flags, double* meterArray, unsigned meterCnt, unsigned* faultCntPtr );

      // Do all enabled input/output channels on this device have samples available?
      // 'flags' can be set to either or both kInFl and kOutFl
      bool  isDeviceReady( handle_t h, unsigned devIdx, unsigned flags ); 

      // This function is called by the application to get full incoming sample buffers and
      // to fill empty outgoing sample buffers.
      // Upon return each element in bufArray[bufChCnt] holds a pointer to a buffer assoicated 
      // with an audio channel or to NULL if the channel is disabled.
      // 'flags' can be set to kInFl or kOutFl but not both.
      // The buffers pointed to by bufArray[] each contain 'dspFrameCnt' samples. Where 
      // 'dspFrameCnt' was set in the earlier call to setup() for this device.
      // (see initialize()).
      // Note that this function just returns audio information it does not
      // change any internal states.
      void get( handle_t h, unsigned devIdx, unsigned flags, sample_t* bufArray[], unsigned bufChCnt );

      // This function replaces calls to get() and implements pass-through and output 
      // buffer zeroing: 
      // 
      // 1) get(in);
      // 2) get(out);
      // 3) Copy through channels marked for 'pass' and set the associated oBufArray[i] channel to NULL.
      // 4) Zero all other enabled output channels.
      //
      // Notes:
      // 1) The oBufArray[] channels that are disabled or marked for pass-through will 
      // be set to NULL.
      // 2) The client is required to use this function to implement pass-through internally.
      // 3) This function just returns audio information it does not
      // change any internal states.
      // 4) The timestamp pointers are optional.
      void getIO(   handle_t h, unsigned iDevIdx, sample_t* iBufArray[], unsigned iBufChCnt, time::spec_t* iTimeStampPtr, 
        unsigned oDevIdx, sample_t* oBufArray[], unsigned oBufChCnt, time::spec_t* oTimeStampPtr );


      // The application calls this function each time it completes processing of a bufArray[]
      // returned from get(). 'flags' can be set to either or both kInFl and kOutFl.
      // This function should only be called from the client thread.
      void advance( handle_t h, unsigned devIdx, unsigned flags );

      // Copy all available samples incoming samples from an input device to an output device.
      // The source code for this example is a good example of how an application should use get()
      // and advance().
      void inputToOutput( handle_t h, unsigned inDevIdx, unsigned outDevIdx );

      // Print the current buffer state.
      void report( handle_t h );

      // Run a buffer usage simulation to test the class. cmAudioPortTest.c calls this function.
      void test();

      //)

    }
  }
}

#endif
