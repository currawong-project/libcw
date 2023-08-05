#ifndef cwSfScoreParser_h
#define cwSfScoreParser_h

namespace cw
{
  namespace sfscore
  {

    enum {
      kEvenFl     = 0x0001,
      kEvenEndFl  = 0x0002,
      kDynFl      = 0x0004,
      kDynEndFl   = 0x0008,
      kTempoFl    = 0x0010,
      kTempoEndFl = 0x0020,

      kFlagMask   = 0x003f
    };
      
    
    unsigned    opcode_label_to_id( const char* label );
    const char* opcode_id_to_label( unsigned opcode_id );

    unsigned    var_label_to_type_id( const char* label );
    unsigned    var_label_to_type_flag( const char* label );
    const char* var_type_id_to_label( unsigned varTypeId );
    const char* var_type_flag_to_label( unsigned varTypeFlag );
    char        var_type_flag_to_char( unsigned varTypeFlag );
    unsigned    var_type_id_to_flag( unsigned varTypeId );     // returns type flag
    unsigned    var_type_id_to_mask( unsigned varTypeId );     // returns type flag and end flag mask 
    unsigned    var_type_id_to_end_flag( unsigned varTypeId ); // returns end flag
    unsigned    var_type_flag_to_id( unsigned varTypeFlag );
    
    namespace parser
    {
      typedef handle<struct sfscore_parser_str> handle_t; 

      typedef struct p_section_str
      {
        char*    label;
        unsigned begEvtIdx;
        unsigned endEvtIdx;
        struct p_section_str* link;
      } p_section_t;

      typedef struct p_set_str
      {
        unsigned             id;             // unique id for this set
        unsigned             varTypeId;      // k???ScFl 
        p_section_t*         target_section; // section to which this set will be applied
        struct p_event_str*  beg_event;      // first event in this section
        unsigned             eventN;         // count of events in this set
        struct p_event_str** eventA;         // points to each event this set
        struct p_set_str*    link;           // 
      } p_set_t;
    
    
      typedef struct p_event_str
      {
        unsigned     typeId;      // opcode type id
        unsigned     csvEventId;  // CSV 'evt' id
        unsigned     csvRowNumb;  // CSV line number
        unsigned     line;        // 
        unsigned     index;       // index into eventArray[]
        double       secs;        // event offset from beginning of score in seconds
        double       barSecs;     // event offset from bar in sectonds
        unsigned     locIdx;      // location index (chord notes share the same locIdx)
        unsigned     barNumb;     // bar this event belongs to
        unsigned     barNoteIdx;  // note index of this note in the bar to which it belongs.
        p_section_t* section;     // section to which this event belongs
        unsigned     sectionIdx;  // note index of this note in the section to which it belongs
        unsigned     flags;       // attribute flags
        midi::byte_t pitch;
        midi::byte_t vel;
        unsigned     dynVal;      // index into dynRefA[]
        double       t_frac;      //
        char*        sciPitch;    //

        char*    sectionLabelA[ kScVarCnt ]; // var end section labels (if this event is the last event in a section)
        p_set_t* setA[ kScVarCnt ];          // set points (if this event is part of a set)
      
      } p_event_t;
    
      rc_t create( handle_t& hRef, const char* fname, const dyn_ref_t* dynRefA, unsigned dynRefN );
      rc_t destroy( handle_t& hRef );

      unsigned event_count( handle_t h );
      const p_event_t* event_array( handle_t h );

      unsigned section_count( handle_t h );
      // Returns a linked list of section records in time order.
      const p_section_t* section_list( handle_t h );

      unsigned set_count( handle_t h );
      // Returns a linked list of sets.
      const p_set_t* set_list( handle_t h );


      void report( handle_t h );

      rc_t test( const char* fname, const dyn_ref_t* dynRefA, unsigned dynRefN );

    }
    
  }
}

#endif
