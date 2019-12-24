#ifndef cwAudioPortAlsa_H
#define cwAudioPortAlsa_H

namespace cw
{
  namespace audio
  {
    namespace device
    {
      namespace alsa
      {
        //{ { label: cmApAlsa kw:[ audio, device, rt ] }
        //
        //( 
        //  ALSA audio device API
        //
        //  This API is used by the cmAudioPort interface when 
        //  the library is compiled for a Linux platform.
        //
        //)

        //[

        rc_t                initialize( unsigned baseApDevIdx );
        rc_t                finalize();
        
        rc_t                deviceCount();
        const char*         deviceLabel(          unsigned devIdx );
        unsigned            deviceChannelCount(   unsigned devIdx, bool inputFl );
        double              deviceSampleRate(     unsigned devIdx );
        unsigned            deviceFramesPerCycle( unsigned devIdx, bool inputFl );
        
        rc_t      deviceSetup(          
          unsigned          devIdx, 
          double            srate, 
          unsigned          framesPerCycle, 
          device::cbFunc_t callbackPtr,
          void*             userCbPtr );
        
        rc_t                deviceStart( unsigned devIdx );
        rc_t                deviceStop(  unsigned devIdx );
        bool                deviceIsStarted( unsigned devIdx );
        
        void                deviceReport(  );

        //]
        //}

      }
    }
  }
}
#endif
