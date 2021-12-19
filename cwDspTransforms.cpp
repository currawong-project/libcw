#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
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
      void _ms_to_samples( obj_t*p, real_t ms, unsigned& outRef )
      {
        outRef = std::max((real_t)1,(real_t)floor(ms * p->srate / 1000.0));
      }
    }
  }
}

cw::rc_t cw::dsp::compressor::create( obj_t*& p, real_t srate, unsigned procSmpCnt, real_t inGain, real_t rmsWndMaxMs, real_t rmsWndMs, real_t threshDb, real_t ratio_num, real_t atkMs, real_t rlsMs, real_t outGain, bool bypassFl )
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

  real_t rmsLin = vop::mean(p->rmsWnd,p->rmsWndCnt);                   // calc avg RMS
  real_t rmsDb  = std::max(-100.0,20 * log10(std::max((real_t)0.00001,rmsLin)));  // convert avg RMS to dB
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

    
void cw::dsp::compressor::set_attack_ms(  obj_t* p, real_t ms )
{
  _ms_to_samples(p,ms,p->atkSmp);
}
    
void cw::dsp::compressor::set_release_ms( obj_t* p, real_t ms )
{
  _ms_to_samples(p,ms,p->rlsSmp);
}
    
void cw::dsp::compressor::set_rms_wnd_ms( obj_t* p, real_t ms )
{
  p->rmsWndCnt = std::max((unsigned)1,(unsigned)floor(ms * p->srate / (1000.0 * p->procSmpCnt)));

  // do not allow rmsWndCnt to exceed rmsWndAllocCnt
  if( p->rmsWndCnt > p->rmsWndAllocCnt )
    p->rmsWndCnt = p->rmsWndAllocCnt;      
}



cw::rc_t cw::dsp::recorder::create(  obj_t*& pRef, real_t srate, real_t max_secs, unsigned chN )
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

  if( p != nullptr )
  {
    mem::release(p->buf);
    mem::release(p);
  }
  
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
