#ifndef cwPianoScore_h
#define cwPianoScore_h

namespace cw
{
  namespace score
  {
    typedef handle<struct score_str> handle_t;

    typedef struct event_str
    {
      unsigned uid;           // unique id for this event
      unsigned meas;          // measure number
      unsigned voice;         // score number
      unsigned loc;           // score location
      unsigned tick;          // event tick location
      double   sec;           // event absolute time in seconds
      double   rval;          // event rythmic value 2=1/2 1/4 .5=2 or 0
      char     sci_pitch[4];  // scientific pitch
      char     dmark[6];      // dynamic mark (e.g. "pp","mf","fff")
      unsigned dlevel;        // dynamic level as an integer associated with dyn. mark 
      unsigned status;        // MIDI status < type | channel > or 0
      unsigned d0;            // MIDI d0 or 0
      unsigned d1;            // MIDI d1 or 0
      unsigned bpm;           // tempo BPM or 0  
      char     grace_mark[4]; // grace mark or 0
      unsigned bar;           // bar number or 0
      unsigned section;       // section number or 0
      struct event_str* link; // list link
    } event_t;
        
    rc_t create(  handle_t& hRef, const char* fn );
    rc_t create(  handle_t& hRef, const object_t* cfg );
    rc_t destroy( handle_t& hRef );

    
    unsigned       event_count( handle_t h );
    const event_t* base_event( handle_t h );

    unsigned       loc_count( handle_t h );
    bool           is_loc_valid( handle_t h, unsigned locId );

    // Format the event as a string for printing.
    rc_t  event_to_string( handle_t h, unsigned uid, char* buf, unsigned buf_byte_cnt );
    
    rc_t test( const object_t* cfg );
    
  }
}


#endif
