//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwText_H
#define cwText_H


namespace cw
{
  // Return 0 if s is null.
  unsigned textLength( const char* s );

  // dstN is the count of bytes in dst[]
  // If dst is non-null then dst is always 0-terminated.
  // srcN is the count of char's in src (as returned by textLength()).
  // src will be truncated if srcN > dstN-1.
  // If dst is null then null is returned
  // if src is null then dst[0] = 0.
  // if srcN is 0 then textLength(src) is used for srcN
  // Returns 'dst'.
  const char* textCopy( char* dst, unsigned dstN, const char* src, unsigned srcN=0 );
  
  // Same as textCopy( dst + textLength(dst), dstN, src, srcN )
  const char* textCat(  char* dst, unsigned dstN, const char* src, unsigned srcN=0 );
  

  void textToLower( char* s );
  void textToUpper( char* s );

  void textToLower( char* dst, const char* src, unsigned dstN );
  void textToUpper( char* dst, const char* src, unsigned dstN );
  
  // Note: if both s0 and s1 are nullptr then a match is indicated
  int textCompare(  const char* s0, const char* s1 );
  int textCompare(  const char* s0, const char* s1, unsigned n);
  
  // Case insensitive compare
  int textCompareI( const char* s0, const char* s1 );
  int textCompareI( const char* s0, const char* s1, unsigned n);
  
  inline bool textIsEqual( const char* s0, const char* s1 )             { return textCompare(s0,s1) == 0; }
  inline bool textIsEqual( const char* s0, const char* s1, unsigned n ) { return textCompare(s0,s1,n) == 0; }

  // Case insensitive is-equal
  inline bool textIsEqualI( const char* s0, const char* s1 )             { return textCompareI(s0,s1) == 0; }
  inline bool textIsEqualI( const char* s0, const char* s1, unsigned n ) { return textCompareI(s0,s1,n) == 0; }
  
  inline bool textIsNotEqual( const char* s0, const char* s1 )             { return !textIsEqual(s0,s1);   }
  inline bool textIsNotEqual( const char* s0, const char* s1, unsigned n ) { return !textIsEqual(s0,s1,n); }

  // Case insensitive is-not-equal
  inline bool textIsNotEqualI( const char* s0, const char* s1 )             { return !textIsEqualI(s0,s1);   }
  inline bool textIsNotEqualI( const char* s0, const char* s1, unsigned n ) { return !textIsEqualI(s0,s1,n); }

  
  
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

  // Return a pointer to the first occurrence of 'c' in s[] or nullptr
  // if 'c' does not occur in s[]
  char*       firstMatchChar( char* s, char c );
  const char* firstMatchChar( const char* s, char c );
  char*       firstMatchChar( char* s, unsigned sn, char c );
  const char* firstMatchChar( const char* s, unsigned sn, char c );
  
  // Find the last occurrent of 'c' in s[]. 
  char*       lastMatchChar( char* s, char c ); 
  const char* lastMatchChar( const char* s, char c );

  char*       removeTrailingWhitespace( char* s );
  
  bool isInteger( const char* );        // text contains only [0-9]
  bool isReal( const char* );           // text contains only [0-9] with one decimal place
  bool isIdentifier( const char* );      // text is a legal id [0-9,A-Z,a-z,_] w/o leading number

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
