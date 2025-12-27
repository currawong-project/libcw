//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwUtility.h"
#include "cwMath.h"
#include "cwVectOps.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include "cwText.h"


//----------------------------------------------------------------------------------------------------------------------
//  fft
//

unsigned cw::dsp::fft::window_sample_count_to_bin_count( unsigned wndSmpN )
{ return wndSmpN/2 + 1; }

unsigned cw::dsp::fft::bin_count_to_window_sample_count( unsigned binN )
{ return (binN-1) * 2; }

cw::rc_t cw::dsp::fft::test()
{
  typedef float real_t;
  
  rc_t     rc    = kOkRC;
  unsigned flags = kToPolarFl;
  unsigned xN    = 16;
  real_t   srate = xN;
  real_t   hz    = 1;
  real_t   xV[xN];
  struct obj_str<real_t>* p = nullptr;

  create<real_t>(p,xN,flags);
  
  if(p == nullptr )
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
  
  struct fft::obj_str<real_t>* ft  = nullptr;
  struct obj_str<real_t>*      ift = nullptr;
  rc_t                         rc  = kOkRC;
  unsigned                     xN  = 16;
  real_t                       xV[xN];

  if( (rc = fft::create<real_t>(ft,xN,fft::kToPolarFl)) != kOkRC )
  {
    rc = cwLogError(rc,"FFT procedure allocation failed.");
    goto errLabel;
  }

  if((rc = create<real_t>(ift, fft::bin_count(ft))) != kOkRC )
  {
    rc = cwLogError(rc,"IFFT procedure allocation failed.");
    goto errLabel;
  }
  
  vop::zero(xV,xN);
  vop::sine(xV,xN,(double)xN,1.0);
  
  fft::exec(ft,xV,xN);


  exec_polar(ift, fft::magn(ft), fft::phase(ft) );

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
//  intel_fft
//
#ifdef cwMKL

unsigned cw::dsp::intel_fft::window_sample_count_to_bin_count( unsigned wndSmpN )
{ return wndSmpN/2 + 1; }

unsigned cw::dsp::intel_fft::bin_count_to_window_sample_count( unsigned binN )
{ return (binN-1) * 2; }

cw::rc_t cw::dsp::intel_fft::test()
{
  typedef float real_t;
  
  rc_t     rc    = kOkRC;
  unsigned flags = kToPolarFl;
  unsigned xN    = 16;
  real_t   srate = xN;
  real_t   hz    = 1;
  real_t   xV[xN];
  struct obj_str<real_t>* p = nullptr;

  create<real_t>(p,xN,flags);
  
  if(p == nullptr )
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
//  intel_ifft
//

cw::rc_t cw::dsp::intel_ifft::test()
{
  typedef double real_t;
  
  struct fft::obj_str<real_t>* ft  = nullptr;
  struct obj_str<real_t>*      ift = nullptr;
  rc_t                         rc  = kOkRC;
  unsigned                     xN  = 16;
  real_t                       xV[xN];

  if( (rc = fft::create<real_t>(ft,xN,fft::kToPolarFl)) != kOkRC )
  {
    rc = cwLogError(rc,"FFT procedure allocation failed.");
    goto errLabel;
  }

  if((rc = create<real_t>(ift, fft::bin_count(ft))) != kOkRC )
  {
    rc = cwLogError(rc,"IFFT procedure allocation failed.");
    goto errLabel;
  }
  
  vop::zero(xV,xN);
  vop::sine(xV,xN,(double)xN,1.0);
  
  fft::exec(ft,xV,xN);


  exec_polar(ift, fft::magn(ft), fft::phase(ft) );

  vop::print( xV,          xN,               "%f ", "sin " );
  vop::print( magn(ft),  fft::bin_count(ft), "%f ", "mag " );
  vop::print( phase(ft), fft::bin_count(ft), "%f ", "phs " );
  vop::print( out(ift),     out_count(ift),  "%f ", "sig " );
  
 errLabel:
  destroy(ft);
  destroy(ift);

  return rc;
  
}

#endif

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
  
   
  obj_str<real_t>* p = nullptr;

  create(p,hV,hN,xN);

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


cw::rc_t cw::dsp::test_dsp( const test::test_args_t& args )
{
  rc_t rc = kOkRC;

  if( textIsEqual(args.test_label,"fft") )
  {
    rc = fft::test();
    goto errLabel;
  }
  
  if( textIsEqual(args.test_label,"ifft") )
  {
    rc = ifft::test();
    goto errLabel;
  }
#ifdef cwMKL
  if( textIsEqual(args.test_label,"intel_fft") )
  {
    rc = intel_fft::test();
    goto errLabel;
  }
  
  if( textIsEqual(args.test_label,"intel_ifft") )
  {
    rc = intel_ifft::test();
    goto errLabel;
  }
#endif  
  if( textIsEqual(args.test_label,"convolve") )
  {
    rc = convolve::test();
    goto errLabel;
  }
  
  rc = cwLogError(kInvalidArgRC,"Unknown dsp test case module:%s test:%s.",args.module_label,args.test_label);
  
errLabel:
  return rc;
}
