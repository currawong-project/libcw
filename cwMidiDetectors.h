namespace cw {

  namespace midi_detect {

    typedef struct state_str
    {
      unsigned order;     // this field is ignored by the piano detecor
      unsigned ch;
      unsigned status;
      unsigned d0;
      bool     release_fl;
    } state_t;
    
    namespace piano {

      typedef handle<struct piano_det_str> handle_t;

      // If the pedal is less than pedal_thresh then the pedal is considered up.
      rc_t create( handle_t& hRef, unsigned allocDetN, unsigned pedal_thresh=30 );
      rc_t destroy( handle_t& hRef );

      // Setup a detector and get back a detector id.
      rc_t setup_detector( handle_t h, const state_t* stateA, unsigned stateN, unsigned& det_id_ref );
      
      // Sets the keys and pedals to the released state.
      rc_t reset( handle_t h );

      // Update the internal key and pedal state from the MIDI messages
      rc_t on_midi( handle_t h, const midi::ch_msg_t* msgA, unsigned msgN  );

      // Set match_fl_ref to true if any of the states in the detector match the correspond current key/pedal state.
      rc_t is_any_state_matched( handle_t h, unsigned det_id, bool& is_matched_fl_ref );      
      
    };

    namespace seq {

      typedef handle<struct seq_det_str> handle_t;
      
      rc_t create( handle_t& hRef, unsigned allocDetN, unsigned pedal_thresh = 30 );
      rc_t destroy( handle_t& hRef );

      rc_t setup_detector( handle_t h, const state_t* stateA, unsigned stateN, unsigned& det_id_ref );

      // clear the currently armed detector and enter the disarmed state
      rc_t reset( handle_t h);

      rc_t on_midi( handle_t h, const midi::ch_msg_t* msgA, unsigned msgN );

      rc_t arm_detector( handle_t h, unsigned det_id );

      bool is_detector_triggered( handle_t h );

      rc_t test( const object_t* cfg );

    };
  }
}
