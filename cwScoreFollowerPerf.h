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
