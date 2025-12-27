//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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
     kDimV_NoReleaseFl = 0x04,  // do not release the dimV array when the matrix is released.
     kDuplDataFl       = 0x08,  //  allocate data space and copy the data in
     kZeroFl           = 0x10,  // zero the newly allocated data 
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
      if( cwIsNotFlag(m.flags,kDimV_NoReleaseFl) )
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

    // Release data memory when this matrix is released
    template< typename T >
      void set_memory_release_flag( struct mtx_str<T>& m, bool linkDimVFl=true )
    {
      m.flags = cwClearFlag(m.flags,kAliasNoReleaseFl);
      if( linkDimVFl )
        m.flags = cwClearFlag(m.flags,kDimV_NoReleaseFl);
    }

    // Do NOT release data memory when this matrix is released.
    template< typename T >
      void clear_memory_release_flag( struct mtx_str<T>& m, bool linkDimVFl=true )
    {
      m.flags = cwSetFlag(m.flags,kAliasNoReleaseFl);
      if( linkDimVFl )
        m.flags = cwSetFlag(m.flags,kDimV_NoReleaseFl);
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
        assert( base != nullptr );
        
        if( base != nullptr )
          memcpy(m->base,base, eleN*sizeof(T) );
      }

      m->flags = flags;
      
      return m;
      
    }

    template< typename T >
      struct mtx_str<T>* alloc( const unsigned* dimV, unsigned dimN, T* base, unsigned flags=0 )
    { return _init<T>( nullptr, dimN, dimV, base, flags); }

    
    template< typename T>
      struct mtx_str<T>* _alloc_( unsigned flags, unsigned* dimV, unsigned dimN)
    {
      return alloc<T>(dimV, dimN, nullptr, flags );
    }
    
    template< typename T, typename... ARGS>
      struct mtx_str<T>* _alloc_( unsigned flags, unsigned* dimV, unsigned dimN, unsigned n, ARGS&&... args)
    {
      unsigned _dimV[ dimN + 1 ];
      vop::copy(_dimV,dimV,dimN);
      _dimV[dimN] = n;
      return _alloc_<T>(flags,_dimV, dimN+1, std::forward<ARGS>(args)...);      
    }

    template< typename T, typename... ARGS>
      struct mtx_str<T>* alloc( unsigned flags, ARGS&&... args)
    { return _alloc_<T>(flags,nullptr,0,args...); }
    
        
    // Allocate the matrix w/o zeroing the initial contents
    template< typename T >
      struct mtx_str<T>* alloc( const unsigned* dimV, unsigned dimN  )
    { return _init<T>( nullptr, dimN, dimV, nullptr, 0); }

    // Allocate the matrix and zero the contents
    template< typename T >
      struct mtx_str<T>* allocZ( const unsigned* dimV, unsigned dimN )
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
    unsigned _mtx_object_get_degree(  const struct object_str* cfg );
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

    // Allocate a new matrix by parsing an object_t description.
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
        if((m = alloc<T>(dimV,dimN)) == nullptr )
          cwLogError(kObjAllocFailRC,"A matrix allocation failed.");        
        else
          // 
          if(_get_mtx_eles_from_cfg<T>(cfg,m,0,idxV) == kOkRC )
            return m;
        
      }
      return nullptr;
    }

    template< typename T >
      void _slice_setup( const struct mtx_str<T>& m, const unsigned* sIdxV, const unsigned* sCntV, unsigned* siV, unsigned *snV )
    {
      // if sIdx is not given then assume it is the origin
      if( sIdxV != nullptr )
        vop::copy(siV,sIdxV,m.dimN);
      else
        vop::zero(siV,m.dimN);

      // calculate the length in each dimension
      for(unsigned i=0; i<m.dimN; ++i)
        snV[i] = (sCntV==nullptr || sCntV[i] == kInvalidCnt) ? m.dimV[i]-siV[i] : sCntV[i];
      
    }
    
    
    // Allocate a new matrix by slicing an existing matrix and duplicating the contents into a new matrix
    // Set the elements of sCntV to kInvalidCnt to indicate that the entire dimension following the offset index should be copied.
    // Set sIdxV to nullptr to begin at 0,0,...
    // Set sCntV to nullptr to take all elements after sIdxV.
    template< typename T0, typename T1 >
      struct mtx_str<T0>* alloc( const struct mtx_str<T1>& src, const unsigned* sIdxV, const unsigned* sCntV )
    {
      struct mtx_str<T0>* m;
      unsigned siV[ src.dimN ];
      unsigned snV[ src.dimN ];
      unsigned dIdxV[ src.dimN ];
      vop::zero(dIdxV,src.dimN);
      
      _slice_setup<T1>(src,sIdxV,sCntV,siV,snV);
      
      if((m = alloc<T0>(snV,src.dimN) ) == nullptr )
        return nullptr;

      copy(*m,dIdxV,src,siV,snV);
      return m;
    }


    // Allocate a new matrix by slicing an existing matrix and aliasing the contents into a new matrix.
    // Set the elements of sCntV to kInvalidCnt to indicate that the entire dimension following the offset index should be copied.
    // Set sIdxV to nullptr to begin at 0,0,...
    // Set sCntV to nullptr to take all elements after sIdxV.
    template< typename T >
      struct mtx_str<T>* sliceAlias( const struct mtx_str<T>& src, const unsigned* sIdxV, const unsigned* sCntV )
    {
      struct mtx_str<T>* m;
      unsigned siV[ src.dimN ];
      unsigned snV[ src.dimN ];

      _slice_setup(src,sIdxV,sCntV,siV,snV);
      
      m = allocAliasNoRelease( src.dimN, snV, addr(src,siV) );

      // the memory layout in the slice mtx is the same as the matrix
      // that it aliases and therefore the 'mulV' vector is the same
      // in both matrices.
      vop::copy(m->mulV,src.mulV,src.dimN);

      return m;
    }
    
    
    template<typename T0, typename T1>
      struct mtx_str<T0>* _slice( const struct mtx_str<T1>& m, unsigned* iV, unsigned iN, unsigned index )
    {
      // the offset index vector must be fully specified
      if( index < m.dimN )
      {
        cwLogError(kInvalidArgRC,"An invalid number index + count values was given to slice(). %i < %i.",index,m.dimN);
        return nullptr;
      }

      // fill in the end of iV[] with kInvalidCnt to indicate that all values after the offset should be copied
      for(; index<iN; ++index)
        iV[index] = kInvalidCnt;

      // allocate a matrix to hold the slice
      return alloc<T0,T1>(m,iV,iV+m.dimN);
    }
    
    template< typename T0, typename T1, typename... ARGS>
      struct mtx_str<T0>* _slice( const struct mtx_str<T1>& m, unsigned* iV, unsigned iN, unsigned index, unsigned n,  ARGS&&... args)
    {
      if( index >= iN )
      {
        cwLogError(kInvalidArgRC,"Too many index/count arguments were passed to mtx::slice().");
        return nullptr;
      }
      
      iV[index] = n;
      return _slice<T0,T1>(m,iV,iN,index+1,std::forward<ARGS>(args)...);
    }

    // This function is a wrapper around alloc(const struct mtx_str<T>& src, const unsigned* sIdxV, const unsigned* sCntV ).
    // The argument list should specify the values for sIdxV[0:dimN] and sCntV[0:dimN].
    // The count of arguments should therefore not exceed src.dimN*2.
    // The sCntV[] argument list may be truncated, or set to kInvalidCnt, if all values after the offset for a given dimension are to be copied.
    template< typename T0, typename T1, typename... ARGS>
      struct mtx_str<T0>* slice( const struct mtx_str<T1>& m, ARGS&&... args)
    {
      unsigned iV[ m.dimN*2 ];
      return _slice<T0,T1>(m, iV, m.dimN*2, 0, std::forward<ARGS>(args)...);
    }

    template<typename T>
      struct mtx_str<T>* _sliceAlias( const struct mtx_str<T>& m, unsigned* iV, unsigned iN, unsigned index )
    {
      // the offset index vector must be fully specified
      if( index < m.dimN )
      {
        cwLogError(kInvalidArgRC,"An invalid number index + count values was given to sliceAlias(). %i < %i.",index,m.dimN);
        return nullptr;
      }

      // fill in the end of iV[] with kInvalidCnt to indicate that all values after the offset should be copied
      for(; index<iN; ++index)
        iV[index] = kInvalidCnt;

      // allocate a matrix to hold the slice
      return sliceAlias<T>(m,iV,iV+m.dimN);
    }
    
    template< typename T, typename... ARGS>
      struct mtx_str<T>* _sliceAlias( const struct mtx_str<T>& m, unsigned* iV, unsigned iN, unsigned index, unsigned n,  ARGS&&... args)
    {
      if( index >= iN )
      {
        cwLogError(kInvalidArgRC,"Too many index/count arguments were passed to mtx::sliceAlias().");
        return nullptr;
      }
      
      iV[index] = n;
      return _sliceAlias<T>(m,iV,iN,index+1,std::forward<ARGS>(args)...);
    }
    
    template< typename T, typename... ARGS>
      struct mtx_str<T>* slice_alias( const struct mtx_str<T>& m, ARGS&&... args)
    {
      unsigned iV[ m.dimN*2 ];
      return _sliceAlias<T>(m,iV, m.dimN*2, 0, args...);
    }

    
    // resize m[] 
    template< typename T >
      struct mtx_str<T>* resize( struct mtx_str<T>* m, const unsigned* dimV, unsigned dimN, T* base=nullptr, unsigned flags=0 )
    { return _init<T>( m, dimN, dimV, base, flags ); }

    // resize y[] to have the same size as x[]
    template< typename T >
      struct mtx_str<T>* resize( struct mtx_str<T>* y, const struct mtx_str<T>& x )
    { return resize(y,x->dimV,x->dimN); }


    // Copy a slice of src[] into dst[] at a particular location.
    // dst[] is assumed to be allocated with sufficient size to receive src[].
    // Set dIdxV to nullptr to copy to the 0,0, ... of the dst matrix
    // Set sIdxV to nullptr to copy from the 0,0, ... of the src matrix.
    // Set sCntV to nullptr to cop all of the src matrix.
    template< typename T0, typename T1 >
      rc_t copy( struct mtx_str<T0>& dst, const unsigned* dIdxV, const struct mtx_str<T1>& src, const unsigned* sIdxV, const unsigned* sCntV )
    {
      rc_t rc = kOkRC;
      unsigned nV[ src.dimN ];
      unsigned siV[ src.dimN ];
      unsigned diV[ dst.dimN ];
      vop::zero(nV,src.dimN);

      if( sCntV == nullptr )
        sCntV = src.dimV;

      if( sIdxV == nullptr )
        vop::zero(siV,src.dimN);
      else
        vop::copy(siV,sIdxV,src.dimN);
      
      if( dIdxV == nullptr )
        vop::zero(diV,dst.dimN);
      else
        vop::copy(diV,dIdxV,dst.dimN);
      
#ifndef NDEBUG

      // verify the starting address
      assert( is_legal_address(dst, diV) );
      assert( is_legal_address(src, siV) );

      vop::add(siV,sCntV,src.dimN);
      vop::add(diV,sCntV,dst.dimN);

      vop::sub(siV,1,src.dimN);
      vop::sub(diV,1,dst.dimN);

      //verify the ending address
      assert( is_legal_address(dst, diV) );
      assert( is_legal_address(src, siV) );
      
      vop::copy(siV,sIdxV,src.dimN);
      vop::copy(diV,dIdxV,dst.dimN);
      
#endif
      
      // copy one element
      ele(dst, diV ) = ele(src, siV );

      
      for(int j=0; j >= 0; )
      {
        // increment the src and dst addr

        // from highest to lowest degree
        for(j=src.dimN-1; j>=0; --j)
        {
          // if incrementing the jth dim does not overflow ...
          if( ++nV[j] < sCntV[j] )
          {
            siV[j] += 1;
            diV[j] += 1;

            // copy one element
            ele(dst, diV ) = ele(src, siV );
            
            break;   // .. then incr siV[] and diV[] with the next src/dst address
          }

          // otherwise reset the counter and address for this dim and backup by one dim.
          nV[j] = 0;
          diV[j] = dIdxV[j];
          siV[j] = sIdxV[j];
        }
        
      }
      
      return rc;
    }

    
    template< typename T >
      rc_t _join_update_dims( unsigned index, unsigned* dimV, unsigned dimN, const struct mtx_str<T>& m  )
    {
      rc_t rc = kOkRC;
      // verify that the degree of all matrices are the same
      if( m.dimN != dimN )
        return cwLogError(kInvalidArgRC,"Join matrix size mismatch. dimN:%i != %i", m.dimN, dimN);

      // only the dimension specified by 'index' may be different
      for(unsigned i=0; i<dimN; ++i)
      {
        if( i == index )
          dimV[i] += m.dimV[i]; 
        else
        {
          if( dimV[i] != m.dimV[i] )
            return cwLogError(kInvalidArgRC,"Join matrix dimV[%i] mismatch: (%i != %i). ",i,dimV[i],m.dimV[i]);
        }
      }
      
      return rc;
    }

    template< typename T >
      void _join_copy( struct mtx_str<T>& dst, const struct mtx_str<T>& src, unsigned index, unsigned ii )
    {
      unsigned dIdxV[ dst.dimN ];
      unsigned sIdxV[ src.dimN ];
      vop::zero(dIdxV,dst.dimN);
      vop::zero(sIdxV,src.dimN);
      dIdxV[ index ] = ii;
      copy(dst,dIdxV,src,sIdxV,src.dimV);
    }
    

    template< typename T >
      struct mtx_str<T>* _join( unsigned index, unsigned* dimV, unsigned dimN, unsigned ii )
    {
      // Allocate an empty matrix to copy the joined matrices into.
      return alloc<T>(dimV,dimN);
    }

    
    template< typename T, typename... ARGS>
      struct mtx_str<T>* _join( unsigned index, unsigned* dimV, unsigned dimN, unsigned ii, const struct mtx_str<T>& m,  ARGS&&... args)
    {
      struct mtx_str<T>* y = nullptr;

      if( _join_update_dims<T>(index,dimV,dimN,m) != kOkRC )
        return nullptr;
      
      if((y =  _join<T>( index, dimV, dimN, ii + m.dimV[index], std::forward<ARGS>(args)...)) != nullptr )
      {
        _join_copy(*y,m,index,ii);
      }

      return y;
    }
    
    template< typename T, typename... ARGS>
      struct mtx_str<T>* join( unsigned index, const struct mtx_str<T>& m, ARGS&&... args)
    {
      
      struct mtx_str<T>* y = nullptr;
      unsigned dimV[ m.dimN ];

      for(unsigned i=0; i<m.dimN; ++i)        
        dimV[i] = m.dimV[i];

      if((y = _join( index, dimV, m.dimN, m.dimV[index], std::forward<ARGS>(args)...)) != nullptr )
      {
        _join_copy(*y,m,index,0);
      }

      return y;
    }

    
    template< typename T >
      bool is_legal_address( const struct mtx_str<T>& m, const unsigned* idxV )
    {
      for(unsigned i=0; i<m.dimN; ++i)
        if( idxV[i] >= m.dimV[i] )
          return false;
      
      return true;
    }

    template< typename T >
      unsigned offset( const struct mtx_str<T>& m, const unsigned* idxV )
    { return  vop::mac(idxV,m.mulV,m.dimN); }
    
    
    template< typename T >
      unsigned _offset( const struct mtx_str<T>& m, int i, unsigned offs )
    { return offs; }

    template< typename T, typename... ARGS>
      unsigned _offset( const struct mtx_str<T>& m, int i, unsigned offs, unsigned idx, ARGS&&... args)
    { return _offset(m,i+1, offs + idx*m.mulV[i], std::forward<ARGS>(args)...);   }
    
    template< typename T, typename... ARGS>
      unsigned offset( const struct mtx_str<T>& m, unsigned idx, ARGS&&... args)
    { return _offset(m,0,0,idx,std::forward<ARGS>(args)...); }

    
    template< typename T >
      T* addr( struct mtx_str<T>& m, const unsigned* idxV )
    { return m.base + offset(m,idxV); }

    template< typename T >
      const T* addr( const struct mtx_str<T>& m, const unsigned* idxV )
    { return m.base + offset(m,idxV); }
    
    
    template< typename T, typename... ARGS>
      T* addr( struct mtx_str<T>& m, unsigned i, ARGS&&... args)
    { return m.base + offset(m,i,std::forward<ARGS>(args)...); }


    template< typename T, typename... ARGS>
      const T* addr( const struct mtx_str<T>& m, unsigned i, ARGS&&... args)
    { return m.base + offset(m,i,std::forward<ARGS>(args)...); }
    



    
    template< typename T >
      T& ele( struct mtx_str<T>& m, const unsigned* idxV )
    { return *addr(m,idxV); }

    template< typename T >
      const T& ele( const struct mtx_str<T>& m, const unsigned* idxV )
    { return *addr(m,idxV); }
        
    template< typename T, typename... ARGS>
      T& ele( struct mtx_str<T>& m, unsigned i, ARGS&&... args)
    { return *addr(m,i,std::forward<ARGS>(args)...); }


    template< typename T, typename... ARGS>
      const T& ele( const struct mtx_str<T>& m, unsigned i, ARGS&&... args)
    { return *addr(m,i,std::forward<ARGS>(args)...); }
    
    
    template< typename T >
      bool is_col_vector( const struct mtx_str<T>& m )
    { return m.dimN==1 || (m.dimN==2 && m.dimV[1]==1); };

    template< typename T >
      bool is_row_vector( const struct mtx_str<T>& m )
    { return m.dimN==2 && m.dimV[0]==1; }

    template< typename T >
      bool is_vector( const struct mtx_str<T>& m )
    { return is_col_vector(m) || is_row_vector(m); }
      
    // Return 'true' if the matrices have the same size.
    template< typename T >
      bool is_size_equal( const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      if( x0.dimN != x1.dimN )
        return false;

      return vop::is_equal(x0.dimV,x1.dimV,x0.dimN);
    }
    
    template< typename T >
      bool is_equal( const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      if( !is_size_equal(x0,x1) )
        return false;

      return vop::is_equal(x0.base,x1.base,ele_count(x0));
    }

    
    // Return the count of elements in the matrix
    template< typename T >
      unsigned ele_count( const struct mtx_str<T>& x )
    { return vop::prod(x.dimV,x.dimN); }

    
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
        double v = ele( m, idxV );

        // print the value
        cwLogPrint("%*.*f ",colWidth,decPl,v);
      }
      else
      {
        for(unsigned j=0; j<m.dimV[i]; ++j)
        {
          if( m.dimN>=2 && i == m.dimN-2 )
          {
            // print the dimension index for matrices with 3+ dim's
            if( i > 0 && j == 0 )
              cwLogPrint("%i\n",idxV[i-1]);

            // print the row index for matrices with 2+ dim's
            if( m.dimN>1 )
              cwLogPrint("%i | ",j);
          }
          
          idxV[i] = j;
          _print(m, idxV, i+1, decPl, colWidth );
        }
        
        // prevent multiple newlines on last printed line
        if( m.dimN==1 || (m.dimN>=2 && i > m.dimN-2) )
          cwLogPrint("\n");
      }
    }
    
    template< typename T >
      void print( const struct mtx_str<T>& m, unsigned decPl=3, unsigned colWidth=10 )
    {
      unsigned idxV[ m.dimN ];
      memset(idxV,0,sizeof(idxV));

      if( std::numeric_limits<T>::is_integer )
        decPl = 0;
        
      _print( m, idxV, 0, decPl, colWidth );
    }
    
    template< typename T >
      void report( const struct mtx_str<T>& m, const char* label, unsigned decPl=3, unsigned colWidth=10 )
    {
      cwLogPrint("%s :",label);
      for(unsigned i=0; i<m.dimN; ++i)
        cwLogPrint("%i ", m.dimV[i] );
      cwLogPrint("\n");
      
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
      vop::mul(y.base,x.base,ele_count<T>(x));
    }

    // y = x * scalar (elementwise)
    template< typename T >
      void mult( struct mtx_str<T>& y, const struct mtx_str<T>& x, const T& scalar )
    {
      resize<T>(&y,x); // resize y to the same dim's as m
      vop::mul(y.base,x.base,ele_count<T>(x),scalar);
    }

    // y *= scalar (elementwise)
    template< typename T >
    void mult( struct mtx_str<T>& y, const T& scalar )
    { vop::mul(y.base,scalar,ele_count<T>(y)); }

    // y = m + x (elementwise)
    template< typename T >
      void add( struct mtx_str<T>& y, const struct mtx_str<T>& x0, const struct mtx_str<T>& x1 )
    {
      assert( is_size_equal(x0,x1) );
      resize<T>(&y,x0); // resize y to the same dim's as m
      vop::add(y.base,x0.base,x1.base,ele_count(x0));
    }

    // y += x (elementwise)
    template< typename T >
      void add( struct mtx_str<T>& y, const struct mtx_str<T>& x )
    {
      assert( is_size_equal(y,x) );
      vop::add(y.base,x.base,ele_count(x));      
    }

    // y = x + scalar (elementwise)
    template< typename T >
      void add( struct mtx_str<T>& y, const struct mtx_str<T>& x, const T& scalar )
    {
      resize(&y,x);
      vop::add(y.base,x.base,scalar,ele_count<T>(y));
    }

    // y += scalar (elementwise)
    template< typename T >
    void add( struct mtx_str<T>& y, const T& scalar )
    {
      vop::add(y.base,scalar,ele_count<T>(y));
    }


    template<typename T >
      const T max( const struct mtx_str<T>& x )
    {
      return vop::max(x.base,ele_count<T>(x));
    }

    template<typename T >
      const T min( const struct mtx_str<T>& x )
    {
      return vop::min(x.base,ele_count<T>(x));
    }

    template< typename T>
      struct mtx_str<T>*  alloc_one_hot( const struct mtx_str<T>& mV )
    {
      if( !is_vector(mV) )
      {
        cwLogError(kInvalidArgRC,"Only vectors can be converted to one-hot matrices.");
        return nullptr;
      }
      
      int min_val = (int)mtx::min<T>(mV);
      int max_val = (int)mtx::max<T>(mV);
      unsigned rN = (max_val - min_val) + 1;
      unsigned cN = ele_count<T>(mV);
      struct mtx::mtx_str<T>* zM = mtx::alloc<T>(kZeroFl,rN,cN);

      for(unsigned i=0; i<cN; ++i)
      {
        unsigned j = (unsigned)ele<T>(mV,i) - min_val;
        ele(*zM, j, i) = 1;
      }

      return zM;      
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

      //cwLogPrint("%i %i : %i %i\n",mrn,mcn,xrn,xcn);
      
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

    rc_t test( const test::test_args_t& args );
    
    
  }

}


#endif
