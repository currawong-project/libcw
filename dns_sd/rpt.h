#ifndef rpt_h
#define rpt_h

typedef void (*printCallback_t)( const char* text );

void vrpt(   printCallback_t printCbFunc, const char* fmt, va_list vl );
void rpt(    printCallback_t printCbFunc, const char* fmt, ... );
void rptHex( printCallback_t printCbFunc, const void* buf, unsigned bufByteN, const char* label = nullptr, bool asciiFl=true );

#endif
