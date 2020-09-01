#ifndef cwMtx_h
#define cwMtx_h

/*
Memory Layout:

3x2 mtx:

0 3
1 4
2 5

3x2x2 mtx

front  back
0 3  |  6 9
1 4  |  7 10
2 5  |  8 11


More about dope and weight vectors.
https://stackoverflow.com/questions/30409991/use-a-dope-vector-to-access-arbitrary-axial-slices-of-a-multidimensional-array

 */

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
      unsigned* mulV  = nullptr;
      T*        base  = nullptr;
      unsigned  allocEleN = 0;     // always 0 if data is aliased
    };  

    
    template< typename T >
      void release( struct mtx_str<T>& m )
    {
        mem::release(m.dimV);
        if( cwIsNotFlag(m.flags,kAliasNoReleaseFl) )
          mem::release(m.base);
    }
    
    template< typename T >
      void release( struct mtx_str<T>*& m )
    {
      if( m != nullptr )
      {
        release(*m);
        mem::release(m);
      }
    }

    // Note that dimV[] is always copied and therefore is the reponsibility of the caller to free.
    template< typename T >
      struct mtx_str<T>* _init( struct mtx_str<T>* m, unsigned dimN, const unsigned* dimV, T* base=nullptr, unsigned flags=0 )
    {
      // if a pre-allocated mtx obj was not given then allocate one
      if( m == nullptr )
        m = mem::allocZ<mtx_str<T>>(1);

      // if the pre-allocd mtx obj has more dim's than the new one 
      if( m->dimN >= dimN )
        m->dimN = dimN; 
      else // else expand dimV[]
      {
        m->dimV = mem::resize<unsigned>(m->dimV,dimN*2);
        m->mulV = m->dimV + dimN;
        m->dimN = dimN;
      }

      // update dimV[] with the new extents and calc. the new ele count
      unsigned eleN = 0;
      unsigned mul  = 1;
      for(unsigned i=0; i<dimN; ++i)
      {
        m->dimV[i] = dimV[i];
        m->mulV[i] = mul;
        mul *= dimV[i];
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


    unsigned _offsetDimV( const unsigned* dimV, unsigned dimN, unsigned* idxV );
    unsigned _offsetMulV( const unsigned* dimV, unsigned dimN, unsigned* idxV );
    unsigned     _mtx_object_get_degree(  const struct object_str* cfg );
    rc_t     _mtx_object_get_shape(   const struct object_str* cfg, unsigned i,  unsigned* dimV, unsigned dimN, unsigned& eleN );

    // 'i' is the index into 'idxV[]' of the matrix dimension which 'cfg' refers to
    template< typename T>
      rc_t _get_mtx_eles_from_cfg( const struct object_str* cfg, struct mtx_str<T>* m, unsigned i, unsigned* idxV )
    {
      rc_t rc = kOkRC;

      // if cfg is not a list then this must be a value
      if( !cfg->is_list() )
      {
        // get the value
        T v;
        if(cfg->value(v) != kOkRC )
          return cwLogError(kSyntaxErrorRC,"Unable to obtain matrix value in dimension index: %i\n",i);

        // and store it in the current idxV[] location
        m->base[ _offsetMulV(m->mulV,m->dimN,idxV) ] = v;
        
        return kOkRC;
      }

      // otherwise this is a list - and the list must contain lists or values
      for(unsigned j=0; j<cfg->child_count(); ++j)
      {
        // update idxV[] which the dimension of the ith child elment
        idxV[i] = j;

        // recurse!
        if((rc = _get_mtx_eles_from_cfg(cfg->child_ele(j) ,m, i+1, idxV)) != kOkRC )
          break;
      }


      return rc;
    }

    template< typename T >
      struct mtx_str<T>* allocCfg( const struct object_str* cfg  )
    {
      unsigned           dimN = 0;

      // get the degree of the matrix
      dimN = _mtx_object_get_degree(cfg);

      // if 'cfg' does not refer to a matrix
      if( dimN == 0 )
      {
        cwLogError(kSyntaxErrorRC,"The matrix object does not have a list-list syntax.");
      }
      else
      {
        // allocate the shape vector
        unsigned dimV[dimN];
        unsigned idxV[dimN];
        unsigned eleN = 0;
        struct mtx_str<T>* m = nullptr;

        // get the shape of the matrix
        if( _mtx_object_get_shape(cfg,0,dimV,dimN,eleN) != kOkRC )
          return nullptr;

        // allocate the matrix
        if((m = alloc<T>(dimN,dimV)) == nullptr )
          cwLogError(kObjAllocFailRC,"A matrix allocation failed.");        
        else
          // 
          if(_get_mtx_eles_from_cfg<T>(cfg,m,0,idxV) == kOkRC )
            return m;
        
      }
      
      return nullptr;

    }
    

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


    template< typename T >
      unsigned offset( const struct mtx_str<T>* m, const unsigned* idxV )
    {
      unsigned offset = 0;      
      for(unsigned i=0; i<m->dimN; ++i)
        offset += idxV[i] * m->mulV[i];
      
      return offset;
    }
    
    
    template< typename T >
      unsigned _offset( const struct mtx_str<T>* m, int i, unsigned offs )
    { return offs; }

    template< typename T, typename... ARGS>
      unsigned _offset( const struct mtx_str<T>* m, int i, unsigned offs, unsigned idx, ARGS&&... args)
    { return _offset(m,i+1, offs + idx*m->mulV[i], std::forward<ARGS>(args)...);   }
    
    template< typename T, typename... ARGS>
      unsigned offset( const struct mtx_str<T>* m, unsigned idx, ARGS&&... args)
    { return _offset(m,0,0,idx,std::forward<ARGS>(args)...); }

    template< typename T, typename... ARGS>
      unsigned offset( const struct mtx_str<T>& m, unsigned idx, ARGS&&... args)
    { return _offset(&m,0,0,idx,std::forward<ARGS>(args)...); }
    
    template< typename T >
      T* addr( const struct mtx_str<T>* m, const unsigned* idxV )
    { return m->base + offset(m,idxV); }
    
    template< typename T, typename... ARGS>
      T* addr( struct mtx_str<T>* m, unsigned i, ARGS&&... args)
    { return m->base + offset(m,i,std::forward<ARGS>(args)...); }

    template< typename T >
      T& ele( const struct mtx_str<T>* m, const unsigned* idxV )
    { return *addr(m,idxV); }
    
    template< typename T, typename... ARGS>
      T& ele( struct mtx_str<T>* m, unsigned i, ARGS&&... args)
    { return *addr(m,i,std::forward<ARGS>(args)...); }
    
    
    template< typename T >
      bool is_col_vector( const struct mtx_str<T>& m )
    { return m->dimN==1 || (m->dimN==2 && m->dimV[1]==1); };

    template< typename T >
      bool is_row_vector( const struct mtx_str<T>& m )
    { return m->dimN==2 && m->dimV[0]==1; }

    template< typename T >
      bool is_vector( const struct mtx_str<T>& m )
    { return is_col_vector(m) || is_row_vector(m); }
      
    // Return 'true' if the matrices have the same size.
    template< typename T >
      bool is_size_equal( const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      if( x0.dimN != x1.dimN )
        return false;
      
      for(unsigned i=0; i<x0.dimN; ++i)
        if( x0.dimV[i] != x1.dimV[i] )
          return false;

      return true;
    }
    
    template< typename T >
      bool is_equal( const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      if( !is_size_equal(x0,x1) )
        return false;

      unsigned N = ele_count(x0);
      for(unsigned i=0; i<N; ++i)
        if( x0.base[i] != x1.base[i] )
          return false;

      return true;
    }

    
    // Return the count of elements in the matrix
    template< typename T >
      unsigned ele_count( const struct mtx_str<T>& x )
    {
      unsigned eleN = 1;
      for(unsigned i=0; i<x.dimN; ++i)
        eleN *= x.dimV[i];
      return eleN;
    }

    
    template< typename T >
      void transpose( struct mtx_str<T>& m )
    {
      for(unsigned i=0; i<m.dimN/2; ++i)
      {
        unsigned x             = m.mulV[i];
        m.mulV[i]              = m.mulV[ m.dimN-(i+1) ];
        m.mulV[ m.dimN-(i+1) ] = x;

        x = m.dimV[i];
        m.dimV[i] = m.dimV[ m.dimN-(i+1) ];
        m.dimV[m.dimN-(i+1)] = x;
      }
    }


    template< typename T >
      void _print( const struct mtx_str<T>& m, unsigned* idxV, unsigned i, unsigned decPl, unsigned colWidth )
    {
      if( i == m.dimN )
      {
        double v = ele( &m, idxV );

        // print the value
        printf("%*.*f ",colWidth,decPl,v);
      }
      else
      {
        for(unsigned j=0; j<m.dimV[i]; ++j)
        {
          if( m.dimN>=2 && i == m.dimN-2 )
          {
            // print the dimension index for matrices with 3+ dim's
            if( i > 0 && j == 0 )
              printf("%i\n",idxV[i-1]);

            // print the row index for matrices with 2+ dim's
            if( m.dimN>1 )
              printf("%i | ",j);
          }
          
          idxV[i] = j;
          _print(m, idxV, i+1, decPl, colWidth );
        }
        
        // prevent multiple newlines on last printed line
        if( m.dimN==1 || (m.dimN>=2 && i > m.dimN-2) )
          printf("\n");
      }
    }
    
    template< typename T >
      void print( const struct mtx_str<T>& m, unsigned decPl=3, unsigned colWidth=10 )
    {
      unsigned idxV[ m.dimN ];
      memset(idxV,0,sizeof(idxV));

      if( is_int<T>(*m.base) )
        decPl = 0;
        
      _print( m, idxV, 0, decPl, colWidth );
    }
    
    template< typename T >
      void report( const struct mtx_str<T>& m, const char* label, unsigned decPl=3, unsigned colWidth=10 )
    {
      printf("%s :",label);
      for(unsigned i=0; i<m.dimN; ++i)
        printf("%i ", m.dimV[i] );
      printf("\n");
      
      print(m,decPl,colWidth);
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
    
    
    template< typename T0, typename T1 >
      rc_t mtx_mul( struct mtx_str<T0>& y, const struct mtx_str<T0>& m, const struct mtx_str<T1>& x )
    {
      assert( x.dimN >= 1 && m.dimN >= 1 );
      
      unsigned xrn = x.dimN==1 ? ele_count<T1>(x) : x.dimV[0];
      unsigned xcn = x.dimN==1 ? 1                : x.dimV[1];
      unsigned mrn = m.dimN==1 ? 1                : m.dimV[0];
      unsigned mcn = m.dimN==1 ? ele_count<T0>(m) : m.dimV[1];
      unsigned yDimV[] = { mrn, xcn };
      if( mcn != xrn )
        return cwLogError(kInvalidArgRC, "Mtx mult. failed. Size mismatch: m[%i,%i] x[%i,%i].",mrn,mcn,xrn,xcn);

      //printf("%i %i : %i %i\n",mrn,mcn,xrn,xcn);
      
      resize(&y,yDimV, 2 );

      // go across the columns of x
      for(unsigned i=0; i<xcn; ++i)
      {
        // go down the rows of m[]
        for(unsigned j=0; j<mrn; ++j)
        {
          // calc the first memory offset for each mtx
          unsigned yi = offset(y,j,i);  
          unsigned mi = offset(m,j,0);
          unsigned xi = offset(x,0,i);

          // calc increment for each offset by calc'ing
          // the second offset and subtracting the first
          unsigned dxi = offset(x,1,i) - xi;
          unsigned dmi = offset(m,j,1) - mi;

          // calc stopping point
          unsigned mN = mi + (mcn*dmi);

          y.base[ yi ] = 0;

          // go down the rows of x[] and across the columns of m[]
          for(; mi<mN; mi+=dmi,xi+=dxi)
            y.base[ yi ] += m.base[ mi ] * x.base[ xi ];
          
        }
      }
      
      return kOkRC;
    }


    
    typedef struct mtx_str<float>  f_t;
    typedef struct mtx_str<double> d_t;

    rc_t test( const struct object_str* cfg );
    
    
  }

}


#endif
