//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace score_parse
  {

    enum {
      kInvalidLocId = 0
    };
    
    enum {
      kInvalidTId,
      kBarTId,
      kSectionTId,
      kBpmTId,
      kNoteOnTId,
      kNoteOffTId,
      kPedalTId,
      kRestTId,
      kCtlTId,
    };

    const char* opcode_id_to_label( unsigned opId );
    unsigned    opcode_label_to_id( const char* label );

    const char* dyn_ref_id_to_label( unsigned dynId );
    unsigned    dyn_ref_label_to_id( const char* label );

    unsigned    attr_char_to_flags( const char* label );
    const char* attr_flags_to_char( unsigned flag );

    enum {
      kDynVarFl      = 0x0001,
      kEvenVarFl     = 0x0002,
      kTempoVarFl    = 0x0004,
      kSetEndVarFl   = 0x0008,
      kGraceFl       = 0x0010,
      kTieBegFl      = 0x0020,
      kTieContinueFl = 0x0040,
      kTieEndFl      = 0x0080,
      kOnsetFl       = 0x0100      
    };

    enum {
      kMinVarIdx = 0,
      kDynVarIdx = kMinVarIdx,
      kEvenVarIdx,
      kTempoVarIdx,
      kVarCnt
    };

    typedef enum {
      kDynStatIdx,
      kEvenStatIdx,
      kTempoStatIdx,
      kCostStatIdx,
      kStatCnt
    } stats_idx_t;

    struct set_str;
    struct event_str;

    typedef struct stats_str
    {
      stats_idx_t id;   
      double min;
      double max;
      double mean;
      double std;      
    } stats_t;
    
    typedef struct section_str
    {
      char*               label;       // This sections label
      unsigned            csvRowNumb;  // score CSV row number where this section starts
      unsigned            setN;        // Count of elements in setA[]
      struct set_str**    setA;        // setA[setN] Array of pointers to sets that are applied to this section.
      struct event_str*   begEvent;    // first event in this section
      struct event_str*   endEvent;    // last event in this section
      struct event_str*   begSetEvent; // first set event in this section
      struct event_str*   endSetEvent; // last set event in this section
      
      stats_t             statsA[ kStatCnt ];
      
      struct section_str* link;        // p->sectionL links
    } section_t;

    struct event_str;
    
    typedef struct set_str
    {
      unsigned           id;             // Unique id for this set
      unsigned           varTypeId;      // Type of measurement to perform on this set.
      unsigned           eventN;         // Count of elements in eventA[]
      struct event_str** eventA;         // eventA[eventN] Pointers to all events in this set.
      section_t*         targetSection;  // Section this set will be applied to.
      unsigned           sectionSetIdx;  // Index of this set in the targetSection->setA[].
      struct set_str*    link;           // p->setL link
    } set_t;

    typedef struct event_var_str
    {
      unsigned   flags;           // var flags
      set_t*     set;             // set this event belongs to 
      unsigned   setNoteIdx;      // the index of this event in it's owner set
      section_t* target_section;  // the target section of this set - if this is the last event in the set
    } event_var_t;
    
    typedef struct event_str
    {
      unsigned     csvRowNumb;  // CSV row number this event was derived from
      unsigned     opId;        // event type
      unsigned     index;       // index of this recod in event_array() 
      section_t*   section;     // section that this event belongs to
      unsigned     barNumb;     //
      unsigned     barEvtIdx;   // index of this event in this bar
      unsigned     barPitchIdx; // count of this pitch in this bar
      unsigned     voice; 
      unsigned     tick;
      double       sec;
      double       rval;
      unsigned     dotCnt;
      unsigned     dynLevel;  // dynamic level based on marker
      unsigned     hash;      // [ op_id:4 bar:12 pitch:8 bar_pitch_n:8 ]
      unsigned     eLocId;    // event location id (includes all events, oloc includes just note-ons)
      unsigned     oLocId;    // onset location id (onset loc id's used by sfScore)
      unsigned     bpm;
      double       bpm_rval;
      unsigned     flags;
      midi::byte_t status;
      midi::byte_t d0;
      midi::byte_t d1;
      char*        sciPitch;
      event_var_t  varA[ kVarCnt ];
      
    } event_t;

    
    typedef handle<struct score_parse_str> handle_t;

    const char* opcode_id_to_label( unsigned opId );
    unsigned    opcode_label_to_id( const char* label );
    
    unsigned    var_char_to_flags( const char* label );
    const char* var_flags_to_char( unsigned flags );
    const char* var_index_to_char( unsigned var_idx );
    
    const char* dyn_ref_level_to_label( handle_t h, unsigned vel );
    unsigned    dyn_ref_label_to_level( handle_t h, const char* label );
    unsigned    dyn_ref_vel_to_level(   handle_t h, uint8_t vel );

    unsigned    form_hash( unsigned op_id, unsigned bar, uint8_t midi_pitch, unsigned barPitchIdx );
    void        parse_hash( unsigned hash, unsigned& op_idRef, unsigned& barRef, uint8_t& midi_pitchRef, unsigned& barPitchIdxRef );
    
    rc_t create( handle_t& hRef, const char* fname, double srate, dyn_ref_tbl::handle_t dynRefH, bool show_warnings_fl=false );
    rc_t destroy( handle_t& hRef );

    double sample_rate( handle_t h );

    unsigned       event_count( handle_t h );
    const event_t* event_array( handle_t h );

    unsigned section_count( handle_t h );
    // Returns a linked list of section records in time order.
    const section_t* section_list( handle_t h );

    unsigned set_count( handle_t h );
    // Returns a linked list of sets.
    const set_t* set_list( handle_t h );
    

    void report( handle_t h );

    // see score_test::test() for testing this object

  }
}
