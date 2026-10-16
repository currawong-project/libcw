#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwCsv.h"
#include "cwVectOps.h"
#include "cwScoreFollow3.h"
#include "cwScoreFollow3Impl.h"

namespace cw
{
  namespace score_follow_3
  {

    // Return a conservative upper bound on the count of locations that any search window can contain.
    //
    // The window around a predicted time spans at least wnd_pre_sec/wnd_pre_loc_cnt backward and
    // at least wnd_post_sec/wnd_post_loc_cnt forward. The bound is formed from:
    //   - the largest count of locations within any time span of wnd_pre_sec + wnd_post_sec,
    //   - plus the location count minimums of both sides and the center location,
    //   - plus one location on each side to allow for the extension that first
    //     crosses the time boundary.
    // It is computed over the whole score, rather than the active region, so that reset() never needs to reallocate.
    unsigned _max_window_loc_count( const sf_t* p )
    {
      double   span_sec   = p->args.wnd_pre_sec + p->args.wnd_post_sec;
      unsigned max_span_n = 0;

      // Two-pointer sweep: for each begin location i advance j to the first location beyond the span.
      for(unsigned i=0,j=0; i<p->locN; ++i)
      {
        if( j < i )
          j = i;

        while( j < p->locN && p->locA[j].sec - p->locA[i].sec <= span_sec )
          ++j;

        max_span_n = std::max(max_span_n, j - i);
      }

      unsigned n = max_span_n + p->args.wnd_pre_loc_cnt + p->args.wnd_post_loc_cnt + 1 + 2;

      // a window can never contain more than every location in the score
      return std::min(n, p->locN);
    }

    // Return the index of the location in the active region whose time is nearest to 'sec'.
    unsigned _nearest_active_loc_idx( const sf_t* p, double sec )
    {
      unsigned lo = p->active_beg_loc_idx;
      unsigned hi = p->active_end_loc_idx;

      // Binary search for the first location whose time is >= sec (or hi if there is none).
      while( lo < hi )
      {
        unsigned mid = lo + (hi - lo)/2;

        if( p->locA[mid].sec < sec )
          lo = mid + 1;
        else
          hi = mid;
      }

      // The nearest location is either lo or the location just before it.
      if( lo > p->active_beg_loc_idx && sec - p->locA[lo-1].sec <= p->locA[lo].sec - sec )
        lo -= 1;

      return lo;
    }

    // Locate the search window around a particle's predicted score time.
    //
    // The window extends backward from the nearest location until it spans at least
    // wnd_pre_sec before 'pred_score_sec' and contains at least wnd_pre_loc_cnt
    // locations before the nearest location. It extends forward in the same way using
    // wnd_post_sec and wnd_post_loc_cnt. The window is hard clipped to the active region.
    //
    // beg_loc_idx_ref and end_loc_idx_ref are set to the first and last (inclusive)
    // location of the window. dur_sec_ref is set to the window duration, with a floor
    // of time_sigma_sec, for use as the nomatch uniform density support. The floor
    // prevents a zero duration (and an infinite density) when the clipped window holds a single location.
    void _window( const sf_t* p, double pred_score_sec, unsigned& beg_loc_idx_ref, unsigned& end_loc_idx_ref, double& dur_sec_ref )
    {
      // The nearest location is searched for inside the active region so the clipped window is never empty.
      unsigned c = _nearest_active_loc_idx(p,pred_score_sec);
      unsigned b = c;
      unsigned e = c;

      // extend backward until both the duration and the location count minimums are satisfied
      while( b > p->active_beg_loc_idx && (c - b < p->args.wnd_pre_loc_cnt || pred_score_sec - p->locA[b].sec < p->args.wnd_pre_sec) )
        --b;

      // extend forward until both the duration and the location count minimums are satisfied
      while( e < p->active_end_loc_idx && (e - c < p->args.wnd_post_loc_cnt || p->locA[e].sec - pred_score_sec < p->args.wnd_post_sec) )
        ++e;

      beg_loc_idx_ref = b;
      end_loc_idx_ref = e;
      dur_sec_ref     = std::max(p->locA[e].sec - p->locA[b].sec, p->args.time_sigma_sec);
    }

    // Return a draw from N(mean,sigma).
    // std::normal_distribution requires sigma > 0, therefore sigma == 0 returns 'mean' exactly.
    // The distribution object is created on the stack and does not allocate memory.
    double _rand_normal( sf_t* p, double mean, double sigma )
    {
      if( sigma <= 0 )
        return mean;

      std::normal_distribution<double> dist(mean,sigma);
      return dist(p->rng);
    }

    double _clamp_time_fact( const sf_t* p, double time_fact )
    {
      return std::clamp(time_fact, p->args.time_fact_min, p->args.time_fact_max);
    }

    // Calculate the population mean and standard deviation of pred_score_sec and time_fact.
    void _particle_stats( const sf_t* p, double& score_sec_mean_ref, double& score_sec_std_ref, double& time_fact_mean_ref, double& time_fact_std_ref )
    {
      double ss_sum = 0, ss_sq_sum = 0;
      double tf_sum = 0, tf_sq_sum = 0;

      for(unsigned i=0; i<p->particleN; ++i)
      {
        const particle_t& pt = p->particleA[i];
        ss_sum    += pt.pred_score_sec;
        ss_sq_sum += pt.pred_score_sec * pt.pred_score_sec;
        tf_sum    += pt.time_fact;
        tf_sq_sum += pt.time_fact * pt.time_fact;
      }

      score_sec_mean_ref = ss_sum / p->particleN;
      time_fact_mean_ref = tf_sum / p->particleN;

      // max(0,...) guards against a tiny negative variance due to round-off
      score_sec_std_ref  = std::sqrt(std::max(0.0, ss_sq_sum / p->particleN - score_sec_mean_ref * score_sec_mean_ref));
      time_fact_std_ref  = std::sqrt(std::max(0.0, tf_sq_sum / p->particleN - time_fact_mean_ref * time_fact_mean_ref));
    }

    //----------------------------------------------------------------------------------------
    // Measurement model
    //

    // Gaussian time likelihood N(dsec; 0, time_sigma_sec).
    // The normalizing constant 1/(sigma*sqrt(2*pi)) is required: the match and mismatch time terms
    // are compared directly with the nomatch uniform density (1/window_dur_sec) and therefore all
    // three must be densities over score seconds.
    double _time_likelihood( const sf_t* p, double dsec )
    {
      double sigma = p->args.time_sigma_sec;
      double z     = dsec / sigma;
      return std::exp(-0.5 * z * z) / (sigma * std::sqrt(2.0 * M_PI));
    }

    // Return the count of notes at a location.
    unsigned _loc_note_count( const sf_t* p, unsigned loc_idx )
    {
      return p->locA[loc_idx].end_note_idx - p->locA[loc_idx].beg_note_idx + 1;
    }

    // Return true if 'pitch' is one of the notes at a location.
    bool _loc_has_pitch( const sf_t* p, unsigned loc_idx, unsigned pitch )
    {
      const loc_t& loc = p->locA[loc_idx];
      for(unsigned i=loc.beg_note_idx; i<=loc.end_note_idx; ++i)
        if( p->noteA[i].pitch == pitch )
          return true;
      return false;
    }

    // Append a branch to the branch pool.
    rc_t _add_branch( sf_t* p, unsigned parent_idx, match_status_id_t tid, unsigned loc_idx, double weight )
    {
      // The pool bound is conservative, so this should never occur, but report it rather than overflowing.
      if( p->branchN >= p->branchAllocN )
        return cwLogError(kBufTooSmallRC,"The branch pool is full (%i branches).",p->branchAllocN);

      branch_t& br  = p->branchA[ p->branchN++ ];
      br.parent_idx = parent_idx;
      br.tid        = tid;
      br.loc_idx    = loc_idx;
      br.weight     = weight;

      return kOkRC;
    }

    //----------------------------------------------------------------------------------------
    // Particle filter steps. These are called in order by _on_new_note_impl().
    //

    // A pitch outside [pitch_min,pitch_max] lies outside the pitch space of the model.
    // Return false if the pitch is out of range, in which case the result is set to kNoMatchTId.
    bool _check_pitch_range( sf_t* p, unsigned uid, unsigned pitch, result_t& result_ref )
    {
      if( p->args.pitch_min <= pitch && pitch <= p->args.pitch_max )
        return true;

      cwLogWarning("The performed pitch %i (uid:%i) is outside the range [%i,%i] and was reported as nomatch.",pitch,uid,p->args.pitch_min,p->args.pitch_max);

      // The model gives an out-of-range pitch no chance of matching, so the nomatch result is certain.
      result_ref.loc_id          = kInvalidId;
      result_ref.match_status_id = kNoMatchTId;
      result_ref.confidence      = 1.0;
      return false;
    }

    // Advance each particle's predicted score time by the elapsed performance time since the previous note.
    void _predict( sf_t* p, double perf_sec )
    {
      double delta_perf_sec = perf_sec - p->prev_perf_sec;

      for(unsigned i=0; i<p->particleN; ++i)
        p->particleA[i].pred_score_sec += delta_perf_sec / p->particleA[i].time_fact;

      p->prev_perf_sec = perf_sec;
    }

    // Expand each particle into its candidate branches and weight each branch by
    // parent.weight * prior(tid) * likelihood.
    //
    // Match    : prior_match    * N(loc.sec - pred_score_sec; 0, time_sigma_sec) * 1/k
    // Mismatch : prior_mismatch * N(nearest.sec - pred_score_sec; 0, time_sigma_sec) * 1/(P-k)
    // NoMatch  : prior_nomatch  * 1/window_dur_sec * 1/P
    //
    // where P is the size of the pitch space and k is the count of notes at the candidate location.
    rc_t _branch( sf_t* p, unsigned pitch )
    {
      rc_t     rc = kOkRC;
      unsigned P  = p->args.pitch_max - p->args.pitch_min + 1;

      p->branchN = 0;

      for(unsigned i=0; i<p->particleN; ++i)
      {
        const particle_t& pt              = p->particleA[i];
        unsigned          beg_loc_idx     = kInvalidIdx;
        unsigned          end_loc_idx     = kInvalidIdx;
        double            wnd_dur_sec     = 0;
        unsigned          nearest_loc_idx = kInvalidIdx;
        double            nearest_dsec    = 0;

        _window(p, pt.pred_score_sec, beg_loc_idx, end_loc_idx, wnd_dur_sec);

        // Match branches: one for each window location which contains the performed pitch.
        for(unsigned loc_idx=beg_loc_idx; loc_idx<=end_loc_idx; ++loc_idx)
        {
          double dsec = p->locA[loc_idx].sec - pt.pred_score_sec;

          // track the location nearest in time to the predicted score time
          if( nearest_loc_idx == kInvalidIdx || std::fabs(dsec) < std::fabs(nearest_dsec) )
          {
            nearest_loc_idx = loc_idx;
            nearest_dsec    = dsec;
          }

          if( _loc_has_pitch(p, loc_idx, pitch) )
          {
            // The 1/k term keeps a chord location from receiving k times the evidence of a single-note location.
            double k = _loc_note_count(p, loc_idx);
            double w = pt.weight * p->args.prior_match * _time_likelihood(p, dsec) / k;

            if((rc = _add_branch(p, i, kMatchTId, loc_idx, w)) != kOkRC )
              goto errLabel;
          }
        }

        // Mismatch branch: the note is assigned to the location nearest in time.
        // It is skipped if that location contains the pitch because the match branch already covers that case.
        if( !_loc_has_pitch(p, nearest_loc_idx, pitch) )
        {
          unsigned k = _loc_note_count(p, nearest_loc_idx);

          // a mismatch is impossible if every pitch in the pitch space is present at the location
          if( k < P )
          {
            double w = pt.weight * p->args.prior_mismatch * _time_likelihood(p, nearest_dsec) / (P - k);

            if((rc = _add_branch(p, i, kMismatchTId, nearest_loc_idx, w)) != kOkRC )
              goto errLabel;
          }
        }

        // NoMatch branch: the note is unrelated to any score location.
        // Its time density is uniform over the particle's window.
        {
          double w = pt.weight * p->args.prior_nomatch / wnd_dur_sec / P;

          if((rc = _add_branch(p, i, kNoMatchTId, kInvalidIdx, w)) != kOkRC )
            goto errLabel;
        }
      }

    errLabel:
      return rc;
    }

    // Divide each branch weight by the total branch weight so that the branch weights form the posterior over hypotheses for this note.
    rc_t _normalize_branches( sf_t* p )
    {
      double sum = 0;

      for(unsigned i=0; i<p->branchN; ++i)
        sum += p->branchA[i].weight;

      // This cannot occur while prior_nomatch > 0 (see _validate_args()) but check rather than divide by zero.
      if( !(sum > 0) || !std::isfinite(sum) )
        return cwLogError(kInvalidStateRC,"The total branch weight (%f) is invalid.",sum);

      for(unsigned i=0; i<p->branchN; ++i)
        p->branchA[i].weight /= sum;

      return kOkRC;
    }

    // Insert a key into map_topA[], which is kept in decreasing mass order. Keys with no mass are ignored.
    void _map_top_insert( sf_t* p, unsigned loc_idx, match_status_id_t tid, double mass )
    {
      if( mass <= 0 )
        return;

      // find the insertion point
      unsigned i = p->map_topN;
      while( i > 0 && p->map_topA[i-1].mass < mass )
        --i;

      if( i >= kMapTopN )
        return;

      // shift the smaller keys down, dropping the last one if the array is full
      unsigned n = std::min(p->map_topN + 1, (unsigned)kMapTopN);
      for(unsigned j=n-1; j>i; --j)
        p->map_topA[j] = p->map_topA[j-1];

      p->map_topA[i].loc_idx = loc_idx;
      p->map_topA[i].tid     = tid;
      p->map_topA[i].mass    = mass;
      p->map_topN            = n;
    }

    // Sum the normalized branch weights by (loc_idx,tid) and report the key with the largest total mass (MAP).
    // All nomatch branches are summed into a single bucket.
    // This occurs before resampling because the normalized branch weights are the exact posterior for this note.
    // Returns the index of the reported location or kInvalidIdx if the nomatch bucket won.
    unsigned _report_map( sf_t* p, result_t& result_ref )
    {
      double nomatch_mass = 0;

      // accumulate the mass for each key
      for(unsigned i=0; i<p->branchN; ++i)
      {
        const branch_t& br = p->branchA[i];

        if( br.tid == kNoMatchTId )
        {
          nomatch_mass += br.weight;
          continue;
        }

        // Record the location the first time it receives mass.
        // Once an accumulator is positive it never returns to zero during this note, so each location is recorded at most once.
        if( br.weight > 0 && p->match_massA[ br.loc_idx ] == 0 && p->mismatch_massA[ br.loc_idx ] == 0 )
          p->touched_loc_idxA[ p->touchedN++ ] = br.loc_idx;

        if( br.tid == kMatchTId )
          p->match_massA[ br.loc_idx ] += br.weight;
        else
          p->mismatch_massA[ br.loc_idx ] += br.weight;
      }

      // select the key with the largest mass - the nomatch bucket wins ties
      match_status_id_t best_tid     = kNoMatchTId;
      unsigned          best_loc_idx = kInvalidIdx;
      double            best_mass    = nomatch_mass;

      for(unsigned i=0; i<p->touchedN; ++i)
      {
        unsigned loc_idx = p->touched_loc_idxA[i];

        if( p->match_massA[loc_idx] > best_mass )
        {
          best_tid     = kMatchTId;
          best_loc_idx = loc_idx;
          best_mass    = p->match_massA[loc_idx];
        }

        if( p->mismatch_massA[loc_idx] > best_mass )
        {
          best_tid     = kMismatchTId;
          best_loc_idx = loc_idx;
          best_mass    = p->mismatch_massA[loc_idx];
        }
      }

      result_ref.match_status_id = best_tid;
      result_ref.loc_id          = best_loc_idx == kInvalidIdx ? kInvalidId : p->locA[ best_loc_idx ].loc_id;
      result_ref.confidence      = best_mass;

      if( best_loc_idx != kInvalidIdx )
        p->last_loc_idx = best_loc_idx;

      // Once the end of the active region has been reported the follower remains done until the next reset().
      if( best_loc_idx == p->active_end_loc_idx )
        p->done_fl = true;

      // collect the largest keys for the report_level 3 MAP table
      if( p->args.report_level >= 3 )
      {
        p->map_topN = 0;
        _map_top_insert(p, kInvalidIdx, kNoMatchTId, nomatch_mass);

        for(unsigned i=0; i<p->touchedN; ++i)
        {
          unsigned loc_idx = p->touched_loc_idxA[i];
          _map_top_insert(p, loc_idx, kMatchTId,    p->match_massA[loc_idx]);
          _map_top_insert(p, loc_idx, kMismatchTId, p->mismatch_massA[loc_idx]);
        }
      }

      // clear only the accumulators which were written on this note
      for(unsigned i=0; i<p->touchedN; ++i)
      {
        p->match_massA[    p->touched_loc_idxA[i] ] = 0;
        p->mismatch_massA[ p->touched_loc_idxA[i] ] = 0;
      }
      p->touchedN = 0;

      return best_loc_idx;
    }

    // Systematic resampling of the branch pool back to particleN particles.
    // A single uniform draw u in [0,1/N) places N evenly spaced pointers u + i/N which are
    // walked against the cumulative branch weights. Each selected branch becomes one particle.
    void _resample( sf_t* p )
    {
      double                                 N        = p->particleN;
      double                                 init_wgt = 1.0 / N;
      std::uniform_real_distribution<double> dist(0.0, init_wgt);
      double                                 u        = dist(p->rng);
      unsigned                               j        = 0;
      double                                 cum      = p->branchA[0].weight;

      for(unsigned i=0; i<p->particleN; ++i)
      {
        double ptr = u + i * init_wgt;

        // The j guard protects against the cumulative sum ending slightly below 1.0 due to round-off.
        while( ptr > cum && j < p->branchN-1 )
          cum += p->branchA[ ++j ].weight;

        const branch_t&   br     = p->branchA[j];
        const particle_t& parent = p->particleA[ br.parent_idx ];
        particle_t&       pt     = p->next_particleA[i];

        // match and mismatch branches snap to the location, nomatch branches keep the parent's predicted state
        pt                = parent;
        pt.weight         = init_wgt;
        pt.pred_loc_idx   = br.loc_idx;  // kInvalidIdx on nomatch branches

        if( br.tid != kNoMatchTId )
          pt.pred_score_sec = p->locA[ br.loc_idx ].sec;
      }

      // The parents were read from particleA[] while the survivors were written to next_particleA[].
      std::swap(p->particleA, p->next_particleA);
    }

    //----------------------------------------------------------------------------------------
    // Debug reporting
    //

    // Return the location id of a location index, or -1 if the index is kInvalidIdx (matches the result file convention).
    int _loc_idx_to_report_id( const sf_t* p, unsigned loc_idx )
    {
      return loc_idx == kInvalidIdx ? -1 : (int)p->locA[loc_idx].loc_id;
    }

    // Return the measure number of a location index, or 0 if the index is kInvalidIdx (matches the result file convention).
    unsigned _loc_idx_to_report_meas( const sf_t* p, unsigned loc_idx )
    {
      return loc_idx == kInvalidIdx ? 0 : p->locA[loc_idx].meas_num;
    }

    // report_level >= 2: one line per note.
    // The population statistics describe the particles after resampling and the time_fact walk.
    void _report_note( const sf_t* p, unsigned uid, double perf_sec, unsigned pitch, unsigned loc_idx, const result_t& r )
    {
      double ss_mean, ss_std, tf_mean, tf_std;
      char   expect_buf[128];

      _particle_stats(p, ss_mean, ss_std, tf_mean, tf_std);

      expect_buf[0] = 0;

      // on mismatch lines list the pitches that the score expected at the reported location
      if( r.match_status_id == kMismatchTId && loc_idx != kInvalidIdx )
      {
        const loc_t& loc = p->locA[loc_idx];
        int          n   = snprintf(expect_buf, sizeof(expect_buf), " expect:");

        for(unsigned i=loc.beg_note_idx; i<=loc.end_note_idx && n>0 && (unsigned)n<sizeof(expect_buf); ++i)
          n += snprintf(expect_buf + n, sizeof(expect_buf) - n, " %i", p->noteA[i].pitch);
      }

      cwLogInfo("sf3 uid:%5i sec:%8.3f pitch:%3i -> loc:%5i meas:%3i %-8s conf:%.3f | score_sec mean:%8.3f std:%6.3f time_fact mean:%6.3f std:%6.3f%s",
                uid, perf_sec, pitch,
                _loc_idx_to_report_id(p, loc_idx), _loc_idx_to_report_meas(p, loc_idx),
                cwStringNullGuard(match_status_id_to_label(r.match_status_id)), r.confidence,
                ss_mean, ss_std, tf_mean, tf_std,
                expect_buf);
    }

    // report_level >= 3: the largest (loc_id,tid) keys of the current note and their posterior mass.
    void _report_map_table( const sf_t* p )
    {
      for(unsigned i=0; i<p->map_topN; ++i)
      {
        const map_key_t& k = p->map_topA[i];
        cwLogInfo("    map %i: loc:%5i meas:%3i %-8s mass:%.4f",
                  i, _loc_idx_to_report_id(p, k.loc_idx), _loc_idx_to_report_meas(p, k.loc_idx),
                  cwStringNullGuard(match_status_id_to_label(k.tid)), k.mass);
      }
    }

    // report_level >= 4: every particle after resampling and the time_fact walk.
    void _report_particles( const sf_t* p )
    {
      for(unsigned i=0; i<p->particleN; ++i)
      {
        const particle_t& pt = p->particleA[i];
        cwLogInfo("    particle %4i: score_sec:%8.3f time_fact:%6.4f loc:%5i weight:%.5f",
                  i, pt.pred_score_sec, pt.time_fact, _loc_idx_to_report_id(p, pt.pred_loc_idx), pt.weight);
      }
    }

    // Jitter each particle's time_fact by a multiplicative random walk.
    // This occurs after resampling so that copies of the same branch each receive their own tempo hypothesis.
    void _walk_time_fact( sf_t* p )
    {
      for(unsigned i=0; i<p->particleN; ++i)
      {
        particle_t& pt = p->particleA[i];
        pt.time_fact = _clamp_time_fact(p, pt.time_fact * std::exp(_rand_normal(p, 0.0, p->args.time_fact_walk_sigma)));
      }
    }

  }
}

cw::rc_t cw::score_follow_3::_create_impl( sf_t* p, const args_t& args )
{
  rc_t     rc                 = kOkRC;
  unsigned max_window_loc_cnt = 0;

  // sf_t was allocated with mem::allocZ() which does not run constructors, therefore construct the generator in place.
  // It is re-seeded on every reset().
  new (&p->rng) std::mt19937(args.rand_seed);

  // Each particle produces at most one match branch per window location, plus one mismatch and one nomatch branch.
  max_window_loc_cnt = _max_window_loc_count(p);

  p->particleN      = args.particle_cnt;
  p->particleA      = mem::allocZ<particle_t>(p->particleN);
  p->next_particleA = mem::allocZ<particle_t>(p->particleN);

  p->branchAllocN   = p->particleN * (max_window_loc_cnt + 2);
  p->branchA        = mem::allocZ<branch_t>(p->branchAllocN);
  p->branchN        = 0;

  p->match_massA      = mem::allocZ<double>(p->locN);
  p->mismatch_massA   = mem::allocZ<double>(p->locN);
  p->touched_loc_idxA = mem::allocZ<unsigned>(p->locN);
  p->touchedN         = 0;

  p->prev_perf_sec    = 0;
  p->last_loc_idx     = kInvalidIdx;
  p->done_fl          = false;

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Score follower implementation create failed.");
  return rc;
}

cw::rc_t cw::score_follow_3::_destroy_impl( sf_t* p )
{
  // Note that this call may occur without a prior call to _create_impl().
  // mem::release() of a null pointer is safe so every array is released unconditionally.
  // p->rng is trivially destructible and needs no cleanup.
  rc_t rc = kOkRC;

  mem::release(p->particleA);
  mem::release(p->next_particleA);
  mem::release(p->branchA);
  mem::release(p->match_massA);
  mem::release(p->mismatch_massA);
  mem::release(p->touched_loc_idxA);

  return rc;
}
  
cw::rc_t cw::score_follow_3::_reset_impl( sf_t* p )
{
  rc_t   rc        = kOkRC;
  double beg_sec   = p->locA[ p->active_beg_loc_idx ].sec;
  double init_wgt  = 1.0 / p->particleN;

  // Re-seed so that every performance produces the same output whether it is run alone or after others.
  p->rng.seed(p->args.rand_seed);

  // Every particle starts at the begin location (a point start) with its own tempo hypothesis.
  for(unsigned i=0; i<p->particleN; ++i)
  {
    particle_t& pt = p->particleA[i];
    pt.pred_score_sec = beg_sec;
    pt.pred_loc_idx   = kInvalidIdx;
    pt.time_fact      = _clamp_time_fact(p, _rand_normal(p, 1.0, p->args.time_fact_prior_sigma));
    pt.weight         = init_wgt;
  }

  p->branchN       = 0;
  p->prev_perf_sec = 0;
  p->last_loc_idx  = kInvalidIdx;
  p->done_fl       = false;

  // Clear the MAP accumulators. This is O(locN) but occurs only on reset(). During a note only the touched entries are cleared.
  for(unsigned i=0; i<p->locN; ++i)
  {
    p->match_massA[i]    = 0;
    p->mismatch_massA[i] = 0;
  }
  p->touchedN = 0;

  if( p->args.report_level >= 1 )
  {
    double ss_mean, ss_std, tf_mean, tf_std;
    _particle_stats(p, ss_mean, ss_std, tf_mean, tf_std);

    cwLogInfo("sf3 reset: loc_id:[%i,%i] loc_idx:[%i,%i] N:%i branch pool:%i (max wnd loc cnt:%i) time_fact: mean:%f std:%f",
              p->locA[ p->active_beg_loc_idx ].loc_id, p->locA[ p->active_end_loc_idx ].loc_id,
              p->active_beg_loc_idx, p->active_end_loc_idx,
              p->particleN, p->branchAllocN, p->branchAllocN / p->particleN - 2,
              tf_mean, tf_std );
  }

  return rc;
}

cw::rc_t cw::score_follow_3::_on_new_note_impl( sf_t* p, unsigned uid, double perf_sec, unsigned pitch, unsigned vel, result_t& result_ref )
{
  rc_t     rc      = kOkRC;
  unsigned loc_idx = kInvalidIdx;

  p->map_topN = 0;

  // An out of range pitch is reported as nomatch and the particles are neither updated nor resampled.
  // prev_perf_sec is not updated, so the next prediction covers the whole time since the particles were last advanced.
  if( !_check_pitch_range(p, uid, pitch, result_ref) )
    goto reportLabel;

  _predict(p, perf_sec);

  if((rc = _branch(p, pitch)) != kOkRC )
    goto errLabel;

  if((rc = _normalize_branches(p)) != kOkRC )
    goto errLabel;

  loc_idx = _report_map(p, result_ref);

  _resample(p);

  _walk_time_fact(p);

reportLabel:
  if( p->args.report_level >= 2 )
    _report_note(p, uid, perf_sec, pitch, loc_idx, result_ref);

  if( p->args.report_level >= 3 )
    _report_map_table(p);

  if( p->args.report_level >= 4 )
    _report_particles(p);

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Score follower note processing failed on uid:%i.",uid);
  return rc;
}

cw::rc_t cw::score_follow_3::_exec_impl( sf_t* p, double perf_sec )
{
  // In stage 1 the filter is entirely note driven and all of its steps occur in _on_new_note_impl().
  rc_t rc = kOkRC;
  return rc;
}

bool cw::score_follow_3::_is_done_impl( sf_t* p )
{
  // Stage 1: the follower is done once the last location of the active region has been reported
  // (see _report_map()) and remains done until the next reset(), even if later notes are reported
  // at earlier locations. The active region is a hard boundary for the search window, so no later
  // location can be reported. Detecting the end of the region more robustly is deferred to a later stage.
  return p->done_fl;
}
