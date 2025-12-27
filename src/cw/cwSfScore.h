//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwSfScore_h
#define cwSfScore_h

namespace cw
{
  namespace sfscore
  {
    
    struct loc_str;
    struct set_str;

    typedef score_parse::stats_t stats_t;

    // The score can be divided into arbitrary non-overlapping sections.
    typedef struct section_str
    {
      const char*      label;             // section label
      unsigned         index;             // index of this record in the internal section array
      struct loc_str*  measLocPtr;        // last location of last set to be applied to this section
      struct loc_str*  locPtr;            // location where this section starts
      unsigned         begEvtIndex;       // score element index where this section starts
      unsigned         endEvtIndex;       // last element in this section
      unsigned         setCnt;            // Count of elements in setArray[]
      struct set_str** setArray;          // Ptrs to sets which are applied to this section.
      //stats_t          statsA[ score_parse::kStatCnt ];
    } section_t;

    typedef struct var_str
    {
      unsigned        flags;
      unsigned        varId;
      struct set_str* set;
    } var_t;

    typedef struct event_str
    {
      unsigned     type;         // See score_parse ???TId
      double       secs;         // Time location in seconds 
      double       durSecs;      // Duration in seconds
      unsigned     index;        // Index of this event in the event array.
      unsigned     oLocId;       // Index of the onset location (oloc) containing this event
      midi::byte_t pitch;        // MIDI pictch of this note or the MIDI pedal id of pedal down/up msg (64=sustain 65=sostenuto 66=soft)
      midi::byte_t vel;          // MIDI velocity of this note
      unsigned     flags;        // Attribute flags for this event
      unsigned     dynLevel;     // Dynamcis value pppp to ffff (1 to 11) for this note.
      double       frac;         // Note's time value for tempo and non-grace evenness notes.
      section_t*   section;      // The section to which this event belongs
      unsigned     barNumb;      // Bar id of the measure containing this event.
      unsigned     barNoteIdx;   // Index of this note in this bar
      unsigned     csvRowNumb;   // File row number (not index) from which this record originated
      unsigned     line;         // Line number of this event in the score file.
      unsigned     parseEvtIdx;  // Index of event from score_parse event index.
      unsigned     hash;         // unique hash id for this note
      char*        sciPitch;     // Sci. pitch of this note

      var_t*       varA;         // varA[varN] set's this event belongs to
      unsigned     varN;         // Length of varA[]

      unsigned     bpm;           // beats per minute
      double       bpm_rval;      // 
      double       relTempo;      // relative tempo (1=min tempo)

      bool         perfFl;        // has this event been performed
      unsigned     perfCnt;       // count of time this event was performed (if perfCnt > 1 then the event was duplicated during performance)
      double       perfSec;       // performance event time
      uint8_t      perfVel;       // performance event velocity
      unsigned     perfDynLevel;  // performance dynamic level
      double       perfMatchCost; // performance match cost (or DBL_MAX if not valid)
      
      
    } event_t;

    // A 'set' is a collection of events that are grouped in time and all marked with a given attribute.
    // (e.g. eveness, tempo, dynamcs ... )
    typedef struct set_str
    {
      unsigned        id;           // Unique id for this set
      unsigned        varId;        // See score_parse::k???VarIdx 
      event_t**       evtArray;     // Events that make up this set in time order
      unsigned        evtCnt;       //
      unsigned        locN;         // count of locations coverted by this set
      section_t**     sectArray;    // Sections this set will be applied to
      unsigned        sectCnt;      // 
      struct set_str* llink;        // loc_t setList link

      unsigned        perfEventCnt;  // count of events in this set that have been performed (never greater than eleCnt)
      unsigned        perfUpdateCnt; // count of event updates this has received (incr'd every time a event is performed - may be greater than eleCnt)
    } set_t;
    
    // All events which are simultaneous are collected into a single
    // locc_t record.
    typedef struct loc_str
    {
      unsigned       index;      // oloc id and index of this location record 
      double         secs;       // Time of this location
      unsigned       evtCnt;     // Count of events in evtArray[].
      event_t**      evtArray;   // Events which occur at this time.
      unsigned       barNumb;    // Bar number this event is contained by.                            
      set_t*         setList;    // Set's which end on this time location (linked through set_t.llink)
      section_t*     begSectPtr; // NULL if this location does not start a section
    } loc_t;

    typedef dyn_ref_tbl::dyn_ref_t dyn_ref_t;
    typedef handle<struct sfscore_str> handle_t;

    // Create the score from a provided score parser
    rc_t create( handle_t& h,
                 score_parse::handle_t spH,
                 bool show_warnings_fl = false);

    // Create an internal score parser.
    rc_t create( handle_t&             h,
                 const char*           fname,
                 double                srate,
                 dyn_ref_tbl::handle_t dynRefH,
                 bool                  show_warnings_fl = false);

    
    rc_t destroy( handle_t& h );


    void clear_all_performance_data( handle_t h );
    rc_t set_perf( handle_t h, unsigned event_idx, double secs, uint8_t pitch, uint8_t vel, double cost );

    // Return true if all events that are assigned to a set performed at the given location.
    bool are_all_loc_set_events_performed( handle_t h, unsigned locId );
    
    
    double sample_rate( handle_t& h );
    
    unsigned         event_count( handle_t h );
    const event_t*   event( handle_t h, unsigned idx );
    
    const event_t*   hash_to_event( handle_t h, unsigned hash );
    
    // Return the first event in the bar.
    const event_t*   bar_to_event( handle_t h, unsigned barNumb );

    unsigned         loc_count( handle_t h );
    const loc_t*     loc_base( handle_t h );

    unsigned         set_count( handle_t h );
    const set_t*     set_base( handle_t h );

    unsigned         section_count( handle_t h );
    const section_t* section_base( handle_t h );

    const section_t* event_index_to_section( handle_t h, unsigned event_idx );

    
    void report( handle_t h, const char* out_fname=nullptr );

    // see score_test::test() for testing this object

  }
}



#endif
