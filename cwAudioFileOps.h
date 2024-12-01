//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwAudioFileOps_h
#define cwAudioFileOps_h

namespace cw
{
  namespace afop
  {

    // Generate a sine tone and write it to a file.
    rc_t sine( const char* fn, double srate, unsigned bits, double hz, double gain, double secs );
    rc_t sine( const object_t* cfg );

    // Generate clicks (unit impulses) at the sample locations.
    rc_t clicks( const char* fn, double srate, unsigned bits, double secs, const unsigned* clickSmpOffsArray, unsigned clickSmpOffsetN, double clickAmp = 0.9, double decay=0.7, double burstMs=10 );

    // Parse the 'generate' paramter configuration and generate a file.
    rc_t generate( const object_t* cfg );

    // Mix a set of audio files.
    rc_t mix( const char* fnV[], const float* gainV, unsigned srcN, const char* outFn, unsigned outBits );
    rc_t mix( const object_t* cfg );

    // Copy a time selection to an audio output file.
    rc_t selectToFile( const char* srcFn, double beg0Sec, double beg1Sec, double end0Sec, double end1Sec, unsigned outBits, const char* outDir, const char* outFn );
    rc_t selectToFile( const object_t* cfg );

    
    // Arbitrary cross fader
    typedef struct {
      const char* srcFn;          // source audio file name
      double      srcBegSec;      // source clip begin
      double      srcEndSec;      // source clip end
      double      srcBegFadeSec;  // length of fade in   (fade begins at srcBegSec and ends at srcBegSec+srcBegFadeSec)
      double      srcEndFadeSec;  // length of fade out  (fade begins at srcEndSec-srcEndFadeSec and ends at srcEndSec)
      double      dstBegSec;      // clip output location
      double      gain;           // scale the signal
    } cutMixArg_t;
    
    rc_t cutAndMix( const char* outFn, unsigned outBits, const char* srcDir, const cutMixArg_t* argL, unsigned argN );
    rc_t cutAndMix( const object_t* cfg );

    // Given a collection of overlapping tracks fade in/out sections of the tracks at specified times.
    // This is a wrapper around cutAndMix()
    typedef struct
    {
      const char* srcFn;
      double      srcBegSec;
      double      srcEndSec;
      double      fadeOutSec;
      double      gain;
    } parallelMixArg_t;

    rc_t parallelMix( const char* dstFn, unsigned dstBits, const char* srcDir, const parallelMixArg_t* argL, unsigned argN );
    rc_t parallelMix( const object_t* cfg );

    // Currawong on-line transform application 
    rc_t transformApp( const object_t* cfg );

    // Convolve an impulse response from 'impulseRespnseFn' with 'srcFn'.
    rc_t convolve( const char* dstFn, unsigned dstBits, const char* srcFn, const char* impulseResponseFn, float irScale=1 );
    rc_t convolve( const object_t* cfg );

    rc_t test( const object_t* cfg );
    
  }
}


#endif
