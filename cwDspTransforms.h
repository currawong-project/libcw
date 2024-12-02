//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwDspTransforms_h
#define cwDspTransforms_h

namespace cw
{
  namespace dsp
  {
    //---------------------------------------------------------------------------------------------------------------------------------
    // compressor
    //
    namespace compressor
    {
      enum { kAtkCompId, kRlsCompId };

      typedef struct
      {
        srate_t     srate;          // system sample rate
        unsigned    procSmpCnt;     // samples per exec cycle
        coeff_t      inGain;         // input gain
        coeff_t      threshDb;       // threshold in dB (max:100 min:0) 
        coeff_t      ratio_num;      // numerator of the ratio
        unsigned    atkSmp;         // time to reduce   the signal by 10.0 db
        unsigned    rlsSmp;         // time to increase the signal by 10.0 db
        coeff_t      outGain;        // makeup gain
        bool        bypassFl;       // bypass enable
        sample_t*   rmsWnd;         // rmsWnd[rmsWndAllocCnt]
        unsigned    rmsWndAllocCnt; // 
        unsigned    rmsWndCnt;      // current RMS window size (rmsWndCnt must be <= rmsWndAllocCnt)
        unsigned    rmsWndIdx;      // next RMS window input index
        unsigned    state;          // env. state
        coeff_t      rmsDb;          // current incoming signal RMS (max:100 min:0)
        coeff_t      gain;           // current compressor  gain
        coeff_t      timeConstDb;    // the atk/rls will incr/decr by 'timeConstDb' per atkMs/rlsMs.
        coeff_t      pkDb;           //
        coeff_t      accumDb;        //

      } obj_t;
      
      rc_t create( obj_t*& p, srate_t srate, unsigned procSmpCnt, coeff_t inGain, ftime_t rmsWndMaxMs, ftime_t rmsWndMs, coeff_t threshDb, coeff_t ratio, ftime_t atkMs, ftime_t rlsMs, coeff_t outGain, bool bypassFl );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n );
      
      void set_attack_ms(  obj_t* p, ftime_t ms );
      void set_release_ms( obj_t* p, ftime_t ms );
      void set_thresh_db(  obj_t* p, coeff_t thresh );
      void set_rms_wnd_ms( obj_t* p, ftime_t ms );
    }

    namespace limiter
    {
      typedef struct
      {
        unsigned procSmpCnt;
        coeff_t   igain;   // applied before thresholding
        coeff_t   thresh;  // linear (0.0-1.0) threshold.
        coeff_t   ogain;   // applied after thresholding
        bool     bypassFl;
      } obj_t;
      
      rc_t create( obj_t*& p, srate_t srate, unsigned procSmpCnt, coeff_t thresh, coeff_t igain, coeff_t ogain, bool bypassFl );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n );
    }

    namespace dc_filter
    {
      typedef struct
      {
        coeff_t d[2]; //
        coeff_t b[1]; // 
        coeff_t a[1]; // a[dn] feedback coeff's
        coeff_t b0;   // feedforward coeff 0
        bool   bypassFl;
        coeff_t gain;
      } obj_t;

      rc_t create( obj_t*& p, srate_t srate, unsigned procSmpCnt, coeff_t gain, bool bypassFl );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n );
      rc_t set( obj_t* p, coeff_t gain, bool bypassFl );
    }

    namespace recorder
    {
      typedef struct
      {
        srate_t    srate;      //
        unsigned  maxFrameN;  //
        unsigned  chN;        //  channel count
        unsigned  frameIdx;   //  next frame to write
        sample_t* buf;        //   [ [maxFrameN] [maxFrameN] ]
      } obj_t;                //        ch0          ch1

      rc_t create(  obj_t*& pRef, srate_t srate, ftime_t max_secs, unsigned chN );
      rc_t destroy( obj_t*& pRef);
      
      rc_t exec(    obj_t* p, const sample_t* buf,   unsigned chN, unsigned frameN );
      rc_t exec(    obj_t* p, const sample_t* chA[], unsigned chN, unsigned frameN );
      rc_t write(   obj_t* p, const char* fname );      
    }

    namespace audio_meter
    {
      typedef struct
      {
        unsigned  maxWndMs;
        unsigned  maxWndSmpN;
        unsigned  wndSmpN;
        sample_t* wndV;
        srate_t   srate;
        coeff_t    peakThreshDb;
        coeff_t    outLin;
        coeff_t    outDb;
        bool      peakFl;
        bool      clipFl;
        unsigned  peakCnt;
        unsigned  clipCnt;
        unsigned  wi;
      } obj_t;
      
      rc_t create( obj_t*& p, srate_t srate, ftime_t maxWndMs, ftime_t wndMs, coeff_t peakThreshDb );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, unsigned n );
      void reset( obj_t* p );
      void set_window_ms( obj_t* p, ftime_t wndMs );

    }
  }  
}

#endif
