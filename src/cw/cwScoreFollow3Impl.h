namespace cw
{
  namespace score_follow_3
  {
    struct loc_str;
    
    typedef struct note_str
    {
      unsigned       uid;       // unique id of this note in the score
      const loc_str* loc;       // location that this note belongs to
      unsigned       pitch;     // score MIDI pitch
      unsigned       velocity;  // score MIDI velocity
      bool           grace_fl;  // true if this is a grace note
    } note_t;
    
    typedef struct loc_str
    {
      unsigned  loc_id;       // location id of this location
      unsigned  meas_num;     // measure number of this score event
      double    sec;          // score time of this
      unsigned  beg_note_idx; // index into sf_t.noteA[] of the first note associated with this location
      unsigned  end_note_idx; // index into sf_t.noteA[] of the last note associated with this location
    } loc_t;

    typedef struct particle_str
    {
      double   pred_score_sec;     // particles predicted location in score seconds
      double   time_fact;          // factor used to convert performed time to score time score_time = perf_time / time_fact
      unsigned pred_loc_idx;       // location index of the last predicted location or kInvalidIdx if no location was predicted,
      
      double   weight;   // Particle filter weight for this particle.
      
    } particle_t;

    typedef struct branch_str
    {
      particle_t* parent;

      match_status_id_t tid;
      
      
    } branch_t;

      
    typedef struct sf_str
    {
      note_t*  noteA;  // noteA[noteN] - score notes in increasing time order
      unsigned noteN;
      
      loc_t*   locA;   // locA[locN] - score locations in increasing time order
      unsigned locN;

      unsigned min_score_loc_id;  // The min and max score location id's 
      unsigned max_score_loc_id;

      unsigned active_beg_loc_idx; // Currently active score segment 
      unsigned active_end_loc_idx;

      
      double perf_base_time;
    } sf_t;

    rc_t _create_impl( sf_t* p, const args_t& args );
    rc_t _destroy_impl( sf_t* p );

    // See p->active_beg_loc_idx and p->active_end_loc_idx for the boundaries of the score region to be tracked
    rc_t _reset_impl( sf_t* p );
    rc_t _on_new_note_impl( sf_t* p, unsigned uid, double perf_sec, unsigned pitch, unsigned vel, result_t& result_ref );
    rc_t _exec_impl( sf_t* p, double perf_sec );
    bool _is_done_impl( sf_t* p );
    
  }
}
