#ifndef cwMidiState_h
#define cwMidiState_h

namespace cw
{
  namespace midi_state
  {
    // This object receives MIDI note and pedal (sustain,sostenuto,soft) messages
    // and generates voice and pedal control information.
    // Primarily it maintains the state of the voice associated with each
    // MIDI channel and pitch and notifies the client of the changes to
    // that state as various MIDI messages arrive.
    // The primary features are:
    //
    // 1. Maintains note gate and pedal gate information to determine
    // when a note is sounding. For example a note gate may be off
    // but the voice may still be sounding due to the state of the
    // sustain or sostenuto pedal.
    //
    // 2. Handles reporting 'half' pedal state on the sustain pedal.
    //
    // 3. Reports 're-attacks' when a note gate is on and receives
    // another note-on messages.
    //
    // 4. Report 'no-change' state when a MIDI message is received
    // but does not change the state of the voice or pedal.
    // These messages often indicate a problem with the MIDI stream
    // format.

    // TODO:
    // 1. automatically decrease note-on velocity when soft pedal is down
    //
    // 2. handle legato and portmento controls
    //
    // 3. send a 'decay rate' based on the state of the pedals
    //    when when the note gate goes off (i.e. if the note is being
    //    held decrease the decay rate.
    //
    // 4. automatically send note-off / snd-off to all voices that
    //    are sounding on 'reset()'.
    //
    // 5. Allow sending arbitrary time markes (e.g. bars and sections)
    //    though this mechanism so that they can be ordered with the
    //    MIDI events
    
    typedef handle<struct midi_state_str> handle_t;

    enum {
      kNoteOnFl        = 0x0001,  // note-on
      kNoteOffFl       = 0x0002,  // note-off
      kSoundOnFl       = 0x0004,  // note-on
      kSoundOffFl      = 0x0008,  // note-off,sustain up,sostenuto up
      kReattackFl      = 0x0010,  // note-on (when note gate is already on)
      kSoftPedalFl     = 0x0020,  // soft pedal up/down (also accompanies note-on msg's when the pedal is down)
      kUpPedalFl       = 0x0040,  // soft,sustain,sost pedal up
      kHalfPedalFl     = 0x0080,  // sustain pedal entered half pedal range (also sent to all sounding notes)
      kDownPedalFl     = 0x0100,  // soft,sustain,sost pedal down
      kNoChangeFl      = 0x0200,  // a midi-msg arrived but did not cause a change in note or pedal state
      kPedalEvtFl      = 0x0400,  // This is a pedal event
      kNoteEvtFl       = 0x0800,  // This is a note event
      kMarkerEvtFl     = 0x1000   // This is a marker event (bar or section)
    };

    typedef struct midi_msg_str
    {
      unsigned uid;     // midi event unique id
      uint8_t  ch;      // 0-15
      uint8_t  status;  // status w/o channel
      uint8_t  d0;      //
      uint8_t  d1;      // 
    } midi_msg_t;

    typedef struct marker_msg_str
    {
      unsigned uid;
      unsigned typeId; // marker type id
      unsigned value;  // marker value
      uint8_t  ch;
      uint8_t  pad[3];
    } marker_msg_t;

    enum {
      kMidiMsgTId,
      kMarkerMsgTId
    };
    
    typedef struct msg_str
    {
      unsigned typeId;
      union {
        midi_msg_t   midi;
        marker_msg_t marker;
      } u;
    } msg_t;

    // Cached event record 
    typedef struct event_str
    {
      unsigned            flags;  // see flags above
      double              secs;   // time of event
      const msg_t*        msg;    // accompanying midi info
      struct event_str*   link;   // null terminated list of following events
      struct event_str*  tlink;   // time order link
    } event_t;

    const char* flag_to_label( unsigned flag );


    // Returned string must be released via mem::release()
    unsigned    flags_to_string_max_string_length();
    rc_t        flags_to_string( unsigned flags, char* str, unsigned strCharCnt );

    // Note that if the cache is not enabled then 'msg' is only valid during the callback and
    // therefore should not be stored.  If the cache is enabled then 'msg' is valid until
    // the next call to 'reset()' or until the midi_state_t instance is destroyed.
    typedef void (*callback_t)( void* arg, unsigned flags, double secs,  const msg_t* msg );
    
    rc_t create( handle_t&  hRef,
                 callback_t cbFunc,                 // set to nullptr to disable callbacks
                 void*      cbArg,                  // callback arg
                 bool       cacheEnableFl,          // enable/disable event caching  
                 unsigned   cacheBlockMsgN,         // count of cache messages to pre-allocate in each block
                 unsigned   pedalHalfMinMidiValue,  // sustain half pedal lower value
                 unsigned   pedalHalfMaxMidialue,   // sustain pedal half pedal upper value       
                 unsigned   pedalUpMidiValue = 64 );// soft and sostenuto pedal down min value

    rc_t create( handle_t&       hRef,
                 callback_t      cbFunc,         // set to nullptr to disable callbacks
                 void*           cbArg,          // callback arg
                 bool            cacheEnableFl,  // 
                 const object_t* cfg );          //


    rc_t destroy( handle_t& hRef );

    rc_t setMidiMsg( handle_t h, double sec, unsigned uid, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 );
    rc_t setMarker(  handle_t h, double sec, unsigned uid, uint8_t ch, unsigned typeId, unsigned value );

    void reset( handle_t h );

    // Get the first link in time, then use event.tlink to traverse the
    // events in time.
    const event_t*  get_first_link( handle_t h );

    // Return cached events.
    // Note that the cached events are returned as a link list based on each channel note and pedal.
    // This list is ordered in time and gives the recorded state of the voice/pedal.
    const event_t* note_event_list(  handle_t h, uint8_t ch, uint8_t pitch );
    const event_t* pedal_event_list( handle_t h, uint8_t ch, unsigned pedal_index );

    // Returns kInvalidIdx if 'midi_ctl_id' is not a valid MIDi pedal control id.
    unsigned pedal_midi_ctl_id_to_index( unsigned midi_ctl_id );
    unsigned pedal_index_to_midi_ctl_id( unsigned pedal_idx );
    unsigned pedal_count( handle_t h );
    
    void get_note_extents(  handle_t h, uint8_t& minPitchRef, uint8_t& maxPitchRef, double& minSecRef, double& maxSecRef );
    void get_pedal_extents( handle_t h, double& minSecRef, double& maxSecRef );


    rc_t load_from_midi_file( handle_t h, const char* midi_fname );
    
    rc_t test( const object_t* cfg );
    
  }
}

#endif
