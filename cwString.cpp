#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwString.h"


unsigned cw::str::len( const char* s)
{
  if( s == nullptr )
    return 0;
  return strlen(s);
}

char* cw::str::dupl( const char* s )
{ return mem::duplStr(s); }

char* cw::str::join( const char* sep, const char** subStrArray, unsigned ssN )
{
  unsigned sN = 0;
  char* s = nullptr;
  
  for(unsigned i=0; i<ssN; ++i)
    sN += len(subStrArray[i]);

  if( ssN >= 1 )
    sN += (ssN-1) * len(sep);

  if( sN == 0 )
    return nullptr;
  
  sN += 1;
  
  s = mem::alloc<char>(sN);

  s[0] = 0;
  for(unsigned i=0; i<ssN; ++i)
  {
    strcat(s,subStrArray[i]);
    if( sep != nullptr && ssN>=1 && i<ssN-1 )
      strcat(s,sep);
    
    assert( len(s) < sN );
  }

  return s;
}
