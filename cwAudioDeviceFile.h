#ifndef cwAudioDeviceFile_H
#define cwAudioDeviceFile_H


namespace cw
{
  namespace audio
  {
    namespace device
    {
      namespace file
      {
        typedef handle<struct dev_mgr_str> handle_t;
        
        rc_t        create( handle_t& hRef, struct driver_str*& drvRef );
        rc_t        destroy( handle_t& hRef );
        
        unsigned    deviceCount(          struct driver_str* drv);
        const char* deviceLabel(          struct driver_str* drv, unsigned devIdx );
        unsigned    deviceChannelCount(   struct driver_str* drv, unsigned devIdx, bool inputFl );
        double      deviceSampleRate(     struct driver_str* drv, unsigned devIdx );
        unsigned    deviceFramesPerCycle( struct driver_str* drv, unsigned devIdx, bool inputFl );
        rc_t        deviceSetup(          struct driver_str* drv, unsigned devIdx, double sr, unsigned frmPerCycle, cbFunc_t cbFunc, void* cbArg );
        rc_t        deviceStart(          struct driver_str* drv, unsigned devIdx );
        rc_t        deviceStop(           struct driver_str* drv, unsigned devIdx );
        bool        deviceIsStarted(      struct driver_str* drv, unsigned devIdx );
        void        deviceRealTimeReport( struct driver_str* drv, unsigned devIdx );

        enum {
          kRewindOnStartFl = 0x01,
        };
        
        rc_t        createInDevice(  handle_t& h, const char* label, const char* audioInFile,  unsigned flags );
        rc_t        createOutDevice( handle_t& h, const char* label, const char* audioOutFile, unsigned flags, unsigned chCnt, unsigned bitsPerSample );

        // Generate an audio callback on the specified device.
        rc_t        deviceExec( handle_t& h, unsigned devIdx );
        
        rc_t        report(handle_t h );
        rc_t        report();
      }
    }
  }
  
}

#endif
