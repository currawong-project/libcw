#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwUtility.h"
#include "cwVectOps.h"
#include "cwDsp.h"


//----------------------------------------------------------------------------------------------------------------------
//  fft
//

cw::rc_t cw::dsp::fft::test()
{
  typedef float real_t;
  
  rc_t     rc    = kOkRC;
  unsigned flags = kToPolarFl;
  unsigned xN    = 16;
  real_t   srate = xN;
  real_t   hz    = 1;
  real_t   xV[xN];
  struct ptr_str<real_t>* p = create<real_t>(xN,flags);
  
  if(p != nullptr )
  {
    rc = cwLogError(kOpFailRC,"FFT procedure allocation failed.");
    goto errLabel;
  }
  
  vop::zero(xV,xN);
  vop::sine(xV,xN,srate,hz);
  
  exec(p,xV,xN);

  vop::print( xV,         xN,           "%f ", "sin " );
  vop::print( magn(p),  bin_count(p), "%f ", "mag " );
  vop::print( phase(p), bin_count(p), "%f ", "phs " );

 errLabel:
  destroy(p);

  return rc;
  
}


//----------------------------------------------------------------------------------------------------------------------
//  ifft
//

cw::rc_t cw::dsp::ifft::test()
{
  typedef double real_t;
  
  struct fft::ptr_str<real_t>* ft  = nullptr;
  struct ptr_str<real_t>*      ift = nullptr;
  rc_t                         rc  = kOkRC;
  unsigned                     xN  = 16;
  real_t                       xV[xN];

  if( (ft = fft::create<real_t>(xN,fft::kToPolarFl)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"FFT procedure allocation failed.");
    goto errLabel;
  }

  if((ift = create<real_t>(fft::bin_count(ft))) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"IFFT procedure allocation failed.");
    goto errLabel;
  }
  
  vop::zero(xV,xN);
  vop::sine(xV,xN,(double)xN,1.0);
  
  fft::exec(ft,xV,xN);


  exec(ift, fft::magn(ft), fft::phase(ft) );

  vop::print( xV,          xN,               "%f ", "sin " );
  vop::print( magn(ft),  fft::bin_count(ft), "%f ", "mag " );
  vop::print( phase(ft), fft::bin_count(ft), "%f ", "phs " );
  vop::print( out(ift),     out_count(ift),  "%f ", "sig " );
  
 errLabel:
  destroy(ft);
  destroy(ift);

  return rc;
  
}


//----------------------------------------------------------------------------------------------------------------------
//  convolve
//

cw::rc_t cw::dsp::convolve::test()
{
  typedef float real_t;
  
  real_t hV[] = { 1, .5, .25, .1, .05 };
  unsigned hN = sizeof(hV) / sizeof(hV[0]);
  real_t xV[] = { 1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0 };
  unsigned xN = 4; //sizeof(xV) / sizeof(xV[0]);
  real_t yV[] = { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0 }; 
  unsigned yN = sizeof(yV) / sizeof(yV[0]);
  // correct output from apply: real_t zV[] = { 1.,  0.5, 0.25,0.1, 1.05,0.5, 0.25,0.1, 1.05,0.5, 0.25,0.1, 0.05,0.0,   0.0 };
  
   
  ptr_str<real_t>* p = create(hV,hN,xN);

  exec(p,xV,xN);

  vop::print( p->outV, p->outN ,  "%f ", "out ");
  vop::print( p->olaV, p->olaN,   "%f ", "ola " );

  exec(p,xV+4,xN);
  
  vop::print( p->outV, p->outN ,  "%f ", "out ");
  vop::print( p->olaV, p->olaN,   "%f ", "ola " );

  exec(p,xV+8,xN);
  
  vop::print( p->outV, p->outN ,  "%f ", "out ");
  vop::print( p->olaV, p->olaN,   "%f ", "ola " );

  xN = sizeof(xV) / sizeof(xV[0]);
  
  apply(xV,xN,hV,hN,yV,yN);
  vop::print( yV, yN ,  "%4.2f ", "yV ");
  
  destroy(p);



  return kOkRC;
}

// 1.   0.5  0.25 0.1  1.05 0.5  0.25 0.1  1.05 0.5  0.25 0.1  0.05 0.0.   0.  ]
// 1.0  0.5  0.25 0.1  1.05 0.5  0.25 0.1  1.05 0.5  0.25 0.1  1.05 1.0    0.75 0.
