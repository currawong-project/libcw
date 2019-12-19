#include "cwCommon.h"
#include "cwLog.h"
#include "cwText.h"
#include "cwCommonImpl.h"
#include "cwMem.h"


unsigned cw::textLength( const char* s )
{ return s == nullptr ? 0 : strlen(s); }

int cw::textCompare( const char* s0, const char* s1 )
{
  if( s0 == nullptr || s1 == nullptr )
    return s0==s1 ? 0 : 1; // if both pointers are nullptr then trigger a match

  return strcmp(s0,s1);
}
