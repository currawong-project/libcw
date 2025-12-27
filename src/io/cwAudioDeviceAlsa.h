//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwAudioDeviceAlsa_H
#define cwAudioDeviceAlsa_H


namespace cw
{
  namespace audio
  {
    namespace device
    {
      namespace alsa
      {
        typedef handle<struct alsa_str> handle_t;
        
        rc_t        create( handle_t& hRef, struct driver_str*& drvRef );
        rc_t        destroy( handle_t& hRef );
        
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
        
        rc_t        report(handle_t h );
        rc_t        report();
      }
    }
  }
  
}

#endif
