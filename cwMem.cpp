//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"


void* cw::mem::_alloc( void* p0, unsigned n, unsigned flags )
  {
    void*     p    = nullptr;   // ptr to new block
    unsigned  p0N  = 0;         // size of existing block
    unsigned* p0_1 = nullptr;   // pointer to base of existing block

    n += 2*sizeof(unsigned); // add space for the size of the block
    
    // if there is an existing block
    if( p0 != nullptr )
    {
      // get a pointer to the base of the exsting block
      p0_1 = ((unsigned*)p0) - 2;
      
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
      mem::free(p0);  // free the existing block
    }


    // zero all memory
    if( cwIsFlag(flags, kZeroAllFl))
      memset(((char*)p),0,n);
    else
      // zero the exapnded memory but leave existing memory unchanged
      if( cwIsFlag(flags, kZeroNewFl ))
        memset(((char*)p)+p0N,0,n-p0N);

    // get pointer to base of new block
    unsigned* p1 = static_cast<unsigned*>(p);
      
    p1[0] = n; // set size of new block

    // advance past the block size and return
    return p1+2;    
    
    /*
    n += 8;
    char* p = (char*)calloc(1,n);
    return p+8;
    */
  }



unsigned cw::mem::byteCount( const void* p )
{
  return p==nullptr ? 0 : static_cast<const unsigned*>(p)[-1];
}


char* cw::mem::allocStr( const char* s )
{
  char* s1 = nullptr;
  
  if( s != nullptr )
  {
    unsigned sn = strlen(s);
    s1 = static_cast<char*>(_alloc(nullptr,sn+1,false));
    memcpy(s1,s,sn);
    s1[sn] = 0;
  }

  return s1;
}

void* cw::mem::_allocDupl( const void* p0, unsigned byteN )
{
  if( p0 == nullptr || byteN == 0 )
    return nullptr;
  
  void* p1 = _alloc(nullptr,byteN,false);
  memcpy(p1,p0,byteN);
  return p1;
}

void* cw::mem::_allocDupl( const void* p )
{
  return _allocDupl(p,byteCount(p));
}



void cw::mem::free( void* p )
{
  if( p != nullptr)
  {
    ::free(static_cast<unsigned*>(p)-2);
  }
}
