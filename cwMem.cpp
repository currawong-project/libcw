#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"


namespace cw
{
  
  void* _memAlloc( void* p0, unsigned n, bool zeroFl )
  {
    void*    p   = nullptr;     // ptr to new block
    unsigned p0N = 0;           // size of existing block
    unsigned* p0_1 = nullptr;   // pointer to base of existing block

    n += sizeof(unsigned); // add space for the size of the block
    
    // if there is no existing block
    if( p0 != nullptr )
    {
      // get a pointer to the base of the exsting block
      p0_1 = ((unsigned*)p0) - 1;
      
      p0N = p0_1[0]; // get size of existing block

      // if the block is shrinking
      if( p0N >= n )
        return p0;
    }
    
    p = malloc(n);  // allocate new memory

    // if expanding then copy in data from existing block
    if( p0 != nullptr )
    {
      memcpy(p,p0_1,p0N);
      memFree(p0);  // free the existing block
    }

    // if requested zero the block
    if( zeroFl )
      memset(((char*)p)+p0N,0,n-p0N);

    // get pointer to base of new block
    unsigned* p1 = static_cast<unsigned*>(p);
      
    p1[0] = n; // set size of new block

    // advance past the block size and return
    return p1+1;    
  }

}

unsigned cw::memByteCount( const void* p )
{
  return p==nullptr ? 0 : static_cast<const unsigned*>(p)[-1];
}


char* cw::memAllocStr( const char* s )
{
  char* s1 = nullptr;
  
  if( s != nullptr )
  {
    unsigned sn = strlen(s);
    s1 = static_cast<char*>(_memAlloc(nullptr,sn+1,false));
    memcpy(s1,s,sn);
    s1[sn] = 0;
  }

  return s1;
}

void* cw::memAllocDupl( const void* p0, unsigned byteN )
{
  if( p0 == nullptr || byteN == 0 )
    return nullptr;
  
  void* p1 = _memAlloc(nullptr,byteN,false);
  memcpy(p1,p0,byteN);
  return p1;
}

void* cw::memAllocDupl( const void* p )
{
  return memAllocDupl(p,memByteCount(p));
}



void cw::memFree( void* p )
{
  if( p != nullptr)
  {
    free(static_cast<unsigned*>(p)-1);
  }
}
