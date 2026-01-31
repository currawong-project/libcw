//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.

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

    typedef  struct timespec spec_t;

    // Get the current time from the system's high-resolution clock.
    void get( spec_t& tRef );
    // Get the current time from the system's high-resolution clock.
    spec_t current_time(); // same as get()

    // Normalize a timespec so that the nanoseconds field is in the range [0, 999999999].
    void normalize( spec_t& t );
    // Accumulate the elapsed time between t0 and t1 into 'acc'.
    rc_t accumulate_elapsed( spec_t& acc, const spec_t& t0, const spec_t& t1 );
    // Accumulate the elapsed time between t0 and the current time into 'acc'.
    rc_t accumulate_elapsed_current( spec_t& acc, const spec_t& t0 );
    // Accumulate a duration into a timespec.
    void accumulate( spec_t& acc, const spec_t& dur );
    // Convert a timespec to a floating-point number of seconds.
    double seconds( const spec_t& t );
    
    // Return the elapsed time (t1 - t0) in microseconds.
    // t1 is assumed to be at a later time than t0.
    unsigned long long elapsedMicros( const spec_t&  t0, const spec_t& t1 );
    // Return the elapsed time from t0 to now in microseconds.
    unsigned long long elapsedMicros( const spec_t&  t0 );

    // Wrapper for elapsedMicros()
    unsigned elapsedMs( const spec_t&  t0, const spec_t& t1 );
    unsigned elapsedMs( const spec_t&  t0 );

    // Wrapper for elapsedMicros()
    double elapsedSecs( const spec_t&  t0, const spec_t& t1 );
    double elapsedSecs( const spec_t&  t0 );

    // Return the absolute elapsed time between t0 and t1 in microseconds.
    // The times are not assumed to be ordered.
    unsigned absElapsedMicros( const spec_t&  t0, const spec_t& t1 ); 


    // Return the difference between t1 and t0 in microseconds.
    // The result is negative if t0 is after t1.
    int diffMicros( const spec_t&  t0, const spec_t& t1 );


    // Returns true if t0 is less than or equal to t1.
    bool isLTE( const spec_t& t0, const spec_t& t1 );
    
    // Returns true if t0 is less than t1.
    bool isLT(  const spec_t& t0, const spec_t& t1 );

    // Return true if t0 is greater than or equal to t1.
    bool isGTE( const spec_t& t0, const spec_t& t1 );
    
   // Return true if t0 is greater than t1. 
   bool isGT(  const spec_t& t0, const spec_t& t1 );

   // Return true if t0 is equal to t1.
    bool isEqual( const spec_t& t0, const spec_t& t1 );

    // Return true if the timespec represents zero time.
    bool isZero( const spec_t& t0 );

    // Set a timespec to zero.
    void setZero( spec_t& t0 );

    // Get the current time from the system's high-resolution clock.
    rc_t now( spec_t& ts ); // same as get()

    // Subtract a number of microseconds from a timespec.
    void subtractMicros( spec_t& ts, unsigned us );
    
    // Advance 'ts' by 'us' microseconds.
    void advanceMicros( spec_t& ts, unsigned us );
    // Advance 'ts' by 'ms' milliseconds.
    void advanceMs( spec_t& ts, unsigned ms );

    // Calculate a future time by adding 'ms' milliseconds to the current time.
    rc_t futureMs( spec_t& ts, unsigned ms );

    // Convert a fractional number of seconds to a timespec.
    void fracSecondsToSpec(      spec_t& ts, double sec );
    // Convert an integer number of seconds to a timespec.
    void secondsToSpec(      spec_t& ts, unsigned sec );
    // Convert a timespec to a floating-point number of seconds.
    double specToSeconds(  const spec_t& ts );

    // Convert a timespec to an integer number of microseconds.
    unsigned long long specToMicroseconds( const spec_t& ts );

    // Convert a number of milliseconds to a timespec.
    void millisecondsToSpec( spec_t& ts, unsigned ms );
    // Convert a number of microseconds to a timespec.
    void microsecondsToSpec( spec_t& ts, unsigned long long us );
    // Convert a number of microseconds to a timespec.
    spec_t microsecondsToSpec( unsigned long long us );

    // Format the current time as a string (e.g., "HH:MM:SS.mmm").
    // Returns the number of bytes written to the buffer.
    unsigned formatDateTime( char* buf, unsigned bufN, bool includeDateFl=false );

    // Run tests for the cwTime module.
    rc_t test( const test::test_args_t& test );


  }
}

#endif

