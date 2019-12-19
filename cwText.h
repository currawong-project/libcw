#ifndef cwText_H
#define cwText_H


namespace cw
{
  unsigned textLength( const char* s );
  
  // if both s0 and s1 are nullptr then a match is indicated
  int textCompare( const char* s0, const char* s1 );
}

#endif
