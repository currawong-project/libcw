/*
- Maintain two windows:
  + affinity window: defines the time span of a score envelope
  + search window: defines the space in which a search for a matching note will occur.
     
- Upon receiving a new note:
   The score of each matching note in the search window is computed.
   If a match is found calculate the following values:
     - d_loc_id : difference between matched and expected score locations
     - d_match_score_sec : time between this match and the previous match in score time
     - d_match_perf_sec  : time between this match and the previous match in perf time
     - d_corr_sec        : time between cur perf sec and corrected score time

   Reject the match if:
     -  both the low loc and low time threshold are violated
        (d_loc_id > lo_loc_thresh && fabs(d_corr_sec) > d_sec_err_thresh_lod
         
     - the hi loc threshold is violated
       (d_loc_id > hi_loc_thresh)

       
     - the hi time is violated and d_loc_id is not 0
       (d_corr_sec > d_sec_err_thresh_hi) && d_loc_id > 0

   If the match is not rejected update expV[] by adding in the affinity window centered on the match location.

- On every DSP cycle update decay expV[] inside the current search window so that the strength
  of the expected match area decreases with time.
   
 */

namespace cw
{
  namespace score_follow_2
  {
    typedef handle< struct sf_str > handle_t;

    typedef struct args_str
    {      
      double   pre_affinity_sec;     // 1.0 look back affinity duration
      double   post_affinity_sec;    // 3.0 look forward affinity duration
      unsigned min_affinity_loc_cnt; // min. loc's in back/forward aff. window
      double   pre_wnd_sec;          // 2.0 look back search window 
      double   post_wnd_sec;         // 5.0 look forward search window
      unsigned min_wnd_loc_cnt;      // min. loc's in back/forward search window
        
      double decay_coeff;          // 0.995 affinity decay coeff

      double d_sec_err_thresh_lo;  // 0.4 reject if d_loc > d_loc_thresh_lod and d_time > d_time_thresh_lo
      int    d_loc_thresh_lo;      //   3  
      
      double d_sec_err_thresh_hi;  // 1.5 reject if d_loc != 0 and d_time > d_time_thresh_hi
      int      d_loc_thresh_hi;    // 4   reject if d_loc > d_loc_thresh_hi
      int      d_loc_stats_thresh; // 3   reject for time stats updates if d_loc > d_loc_stats_thresh

      bool rpt_fl;  // set to turn on debug reporting
      
    } args_t;

    rc_t parse_args( const object_t* cfg, args_t& args );

    rc_t create( handle_t& hRef, const args_t& args, perf_score::handle_t scoreH );

    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h, unsigned beg_loc_id, unsigned end_loc_id );

    rc_t on_new_note( handle_t h, unsigned uid, double sec, uint8_t pitch, uint8_t vel, unsigned& loc_id_ref, unsigned& meas_numb_ref, unsigned& score_vel_ref );

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

    unsigned max_loc_id( handle_t h );

  }
}
