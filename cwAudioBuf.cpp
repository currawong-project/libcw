#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwTextBuf.h"
#include "cwAudioDevice.h"
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
        unsigned              n;          // length of b[] (multiple of dspFrameCnt)  bufCnt*framesPerCycle
        double                srate;      // device sample rate;
        unsigned              faultCnt;
        unsigned              framesPerCycle;
        unsigned              dspFrameCnt;
        time::spec_t          timeStamp;  // base (starting) time stamp for this device
        std::atomic<unsigned> ioFrameCnt; // count of frames input or output for this device

      } cmApIO;

      typedef struct
      {
        // ioArray[] always contains 2 elements - one for input the other for output.
        cmApIO     ioArray[kIoApCnt]; 
      } cmApDev;

      typedef struct
      {
        cmApDev*   devArray;       
        unsigned   devCnt;
        unsigned   meterMs;

        sample_t* zeroBuf;      // buffer of zeros 
        unsigned  zeroBufCnt;   // max of all dspFrameCnt for all devices.
      } cmApBuf;

      cmApBuf _theBuf;


      sample_t _cmApMeterValue( const cmApCh* cp )
      {
        double sum = 0;
        unsigned i;
        for(i=0; i<cp->mn; ++i)
          sum += cp->m[i];

        return cp->mn==0 ? 0 : (sample_t)sqrt(sum/cp->mn);
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

        for(; b<ep; b+=stride)
          sum += *b * *b;

        return sum / bn;
      }

      void _cmApChFinalize( cmApCh* chPtr )
      {
        memRelease( chPtr->b );
        memRelease( chPtr->m );
      }

      // n=buf sample cnt mn=meter buf smp cnt
      void _cmApChInitialize( cmApCh* chPtr, unsigned n, unsigned mn )
      {
        _cmApChFinalize(chPtr);

        chPtr->b    = n==0 ? NULL : memAllocZ<sample_t>( n );
        chPtr->ii   = 0;
        chPtr->oi   = 0;
        chPtr->fn   = 0;
        chPtr->fl   = (n!=0 ? kChFl : 0);
        chPtr->hz   = 1000;
        chPtr->gain = 1.0;
        chPtr->mn   = mn;
        chPtr->m    = memAllocZ<sample_t>(mn);
        chPtr->mi   = 0;
      }

      void _cmApIoFinalize( cmApIO* ioPtr )
      {
        unsigned i;
        for(i=0; i<ioPtr->chCnt; ++i)
          _cmApChFinalize( ioPtr->chArray + i );

        memRelease(ioPtr->chArray);
        ioPtr->chCnt = 0;
        ioPtr->n     = 0;
      }

      void _cmApIoInitialize( cmApIO* ioPtr, double srate, unsigned framesPerCycle, unsigned chCnt, unsigned n, unsigned meterBufN, unsigned dspFrameCnt )
      {
        unsigned i;

        _cmApIoFinalize(ioPtr);

        n += (n % dspFrameCnt); // force buffer size to be a multiple of dspFrameCnt
  
        ioPtr->chArray           = chCnt==0 ? NULL : memAllocZ<cmApCh>(chCnt );
        ioPtr->chCnt             = chCnt;
        ioPtr->n                 = n;
        ioPtr->faultCnt          = 0;
        ioPtr->framesPerCycle    = framesPerCycle;
        ioPtr->srate             = srate;
        ioPtr->dspFrameCnt       = dspFrameCnt;
        ioPtr->timeStamp.tv_sec  = 0;
        ioPtr->timeStamp.tv_nsec = 0;
        ioPtr->ioFrameCnt        = 0;

        for(i=0; i<chCnt; ++i )
          _cmApChInitialize( ioPtr->chArray + i, n, meterBufN );

      }

      void _cmApDevFinalize( cmApDev* dp )
      {
        unsigned i;
        for(i=0; i<kIoApCnt; ++i)
          _cmApIoFinalize( dp->ioArray+i);
      }

      void _cmApDevInitialize( cmApDev* dp, double srate, unsigned iFpC, unsigned iChCnt, unsigned iBufN, unsigned oFpC, unsigned oChCnt, unsigned oBufN, unsigned meterBufN, unsigned dspFrameCnt )
      {
        unsigned i;

        _cmApDevFinalize(dp);

        for(i=0; i<kIoApCnt; ++i)
        {
          unsigned chCnt = i==kInApIdx ? iChCnt : oChCnt;
          unsigned bufN  = i==kInApIdx ? iBufN  : oBufN;
          unsigned fpc   = i==kInApIdx ? iFpC   : oFpC;
          _cmApIoInitialize( dp->ioArray+i, srate, fpc, chCnt, bufN, meterBufN, dspFrameCnt );

        }
      
      } 

void _theBufCalcTimeStamp( double srate, const time::spec_t* baseTimeStamp, unsigned frmCnt, time::spec_t* retTimeStamp )
{
  if( retTimeStamp==NULL )
    return;

  double   secs         = frmCnt / srate;
  unsigned int_secs     = floor(secs);
  double   frac_secs    = secs - int_secs;

  retTimeStamp->tv_nsec = floor(baseTimeStamp->tv_nsec + frac_secs * 1000000000);
  retTimeStamp->tv_sec  = baseTimeStamp->tv_sec  + int_secs;

  if( retTimeStamp->tv_nsec > 1000000000 )
  {
    retTimeStamp->tv_nsec -= 1000000000;
    retTimeStamp->tv_sec  += 1;
  }
}


    }
  }
}

cw::rc_t cw::audio::buf::initialize( unsigned devCnt, unsigned meterMs )
{
  rc_t rc;

  if((rc = finalize()) != kOkRC )
    return rc;

  _theBuf.devArray        = memAllocZ<cmApDev>(devCnt );
  _theBuf.devCnt          = devCnt;
  setMeterMs(meterMs);
  return kOkRC;
}

cw::rc_t cw::audio::buf::finalize()
{
  unsigned i;
  for(i=0; i<_theBuf.devCnt; ++i)    
    _cmApDevFinalize(_theBuf.devArray + i);

  memRelease( _theBuf.devArray );
  memRelease( _theBuf.zeroBuf );
  
  _theBuf.devCnt          = 0;

  return kOkRC;
}

cw::rc_t cw::audio::buf::setup( 
  unsigned devIdx,
  double   srate,
  unsigned dspFrameCnt,
  unsigned bufCnt,
  unsigned inChCnt, 
  unsigned inFramesPerCycle,
  unsigned outChCnt, 
  unsigned outFramesPerCycle)
{
  cmApDev* devPtr    = _theBuf.devArray + devIdx;
  unsigned iBufN     = bufCnt * inFramesPerCycle;
  unsigned oBufN     = bufCnt * outFramesPerCycle;
  unsigned meterBufN = std::max(1.0,floor(srate * _theBuf.meterMs / (1000.0 * outFramesPerCycle)));

  _cmApDevInitialize( devPtr, srate, inFramesPerCycle, inChCnt, iBufN, outFramesPerCycle, outChCnt, oBufN, meterBufN, dspFrameCnt );

  if( inFramesPerCycle > _theBuf.zeroBufCnt || outFramesPerCycle > _theBuf.zeroBufCnt )
  {
    _theBuf.zeroBufCnt = std::max(inFramesPerCycle,outFramesPerCycle);
    _theBuf.zeroBuf    = memResizeZ<sample_t>(_theBuf.zeroBuf,_theBuf.zeroBufCnt);
  }

  return kOkRC;  
}

cw::rc_t cw::audio::buf::primeOutput( unsigned devIdx, unsigned audioCycleCnt )
{
  cmApIO*  iop = _theBuf.devArray[devIdx].ioArray + kOutApIdx;
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

void cw::audio::buf::onPortEnable( unsigned devIdx, bool enableFl )
{
  if( devIdx == kInvalidIdx || enableFl==false)
    return;
   
  cmApIO*  iop = _theBuf.devArray[devIdx].ioArray + kOutApIdx;
  iop->timeStamp.tv_sec = 0;
  iop->timeStamp.tv_nsec = 0;
  iop->ioFrameCnt = 0;

  iop = _theBuf.devArray[devIdx].ioArray + kInApIdx;
  iop->timeStamp.tv_sec = 0;
  iop->timeStamp.tv_nsec = 0;
  iop->ioFrameCnt = 0;


}

cw::rc_t cw::audio::buf::update(
  device::audioPacket_t* inPktArray, 
  unsigned               inPktCnt, 
  device::audioPacket_t* outPktArray, 
  unsigned               outPktCnt )
{
  unsigned i,j;

  // copy samples from the packet to the buffer
  if( inPktArray != NULL )
  {
    for(i=0; i<inPktCnt; ++i)
    {
      device::audioPacket_t* pp = inPktArray + i;           
      cmApIO*                ip = _theBuf.devArray[pp->devIdx].ioArray + kInApIdx; // dest io recd

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
        const sample_t* sp    = enaFl ? ((sample_t*)pp->audioBytesPtr) + j : _theBuf.zeroBuf;
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
      cmApIO*                op = _theBuf.devArray[pp->devIdx].ioArray + kOutApIdx; // dest io recd

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
          const sample_t* sp = enaFl ? cp->b + cp->oi : _theBuf.zeroBuf;
          const sample_t* ep = sp + n0;

          // copy the first segment
          for(; sp < ep; dp += pp->chCnt )
            *dp = cp->gain * *sp++;
          
          // if there is a second segment
          if( n1 > 0 )
          {
            // copy the second segment
            sp    = enaFl ? cp->b : _theBuf.zeroBuf;
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

unsigned cw::audio::buf::meterMs()
{ return _theBuf.meterMs; }

void     cw::audio::buf::setMeterMs( unsigned meterMs )
{ _theBuf.meterMs = std::min(1000u,std::max(10u,meterMs)); }

unsigned cw::audio::buf::channelCount( unsigned devIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return 0;

  unsigned idx      = flags & kInFl     ? kInApIdx : kOutApIdx;
  return _theBuf.devArray[devIdx].ioArray[ idx ].chCnt; 
}

void cw::audio::buf::setFlag( unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return;

  unsigned idx      = flags & kInFl     ? kInApIdx : kOutApIdx;
  bool     enableFl = flags & kEnableFl ? true : false; 
  unsigned i        = chIdx != kInvalidIdx ? chIdx   : 0;
  unsigned n        = chIdx != kInvalidIdx ? chIdx+1 : _theBuf.devArray[devIdx].ioArray[idx].chCnt;
  
  for(; i<n; ++i)
  {
    cmApCh*  cp  = _theBuf.devArray[devIdx].ioArray[idx].chArray + i;
    cp->fl = cwEnaFlag(cp->fl, flags & (kChFl|kToneFl|kMeterFl|kMuteFl|kPassFl), enableFl );
  }
  
}  

bool cw::audio::buf::isFlag( unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return false;

  unsigned idx      = flags & kInFl ? kInApIdx : kOutApIdx;
  return cwIsFlag(_theBuf.devArray[devIdx].ioArray[idx].chArray[chIdx].fl,flags);
}


void  cw::audio::buf::enableChannel(   unsigned devIdx, unsigned chIdx, unsigned flags )
{  setFlag(devIdx,chIdx,flags | kChFl); }

bool  cw::audio::buf::isChannelEnabled(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(devIdx, chIdx, flags | kChFl); }

void  cw::audio::buf::enableTone(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(devIdx,chIdx,flags | kToneFl); }

bool  cw::audio::buf::isToneEnabled(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(devIdx,chIdx,flags | kToneFl); }

void  cw::audio::buf::enableMute(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(devIdx,chIdx,flags | kMuteFl); }

bool  cw::audio::buf::isMuteEnabled(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(devIdx,chIdx,flags | kMuteFl); }

void  cw::audio::buf::enablePass(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(devIdx,chIdx,flags | kPassFl); }

bool  cw::audio::buf::isPassEnabled(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(devIdx,chIdx,flags | kPassFl); }

void  cw::audio::buf::enableMeter(   unsigned devIdx, unsigned chIdx, unsigned flags )
{ setFlag(devIdx,chIdx,flags | kMeterFl); }

bool  cw::audio::buf::isMeterEnabled(unsigned devIdx, unsigned chIdx, unsigned flags )
{ return isFlag(devIdx,chIdx,flags | kMeterFl); }

cw::audio::buf::sample_t cw::audio::buf::meter(unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return 0;

  unsigned            idx = flags & kInFl  ? kInApIdx : kOutApIdx;
  const cmApCh*       cp  = _theBuf.devArray[devIdx].ioArray[idx].chArray + chIdx;
  return _cmApMeterValue(cp);  
} 

void cw::audio::buf::setGain( unsigned devIdx, unsigned chIdx, unsigned flags, double gain )
{
  if( devIdx == kInvalidIdx )
    return;

  unsigned idx      = flags & kInFl           ? kInApIdx : kOutApIdx;
  unsigned i        = chIdx != kInvalidIdx      ? chIdx : 0;
  unsigned n        = i + (chIdx != kInvalidIdx ? 1     : _theBuf.devArray[devIdx].ioArray[idx].chCnt);

  for(; i<n; ++i)
    _theBuf.devArray[devIdx].ioArray[idx].chArray[i].gain = gain;
}

double cw::audio::buf::gain( unsigned devIdx, unsigned chIdx, unsigned flags )
{
  if( devIdx == kInvalidIdx )
    return 0;

  unsigned idx   = flags & kInFl  ? kInApIdx : kOutApIdx;
  return  _theBuf.devArray[devIdx].ioArray[idx].chArray[chIdx].gain;  
}


unsigned cw::audio::buf::getStatus( unsigned devIdx, unsigned flags, double* meterArray, unsigned meterCnt, unsigned* faultCntPtr )
{
  if( devIdx == kInvalidIdx )
    return 0;

  unsigned ioIdx = cwIsFlag(flags,kInFl) ? kInApIdx : kOutApIdx;
  cmApIO*  iop   = _theBuf.devArray[devIdx].ioArray + ioIdx;
  unsigned chCnt = std::min(iop->chCnt, meterCnt );
  unsigned i;

  if( faultCntPtr != NULL )
    *faultCntPtr = iop->faultCnt;

  for(i=0; i<chCnt; ++i)
    meterArray[i] = _cmApMeterValue(iop->chArray + i);        
  return chCnt;
}


bool cw::audio::buf::isDeviceReady( unsigned devIdx, unsigned flags )
{
  //bool     iFl = true;
  //bool     oFl = true;
  unsigned i   = 0;

  if( devIdx == kInvalidIdx )
    return false;

  if( flags & kInFl )
  {
    const cmApIO* ioPtr = _theBuf.devArray[devIdx].ioArray + kInApIdx;
    for(i=0; i<ioPtr->chCnt; ++i)
      if( ioPtr->chArray[i].fn < ioPtr->dspFrameCnt )
        return false;

    //iFl = ioPtr->fn > ioPtr->dspFrameCnt;
  }

  if( flags & kOutFl )
  {
    const cmApIO* ioPtr = _theBuf.devArray[devIdx].ioArray + kOutApIdx;

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
void cw::audio::buf::get( unsigned devIdx, unsigned flags, sample_t* bufArray[], unsigned bufChCnt )
{
  unsigned i;
  if( devIdx == kInvalidIdx )
  {
    for(i=0; i<bufChCnt; ++i)
      bufArray[i] = NULL;
    return;
  }

  unsigned      idx   = flags & kInFl ? kInApIdx : kOutApIdx;
  const cmApIO* ioPtr = _theBuf.devArray[devIdx].ioArray + idx;
  unsigned      n     = bufChCnt < ioPtr->chCnt ? bufChCnt : ioPtr->chCnt;
  //unsigned      offs  = flags & kInFl ? ioPtr->oi : ioPtr->ii; 
  cmApCh*       cp    = ioPtr->chArray;

  for(i=0; i<n; ++i,++cp)
  {
    unsigned offs = flags & kInFl ? cp->oi : cp->ii;
    bufArray[i] = cwIsFlag(cp->fl,kChFl) ? cp->b + offs : NULL;
  }
  
}

void cw::audio::buf::getIO(   unsigned iDevIdx, sample_t* iBufArray[], unsigned iBufChCnt, time::spec_t* iTimeStampPtr, unsigned oDevIdx, sample_t* oBufArray[], unsigned oBufChCnt, time::spec_t* oTimeStampPtr )
{
  get( iDevIdx, kInFl, iBufArray, iBufChCnt );
  get( oDevIdx, kOutFl,oBufArray, oBufChCnt );

  unsigned i       = 0;

  if( iDevIdx != kInvalidIdx && oDevIdx != kInvalidIdx )
  {
    const cmApIO* ip       = _theBuf.devArray[iDevIdx].ioArray + kInApIdx;
    const cmApIO* op       = _theBuf.devArray[oDevIdx].ioArray + kOutApIdx;
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
    const cmApIO* op  = _theBuf.devArray[oDevIdx].ioArray + kOutApIdx;
    unsigned byteCnt  = op->dspFrameCnt * sizeof(sample_t);

    _theBufCalcTimeStamp(op->srate, &op->timeStamp, op->ioFrameCnt, oTimeStampPtr );

    for(; i<oBufChCnt; ++i)
      if( oBufArray[i] != NULL )
        memset( oBufArray[i], 0, byteCnt );
  }
}

void cw::audio::buf::advance( unsigned devIdx, unsigned flags )
{
  unsigned i;

  if( devIdx == kInvalidIdx )
    return;

  if( flags & kInFl )
  {
    cmApIO* ioPtr = _theBuf.devArray[devIdx].ioArray + kInApIdx;

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
    cmApIO* ioPtr = _theBuf.devArray[devIdx].ioArray + kOutApIdx;
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


void cw::audio::buf::inputToOutput( unsigned iDevIdx, unsigned oDevIdx )
{
  if( iDevIdx == kInvalidIdx || oDevIdx == kInvalidIdx )
    return;

  unsigned    iChCnt   = channelCount( iDevIdx, kInFl  );
  unsigned    oChCnt   = channelCount( oDevIdx, kOutFl );
  unsigned    chCnt    = iChCnt < oChCnt ? iChCnt : oChCnt;
  
  unsigned    i;

  sample_t* iBufPtrArray[ iChCnt ];
  sample_t* oBufPtrArray[ oChCnt ];


  while( isDeviceReady( iDevIdx, kInFl ) && isDeviceReady( oDevIdx, kOutFl ) )
  {
    get( iDevIdx, kInFl,  iBufPtrArray, iChCnt );
    get( oDevIdx, kOutFl, oBufPtrArray, oChCnt );

    // Warning: buffer pointers to disabled channels will be set to NULL

    for(i=0; i<chCnt; ++i)
    {
      cmApIO* ip = _theBuf.devArray[iDevIdx ].ioArray + kInApIdx;
      cmApIO* op = _theBuf.devArray[oDevIdx].ioArray + kOutApIdx;

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

    advance( iDevIdx, kInFl );
    advance( oDevIdx, kOutFl );
  }
  
}

void cw::audio::buf::report()
{
  unsigned i,j,k;
  for(i=0; i<_theBuf.devCnt; ++i)
  {
    for(j=0; j<kIoApCnt; ++j)
    {
      cmApIO* ip = _theBuf.devArray[i].ioArray + j;

      unsigned ii = 0;
      unsigned oi = 0;
      unsigned fn  = 0;
      sample_t m   = 0;
      for(k=0; k<ip->chCnt; ++k)
      {
        cmApCh* cp = ip->chArray + i;
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

void cw::audio::buf::test()
{
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
  initialize(devCnt,meterMs);

  // setup the buffer with the specific parameters to by used by the host audio ports
  setup(devIdx,srate,dspFrameCnt,cycleCnt,inChCnt,framesPerCycle,outChCnt,framesPerCycle);

  // simulate cylcing through sigN buffer transactions
  for(i=0; i<sigN; i+=framesPerCycle*inChCnt)
  {
    // setup an incoming audio packet
    pkt.audioFramesCnt = framesPerCycle;
    pkt.audioBytesPtr  = iSig+i;

    // simulate a call from the audio port with incoming samples 
    // (fill the audio buffers internal input buffers)
    update(&pkt,1,NULL,0);

    // if all devices need to be serviced
    while( isDeviceReady( devIdx, kInFl | kOutFl ))
    {
      // get pointers to full internal input buffers
      get(devIdx, kInFl, inBufArray, bufChCnt );
      
      // get pointers to empty internal output buffers
      get(devIdx, kOutFl, outBufArray, bufChCnt );

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
      advance( devIdx, kInFl | kOutFl );
    }

    pkt.audioBytesPtr = os;
    
    // simulate a call from the audio port picking up outgoing samples
    // (empties the audio buffers internal output buffers)
    update(NULL,0,&pkt,1);

    os += pkt.audioFramesCnt * pkt.chCnt;
  }

  for(i=0; i<sigN; ++i)
    cwLogInfo("%f ",oSig[i]);
  cwLogInfo("\n");

  finalize();
}

/// [cwAudioBufExample]


