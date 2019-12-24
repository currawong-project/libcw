//( { file_desc: "Cross platform audio device interface." kw:[audio rt] }
//
// This interface provides data declarations for platform dependent 
// audio I/O functions. The implementation for the functions are
// in platform specific modules. See cmAudioPortOsx.c and cmAudioPortAlsa.c.
//
// ALSA Notes:  
// Assign capture device to line or mic input:
// amixer -c 0 cset iface=MIXER,name='Input Source',index=0 Mic
// amixer -c 0 cset iface=MIXER,name='Input Source',index=0 Line
//
// -c 0                            select the first card
// -iface=MIXER                    the cset is targetting the MIXER component
// -name='Input Source',index=0    the control to set is the first 'Input Source'
// Note that the 'Capture' control sets the input gain.
//
// See alsamixer for a GUI to accomplish the same thing.
//
//
//)

#ifndef cwAudioPort_H
#define cwAudioPort_H

namespace cw
{
  namespace audio
  {
    namespace device
    {
      typedef float sample_t;

      // audioPacket_t flags
      enum
      {
       kInterleavedApFl = 0x01,  // The audio samples are interleaved.
       kFloatApFl       = 0x02   // The audio samples are single precision floating point values.
      };

      // Audio packet record used by the audioPacket_t callback.
      // Audio ports send and receive audio using this data structure. 
      typedef struct
      {
        unsigned devIdx;         // device associated with packet
        unsigned begChIdx;       // first device channel 
        unsigned chCnt;          // count of channels
        unsigned audioFramesCnt; // samples per channel (see note below)
        unsigned bitsPerSample;  // bits per sample word
        unsigned flags;          // kInterleavedApFl | kFloatApFl
        void*    audioBytesPtr;  // pointer to sample data
        void*    userCbPtr;      // user defined argument passed in via deviceSetup()
        time::spec_t timeStamp;  // Packet time stamp.
      }  audioPacket_t; 


      // Audio port callback signature. 
      // inPktArray[inPktCnt] are full packets of audio coming from the ADC to the application.
      // outPktArray[outPktCnt] are empty packets of audio which will be filled by the application 
      // and then sent to the DAC.
      //
      // The value of audioFrameCnt  gives the number of samples per channel which are available
      // in the packet data buffer 'audioBytesPtr'.  The callback function may decrease this number in
      // output packets if the number of samples available is less than the size of the buffer.
      // It is the responsibility of the calling audio port to notice this change and pass the new,
      // decreased number of samples to the hardware.
      //
      // In general it should be assmed that this call is made from a system thread which is not 
      // the same as the application thread.
      // The usual thread safety precautions should therefore be taken if this function implementation
      // interacts with data structures also handled by the application. The audio buffer class (\see cmApBuf.h) 
      // is designed to provide a safe and efficient way to communicate between
      // the audio thread and the application.
      typedef void (*cbFunc_t)( audioPacket_t* inPktArray, unsigned inPktCnt, audioPacket_t* outPktArray, unsigned outPktCnt );
      
      rc_t        initialize();
      rc_t        finalize();
      unsigned    deviceCount();
      const char* deviceLabel( unsigned devIdx );
      unsigned    deviceLabelToIndex( const char* label );
      unsigned    deviceChannelCount( unsigned devIdx, bool inputFl );
      
      // Get the current sample rate of a device.  Note that if the device has both
      // input and output capability then the sample rate is the same for both.
      double      deviceSampleRate( unsigned devIdx );
      unsigned    deviceFramesPerCycle( unsigned devIdx, bool inputFl );

      // Configure a device.  
      // All devices must be setup before they are started.
      // framesPerCycle is the requested number of samples per audio callback. The
      // actual number of samples made from a callback may be smaller. See the note
      // regarding this in audioPacket_t.
      // If the device cannot support the requested configuration then the function
      // will return an error code.
      // If the device is started when this function is called then it will be 
      // automatically stopped and then restarted following the reconfiguration.
      // If the reconfiguration fails then the device may not be restared.      
      rc_t        deviceSetup( unsigned devIdx, double srate, unsigned framesPerCallback, cbFunc_t cbFunc, void* cbArg );

      // Start a device. Note that the callback may be made prior to this function returning.      
      rc_t        deviceStart( unsigned devIdx );
      rc_t        deviceStop( unsigned devIdx );
      bool        deviceIsStarted( unsigned devIdx );

      // Print a report of all the current audio device configurations.      
      void        report();

      // Test the audio port by synthesizing a sine signal or passing audio through
      // from the input to the output.  This is also a good example of how to 
      // use all of the functions in the interface.
      // Set runFl to false to print a report without starting any audio devices.
      // See cmAudiotPortTest.c for usage example for this function.
      rc_t        test( bool runFl, int argc, const char** argv );
      
    }
  }
  
}

#endif
