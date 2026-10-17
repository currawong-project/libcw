
namespace cw
{
  namespace score_follow_3
  {
    typedef handle< struct sf_str > handle_t;
    
    typedef struct args_str
    {
      unsigned pitch_min;           // mininum expected MIDI pitch value
      unsigned pitch_max;           // maximum expected MIDI pitch value
      
      double   wnd_pre_sec;         // 2.0 look back search window 
      double   wnd_post_sec;        // 5.0 look forward search window
      unsigned wnd_pre_loc_cnt;     // min. loc's in backward search window
      unsigned wnd_post_loc_cnt;    // min. loc's in the forward search window

      unsigned particle_cnt;        // Number of particles N. Sizes the particle array, and with the window bound it sizes the branch pool.
      unsigned rand_seed;           // Seed for the std::mt19937, re-applied at each reset() so runs are reproducible.  
      double prior_match;           // Prior probability that a note is a match, i.e. the right pitch near its predicted time.
      double prior_mismatch;        // Prior probability that a note is a mismatch, i.e. the right time but the wrong pitch. 
      double prior_nomatch;         // Prior probability that a note is a nomatch, unrelated to any nearby score note. The three priors must add up to 1.0.  

      // Each particle tracks its score position s and tempo r = 1/time_fact (score sec per perf sec) with a Kalman filter.
      double time_sigma_sec;        // Onset noise: standard deviation, in score seconds, of a matched location's time around the true score position (Kalman R).
      double time_fact_prior_sigma; // Standard deviation of the tempo r at reset(), where r starts at 1.0.
      double time_fact_walk_sigma;  // Tempo process noise: standard deviation of the change in r per sqrt(performance second).
      double pos_walk_sigma;        // Position process noise: standard deviation, in score seconds, of the change in s per sqrt(performance second).
      double time_fact_min;         // Lower clamp for time_fact (i.e. 1/time_fact_min is the upper clamp for r).
      double time_fact_max;         // Upper clamp for time_fact.

      // Jump prior: the relative weight of a candidate location given its distance d = loc_idx - anchor_loc_idx
      // from the location the particle last matched. The weights are normalized over each particle's candidate locations.
      double jump_stay;             // d == 0: another note of the anchor location (only while it has unplayed notes).
      double jump_next;             // d == 1: the next location.
      double jump_skip;             // d >= 2: jump_skip * jump_decay^(d-2). Skipped locations.
      double jump_back;             // d <  0: jump_back * jump_decay^(-d-1). Backward jumps and repeats.
      double jump_decay;            // Geometric decay (0,1) applied per additional location of a skip or backward jump.

      // Consecutive locations within loc_group_sec of the first location of their group are treated by the jump prior
      // as one chord, so they may be performed in any order. 0 disables grouping (every location is its own group).
      double loc_group_sec;

      // End of region detection. Besides reporting the last location of the region, the follower is done when
      // its last reported location is within end_zone_sec of the end of the region and no note has arrived for
      //   done_timeout_mult * time_fact_max * (largest inter-onset gap remaining before the end) + done_timeout_min_sec
      // performance seconds. The timeout is evaluated by exec().
      double end_zone_sec;          // Length, in score seconds, of the zone before the end of the region in which the timeout is armed.
      double done_timeout_mult;     // Safety factor (>= 1) applied to the largest remaining inter-onset gap.
      double done_timeout_min_sec;  // Floor (> 0) of the timeout in performance seconds.


      double   exec_period_sec;      // period of calls to exec() in seconds
      
      unsigned report_level;  // 0=silent set to turn on debug reporting
      
    } args_t;

    typedef struct score_event_str
    {
      unsigned loc_id;     // location id of this event
      unsigned meas_num;   // measure number of this event
      double   sec;        // location of this score event in seconds
      unsigned pitch;      // MIDI pitch
      unsigned velocity;   // expected loudness of this event: 0-128
      bool     grace_fl;   // true if this is a grace note
    } score_event_t;

    typedef enum {
      kBlankTId,       // This match was skipped or not evaluated
      kMatchTId,       // A match was made in both pitch and time.
      kMismatchTId,    // A match was made in time but not pitch.
      kNoMatchTId      // The note matched in neither pitch nor time.
    } match_status_id_t;

    // Returns nullptr if tid is not found.
    const char*       match_status_id_to_label( match_status_id_t tid );

    // Return kEleNotFoundRC if the label does not identify a known match status
    rc_t match_status_label_to_id( const char* label, match_status_id_t& id_ref );

    typedef struct result_str
    {
      unsigned          perf_uid;        // uid of a performed note
      unsigned          loc_id;          // Location assocated with this or kInvalidId if no location
      match_status_id_t match_status_id; // The type of score match this result represents.
      double            confidence;
    } result_t;
    
    rc_t create( handle_t& hRef, const args_t& args, const score_event_t* scoreA, unsigned scoreN );

    rc_t destroy( handle_t& hRef );

    // Prepare to track the section of the score identified by the range beg_loc_id to end_loc_id.
    // The next calls to on_new_note() should therefore match to notes at or close to beg_loc_id.
    rc_t reset( handle_t h, unsigned beg_loc_id, unsigned end_loc_id );

   
    // This function 
    rc_t on_new_note( handle_t h,
                      unsigned perf_uid,   // unique id associated with this performed note
                      double  sec,         // time at which this note was performed
                      unsigned perf_pitch,  // MIDI pitch of the performed note
                      unsigned perf_vel,
                      result_t& result_ref ); // MIDI velocity of the performed note

    // This function is called periodically as time advances.
    rc_t exec( handle_t h, double sec );

    // Return true if the end of the score section identified by the last call
    // to reset() has been reached or possibly passed over.
    bool is_done( handle_t h );


    
  }
}
