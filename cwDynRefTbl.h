//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwDynRefTbl_h
#define cwDynRefTbl_h

namespace cw
{
  namespace dyn_ref_tbl
  {
    typedef struct dyn_ref_str
    {
      char*       marker;   // text marker (e.g. p,mf_m,mf,mf_p, ff, ...)
      unsigned    level;    // level as a numeric id - generally in range 0-19   (0=silent, 1=lowest audible dynamic)
      uint8_t     velocity; // level as a MIDI velocity
    } dyn_ref_t;
    
    typedef handle<struct dyn_ref_tbl_str> handle_t;

    
    // Parse object like: [ { mark:<> level:<>, vel:<> } ]
    rc_t create( handle_t& hRef, const object_t* cfg );
    rc_t destroy( handle_t& hRef );

    const char* level_to_marker( handle_t h, unsigned level );
    
    // Returns kInvalidIdx on error.
    unsigned    marker_to_level( handle_t h, const char* marker );

    // Return midi::kInvalidMidiByte on error
    midi::byte_t     level_to_velocity( handle_t h, unsigned level );

    // MIDI velocity to nearest dynamic level    
    unsigned    velocity_to_level( handle_t h, midi::byte_t vel );

    void report( handle_t h );
  }
}

#endif
