#ifndef cwWaveTableBank_h
#define cwWaveTableBank_h

namespace cw
{
  namespace wt_bank
  {
    typedef handle<struct wt_bank_str> handle_t;
    typedef dsp::sample_t sample_t;
    typedef dsp::srate_t  srate_t;

    typedef enum {
      kAttackTId,
      kLoopTId
    } seg_tid_t;

    typedef struct seg_str
    {
      seg_tid_t       tid;
      double          cost;
      sample_t*       aV;       // aV[ padN + aN + padN ]
      unsigned        aN;       // Count of unique samples
      unsigned        padN;     // Count of pre/post repeat samples
    } seg_t;

    typedef struct ch_str
    {
      unsigned     ch_idx;
      unsigned     segN;
      seg_t*       segA; // segV[ segN ]
    } ch_t;

    typedef struct wt_str
    {
      unsigned    instr_id;
      srate_t     srate;
      unsigned    pitch;
      unsigned    vel;
      
      unsigned    chN;
      ch_t*       chA;  // chA[ chN ]
    } wt_t;


    

    rc_t create( handle_t& hRef, const char* dir, unsigned padN );
    
    rc_t destroy( handle_t& hRef );

    void report( handle_t h );

    unsigned instr_count( handle_t h );

    unsigned instr_index( handle_t h, const char* instr_label );

    const wt_t* get_wave_table( handle_t h, unsigned instr_idx, unsigned pitch, unsigned vel );

    rc_t test( const char* cfg_fname );

    
  }
}


#endif
