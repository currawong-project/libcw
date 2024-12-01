//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef rpt_h
#define rpt_h

typedef void (*printCallback_t)( const char* text );

void vrpt(   printCallback_t printCbFunc, const char* fmt, va_list vl );
void rpt(    printCallback_t printCbFunc, const char* fmt, ... );
void rptHex( printCallback_t printCbFunc, const void* buf, unsigned bufByteN, const char* label = nullptr, bool asciiFl=true );

#endif
