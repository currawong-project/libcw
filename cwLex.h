#ifndef cwLex_H
#define cwLex_H


//( { file_desc:"User configurable lexer for tokenizing text files." kw:[text]}

namespace cw
{

  // Predefined Lexer Id's
  enum
  {
   kErrorLexTId,    //  0  the lexer was unable to identify the current token
   kUnknownLexTId,  //  1  the token is of an unknown type (only used when kReturnUnknownLexFl is set)
   kEofLexTId,      //  2  the lexer reached the end of input
   kSpaceLexTId,    //  3  white space
   kRealLexTId,     //  4  real number (contains a decimal point or is in scientific notation) 
   kIntLexTId,      //  5  decimal integer
   kHexLexTId,      //  6  hexidecimal integer
   kIdentLexTId,    //  7  identifier
   kQStrLexTId,     //  8  quoted string
   kQCharLexTId,    //  9  quoted char
   kBlockCmtLexTId, // 10  block comment
   kLineCmtLexTId,  // 11  line comment
   kUserLexTId      // 12 user registered token (See lexRegisterToken().)
  };

  // Lexer control flags used with lexInit().
  enum
  {
   kReturnSpaceLexFl    = 0x01, //< Return space tokens
   kReturnCommentsLexFl = 0x02, //< Return comment tokens
   kReturnUnknownLexFl  = 0x04, //< Return unknown tokens
   kReturnQCharLexFl    = 0x08, //< Return quoted characters
   kUserDefPriorityLexFl= 0x10  //< User defined tokens take priority even if a kIdentLexTId token has a longer match
  };


  typedef handle<struct lex_str> lexH_t;
  extern lexH_t lexNullHandle;

  // Iniitalize the lexer and receive a lexer handle in return.
  // Set cp to nullptr if the buffer will be later via lexSetTextBuffer();
  // See the kXXXLexFl enum's above for possible flag values.
  rc_t             lexCreate( lexH_t& hRef, const char* cp, unsigned cn, unsigned flags );

  // Finalize a lexer created by an earlier call to lexInit()
  rc_t             lexDestroy( lexH_t& hRef );

  // Rewind the lexer to the begining of the buffer (the same as post initialize state)
  rc_t             lexReset( lexH_t h );

  // Verify that a lexer handle is valid
  bool               lexIsValid( lexH_t h );

  // Set a new text buffer and reset the lexer to the post initialize state.
  rc_t             lexSetTextBuffer( lexH_t h, const char* cp, unsigned cn );
  rc_t             lexSetFile( lexH_t h, const char* fn );

  // Register a user defined token. The id of the first user defined token should be
  // kUserLexTId+1.  Neither the id or token text can be used by a previously registered
  // or built-in token. 
  rc_t             lexRegisterToken( lexH_t h, unsigned id, const char* token );

  // Register a user defined token recognition function.  This function should return the count
  // of initial, consecutive, characters in 'cp[cn]' which match its token pattern.
  typedef unsigned (*lexUserMatcherPtr_t)( const char* cp, unsigned cn );

  rc_t             lexRegisterMatcher( lexH_t h, unsigned id, lexUserMatcherPtr_t funcPtr );

  // Enable or disable the specified token type.
  rc_t             lexEnableToken( lexH_t h, unsigned id, bool enableFl );

  // Get and set the lexer filter flags kReturnXXXLexFl.
  // These flags can be safely enabled and disabled between
  // calls to lexGetNextToken().
  unsigned           lexFilterFlags( lexH_t h );
  void               lexSetFilterFlags( lexH_t h, unsigned flags );

  // Return the type id of the current token and advances to the next token
  unsigned           lexGetNextToken( lexH_t h );

  // Return the type id associated with the current token. This is the same value
  // returned by the previous call to lexGetNextToken().
  unsigned           lexTokenId( lexH_t h ); 

  // Return a pointer to the first character of text associated with the 
  // current token. The returned pointer directly references the text contained
  // in the buffer given to the lexer in the call to lexInit().  The string
  // is therefore not zero terminated. Use lexTokenCharCount() to get the 
  // length of the token string.
  const char* lexTokenText( lexH_t h );

  // Return the count of characters in the text associated with the current token.
  // This is the only way to get this count since the string returned by 
  // lexTokenText() is not zero terminated.
  unsigned           lexTokenCharCount(  lexH_t h );

  // Return the value of the current token as an integer.
  int                lexTokenInt( lexH_t h );

  // Return the value of the current token as an unsigned integer.
  unsigned           lexTokenUInt( lexH_t h );

  // Return the value of the current token as a float.
  float              lexTokenFloat( lexH_t h );

  // Return the value of the current token as a double.
  double             lexTokenDouble( lexH_t h );

  // Return true if the current token is an int and it was suffixed
  // with 'u' to indicate that it is unsigned.
  bool               lexTokenIsUnsigned( lexH_t h );

  // Return true if the current token is a real and it was suffexed 
  // with 'f' to indicate that it is a single precision float.
  bool               lexTokenIsSinglePrecision( lexH_t h );

  // Return the line number associated with the current token 
  unsigned           lexCurrentLineNumber( lexH_t h );

  // Return the starting column of the current token
  unsigned           lexCurrentColumnNumber( lexH_t h ); 

  // Return the RC code associated with the last error
  unsigned           lexErrorRC( lexH_t h );

  // Return the label associated with a token id
  const char* lexIdToLabel( lexH_t h, unsigned typeId );

  // Lexer testing stub.
  void lexTest(  );

}

//)



#endif
