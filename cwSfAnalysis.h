//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace sf_analysis
  {

    rc_t gen_analysis( sfscore::handle_t        scH,
                       const sftrack::result_t* resultA,
                       unsigned                 resultN,
                       unsigned                 begLoc,
                       unsigned                 endLoc,
                       const char*              fname );

    

  }
}
