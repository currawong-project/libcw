//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace score_follower
  {

    rc_t svgScoreFollowWrite( sfscore::handle_t scoreH,
                              sftrack::handle_t trackH,
                              ssf_note_on_t*  perfA,
                              unsigned        perfN,
                              const char*     out_fname,
                              bool            show_muid_fl);
    
  }
}
