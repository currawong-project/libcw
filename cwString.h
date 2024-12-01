//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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
