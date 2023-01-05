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
        real_t      srate;          // system sample rate
        unsigned    procSmpCnt;     // samples per exec cycle
        real_t      inGain;         // input gain
        real_t      threshDb;       // threshold in dB (max:100 min:0) 
        real_t      ratio_num;      // numerator of the ratio
        unsigned    atkSmp;         // time to reduce   the signal by 10.0 db
        unsigned    rlsSmp;         // time to increase the signal by 10.0 db
        real_t      outGain;        // makeup gain
        bool        bypassFl;       // bypass enable
        sample_t*   rmsWnd;         // rmsWnd[rmsWndAllocCnt]
        unsigned    rmsWndAllocCnt; // 
        unsigned    rmsWndCnt;      // current RMS window size (rmsWndCnt must be <= rmsWndAllocCnt)
        unsigned    rmsWndIdx;      // next RMS window input index
        unsigned    state;          // env. state
        real_t      rmsDb;          // current incoming signal RMS (max:100 min:0)
        real_t      gain;           // current compressor  gain
        real_t      timeConstDb;    // the atk/rls will incr/decr by 'timeConstDb' per atkMs/rlsMs.
        real_t      pkDb;           //
        real_t      accumDb;        //

      } obj_t;
      
      rc_t create( obj_t*& p, real_t srate, unsigned procSmpCnt, real_t inGain, real_t rmsWndMaxMs, real_t rmsWndMs, real_t threshDb, real_t ratio, real_t atkMs, real_t rlsMs, real_t outGain, bool bypassFl );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n );
      
      void set_attack_ms(  obj_t* p, real_t ms );
      void set_release_ms( obj_t* p, real_t ms );
      void set_thresh_db(  obj_t* p, real_t thresh );
      void set_rms_wnd_ms( obj_t* p, real_t ms );
    }

    namespace limiter
    {
      typedef struct
      {
        unsigned procSmpCnt;
        real_t   igain;   // applied before thresholding
        real_t   thresh;  // linear (0.0-1.0) threshold.
        real_t   ogain;   // applied after thresholding
        bool     bypassFl;
      } obj_t;
      
      rc_t create( obj_t*& p, real_t srate, unsigned procSmpCnt, real_t thresh, real_t igain, real_t ogain, bool bypassFl );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n );
    }

    namespace dc_filter
    {
      typedef struct
      {
        real_t d[2]; //
        real_t b[1]; // 
        real_t a[1]; // a[dn] feedback coeff's
        real_t b0;   // feedforward coeff 0
        bool   bypassFl;
        real_t gain;
      } obj_t;

      rc_t create( obj_t*& p, real_t srate, unsigned procSmpCnt, real_t gain, bool bypassFl );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, sample_t* y, unsigned n );
      rc_t set( obj_t* p, real_t gain, bool bypassFl );
    }

    namespace recorder
    {
      typedef struct
      {
        real_t    srate;      //
        unsigned  maxFrameN;  //
        unsigned  chN;        //  channel count
        unsigned  frameIdx;   //  next frame to write
        sample_t* buf;        //   [ [maxFrameN] [maxFrameN] ]
      } obj_t;                //        ch0          ch1

      rc_t create(  obj_t*& pRef, real_t srate, real_t max_secs, unsigned chN );
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
	real_t    srate;
	real_t    peakThreshDb;
	real_t    outLin;
	real_t    outDb;
	bool      peakFl;
	bool      clipFl;
	unsigned  peakCnt;
	unsigned  clipCnt;
	unsigned  wi;
      } obj_t;
      
      rc_t create( obj_t*& p, real_t srate, real_t maxWndMs, real_t wndMs, real_t peakThreshDb );
      rc_t destroy( obj_t*& pp );
      rc_t exec( obj_t* p, const sample_t* x, unsigned n );
      void reset( obj_t* p );
      void set_window_ms( obj_t* p, real_t wndMs );

    }
  }  
}

#endif
