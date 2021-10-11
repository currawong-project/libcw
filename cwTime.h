
//( { file_desc:"Time cand clock related functions." kw: [ time system ] }
//
//
// This interface is used to read the systems high resolution timer and 
// calculate elapsed time.
//)


#ifndef cwTime_H
#define cwTime_H

namespace cw
{
  namespace time
  {

    //(
    typedef  struct timespec spec_t;

    // Get the time 
    void get( spec_t& tRef );

    // Return the elapsed time (t1 - t0) in microseconds
    // t1 is assumed to be at a later time than t0.
    unsigned elapsedMicros( const spec_t&  t0, const spec_t& t1 );
    unsigned elapsedMicros( const spec_t&  t0 );

    // Wrapper on elapsedMicros()
    unsigned elapsedMs( const spec_t&  t0, const spec_t& t1 );
    unsigned elapsedMs( const spec_t&  t0 );
  
    // Same as elapsedMicros() but the times are not assumed to be ordered.
    // The function therefore begins by swapping t1 and t0 if t0 is after t1.
    unsigned absElapsedMicros( const spec_t&  t0, const spec_t& t1 ); 


    // Same as elapsedMicros() but returns a negative value if t0 is after t1.
    int diffMicros( const spec_t&  t0, const spec_t& t1 );


    // Returns true if t0 <=  t1.
    bool isLTE( const spec_t& t0, const spec_t& t1 );

    // Return true if t0 >= t1.
    bool isGTE( const spec_t& t0, const spec_t& t1 );

    bool isEqual( const spec_t& t0, const spec_t& t1 );

    bool isZero( const spec_t& t0 );

    void setZero( spec_t& t0 );

    rc_t now( spec_t& ts );

    void subtractMicros( spec_t& ts, unsigned us );
    
    // Advance 'ts' by 'ms' milliseconds.
    void advanceMs( spec_t& ts, unsigned ms );

    // Advance the current time by 'ms' milliseconds;
    rc_t futureMs( spec_t& ts, unsigned ms );

    void secondsToSpec(      spec_t& ts, unsigned sec );
    void millisecondsToSpec( spec_t& ts, unsigned ms );
    void microsecondsToSpec( spec_t& ts, unsigned us );

    rc_t test();

    //)

  }
}

#endif

