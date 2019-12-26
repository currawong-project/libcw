#ifndef cwMem_H
#define cwMem_H

namespace cw
{

  void* _memAlloc( void* p, unsigned n, bool zeroFl );
  
  char* memAllocStr( const char* );
  void* memAllocDupl( const void* p, unsigned byteN );
  void* memAllocDupl( const void* p );
  void  memFree( void* );

  unsigned memByteCount( const void* p );
  
  template<typename T>
    void memRelease(T& p) { memFree(p); p=nullptr; }
  
  template<typename T>
    T* memAllocZ(unsigned n=1) { return static_cast<T*>(_memAlloc(nullptr,n*sizeof(T),true)); }

    template<typename T>
    T* memAlloc(unsigned n=1) { return static_cast<T*>(_memAlloc(nullptr,n*sizeof(T),false)); }

  template<typename T>
    T* memResizeZ(T* p, unsigned n=1) { return static_cast<T*>(_memAlloc(p,n*sizeof(T),true)); }


  template<typename T>
    size_t _memTextLength(const T* s )
  {
    if( s == nullptr )
      return 0;
    
    // get length of source string
    size_t n=0;
    
    for(; s[n]; ++n)
    {}

    return n;
  }
  
  template<typename T>
    T* memDuplStr( const T* s, size_t n )
  {
    if( s == nullptr )
      return nullptr;
    
    n+=1; // add one for terminating zero

    // allocate space for new string
    T* s1 = memAlloc<T>(n);

    // copy in new string
    for(size_t i=0; i<n-1; ++i)
      s1[i] = s[i];

    s1[n-1] = 0;
    return s1;
  }

  template<typename T>
    T* memDuplStr( const T* s )
  {
    if( s == nullptr )
      return nullptr;
    
    
    return memDuplStr(s,_memTextLength(s));
  }

  template<typename T>
    T* memReallocStr( T* s0, const T* s1 )
  {
    if( s1 == nullptr )
    {
      memFree(s0);
      return nullptr;
    }

    if( s0 == nullptr )
      return memDuplStr(s1);

    
    size_t s0n = _memTextLength(s0);
    size_t s1n = _memTextLength(s1);

    // if s1[] can't fit in space of s0[]
    if( s1n > s0n )
    {
      memFree(s0);
      return memDuplStr(s1);
    }

    // s1n is <= s0n 
    // copy s1[] into s0[]
    size_t i=0;
    for(; s1[i]; ++i)
      s0[i] = s1[i];
    s0[i] = 0;

    return s0;
  }

  template<typename C>
    C* memPrintf(C* p0, const char* fmt, va_list vl0 )
  {
    va_list vl1;
    va_copy(vl1,vl0);
    
    size_t bufN = vsnprintf(nullptr,0,fmt,vl0);

    if( bufN == 0)
    {
      memFree(p0);
      return nullptr;
    }

    
    C buf[ bufN + 1 ];
    size_t n = vsnprintf(buf,bufN+1,fmt,vl1);

    cwAssert(n <= bufN);

    buf[bufN] = 0;
    
    va_end(vl1);

    return memReallocStr(p0,buf);
  }

  
  template<typename C>
    C* memPrintf(C* p0, const char* fmt, ... )
  {
    va_list vl;
    va_start(vl,fmt);
    C* p1 = memPrintf(p0,fmt,vl);
    va_end(vl);
    return p1;
  }
  

  
}

#endif
