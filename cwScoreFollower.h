#ifndef cwScoreFollower_h
#define cwScoreFollower_h

namespace cw
{
  namespace score_follower
  {
    typedef handle< struct score_follower_str > handle_t;
    
    rc_t create( handle_t& hRef, const object_t* cfg, cm::handle_t cmCtxH, double srate );
    
    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h, unsigned loc );

    // If 'new_match_fl_ref' is returned as true then there are new match id's in the current_match_id_array[]
    rc_t exec(  handle_t h, unsigned smpIdx, unsigned muid, unsigned status, uint8_t d0, uint8_t d1, bool& new_match_fl_ref );

    // Get a pointer to the event id's associated with the latest set of matches.
    unsigned* current_match_id_array( handle_t h, unsigned& cur_match_id_array_cnt_ref );

    // Clear the match id array.  This should be done to empty the current_match_id_array() 
    void clear_match_id_array( handle_t h );

    rc_t test( const object_t* cfg );
  }
}


#endif
