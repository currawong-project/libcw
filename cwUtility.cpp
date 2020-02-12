#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwUtility.h"

void cw::printHex( const void* buf, unsigned bufByteN, bool asciiFl )
{
  const unsigned char* data = static_cast<const unsigned char*>(buf);
  const unsigned       colN = 8;
  unsigned             ci   = 0;
        
  for(unsigned i=0; i<bufByteN; ++i)
  {
    printf("%02x ", data[i] );

    ++ci;
    if( ci == colN || i+1 == bufByteN )
    {
      unsigned n = ci==colN ? colN-1 : ci-1;

      for(unsigned j=0; j<(colN-n)*3; ++j)
        printf(" ");

      if( asciiFl )
      {
        for(unsigned j=i-n; j<=i; ++j)
          if( 32<= data[j] && data[j] < 127 )
            printf("%c",data[j]);
          else
            printf(".");
      }
      
      printf("\n");
      ci = 0;
    }
  }  
}
