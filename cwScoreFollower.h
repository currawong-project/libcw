#ifndef cwScoreFollower_h
#define cwScoreFollower_h

namespace cw
{
  namespace score_follower
  {
    typedef handle< struct score_follower_str > handle_t;
    
    rc_t create( handle_t& hRef, const object_t* cfg, double srate );
    
    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h, unsigned loc );
    
    rc_t exec(  handle_t h, unsigned smpIdx, unsigned muid, unsigned status, uint8_t d0, uint8_t d1, unsigned* scLocIdxPtr );
  }
}


#endif
