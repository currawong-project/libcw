#ifndef cwText_H
#define cwText_H


namespace cw
{
  // Return 0 if s is null.
  unsigned textLength( const char* s );
  
  // if both s0 and s1 are nullptr then a match is indicated
  int textCompare( const char* s0, const char* s1 );
  int textCompare( const char* s0, const char* s1, unsigned n);
  
  inline bool textIsEqual( const char* s0, const char* s1 )             { return textCompare(s0,s1) == 0; }
  inline bool textIsEqual( const char* s0, const char* s1, unsigned n ) { return textCompare(s0,s1,n) == 0; }

  inline bool textIsNotEqual( const char* s0, const char* s1 )             { return !textIsEqual(s0,s1);   }
  inline bool textIsNotEqual( const char* s0, const char* s1, unsigned n ) { return !textIsEqual(s0,s1,n); }
  
  // Return a pointer to the next white space char
  // or nullptr if 's' is null are there are no whitespace char's.
  const char* nextWhiteChar( const char* s );

  
  // Return a pointer to the next white space char,
  // a pointer to the EOS if there are no white space char's,
  // or nullptr if 's' is null.
  const char* nextWhiteCharEOS( const char* s );

  // Return a pointer to the next non-white space char
  // or nullptr if 's' is null are there are no non-whitespace char's.
  const char* nextNonWhiteChar( const char* s );

  // Return a pointer to the next non-white space char,
  // a pointer to the EOS if there are no non-white space char's,
  // or nullptr if 's' is null.
  const char* nextNonWhiteCharEOS( const char* s );

  // Join s0 and s1 to form one long string.  Release the returned string with mem::free()
  char* textJoin( const char* s0, const char* s1 );

  // Realloc s0 and append s1.
  char* textAppend( char* s0, const char* s1 );
  
  unsigned toText( char* buf, unsigned bufN, bool v );
  unsigned toText( char* buf, unsigned bufN, unsigned char v );
  unsigned toText( char* buf, unsigned bufN, char v );
  unsigned toText( char* buf, unsigned bufN, unsigned short v );
  unsigned toText( char* buf, unsigned bufN, short v );
  unsigned toText( char* buf, unsigned bufN, unsigned int v  );
  unsigned toText( char* buf, unsigned bufN, int v );
  unsigned toText( char* buf, unsigned bufN, unsigned long long v  );
  unsigned toText( char* buf, unsigned bufN, long long v );
  unsigned toText( char* buf, unsigned bufN, float v );
  unsigned toText( char* buf, unsigned bufN, double v );
  unsigned toText( char* buf, unsigned bufN, const char* v );



  
}

#endif
