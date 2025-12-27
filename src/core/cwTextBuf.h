//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwTextBuf_H
#define cwTextBuf_H


namespace cw
{
  namespace textBuf
  {
    typedef handle<struct textBuf_str> handle_t;

    rc_t create( handle_t& hRef, unsigned initCharN=1024, unsigned expandCharN=1024 );
    handle_t create(unsigned initCharN=1024, unsigned expandCharN=1024 );
    
    rc_t destroy(handle_t& hRef );

    void clear( handle_t h );
    const char* text( handle_t h);
  
    rc_t print( handle_t h, const char* fmt, va_list vl );
    rc_t print( handle_t h, const char* fmt,  ... );

    rc_t printBool( handle_t h, bool v );
    rc_t printInt( handle_t h, int v );
    rc_t printUInt( handle_t h, unsigned v );
    rc_t printFloat( handle_t h, double v );

    rc_t setBoolFormat( handle_t h, bool v, const char* s);
    rc_t setIntFormat( handle_t h, unsigned width, unsigned flags );
    rc_t setFloatFormat( handle_t h, unsigned width, unsigned decPlN );

    rc_t test( const test::test_args_t& args);
  }  
}

#endif
