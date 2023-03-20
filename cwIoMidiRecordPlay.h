#ifndef cwIoMidiRecordPlay_h
#define cwIoMidiRecordPlay_h


namespace cw
{
  namespace midi_record_play
  {
    typedef handle< struct midi_record_play_str > handle_t;

    typedef struct midi_msg_str
    {
      unsigned      id;
      time::spec_t  timestamp;
      unsigned      loc;
      uint8_t       ch;
      uint8_t       status;
      uint8_t       d0;
      uint8_t       d1;
    } midi_msg_t;

    enum {
      kMidiEventActionId,
      kPlayerStoppedActionId
    };

    typedef void (*event_callback_t)( void* arg, unsigned actionId, unsigned id, const time::spec_t timestamp, unsigned loc, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 );
    
    enum
    {
      kSampler_MRP_DevIdx = 0,
      kPiano_MRP_DevIdx   = 1
    };


    
    rc_t create( handle_t& hRef,
                 io::handle_t ioH,
                 const object_t& cfg,
                 const char* velTableFname=nullptr,
                 event_callback_t cb=nullptr,
                 void* cb_arg=nullptr );
    
    rc_t destroy( handle_t& hRef );

    // Set rewindFl to play from start, otherwise play from current output location.
    rc_t start( handle_t h, bool rewindFl=true, const time::spec_t* end_play_event_timestamp=nullptr );
    rc_t stop( handle_t h );
    bool is_started( handle_t h );
    
    rc_t rewind( handle_t h );
    rc_t clear( handle_t h );
    rc_t set_record_state( handle_t h, bool record_fl );
    bool record_state( handle_t h );

    rc_t set_mute_state( handle_t h, bool record_fl );
    bool mute_state( handle_t h );

    rc_t set_thru_state( handle_t h, bool record_thru );
    bool thru_state( handle_t h );

    rc_t save( handle_t h, const char* fn );
    rc_t save_csv( handle_t h, const char* fn );
    
    rc_t open( handle_t h, const char* fn );

    // Load the playback buffer with messages to output.
    rc_t load( handle_t h, const midi_msg_t* msg, unsigned msg_count );

    rc_t seek( handle_t h, time::spec_t timestamp );
    
    unsigned elapsed_micros( handle_t h );

    unsigned event_count( handle_t h ); // Current count of stored messages.
    unsigned event_index( handle_t h ); // record mode: index of next event to store play mode:index of next event to play
    unsigned event_loc( handle_t h );   // play mode: loc of next event to play record mode:kInvalidId
    
    rc_t exec( handle_t h, const io::msg_t& msg );

    unsigned device_count( handle_t h );
    bool is_device_enabled( handle_t h, unsigned devIdx );
    void enable_device( handle_t h, unsigned devIdx, bool enableFl );
    rc_t send_midi_msg( handle_t h, unsigned devIdx, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 );

    void half_pedal_params( handle_t h, unsigned noteDelayMs, unsigned pitch, unsigned vel, unsigned pedal_vel, unsigned noteDurMs, unsigned downDelayMs );

    // Convert an audio-midi file to a MIDI file
    rc_t am_to_midi_file( const char* am_filename, const char* midi_filename );
    rc_t am_to_midi_dir( const char* inDir );
    rc_t am_to_midi_file( const object_t* cfg );

    unsigned dev_count( handle_t h );
    
    unsigned       vel_table_count( handle_t h, unsigned devIdx );
    const uint8_t* vel_table( handle_t h, unsigned devIdx );
    rc_t           vel_table_set( handle_t h, unsigned devIdx, const uint8_t* tbl, unsigned tblN );
    
    void report( handle_t h );

    
    
  }
}


#endif
