#include <stdio.h>
#include <stdarg.h>

#include "rpt.h"

void vrpt( printCallback_t printCbFunc, const char* fmt, va_list vl )
{
  if( printCbFunc != nullptr )
  {
    const int bufN = 32;
    char buf[bufN];
    vsnprintf(buf,bufN,fmt,vl);
    buf[bufN-1] = '\0';
    printCbFunc(buf);
  }
}

void rpt( printCallback_t printCbFunc, const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  vrpt(printCbFunc,fmt,vl);
  va_end(vl);
}


void rptHex(  printCallback_t printCbFunc, const void* buf, unsigned bufByteN, const char* label, bool asciiFl )
{
  const unsigned char* data = static_cast<const unsigned char*>(buf);
  const unsigned       colN = 8;
  unsigned             ci   = 0;

  if( label != nullptr )
    rpt(printCbFunc,"%s\n",label);
        
  for(unsigned i=0; i<bufByteN; ++i)
  {
    rpt(printCbFunc,"%02x ", data[i] );

    ++ci;
    if( ci == colN || i+1 == bufByteN )
    {
      unsigned n = ci==colN ? colN-1 : ci-1;

      for(unsigned j=0; j<(colN-n)*3; ++j)
        rpt(printCbFunc," ");

      if( asciiFl )
      {
        for(unsigned j=i-n; j<=i; ++j)
          if( 32<= data[j] && data[j] < 127 )
            rpt(printCbFunc,"%c",data[j]);
          else
            rpt(printCbFunc,".");
      }
      
      rpt(printCbFunc,"\n");
      ci = 0;
    }
  }  
}
