//( { file_desc: "Cross platform audio device interface." kw:[audio rt] }
//
// This interface provides data declarations for platform dependent 
// audio I/O functions. The implementation for the functions are
// in platform specific modules. See cwAudioDeviceAlsa.cpp.
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

#ifndef cwAudioDevice_H
#define cwAudioDevice_H

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
        unsigned     devIdx;         // device associated with packet
        unsigned     begChIdx;       // first device channel 
        unsigned     chCnt;          // count of channels
        unsigned     audioFramesCnt; // samples per channel (see note below)
        unsigned     bitsPerSample;  // bits per sample word
        unsigned     flags;          // kInterleavedApFl | kFloatApFl
        void*        audioBytesPtr;  // pointer to sample data
        void*        cbArg;          // user defined argument passed in via deviceSetup()
        time::spec_t timeStamp;      // Packet time stamp.
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
      // interacts with data structures also handled by the application. The audio buffer class (\see cwAudioBuf.h) 
      // is designed to provide a safe and efficient way to communicate between
      // the audio thread and the application.
      typedef void (*cbFunc_t)( audioPacket_t* inPktArray, unsigned inPktCnt, audioPacket_t* outPktArray, unsigned outPktCnt );
      
      typedef struct driver_str
      {
        void*       drvArg;
        rc_t        (*deviceCount)(          struct driver_str* drvArg);
        const char* (*deviceLabel)(          struct driver_str* drvArg, unsigned devIdx );
        unsigned    (*deviceChannelCount)(   struct driver_str* drvArg, unsigned devIdx, bool inputFl );
        double      (*deviceSampleRate)(     struct driver_str* drvArg, unsigned devIdx );
        unsigned    (*deviceFramesPerCycle)( struct driver_str* drvArg, unsigned devIdx, bool inputFl );
        rc_t        (*deviceSetup)(          struct driver_str* drvArg, unsigned devIdx, double sr, unsigned frmPerCycle, cbFunc_t cb, void* cbData );
        rc_t        (*deviceStart)(          struct driver_str* drvArg, unsigned devIdx );
        rc_t        (*deviceStop)(           struct driver_str* drvArg, unsigned devIdx );
        bool        (*deviceIsStarted)(      struct driver_str* drvArg, unsigned devIdx );          
        void        (*deviceRealTimeReport)( struct driver_str* drvArg, unsigned devIdx );          
      } driver_t;          
        
      typedef handle<struct device_str> handle_t;
      
      rc_t create(  handle_t& hRef );
      rc_t destroy( handle_t& hRef );
        
      rc_t registerDriver( handle_t h, driver_t* drv );

      unsigned    deviceCount(          handle_t h );
      unsigned    deviceLabelToIndex(   handle_t h, const char* label );
      const char* deviceLabel(          handle_t h, unsigned devIdx );
      unsigned    deviceChannelCount(   handle_t h, unsigned devIdx, bool inputFl );
      double      deviceSampleRate(     handle_t h, unsigned devIdx );
      unsigned    deviceFramesPerCycle( handle_t h, unsigned devIdx, bool inputFl );
      
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
      rc_t        deviceSetup(
        handle_t h,
        unsigned devIdx,
        double   sr,
        unsigned frmPerCycle,
        cbFunc_t cb,
        void*    cbData );
      
      rc_t        deviceStart(          handle_t h, unsigned devIdx );
      rc_t        deviceStop(           handle_t h, unsigned devIdx );
      bool        deviceIsStarted(      handle_t h, unsigned devIdx );
      void        deviceRealTimeReport( handle_t h, unsigned devIdx );

      void report( handle_t h );      
    }
  }  
}
  


#endif
