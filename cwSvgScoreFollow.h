namespace cw
{
  namespace score_follower
  {

    rc_t svgScoreFollowWrite( cmScH_t         cmScH,
                              cmScMatcher*    matcher,
                              ssf_note_on_t*  perfA,
                              unsigned        perfN,
                              const char*     out_fname,
                              bool            show_muid_fl);
    
  }
}
