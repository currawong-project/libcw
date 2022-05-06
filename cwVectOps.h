#ifndef cwVectOps_h
#define cwVectOps_h


namespace cw
{
  namespace vop
  {
    //==================================================================================================================
    // Input / Output
    //

    template< typename T0 >
    void print( const T0* v0, unsigned n, const char* fmt, const char* label=nullptr, unsigned colN=0 )
    {
      bool newline_fl = false;
      
      if( label != nullptr )
      {
        printf("%s : ",label);
        if( colN && n > colN )
        {
          printf("\n");
          newline_fl = true;
        }
      }
      
      if( colN == 0 )
        colN = n;
      
      for(unsigned i=0; i<n; ++i)
      {
        printf(fmt,v0[i]);

        newline_fl = false;
        
        if( (n+1) % colN == 0 )
        {
          printf("\n");
          newline_fl = true;
        }
      }

      if( !newline_fl )
        printf("\n");
    }


    
    //==================================================================================================================
    // Move,fill,copy
    //
    template< typename T0, typename T1 >
      void copy( T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] = (T0)v1[i];
    }
    
    template< typename T0, typename T1 >
      void fill( T0* v, unsigned n, const T1& value=0 )
    {
      for(unsigned i=0; i<n; ++i)
        v[i] = value;
    }

    template< typename T >
      void zero( T* v, unsigned n )
    { fill(v,n,0); }


    //==================================================================================================================
    // Compare
    //
    template< typename T0, typename T1 >
      bool is_equal( const T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        if( v0[i] != v1[i] )
          return false;
      
      return true;
    }

    //==================================================================================================================
    // Min,max
    //
    template< typename T >
      unsigned arg_max( const T* v, unsigned n )
    {
      if( n == 0 )
        return kInvalidIdx;

      unsigned mi = 0;
      
      for(unsigned i=1; i<n; ++i)
        if( v[i] > v[mi])
          mi = i;
      
      return mi;
    }

    template< typename T >
      unsigned arg_min( const T* v, unsigned n )
    {
      if( n == 0 )
        return kInvalidIdx;

      unsigned mi = 0;
      
      for(unsigned i=1; i<n; ++i)
        if( v[i] < v[mi])
          mi = i;
      
      return mi;
    }

    
    template< typename T >
      const T max( const T* v, unsigned n )
    {
      unsigned mi;
      if((mi = arg_max(v,n)) == kInvalidIdx )
        return std::numeric_limits<T>::max();
      return v[mi];
    }

    template< typename T >
      const T min( const T* v, unsigned n )
    {
      unsigned mi;
      if((mi = arg_min(v,n)) == kInvalidIdx )
        return std::numeric_limits<T>::max();
      return v[mi];
    }
    
    template< typename T0, typename T1 >
      T0 mac( const T0* v0, const T1* v1, unsigned n )
    {
      T0 acc = 0;
      for(unsigned i=0; i<n; ++i)
        acc += v0[i] * v1[i];
      return acc;
    }

    
    //==================================================================================================================
    // Arithmetic
    //
    template< typename T0, typename T1 >
      void mul( T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] = v0[i] * (T1)v1[i];
    }

    template< typename T0, typename T1 >
      void mul( T0* y0, const T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] * v1[i];
    }

    
    template< typename T0, typename T1 >
      void mul( T0* v0, const T1& scalar, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] *= scalar;
    }

    template< typename T0, typename T1, typename T2 >
      void mul( T0* y0, const T1* v0, const T2& scalar, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] * scalar;
    }
    
    template< typename T0, typename T1 >
      void add( T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] += v1[i];
    }

    template< typename T0, typename T1 >
      void add( T0* y0, const T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] + v1[i];
    }

    template< typename T0, typename T1 >
      void add( T0* v0, const T1& scalar, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] += scalar;
    }

    template< typename T0, typename T1 >
      void add( T0* y0, const T0* v0, const T1& scalar, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] + scalar;
    }

    template< typename T0, typename T1 >
      void div( T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] /= v1[i];
    }

    template< typename T0, typename T1 >
      void div( T0* y0, const T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] / v1[i];
    }

    template< typename T0, typename T1 >
      void div( T0* v0, const T1& denom, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] /= denom;
    }

    template< typename T0, typename T1 >
      void div( T0* y0, const T0* v0, const T1& denom, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] / denom;
    }
    
    template< typename T0, typename T1 >
      void sub( T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] -= v1[i];
    }

    template< typename T0, typename T1 >
      void sub( T0* y0, const T0* v0, const T1* v1, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] / v1[i];
    }

    template< typename T0, typename T1 >
      void sub( T0* v0, const T1& scalar, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        v0[i] -= scalar;
    }

    template< typename T0, typename T1 >
      void sub( T0* y0, const T0* v0, const T1& scalar, unsigned n )
    {
      for(unsigned i=0; i<n; ++i)
        y0[i] = v0[i] / scalar;
    }

    //==================================================================================================================
    // Sequence generators
    //
    // Fill y[0:min(n,cnt)] with values {beg,beg+step,beg+2*step .... beg+(cnt-1)*step}}
    template< typename T >
      void seq( T* y, unsigned n, const T& beg, const T& cnt, const T& step )
    {
      if( cnt < n )
        n = cnt;

      T v = beg;
      for(unsigned i=0; i<n; ++i, v+=step)
        y[i] = v;
    }

    // Same as Matlab linspace() v[i] = i * (limit-1)/n
    template< typename T >
      T* linspace( T* y, unsigned yN, T base, T limit )
    {
      unsigned i = 0;
      for(; i<yN; ++i)
        y[i] = base + i*(limit-base)/(yN-1);
      return y;
    }

    // Fill y[0:dn] with [beg+0,beg+1, ... beg+dn]
    template< typename T >
      T seq( T* dbp, unsigned dn, const T& beg, const T& incr )
    {
      const T* dep = dbp + dn;
      unsigned i = 0;
      for(; dbp<dep; ++i)
        *dbp++ = beg + (incr*i);
      return beg + (incr*i);
    }

    template< typename T >
      T sum( const T* v, unsigned n )
    {
      T y = 0;
      for(unsigned i=0; i<n; ++i)
        y += v[i];

      return y;
    }

    template< typename T >
      T prod( const T* v, unsigned n )
    {
      T y = 1;
      for(unsigned i=0; i<n; ++i)
        y *= v[i];
      return y;
    }


    template< typename T0, typename T1 >
    T0 sum_sq_diff( const T0* v0, const T1* v1, unsigned n )
    {
      T0 sum = 0;
      for(unsigned i=0; i<n; ++i)
        sum += (v0[i]-v1[i]) * (v0[i]-v1[i]);

      return sum;
    }


    //==================================================================================================================
    // Statistics
    //
    template< typename T >
    T mean( const T* v, unsigned n )
    {
      if( n == 0 )
        return 0;

      return sum(v,n)/n;
    }
    
    //==================================================================================================================
    // Signal Processing
    //

    template< typename T >
      unsigned phasor( T* y, unsigned n, T srate, T hz, unsigned init_idx=0 )
    {
      for(unsigned i=init_idx; i<n; ++i)
        y[i] = (M_PI*2*hz*i) / srate;

      return init_idx + n;
    }

    template< typename T >
      unsigned sine( T* y, unsigned n, T srate, T hz, unsigned init_idx=0 )
    {
      init_idx = phasor(y,n,srate,hz,init_idx);

      for(unsigned i=0; i<n; ++i)
        y[i] = sin(y[i]);
      
      return init_idx;
    }

    template< typename T0, typename T1 >
    T0* ampl_to_db( T0* dbp, const T1* sbp, unsigned dn, T0 minDb=-1000 )
    {
      T0  minVal = pow(10.0,minDb/20.0);
      T0* dp     = dbp;
      T0* ep     = dp + dn;

      for(; dp<ep; ++dp,++sbp)
        *dp = (T0)(*sbp<minVal ? minDb : 20.0 * log10(*sbp));
      return dbp;
      
    }

    template< typename T >
    T* db_to_ampl( T* dbp, const T* sbp, unsigned dn, T minDb=-1000 )
    {
      T* dp = dbp;
      T* ep = dp + dn;
      for(; dp<ep; ++dp,++sbp)
        *dp = pow(10.0,*sbp/20.0);
      return dbp;
      
    }


    template< typename T >
    T rms( const T* x, unsigned xN )
    {
      T rms = 0;
      if( xN > 0 )
      {
        T x0[ xN ];
        mul(x0,x,x,xN);
        rms = std::sqrt(sum(x0,xN)/(T)xN);
        
      }
      return rms;  
        
    }
    

    
  }
}


#endif
