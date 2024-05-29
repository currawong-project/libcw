#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwUtility.h"
#include "cwFileSys.h"
#include "cwAudioFileOps.h"
#include "cwVectOps.h"
#include "cwMath.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include "cwDspTransforms.h"

namespace cw
{
  namespace dsp
  {
    namespace compressor
    {
      void _ms_to_samples( obj_t*p, ftime_t ms, unsigned& outRef )
      {
        outRef = std::max(1u,(unsigned)floor(ms * p->srate / 1000.0));
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------
// compressor
//

cw::rc_t cw::dsp::compressor::create( obj_t*& p, srate_t srate, unsigned procSmpCnt, coeff_t inGain, ftime_t rmsWndMaxMs, ftime_t rmsWndMs, coeff_t threshDb, coeff_t ratio_num, ftime_t atkMs, ftime_t rlsMs, coeff_t outGain, bool bypassFl )
{
  p = mem::allocZ<obj_t>();
      
  p->srate      = srate;
  p->procSmpCnt = procSmpCnt;
  p->threshDb   = threshDb;
  p->ratio_num  = ratio_num;

  set_attack_ms(p,atkMs);
  set_release_ms(p,rlsMs);

  p->inGain     = inGain;
  p->outGain    = outGain;
  p->bypassFl   = bypassFl;

  p->rmsWndAllocCnt = (unsigned)std::max(1.0,floor(rmsWndMaxMs * srate / (1000.0 * procSmpCnt)));
  p->rmsWnd         = mem::allocZ<sample_t>(p->rmsWndAllocCnt);
  set_rms_wnd_ms(p, rmsWndMs );
  p->rmsWndIdx = 0; 

  p->state       = kRlsCompId;
  p->timeConstDb = 10.0;
  p->accumDb     = p->threshDb;

  return kOkRC;
}
    
cw::rc_t cw::dsp::compressor::destroy( obj_t*& p )
{
  mem::release(p->rmsWnd);
  mem::release(p);
  return kOkRC;
}

/*
  The ratio determines to what degree a signal above the threshold is reduced.
  Given a 2:1 ratio, a signal 2dB above the threshold will be reduced to 1db above the threshold.
  Given a 4:1 ratio, a signal 2dB above the threshold will be reduced to 0.25db above the threshold.
  Gain_reduction_db = (thresh - signal) / ratio_numerator  (difference between the threshold and signal level after reduction)
  Gain Coeff = 10^(gain_reduction_db / 20);
 
  Total_reduction_db = signal - threshold + Gain_reduc_db
  (total change in signal level)

  The attack can be viewed as beginning at the threshold and moving to the peak
  over some period of time. In linear terms this will go from 1.0 to the max gain
  reductions. In this case we step from thresh to peak at a fixed rate in dB
  based on the attack time.

  Db:        thresh - [thesh:peak] / ratio_num
  Linear: pow(10, (thresh - [thesh:peak] / ratio_num)/20 );

  During attacks p->accumDb increments toward the p->pkDb.
  During release p->accumDb decrements toward the threshold.
  
  (thresh - accumDb) / ratio_num gives the signal level which will be achieved
  if this value is converted to linear form and applied as a gain coeff.

  See compressor.m
*/
    
cw::rc_t cw::dsp::compressor::exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n )
{

  sample_t xx[n];

  vop::mul(xx,x,p->inGain,n); // apply input gain

  p->rmsWnd[ p->rmsWndIdx ] = vop::rms(xx, n);               // calc and store signal RMS
  p->rmsWndIdx              = (p->rmsWndIdx + 1) % p->rmsWndCnt; // advance the RMS storage buffer

  coeff_t rmsLin = vop::mean(p->rmsWnd,p->rmsWndCnt);                   // calc avg RMS
  coeff_t rmsDb  = std::max(-100.0,20 * log10(std::max((coeff_t)0.00001,rmsLin)));  // convert avg RMS to dB
  rmsDb += 100.0;

  // if the compressor is bypassed
  if( p->bypassFl )
  {
    vop::copy(y,x,n); // copy through - with no input gain
    return kOkRC;
  }

  // if the signal is above the threshold
  if( rmsDb <= p->threshDb )
    p->state = kRlsCompId;
  else
  {
    if( rmsDb > p->pkDb )  
      p->pkDb   = rmsDb;
    
    p->state  = kAtkCompId;    
  }

  switch( p->state )
  {
    case kAtkCompId:                      
      p->accumDb = std::min(p->pkDb, p->accumDb + p->timeConstDb * n / p->atkSmp );
      break;

    case kRlsCompId:
      p->accumDb = std::max(p->threshDb, p->accumDb - p->timeConstDb * n / p->rlsSmp );
      break;
  }

  p->gain = pow(10.0,(p->threshDb - p->accumDb) / (p->ratio_num * 20.0));

  vop::mul(y,xx,p->gain * p->outGain,n);
  
  return kOkRC;
      
}

    
void cw::dsp::compressor::set_attack_ms(  obj_t* p, ftime_t ms )
{
  _ms_to_samples(p,ms,p->atkSmp);
}
    
void cw::dsp::compressor::set_release_ms( obj_t* p, ftime_t ms )
{
  _ms_to_samples(p,ms,p->rlsSmp);
}
    
void cw::dsp::compressor::set_rms_wnd_ms( obj_t* p, ftime_t ms )
{
  p->rmsWndCnt = std::max((unsigned)1,(unsigned)floor(ms * p->srate / (1000.0 * p->procSmpCnt)));

  // do not allow rmsWndCnt to exceed rmsWndAllocCnt
  if( p->rmsWndCnt > p->rmsWndAllocCnt )
    p->rmsWndCnt = p->rmsWndAllocCnt;      
}

//----------------------------------------------------------------------------------------------------------------
// Limiter
//

cw::rc_t cw::dsp::limiter::create( obj_t*& p, srate_t srate, unsigned procSmpCnt, coeff_t thresh, coeff_t igain, coeff_t ogain, bool bypassFl )
{
  p = mem::allocZ<obj_t>();

  p->procSmpCnt = procSmpCnt;
  p->thresh     = thresh;
  p->igain      = igain;
  p->ogain      = ogain;
  return kOkRC;
}

cw::rc_t cw::dsp::limiter::destroy( obj_t*& p )
{
  mem::release(p);
  return kOkRC;
}

cw::rc_t cw::dsp::limiter::exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n )
{
  if( p->bypassFl )
  {
    vop::copy(y,x,n); // copy through - with no input gain
    return kOkRC;
  }
  else
  {
    coeff_t T = p->thresh * p->ogain;
  
    for(unsigned i=0; i<n; ++i)
    {
      sample_t mx = 0.999;
      sample_t s = x[i] < 0.0 ? -mx : mx;
      sample_t v = fabsf(x[i]) * p->igain;
      
      if( v >= mx )
        y[i] = s;
      else
      {
        if( v < p->thresh )
        {
          y[i] = s * T * v/p->thresh;
        }    
        else
        {
          // apply a linear limiting function
          y[i] = s * (T + (1.0f-T) * (v-p->thresh)/(1.0f-p->thresh));
        }
      }
    }
  }
  return kOkRC;
}

//----------------------------------------------------------------------------------------------------------------
// dc-filter
//

cw::rc_t cw::dsp::dc_filter::create( obj_t*& p, srate_t srate, unsigned procSmpCnt, coeff_t gain, bool bypassFl )
{
  p = mem::allocZ<obj_t>();

  p->gain     = gain;
  p->bypassFl = bypassFl;
  p->b0       = 1;
  p->b[0]     = -1;
  p->a[0]     = -0.999;
  p->d[0]     = 0;
  p->d[1]     = 0;
  
  
  return kOkRC;
}

cw::rc_t cw::dsp::dc_filter::destroy( obj_t*& pp )
{
  mem::release(pp);
  return kOkRC;
}

cw::rc_t cw::dsp::dc_filter::exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n )
{

  if( p->bypassFl )
    vop::copy(y,x,n);
  else
    vop::filter<sample_t,coeff_t>(y,n,x,n,p->b0, p->b, p->a,  p->d, 1 );

  return kOkRC;
}

cw::rc_t cw::dsp::dc_filter::set( obj_t* p, coeff_t gain, bool bypassFl )
{
  p->gain = gain;
  p->bypassFl = bypassFl;
  return kOkRC;
}


//----------------------------------------------------------------------------------------------------------------
// Recorder
//

cw::rc_t cw::dsp::recorder::create(  obj_t*& pRef, srate_t srate, ftime_t max_secs, unsigned chN )
{
  obj_t* p     = mem::allocZ<obj_t>();
  p->srate     = srate;
  p->maxFrameN = (unsigned)(max_secs * srate);
  p->chN       = chN;
  p->buf       = mem::allocZ<sample_t>( p->maxFrameN * p->chN );

  pRef = p;

  return kOkRC;
}

cw::rc_t cw::dsp::recorder::destroy( obj_t*& pRef)
{
  obj_t* p = pRef;

  mem::release(p->buf);
  mem::release(p);
  
  pRef = nullptr;
  return kOkRC;
}

cw::rc_t cw::dsp::recorder::exec( obj_t* p, const sample_t* buf, unsigned chN, unsigned frameN )
{
  const sample_t* chA[ chN ];
  for(unsigned i=0; i<chN; ++i)
    chA[i] = buf + (i*frameN);

  return exec(p, chA, chN, frameN );
}

cw::rc_t cw::dsp::recorder::exec( obj_t* p, const sample_t* chA[], unsigned chN, unsigned frameN )
{
  chN = std::min( chN, p->chN );
  
  // 
  if( p->frameIdx + frameN > p->maxFrameN )
    frameN = p->maxFrameN - p->frameIdx;
  
  for(unsigned i=0; i<chN; ++i)
    for(unsigned j=0; j<frameN; ++j )
      p->buf[ i*p->maxFrameN + p->frameIdx + j ] = chA[i][j];

  p->frameIdx += frameN;

  return kOkRC;
}

cw::rc_t cw::dsp::recorder::write(   obj_t* p, const char* fname )
{
  file::handle_t h;
  cw::rc_t rc;
  
  if((rc = file::open(h,fname, file::kWriteFl )) != kOkRC )
  {
    rc = cwLogError(rc,"Recorder file open failed on '%s'.", cwStringNullGuard(fname));
    goto errLabel;
  }

  file::printf(h,"{\n");

  file::printf(h,"\"srate\":%f,\n",p->srate);
  file::printf(h,"\"maxFrameN\":%i,\n",p->frameIdx);

  for(unsigned i=0; i<p->chN; ++i)
  {
    file::printf(h,"\"%i\":[",i);

    for(unsigned j=0; j<p->frameIdx; ++j)
      file::printf(h,"%f%c\n",p->buf[ p->maxFrameN*i + j ], j+1==p->frameIdx ? ' ' : ',');
    
    file::printf(h,"]\n");
  }
  
  file::printf(h,"}\n");

 errLabel:

  if((rc = file::close(h)) != kOkRC )
  {
    rc = cwLogError(rc,"Recorder file close failed on '%s'.", cwStringNullGuard(fname));
    goto errLabel;
  }

  return rc;
}

//----------------------------------------------------------------------------------------------------------------
// Audio Meter
//
namespace cw {
  namespace dsp {
    namespace audio_meter {
      sample_t _sum_square( const sample_t* v, unsigned vn, bool& clipFlRef )
      {
	sample_t sum = 0;
	for(unsigned i=0; i<vn; ++i)
	{
	   sample_t x = v[i]*v[i];
	   sum += x;
	   clipFlRef = x > 1.0;
	   
	}
	return sum;
      }
    }
  }
}

cw::rc_t cw::dsp::audio_meter::create( obj_t*& p, srate_t srate, ftime_t maxWndMs, ftime_t wndMs, coeff_t peakThreshDb )
{
  rc_t rc = kOkRC;

  if( maxWndMs < wndMs )
  {
    cwLogWarning("Audio meter Max. window length (%f ms) is less than requested window length (%f ms). Setting max window length to %f ms.",maxWndMs,wndMs,wndMs);
    maxWndMs = wndMs;
  }
  
  p             = mem::allocZ<obj_t>();
  p->maxWndMs   = maxWndMs;
  p->maxWndSmpN = (unsigned)((maxWndMs * srate)/1000.0);
  p->wndV       = mem::allocZ<sample_t>(p->maxWndSmpN);
  p->srate      = srate;
  p->peakThreshDb = peakThreshDb;
  p->wi         = 0;

  set_window_ms( p, wndMs );
  reset(p);
  
  return rc;
}

cw::rc_t cw::dsp::audio_meter::destroy( obj_t*& pp )
{
  rc_t rc = kOkRC;
  mem::release(pp->wndV);
  mem::release(pp);
  return rc;
}

cw::rc_t cw::dsp::audio_meter::exec( obj_t* p, const sample_t* xV, unsigned xN )
{
  rc_t rc = kOkRC;
  unsigned n = 0;

  // copy the incoming audio samples to the buffer
  while( xN )
  {
    unsigned n0 = std::min( p->maxWndSmpN - p->wi, xN );
    
    vop::copy(p->wndV + p->wi, xV + n, n0 );
    
    n += n0;
    xN -= n0;

    p->wi = (p->wi + n0) % p->maxWndSmpN;
  }

  // get the starting and ending locations of the RMS sub-buffers
  
  unsigned i0 = 0, i1 = 0;
  unsigned n0 = 0, n1 = 0;
  
  if( p->wi >= p->wndSmpN )
  {
    i0 = p->wi - p->wndSmpN;
    n0 = p->wndSmpN;
  }
  else
  {
    i1 = 0;
    n1 = p->wi;
    n0 = p->wndSmpN - n1;
    i0 = p->maxWndSmpN - n0;
  }

  // calc the squared-sum of the RMS buffers
  sample_t sum = _sum_square(p->wndV + i0, n0, p->clipFl);
  if( n1 )
    sum += _sum_square(p->wndV + i1, n1, p->clipFl );

  p->outLin = std::sqrt( sum / (n0+n1) );  // linear RMS
  p->outDb  = ampl_to_db(p->outLin);           // RMS dB

  p->peakFl = p->outDb > p->peakThreshDb;   // set peak flag
  p->clipFl = vop::max(xV,xN) >= 1.0;       // set clip flag

  p->peakCnt += p->peakFl ? 1 : 0;          // count peak violations
  
  return rc;
}

void cw::dsp::audio_meter::reset( obj_t* p )
{
  p->peakFl  = false;
  p->clipFl  = false;
  p->peakCnt = 0;
  p->clipCnt = 0;
}

void cw::dsp::audio_meter::set_window_ms( obj_t* p, ftime_t wndMs )
{
  unsigned wndSmpN  = (unsigned)((wndMs * p->srate)/1000.0);

  if( wndSmpN <= p->maxWndSmpN )
    p->wndSmpN = wndSmpN;
  else
  {
    cwLogWarning("The audio meter window length (%f ms) exceeds the max. window length (%f ms). The window length was reduced to (%f ms).",wndMs,p->maxWndMs,p->maxWndMs);
    p->wndSmpN = p->maxWndSmpN;
  }
  
  
}
