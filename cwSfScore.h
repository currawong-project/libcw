#ifndef cwSfScore_h
#define cwSfScore_h

namespace cw
{
  namespace sfscore
  {
    
    struct loc_str;
    struct set_str;

    // The score can be divided into arbitrary non-overlapping sections.
    typedef struct section_str
    {
      const char*      label;             // section label
      unsigned         index;             // index of this record in the internal section array
      struct loc_str*  locPtr;            // location where this section starts
      unsigned         begEvtIndex;       // score element index where this section starts    
      unsigned         setCnt;            // Count of elements in setArray[]
      struct set_str** setArray;          // Ptrs to sets which are applied to this section.
      double           vars[  score_parse::kVarCnt ]; // Set to DBL_MAX by default.
    } section_t;

    typedef struct event_str
    {
      unsigned     type;         // See score_parse ???TId
      double       secs;         // Time location in seconds 
      double       durSecs;      // Duration in seconds
      unsigned     index;        // Index of this event in the event array.
      unsigned     locIdx;       // Index of the onset location (oloc) containing this event
      midi::byte_t pitch;        // MIDI pitch of this note or the MIDI pedal id of pedal down/up msg (64=sustain 65=sostenuto 66=soft)
      midi::byte_t vel;          // MIDI velocity of this note
      unsigned     flags;        // Attribute flags for this event
      unsigned     dynVal;       // Dynamcis value pppp to ffff (1 to 11) for this note.
      double       frac;         // Note's time value for tempo and non-grace evenness notes.
      unsigned     barNumb;      // Bar id of the measure containing this event.
      unsigned     barNoteIdx;   // Index of this note in this bar
      unsigned     csvRowNumb;   // File row number (not index) from which this record originated
      unsigned     perfSmpIdx;   // Time this event was performed or cmInvalidIdx if the event was not performed.
      unsigned     perfVel;      // Velocity of the performed note or 0 if the note was not performed.
      unsigned     perfDynLvl;   // Index into dynamic level ref. array assoc'd with perfVel  
      unsigned     line;         // Line number of this event in the score file.
      unsigned     csvEventId;   // EventId from CSV 'evt' column.
      unsigned     hash;         // unique hash id for this note
      unsigned     varA[ score_parse::kVarCnt ];
    } event_t;

    // A 'set' is a collection of events that are grouped in time and all marked with a given attribute.
    // (e.g. eveness, tempo, dynamcs ... )o
    typedef struct set_str
    {
      unsigned        id;           // Unique id for this set
      unsigned        varId;        // See kXXXVarScId flags above
      event_t**       eleArray;     // Events that make up this set in time order
      unsigned        eleCnt;       // 
      section_t**     sectArray;    // Sections this set will be applied to
      unsigned        sectCnt;      // 
      unsigned*       symArray;     // symArray[sectCnt] - symbol name of all variables represented by this set (e.g '1a-e', '1b-e', '2-t', etc)
      unsigned*       costSymArray; // costSymArray[sectCnt] - same as symbols in symArray[] with 'c' prepended to front
      bool            doneFl;
      double          value;
      struct set_str* llink;        // loc_t setList link
    } set_t;

    typedef enum
    {
      kInvalidScMId,
      kRecdBegScMId,
      kRecdEndScMId,
      kFadeScMId,
      kPlayBegScMId,
      kPlayEndScMId  
    } markerId_t;

    // score markers
    typedef struct marker_str
    {
      markerId_t         markTypeId;  // marker type
      unsigned           labelSymId;  // marker label
      struct loc_str*    scoreLocPtr; // score location of the marker
      unsigned           csvRowIdx;   // score CSV file line assoc'd w/ this marker
      struct marker_str* link;        // loc_t.markList links
    } marker_t;

    // All events which are simultaneous are collected into a single
    // locc_t record.
    typedef struct loc_str
    {    
      unsigned       index;      // index of this location record
      double         secs;       // Time of this location
      unsigned       evtCnt;     // Count of events in evtArray[].
      event_t**      evtArray;   // Events which occur at this time.
      unsigned       barNumb;    // Bar number this event is contained by.                            
      set_t*         setList;    // Set's which end on this time location (linked through set_t.llink)
      section_t*     begSectPtr; // NULL if this location does not start a section
      marker_t*      markList;   // List of markers assigned to this location
    } loc_t;

    typedef dyn_ref_tbl::dyn_ref_t dyn_ref_t;
    typedef handle<struct sfscore_str> handle_t;

    // Create the score from a provided score parser
    rc_t create( handle_t& h,
                 score_parse::handle_t spH );

    // Create an internal score parser.
    rc_t create( handle_t&             h,
                 const char*           fname,
                 double                srate,
                 dyn_ref_tbl::handle_t dynRefH);

    
    rc_t destroy( handle_t& h );

    double sample_rate( handle_t& h );
    
    unsigned event_count( handle_t h );
    event_t* event( handle_t h, unsigned idx );
    event_t* hash_to_event( handle_t h, unsigned hash );

    unsigned loc_count( handle_t h );
    loc_t*   loc( handle_t h, unsigned idx );

    
    void report( handle_t h, const char* out_fname=nullptr );
    void parse_report( handle_t h );

    // see score_test::test() for testing this object

  }
}



#endif
