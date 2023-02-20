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
        rc_t        start( handle_t h );
        rc_t        stop( handle_t h );
        
        unsigned    deviceCount(          struct driver_str* drv);
        const char* deviceLabel(          struct driver_str* drv, unsigned devIdx );
        unsigned    deviceChannelCount(   struct driver_str* drv, unsigned devIdx, bool inputFl );
        double      deviceSampleRate(     struct driver_str* drv, unsigned devIdx );
        unsigned    deviceFramesPerCycle( struct driver_str* drv, unsigned devIdx, bool inputFl );
        rc_t        deviceSetup(          struct driver_str* drv, unsigned devIdx, double sr, unsigned frmPerCycle, cbFunc_t cbFunc, void* cbArg, unsigned cbDevIdx );
        rc_t        deviceStart(          struct driver_str* drv, unsigned devIdx );
        rc_t        deviceStop(           struct driver_str* drv, unsigned devIdx );
        bool        deviceIsStarted(      struct driver_str* drv, unsigned devIdx );
        rc_t        deviceExecute(        struct driver_str* drv, unsigned devIdx );
        rc_t        deviceEnable(         struct driver_str* drv, unsigned devIdx, bool inputFl, bool enableFl );
        rc_t        deviceSeek(           struct driver_str* drv, unsigned devIdx, bool inputFl, unsigned frameOffset );
        void        deviceRealTimeReport( struct driver_str* drv, unsigned devIdx );

        enum {
          kRewindOnStartFl = 0x01,
          kCacheFl         = 0x02
        };

        // A device may have an input, an output or both.
        // A device with both an input and output can be created by assigning both to the same label
        rc_t        createInDevice(  handle_t& h, const char* label, const char* audioInFName,  unsigned flags );
        
        // Set bitsPerSample to 0 to write in single prec. float.
        rc_t        createOutDevice( handle_t& h, const char* label, const char* audioOutFName, unsigned flags, unsigned chCnt, unsigned bitsPerSample );

        // Generate an audio callback on the specified device.
        rc_t        deviceExec( handle_t& h, unsigned devIdx );
        
        rc_t        report(handle_t h );
        rc_t        report();
        rc_t        test( const object_t* cfg );
      }
    }
  }
  
}

#endif
