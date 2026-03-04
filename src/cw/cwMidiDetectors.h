namespace cw {

  namespace midi_detect {

    typedef struct state_str
    {
      unsigned order;     // this field is ignored by the piano detecor
      unsigned ch;
      unsigned status;
      unsigned d0;
      bool     release_fl; // key or pedal is off/released (this field is ignored by sequence detector)
    } state_t;

    /*
      The piano detector maintains two 16x128 matrices, one hold the last received note velocity (0=note-off)
      and the second holds the last control value.
      A detector is considered triggered if any of the states associated with the detector are matched by
      the state held by the matrices.
      (i.e. keys/pedals in the state array match the current up/down state contained in the matrices)
     */
    
    namespace piano {

      typedef handle<struct piano_det_str> handle_t;

      // If the pedal is less than pedal_thresh then the pedal is considered released (up).
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

    /*
      The sequence detector consists of a two internal detectors.
      The sequence detector looks for a time order sequence of 'note-on' messages.
      The piano detector looks at all arriving MIDI message, not just note-on.
      The operation of the piano detector is outlined above.
      
      The sequence detector triggers when all note-on messages have been recieved in the specified order.      
      If a note-on with a unrecognized pitch is received or the pitch is out of order the matcher is reset and waits
      for the first note to arrive again.
      To specify that a note may arrive out of order (e.g. chord notes) set the 'order' field value
      to kInvalidId.  When these notes arrive they will be recognized and will not cause the matcher
      to be reset.
      The 'order' of the first note to match must be 0.
      The order of all subsequent notes must be sequential or kInvalidId.      
     */

    namespace seq {

      typedef handle<struct seq_det_str> handle_t;
      
      rc_t create( handle_t& hRef, unsigned allocDetN, unsigned pedal_thresh = 30 );
      rc_t destroy( handle_t& hRef );

      // pdetStateA[] is optional if the detector should trigger on the sequence match alone.
      rc_t setup_detector( handle_t h,
                           const state_t* stateA,     unsigned stateN,
                           const state_t* pdetStateA, unsigned pdetStateN,
                           unsigned& det_id_ref );

      // Clear the currently armed detector and enter the disarmed state.
      rc_t reset( handle_t h);

      // Update the state of the detector with incomin MIDI.
      rc_t on_midi( handle_t h, const midi::ch_msg_t* msgA, unsigned msgN );

      // The detector must be armed before it can be triggered.
      rc_t arm_detector( handle_t h, unsigned det_id );

      // Is the piano and sequence detectors of the armed detector triggered?
      bool is_detector_triggered( handle_t h );

    };

    
    rc_t test( const object_t* cfg );


  }
}
