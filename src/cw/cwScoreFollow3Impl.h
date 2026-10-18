#include <random>  // std::mt19937
#include <cstdint> // uint64_t
#include <bit>     // std::popcount

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

    // A group of consecutive locations whose times are within args.loc_group_sec of the first location of the group.
    // The jump prior treats a group as one chord, so its notes may be performed in any order (see _form_groups()).
    // A group's notes are contiguous in sf_t.noteA[] because its locations are.
    typedef struct grp_str
    {
      unsigned beg_loc_idx;  // first location of the group
      unsigned end_loc_idx;  // last location (inclusive) of the group
      unsigned beg_note_idx; // locA[beg_loc_idx].beg_note_idx
      unsigned end_note_idx; // locA[end_loc_idx].end_note_idx
    } grp_t;

    typedef struct particle_str
    {
      // Kalman filter state [pred_score_sec, tempo] and its covariance. See _predict() and _kalman_update() in cwScoreFollow3Impl.cpp.
      double   pred_score_sec;     // s: predicted score position in score seconds
      double   tempo;              // r: score seconds per performance second. time_fact = 1/tempo.
      double   cov_ss;             // covariance of the state: var(s), cov(s,r) and var(r)
      double   cov_sr;
      double   cov_rr;

      unsigned anchor_grp_idx;     // group index of the last match or mismatch, or kInvalidIdx before the first one. Nomatch notes leave it unchanged.
      uint64_t played_mask;        // bit i is set if note grpA[anchor_grp_idx].beg_note_idx+i has been matched since the particle moved to the anchor group.
      uint64_t late_mask;          // bit i is set if note grpA[anchor_grp_idx].beg_note_idx-64+i is pending: it is in the late window and the particle passed it without matching it (see _late_advance()).

      double   weight;   // Particle filter weight for this particle.
      
    } particle_t;

    // A branch is one candidate hypothesis for the current performed note.
    // Each particle is expanded into a set of branches (match, mismatch, nomatch)
    // and the branches are then resampled back into particles.
    typedef struct branch_str
    {
      unsigned          parent_idx; // index into sf_t.particleA[] of the parent particle
      match_status_id_t tid;        // kMatchTId, kMismatchTId or kNoMatchTId
      unsigned          loc_idx;    // candidate location index into sf_t.locA[] or kInvalidIdx for nomatch
      unsigned          note_ofs;   // match: offset of the matched note from the group's beg_note_idx (its played_mask bit), late match: its late_mask bit, otherwise kInvalidIdx
      bool              late_fl;    // true if this is a late match: a pending note of the late window, which does not move the anchor
      double            weight;     // parent.weight * prior(tid) * jump prior * likelihood
    } branch_t;


    // A (loc_idx,tid) key and its total posterior mass. Used to report the top MAP keys at report_level >= 3.
    typedef struct map_key_str
    {
      unsigned          loc_idx;    // location index or kInvalidIdx for the nomatch bucket
      match_status_id_t tid;
      double            mass;
    } map_key_t;

    enum { kMapTopN = 5 };  // count of MAP keys reported at report_level >= 3

    enum { kMaxLocNoteN = 64 }; // max. count of notes at one location, and in one group - the size of particle_t.played_mask in bits

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

      double   active_dur_sec;     // Length of the active score region in seconds (or 1.0 if it is computed as 0.0)
      
      bool     end_timeout_fl;     // true if reporting the last location of the segment waits for the end timeout rather than ending the segment (see reset())

      double perf_base_time;

      //
      // Fields below this point are owned by the implementation (cwScoreFollow3Impl.cpp).
      //

      args_t       args;               // copy of the args_t given to create()

      grp_t*       grpA;               // grpA[grpN] - location groups in increasing time order
      unsigned     grpN;
      unsigned*    loc_grp_idxA;       // loc_grp_idxA[locN] - group index of each location
      unsigned     max_grp_loc_cnt;    // largest count of locations in one group
      unsigned     max_wnd_loc_cnt;    // upper bound on the count of locations in a search window (see _max_window_loc_count())
      unsigned*    late_beg_note_idxA; // late_beg_note_idxA[grpN] - first note of the late window of each anchor group, before clipping to the active region (see _form_late_windows())
      unsigned     max_late_pitch_cnt; // largest count of one pitch in any late window - the bound on the late match branches of one particle

      particle_t*  particleA;          // particleA[particleN] - the current particle population
      particle_t*  next_particleA;     // next_particleA[particleN] - resampling destination, swapped with particleA[] after each note
      unsigned     particleN;          // count of particles (args.particle_cnt)

      branch_t*    branchA;            // branchA[branchAllocN] - preallocated branch pool
      unsigned     branchAllocN;       // allocated size of branchA[]
      unsigned     branchN;            // count of branches in use on the current note

      double*      match_massA;        // match_massA[locN] - per-location match mass accumulator used to form the MAP result
      double*      mismatch_massA;     // mismatch_massA[locN] - per-location mismatch mass accumulator
      unsigned*    touched_loc_idxA;   // touched_loc_idxA[locN] - locations whose accumulators were written on the current note
      unsigned     touchedN;           // count of valid elements in touched_loc_idxA[]
      unsigned     late_resampleN;     // count of particles which took a late match branch on the current note (report_level >= 2)

      std::mt19937 rng;                // random number generator, re-seeded from args.rand_seed on each reset()

      double       prev_perf_sec;      // performance time of the previous note, used to form the prediction delta
      unsigned     last_loc_idx;       // most recently reported location index or kInvalidIdx
      bool         done_fl;            // set when the last location of the active region is reported or the end timeout expires, cleared by reset(), used by is_done()

      double*      rem_max_ioi_secA;   // rem_max_ioi_secA[locN] - largest score time gap between consecutive locations from each location to the end of the active region (set by reset())
      double       last_note_perf_sec; // performance time of the latest note, including notes outside the pitch range
      bool         end_loc_reported_fl;// set when the last location of the active region is first reported while end_timeout_fl is set (used only to log it once)

      map_key_t    map_topA[ kMapTopN ]; // largest MAP keys of the current note in decreasing mass order (report_level >= 3)
      unsigned     map_topN;             // count of valid elements in map_topA[]

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
