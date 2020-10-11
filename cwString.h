#ifndef cwString_H
#define cwString_H


namespace cw
{
  namespace str
  {
    unsigned len( const char* s );
    char*    dupl( const char* s );
    char*    join( const char* sep, const char** subStrArray, unsigned ssN );
  }
}


#endif
