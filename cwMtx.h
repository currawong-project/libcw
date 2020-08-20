#ifndef cwMtx_h
#define cwMtx_h

namespace cw
{
  namespace mtx
  {
    enum
    {
     kAliasReleaseFl   = 0x01,  // do not allocate memory, use the passed data pointer, and eventually release it
     kAliasNoReleaseFl = 0x02,  // do not allocate memory, use the passed data pointer, and do not ever release it
     kDuplDataFl       = 0x04,  //  allocate data space and copy the data in
     kZeroFl           = 0x08,  // zero the newly allocated data 
    };
      
    template< typename T >
     struct mtx_str
    {
      unsigned  flags = 0;
      unsigned  dimN  = 0;
      unsigned* dimV  = nullptr;
      T*        base  = nullptr;
      unsigned  allocEleN = 0;     // always 0 if data is aliased
    };

    template< typename T >
      void release( struct mtx_str<T>*& m )
    {
      if( m != nullptr )
      {
        mem::release(m->dimV);
        if( cwIsNotFlag(m->flags,kAliasNoReleaseFl) )
          mem::release(m->base);
        mem::release(m);
      }
    }

    template< typename T >
      struct mtx_str<T>* _init( struct mtx_str<T>* m, unsigned dimN, const unsigned* dimV, T* base=nullptr, unsigned flags=0 )
    {
      // if a pre-allocated mtx obj was not given then allocate one
      if( m == nullptr )
        m = mem::allocZ<mtx_str<T>>(1);

      // if the pre-allocd mtx obj has more dim's then the new one 
      if( m->dimN >= dimN )
        m->dimN = dimN; 
      else // else expand dimV[]
      {
        m->dimV = mem::resize<unsigned>(m->dimV,dimN);
        m->dimN = dimN;
      }

      // update dimV[] with the new extents and calc. the new ele count
      unsigned eleN = 0;
      for(unsigned i=0; i<dimN; ++i)
      {
        m->dimV[i] = dimV[i];
        eleN = (i==0 ? 1 : eleN) * dimV[i];
      }

      bool aliasFl = cwIsFlag(flags, kAliasNoReleaseFl | kAliasReleaseFl );
      
      // if the new object data is aliased
      if( aliasFl )
      {
        // release any memory the pre-allocated obj may own
        if( cwIsNotFlag(m->flags,kAliasNoReleaseFl) )
          mem::release(m->base);
        
        m->base      = base;
        m->allocEleN = 0;    // always 0 when data is aliased
      }
      else // the new object is not aliased
      {
        // if the current data space is too small then reallocate it
        if( eleN > m->allocEleN )
        {
          // don't allow an alias-no-release ptr to be released
          if( cwIsFlag(m->flags,kAliasNoReleaseFl) )
            m->base = nullptr;
          
          m->base      = mem::resize<T>(m->base, eleN, cwIsFlag(flags,kZeroFl) ? mem::kZeroAllFl : 0 );
          m->allocEleN = eleN;
        }
      }

      // if duplication was requested
      if( cwIsFlag(flags,kDuplDataFl) )
      {
        assert( aliasFl == false );
        memcpy(m->base,base, eleN*sizeof(T) );
      }

      m->flags = flags;
      
      return m;
      
    }

    // Allocate the matrix w/o zeroing the initial contents
    template< typename T >
      struct mtx_str<T>* alloc( unsigned dimN, const unsigned* dimV )
    { return _init<T>( nullptr, dimN, dimV, nullptr, 0); }

    // Allocate the matrix and zero the contents
    template< typename T >
      struct mtx_str<T>* allocZ( unsigned dimN, const unsigned* dimV )
    { return _init<T>( nullptr, dimN, dimV, nullptr, kZeroFl); }

    // Allocate the matrix and copy the data from base[]
    template< typename T >
      struct mtx_str<T>* allocDupl( unsigned dimN, const unsigned* dimV, const T* base )
    { return _init<T>( nullptr, dimN, dimV, const_cast<T*>(base), kDuplDataFl); }

    // Allocate a matrix and use base[] as the data. Release base[] when it is no longer needed.
    template< typename T >
      struct mtx_str<T>* allocAlias( unsigned dimN, const unsigned* dimV, T* base )
    { return _init<T>( nullptr, dimN, dimV, base, kAliasReleaseFl); }

    // Allocate a mtrix and use base[] as the data - do NOT release base[].
    template< typename T >
      struct mtx_str<T>* allocAliasNoRelease( unsigned dimN, const unsigned* dimV, const T* base )
    { return _init<T>( nullptr, dimN, dimV, const_cast<T*>(base), kAliasNoReleaseFl); }

    
    template< typename T >
      struct mtx_str<T>* alloc( unsigned dimN, const unsigned* dimV, T* base=nullptr, unsigned flags=0 )
    { return _init<T>( nullptr, dimN, dimV, base, flags); }

    
    // resize m[] 
    template< typename T >
      struct mtx_str<T>* resize( struct mtx_str<T>* m, const unsigned* dimV, unsigned dimN, T* base=nullptr, unsigned flags=0 )
    { return _init<T>( m, dimN, dimV, base, flags ); }

    // resize y[] to have the same size as x[]
    template< typename T >
      struct mtx_str<T>* resize( struct mtx_str<T>* y, const struct mtx_str<T>& x )
    { return resize(y,x->dimV,x->dimN); }

    // Return 'true' if the matrices have the same size.
    template< typename T >
      bool is_size_equal( const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      if( x0.dimN != x1.dimN )
        return false;
      
      for(unsigned i=0; i<x0->dimN; ++i)
        if( x0.dimV[i] != x1.dimV[i] )
          return false;

      return true;
    }

    // Return the count of elements in the matrix
    template< typename T >
      bool ele_count( const struct mtx_str<T>& x )
    {
      unsigned eleN = 1;
      for(unsigned i=0; i<eleN; ++i)
        eleN *= x.dimV[i];
      return eleN;
    }
    
    // y = m * x (elementwise)
    template< typename T >
      void mult( struct mtx_str<T>& y, const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      assert( is_size_equal(x0,x1) );
      resize<T>(&y,x0); // resize y to the same dim's as m
      unsigned n = ele_count<T>(x0);
      for(unsigned i=0; i<n; ++i)
        y.base[i] = x0.base[i] * x1.base[i];
    }

    // y *= x (elementwise)
    template< typename T >
      void mult( struct mtx_str<T>& y, const struct mtx_str<T>& x )
    {
      assert( is_size_equal(y,x) );
      unsigned n = ele_count<T>(x);
      for(unsigned i=0; i<n; ++i)
        y.base[i] *= x.base[i];
    }

    // y = x * scalar (elementwise)
    template< typename T >
      void mult( struct mtx_str<T>& y, const struct mtx_str<T>& x, const T& scalar )
    {
      resize<T>(&y,x); // resize y to the same dim's as m
      unsigned n = ele_count<T>(x);      
      for(unsigned i=0; i<n; ++i)
        y.base[i] = x.base[i] * scalar;
    }

    // y *= scalar (elementwise)
    template< typename T >
    void mult( struct mtx_str<T>& y, const T& scalar )
    {
      unsigned n = ele_count<T>(y);      
      for(unsigned i=0; i<n; ++i)
        y.base[i] *= scalar;
    }

    // y = m + x (elementwise)
    template< typename T >
      void add( struct mtx_str<T>& y, const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      assert( is_size_equal(x0,x1) );
      resize<T>(&y,x0); // resize y to the same dim's as m
      unsigned n = ele_count<T>(x0);
      for(unsigned i=0; i<n; ++i)
        y.base[i] = x0.base[i] + x1.base[i];
    }

    // y += x (elementwise)
    template< typename T >
      void add( struct mtx_str<T>& y, const struct mtx_str<T>& x )
    {
      assert( is_size_equal(y,x) );
      unsigned n = ele_count<T>(x);
      for(unsigned i=0; i<n; ++i)
        y.base[i] += x.base[i];
    }

    // y = x + scalar (elementwise)
    template< typename T >
      void add( struct mtx_str<T>& y, const struct mtx_str<T>& x, const T& scalar )
    {
      resize(&y,x);
      unsigned n = ele_count<T>(y);      
      for(unsigned i=0; i<n; ++i)
        y.base[i] = x.base[i] + scalar;
    }

    // y += scalar (elementwise)
    template< typename T >
    void add( struct mtx_str<T>& y, const T& scalar )
    {
      unsigned n = ele_count<T>(y);      
      for(unsigned i=0; i<n; ++i)
        y.base[i] += scalar;
    }
    
    
    template< typename T >
      void mtx_mul( struct mtx_str<T>& y, const struct mtx_str<T>& m, const struct mtx_str<T>& x )
    {
    }


    
    typedef struct mtx_str<float> fmtx_t;

    
    
  }

}


#endif
