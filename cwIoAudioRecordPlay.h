//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwIoAudioRecordPlay_h
#define cwIoAudioRecordPlay_h

namespace cw
{
  namespace audio_record_play
  {
    typedef handle< struct audio_record_play_str > handle_t;
    
    rc_t create( handle_t& hRef, io::handle_t ioH, const object_t& cfg );
    rc_t destroy( handle_t& hRef );
    
    rc_t start( handle_t h );
    rc_t stop( handle_t h );
    bool is_started( handle_t h );
    rc_t rewind( handle_t h );
    rc_t clear( handle_t h );
    rc_t set_record_state( handle_t h, bool record_fl );
    bool record_state( handle_t h );
    rc_t set_mute_state( handle_t h, bool mute_fl );
    bool mute_state( handle_t h );
    rc_t save( handle_t h, const char* fn );
    rc_t open( handle_t h, const char* fn );
    double duration_seconds( handle_t h );
    double current_loc_seconds( handle_t h );
    rc_t exec( handle_t h, const io::msg_t& msg );
    
  }
}


#endif
