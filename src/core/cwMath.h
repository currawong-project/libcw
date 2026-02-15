//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwMath_h
#define cwMath_h

namespace cw
{
  namespace math
  {

    double   x80ToDouble( unsigned char s[10] );
    void     doubleToX80( double v, unsigned char s[10] );

    bool     isPowerOfTwo(   unsigned i );
    unsigned nextPowerOfTwo( unsigned i );
    unsigned nearPowerOfTwo( unsigned i );

    bool     isOddU(    unsigned v );
    bool     isEvenU(   unsigned v );
    unsigned nextOddU(  unsigned v );
    unsigned prevOddU(  unsigned v );
    unsigned nextEvenU( unsigned v );
    unsigned prevEvenU( unsigned v );

    /// Increment or decrement 'idx' by 'delta' always wrapping the result into the range
    /// 0 to (maxN-1).
    /// 'idx': initial value 
    /// 'delta':  incremental amount
    /// 'maxN' - 1 : maximum return value.
    unsigned modIncr(int idx, int delta, int maxN );

    // modified bessel function of first kind, order 0
    // ref: orfandis appendix B io.m
    template< typename T >
    T	bessel0( T x )
    {
      T eps = pow(10.0,-9.0);
      T n = 1.0;
      T S = 1.0;
      T D = 1.0;

      while(D > eps*S)
      {
        T t = x /(2.0*n);
        n = n+1;
        D = D * pow(t,2.0);
        S = S + D;
      }

      return S;

    }

    //=================================================================
    // The following elliptic-related function approximations come from
    // Parks & Burrus, Digital Filter Design, Appendix program 9, pp. 317-326
    // which in turn draws directly on other sources

    // Calculate complete elliptic integral (quarter period) K
    // given *complimentary* modulus kc.
    template< typename T >
    T ellipK( T kc )
    {
      T a = 1, b = kc, c = 1, tmp;

      while( c > std::numeric_limits<T>::epsilon )
      {
        c = 0.5*(a-b);
        tmp = 0.5*(a+b);
        b = sqrt(a*b);
        a = tmp;
      }

      return M_PI/(2*a);
    }

    // Calculate elliptic modulus k
    // given ratio of complete elliptic integrals r = K/K'
    // (solves the "degree equation" for fixed N = K*K1'/K'K1)
    template< typename T >
    T ellipDeg( T r )
    {
      T q,a,b,c,d;
      a = b = c = 1;
      d = q = exp(-M_PI*r);

      while( c > std::numeric_limits<T>::epsilon )
      {
        a = a + 2*c*d;
        c = c*d*d;
        b = b + c;
        d = d*q;
      }

      return 4*sqrt(q)*pow(b/a,2);
    }

    // calculate arc elliptic tangent u (elliptic integral of the 1st kind)
    // given argument x = sc(u,k) and *complimentary* modulus kc
    template< typename T >
    T ellipArcSc( T x, T kc )
    {
      T a = 1, b = kc, y = 1/x, tmp;
      unsigned L = 0;

      while( true )
      {
        tmp = a*b;
        a += b;
        b = 2*sqrt(tmp);
        y -= tmp/y;
        if( y == 0 )
          y = sqrt(tmp) * 1E-10;
        if( fabs(a-b)/a < std::numeric_limits<T>::epsilon )
          break;
        L *= 2;
        if( y < 0 )
          L++;
      }

      if( y < 0 )
        L++;

      return (atan(a/y) + M_PI*L)/a;
    }

    // calculate Jacobi elliptic functions sn, cn, and dn
    // given argument u and *complimentary* modulus kc
    template< typename T >
    rc_t ellipJ( T u, T kc, T* sn, T* cn, T* dn )
    {
      assert( sn != NULL || cn != NULL || dn != NULL );

      if( u == 0 )
      {
        if( sn != NULL ) *sn = 0;
        if( cn != NULL ) *cn = 1;
        if( dn != NULL ) *dn = 1;
        return kOkRC;
      }

      int i;
      T a,b,c,d,e,tmp,_sn,_cn,_dn;
      T aa[16], bb[16];

      a = 1;
      b = kc;

      for( i = 0; i < 16; i++ )
      {
        aa[i] = a;
        bb[i] = b;
        tmp = (a+b)/2;
        b = sqrt(a*b);
        a = tmp;
        if( (a-b)/a < std::numeric_limits<T>::epsilon )
          break;
      }

      c = a/tan(u*a);
      d = 1;

      for( ; i >= 0; i-- )
      {
        e = c*c/a;
        c = c*d;
        a = aa[i];
        d = (e + bb[i]) / (e+a);
      }

      _sn = 1/sqrt(1+c*c);
      _cn = _sn*c;
      _dn = d;

      if( sn != NULL ) *sn = _sn;
      if( cn != NULL ) *cn = _cn;
      if( dn != NULL ) *dn = _dn;

      return kOkRC;
    }

    //=================================================================
    // bilinear transform
    // z = (2*sr + s)/(2*sr - s)
    template< typename T >
    rc_t blt( unsigned n, T sr, T* rp, T* ip )
    {
      unsigned i;
      T a = 2*sr,
        tr, ti, td;

      for( i = 0; i < n; i++ )
      {
        tr = rp[i];
        ti = ip[i];
        td = pow(a-tr, 2) + ti*ti;
        rp[i] = (a*a - tr*tr - ti*ti)/td;
        ip[i] = 2*a*ti/td;
        if( tr < -1E15 )
          rp[i] = 0;
        if( fabs(ti) > 1E15 )
          ip[i] = 0;
      }

      return kOkRC;
    }
    



    //=================================================================
    // Floating point byte swapping
    unsigned           ffSwapFloatToUInt( float v );
    float              ffSwapUIntToFloat( unsigned v );
    unsigned long long ffSwapDoubleToULLong( double v );
    double             ffSwapULLongToDouble( unsigned long long v );

    //=================================================================
    template< typename T >
    T rand_range(T min, T max )
    {
      assert( min <= max );
      T range = max - min;
      return min + std::max(0,std::min(range,(T)range * rand() / RAND_MAX));
      
    }
    
    int      randInt( int min, int max );
    unsigned randUInt( unsigned min, unsigned max );
    float    randFloat( float min, float max );
    double   randDouble( double min, double max );

    //=================================================================
    bool isCloseD( double   x0, double   x1, double eps );
    bool isCloseF( float    x0, float    x1, double eps );
    bool isCloseI( int      x0, int      x1, double eps );
    bool isCloseU( unsigned x0, unsigned x1, double eps );

    //=================================================================
    // Run a length 'lfsrN' linear feedback shift register (LFSR) for 'yN' iterations to
    // produce a length 'yN' bit string in yV[yN].
    // 'lfsrN' count of bits in the shift register range: 2<= lfsrN <= 32.
    // 'tapMask' is a bit mask which gives the tap indexes positions for the LFSR. 
    // The least significant bit corresponds to the maximum delay tap position.  
    // The min tap position is therefore denoted by the tap mask bit location 1 << (lfsrN-1).
    // A minimum of two taps must exist.
    // 'seed' sets the initial delay state.
    // 'yV[yN]' is the the output vector
    // 'yN' is count of elements in yV.
    void   lFSR( unsigned lfsrN, unsigned tapMask, unsigned seed, unsigned* yV, unsigned yN );

    // Example and test code for lFSR() 
    bool lFSR_Test();


    // Generate a set of 'goldN' Gold codes using the Maximum Length Sequences (MLS) generated
    // by a length 'lfsrN' linear feedback shift register.
    // 'err' is an error object to be set if the the function fails.
    // 'lfsrN' is the length of the Linear Feedback Shift Registers (LFSR) used to generate the MLS.
    // 'poly_coeff0' tap mask for the first LFSR.
    // 'coeff1' tap mask the the second LFSR.
    // 'goldN' is the count of Gold codes to generate. 
    // 'yM[mlsN', goldN] is a column major output matrix where each column contains a Gold code.
    // 'mlsN' is the length of the maximum length sequence for each Gold code which can be
    // calculated as mlsN = (1 << a->lfsrN) - 1.
    // Note that values of 'lfsrN' and the 'poly_coeffx' must be carefully selected such that
    // they will produce a MLS.  For example to generate a MLS with length 31 set 'lfsrN' to 5 and
    // then select poly_coeff from two different elements of the set {0x12 0x14 0x17 0x1B 0x1D 0x1E}.
    // See http://www.ece.u.edu/~koopman/lfsr/index.html for a complete set of MSL polynomial
    // coefficients for given LFSR lengths.
    // Returns false if insufficient balanced pairs exist.
    bool   genGoldCodes( unsigned lfsrN, unsigned poly_coeff0, unsigned poly_coeff1, unsigned goldN, int* yM, unsigned mlsN  );

    
    
  }    
}


#endif
