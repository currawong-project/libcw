namespace cw
{
  namespace score_parse
  {
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

    struct set_str;
    struct event_str;
    typedef struct section_str
    {
      char*               label;
      unsigned            csvRowNumb; // score CSV row number where this section starts
      unsigned            setN;
      struct set_str**    setA;
      struct event_str*   begEvent;
      struct event_str*   endEvent;
      struct section_str* link;
    } section_t;

    struct event_str;
    
    typedef struct set_str
    {
      unsigned           id;
      unsigned           varTypeId;
      unsigned           eventN;
      struct event_str** eventA;
      section_t*         targetSection;
      unsigned           sectionSetIdx; // index of this set in the section
      struct set_str*    link;
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
      unsigned     csvId;       // CSV 'index' column
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
      unsigned     loc;
      unsigned     loctn;
      unsigned     oloc;
      unsigned     bpm;
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

    unsigned    form_hash( unsigned op_id, unsigned bar, uint8_t midi_pitch, unsigned barPitchIdx );
    void        parse_hash( unsigned hash, unsigned& op_idRef, unsigned& barRef, uint8_t& midi_pitchRef, unsigned& barPitchIdxRef );
    
    rc_t create( handle_t& hRef, const char* fname, double srate, dyn_ref_tbl::handle_t dynRefH );
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
