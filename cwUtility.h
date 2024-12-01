//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwUtility_h
#define cwUtility_h

namespace cw
{
  void printHex( const void* buf, unsigned bufByteN, bool asciiFl=true );
  
  double   x80ToDouble( unsigned char s[10] );
  void     doubleToX80( double v, unsigned char s[10] );

  bool		  isPowerOfTwo( unsigned x );
  unsigned  nextPowerOfTwo(	unsigned val );
  unsigned  nearestPowerOfTwo( unsigned val );
    
  
}

#endif
