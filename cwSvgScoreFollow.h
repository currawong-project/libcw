namespace cw
{
  namespace score_follower
  {
    typedef struct ssf_note_on_str
    {
      double  sec;
      unsigned muid;
      uint8_t pitch;
      uint8_t vel;
      uint8_t pad[2];
    } ssf_note_on_t;

    rc_t svgScoreFollowWrite( cmScH_t         cmScH,
                              cmScMatcher*    matcher,
                              ssf_note_on_t*  perfA,
                              unsigned        perfN,
                              const char*     out_fname,
                              bool            show_muid_fl);
    
  }
}
