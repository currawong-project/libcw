#ifndef cwUtility_h
#define cwUtility_h

namespace cw
{
  void printHex( const void* buf, unsigned bufByteN, bool asciiFl=true );
  
  double   x80ToDouble( unsigned char s[10] );
  void     doubleToX80( double v, unsigned char s[10] );
    
}

#endif
