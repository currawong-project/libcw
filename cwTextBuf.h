#ifndef cwTextBuf_H
#define cwTextBuf_H


namespace cw
{
  typedef handle<struct textBuf_str> textBufH_t;

  rc_t textBufCreate( textBufH_t& hRef, unsigned initCharN=1024, unsigned expandCharN=1024 );
  rc_t textBufDestroy(textBufH_t& hRef );

  const char* textBufText( textBufH_t h);
  
  rc_t textBufPrintf( textBufH_t h, const char* fmt, va_list vl );
  rc_t textBufPrintf( textBufH_t h, const char* fmt,  ... );

  rc_t textBufPrintBool( textBufH_t h, bool v );
  rc_t textBufPrintInt( textBufH_t h, int v );
  rc_t textBufPrintUInt( textBufH_t h, unsigned v );
  rc_t textBufPrintFloat( textBufH_t h, double v );

  rc_t textBufSetBoolFormat( textBufH_t h, bool v, const char* s);
  rc_t textBufSetIntFormat( textBufH_t h, unsigned width, unsigned flags );
  rc_t textBufSetFloatFormat( textBufH_t h, unsigned width, unsigned decPlN );

  
  
}

#endif
