//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwAudioTransforms_h
#define cwAudioTransforms_h


namespace cw
{
  namespace dsp
  {

    
    //---------------------------------------------------------------------------------------------------------------------------------
    // Window Function
    //
    namespace wnd_func
    {

      enum
      {
        kInvalidWndId   = 0x000,
        kHannWndId      = 0x001,
        kHammingWndId   = 0x002,
        kTriangleWndId  = 0x004,
        kKaiserWndId    = 0x008,
        kHannMatlabWndId= 0x010,
        kUnityWndId     = 0x020,
  
        kWndIdMask       = 0x0ff,      

        kNormByLengthWndFl = 0x100,  // mult by 1/wndSmpCnt 
        kNormBySumWndFl    = 0x200,  // mult by wndSmpCnt/sum(wndV)
        kSlRejIsBetaWndFl  = 0x400   // kaiser beta arg. is being passed as kaiserSideLobeRejectDb arg. 
      };

      template< typename sample_t >
      struct obj_str
      {
        unsigned  wndTypeId;        //
        unsigned  flags;            //
        unsigned  maxWndN;          //
        sample_t* wndV;             // wndV[ wndN ]
        unsigned  wndN;             // length of wndV[] and outV[]
        sample_t* outV;             // outV[ wndN ]
        double    kaiserSLRejectDb; // 
      };

      typedef struct obj_str<float>  fobj_t;
      typedef struct obj_str<double> dobj_t;

      const char* wndIdToLabel( unsigned id );            // window type to label
      unsigned    wndLabelToId( const char* label );      // window type label to id

      template< typename sample_t >
      rc_t _apply_window(  struct obj_str<sample_t>*& p )
      {
        rc_t rc = kOkRC;
        
        switch( p->wndTypeId )
        {
          case kHannWndId:
            hann<sample_t>(p->wndV,p->wndN);
            break;
            
          case kHammingWndId:
            hamming<sample_t>(p->wndV,p->wndN);
            break;
            
          case kTriangleWndId:
            triangle<sample_t>(p->wndV,p->wndN);
            break;
            
          case kKaiserWndId:
            {
              sample_t beta =  (p->flags & kSlRejIsBetaWndFl) ? p->kaiserSLRejectDb : kaiser_beta_from_sidelobe_reject<sample_t>(p->kaiserSLRejectDb);
              kaiser<sample_t>(p->wndV,p->wndN, beta);
            } 
            break;
            
          case kHannMatlabWndId:
            hann_matlab<sample_t>(p->wndV, p->wndN);
            break;
            
          case kUnityWndId:
            vop::fill<sample_t,sample_t>(p->wndV,p->wndN,1);
            break;
            
          default:
            rc = cwLogError(kInvalidArgRC,"The window id '%i' (0x%x) is not valid.", p->wndTypeId, p->wndTypeId );
        }

        sample_t den = 0;
        sample_t num = 1;
        if( cwIsFlag(p->flags,kNormBySumWndFl) )
        {
          den = vop::sum(p->wndV, p->wndN);
          num = p->wndN;
        }

        if( cwIsFlag(p->flags,kNormByLengthWndFl) )
          den += p->wndN;
    
        if( den > 0 )
        {
          vop::mul(p->wndV,num,p->wndN);
          vop::div(p->wndV,den,p->wndN);
        }
        
        return rc;
      }
      
      template< typename sample_t >
      rc_t create(  struct obj_str<sample_t>*& p, unsigned wndId, unsigned maxWndSmpCnt, unsigned wndSmpCnt, double kaiserSideLobeRejectDb )
      {
        rc_t  rc = kOkRC;

        p = mem::allocZ< struct obj_str< sample_t > >();
          
        p->wndV             = mem::allocZ<sample_t>(maxWndSmpCnt);
        p->outV             = mem::allocZ<sample_t>(maxWndSmpCnt);
        p->wndN             = wndSmpCnt;
        p->maxWndN          = maxWndSmpCnt;
        p->wndTypeId        = wndId & kWndIdMask;
        p->flags            = wndId & ~kWndIdMask;
        p->kaiserSLRejectDb = kaiserSideLobeRejectDb;

        rc = _apply_window(p);
        
        if( rc != kOkRC )
          destroy(p);

        return rc;
      }
      
      template< typename sample_t >
      rc_t destroy( struct obj_str<sample_t>*& p )
      {
        if( p != nullptr )
        {
          mem::release(p->outV);
          mem::release(p->wndV);
          mem::release(p);
        }
        return kOkRC;
      }

      template< typename sample_t >
      rc_t exec( struct obj_str<sample_t>* p, const sample_t* sigV, unsigned sigN, sample_t* outV=nullptr, unsigned outN=0 )
      {
        rc_t rc = kOkRC;

        if( outN > p->wndN )
          return cwLogError(kInvalidArgRC,"The signal size (%i) is greater than the window size (%i). ",outN,p->wndN);
        
        if( outV == nullptr )
        {
          outV = p->outV;
          outN = p->wndN;          
        }
        
        vop::mul( outV, p->wndV, sigV, outN );
        
        return rc;
      }

      template< typename sample_t >
      rc_t set_window_sample_count( struct obj_str<sample_t>* p, unsigned wndSmpCnt )
      {
        rc_t rc = kOkRC;
        
        if( wndSmpCnt > p->maxWndN )
          return cwLogError( kInvalidArgRC, "The window function sample count (%i) cannot be larger than the max window function sample count (%i).", wndSmpCnt, p->maxWndN );

        if( wndSmpCnt != p->wndN )
        {
        p->wndN = wndSmpCnt;

        if((rc = _apply_window(p)) == kOkRC )
        {
          // zero the end of the window buffer
          vop::zero(p->wndV + p->wndN, p->maxWndN - p->wndN );
        }
        }
        return rc;
      }

      
      rc_t test( const cw::object_t* args );

    }


    //---------------------------------------------------------------------------------------------------------------------------------
    // Overlap Add
    //
    namespace ola
    {
      template< typename sample_t >
      struct obj_str
      {
        wnd_func::obj_str<sample_t>* wf;         //
        unsigned                     wndSmpCnt;  // 
        unsigned                     hopSmpCnt;  //
        unsigned                     procSmpCnt; //
        sample_t*                    bufV;       // bufV[wndSmpCnt] overlap add buffer 
        sample_t*                    outV;       // outV[hopSmpCnt] output vector
        sample_t*                    outPtr;     // outPtr[procSmpCnt] output vector
        unsigned                     idx;        // idx of next val in bufV[] to be moved to outV[]        
      };

      typedef struct obj_str<float>  fobj_t;
      typedef struct obj_str<double> dobj_t;

      // hopSmpCnt must be <= wndSmpCnt.
      // hopSmpCnt must be an even multiple of procSmpCnt.
      // Call exec() at the spectral frame rate.
      // Call execOut() at the time domain audio frame rate.

      // Set wndTypeId to one of the cmWndFuncXXX enumerated widnow type id's.
      
      template< typename sample_t >
      rc_t create( struct obj_str<sample_t>*& p, unsigned wndSmpCnt, unsigned hopSmpCnt, unsigned procSmpCnt, unsigned wndTypeId )
      {
        rc_t rc = kOkRC;

        p = mem::allocZ< struct obj_str<sample_t> >();

        if((rc = wnd_func::create( p->wf, wndTypeId, wndSmpCnt, wndSmpCnt, 0)) != kOkRC ) 
          return rc;

        p->bufV   = mem::allocZ<sample_t>( wndSmpCnt );
        p->outV   = mem::allocZ<sample_t>( hopSmpCnt );
        p->outPtr = p->outV + hopSmpCnt;

        // hopSmpCnt must be an even multiple of procSmpCnt
        assert( hopSmpCnt % procSmpCnt == 0 );
        
        assert( wndSmpCnt >= hopSmpCnt );
        
        p->wndSmpCnt  = wndSmpCnt;
        p->hopSmpCnt  = hopSmpCnt;
        p->procSmpCnt = procSmpCnt;
        p->idx        = 0;
                
        return rc;
      }

      template< typename sample_t >
      rc_t destroy( struct obj_str<sample_t>*& p )
      {
        rc_t rc = kOkRC;
        if( p != nullptr )
        {
          wnd_func::destroy(p->wf);
        
          mem::release( p->bufV );
          mem::release( p->outV );
          mem::release( p );
        }
        return rc;
      }

      template< typename sample_t >
      rc_t exec( struct obj_str<sample_t>* p, const sample_t* sp, unsigned sN )
      {
        rc_t rc = kOkRC;

        assert( sN == p->wndSmpCnt );
        const sample_t* ep = sp + sN;
        const sample_t* wp = p->wf->wndV;
        int         i,j,k,n;

        // [Sum head of incoming samples with tail of ola buf]
        // fill outV with the bufV[idx:idx+hopSmpCnt] + sp[hopSmpCnt]
        for(i=0; i<(int)p->hopSmpCnt; ++i)
        {
          p->outV[i] = p->bufV[p->idx++] + (*sp++ * *wp++);

          if( p->idx == p->wndSmpCnt )
            p->idx = 0;
        }
  
        // [Sum middle of incoming samples with middle of ola buf]
        // sum next wndSmpCnt - hopSmpCnt samples of sp[] into bufV[]
        n = p->wndSmpCnt - (2*p->hopSmpCnt);
        k = p->idx;

        for(j=0; j<n; ++j)
        {
          p->bufV[k++] += (*sp++ * *wp++);
          if( k == (int)p->wndSmpCnt )
            k = 0;
        } 

        // [Assign tail of incoming to tail of ola buf]
        // assign ending samples from sp[] into bufV[]
        while( sp < ep )
        {
          p->bufV[k++] = (*sp++ * *wp++);

          if( k == (int)p->wndSmpCnt )
            k = 0;
        }

        p->outPtr = p->outV;
        
        return rc;
      }
      
      template< typename sample_t >
      const sample_t* execOut( struct obj_str<sample_t>* p )
      {
        const sample_t* sp = p->outPtr;
        if( sp >= p->outV + p->hopSmpCnt )
          return NULL;

        p->outPtr += p->procSmpCnt;

        return sp;
      }

      rc_t test( const cw::object_t* args );
      
    }

    //---------------------------------------------------------------------------------------------------------------------------------
    // Shift Buffer
    //
    namespace shift_buf
    {
      template< typename sample_t >
      struct obj_str
      {
        unsigned  bufSmpCnt;    // wndSmpCnt + hopSmpCnt
        sample_t* bufV;         // bufV[bufSmpCnt] all other pointers use this memory
        sample_t* outV;         // output window outV[ outN ]
        unsigned  outN;         // outN == wndSmpCnt
        unsigned  procSmpCnt;   // input sample count
        unsigned  maxWndSmpCnt; // maximum value for wndSmpCnt
        unsigned  wndSmpCnt;    // output sample count
        unsigned  hopSmpCnt;    // count of samples to shift the buffer by on each call to cmShiftExec()
        sample_t* inPtr;        // ptr to location in outV[] to recv next sample
        bool      fl;           // reflects the last value returned by cmShiftBufExec().
      };

      typedef obj_str<float>  fobj_t;
      typedef obj_str<double> dobj_t;

      template< typename sample_t >
      rc_t create( struct obj_str<sample_t>*& p, unsigned procSmpCnt, unsigned maxWndSmpCnt, unsigned wndSmpCnt, unsigned hopSmpCnt )
      {
        rc_t rc = kOkRC;
        
        p = mem::allocZ< struct obj_str<sample_t> >();

        p->maxWndSmpCnt = maxWndSmpCnt;
        if((rc = set_window_sample_count(p,wndSmpCnt )) != kOkRC )
          return rc;
        
        if( hopSmpCnt > wndSmpCnt )
          return cwLogError( kInvalidArgRC, "The shift buffer window sample count (%i) must be greater than or equal to the hop sample count (%i).", wndSmpCnt, hopSmpCnt );

        // The worst case storage requirement is where there are wndSmpCnt-1 samples in outV[] and procSmpCnt new samples arrive.
        p->bufSmpCnt    = maxWndSmpCnt + procSmpCnt;
        p->bufV         = mem::allocZ<sample_t>( p->bufSmpCnt );
        p->outV         = p->bufV;
        //p->outN         = wndSmpCnt;
        //p->maxWndSmpCnt = maxWndSmpCnt;
        //p->wndSmpCnt    = wndSmpCnt;
        p->procSmpCnt   = procSmpCnt;
        p->hopSmpCnt    = hopSmpCnt;
        p->inPtr        = p->outV;
        p->fl           = false;
        return rc;
      }

      template< typename sample_t >
      rc_t destroy( struct obj_str<sample_t>*& p )
      {
        if( p != nullptr )
        {
          mem::release(p->outV);
          mem::release(p);
        }
        return kOkRC;
      }

      // Returns true if there are 'wndSmpCnt' available samples at outV[] otherwise returns false.
      template< typename sample_t >
      bool exec( struct obj_str<sample_t>* p, const sample_t* sp, unsigned sn )
      {
        assert( sn <= p->procSmpCnt );

        // The active samples are in outV[wndSmpCnt]
        // Stored samples are between outV + wndSmpCnt and inPtr.

        // if the previous call to this function returned true then the buffer must be
        // shifted by hopSmpCnt samples - AND sp[] is ignored.
        if( p->fl )
        {
          // shift the output buffer to the left to remove expired samples 
          p->outV += p->hopSmpCnt;

          // if there are not  wndSmpCnt samples left in the buffer 
          if( p->inPtr - p->outV < p->wndSmpCnt )
          {
            // then copy the remaining active samples (between outV and inPtr) 
            // to the base of the physicalbuffer
            unsigned n = p->inPtr - p->outV;
            memmove( p->bufV, p->outV, n * sizeof(sample_t));

            p->inPtr  = p->bufV + n; // update the input and output positions
            p->outV   = p->bufV;
          }
        }
        else
        {
          // if the previous call to this function returned false then sp[sn] should not be ignored
          assert( p->inPtr + sn <= p->outV + p->bufSmpCnt );
          // copy the incoming samples into the buffer
          vop::copy(p->inPtr,sp,sn);
          p->inPtr += sn;
        }

        // if there are at least wndSmpCnt available samples in outV[]
        p->fl = p->inPtr - p->outV >= p->wndSmpCnt;
  
        return p->fl;        
      }

      template< typename sample_t >
      rc_t set_window_sample_count( struct obj_str<sample_t>* p, unsigned wndSmpCnt )
      {
        if( wndSmpCnt > p->maxWndSmpCnt )
          return cwLogError( kInvalidArgRC, "The shift buffer window sample count (%i) cannot be larger than the max window sample count (%i).", p->wndSmpCnt, p->maxWndSmpCnt );

        p->wndSmpCnt = wndSmpCnt;
        p->outN      = wndSmpCnt;

        return kOkRC;
      }

      rc_t test( const cw::object_t* args );
      
    }


    //---------------------------------------------------------------------------------------------------------------------------------
    // Phase to Frequency
    //
    namespace phs_to_frq
    {
      template< typename T >
      struct obj_str
      {
        T*       hzV;           // hzV[binCnt] output vector - frequency in Hertz 
        T*       phsV;          // phsV[binCnt] 
        T*       wV;            // bin freq in rads/hop
        double   srate;
        unsigned hopSmpCnt;
        unsigned binCnt;
      };

      typedef obj_str< float > fobj_t;
      typedef obj_str< double> dobj_t;
      
      template< typename T >
      rc_t create( struct obj_str<T>*& p, const T& srate, unsigned binCnt, unsigned hopSmpCnt )
      {
        rc_t rc = kOkRC;
        
        p = mem::allocZ< struct obj_str<T> >();

        p->hzV       = mem::allocZ<T>( binCnt );
        p->phsV      = mem::allocZ<T>( binCnt );  
        p->wV        = mem::allocZ<T>( binCnt );
        p->srate     = srate;
        p->binCnt    = binCnt;
        p->hopSmpCnt = hopSmpCnt;

        for(unsigned i=0; i<binCnt; ++i)
          p->wV[i] = M_PI * i * hopSmpCnt / (binCnt-1);
        
        
        return rc;
      }

      template< typename T >
      rc_t destroy( struct obj_str<T>*& p )
      {
        if( p != nullptr )
        {
          mem::release( p->hzV );
          mem::release( p->phsV );
          mem::release( p->wV );
          mem::release( p );
        }
        return kOkRC;
      }

      template< typename T >
      rc_t exec( struct obj_str<T>* p, const T* phsV )
      {
        rc_t     rc    = kOkRC;
        unsigned i;
        double   twoPi = 2.0 * M_PI;
        double   den   = twoPi * p->hopSmpCnt;

        for(i=0; i<p->binCnt; ++i)
        {
          T dPhs = phsV[i] - p->phsV[i];

          // unwrap phase - see phase_study.m for explanation
          T k = round( (p->wV[i] - dPhs) / twoPi);

          // convert phase change to Hz
          p->hzV[i] = (k * twoPi + dPhs) * p->srate /  den;
  
          // store phase for next iteration
          p->phsV[i] = phsV[i];

        }
  
        return rc;  
        
      }
      
    }

    //---------------------------------------------------------------------------------------------------------------------------------
    // Phase Vocoder (Analysis)
    //

    namespace pv_anl
    {

      enum 
      {
        kNoCalcHzPvaFl = 0x00,
        kCalcHzPvaFl   = 0x01,
      };
      
      template< typename T0, typename T1 >
      struct obj_str
      {
        struct shift_buf::obj_str<T0>*  sb;
        struct wnd_func::obj_str<T0>*   wf;
        struct fft::obj_str<T1>*        ft;
        struct phs_to_frq::obj_str<T1>* pf;
        
        unsigned               flags;
        unsigned               procSmpCnt;
        T1                     srate;
        
        unsigned               maxWndSmpCnt;
        unsigned               maxBinCnt;
        
        unsigned               wndSmpCnt;
        unsigned               hopSmpCnt;
        unsigned               binCnt;
        
        const T1*               magV; // amplitude NOT power - alias to ft->magV
        const T1*               phsV; //                       alias to ft->phsV
        const T1*               hzV;

      };

      typedef obj_str< float, float > fobj_t;
      typedef obj_str< double, double> dobj_t;
      
      template< typename T0, typename T1 >
      rc_t create( struct obj_str<T0,T1>*& p, unsigned procSmpCnt, const T1& srate, unsigned maxWndSmpCnt, unsigned wndSmpCnt, unsigned hopSmpCnt, unsigned flags )
      {
        rc_t rc = kOkRC;
        
        p = mem::allocZ< struct obj_str<T0,T1> >();

        shift_buf::create( p->sb, procSmpCnt, maxWndSmpCnt, wndSmpCnt, hopSmpCnt );
        wnd_func::create(  p->wf, wnd_func::kHannWndId  | wnd_func::kNormByLengthWndFl, maxWndSmpCnt, wndSmpCnt, 0 );
        fft::create(       p->ft, maxWndSmpCnt, fft::kToPolarFl);
        phs_to_frq::create(p->pf, srate, p->ft->binN, hopSmpCnt );

        p->flags      = flags;
        p->procSmpCnt = procSmpCnt;
        p->maxWndSmpCnt = maxWndSmpCnt;
        p->maxBinCnt    = fft::window_sample_count_to_bin_count(maxWndSmpCnt);
        p->wndSmpCnt  = wndSmpCnt;
        p->hopSmpCnt  = hopSmpCnt;
        p->binCnt     = p->ft->binN;

        p->magV       = p->ft->magV; 
        p->phsV       = p->ft->phsV;
        p->hzV        = p->pf->hzV;
        
        return rc;
      }

      template< typename T0, typename T1 >
      rc_t destroy( struct obj_str<T0,T1>*& p )
      {
        if( p != nullptr )
        {
          shift_buf::destroy( p->sb );
          wnd_func::destroy( p->wf );
          fft::destroy( p->ft );
          phs_to_frq::destroy( p->pf );
          mem::release( p );
        }
        return kOkRC;
      }

      template< typename T0, typename T1 >
      bool exec( struct obj_str<T0,T1>* p, const T0* x, unsigned xN )
      {
        bool fl = false;
        while( shift_buf::exec(p->sb,x,xN) )
        {
          wnd_func::exec(p->wf, p->sb->outV, p->sb->wndSmpCnt );

	  // convert float to double
	  T1 cvtV[ p->wf->wndN ];
	  vop::copy(cvtV, p->wf->outV, p->wf->wndN );

          fft::exec(p->ft, cvtV, p->wf->wndN);

          if( cwIsFlag(p->flags,kCalcHzPvaFl) )
            phs_to_frq::exec(p->pf,p->phsV);

          fl = true;
        }

        return fl;
        
      }

      template< typename T0, typename T1 >
      rc_t set_window_length( struct obj_str<T0,T1>* p, unsigned wndSmpCnt )
      {
        rc_t rc;
        
        if((rc = shift_buf::set_window_sample_count( p->sb, wndSmpCnt )) == kOkRC )
          rc = wnd_func::set_window_sample_count( p->wf, wndSmpCnt );
          
        return rc;
      }
    }

    //---------------------------------------------------------------------------------------------------------------------------------
    // Phase Vocoder (Synthesis)
    //
    
    namespace pv_syn
    {
      template< typename T0, typename T1 >
      struct obj_str
      {
        ifft::obj_str<T1>*     ft;
        wnd_func::obj_str<T0>* wf;
        ola::obj_str<T0>*      ola;
        
        T1*                    minRphV;
        T1*                    maxRphV;
        T1*                    itrV;
        T1*                    phs0V;
        T1*                    mag0V;
        T1*                    phsV;
        T1*                    magV;
        
        double                outSrate;
        unsigned              procSmpCnt;
        unsigned              wndSmpCnt;
        unsigned              hopSmpCnt;
        unsigned              binCnt;
        
      };

      typedef obj_str< float, float > fobj_t;
      typedef obj_str< double, double > dobj_t;

      template< typename T0, typename T1 >
      rc_t create( struct obj_str<T0,T1>*& p, unsigned procSmpCnt, const T1& outSrate, unsigned wndSmpCnt, unsigned hopSmpCnt, unsigned wndTypeId=wnd_func::kHannWndId )
      {
        rc_t rc = kOkRC;
        
        p = mem::allocZ< struct obj_str<T0,T1> >();

        int      k;
        double   twoPi     = 2.0 * M_PI;
        bool     useHannFl = true;
        int      m         = useHannFl ? 2 : 1;

        p->outSrate   = outSrate;
        p->procSmpCnt = procSmpCnt;
        p->wndSmpCnt  = wndSmpCnt;
        p->hopSmpCnt  = hopSmpCnt;
        p->binCnt     = wndSmpCnt / 2 + 1;

        p->minRphV    = mem::allocZ<T1>( p->binCnt );
        p->maxRphV    = mem::allocZ<T1>( p->binCnt );
        p->itrV       = mem::allocZ<T1>( p->binCnt );
        p->phs0V      = mem::allocZ<T1>( p->binCnt );
        p->phsV       = mem::allocZ<T1>( p->binCnt );
        p->mag0V      = mem::allocZ<T1>( p->binCnt );
        p->magV       = mem::allocZ<T1>( p->binCnt );


        wnd_func::create( p->wf, wndTypeId, wndSmpCnt, wndSmpCnt, 0);
        ifft::create(     p->ft, p->binCnt );
        ola::create(      p->ola, wndSmpCnt, hopSmpCnt, procSmpCnt, wndTypeId );
        
        for(k=0; k<(int)p->binCnt; ++k)
        {
          // complete revolutions per hop in radians
          p->itrV[k] = twoPi * floor((double)k * hopSmpCnt / wndSmpCnt ); 

          p->minRphV[k] = ((T1)(k-m)) * hopSmpCnt * twoPi / wndSmpCnt;
          p->maxRphV[k] = ((T1)(k+m)) * hopSmpCnt * twoPi / wndSmpCnt;

          //printf("%f %f %f\n",p->itrV[k],p->minRphV[k],p->maxRphV[k]);
        }

        return rc;  
      }
      
      template< typename T0, typename T1 >
      rc_t destroy( struct obj_str<T0,T1>*& p )
      {
        if( p != nullptr )
        {
          wnd_func::destroy(p->wf);
          ifft::destroy(p->ft);
          ola::destroy(p->ola);

          mem::release(p->minRphV);
          mem::release(p->maxRphV);
          mem::release(p->itrV);
          mem::release(p->phs0V);
          mem::release(p->phsV);
          mem::release(p->mag0V);
          mem::release(p->magV);
        
          mem::release( p );
        }
        return kOkRC;
      }

      template< typename T0, typename T1 >
      rc_t exec( struct obj_str<T0,T1>* p, const T1* magV, const T1* phsV )
      {

        double   twoPi = 2.0 * M_PI;
        unsigned k;

        for(k=0; k<p->binCnt; ++k)
        {
          // phase dist between cur and prv frame
          T1 dp = phsV[k] - p->phs0V[k];

          // dist must be positive (accum phase always increases)
          if( dp < -0.00001 )
            dp += twoPi;

          // add in complete revolutions based on the bin frequency
          // (these would have been lost from 'dp' due to phase wrap)
          dp += p->itrV[k];

          // constrain the phase change to lie within the range of the kth bin
          if( dp < p->minRphV[k] )
            dp += twoPi;

          if( dp > p->maxRphV[k] )
            dp -= twoPi;

          p->phsV[k] = p->phs0V[k] + dp;
          p->magV[k] = p->mag0V[k];
     
    
          p->phs0V[k] = phsV[k];
          p->mag0V[k] = magV[k];
        }
  
        ifft::exec_polar( p->ft, magV, phsV );

        // convert double to float
        T0 v[ p->ft->outN ];
        vop::copy( v, p->ft->outV, p->ft->outN );
  
        ola::exec( p->ola, v, p->ft->outN ); 

        //printf("%i %i\n",p->binCnt,p->ft.binCnt );

        //cmVOR_Print( p->obj.ctx->outFuncPtr, 1, p->binCnt, magV );
        //cmVOR_Print( p->obj.ctx->outFuncPtr, 1, p->binCnt, p->phsV );
        //cmVOS_Print( p->obj.ctx->outFuncPtr, 1, 10, p->ft.outV );

        return kOkRC;
        
      }
      
    } 

    
    //---------------------------------------------------------------------------------------------------------------------------------
    // Spectral Distortion
    //
    
    namespace spec_dist
    {
      template< typename T0, typename T1  >
      struct obj_str
      {
        bool bypassFl;
        
        T1   ceiling;
        T1   expo;    
        T1   mix;
        T1   thresh;
        T1   uprSlope;
        T1   lwrSlope;

        T0   ogain;

        T0*  outMagV;
        T0*  outPhsV;
        
      };

      typedef struct obj_str<float,float>   fobj_t;
      typedef struct obj_str<double,double> dobj_t;

      template< typename T0, typename T1 >
      rc_t create( struct obj_str<T0,T1>*& p, unsigned binN, bool bypassFl=false, T1 ceiling=30, T1 expo=2, T1 thresh=60, T1 uprSlope=0, T1 lwrSlope=2, T1 mix=0 )
      {
        rc_t rc = kOkRC;
        
        p = mem::allocZ< struct obj_str<T0,T1> >();

        p->bypassFl = bypassFl;
        p->ceiling  = ceiling;
        p->expo     = expo;    
        p->thresh   = thresh;
        p->uprSlope = uprSlope;
        p->lwrSlope = lwrSlope;
        p->mix      = mix;
        p->ogain    = 1;

        p->outMagV  = mem::allocZ<T0>( binN );
        p->outPhsV  = mem::allocZ<T0>( binN );

        return rc;
      }

      template< typename T0, typename T1 >
      rc_t destroy( struct obj_str<T0,T1>*& p )
      {
        rc_t rc = kOkRC;
        if( p != nullptr )
        {
          mem::release(p->outMagV);
          mem::release(p->outPhsV);
          mem::release(p);
        }
        return rc;
      }

      template< typename T0, typename T1 >
      void _cmSpecDist2Bump( struct obj_str<T0,T1>* p, double* x, unsigned binCnt, double thresh, double expo)
      {
        unsigned i     = 0;  
        double       minDb = -100.0;
  
        thresh = -fabs(thresh);

        for(i=0; i<binCnt; ++i)
        {
          double y;

          if( x[i] < minDb )
            x[i] = minDb;

          if( x[i] > thresh )
            y = 1;
          else
          {
            y  = (minDb - x[i])/(minDb - thresh);  
            y += y - pow(y,expo);
          }

          x[i] = minDb + (-minDb) * y;

        }  
      }

      template< typename T0, typename T1 >
      void _cmSpecDist2BasicMode_Original( struct obj_str<T0,T1>* p, double* X1m, unsigned binCnt, double thresh, double upr, double lwr )
      {

        unsigned i=0;

        if( lwr < 0.3 )
          lwr = 0.3;

        for(i=0; i<binCnt; ++i)
        {
          double a = fabs(X1m[i]);
          double d = a - thresh;

          X1m[i] = -thresh;

          if( d > 0 )
            X1m[i] -= (lwr*d);
          else
            X1m[i] -= (upr*d);
        }

      }

      template< typename T0, typename T1 >
      void _cmSpecDist2BasicMode_WithKnee( struct obj_str<T0,T1>* p, double* X1m, unsigned binCnt, double thresh, double upr, double lwr )
      {

        unsigned i=0;

        if( lwr < 0.3 )
          lwr = 0.3;

        for(i=0; i<binCnt; ++i)
        {
          double a = fabs(X1m[i]);
          double d = a - thresh;
          double curve_thresh = 3;
          
          X1m[i] = -thresh;

          if( d > curve_thresh )
            X1m[i] -= (lwr*d);
          else
          {
            if( d < -curve_thresh )
              X1m[i] -= (upr*d);
            else
            {
              double a  = (d+curve_thresh)/(curve_thresh*2.0);
              double slope = lwr*a + upr*(1.0-a);
              X1m[i] -=  slope * d;
            }
            
          }
        }

      }

      
      template< typename T0, typename T1 >
      rc_t exec( struct obj_str<T0,T1>* p, const T0* magV, const T0* phsV, unsigned binN )
      {
        rc_t rc = kOkRC;

        double X0m[binN];
        double X1m[binN]; 

        // take the mean of the the input magntitude spectrum
        double u0 = vop::mean(magV,binN);

        // convert magnitude to db (range=-1000.0 to 0.0)
        vop::ampl_to_db(X0m, magV, binN );
        vop::copy(X1m,X0m,binN);

        // bump transform X0m
        _cmSpecDist2Bump(p,X0m, binN, p->ceiling, p->expo);

        // mix bump output with raw input: X1m = (X0m*mix) + (X1m*(1.0-mix))
        vop::mul(X0m, p->mix,       binN );
        vop::mul(X1m, 1.0 - p->mix, binN );
        vop::add(X1m, X0m,          binN );


        // basic transform 
        _cmSpecDist2BasicMode_WithKnee(p,X1m,binN,p->thresh,p->uprSlope,p->lwrSlope);

        // convert db back to magnitude
        vop::db_to_ampl(X1m, X1m, binN );

        
        // convert the mean input magnitude to db
        double idb = 20*log10(u0);
    
        // get the mean output magnitude spectra
        double u1 = vop::mean(X1m,binN);
        
        //if( p->mix > 0 )
        if(1)
        {
          if( idb > -150.0 )
          {
            // set the output gain such that the mean output magnitude
            // will match the mean input magnitude
            p->ogain = u0/u1;  
          }
          else
          {
            T0 a0 = 0.9;
            p->ogain *= a0;
          }
        }
        
        
        // apply the output gain
        if( p->bypassFl )
          vop::copy( p->outMagV, magV, binN );
        else
          //vop::mul(  p->outMagV, X1m, std::min((T1)4.0,p->ogain), binN);
          vop::mul(  p->outMagV, X1m, p->ogain, binN);
        
        vop::copy( p->outPhsV, phsV,                binN);

        return rc;
      }
      
    }


    //---------------------------------------------------------------------------------------------------------------------------------
    // Data Recorder
    //
    // Record frames of data and write them to a CSV file.
    // A frame consists of 'sigN' values of type T.
    //
    namespace data_recorder
    {
      
      template< typename T  >
      struct block_str
      {
        T*                   buf;       // buf[frameN,sigN]
        struct block_str<T>* link;      // link to next block in chain
      };
      
      template< typename T  >
      struct obj_str
      {
        unsigned             sigN;     // count of channels per frame
        unsigned             frameN;   // count of frames per block
        struct block_str<T>* head;     // first block  
        struct block_str<T>* tail;     // last block and the one being currrently filled
        
        unsigned             frameIdx;  // index into tail of frame to fill
        char*                fn;        // output CSV filename
        char**               colLabelA; // output CSV column labels
        unsigned             colLabelN; // count of CSV column labels
        bool                 enableFl;  
      };

      typedef struct obj_str<float>  fobj_t;
      typedef struct obj_str<double> dobj_t;

      template< typename T >
      struct block_str<T>* _block_alloc( struct obj_str<T>* p )
      {
        struct block_str<T>* block = mem::allocZ< struct block_str<T> >();
        
        block->buf = mem::alloc<T>( p->frameN * p->sigN );
        
        if( p->head == nullptr )
          p->head = block;
        
        if( p->tail != nullptr )
          p->tail->link = block;

        p->tail = block;

        return block;
      }

      template< typename T >
      rc_t create( struct obj_str<T>*& p,
                   unsigned     sigN,
                   unsigned     frameCacheN,
                   const char*  fn,
                   const char** colLabelA,
                   unsigned     colLabelN,
                   bool         enableFl )
      {
        rc_t rc = kOkRC;
        
        p            = mem::allocZ< struct obj_str<T> >();
        
        p->frameN    = frameCacheN;
        p->sigN      = sigN;
        p->fn        = mem::duplStr(fn);        
        p->colLabelN = colLabelN;
        p->colLabelA = mem::allocZ< char* >( colLabelN );
        p->enableFl  = enableFl;
        
        for(unsigned i=0; i<colLabelN; ++i)
          p->colLabelA[i] = mem::duplStr(colLabelA[i]);
        
        _block_alloc(p);
        
        return rc;
      }

      template< typename T >
      rc_t create( struct obj_str<T>*& p, const object_t* cfg )
      {
        rc_t            rc        = kOkRC;
        bool            enableFl  = true;
        unsigned        sigN      = 0;
        unsigned        frameN    = 0;
        const char*     filename  = nullptr;
        const object_t* colLabelL = nullptr;
        
        // parse the recorder spec
        if((rc = cfg->getv("enableFl",   enableFl,
                           "sigN",     sigN,
                           "frameN",   frameN,
                           "filename", filename,
                           "colLabelL",colLabelL)) != kOkRC )
        {
          rc = cwLogError(rc,"Record cfg. parse failed.");
        }
        else
        {
          unsigned    labelN = colLabelL->child_count();
          const char* labelL[ labelN ];
          
          for(unsigned i=0; i<labelN; ++i)
            colLabelL->child_ele(i)->value( labelL[i] );

          rc = create(p, sigN, frameN, filename, labelL, labelN, enableFl );
        }
        
        return rc;
      }
            
      template< typename T>
      rc_t destroy( struct obj_str<T>*& p )
      {
        if( p != nullptr )
        {

          if( p->enableFl && p->fn != nullptr && textLength(p->fn)!=0 )
            write_as_csv(p,p->fn);
            
          for(unsigned i=0; i<p->colLabelN; ++i)
            mem::release( p->colLabelA[i] );
          
          struct block_str<T>* b0 = p->head;
          while( b0 != nullptr )
          {
            struct block_str<T>* b1 = b0->link;
            mem::release(b0->buf);
            mem::release(b0);
            b0 = b1;
          }

          mem::release(p->fn);
          mem::release(p);
        }
        return kOkRC;
      }

      // Pass a partial (or full) frame of data to the object.
      // xV[xN] is the data to record.
      // chIdx is the first channel to write to.
      // (xN + chIdx must be less than p->sigN)
      // Set advance_fl to true to advance to the next frame.
      template< typename T>
      rc_t exec( struct obj_str<T>* p, const T* xV, unsigned xN, unsigned chIdx=0, bool advance_fl = true  )
      {
        struct block_str<T>* b = p->tail;
        
        if( chIdx + xN > p->sigN )
          return cwLogError(kInvalidArgRC,"Channel index (%i) plus channel count (%i) is out of range of the allocated channe count (%i).", chIdx, xN, p->sigN );


        if( p->enableFl )
        {
          for(unsigned i=chIdx; i-chIdx<xN; ++i)
          {
            assert( p->frameIdx * p->sigN + i < p->frameN * p->sigN );
            b->buf[ p->frameIdx * p->sigN + i ] = xV[i-chIdx];
          }

          if( advance_fl )
            advance(p);
        }
        
        return kOkRC;
      }

      template< typename T>
      rc_t advance( struct obj_str<T>* p, unsigned frameN=1 )
      {
        for(unsigned i=0; i<frameN; ++i)
        {
          p->frameIdx += 1;
          
          if( p->frameIdx >=  p->frameN )
          {
            _block_alloc(p);
            p->frameIdx = 0;
          }
        }

        return kOkRC;
      }

      template< typename T>
      rc_t write_as_csv( const struct obj_str<T>* p, const char* fn )
      {
        rc_t rc = kOkRC;
        file::handle_t h;
        struct block_str<T>* b = p->head;
        
        if((rc = file::open(h,fn,file::kWriteFl)) != kOkRC )
        {
          rc = cwLogError(kOpenFailRC,"Create failed on the data recorder output file '%s'.", cwStringNullGuard(fn));
          goto errLabel;          
        }

        for(; b!=nullptr; b=b->link)
        {
          unsigned frameN = b->link==NULL ? p->frameIdx : p->frameN;
          for(unsigned fi=0, rowIdx=0; fi<frameN; ++fi,++rowIdx)
          {
            if( rowIdx == 0 )
            {
              for(unsigned ci=0; ci<p->colLabelN; ++ci)
                file::printf(h,"%s, ", p->colLabelA[ci] );
            }
            else
            {
              for(unsigned ci=0; ci<p->sigN; ++ci)
                file::printf(h,"%f,", b->buf[ fi*p->sigN + ci ]);
            }
            
            file::print(h,"\n");
          }
        }
        

      errLabel:
        
        file::close(h);
        return rc;
      }
      
    }


    //---------------------------------------------------------------------------------------------------------------------------------
    // wt_osc
    //
    
    namespace wt_osc
    {

      typedef enum {
        kInvalidWtTId,
        kOneShotWtTId,
        kLoopWtTId
      } wt_tid_t;

      template< typename sample_t, typename srate_t >
      struct wt_str
      {
        wt_tid_t        tid;
        unsigned        cyc_per_loop; // count of cycles in the loop
        sample_t*       aV;       // aV[ padN + aN + padN ]
        unsigned        aN;       // Count of unique samples
        double          rms;
        double          hz;
        srate_t         srate;
        unsigned        pad_smpN;
        unsigned        posn_smp_idx; // The location of this sample in the original audio file. 
      };

      template< typename sample_t >
      sample_t table_read_2( const sample_t* tab, double frac )
      {

        unsigned i0 = floor(frac);
        unsigned i1 = i0 + 1;
        double   f  = frac - int(frac);

        sample_t r = (sample_t)(tab[i0] + (tab[i1] - tab[i0]) * f);

        //intf("r:%f frac:%f i0:%i f:%f\n",r,frac,i0,f);
        return r;
      }

      template< typename sample_t >
      sample_t hann_read( double x, double N )
      {
        while( x > N)
          x -= N;

        x = x - (N/2) ;

        return (sample_t)(0.5 + 0.5 * cos(2*M_PI * x / N));
      }
      
      template< typename sample_t, typename srate_t >
      struct obj_str
      {
        const wt_str<sample_t,srate_t>* wt;
        
        double    phs;          // current fractional phase into wt->aV[]
        double    fsmp_per_wt;  // 
        
      };

      template< typename sample_t, typename srate_t >
      bool validate_srate(const struct obj_str<sample_t,srate_t>* p, srate_t expected_srate)
      { return p->wt != nullptr && p->wt->srate == expected_srate; }
      
      template< typename sample_t, typename srate_t >
      bool is_init(const struct obj_str<sample_t,srate_t>* p)
      { return p->wt != nullptr;  }
      
      template< typename sample_t, typename srate_t >
      void init(struct obj_str<sample_t,srate_t>* p, struct wt_str<sample_t,srate_t>* wt)
      {
        if( wt == nullptr )
          p->wt = nullptr;
        else
        {
          double fsmp_per_cyc = wt->srate/wt->hz;
          p->fsmp_per_wt  = fsmp_per_cyc * 2;  // each wavetable contains 2
          
          p->wt     = wt;
          p->phs   = 0;
        }        
      }

      template< typename sample_t, typename srate_t >
      void _process_loop(struct obj_str<sample_t,srate_t>* p, sample_t* aV, unsigned aN, unsigned& actual_Ref)
      {
        double   phs0       = p->phs;
        double   phs1       = phs0 + p->fsmp_per_wt/2;
        unsigned smp_per_wt = (int)floor(p->fsmp_per_wt); // 

        while(phs1 >= smp_per_wt)
          phs1 -= smp_per_wt;
        
        for(unsigned i=0; i<aN; ++i)
        {
          sample_t s0 = table_read_2( p->wt->aV+p->wt->pad_smpN, phs0 );
          sample_t s1 = table_read_2( p->wt->aV+p->wt->pad_smpN, phs1 );

          sample_t e0 = hann_read<sample_t>(phs0,p->fsmp_per_wt);
          sample_t e1 = hann_read<sample_t>(phs1,p->fsmp_per_wt);

          aV[ i ] = e0*s0 + e1*s1;

          // advance the phases of the oscillators
          phs0 += 1;
          while(phs0 >= smp_per_wt)
            phs0 -= smp_per_wt;

          phs1 += 1;
          while(phs1 >= smp_per_wt)
            phs1 -= smp_per_wt;

        }
        
        p->phs     = phs0;
        actual_Ref = aN;
      }

      template< typename sample_t, typename srate_t >
      void _process_one_shot(struct obj_str<sample_t,srate_t>* p, sample_t* aV, unsigned aN, unsigned& actual_Ref)
      {        
        unsigned phs = (unsigned)p->phs;
        unsigned i;
        for(i=0; i<aN && phs<p->wt->aN; ++i,++phs)
          aV[i] = p->wt->aV[ p->wt->pad_smpN + phs ];

        p->phs     = phs;
        actual_Ref = i;
        
      }
      
      template< typename sample_t, typename srate_t >
      void process(struct obj_str<sample_t,srate_t>* p, sample_t* aV, unsigned aN, unsigned& actual_Ref)
      {
        actual_Ref = 0;
        switch( p->wt->tid )
        {
          case wt_osc::kLoopWtTId:
            _process_loop(p,aV,aN,actual_Ref);
            break;
            
          case wt_osc::kOneShotWtTId:
            _process_one_shot(p,aV,aN,actual_Ref);
            break;
            
          default:
            assert(0);
        }
        
      }
      
      rc_t test();
     
    } // wt_osc     
      
      
    namespace wt_seq_osc
    {

      template< typename sample_t, typename srate_t >
      struct wt_seq_str
      {
        struct wt_osc::wt_str<sample_t,srate_t>* wtA;
        unsigned                                 wtN;
      };
      
      template< typename sample_t, typename srate_t >
      struct obj_str
      {
        struct wt_seq_osc::wt_seq_str<sample_t,srate_t>* wt_seq;
        struct wt_osc::obj_str<sample_t,srate_t>         osc0;
        struct wt_osc::obj_str<sample_t,srate_t>         osc1;
        
        unsigned wt_idx; // index of wt0 in wt_seq->wtA[]

        
        unsigned mix_interval_smp; // osc0/osc1 crossfade interval in samples
        unsigned mix_phs;          // current crossfade phase (0 <= mix_phs <= mix_interval_smp)
      };

      template< typename sample_t, typename srate_t >
      rc_t _update_wt( struct obj_str<sample_t,srate_t>* p, unsigned wt_idx )
      {
        rc_t rc = kOkRC;        
        struct wt_osc::wt_str<sample_t,srate_t>* wt0 = nullptr;
        struct wt_osc::wt_str<sample_t,srate_t>* wt1 = nullptr;

        p->mix_interval_smp = 0;
        
        if( wt_idx < p->wt_seq->wtN )
          wt0 = p->wt_seq->wtA + wt_idx;
        
        if( (wt_idx+1) < p->wt_seq->wtN )
        {
          wt1 = p->wt_seq->wtA + (wt_idx+1);
          
          unsigned posn0_smp_idx = wt0->posn_smp_idx;
          unsigned posn1_smp_idx = wt1->posn_smp_idx;

          if( posn1_smp_idx < posn0_smp_idx )
          {
            rc = cwLogError(kInvalidStateRC,"The position of the wavetable at wt. seq index:%i must be greater than the position of the previous wt.",wt_idx+1);
            
            goto errLabel;
          }
          
          p->mix_interval_smp = posn1_smp_idx - posn0_smp_idx;
        }

        wt_osc::init(&p->osc0,wt0);
        wt_osc::init(&p->osc1,wt1);
        
        p->wt_idx = wt_idx;
        p->mix_phs = 0;

      errLabel:
        return rc;
      }

      template< typename sample_t, typename srate_t >
      bool validate_srate(const struct obj_str<sample_t,srate_t>* p, srate_t expected_srate)
      {
        if( p->wt_seq == nullptr )
          return false;
        for(unsigned i=0; i<p->wt_seq->wtN; ++i)
          if( p->wt_seq->wtA[i].srate != expected_srate )
            return false;
        
        return true;
      }

      template< typename sample_t, typename srate_t >
      bool is_init( const struct obj_str<sample_t,srate_t>* p )
      {
        return is_init(&p->osc0);
      }
      
      template< typename sample_t, typename srate_t >
      rc_t init(struct obj_str<sample_t,srate_t>* p, struct wt_seq_osc::wt_seq_str<sample_t,srate_t>* wt_seq)
      {
        rc_t rc = kOkRC;
        p->wt_seq = wt_seq;
        p->wt_idx = 0;

        if((rc = _update_wt(p,0)) != kOkRC )
          goto errLabel;

      errLabel:
        return rc;
      }

      
      template< typename sample_t, typename srate_t >
      rc_t process(struct obj_str<sample_t,srate_t>* p, sample_t* aV, unsigned aN, unsigned& actual_Ref)
      {
        actual_Ref = 0;
        
        rc_t rc = kOkRC;
        unsigned actual;
        bool atk_fl = p->wt_idx==0 && p->osc0.wt->tid == wt_osc::kOneShotWtTId;

        // if the osc is in the attack phase
        if( atk_fl )
        {
          // update aV[aN] from osc0
          wt_osc::process(&p->osc0,aV,aN,actual);

          actual_Ref = actual;

          // if all requested samples were generated we are done ...
          if( actual >= aN )
            return rc;

          // otherwise all requested samples were not generated
          // fill the rest of aV[] from the next one or two wave tables.
          aN -= actual;
          aV += actual;

          // initialize osc0 and osc1
          if((rc = _update_wt(p, 1)) != kOkRC )
            goto errLabel;          
        }

        wt_osc::process(&p->osc0,aV,aN,actual);

        // if the second oscillator is initialized
        if( wt_osc::is_init(&p->osc1) )
        {
          unsigned actual1 = 0;
          sample_t tV[ aN ];
          // generate aN samples into tV[aN]
          wt_osc::process(&p->osc1,tV,aN,actual1);

          assert( actual1 == actual );

          
          sample_t g = (sample_t)std::min(1.0,(double)p->mix_phs / p->mix_interval_smp);

          // mix the output of the second oscillator into the output signal
          vop::scale_add(aV,aV,(1.0f-g),tV,g,actual1);

          p->mix_phs += actual;

          // if the osc0/osc1 xfade is complete ...
          if( p->mix_phs >= p->mix_interval_smp )
          {
            // ... then advance to the next set of wavetables
            if((rc = _update_wt(p, p->wt_idx+1)) != kOkRC )
              goto errLabel;
          }
          
        }
         
        actual_Ref += actual;          
       
      errLabel:
        return rc;
      }
      
      rc_t test();
      
    } // wt_seq_osc

    namespace multi_ch_wt_seq_osc
    {
      template< typename sample_t, typename srate_t >
      struct multi_ch_wt_seq_str
      {
        struct wt_seq_osc::wt_seq_str<sample_t,srate_t>* chA;
        unsigned                                         chN;
      };

      template< typename sample_t, typename srate_t >
      struct obj_str
      {
        const struct multi_ch_wt_seq_str<sample_t,srate_t>* mcs      = nullptr;
        struct wt_seq_osc::obj_str<sample_t,srate_t>*       chA      = nullptr;
        unsigned                                            chAllocN = 0;
        unsigned                                            chN      = 0;
        bool                                                done_fl  = true;
      };


      // if mcs != nullptr and expected_srate is non-zero then the expected_srate will be validated
      template< typename sample_t, typename srate_t >
      rc_t create(struct obj_str<sample_t,srate_t>* p, unsigned maxChN, const struct multi_ch_wt_seq_str<sample_t,srate_t>* mcs=nullptr, srate_t expected_srate=0 )
      {
        rc_t rc = kOkRC;

        destroy(p);

        p->chA = mem::allocZ< struct wt_seq_osc::obj_str<sample_t,srate_t> >(maxChN);
        p->chAllocN = maxChN;
        p->chN = 0;
        p->done_fl = true;

        if( mcs != nullptr )
          setup(p,mcs);
        
        return rc;
      }

      template< typename sample_t, typename srate_t >
      rc_t destroy(struct obj_str<sample_t,srate_t>* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->chA);
        p->chAllocN = 0;
        p->chN = 0;
        p->done_fl = true;
        return rc;
      }

      // if mcs != nullptr and expected_srate is non-zero then the expected_srate will be validated
      template< typename sample_t, typename srate_t >
      rc_t setup( struct obj_str<sample_t,srate_t>* p, const struct multi_ch_wt_seq_str<sample_t,srate_t>* mcs, srate_t expected_srate=0 )
      {
        rc_t rc = kOkRC;
        
        if( mcs->chN > p->chAllocN )
        {
          rc = cwLogError(kInvalidArgRC,"Invalid multi-ch-wt-osc channel count. (%i > %i)",mcs->chN,p->chAllocN);
          goto errLabel;
        }
        
        p->mcs = mcs;
        p->done_fl = false;
        p->chN = mcs->chN;
        for(unsigned i=0; i<mcs->chN; ++i)
          if((rc = wt_seq_osc::init(p->chA+i,mcs->chA + i)) != kOkRC )
            goto errLabel;

        if( mcs != nullptr && expected_srate != 0 )
          if( !validate_srate(p,expected_srate) )
          {
            rc = cwLogError(kInvalidArgRC,"The srate is not valid. All wave tables do not share the same sample rate.");
            goto errLabel;                                                                                        
          }
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"multi-ch-wt-osc setup failed.");
        
        return rc;
      }

      template< typename sample_t, typename srate_t >
      bool validate_srate(const struct obj_str<sample_t,srate_t>* p, srate_t expected_srate)
      {
        if( p->chA == nullptr )
          return false;
        
        for(unsigned i=0; i<p->chN; ++i)
          if( !validate_srate(p->chA+i,expected_srate) )
            return false;
        return true;
      }
      

      template< typename sample_t, typename srate_t >
      rc_t is_done( struct obj_str<sample_t,srate_t>* p )
      { return p->done_fl; }
      
      template< typename sample_t, typename srate_t >
      rc_t process( struct obj_str<sample_t,srate_t>* p, sample_t* aM, unsigned chN, unsigned frmN, unsigned& actual_Ref )
      {
        rc_t     rc     = kOkRC;
        unsigned actual = 0;
        unsigned doneN  = 0;
        
        for(unsigned i=0; i<p->chN; ++i)
        {
          unsigned actual0 = 0;
          sample_t* aV = aM + (i*frmN);
          
          if( !wt_seq_osc::is_init(p->chA + i) )
          {
            vop::zero(aV,frmN);
            actual0 = frmN;
            doneN += 1;
          }
          else
          {
            if((rc = wt_seq_osc::process(p->chA + i, aV, frmN, actual0 )) != kOkRC )
              goto errLabel;
          }
          
          if( i!=0 && actual0 != actual )
          {
            rc = cwLogError(kInvalidStateRC,"An inconsistent sample count was generated across channels (%i != !i).",actual0,actual);
            goto errLabel;
          }

          actual = actual0;
        }

        actual_Ref = actual;
        p->done_fl = doneN == p->chN;
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"multi-ch-wt-osc process failed.");
        
        return rc;
      }

      rc_t test();
      
    } //multi_ch_wt_seq_osc

    
    rc_t test( const test::test_args_t& args );
    
  } // dsp
} // cw


#endif
