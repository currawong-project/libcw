#ifndef cwDsp_H
#define cwDsp_H

#ifdef cwFFTW
#include <fftw3.h>
#else
#include "cwFFT.h"
#endif

#include <type_traits>

namespace cw
{
  namespace dsp
  {
    typedef std::complex<double> complex_d_t;
    typedef std::complex<float>  complex_f_t;

    //---------------------------------------------------------------------------------------------------------------------------------
    // Window functions
    //

    template< typename T >
      T kaiser_beta_from_sidelobe_reject(  const T& sidelobeRejectDb )
    { 
      double beta;
      T slrDb = sidelobeRejectDb;
      
      if( slrDb < 13.26 )
        slrDb = 13.26;
      else
        if( slrDb > 120.0)
          slrDb = 120.0; 

      if( slrDb < 60.0 )
        beta = (0.76609 * pow(slrDb - 13.26,0.4))  + (0.09834*(slrDb-13.26));
      else
        beta = 0.12438 * (slrDb + 6.3);
    
      return beta;
    }

    
    template< typename T >
      T kaiser_freq_resolution_factor( const T& sidelobeRejectDb )
    {	return (6.0 * (sidelobeRejectDb + 12.0))/155.0;	}

    template< typename T >      
      T* kaiser( T* dbp, unsigned n, const T& beta )
    {
      bool	   zeroFl 	= false;
      int		   M		    = 0;
      double	 den		  = math::bessel0<T>(beta);	// wnd func denominator
      int		   cnt		  = n;
      int      i;

      assert( n >= 3 );

      // force ele cnt to be odd 
      if( is_even(cnt)  )
      {
        cnt--;
        zeroFl = true;
      }

      // at this point cnt is odd and >= 3
		
      // calc half the window length
      M = (int)((cnt - 1.0)/2.0);
		
      double Msqrd = M*M;
		
      for(i=0; i<cnt; i++)
      {		
        double v0 = (double)(i - M);

        double num = math::bessel0(beta * sqrt(1.0 - ((v0*v0)/Msqrd)));
			
        dbp[i] = (T)(num/den);
      }								 


      if( zeroFl )
        dbp[cnt] = 0.0;  // zero the extra element in the output array

      return dbp;
    }

    template< typename T >      
      T*	gaussian( T* dbp, unsigned dn, double mean, double variance )
    {
  
      int      M		    =  dn-1;
      double   sqrt2pi	= sqrt(2.0*M_PI);
      unsigned i;

      for(i=0; i<dn; i++)
      {
        double arg = ((((double)i/M) - 0.5) * M);

        arg = pow( (double)(arg-mean), 2.0);

        arg = exp( -arg / (2.0*variance));

        dbp[i] = (T)(arg / (sqrt(variance) * sqrt2pi));
      }
		
      return dbp;
    }


    template< typename T >      
      T* hamming( T* dbp, unsigned dn )
    {
      const T* dep = dbp + dn;
      T* dp   = dbp;
      double        fact = 2.0 * M_PI / (dn-1);
      unsigned      i;

      for(i=0;  dbp < dep; ++i )
        *dbp++  = (T)(.54 - (.46 * cos(fact*i)));

      return dp;
    }

    template< typename T >      
      T* hann( T* dbp, unsigned dn )
    {
      const T* dep = dbp + dn;
      T* dp   = dbp;
      double        fact = 2.0 * M_PI / (dn-1);
      unsigned      i;

      for(i=0;  dbp < dep; ++i )
        *dbp++  = (T)(.5 - (.5 * cos(fact*i)));

      return dp;
    }

    template< typename T >      
      T* hann_matlab( T* dbp, unsigned dn )
    {
      const T* dep = dbp + dn;
      T* dp   = dbp;
      double        fact = 2.0 * M_PI / (dn+1);
      unsigned      i;

      for(i=0;  dbp < dep; ++i )
        *dbp++  = (T)(0.5*(1.0-cos(fact*(i+1))));

      return dp;
    }



    template< typename T >      
      T* triangle( T* dbp, unsigned dn )
    {
      unsigned     n    = dn/2;
      T incr = 1.0/n;

      T v0 = 0;
      T v1 = 1;
      vop::seq(dbp,n,v0,incr);

      vop::seq(dbp+n,dn-n,v1,-incr);

      return dbp;
    }

    template< typename T >      
      T*	gauss_window( T* dbp, unsigned dn, double arg )	
    {
      const T* dep = dbp + dn;
      T* rp = dbp;
      int           N  = (dep - dbp) - 1;
      int           n  = -N/2;
  
      if( N == 0 )
        *dbp = 1.0;
      else
      {
        while( dbp < dep )
        {
          double a = (arg * n++) / (N/2);

          *dbp++ = (T)exp( -(a*a)/2 );
        }
      }
      return rp;
    }

    //---------------------------------------------------------------------------------------------------------------------------------
    // FFT
    //
    namespace fft
    {

      
      unsigned window_sample_count_to_bin_count( unsigned wndSmpN );
      unsigned bin_count_to_window_sample_count( unsigned binN );
      
      enum
      {
        kToPolarFl = 0x01,  // convert to polar (magn./phase)
        kToRectFl  = 0x02,  // convert to rect (real/imag)
      };
      
      template< typename T >
        struct obj_str
      {
        unsigned         flags;
        
        T*               inV;
        std::complex<T>* cplxV;
        
        T*               magV;
        T*               phsV;
        
        unsigned         inN;
        unsigned         binN;
        
        union
        {
          fftw_plan  dplan;
          fftwf_plan fplan;
        } u;
        
      };

      template< typename T >
      rc_t create( struct obj_str<T>*& p, unsigned xN, unsigned flags=kToPolarFl )
      {
        p = mem::allocZ< obj_str<T> >(1);
        
        p->flags = flags;
        p->inN   = xN;
        p->binN  = window_sample_count_to_bin_count(xN); 
        p->magV  = mem::allocZ<T>(p->binN);
        p->phsV  = mem::allocZ<T>(p->binN);  

        if( std::is_same<T,float>::value )
        {
          p->inV      = (T*)fftwf_malloc( sizeof(T)*xN );
          p->cplxV    = (std::complex<T>*)fftwf_malloc( sizeof(std::complex<T>)*xN);
          p->u.fplan  = fftwf_plan_dft_r2c_1d((int)xN, (float*)p->inV, reinterpret_cast<fftwf_complex*>(p->cplxV), FFTW_MEASURE );
          
        }
        else
        {
          p->inV     = (T*)fftw_malloc( sizeof(T)*xN );
          p->cplxV   = (std::complex<T>*)fftw_malloc( sizeof(std::complex<T>)*xN);
          p->u.dplan = fftw_plan_dft_r2c_1d((int)xN, (double*)p->inV, reinterpret_cast<fftw_complex*>(p->cplxV), FFTW_MEASURE );          
        }

        return kOkRC;        
      }

      template< typename T >
        rc_t destroy( struct obj_str<T>*& p )
      {
        if( p == nullptr )
          return kOkRC;

        if( std::is_same<T,float>::value )
        {
          fftwf_destroy_plan( p->u.fplan );
          fftwf_free(p->inV);
          fftwf_free(p->cplxV);
        }
        else
        {
          fftw_destroy_plan( p->u.dplan );
          fftw_free(p->inV);
          fftw_free(p->cplxV);
        }

        //p->u.dplan = nullptr;
        mem::release(p->magV);
        mem::release(p->phsV);
        mem::release(p);
        
        return kOkRC;
      }
      
      
      template< typename T >
        rc_t exec( struct obj_str<T>* p, const T* xV, unsigned xN )
      {
        rc_t   rc = kOkRC;

        assert( xN <= p->inN);

        // if the incoming vector size is less than the FT buffer size
        // then zero the extra values at the end of the buffer
        if( xN < p->inN )
          memset(p->inV + xN, 0, sizeof(T) * (p->inN-xN) );

        // copy the incoming samples into the input buffer
        memcpy(p->inV,xV,sizeof(T)*xN);

        // execute the FT
        if( std::is_same<T,float>::value )
          fftwf_execute(p->u.fplan);
        else
          fftw_execute(p->u.dplan);

        // convert to polar
        if( cwIsFlag(p->flags,kToPolarFl) )
        {
          for(unsigned i=0; i<p->binN; ++i)
          {
            p->magV[i] = std::abs(p->cplxV[i])/(p->inN/2);
            p->phsV[i] = std::arg(p->cplxV[i]);
          }
        }
        else
          // convert to rect
          if( cwIsFlag(p->flags,kToRectFl) )
          {
            for(unsigned i=0; i<p->binN; ++i)
            {
              p->magV[i] = std::real(p->cplxV[i]);
              p->phsV[i] = std::imag(p->cplxV[i]);
            }
          }
          else
          {
            // do nothing - leave the result in p->cplxV[]
          }
  
        return rc;        
      }

      template< typename T >
        unsigned bin_count( obj_str<T>* p ) { return p->binN; }
      
      template< typename T >
        const T* magn( obj_str<T>* p ) { return p->magV; }
      
      template< typename T >
        const T* phase( obj_str<T>* p ) { return p->phsV; }
      
      rc_t test();      
    }

    //---------------------------------------------------------------------------------------------------------------------------------
    // IFFT
    //
    namespace ifft
    {

      template< typename T >
        struct obj_str
      {
        T               *outV;        
        std::complex<T> *cplxV;
        
        unsigned         outN;
        unsigned         binN;

        union
        {
          fftw_plan  dplan;
          fftwf_plan fplan;
        } u;
      };
      
      template< typename T >
      rc_t create( struct obj_str<T>*& p, unsigned binN )
      {
        p = mem::allocZ< obj_str<T> >(1);
        
        p->binN  = binN;
        p->outN  = fft::bin_count_to_window_sample_count(binN); 

        if( std::is_same<T,float>::value )
        {
          p->outV  = (T*)fftwf_malloc( sizeof(T)*p->outN );
          p->cplxV = (std::complex<T>*)fftwf_malloc( sizeof(std::complex<T>)*p->outN);  
          p->u.fplan  = fftwf_plan_dft_c2r_1d((int)p->outN, reinterpret_cast<fftwf_complex*>(p->cplxV), (float*)p->outV, FFTW_BACKWARD | FFTW_MEASURE );          
        }
        else
        {
          p->outV  = (T*)fftw_malloc( sizeof(T)*p->outN );
          p->cplxV = (std::complex<T>*)fftw_malloc( sizeof(std::complex<T>)*p->outN);  
          p->u.dplan  = fftw_plan_dft_c2r_1d((int)p->outN, reinterpret_cast<fftw_complex*>(p->cplxV), (double*)p->outV, FFTW_BACKWARD | FFTW_MEASURE );          
        }

        return kOkRC;;
      }

      template< typename T >
        rc_t destroy( struct obj_str<T>*& p )
      {
        if( p == nullptr )
          return kOkRC;

        if( std::is_same<T,float>::value )
        {
          fftwf_destroy_plan( p->u.fplan );
          fftwf_free(p->outV);
          fftwf_free(p->cplxV);
        }
        else
        {
          fftw_destroy_plan( p->u.dplan );
          fftw_free(p->outV);
          fftw_free(p->cplxV);
        }

        //p->u.dplan = nullptr;
        mem::release(p);
        
        return kOkRC;
      }

      
      template< typename T >
        rc_t exec_polar( struct obj_str<T>* p, const T* magV, const T* phsV )
      {
        rc_t    rc    = kOkRC;

        if( magV != nullptr && phsV != nullptr )
        {
          for(unsigned i=0; i<p->binN; ++i)
            p->cplxV[i] = std::polar( magV[i] / 2, phsV[i] );
        
          for(unsigned i=p->outN-1,j=1; j<p->binN-1; --i,++j)
            p->cplxV[i] = std::polar( magV[j] / 2, phsV[j] ); 
        }
        
        if( std::is_same<T,float>::value )
          fftwf_execute(p->u.fplan);
        else
          fftw_execute(p->u.dplan);
        
        return rc;
      }

      template< typename T >
        rc_t exec_rect( struct obj_str<T>* p, const T* iV, const T* rV )
      {
        rc_t rc = kOkRC;

        if( iV != nullptr && rV != nullptr )
        {
          unsigned i,j;
          
          for(i=0; i<p->binN; ++i)
            p->cplxV[i] = std::complex(rV[i], iV[i]);

          for(i=p->outN-1,j=1; j<p->binN-1; --i,++j)
            p->cplxV[i] = std::complex(rV[j], iV[j]);
          
          if( std::is_same<T,float>::value )
            fftwf_execute(p->u.fplan);
          else
            fftw_execute(p->u.dplan);          
        }
        
        
        return rc;
      }
      
      template< typename T >
        unsigned out_count( struct obj_str<T>* p ) { return p->outN; }

      template< typename T >
        const T* out( struct obj_str<T>* p ) { return p->outV; }
      
      
      rc_t test();         
    }

    //---------------------------------------------------------------------------------------------------------------------------------
    // Convolution
    //
    namespace convolve
    {

      template< typename T >
        struct obj_str
      {
        struct fft::obj_str<T>*  ft;
        struct ifft::obj_str<T>* ift;

        std::complex<T>*   hV;
        unsigned           hN;
        
        T*                 olaV; // olaV[olaN]
        unsigned           olaN; // olaN == cN - procSmpN
        T*                 outV; // outV[procSmpN]
        unsigned           outN; // outN == procSmpN
      };

      template< typename T >
      rc_t create(struct obj_str<T>*& p, const T* hV, unsigned hN, unsigned procSmpN, T hScale=1 )
      {
        p = mem::allocZ<struct obj_str<T>>(1);

        unsigned cN   = math::nextPowerOfTwo( hN + procSmpN - 1 );
        
        fft::create<T>(p->ft,cN,0);

        unsigned binN = fft::bin_count( p->ft );

        ifft::create<T>(p->ift, binN);
        
        p->hN   = hN;
        p->hV   = mem::allocZ< std::complex<T> >(binN);
        p->outV = mem::allocZ<T>( cN );
        p->outN = procSmpN;
        p->olaV = p->outV + procSmpN;  // olaV[] overlaps outV[] with an offset of procSmpN
        p->olaN = cN - procSmpN;
       
        fft::exec( p->ft, hV, hN );

        for(unsigned i=0; i<binN; ++i)
          p->hV[i] = hScale * p->ft->cplxV[i] / ((T)cN);

        printf("procN:%i cN:%i hN:%i binN:%i outN:%i\n", procSmpN, cN, hN, binN, p->outN );
        
        return kOkRC;
      }

      template< typename T >
        rc_t destroy( struct obj_str<T>*& pRef )
      {
        if( pRef == nullptr )
          return kOkRC;

        fft::destroy(pRef->ft);
        ifft::destroy(pRef->ift);
        mem::release(pRef->hV);
        mem::release(pRef->outV);
        mem::release(pRef);
        return kOkRC;
      }

      template< typename T >
        rc_t exec( struct obj_str<T>* p, const T* xV, unsigned xN )
      {
        // take FT of input signal
        fft::exec(  p->ft, xV, xN );

        // multiply the signal spectra of the input signal and impulse response
        for(unsigned i=0; i<p->ft->binN; ++i)
          p->ift->cplxV[i] = p->hV[i] * p->ft->cplxV[i];

        // take the IFFT of the convolved spectrum
        ifft::exec_polar<T>(p->ift,nullptr,nullptr);

        // sum with previous impulse response tail
        vop::add( p->outV,  (const T*)p->olaV, (const T*)p->ift->outV, p->outN-1 );

        // first sample of the impulse response tail is complete 
        p->outV[p->outN-1] = p->ift->outV[p->outN-1];

        // store the new impulse response tail
        vop::copy(p->olaV, p->ift->outV + p->outN, p->hN-1 );

        return kOkRC;
      }

      template< typename T >
        rc_t apply( const T* xV, unsigned xN, const T* hV, unsigned hN, T* yV, unsigned yN, T hScale=1 )
      {
        unsigned    procSmpN = std::min(xN,hN);
        obj_str<T> *p        = nullptr;
        unsigned    yi       = 0;

        create(p,hV,hN,procSmpN,hScale);
        
        //printf("procSmpN:%i\n",procSmpN);
        
        for(unsigned xi=0; xi<xN && yi<yN; xi+=procSmpN )
        {
          exec<T>(p,xV+xi,std::min(procSmpN,xN-xi));
          
          unsigned outN = std::min(yN-yi,p->outN);
          vop::copy(yV+yi, p->outV, outN );


          //printf("xi:%i yi:%i outN:%i\n", xi, yi, outN );
          //vop::print( yV+yi, outN, "%f ", "outV ");
          
          yi += outN;
        }

        //printf("yi:%i\n",yi);

        /*
        // if the tail of the hV[] is still in the OLA buffer
        if( yi < yN )
        {
          
          unsigned outN = std::min(yN-yi, p->olaN);

          // fill yV[] with as much of OLA as is available
          vop::copy(yV + yi, p->olaV, outN);
          yi += outN;

          // zero any remaining space in yV[]
          vop::zero(yV + yi, yN-yi );
        }
        */
        
        destroy(p);
        
        return kOkRC;
      }
      
      rc_t test();
        
      
    }    
    
  }
}


#endif
