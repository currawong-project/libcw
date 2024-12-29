//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwWaveTableBank_h
#define cwWaveTableBank_h


namespace cw
{
  namespace wt_bank
  {

    typedef handle<struct wt_bank_str> handle_t;
    typedef dsp::sample_t              sample_t;
    typedef dsp::srate_t               srate_t;
    
    typedef        dsp::wt_osc::wt_tid_t                                           wt_tid_t;
    typedef struct dsp::wt_osc::wt_str<sample_t,srate_t>                           wt_t;
    typedef struct dsp::wt_seq_osc::wt_seq_str<sample_t,srate_t>                   wt_seq_t;
    typedef struct dsp::multi_ch_wt_seq_osc::multi_ch_wt_seq_str<sample_t,srate_t> multi_ch_wt_seq_t;

    
    rc_t create( handle_t& hRef, unsigned padSmpN, const char* instr_json_fname=nullptr );
    rc_t destroy( handle_t& hRef );

    void report( handle_t h );
    
    rc_t load( handle_t h, const char* instr_json_fname, unsigned threadN=16 );

    unsigned instr_count( handle_t h );

    unsigned instr_index( handle_t h, const char* instr_label );
    
    // Return the actual measured velocities, not the interpolated velocities, for a given instr/pitch.
    rc_t instr_pitch_velocities( handle_t h, unsigned instr_idx, unsigned pitch, unsigned* velA, unsigned velCnt, unsigned& velCnt_Ref );

    rc_t get_wave_table( handle_t h, unsigned instr_idx, unsigned pitch, unsigned vel, multi_ch_wt_seq_t const* & mcs_Ref  );

    rc_t test( const test::test_args_t& args );
    
  }
}

#endif
