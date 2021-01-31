#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwTextBuf.h"
#include "cwAudioDevice.h"
#include "cwAudioBufDecls.h"
#include "cwAudioBuf.h"

/*
  This API is called by two types of threads:
  audio device threads and the client thread.  There
  may be multiple device threads, however, there is only
  one client thread.

  The audio device threads only call update().
  update() is never called by any other threads.
  A call from the audio update threads targets specific channels
  (cmApCh records).  The variables within each channels that
  it modifies are confined to:
  on input channels:   increments ii and increments fn (data is entering the ch. buffers)
  on output channels:  increments oi and decrements fn (data is leaving the ch. buffers)

  The client picks up incoming audio and provides outgoing audio via
  get(). It then informs the buf that it has completed
  the audio data transfer by calling advance().

  advance() modifies the following internal variables:
  on input channels:  increments oi and decrements fn (data has left the ch buffer)
  on output channels: increments ii and increments fn (data has enterned the ch. buffer)

  Based on the above scenario the channel ii and oi variables are always thread-safe
  because they are only changed by a single thread. 

  ii       oi     fn
  ------   -----  ----
  input  ch:  audio    client both
  output ch:  client   audio  both
  
  The fn variable however is not thread-safe and therefore care must be taken as
  to how it is read and updated.
  
  
  
*/

#define atomicUIntIncr( vRef, dVal ) std::atomic_fetch_add<unsigned>(vRef,dVal)
#define atomicUIntDecr( vRef, dVal ) std::atomic_fetch_sub<unsigned>(vRef,dVal)

namespace cw
{
  namespace audio
  {
    namespace buf
    {

      enum { kInApIdx=0, kOutApIdx=1, kIoApCnt=2 };

      typedef struct
      {
        unsigned  fl;   // kChFl|kToneFl|kMeterFl ...
        sample_t* b;    // b[n]
        unsigned  ii;   // next in
        unsigned  oi;   // next out
        std::atomic<unsigned>  fn;   // full cnt  - count of samples currently in the buffer - incr'd by incoming, decr'd by outgoing
        unsigned  phs;  // tone phase
        double    hz;   // tone frequency 
        double    gain; // channel gain
        sample_t* m;    // m[mn] meter sample sum  
        unsigned  mn;   // length of m[]
        unsigned  mi;   // next ele of m[] to rcv sum
      } cmApCh;

      typedef struct
      {
        unsigned              chCnt;
        cmApCh*               chArray;
        unsigned              n; // length of b[] (multiple of dspFrameCnt)  bufCnt*framesPerCycle
        double                srate; // device sample rate;
        unsigned              faultCnt;
        unsigned              framesPerCycle;
        unsigned              dspFrameCnt;
        time::spec_t          timeStamp; // base (starting) time stamp for this device
        std::atomic<unsigned> ioFrameCnt; // count of frames input or output for this device

      } cmApIO;

      typedef struct
      {
        // ioArray[] always contains 2 elements - one for input the other for output.
        cmApIO ioArray[kIoApCnt]; 
      } cmApDev;

      typedef struct audioBuf_str
      {
        cmApDev* devArray;       
        unsigned devCnt;
        unsigned meterMs;

        sample_t* zeroBuf;      // buffer of zeros 
        unsigned  zeroBufCnt;   // max of all dspFrameCnt for all devices.
      } audioBuf_t;

      inline audioBuf_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,audioBuf_t>(h); }


      sample_t _cmApMeterValue( const cmApCh* cp )
      {
        double   sum  = 0;
        unsigned i;
        for(i=0; i<cp->mn; ++i)
          sum        += cp->m[i];

        return cp->mn == 0 ? 0 : (sample_t)sqrt(sum/cp->mn);
      }

      void _cmApSine( cmApCh* cp, sample_t* b0, unsigned n0, sample_t* b1, unsigned n1, unsigned stride, float srate )
      {
        unsigned i;

        for(i=0; i<n0; ++i,++cp->phs)
          b0[i*stride] = (float)(cp->gain * sin( 2.0 * M_PI * cp->hz * cp->phs / srate ));

        for(i=0; i<n1; ++i,++cp->phs)
          b1[i*stride] = (float)(cp->gain * sin( 2.0 * M_PI * cp->hz * cp->phs / srate ));
      }

      sample_t _cmApMeter( const sample_t* b, unsigned bn, unsigned stride )
      {
        const sample_t* ep  = b + bn;
        sample_t        sum = 0;

        for(; b<ep; b += stride)
          sum         += *b * *b;

        return sum / bn;
      }

      void _cmApChFinalize( cmApCh* chPtr )
      {
        mem::release( chPtr->b );
        mem::release( chPtr->m );
      }

      // n = buf sample cnt mn=meter buf smp cnt
      void _cmApChInitialize( cmApCh* chPtr, unsigned n, unsigned mn )
      {
        _cmApChFinalize(chPtr);

        chPtr->b    = n==0 ? NULL : mem::allocZ<sample_t>( n );
        chPtr->ii   = 0;
        chPtr->oi   = 0;
        chPtr->fn   = 0;
        chPtr->fl   = (n!=0 ? kChFl : 0);
        chPtr->hz   = 1000;
        chPtr->gain = 1.0;
        chPtr->mn   = mn;
        chPtr->m    = mem::allocZ<sample_t>(mn);
        chPtr->mi   = 0;
      }

      void _cmApIoFinalize( cmApIO* ioPtr )
      {
        unsigned i;
        for(i=0; i<ioPtr->chCnt; ++i)
          _cmApChFinalize( ioPtr->chArray + i );

        mem::release(ioPtr->chArray);
        ioPtr->chCnt = 0;
        ioPtr->n     = 0;
      }

      void _cmApIoInitialize( cmApIO* ioPtr, double srate, unsigned framesPerCycle, unsigned chCnt, unsigned n, unsigned meterBufN, unsigned dspFrameCnt )
      {
        unsigned i;

        _cmApIoFinalize(ioPtr);

        n += (n % dspFrameCnt); // force buffer size to be a multiple of dspFrameCnt
  
        ioPtr->chArray           = chCnt==0 ? NULL : mem::allocZ<cmApCh>(chCnt );
        ioPtr->chCnt             = chCnt;
        ioPtr->n                 = n;
        ioPtr->faultCnt          = 0;
        ioPtr->framesPerCycle    = framesPerCycle;
        ioPtr->srate             = srate;
        ioPtr->dspFrameCnt       = dspFrameCnt;
        ioPtr->timeStamp.tv_sec  = 0;
        ioPtr->timeStamp.tv_nsec = 0;
        ioPtr->ioFrameCnt        = 0;

        for(i = 0; i<chCnt; ++i )
          _cmApChInitialize( ioPtr->chArray + i, n, meterBufN );

      }

      void _cmApDevFinalize( cmApDev* dp )
      {
        unsigned i;
        for(i = 0; i<kIoApCnt; ++i)
          _cmApIoFinalize( dp->ioArray+i);
      }

      void _cmApDevInitialize( cmApDev* dp, double srate, unsigned iFpC, unsigned iChCnt, unsigned iBufN, unsigned oFpC, unsigned oChCnt, unsigned oBufN, unsigned meterBufN, unsigned dspFrameCnt )
      {
        unsigned i;

        _cmApDevFinalize(dp);

        for(i = 0; i<kIoApCnt; ++i)
        {
          unsigned chCnt = i==kInApIdx ? iChCnt : oChCnt;
          unsigned bufN  = i==kInApIdx ? iBufN  : oBufN;
          unsigned fpc   = i==kInApIdx ? iFpC   : oFpC;
          _cmApIoInitialize( dp->ioArray+i, srate, fpc, chCnt, bufN, meterBufN, dspFrameCnt );

        }
      
      }

      void _setFlag( audioBuf_t* p, unsigned devIdx, unsigned chIdx, unsigned flags, unsigned ioIdx )
      {

        bool     enableFl = flags & kEnableFl ? true : false; 
        unsigned i        = chIdx != kInvalidIdx ? chIdx   : 0;
        unsigned n        = chIdx != kInvalidIdx ? chIdx+1 : p->devArray[devIdx].ioArray[ioIdx].chCnt;
  
        for(; i<n; ++i)
        {
          cmApCh*  cp  = p->devArray[devIdx].ioArray[ioIdx].chArray + i;
          cp->fl = cwEnaFlag(cp->fl, flags & (kChFl|kToneFl|kMeterFl|kMuteFl|kPassFl), enableFl );
    
        }
   
      }
      

      void _theBufCalcTimeStamp( double srate, const time::spec_t* baseTimeStamp, unsigned frmCnt, time::spec_t* retTimeStamp )
      {
        if( retTimeStamp == NULL )
          return;

        double   secs      = frmCnt / srate;
        unsigned int_secs  = floor(secs);
        double   frac_secs = secs - int_secs;

        retTimeStamp->tv_nsec = floor(baseTimeStamp->tv_nsec + frac_secs * 1000000000);
        retTimeStamp->tv_sec = baseTimeStamp->tv_sec  + int_secs;

        if( retTimeStamp->tv_nsec > 1000000000 )
        {
          retTimeStamp->tv_nsec -= 1000000000;
          retTimeStamp->tv_sec  += 1;
        }
      }


    }
  }
}

cw::rc_t cw::audio::buf::create( handle_t& hRef, unsigned devCnt, unsigned meterMs )
{
  rc_t rc;

  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  audioBuf_t* p = mem::allocZ<audioBuf_t>();
  
  p->devArray        = mem::allocZ<cmApDev>(devCnt );
  p->devCnt = devCnt;

  hRef.set(p);
  
  setMeterMs(hRef,meterMs);

  return kOkRC;
}

cw::rc_t cw::audio::buf::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return rc;

  audioBuf_t* p = _handleToPtr(hRef);
  
  unsigned i;
  for(i=0; i<p->devCnt; ++i)    
    _cmApDevFinalize(p->devArray + i);

  mem::release( p->devArray );
  mem::release( p->zeroBuf );
  
  p->devCnt          = 0;

  mem::release( p );
  
  return kOkRC;
}

cw::rc_t cw::audio::buf::setup(
  handle_t h,
  unsigned devIdx,
  double   srate,
  unsigned dspFrameCnt,
  unsigned bufCnt,
  unsigned inChCnt, 
  unsigned inFramesPerCycle,
  unsigned outChCnt, 
  unsigned outFramesPerCycle)
{
  audioBuf_t* p = _handleToPtr(h);
  cmApDev* devPtr    = p->devArray + devIdx;
  unsigned iBufN     = bufCnt * inFramesPerCycle;
  unsigned oBufN     = bufCnt * outFramesPerCycle;
  unsigned meterBufN = std::max(1.0,floor(srate * p->meterMs / (1000.0 * outFramesPerCycle)));

  _cmApDevInitialize( devPtr, srate, inFramesPerCycle, inChCnt, iBufN, outFramesPerCycle, outChCnt, oBufN, meterBufN, dspFrameCnt );

  if( inFramesPerCycle > p->zeroBufCnt || outFramesPerCycle > p->zeroBufCnt )
  {
    p->zeroBufCnt = std::max(inFramesPerCycle,outFramesPerCycle);
    p->zeroBuf    = mem::resizeZ<sample_t>(p->zeroBuf,p->zeroBufCnt);
  }

  return kOkRC;  
}

cw::rc_t cw::audio::buf::primeOutput( handle_t h, unsigned devIdx, unsigned audioCycleCnt )
{
  audioBuf_t* p = _handleToPtr(h);
  cmApIO*  iop = p->devArray[devIdx].ioArray + kOutApIdx;
  unsigned i;
  
  for(i=0; i<iop->chCnt; ++i)
  {
    cmApCh*  cp = iop->chArray + i;
    unsigned bn = iop->n * sizeof(sample_t);
    memset(cp->b,0,bn);
    cp->oi = 0;
    cp->ii = iop->framesPerCycle * audioCycleCnt;
    cp->fn = iop->framesPerCycle * audioCycleCnt;
  }

  return kOkRC;
}

void cw::audio::buf::onPortEnable( handle_t h, unsigned devIdx, bool enableFl )
{
  audioBuf_t* p = _handleToPtr(h);
  if( devIdx == kInvalidIdx || enableFl==false)
    return;
   
  cmApIO*  iop = p->devArray[devIdx].ioArray + kOutApIdx;
  iop->timeStamp.tv_sec = 0;
  iop->timeStamp.tv_nsec = 0;
  iop->ioFrameCnt = 0;

  iop = p->devArray[devIdx].ioArray + kInApIdx;
  iop->timeStamp.tv_sec = 0;
  iop->timeStamp.tv_nsec = 0;
  iop->ioFrameCnt = 0;


}

cw::rc_t cw::audio::buf::update(
  handle_t h,
  device::audioPacket_t* inPktArray, 
  unsigned               inPktCnt, 
  device::audioPacket_t* outPktArray, 
  unsigned               outPktCnt )
{
  unsigned i,j;
  audioBuf_t* p = _handleToPtr(h);

  // copy samples from the packet to the buffer
  if( inPktArray != NULL )
  {
    for(i=0; i<inPktCnt; ++i)
    {
      device::audioPacket_t* pp = inPktArray + i;           
      cmApIO*                ip = p->devArray[pp->devIdx].ioArray + kInApIdx; // dest io recd

      // if the base time stamp has not yet been set - then set it
      if( ip->timeStamp.tv_sec==0 && ip->timeStamp.tv_nsec==0 )
        ip->timeStamp = pp->timeStamp;

      // for each source packet channel and enabled dest channel
      for(j=0; j<pp->chCnt; ++j)
      {
        cmApCh*  cp = ip->chArray + pp->begChIdx +j; // dest ch
        unsigned n0 = ip->n - cp->ii;                // first dest segment
        unsigned n1 = 0;                             // second dest segment

        cwAssert(pp->begChIdx + j < ip->chCnt );

        // if the incoming samples  would overflow the buffer then ignore them
        if( cp->fn + pp->audioFramesCnt > ip->n )
        {
          ++ip->faultCnt; // record input overflow 
          continue;
        }

        // if the incoming samples would go off the end of the buffer then 
        // copy in the samples in two segments (one at the end and another at begin of dest channel)
        if( n0 < pp->audioFramesCnt )
          n1 = pp->audioFramesCnt-n0;
        else
          n0 = pp->audioFramesCnt;

        bool            enaFl = cwIsFlag(cp->fl,kChFl) && cwIsFlag(cp->fl,kMuteFl)==false;
        const sample_t* sp    = enaFl ? ((sample_t*)pp->audioBytesPtr) + j : p->zeroBuf;
        unsigned        ssn   = enaFl ? pp->chCnt : 1; // stride (packet samples are interleaved)
        sample_t*       dp    = cp->b + cp->ii;
        const sample_t* ep    = dp    + n0;


        // update the meter
        if( cwIsFlag(cp->fl,kMeterFl) )
        {
          cp->m[cp->mi] = _cmApMeter(sp,pp->audioFramesCnt,pp->chCnt);
          cp->mi = (cp->mi + 1) % cp->mn;
        }

        // if the test tone is enabled on this input channel
        if( enaFl && cwIsFlag(cp->fl,kToneFl) )
        {
          _cmApSine(cp, dp, n0, cp->b, n1, 1, ip->srate );
        }
        else // otherwise copy samples from the packet to the buffer
        {
          // copy the first segment
          for(; dp < ep; sp += ssn )
            *dp++ = cp->gain * *sp;

          // if there is a second segment
          if( n1 > 0 )
          {
            // copy the second segment
            dp = cp->b;
            ep = dp + n1;
            for(; dp<ep; sp += ssn )
              *dp++ = cp->gain * *sp;
          }
        }

        
        // advance the input channel buffer
        cp->ii  = n1>0 ? n1 : cp->ii + n0;
        //cp->fn += pp->audioFramesCnt;
        atomicUIntIncr(&cp->fn,pp->audioFramesCnt);

      }
    }
  }

  // copy samples from the buffer to the packet
  if( outPktArray != NULL )
  {
    for(i=0; i<outPktCnt; ++i)
    {
      device::audioPacket_t* pp = outPktArray + i;           
      cmApIO*                op = p->devArray[pp->devIdx].ioArray + kOutApIdx; // dest io recd

      // if the base timestamp has not yet been set then set it.
      if( op->timeStamp.tv_sec==0 && op->timeStamp.tv_nsec==0 )
        op->timeStamp = pp->timeStamp;

      // for each dest packet channel and enabled source channel
      for(j=0; j<pp->chCnt; ++j)
      {
        cmApCh*           cp = op->chArray + pp->begChIdx + j; // dest ch
        unsigned          n0 = op->n - cp->oi;                 // first src segment
        unsigned          n1 = 0;                              // second src segment
        volatile unsigned fn = cp->fn;                         // store fn because it may be changed by the client thread

        // if the outgoing samples  will underflow the buffer 
        if( pp->audioFramesCnt > fn )
        {
          ++op->faultCnt;         // record an output underflow

          // if the buffer is empty - zero the packet and return
          if( fn == 0 )
          {
            memset( pp->audioBytesPtr, 0, pp->audioFramesCnt*sizeof(sample_t));
            continue;
          }

          // ... otherwise decrease the count of returned samples
          pp->audioFramesCnt = fn;

        }

        // if the outgong segments would go off the end of the buffer then 
        // arrange to wrap to the begining of the buffer
        if( n0 < pp->audioFramesCnt )
          n1 = pp->audioFramesCnt-n0;
        else
          n0 = pp->audioFramesCnt;

        sample_t* dp    = ((sample_t*)pp->audioBytesPtr) + j;
        bool      enaFl = cwIsFlag(cp->fl,kChFl) && cwIsFlag(cp->fl,kMuteFl)==false;

        // if the tone is enabled on this channel
        if( enaFl && cwIsFlag(cp->fl,kToneFl) )
        {
          _cmApSine(cp, dp, n0, dp + n0*pp->chCnt, n1, pp->chCnt, op->srate );
        }
        else                    // otherwise copy samples from the output buffer to the packet
        {
          const sample_t* sp = enaFl ? cp->b + cp->oi : p->zeroBuf;
          const sample_t* ep = sp + n0;

          // copy the first segment
          for(; sp < ep; dp += pp->chCnt )
            *dp = cp->gain * *sp++;
          
          // if there is a second segment
          if( n1 > 0 )
          {
            // copy the second segment
            sp    = enaFl ? cp->b : p->zeroBuf;
            ep    = sp + n1;
            for(; sp<ep; dp += pp->chCnt )
              *dp = cp->gain * *sp++;

          }
        }

        // update the meter
        if( cwIsFlag(cp->fl,kMeterFl) )
        {
          cp->m[cp->mi] = _cmApMeter(((sample_t*)pp->audioBytesPtr)+j,pp->audioFramesCnt,pp->chCnt);
          cp->mi        = (cp->mi + 1) % cp->mn;
        }

        // advance the output channel buffer
        cp->oi  = n1>0 ? n1 : cp->oi + n0;
        //cp->fn -= pp->audioFramesCnt;
        atomicUIntDecr(&cp->fn,pp->audioFramesCnt);

      }
    }    
  }
  return kOkRC;
}

unsigned cw::audio::buf::meterMs( handle_t h )
{
  audioBuf_t* p = _handleToPtr(h);
  return p->meterMs;
}

void     cw::audio::buf::setMeterMs( handle_t h, unsigned meterMs )
{
  audioBuf_t* p = _handleToPtr(h);

  p->meterMs = std::min(1000u,std::max(10u,meterMs));
}

unsigned cw::audio::buf::channelCount( handle_t h, unsigned devIdx, unsigned flags )
{
  
  if( devIdx == kInvalidIdx )
    return 0;
  audioBuf_t* p = _handleToPtr(h);

  unsigned idx      = flags & kInFl     ? kInApIdx : kOutApIdx;
  return p->devArray[devIdx].ioArray[ idx ].chCnt; 
}

void cw::audio::buf::setFlag( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return;

  audioBuf_t* p = _handleToPtr(h);

  if( flags & kInFl )
    _setFlag( p, devIdx, chIdx, flags, kInApIdx );

  if( flags & kOutFl )
    _setFlag( p, devIdx, chIdx, flags, kOutApIdx );
  
}  

bool cw::audio::buf::isFlag( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return false;

  audioBuf_t* p = _handleToPtr(h);
  unsigned idx      = flags & kInFl ? kInApIdx : kOutApIdx;
  return cwIsFlag(p->devArray[devIdx].ioArray[idx].chArray[chIdx].fl,flags);
}


void  cw::audio::buf::enableChannel(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{  setFlag(h,devIdx,chIdx,flags | kChFl); }

bool  cw::audio::buf::isChannelEnabled(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(h,devIdx, chIdx, flags | kChFl); }

void  cw::audio::buf::enableTone(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(h,devIdx,chIdx,flags | kToneFl); }

bool  cw::audio::buf::isToneEnabled(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(h,devIdx,chIdx,flags | kToneFl); }

void  cw::audio::buf::toneFlags(    handle_t h, unsigned devIdx, unsigned flags, bool* enableFlA, unsigned chCnt )
{
  chCnt = std::min( chCnt, channelCount(h,devIdx,flags));
  
  for(unsigned i=0; i<chCnt; ++i)
    enableFlA[i] = isFlag(h, devIdx, i, flags | kToneFl); 
}


void  cw::audio::buf::enableMute(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(h,devIdx,chIdx,flags | kMuteFl); }

bool  cw::audio::buf::isMuteEnabled(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(h,devIdx,chIdx,flags | kMuteFl); }

void  cw::audio::buf::muteFlags(    handle_t h, unsigned devIdx, unsigned flags, bool* muteFlA, unsigned chCnt )
{
  chCnt = std::min( chCnt, channelCount(h,devIdx,flags));
  
  for(unsigned i=0; i<chCnt; ++i)
    muteFlA[i] = isFlag(h, devIdx, i, flags | kToneFl); 
}

void  cw::audio::buf::enablePass(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(h,devIdx,chIdx,flags | kPassFl); }

bool  cw::audio::buf::isPassEnabled(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(h,devIdx,chIdx,flags | kPassFl); }

void  cw::audio::buf::enableMeter(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(h,devIdx,chIdx,flags | kMeterFl); }

bool  cw::audio::buf::isMeterEnabled(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(h,devIdx,chIdx,flags | kMeterFl); }

cw::audio::buf::sample_t cw::audio::buf::meter(handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return 0;

  audioBuf_t* p = _handleToPtr(h);
  
  unsigned            idx = flags & kInFl  ? kInApIdx : kOutApIdx;
  const cmApCh*       cp  = p->devArray[devIdx].ioArray[idx].chArray + chIdx;
  return _cmApMeterValue(cp);  
}

void  cw::audio::buf::meter(handle_t h, unsigned devIdx, unsigned flags, sample_t* meterA, unsigned meterN )
{
  if( devIdx == kInvalidIdx )
    return;
  
  audioBuf_t* p   = _handleToPtr(h);
  unsigned    idx = flags & kInFl ? kInApIdx : kOutApIdx;
  unsigned    n   = std::min(meterN,channelCount(h,devIdx,flags));

  for(unsigned i=0; i<n; ++i)
  {
    const cmApCh*  cp  = p->devArray[devIdx].ioArray[idx].chArray + i;
    meterA[i] = _cmApMeterValue(cp);
  }
  
}


void cw::audio::buf::setGain( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags, double gain )
{
  if( devIdx == kInvalidIdx )
    return;
  audioBuf_t* p = _handleToPtr(h);

  unsigned idx      = flags & kInFl           ? kInApIdx : kOutApIdx;
  unsigned i        = chIdx != kInvalidIdx      ? chIdx : 0;
  unsigned n        = i + (chIdx != kInvalidIdx ? 1     : p->devArray[devIdx].ioArray[idx].chCnt);

  for(; i<n; ++i)
    p->devArray[devIdx].ioArray[idx].chArray[i].gain = gain;
}

double cw::audio::buf::gain( handle_t h, unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return 0;
  
  audioBuf_t* p = _handleToPtr(h);

  unsigned idx   = flags & kInFl  ? kInApIdx : kOutApIdx;
  return  p->devArray[devIdx].ioArray[idx].chArray[chIdx].gain;  
}

void   cw::audio::buf::gain( handle_t h, unsigned devIdx, unsigned flags, double* gainA, unsigned gainN )
{
  if( devIdx == kInvalidIdx )
    return;
  
  audioBuf_t* p   = _handleToPtr(h);
  unsigned    idx = flags & kInFl  ? kInApIdx : kOutApIdx;
  unsigned    n   = std::min(gainN,channelCount(h,devIdx,flags));

  for(unsigned i=0; i<n; ++i)
    gainA[i] = p->devArray[devIdx].ioArray[idx].chArray[i].gain;

}


unsigned cw::audio::buf::getStatus( handle_t h, unsigned devIdx, unsigned flags, double* meterArray, unsigned meterCnt, unsigned* faultCntPtr )
{
  if( devIdx == kInvalidIdx )
    return 0;

  audioBuf_t* p = _handleToPtr(h);
  unsigned ioIdx = cwIsFlag(flags,kInFl) ? kInApIdx : kOutApIdx;
  cmApIO*  iop   = p->devArray[devIdx].ioArray + ioIdx;
  unsigned chCnt = std::min(iop->chCnt, meterCnt );
  unsigned i;

  if( faultCntPtr != NULL )
    *faultCntPtr = iop->faultCnt;

  for(i=0; i<chCnt; ++i)
    meterArray[i] = _cmApMeterValue(iop->chArray + i);        
  return chCnt;
}


bool cw::audio::buf::isDeviceReady( handle_t h, unsigned devIdx, unsigned flags )
{  
  //bool     iFl = true;
  //bool     oFl = true;
  unsigned i   = 0;

  if( devIdx == kInvalidIdx )
    return false;
  audioBuf_t* p = _handleToPtr(h);

  if( flags & kInFl )
  {
    const cmApIO* ioPtr = p->devArray[devIdx].ioArray + kInApIdx;
    for(i=0; i<ioPtr->chCnt; ++i)
      if( ioPtr->chArray[i].fn < ioPtr->dspFrameCnt )
        return false;

    //iFl = ioPtr->fn > ioPtr->dspFrameCnt;
  }

  if( flags & kOutFl )
  {
    const cmApIO* ioPtr = p->devArray[devIdx].ioArray + kOutApIdx;

    for(i=0; i<ioPtr->chCnt; ++i)
      if( (ioPtr->n - ioPtr->chArray[i].fn) < ioPtr->dspFrameCnt )
        return false;


    //oFl = (ioPtr->n - ioPtr->fn) > ioPtr->dspFrameCnt;
  } 

  return true;
  //return iFl & oFl;  
}


// Note that his function returns audio samples but does NOT
// change any internal states.
void cw::audio::buf::get( handle_t h, unsigned devIdx, unsigned flags, sample_t* bufArray[], unsigned bufChCnt )
{
  audioBuf_t* p = _handleToPtr(h);
  unsigned i;
  if( devIdx == kInvalidIdx )
  {
    for(i=0; i<bufChCnt; ++i)
      bufArray[i] = NULL;
    return;
  }

  unsigned      idx   = flags & kInFl ? kInApIdx : kOutApIdx;
  const cmApIO* ioPtr = p->devArray[devIdx].ioArray + idx;
  unsigned      n     = bufChCnt < ioPtr->chCnt ? bufChCnt : ioPtr->chCnt;
  //unsigned      offs  = flags & kInFl ? ioPtr->oi : ioPtr->ii; 
  cmApCh*       cp    = ioPtr->chArray;

  for(i=0; i<n; ++i,++cp)
  {
    unsigned offs = flags & kInFl ? cp->oi : cp->ii;
    bufArray[i] = cwIsFlag(cp->fl,kChFl) ? cp->b + offs : NULL;
  }
  
}

void cw::audio::buf::getIO( handle_t h, unsigned iDevIdx, sample_t* iBufArray[], unsigned iBufChCnt, time::spec_t* iTimeStampPtr, unsigned oDevIdx, sample_t* oBufArray[], unsigned oBufChCnt, time::spec_t* oTimeStampPtr )
{
  audioBuf_t* p = _handleToPtr(h);
  get( h, iDevIdx, kInFl, iBufArray, iBufChCnt );
  get( h, oDevIdx, kOutFl,oBufArray, oBufChCnt );

  unsigned i       = 0;

  if( iDevIdx != kInvalidIdx && oDevIdx != kInvalidIdx )
  {
    const cmApIO* ip       = p->devArray[iDevIdx].ioArray + kInApIdx;
    const cmApIO* op       = p->devArray[oDevIdx].ioArray + kOutApIdx;
    unsigned      minChCnt = std::min(iBufChCnt,oBufChCnt);  
    unsigned      frmCnt   = std::min(ip->dspFrameCnt,op->dspFrameCnt);
    unsigned      byteCnt  = frmCnt * sizeof(sample_t);
    
    _theBufCalcTimeStamp(ip->srate, &ip->timeStamp, ip->ioFrameCnt, iTimeStampPtr );
    _theBufCalcTimeStamp(op->srate, &op->timeStamp, op->ioFrameCnt, oTimeStampPtr );

    for(i=0; i<minChCnt; ++i)
    {
      cmApCh* ocp = op->chArray + i;
      cmApCh* icp = ip->chArray + i;

      if( oBufArray[i] != NULL )
      {
        // if either the input or output channel is marked for pass-through
        if( cwAllFlags(ocp->fl,kPassFl)  || cwAllFlags(icp->fl,kPassFl) )
        {
          memcpy( oBufArray[i], iBufArray[i], byteCnt );

          // set the output buffer to NULL to prevent it being over written by the client
          oBufArray[i] = NULL;
        }
        else
        {
          // zero the output buffer
          memset(oBufArray[i],0,byteCnt);
        }
      }
    }
  }

  if( oDevIdx != kInvalidIdx )
  {
    const cmApIO* op  = p->devArray[oDevIdx].ioArray + kOutApIdx;
    unsigned byteCnt  = op->dspFrameCnt * sizeof(sample_t);

    _theBufCalcTimeStamp(op->srate, &op->timeStamp, op->ioFrameCnt, oTimeStampPtr );

    for(; i<oBufChCnt; ++i)
      if( oBufArray[i] != NULL )
        memset( oBufArray[i], 0, byteCnt );
  }
}

void cw::audio::buf::advance( handle_t h, unsigned devIdx, unsigned flags )
{
  unsigned i;

  if( devIdx == kInvalidIdx )
    return;
  audioBuf_t* p = _handleToPtr(h);

  if( flags & kInFl )
  {
    cmApIO* ioPtr = p->devArray[devIdx].ioArray + kInApIdx;

    for(i=0; i<ioPtr->chCnt; ++i)
    {
      cmApCh* cp = ioPtr->chArray + i;
      cp->oi     = (cp->oi + ioPtr->dspFrameCnt) % ioPtr->n;
      atomicUIntDecr(&cp->fn,ioPtr->dspFrameCnt);
    }

    // count the number of samples input from this device
    if( ioPtr->timeStamp.tv_sec!=0 && ioPtr->timeStamp.tv_nsec!=0 )
    {
      atomicUIntIncr(&ioPtr->ioFrameCnt,ioPtr->dspFrameCnt);
    }
  }
  
  if( flags & kOutFl )
  {
    cmApIO* ioPtr = p->devArray[devIdx].ioArray + kOutApIdx;
    for(i=0; i<ioPtr->chCnt; ++i)
    {
      cmApCh* cp = ioPtr->chArray + i;
      cp->ii     = (cp->ii + ioPtr->dspFrameCnt) % ioPtr->n;
      atomicUIntIncr(&cp->fn,ioPtr->dspFrameCnt);
    }

    // count the number of samples output from this device
    if( ioPtr->timeStamp.tv_sec!=0 && ioPtr->timeStamp.tv_nsec!=0 )
    {
      atomicUIntIncr(&ioPtr->ioFrameCnt,ioPtr->dspFrameCnt);
    }
  }
}


void cw::audio::buf::inputToOutput( handle_t h, unsigned iDevIdx, unsigned oDevIdx )
{
  if( iDevIdx == kInvalidIdx || oDevIdx == kInvalidIdx )
    return;
  audioBuf_t* p = _handleToPtr(h);

  unsigned    iChCnt   = channelCount( h, iDevIdx, kInFl  );
  unsigned    oChCnt   = channelCount( h, oDevIdx, kOutFl );
  unsigned    chCnt    = iChCnt < oChCnt ? iChCnt : oChCnt;
  unsigned    i;

  if( chCnt == 0 )
  {
    cwLogWarning("Both input and output devices must have a non-zero channel count in the call to audio::buf::inputToOutput().");
    return;
  }
  

  sample_t* iBufPtrArray[ iChCnt ];
  sample_t* oBufPtrArray[ oChCnt ];


  while( isDeviceReady( h, iDevIdx, kInFl ) && isDeviceReady( h, oDevIdx, kOutFl ) )
  {
    get( h, iDevIdx, kInFl,  iBufPtrArray, iChCnt );
    get( h, oDevIdx, kOutFl, oBufPtrArray, oChCnt );

    // Warning: buffer pointers to disabled channels will be set to NULL

    for(i=0; i<chCnt; ++i)
    {
      cmApIO* ip = p->devArray[iDevIdx].ioArray + kInApIdx;
      cmApIO* op = p->devArray[oDevIdx].ioArray + kOutApIdx;

      cwAssert( ip->dspFrameCnt == op->dspFrameCnt );

      unsigned    byteCnt  = ip->dspFrameCnt * sizeof(sample_t);

      if( oBufPtrArray[i] != NULL )
      {      
        // the input channel is not disabled
        if( iBufPtrArray[i]!=NULL )
          memcpy(oBufPtrArray[i],iBufPtrArray[i],byteCnt);    
        else
          // the input channel is disabled but the output is not - so fill the output with zeros
          memset(oBufPtrArray[i],0,byteCnt);
      }
    }

    advance( h, iDevIdx, kInFl );
    advance( h, oDevIdx, kOutFl );
  }
  
}

void cw::audio::buf::report(handle_t h)
{
  audioBuf_t* p = _handleToPtr(h);
  unsigned i,j,k;
  for(i=0; i<p->devCnt; ++i)
  {
    for(j=0; j<kIoApCnt; ++j)
    {
      cmApIO* ip = p->devArray[i].ioArray + j;

      unsigned ii = 0;
      unsigned oi = 0;
      unsigned fn  = 0;
      sample_t m   = 0;
      for(k=0; k<ip->chCnt; ++k)
      {
        cmApCh* cp = ip->chArray + k;
        ii += cp->ii;
        oi += cp->oi;
        fn += cp->fn;
        m += _cmApMeterValue(cp);
      }
      

      cwLogInfo("%i :  %s - i:%7i o:%7i f:%7i n:%7i err %s:%7i meter:%f",
        i,j==0?"IN ":"OUT",
        ii,oi,fn,ip->n, (j==0?"over ":"under"), ip->faultCnt, m/ip->chCnt);

    }
  }
}

/// [cwAudioBufExample]

cw::rc_t cw::audio::buf::test()
{
  rc_t     rc             = kOkRC;
  unsigned devIdx         = 0;
  unsigned devCnt         = 1 ;
  unsigned dspFrameCnt    = 10;
  unsigned cycleCnt       = 3;
  unsigned framesPerCycle = 25;
  unsigned inChCnt        = 2;
  unsigned outChCnt       = inChCnt;
  unsigned sigN           = cycleCnt*framesPerCycle*inChCnt;
  double   srate          = 44100.0;
  unsigned meterMs        = 50;

  unsigned              bufChCnt = inChCnt;
  sample_t*             inBufArray[ bufChCnt ];
  sample_t*             outBufArray[ bufChCnt ];
  sample_t              iSig[ sigN ];
  sample_t              oSig[ sigN ];
  sample_t*             os       = oSig;
  device::audioPacket_t pkt;
  unsigned              i,j;
  handle_t              h;
  
  // create a simulated signal
  for(i=0; i<sigN; ++i)
  {
    iSig[i] = i;
    oSig[i] = 0;
  }

  pkt.devIdx         = 0;
  pkt.begChIdx       = 0;
  pkt.chCnt          = inChCnt;
  pkt.audioFramesCnt = framesPerCycle;
  pkt.bitsPerSample  = 32;
  pkt.flags          = 0;
  time::get(pkt.timeStamp);
 

  // initialize a the audio buffer 
  create(h,devCnt,meterMs);

  // setup the buffer with the specific parameters to by used by the host audio ports
  setup(h,devIdx,srate,dspFrameCnt,cycleCnt,inChCnt,framesPerCycle,outChCnt,framesPerCycle);

  // simulate cylcing through sigN buffer transactions
  for(i=0; i<sigN; i+=framesPerCycle*inChCnt)
  {
    // setup an incoming audio packet
    pkt.audioFramesCnt = framesPerCycle;
    pkt.audioBytesPtr  = iSig+i;

    // simulate a call from the audio port with incoming samples 
    // (fill the audio buffers internal input buffers)
    update(h,&pkt,1,NULL,0);

    // if all devices need to be serviced
    while( isDeviceReady( h, devIdx, kInFl | kOutFl ))
    {
      // get pointers to full internal input buffers
      get(h, devIdx, kInFl, inBufArray, bufChCnt );
      
      // get pointers to empty internal output buffers
      get(h, devIdx, kOutFl, outBufArray, bufChCnt );

      // Warning: pointers to disabled channels will be set to NULL.

      // simulate a play through by copying the incoming samples to the outgoing buffers.
      for(j=0; j<bufChCnt; ++j)
        if( outBufArray[j] != NULL )
        {
          // if the input is disabled - but the output is not then zero the output buffer
          if( inBufArray[j] == NULL )
            memset( outBufArray[j], 0, dspFrameCnt*sizeof(sample_t));
          else
            // copy the input to the output
            memcpy( outBufArray[j], inBufArray[j], dspFrameCnt*sizeof(sample_t));
        }

      // signal the buffer that this cycle is complete.
      // (marks current internal input/output buffer empty/full)
      advance( h, devIdx, kInFl | kOutFl );
    }

    pkt.audioBytesPtr = os;
    
    // simulate a call from the audio port picking up outgoing samples
    // (empties the audio buffers internal output buffers)
    update(h,NULL,0,&pkt,1);

    os += pkt.audioFramesCnt * pkt.chCnt;
  }

  for(i=0; i<sigN; ++i)
    cwLogInfo("%f ",oSig[i]);
  cwLogInfo("\n");

  destroy(h);
  
  return rc;
}

/// [cwAudioBufExample]


