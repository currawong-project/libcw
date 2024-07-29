#ifndef cwWaveTableBank_h
#define cwWaveTableBank_h

#ifdef NOT_DEF
namespace cw
{
  namespace wt_bank
  {
    typedef handle<struct wt_bank_str> handle_t;
    typedef dsp::sample_t sample_t;
    typedef dsp::srate_t  srate_t;

    typedef enum {
      kAttackTId,
      kLoopTId,
      kInvalidTId
    } seg_tid_t;

    typedef struct seg_str
    {
      seg_tid_t       tid;
      double          cost;
      unsigned        cyc_per_loop; // count of cycles in the loop
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

    rc_t gen_notes( handle_t h, unsigned instr_idx, const unsigned* pitchA, const unsigned* velA, unsigned noteN, double dur_secs, const char* out_fname, double inter_note_gap_secs=0.1 );
    
    rc_t test( const test::test_args_t& args );
    
    
  }
}
#endif

namespace cw
{
  namespace wt_bank
  {

    typedef handle<struct wt_bank_str> handle_t;
    typedef dsp::sample_t sample_t;
    typedef dsp::srate_t  srate_t;

    typedef struct wt_str
    {
      unsigned        cyc_per_loop; // count of cycles in the loop
      sample_t*       aV;       // aV[ padN + aN + padN ]
      unsigned        aN;       // Count of unique samples
      double          rms;
      double          hz;      
    } wt_t;

    typedef struct wt_seq_str
    {
      wt_t*    wtA;
      unsigned wtN;
    } wt_seq_t;

    typedef struct multi_ch_wt_seq_str
    {
      unsigned  chN;
      wt_seq_t* chA;
    } multi_ch_wt_seq_t;
    
    rc_t create( handle_t& hRef, unsigned padSmpN );
    rc_t destroy( handle_t& hRef );

    void report( handle_t h );
    
    rc_t load( handle_t h, const char* instr_json_fname );

    unsigned instr_count( handle_t h );

    unsigned instr_index( handle_t h, const char* instr_label );

    const wt_t* get_wave_table( handle_t h, unsigned instr_idx, unsigned pitch, unsigned vel );

    rc_t gen_notes( handle_t h, unsigned instr_idx, const unsigned* pitchA, const unsigned* velA, unsigned noteN, double dur_secs, const char* out_fname, double inter_note_gap_secs=0.1 );
    
    rc_t test( const test::test_args_t& args );
    
  }
}

#endif
