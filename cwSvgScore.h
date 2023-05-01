#ifndef cwSvgScore_h
#define cwSvgScore_h

namespace cw
{
  namespace svg_score
  {
    typedef handle<struct svg_score_str> handle_t;

    rc_t create(  handle_t& hRef );
    rc_t destroy( handle_t& hRef );

    rc_t setPianoScore( handle_t h, score::handle_t pianoScH );

    rc_t write( handle_t h, const char* outFname, const char* cssFname );

    rc_t write( const object_t* cfg );
  }
}


#endif
