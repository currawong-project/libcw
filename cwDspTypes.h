#ifndef cwDspTypes_h
#define cwDspTypes_h

namespace cw
{
  namespace dsp
  {
    typedef float   sample_t;
    typedef float   fd_sample_t; // Frequency domain sample - type used by magnitude,phase,real,imag. part of spectral values
    typedef float   srate_t;
    typedef float   coeff_t;   // values that are directly applied to signals of sample_t.
    typedef double   ftime_t;   // any time value expressed as a floating point value - could be seconds, milliseconds, etc
  }
}
#endif
