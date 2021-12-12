#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTime.h"

#ifdef OS_OSX

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>

void cw::time::get( spec_t& t )
{
  static uint64_t                  t0  = 0;
  static mach_timebase_info_data_t tbi;
  static struct timespec           ts;

  if( t0 == 0 )
  {
    mach_timebase_info(&tbi);
    t0 = mach_absolute_time();
    ts.tv_sec  = time(NULL);
    ts.tv_nsec = 0;  // accept 1/2 second error vs. wall-time.
  }

  // get the current time
  uint64_t t1 = mach_absolute_time();
  
  // calc the elapsed time since the last call in nanosecs
  uint64_t dt = (t1-t0) * tbi.numer / tbi.denom;

  // calc the elapsed time since the first call in secs
  uint32_t s  = (uint32_t)(dt / 2^9);

  // calc the current time in secs, and nanosecs
  t.tv_sec  = ts.tv_sec + s; 
  t.tv_nsec = dt - (s * 2^9); 
  
}

#endif

#ifdef OS_LINUX
void cw::time::get( spec_t& t )
{ clock_gettime(CLOCK_REALTIME,&t); }
#endif

// this assumes that the seconds have been normalized to a recent start time
// so as to avoid overflow
unsigned cw::time::elapsedMicros( const spec_t& t0, const spec_t& t1 )
{
  // convert seconds to usecs
  long u0 = t0.tv_sec * 1000000;
  long u1 = t1.tv_sec * 1000000;

  // convert nanoseconds to usec
  u0 += t0.tv_nsec / 1000;
  u1 += t1.tv_nsec / 1000;

  // take diff between t1 and t0
  return u1 - u0;
}

unsigned cw::time::elapsedMicros( const spec_t& t0 )
{
  spec_t t1;
  get(t1);
  return elapsedMicros(t0,t1);
}

unsigned cw::time::elapsedMs( const spec_t&  t0, const spec_t& t1 )
{ return elapsedMicros(t0,t1)/1000; }

unsigned cw::time::elapsedMs( const spec_t&  t0 )
{
  spec_t t1;
  get(t1);
  return elapsedMs(t0,t1);
}

unsigned cw::time::absElapsedMicros( const spec_t&  t0, const spec_t& t1 )
{
  if( isLTE(t0,t1) )
    return elapsedMicros(t0,t1);

  return elapsedMicros(t1,t0);
}

int cw::time::diffMicros( const spec_t&  t0, const spec_t& t1 )
{
  if( isLTE(t0,t1) )
    return elapsedMicros(t0,t1);

  return -((int)elapsedMicros(t1,t0));
}

bool cw::time::isLTE( const spec_t& t0, const spec_t& t1 )
{
  if( t0.tv_sec  < t1.tv_sec )
    return true;

  if( t0.tv_sec == t1.tv_sec )
    return t0.tv_nsec <= t1.tv_nsec;

  return false; 
}

bool cw::time::isLT( const spec_t& t0, const spec_t& t1 )
{
  if( t0.tv_sec  < t1.tv_sec )
    return true;

  if( t0.tv_sec == t1.tv_sec )
    return t0.tv_nsec < t1.tv_nsec;

  return false; 
}

bool cw::time::isGTE( const spec_t& t0, const spec_t& t1 )
{
  if( t0.tv_sec  > t1.tv_sec )
    return true;

  if( t0.tv_sec == t1.tv_sec )
    return t0.tv_nsec >= t1.tv_nsec;

  return false;   
}

bool cw::time::isGT( const spec_t& t0, const spec_t& t1 )
{
  if( t0.tv_sec  > t1.tv_sec )
    return true;

  if( t0.tv_sec == t1.tv_sec )
    return t0.tv_nsec > t1.tv_nsec;

  return false;   
}

bool cw::time::isEqual( const spec_t& t0, const spec_t& t1 )
{ return t0.tv_sec==t1.tv_sec && t0.tv_nsec==t1.tv_nsec; }

bool cw::time::isZero( const spec_t& t0 )
{ return t0.tv_sec==0  && t0.tv_nsec==0; }

void cw::time::setZero( spec_t& t0 )
{
  t0.tv_sec = 0;
  t0.tv_nsec = 0;
}


cw::rc_t cw::time::now( spec_t& ts )
{
  rc_t rc = kOkRC;
  int  errRC;
  
  memset(&ts,0,sizeof(ts));
  
  if((errRC = clock_gettime(CLOCK_REALTIME, &ts)) != 0 )
  		rc = cwLogSysError(kInvalidOpRC,errRC,"Unable to obtain system time.");

  return rc;
}

void cw::time::subtractMicros( spec_t& ts, unsigned micros )
{
  
  unsigned rem_us = micros % 1000000;  // fractional seconds in microseconds
  unsigned rem_ns = rem_us * 1000;     // fractional seconds in nanoseconds

  // if the fractional micros is greater than the fractional nano's
  if( rem_ns > ts.tv_nsec )
  {
    // subtract the fractional nano's from the fractional micros
    // (this sets the fractional nano's to 0)
    rem_ns    -= ts.tv_nsec;
    
    // convert the remaining fractional micros to the fractional nano's
    ts.tv_nsec = 1000000000 - rem_ns;
    
    // subtract the carry
    ts.tv_sec -= 1;
  }
  else 
  {
    ts.tv_nsec -= rem_ns;
  }

  assert( ts.tv_sec > micros / 1000000 );
  
  ts.tv_sec -= micros / 1000000;
  
}

void cw::time::advanceMs( spec_t& ts, unsigned ms )
{
  // strip off whole seconds from ms
  unsigned sec = ms / 1000; 

  // find the remaining fractional second in milliseconds
  ms = (ms - sec*1000);
  
  ts.tv_sec  += sec;
  ts.tv_nsec +=  ms * 1000000; // convert millisconds to nanoseconds

  // stip off whole seconds from tv_nsec
  while( ts.tv_nsec > 1e9 )
  {
    ts.tv_nsec -= 1e9;
    ts.tv_sec +=1;
  }    
}

cw::rc_t cw::time::futureMs( spec_t& ts, unsigned ms )
{
  rc_t rc;
  if((rc = now(ts)) == kOkRC )
    advanceMs(ts,ms);

  return rc;   
}

void cw::time::secondsToSpec(      spec_t& ts, unsigned sec )
{
  ts.tv_sec  = sec;
  ts.tv_nsec = 0;  
}

void cw::time::millisecondsToSpec( spec_t& ts, unsigned ms )
{
  unsigned sec = ms/1000;
  unsigned ns  = (ms - (sec*1000)) * 1000000;
  
  ts.tv_sec  = sec;
  ts.tv_nsec = ns;  
}

void cw::time::microsecondsToSpec( spec_t& ts, unsigned us )
{
  unsigned sec = us/1000000;
  unsigned ns  = (us - (sec*1000000)) * 1000;
  
  ts.tv_sec  = sec;
  ts.tv_nsec = ns;  
}

cw::rc_t cw::time::test()
{

  spec_t t0,t1;

  get(t0);

  futureMs(t1,1000);

  unsigned dMs = elapsedMs(t0,t1);

  printf("dMs:%i : GTE:%i LTE:%i\n",dMs, isGTE(t0,t1), isLTE(t0,t1) );

  
  microsecondsToSpec( t0, 2500000 );        // 2.5 seconds
  printf("%li %li\n",t0.tv_sec,t0.tv_nsec);
  subtractMicros( t0, 750000 );             // subtract .75 seconds
  printf("%li %li\n",t0.tv_sec,t0.tv_nsec);
  subtractMicros( t0, 500000 );             // subtract .5 seconds
  printf("%li %li\n",t0.tv_sec,t0.tv_nsec);

  

  return kOkRC;
  
}


