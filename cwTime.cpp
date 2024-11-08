#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
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
#include <sys/time.h> // gettimeofday()
void cw::time::get( spec_t& t )
{
  clock_gettime(CLOCK_MONOTONIC,&t);
}

cw::time::spec_t cw::time::current_time()
{
  spec_t t;
  get(t);
  return t;
}

#endif

unsigned long long cw::time::elapsedMicros( const spec_t& t0, const spec_t& t1 )
{  
  const unsigned long long ns_per_sec = 1000000000;
  const unsigned long long us_per_sec = 1000000;
  const unsigned long long ns_per_us =  1000;

  // we assume that the time is normalized
  assert( t0.tv_nsec < (long long)ns_per_sec );
  assert( t1.tv_nsec < (long long)ns_per_sec );

  if( t0.tv_sec > t1.tv_sec )
  {
    cwLogWarning("Negative elapsed time detected.");
    spec_t tt0 = t1;
    spec_t tt1 = t0;
    return elapsedMicros(tt0,tt1);
  }
  

  // t1 does not cross a 'seconds' boundary with t0
  if( t0.tv_sec == t1.tv_sec )
  {
    // then t0 nsecs must be <= t1 nsecs
    assert( t0.tv_nsec <= t1.tv_nsec );
    return (t1.tv_nsec - t0.tv_nsec)/ns_per_us;
  }

  
  // t1 occurs in a different second than t0
  unsigned long long d_sec   = (t1.tv_sec - t0.tv_sec) - 1; // difference in seconds
  unsigned long long d_nsec0 = ns_per_sec - t0.tv_nsec;     // time from t0 to next seconds boundary
  unsigned long long d_nsec1 = t1.tv_nsec;                  // time from t1 to prev. seconds boundary

  return (d_sec*us_per_sec) + ((d_nsec0 + d_nsec1)/ns_per_us);
}

/*
unsigned long long cw::time::elapsedMicros0( const spec_t& t0, const spec_t& t1 )
{
  const unsigned long long us_per_sec = 1000000;
  const unsigned long long ns_per_us  =    1000;
  
  // convert seconds to usecs
  unsigned long long u0 = t0.tv_sec * us_per_sec;
  unsigned long long u1 = t1.tv_sec * us_per_sec;


  // convert nanoseconds to usec
  u0 += t0.tv_nsec / ns_per_us;
  u1 += t1.tv_nsec / ns_per_us;

  // take diff between t1 and t0
  return u1 - u0;
}
*/

unsigned long long cw::time::elapsedMicros( const spec_t& t0 )
{
  spec_t t1;
  get(t1);
  return elapsedMicros(t0,t1);
}

unsigned cw::time::elapsedMs( const spec_t&  t0, const spec_t& t1 )
{ return (unsigned)(elapsedMicros(t0,t1)/1000); }

unsigned cw::time::elapsedMs( const spec_t&  t0 )
{
  spec_t t1;
  get(t1);
  return elapsedMs(t0,t1);
}

double cw::time::elapsedSecs( const spec_t&  t0, const spec_t& t1 )
{
  return elapsedMicros(t0,t1) / 1000000.0;
}

double cw::time::elapsedSecs( const spec_t&  t0 )
{
  spec_t t1;
  get(t1);
  return elapsedSecs(t0,t1);
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
  
  if((errRC = clock_gettime(CLOCK_MONOTONIC, &ts)) != 0 )
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


void cw::time::advanceMicros( spec_t& ts, unsigned us )
{
  const unsigned ns_per_sec = 1000000000;
  
  ts.tv_nsec += us * 1000;  // convert us to nano's

  // check if nano's now have more than ns_pser_sec
  time_t sec = ts.tv_nsec / ns_per_sec;
  
  ts.tv_sec  += sec;
  ts.tv_nsec -= sec * ns_per_sec;
}



void cw::time::advanceMs( spec_t& ts, unsigned ms )
{

  const unsigned ns_per_sec = 1000000000;
  
  ts.tv_nsec += ms * 1000000;
  time_t sec = ts.tv_nsec / ns_per_sec;
  
  ts.tv_sec  += sec;
  ts.tv_nsec -= sec * ns_per_sec;
  
}

cw::rc_t cw::time::futureMs( spec_t& ts, unsigned ms )
{
  rc_t rc;
  if((rc = now(ts)) == kOkRC )
    advanceMs(ts,ms);

  return rc;   
}

void cw::time::fracSecondsToSpec( spec_t& ts, double sec )
{
  const unsigned long long ns_per_sec = 1000000000;
  ts.tv_sec  = (unsigned long long)sec;
  ts.tv_nsec = (sec - ts.tv_sec) * ns_per_sec;
}

void cw::time::secondsToSpec(      spec_t& ts, unsigned sec )
{
  ts.tv_sec  = sec;
  ts.tv_nsec = 0;  
}

double cw::time::specToSeconds(  const spec_t& t )
{
  const long long ns_per_sec = 1000000000;
  spec_t ts  = t;
  double sec = ts.tv_sec;
  while( ts.tv_nsec >= ns_per_sec )
  {
    sec += 1.0;
    ts.tv_nsec -= ns_per_sec;
  }
  
  return sec + ((double)ts.tv_nsec)/1e9;
}

unsigned long long cw::time::specToMicroseconds( const spec_t& ts )
{
  const unsigned long long us_per_sec =    1000000;
  const unsigned long long ns_per_sec = 1000000000;
  
  unsigned long long us  = ts.tv_sec * us_per_sec;
  unsigned long long sec = ts.tv_nsec / ns_per_sec;
  us += sec * us_per_sec;
  us += (ts.tv_nsec - (sec * ns_per_sec))/1000;

  return us;
}


void cw::time::millisecondsToSpec( spec_t& ts, unsigned ms )
{
  unsigned sec = ms/1000;
  unsigned ns  = (ms - (sec*1000)) * 1000000;
  
  ts.tv_sec  = sec;
  ts.tv_nsec = ns;  
}

void cw::time::microsecondsToSpec( spec_t& ts, unsigned long long us )
{
  const unsigned long long usPerSec = 1000000;
  unsigned long long sec = us/usPerSec;
  unsigned long long ns  = (us - (sec*usPerSec)) * 1000;
  
  ts.tv_sec  = sec;
  ts.tv_nsec = ns;  
}

cw::time::spec_t cw::time::microsecondsToSpec( unsigned long long us )
{
  spec_t ts;
  microsecondsToSpec(ts,us);
  return ts;
}



unsigned cw::time::formatDateTime( char* buffer, unsigned bufN, bool includeDateFl )
{
  // from here: https://stackoverflow.com/questions/3673226/how-to-print-time-in-format-2009-08-10-181754-811
  int millisec;
  struct tm* tm_info;
  struct timeval tv;
  int n = 0;

  gettimeofday(&tv, NULL);

  millisec = lrint(tv.tv_usec/1000.0); // Round to nearest millisec

  // Allow for rounding up to nearest second
  if (millisec>=1000)
  { 
    millisec -=1000;
    tv.tv_sec++;
  }

  tm_info = localtime(&tv.tv_sec);

  const char* fmt = "%H:%M:%S";

  if( includeDateFl )
    fmt = "%Y:%m:%d %H:%M:%S";
    

  n = strftime(buffer, bufN, fmt, tm_info);
  
  if( n < (int)bufN && bufN-n >= 5 )
    n = snprintf(buffer + n, bufN-n,".%03d", millisec);

  return (unsigned)n;
}

cw::rc_t cw::time::test(const test::test_args_t& test )
{

  spec_t t0,t1;

  get(t0);

  futureMs(t1,1000);

  unsigned dMs = elapsedMs(t0,t1);

  cwLogPrint("dMs:%i : GTE:%i LTE:%i\n",dMs, isGTE(t0,t1), isLTE(t0,t1) );

  
  microsecondsToSpec( t0, 2500000 );        // 2.5 seconds
  cwLogPrint("%li %li\n",t0.tv_sec,t0.tv_nsec);
  subtractMicros( t0, 750000 );             // subtract .75 seconds
  cwLogPrint("%li %li\n",t0.tv_sec,t0.tv_nsec);
  subtractMicros( t0, 500000 );             // subtract .5 seconds
  cwLogPrint("%li %li\n",t0.tv_sec,t0.tv_nsec);


  time::get(t0);
  //time::get(t1);
  t1 = t0;

  advanceMicros(t1,5000);
  
  int usec = time::elapsedMicros(t0,t1);

  cwLogPrint("usec:%i\n",usec);

  t0 = current_time();
  sleepMs(1000);
  cwLogPrint("sleep %i ms\n",elapsedMs(t0));


  return kOkRC;
  
}


