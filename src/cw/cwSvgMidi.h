//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwSvgMidi_h
#define cwSvgMidi_h

namespace cw
{
  namespace svg_midi
  {
    typedef handle<struct svg_midi_str> handle_t;

    rc_t create( handle_t& hRef );
    rc_t destroy( handle_t& hRef );

    rc_t setMidiMsg( handle_t h, double secs,                    unsigned uid, unsigned ch, unsigned status, unsigned d0,  unsigned d1, unsigned userValue );
    rc_t setMarker(  handle_t h, double secs,                    unsigned uid, unsigned ch, unsigned markId, unsigned markValue, unsigned userValue );
    rc_t setSpan(   handle_t h,  double beg_sec, double end_sec, unsigned uid, unsigned ch, unsigned markId, unsigned markValue, unsigned userValue );
    rc_t write( handle_t h, const char* fname );

    
    rc_t write( const char* fname, midi_state::handle_t msH );
    
    rc_t midi_to_svg_file(        const char* midi_fname,        const char* out_fname, const object_t* midi_state_args );
    rc_t piano_score_to_svg_file( const char* piano_score_fname, const char* out_fname, const object_t* midi_state_args );

    rc_t test_midi_file( const object_t* cfg );
    
  }
}

#endif
