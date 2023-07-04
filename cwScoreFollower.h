#ifndef cwScoreFollower_h
#define cwScoreFollower_h

namespace cw
{
  namespace score_follower
  {
    typedef handle< struct score_follower_str > handle_t;
    
    rc_t create( handle_t& hRef, const object_t* cfg, cm::handle_t cmCtxH, double srate );
    
    rc_t destroy( handle_t& hRef );

    bool is_enabled( handle_t h );
    
    // Set the starting search location and calls clear_match_id_array().
    rc_t reset( handle_t h, unsigned loc );

    // If 'new_match_fl_ref' is returned as true then there are new match id's in the current_match_id_array[]
    rc_t exec(  handle_t h, double sec, unsigned smpIdx, unsigned muid, unsigned status, uint8_t d0, uint8_t d1, bool& new_match_fl_ref );

    // Get a pointer to the event id's associated with the latest set of matches.
    const unsigned* current_match_id_array( handle_t h, unsigned& cur_match_id_array_cnt_ref );

    // Clear the match id array.  This should be done to empty the current_match_id_array() 
    void clear_match_id_array( handle_t h );

    // Get the min and max cw loc values for the current score.
    rc_t cw_loc_range( handle_t h, unsigned& minLocRef, unsigned& maxLocRef );
    bool is_loc_in_range( handle_t h, unsigned loc );

    unsigned has_stored_performance( handle_t h );

    // Set the 'loc' field on the stored performance from the stored score following info.
    rc_t     sync_perf_to_score( handle_t h );

    // Return the count of stored performance records in the performance array.
    unsigned perf_count( handle_t h );

    // Return the base of the stored performance array.
    const score_follower::ssf_note_on_t* perf_base( handle_t h );

    // Write an SVG file containing a graphic view of the score following results since the last call to reset().
    // Set show_muid_fl to true to display the 'muid' of the performed notes in the
    // SVG rendering, otherwise the performed note sequence (order of arrival) id is shown.
    rc_t write_svg_file( handle_t h, const char* out_fname, bool show_muid_fl=false );

    // Write the score to 'out_fname'.
    void score_report( handle_t h, const char* out_fname );

    // Use the stored MIDI data received since the last call to reset to generate a report
    // using midi_state::report_events().
    rc_t midi_state_rt_report( handle_t h, const char* out_fname );

    rc_t test( const object_t* cfg );
  }
}


#endif
