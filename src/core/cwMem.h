//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwMem_H
#define cwMem_H

namespace cw
{

  namespace mem
  {
    enum
    {
     kZeroNewFl = 0x01, // zero only the expanded (new) space during a resize() operation
     kZeroAllFl = 0x02  // zero all the space during a resize operation
    };
      
    void* _alloc( void* p, unsigned n, unsigned flags );
    void* _allocDupl( const void* p, unsigned byteN );
    void* _allocDupl( const void* p );
  
    char* allocStr( const char* );
    void  free( void* );

    void set_warn_on_alloc();
    void clear_warn_on_alloc();
    
    unsigned byteCount( const void* p );
  
    template<typename T>
      void release(T& p) { ::cw::mem::free(p); p=nullptr; }

    template<typename T>
      T* alloc(unsigned n, unsigned flags) { return static_cast<T*>(_alloc(nullptr,n*sizeof(T),flags)); }
    
    template<typename T>
      T* allocZ(unsigned n=1) { return alloc<T>(n,kZeroAllFl); }

    template<typename T>
      T* alloc(unsigned n=1) { return alloc<T>(n,0); }

    template<typename T>
      T* resize(T* p, unsigned n, unsigned flags) { return static_cast<T*>(_alloc(p,n*sizeof(T),flags)); }
        
    // zero the newly allocated space but leave the initial space unchanged.
    template<typename T>
      T* resizeZ(T* p, unsigned n=1) { return resize<T>(p,n,kZeroNewFl); }

    template<typename T>
      T* resize(T* p, unsigned n=1) { return resize<T>(p,n,0); }

    template<typename T>
      T* allocDupl(const T* p, unsigned eleN ) { return (T*)_allocDupl(p,eleN*sizeof(T)); }

    template<typename T>
      size_t _textLength(const T* s )
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
      T* duplStr( const T* s, size_t n )
    {
      if( s == nullptr )
        return nullptr;
    
      n+=1; // add one for terminating zero

      // allocate space for new string
      T* s1 = alloc<T>(n);

      // copy in new string
      for(size_t i=0; i<n-1; ++i)
        s1[i] = s[i];

      s1[n-1] = 0;
      return s1;
    }

    template<typename T>
      T* duplStr( const T* s )
    {
      if( s == nullptr )
        return nullptr;
    
    
      return duplStr(s,_textLength(s));
    }

    template<typename T>
      T* reallocStr( T* s0, const T* s1 )
    {
      if( s1 == nullptr )
      {
        free(s0);
        return nullptr;
      }

      if( s0 == nullptr )
        return duplStr(s1);

    
      size_t s0n = _textLength(s0);
      size_t s1n = _textLength(s1);

      // if s1[] can't fit in space of s0[]
      if( s1n > s0n )
      {
        free(s0);
        return duplStr(s1);
      }

      // s1n is <= s0n 
      // copy s1[] into s0[]
      size_t i=0;
      for(; s1[i]; ++i)
        s0[i] = s1[i];
      s0[i] = 0;

      return s0;
    }

    template<typename T>
      T* appendStr( T* s0, const T* s1 )
    {
      if( s1 == nullptr || strlen(s1)== 0 )
        return s0;

      if( s0 == nullptr )
        return duplStr(s1);

    
      size_t s0n = _textLength(s0);
      size_t s1n = _textLength(s1);
      size_t sn  = s0n + s1n;

      T* s = alloc<T>(sn+1);
      strcpy(s,s0);
      strcpy(s+s0n,s1);

      free(s0);
      
      return s;
    }


    template<typename C>
      C* _printf(C* p0, bool appendFl, const char* fmt, va_list vl0 )
    {
      va_list vl1;
      va_copy(vl1,vl0);
    
      size_t bufN = vsnprintf(nullptr,0,fmt,vl0);

      if( bufN == 0)
      {
        free(p0);
        return nullptr;
      }

    
      C buf[ bufN + 1 ];
      size_t n = vsnprintf(buf,bufN+1,fmt,vl1);

      cwAssert(n <= bufN);

      buf[bufN] = 0;
    
      va_end(vl1);

      return appendFl ? appendStr(p0,buf) : reallocStr(p0,buf);
    }

    template<typename C>
      C* printf(C* p0, const char* fmt, va_list vl0 )
    { return _printf(p0,false,fmt,vl0); }

    template<typename C>
      C* printp(C* p0, const char* fmt, va_list vl0 )
    { return _printf(p0,true,fmt,vl0); }

    
    template<typename C>
      C* printf(C* p0, const char* fmt, ... )
    {
      va_list vl;
      va_start(vl,fmt);
      C* p1 = _printf(p0,false,fmt,vl);
      va_end(vl);
      return p1;
    }

    template<typename C>
      C* printp(C* p0, const char* fmt, ... )
    {
      va_list vl;
      va_start(vl,fmt);
      C* p1 = printp(p0,fmt,vl);
      va_end(vl);
      return p1;
    }
    
  }
  
}

#endif
