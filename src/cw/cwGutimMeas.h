#ifndef cwGutimMeas_h
#define cwGutimMeas_h

namespace cw
{
  namespace gutim_meas
  {
    typedef handle<struct gutim_meas_str> handle_t;

    rc_t create( handle_t& hRef, const char* group_info_json_fname, const char* vel_table_fname, const char* vel_table_name );
    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h );

    rc_t set_current_section( handle_t h, unsigned beg_loc_id, unsigned end_loc_id );

    // perf_note_idx is the index of this this note since the beginning of the performance.
    // The score follower may back track and end up re-assigning a previosly reported note to
    // a different location. This index can be used to detect that condition.
    rc_t on_note( handle_t h, unsigned perf_note_idx, unsigned loc_id, double sec, unsigned midi_pitch, unsigned midi_vel );

    bool is_section_complete( handle_t h );

    typedef struct
    {
      // Mean deviation from the center value over all locations in seconds.
      // How closely were the notes associated with the locations grouped?
      double avg_loc_dev_sec; 

      // Mean performed dynamic level (0-24)
      // How loud was this section played?
      double avg_dyn;
      
      // Mean deviation between scored and the fitted performance dynamics values.
      // How closesly did the dynamic envelope of the performance match the score?
      double avg_dyn_dev;

      // Count of chords in this section.
      unsigned chord_cnt;
      
      // Mean time deviation between the notes in all chords.
      // How simultaneously where the notes in the chords played to one another?
      double avg_chord_spread_secs;

      // Count of beat gropus in this section
      unsigned beat_group_cnt;
      
      // Mean deviation from the period across all beat groups
      // How periodic were the beat groups?
      double avg_beat_period_dev_sec;

      // Mean duratoin of each beat group relative to the score.
      // (1=perfect match 0.5=performed at half-speed 2.0=performed at 2x speed)
      // How closely did the duration of the beat groups match the score beat groups.
      double avg_beat_dur_pct;  

      // Count of grace groups in this section
      unsigned grace_group_cnt;
      
      // Mean deviation from the period across all grace groups
      // How periodic were the grace groups?
      double avg_grace_period_dev_sec;

      // Mean duratoin of each grace group relative to the score.
      // (1=perfect match 0.5=performed at half-speed 2.0=performed at 2x speed)
      // How closely did the duration of the grace groups match the score grace groups.
      double avg_grace_dur_pct;  
      
      // Percentage deviation from the sections scored versus performed duration
      // 1.0=identical duration
      double section_dur_dev_pct;
            
    } results_t;

    rc_t get_results( handle_t h, results_t*& results_ref );

    rc_t report( handle_t h );
    
  }
}


#endif


