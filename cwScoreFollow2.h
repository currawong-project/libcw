
namespace cw
{
  namespace score_follow_2
  {
    typedef handle< struct sf_str > handle_t;

    typedef struct args_str
    {
      perf_score::handle_t scoreH;
      
      double pre_affinity_sec;  // 1.0 look back affinity duration
      double post_affinity_sec; // 3.0 look forward affinity duration
      double pre_wnd_sec;       // 2.0 look back search window 
      double post_wnd_sec;      // 5.0 look forward search window
        
      double decay_coeff;          // 0.995affinity decay coeff

      double d_sec_err_thresh_lo;  // 0.4 reject if d_loc > d_loc_thresh_lod and d_time > d_time_thresh_lo
      int d_loc_thresh_lo;    // 3  
      
      double d_sec_err_thresh_hi;  // 1.5 reject if d_loc != 0 and d_time > d_time_thresh_hi
      int      d_loc_thresh_hi;    // 4 reject if d_loc > d_loc_thresh_hi
      int      d_loc_stats_thresh; // 3 reject for time stats updates if d_loc > d_loc_stats_thresh

      bool rpt_fl;  // set to turn on debug reporting
      
    } args_t;

    rc_t parse_args( const object_t* cfg, args_t& args );

    rc_t create( handle_t& hRef, const args_t& args );

    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h, unsigned beg_loc_id, unsigned end_loc_id );

    rc_t on_new_note( handle_t h, unsigned uid, double sec, uint8_t pitch, uint8_t vel, unsigned& loc_id );

    // Decay the affinity window and if necessary trigger a cycle of async background processing
    rc_t do_exec( handle_t h );

    typedef struct rpt_str
    {
      unsigned matchN;      // count of matched notes
      unsigned missN;       // count of missed notes
      unsigned spuriousN;   // count of spurious notes
      unsigned perfNoteN;   // count of performed notes (count of calls to on_new_note())
    } rpt_t;
    
    void report_summary( handle_t h, rpt_t& rpt_ref );

  }
}
