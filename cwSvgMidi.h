#ifndef cwSvgMidi_h
#define cwSvgMidi_h

namespace cw
{
  namespace svg_midi
  {
    rc_t write( const char* fname, midi_state::handle_t msH );
    
    rc_t midi_to_svg_file(        const char* midi_fname,  const char* out_fname, const object_t* midi_state_args );
    //rc_t piano_score_to_svg_file( const char* piano_score_fname, const char* out_fname, const object_t* cfg );

    rc_t test_midi_file( const object_t* cfg );
    
  }
}

#endif
