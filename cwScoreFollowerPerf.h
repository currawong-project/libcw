//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace score_follower
  {
    typedef struct ssf_note_on_str
    {
      double   sec;
      unsigned muid;
      unsigned loc;
      uint8_t  pitch;
      uint8_t  vel;
      uint8_t  pad[2];
    } ssf_note_on_t;
  }
}
