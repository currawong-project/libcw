#ifndef cwIoAudioMeterUi_H
#define cwIoAudioMeterUi_H

namespace cw
{
  namespace io
  {
    namespace audio_panel
    {
      typedef handle<struct audio_panel_str> handle_t;

      rc_t create(  handle_t& hRef, io::handle_t ioH, unsigned baseAppId );
      rc_t destroy( handle_t& hRef );

      rc_t registerDevice( handle_t h, unsigned baseAppId, unsigned devIdx, float dfltGain );

      rc_t exec( handle_t h, const msg_t& msg );

      unsigned maxAppId( handle_t h );

    }    
  }
}


#endif
