#ifndef cwIoMidiRecordPlay_h
#define cwIoMidiRecordPlay_h


namespace cw
{
  namespace midi_record_play
  {
    typedef handle< struct midi_record_play_str > handle_t;
    
    rc_t create( handle_t& hRef, io::handle_t ioH, const object_t& cfg );
    rc_t destroy( handle_t& hRef );
    
    rc_t start( handle_t h );
    rc_t stop( handle_t h );
    bool is_started( handle_t h );
    
    rc_t rewind( handle_t h );
    rc_t clear( handle_t h );
    rc_t set_record_state( handle_t h, bool record_fl );
    bool record_state( handle_t h );

    rc_t set_thru_state( handle_t h, bool record_thru );
    bool thru_state( handle_t h );

    rc_t save( handle_t h, const char* fn );
    rc_t open( handle_t h, const char* fn );
    unsigned event_count( handle_t h );
    unsigned event_index( handle_t h );
    rc_t exec( handle_t h, const io::msg_t& msg );


    rc_t am_to_midi_file( const char* am_filename, const char* midi_filename );
    rc_t am_to_midi_dir( const char* inDir );
    rc_t am_to_midi_file( const object_t* cfg );
    
  }
}


#endif
