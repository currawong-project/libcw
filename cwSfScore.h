#ifndef cwSfScore_h
#define cwSfScore_h

namespace cw
{
  namespace sfscore
  {
    
    enum
    {
      kInvalidEvtScId = 0,
      kTimeSigEvtScId,
      kKeySigEvtScId,
      kTempoEvtScId,
      kTrackEvtScId,
      kTextEvtScId,
      kNameEvtScId,
      kEOTrackEvtScId,
      kCopyEvtScId,
      kBlankEvtScId,
      kBarEvtScId,
      kPgmEvtScId,
      kCtlEvtScId,
      kNonEvtScId,
      kPedalEvtScId
    };

    // Flags used by event_t.flags
    enum
    {
      kEvenScFl    = 0x001,        // This note is marked for evenness measurement
      kDynScFl     = 0x002,        // This note is marked for dynamics measurement
      kTempoScFl   = 0x004,        // This note is marked for tempo measurement
      //kSkipScFl    = 0x008,        // This isn't a real event (e.g. tied note) skip over it
      kGraceScFl   = 0x010,        // This is a grace note
      kInvalidScFl = 0x020,        // This note has a calculated time
      //kPedalDnScFl   = 0x040,        // This is a pedal down event (pitch holds the pedal id and durSecs holds the time the pedal will remain down.)
      //kPedalUpScFl   = 0x080,         // This is a pedal up event (pitch holds the pedal id)
    };


    // Id's used by set_t.varId and as indexes into
    // section_t.vars[].
    enum
    {
      kInvalidVarScId = 0, // 0
      kMinVarScId = 1,
      kEvenVarScId = kMinVarScId,    // 1
      kDynVarScId = 2,     // 2
      kTempoVarScId = 3,   // 3
      kScVarCnt = 4      
    };

    enum : uint8_t  {
      kInvalidDynVel = 0
    };

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
      double           vars[ kScVarCnt ]; // Set to DBL_MAX by default.
    } section_t;

    typedef struct event_str
    {
      unsigned     type;         // Event type
      double       secs;         // Time location in seconds 
      double       durSecs;      // Duration in seconds
      unsigned     index;        // Index of this event in the event array.
      unsigned     locIdx;       // Index of the location containing this event
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
    } event_t;

    // A 'set' is a collection of events that are grouped in time and all marked with a given attribute.
    // (e.g. eveness, tempo, dynamcs ... )
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

    typedef struct
    {
      const char* label;
       uint8_t    vel;
    } dyn_ref_t;

    typedef handle<struct sfscore_str> handle_t;

    rc_t create( handle_t& h, const char* fname, double srate, const dyn_ref_t* dynRefA=nullptr, unsigned dynRefN=0 );
    rc_t destroy( handle_t& h );

    unsigned event_count( handle_t h );
    event_t* event( handle_t h, unsigned idx );

    unsigned loc_count( handle_t h );
    loc_t*   loc( handle_t h, unsigned idx );

    rc_t parse_dyn_ref_cfg( const object_t* cfg, dyn_ref_t*& refArrayRef, unsigned& refArrayNRef);
    
    void report( handle_t h );


    rc_t test( const object_t* cfg );

  }
}



#endif
