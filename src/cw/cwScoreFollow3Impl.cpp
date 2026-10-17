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

    // Clamp the tempo so that time_fact = 1/tempo stays within [time_fact_min,time_fact_max].
    double _clamp_tempo( const sf_t* p, double tempo )
    {
      return std::clamp(tempo, 1.0 / p->args.time_fact_max, 1.0 / p->args.time_fact_min);
    }

    // Return the Kalman filter standard deviation of a particle's time_fact.
    // time_fact = 1/r therefore, to first order, sd(time_fact) = sd(r)/r^2.
    double _kf_time_fact_std( const particle_t& pt )
    {
      return std::sqrt(std::max(0.0, pt.cov_rr)) / (pt.tempo * pt.tempo);
    }

    // Calculate the population mean and standard deviation of pred_score_sec and time_fact,
    // and the population mean of each particle's own Kalman filter time_fact standard deviation.
    // Resampled copies of a particle are identical, so the population spread measures disagreement
    // between paths while the Kalman standard deviation measures each path's own tempo uncertainty.
    void _particle_stats( const sf_t* p, double& score_sec_mean_ref, double& score_sec_std_ref, double& time_fact_mean_ref, double& time_fact_std_ref, double& time_fact_kf_std_ref )
    {
      double ss_sum = 0, ss_sq_sum = 0;
      double tf_sum = 0, tf_sq_sum = 0;
      double kf_sum = 0;

      for(unsigned i=0; i<p->particleN; ++i)
      {
        const particle_t& pt        = p->particleA[i];
        double            time_fact = 1.0 / pt.tempo;

        ss_sum    += pt.pred_score_sec;
        ss_sq_sum += pt.pred_score_sec * pt.pred_score_sec;
        tf_sum    += time_fact;
        tf_sq_sum += time_fact * time_fact;
        kf_sum    += _kf_time_fact_std(pt);
      }

      score_sec_mean_ref   = ss_sum / p->particleN;
      time_fact_mean_ref   = tf_sum / p->particleN;
      time_fact_kf_std_ref = kf_sum / p->particleN;

      // max(0,...) guards against a tiny negative variance due to round-off
      score_sec_std_ref  = std::sqrt(std::max(0.0, ss_sq_sum / p->particleN - score_sec_mean_ref * score_sec_mean_ref));
      time_fact_std_ref  = std::sqrt(std::max(0.0, tf_sq_sum / p->particleN - time_fact_mean_ref * time_fact_mean_ref));
    }

    //----------------------------------------------------------------------------------------
    // Measurement model
    //

    // Return the variance of a particle's Kalman filter predictive density for the score time of the next onset:
    // S = var(s) + time_sigma_sec^2. It is always > 0 because time_sigma_sec > 0 (see _validate_args()).
    double _innovation_var( const sf_t* p, const particle_t& pt )
    {
      return pt.cov_ss + p->args.time_sigma_sec * p->args.time_sigma_sec;
    }

    // Gaussian time likelihood N(dsec; 0, sigma), where sigma is the standard deviation of the particle's predictive density.
    // The normalizing constant 1/(sigma*sqrt(2*pi)) is required: the match and mismatch time terms
    // are compared directly with the nomatch uniform density (1/window_dur_sec) and therefore all
    // three must be densities over score seconds.
    double _time_likelihood( double dsec, double sigma )
    {
      double z = dsec / sigma;
      return std::exp(-0.5 * z * z) / (sigma * std::sqrt(2.0 * M_PI));
    }

    // Return the count of notes at a location.
    unsigned _loc_note_count( const sf_t* p, unsigned loc_idx )
    {
      return p->locA[loc_idx].end_note_idx - p->locA[loc_idx].beg_note_idx + 1;
    }

    // Return the offset from loc.beg_note_idx of the note at a location with 'pitch', or kInvalidIdx if the location does not contain the pitch.
    // A location never contains the same pitch twice (see _check_for_duplicate_loc_pitches()) so the offset is unique.
    unsigned _loc_pitch_note_ofs( const sf_t* p, unsigned loc_idx, unsigned pitch )
    {
      const loc_t& loc = p->locA[loc_idx];
      for(unsigned i=loc.beg_note_idx; i<=loc.end_note_idx; ++i)
        if( p->noteA[i].pitch == pitch )
          return i - loc.beg_note_idx;
      return kInvalidIdx;
    }

    // Return the count of notes in a group.
    unsigned _grp_note_count( const sf_t* p, unsigned grp_idx )
    {
      return p->grpA[grp_idx].end_note_idx - p->grpA[grp_idx].beg_note_idx + 1;
    }

    // Return the particle's anchor group index.
    // Before the first match or mismatch the anchor is a virtual group just before the group of the first location
    // of the active region, so that group is d == 1.
    int _anchor_grp( const sf_t* p, const particle_t& pt )
    {
      return pt.anchor_grp_idx == kInvalidIdx ? (int)p->loc_grp_idxA[ p->active_beg_loc_idx ] - 1 : (int)pt.anchor_grp_idx;
    }

    // Return the jump distance d of a candidate group from the particle's anchor group.
    // The jump prior is over groups rather than locations, so the locations of a chord split across
    // several nearly simultaneous locations may be performed in any order without a backward jump.
    int _jump_dist( const sf_t* p, const particle_t& pt, unsigned grp_idx )
    {
      return (int)grp_idx - _anchor_grp(p, pt);
    }

    // Return the count of notes in the particle's anchor group which it has not yet matched.
    unsigned _unplayed_count( const sf_t* p, const particle_t& pt )
    {
      if( pt.anchor_grp_idx == kInvalidIdx )
        return 0;

      return _grp_note_count(p, pt.anchor_grp_idx) - std::popcount(pt.played_mask);
    }

    // Return the unnormalized jump prior weight of a candidate group at jump distance 'd'.
    // Staying in the anchor group is only possible while it has unplayed notes - a played note can not be matched again,
    // so a repeated note must be explained by a backward jump or a nomatch.
    double _jump_weight( const sf_t* p, int d, unsigned unplayedN )
    {
      if( d == 0 )
        return unplayedN > 0 ? p->args.jump_stay : 0;

      if( d == 1 )
        return p->args.jump_next;

      if( d > 1 )
        return p->args.jump_skip * std::pow(p->args.jump_decay, d - 2);

      return p->args.jump_back * std::pow(p->args.jump_decay, -d - 1);
    }

    // Append a branch to the branch pool.
    // Zero weight branches are not added. They can never be selected by resampling and would only consume the pool.
    rc_t _add_branch( sf_t* p, unsigned parent_idx, match_status_id_t tid, unsigned loc_idx, unsigned note_ofs, double weight )
    {
      if( !(weight > 0) )
        return kOkRC;

      // The pool bound is conservative, so this should never occur, but report it rather than overflowing.
      if( p->branchN >= p->branchAllocN )
        return cwLogError(kBufTooSmallRC,"The branch pool is full (%i branches).",p->branchAllocN);

      branch_t& br  = p->branchA[ p->branchN++ ];
      br.parent_idx = parent_idx;
      br.tid        = tid;
      br.loc_idx    = loc_idx;
      br.note_ofs   = note_ofs;
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

    // Kalman filter prediction: advance each particle's state by the elapsed performance time D since the previous note.
    //
    //   s <- s + r*D                  F = [[1, D],[0, 1]]
    //   P <- F P F' + Q(D)            Q = diag( pos_walk_sigma^2 * D, time_fact_walk_sigma^2 * D )
    //
    // The process noise grows with D so the notes of a chord, which arrive almost together, add almost none.
    void _predict( sf_t* p, double perf_sec )
    {
      // Notes arrive in time order. A negative interval would make Q negative, therefore it is treated as zero.
      double D  = std::max(0.0, perf_sec - p->prev_perf_sec);
      double qs = p->args.pos_walk_sigma       * p->args.pos_walk_sigma       * D;
      double qr = p->args.time_fact_walk_sigma * p->args.time_fact_walk_sigma * D;

      for(unsigned i=0; i<p->particleN; ++i)
      {
        particle_t& pt = p->particleA[i];

        pt.pred_score_sec += pt.tempo * D;

        // cov_ss is updated first because it uses the prior cov_sr and cov_rr
        pt.cov_ss += 2.0 * D * pt.cov_sr + D * D * pt.cov_rr + qs;
        pt.cov_sr += D * pt.cov_rr;
        pt.cov_rr += qr;
      }

      p->prev_perf_sec = perf_sec;
    }

    // Kalman filter update of a particle's state with the observation that the current onset is at score time 'loc_sec'.
    //
    //   S = cov_ss + R    K = [cov_ss, cov_sr]' / S    x <- x + K*(loc_sec - s)    P <- P - K*[cov_ss, cov_sr]
    //
    // The update moves both the position and the tempo, which is how the tempo is learned. Unlike a hard snap to
    // loc_sec it keeps part of the prediction error, so a path which jumped to the wrong location keeps
    // a lasting position error which later notes continue to penalize.
    void _kalman_update( const sf_t* p, particle_t& pt, double loc_sec )
    {
      double S  = _innovation_var(p, pt);
      double ks = pt.cov_ss / S;
      double kr = pt.cov_sr / S;
      double y  = loc_sec - pt.pred_score_sec;

      pt.pred_score_sec += ks * y;
      pt.tempo           = _clamp_tempo(p, pt.tempo + kr * y);

      // cov_rr and cov_sr are updated before cov_ss because they use the prior cov_ss and cov_sr
      pt.cov_rr -= kr * pt.cov_sr;
      pt.cov_sr -= ks * pt.cov_sr;
      pt.cov_ss -= ks * pt.cov_ss;
    }

    // A particle's candidate locations: its search window [beg_loc_idx,end_loc_idx], widened to whole groups,
    // plus up to two extra groups, the anchor group and the group after it, when they fall outside the window.
    // The extra groups guarantee that the follower can always stay on or advance from its anchor,
    // even when the predicted time has drifted away from it. All candidates are clipped to the active region.
    typedef struct cand_str
    {
      unsigned beg_loc_idx;       // window locations
      unsigned end_loc_idx;
      unsigned beg_grp_idx;       // groups of the first and last window locations
      unsigned end_grp_idx;
      double   wnd_dur_sec;       // window duration used by the nomatch time density (see _window())
      unsigned extra_grp_idxA[2]; // extra groups
      unsigned extra_begA[2];     // first and last (inclusive) location of each extra group
      unsigned extra_endA[2];
      unsigned extraN;
    } cand_t;

    void _candidates( const sf_t* p, const particle_t& pt, cand_t& c )
    {
      _window(p, pt.pred_score_sec, c.beg_loc_idx, c.end_loc_idx, c.wnd_dur_sec);

      // Widen the window so that it never holds part of a group. Otherwise the jump prior, which is
      // normalized over groups, would give a partial group the mass of a whole one.
      c.beg_loc_idx = std::max(p->grpA[ p->loc_grp_idxA[c.beg_loc_idx] ].beg_loc_idx, p->active_beg_loc_idx);
      c.end_loc_idx = std::min(p->grpA[ p->loc_grp_idxA[c.end_loc_idx] ].end_loc_idx, p->active_end_loc_idx);
      c.beg_grp_idx = p->loc_grp_idxA[c.beg_loc_idx];
      c.end_grp_idx = p->loc_grp_idxA[c.end_loc_idx];

      // Before the first match the only extra candidate is the group of the first location of the active region.
      unsigned next_grp_idx = pt.anchor_grp_idx == kInvalidIdx ? p->loc_grp_idxA[ p->active_beg_loc_idx ] : pt.anchor_grp_idx + 1;
      unsigned gA[]         = { pt.anchor_grp_idx, next_grp_idx };

      c.extraN = 0;
      for(unsigned grp_idx : gA)
        if( grp_idx != kInvalidIdx && grp_idx < p->grpN && (grp_idx < c.beg_grp_idx || c.end_grp_idx < grp_idx) )
        {
          unsigned b = std::max(p->grpA[grp_idx].beg_loc_idx, p->active_beg_loc_idx);
          unsigned e = std::min(p->grpA[grp_idx].end_loc_idx, p->active_end_loc_idx);

          // skip groups outside the active region
          if( b <= e )
          {
            c.extra_grp_idxA[ c.extraN ] = grp_idx;
            c.extra_begA[ c.extraN ]     = b;
            c.extra_endA[ c.extraN ]     = e;
            c.extraN += 1;
          }
        }
    }

    unsigned _cand_wnd_count( const cand_t& c )
    {
      return c.end_loc_idx - c.beg_loc_idx + 1;
    }

    unsigned _cand_count( const cand_t& c )
    {
      unsigned n = _cand_wnd_count(c);
      for(unsigned i=0; i<c.extraN; ++i)
        n += c.extra_endA[i] - c.extra_begA[i] + 1;
      return n;
    }

    // Return the location index of the k'th candidate, 0 <= k < _cand_count(). The window locations come first.
    unsigned _cand_loc_idx( const cand_t& c, unsigned k )
    {
      unsigned wndN = _cand_wnd_count(c);

      if( k < wndN )
        return c.beg_loc_idx + k;

      k -= wndN;
      for(unsigned i=0; i<c.extraN; ++i)
      {
        unsigned n = c.extra_endA[i] - c.extra_begA[i] + 1;
        if( k < n )
          return c.extra_begA[i] + k;
        k -= n;
      }

      return kInvalidIdx; // not reachable for k < _cand_count()
    }

    // Expand each particle into its candidate branches and weight each branch by
    // parent.weight * prior(tid) * jump prior * likelihood.
    //
    // Match    : prior_match    * J(d) * N(loc.sec - pred_score_sec; 0, sqrt(S)) * 1/k
    // Mismatch : prior_mismatch * J(d) * N(nearest.sec - pred_score_sec; 0, sqrt(S)) * 1/(P-k)
    // NoMatch  : prior_nomatch  * 1/window_dur_sec * 1/P
    //
    // where S is the variance of the particle's Kalman filter predictive density (_innovation_var()),
    // P is the size of the pitch space, d is the distance of the candidate location's group from the
    // particle's anchor group and J(d) is the jump prior (_jump_weight()) normalized over the particle's
    // candidate groups. For a match k is the count of notes in the candidate location's group, which is
    // treated as one chord; on a match in the anchor group (d == 0) k is replaced by the count of its
    // unplayed notes, since the performed note must be one of them. For a mismatch k is the count of
    // notes at the nearest location.
    //
    // Because J sums to 1 over the candidates, prior_match is the total probability of a match
    // regardless of how many candidate locations contain the pitch, and the summed match term is
    // a density over score seconds in the same units as the nomatch term.
    rc_t _branch( sf_t* p, unsigned pitch )
    {
      rc_t     rc = kOkRC;
      unsigned P  = p->args.pitch_max - p->args.pitch_min + 1;

      p->branchN = 0;

      for(unsigned i=0; i<p->particleN; ++i)
      {
        const particle_t& pt              = p->particleA[i];
        unsigned          unplayedN       = _unplayed_count(p, pt);
        unsigned          nearest_loc_idx = kInvalidIdx;
        double            nearest_dsec    = 0;
        double            jump_sum        = 0;
        double            pred_sigma      = std::sqrt(_innovation_var(p, pt));
        cand_t            c;

        _candidates(p, pt, c);

        unsigned candN = _cand_count(c);

        // Sum the jump weights over every candidate group (not only those with the pitch)
        // because J is the prior over where the next performed note comes from.
        // Each group is counted once however many of its locations are candidates.
        for(unsigned grp_idx=c.beg_grp_idx; grp_idx<=c.end_grp_idx; ++grp_idx)
          jump_sum += _jump_weight(p, _jump_dist(p, pt, grp_idx), unplayedN);

        for(unsigned k=0; k<c.extraN; ++k)
          jump_sum += _jump_weight(p, _jump_dist(p, pt, c.extra_grp_idxA[k]), unplayedN);

        // Match branches: one for each candidate location which contains the performed pitch.
        for(unsigned k=0; k<candN && jump_sum>0; ++k)
        {
          unsigned loc_idx  = _cand_loc_idx(c, k);
          unsigned grp_idx  = p->loc_grp_idxA[loc_idx];
          double   dsec     = p->locA[loc_idx].sec - pt.pred_score_sec;
          int      d        = _jump_dist(p, pt, grp_idx);
          unsigned note_ofs = _loc_pitch_note_ofs(p, loc_idx, pitch);

          // track the window location nearest in time to the predicted score time
          if( k < _cand_wnd_count(c) && (nearest_loc_idx == kInvalidIdx || std::fabs(dsec) < std::fabs(nearest_dsec)) )
          {
            nearest_loc_idx = loc_idx;
            nearest_dsec    = dsec;
          }

          if( note_ofs == kInvalidIdx )
            continue;

          // offset of the note within its group - its played_mask bit
          unsigned grp_note_ofs = p->locA[loc_idx].beg_note_idx + note_ofs - p->grpA[grp_idx].beg_note_idx;

          // The 1/k term keeps a chord from receiving k times the evidence of a single note.
          double pitch_n = _grp_note_count(p, grp_idx);

          if( d == 0 )
          {
            // a note which the particle has already matched in its anchor group can not be matched again
            if( pt.played_mask & (uint64_t(1) << grp_note_ofs) )
              continue;

            pitch_n = unplayedN;
          }

          double jw = _jump_weight(p, d, unplayedN) / jump_sum;
          double w  = pt.weight * p->args.prior_match * jw * _time_likelihood(dsec, pred_sigma) / pitch_n;

          if((rc = _add_branch(p, i, kMatchTId, loc_idx, grp_note_ofs, w)) != kOkRC )
            goto errLabel;
        }

        // Mismatch branch: the note is assigned to the window location nearest in time.
        // It is skipped if that location contains the pitch because the match branch already covers that case.
        if( nearest_loc_idx != kInvalidIdx && _loc_pitch_note_ofs(p, nearest_loc_idx, pitch) == kInvalidIdx )
        {
          unsigned k = _loc_note_count(p, nearest_loc_idx);

          // a mismatch is impossible if every pitch in the pitch space is present at the location
          if( k < P )
          {
            double jw = _jump_weight(p, _jump_dist(p, pt, p->loc_grp_idxA[nearest_loc_idx]), unplayedN) / jump_sum;
            double w  = pt.weight * p->args.prior_mismatch * jw * _time_likelihood(nearest_dsec, pred_sigma) / (P - k);

            if((rc = _add_branch(p, i, kMismatchTId, nearest_loc_idx, kInvalidIdx, w)) != kOkRC )
              goto errLabel;
          }
        }

        // NoMatch branch: the note is unrelated to any score location.
        // Its time density is uniform over the particle's window.
        {
          double w = pt.weight * p->args.prior_nomatch / c.wnd_dur_sec / P;

          if((rc = _add_branch(p, i, kNoMatchTId, kInvalidIdx, kInvalidIdx, w)) != kOkRC )
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
      if( best_loc_idx == p->active_end_loc_idx && !p->done_fl )
      {
        p->done_fl = true;

        if( p->args.report_level >= 1 )
          cwLogInfo("sf3 done: end loc:%i",p->locA[ best_loc_idx ].loc_id);
      }

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

    // Stratified resampling of the branch pool back to particleN particles.
    // The pointers (i + u_i)/N, with an independent uniform draw u_i in [0,1) for each pointer,
    // are walked against the cumulative branch weights. Each selected branch becomes one particle.
    //
    // Why not systematic resampling (a single u shared by every pointer): the branches of each parent are
    // contiguous in the pool and, when the parents are identical copies, every parent's block has the same
    // layout and exactly 1/N of the mass. A shared u then places every pointer at the same offset within its
    // parent's block, so the whole population selects the same hypothesis together - a 2% branch takes
    // every particle 2% of the time. Independent draws let each particle choose independently.
    void _resample( sf_t* p )
    {
      double                                 N        = p->particleN;
      double                                 init_wgt = 1.0 / N;
      std::uniform_real_distribution<double> dist(0.0, 1.0);
      unsigned                               j        = 0;
      double                                 cum      = p->branchA[0].weight;

      for(unsigned i=0; i<p->particleN; ++i)
      {
        double ptr = (i + dist(p->rng)) * init_wgt;

        // The j guard protects against the cumulative sum ending slightly below 1.0 due to round-off.
        while( ptr > cum && j < p->branchN-1 )
          cum += p->branchA[ ++j ].weight;

        const branch_t&   br     = p->branchA[j];
        const particle_t& parent = p->particleA[ br.parent_idx ];
        particle_t&       pt     = p->next_particleA[i];

        // Match and mismatch branches update the Kalman filter with the location's time and move the anchor to its group.
        // Nomatch branches keep the parent's predicted state, anchor and played notes.
        pt        = parent;
        pt.weight = init_wgt;

        if( br.tid != kNoMatchTId )
        {
          _kalman_update(p, pt, p->locA[ br.loc_idx ].sec);

          unsigned grp_idx = p->loc_grp_idxA[ br.loc_idx ];

          // moving to a new anchor group starts a new set of played notes
          if( grp_idx != parent.anchor_grp_idx )
          {
            pt.anchor_grp_idx = grp_idx;
            pt.played_mask    = 0;
          }

          // It is not known which score note a mismatch replaced, so only a match marks a note as played.
          if( br.tid == kMatchTId )
            pt.played_mask |= uint64_t(1) << br.note_ofs;
        }
      }

      // The parents were read from particleA[] while the survivors were written to next_particleA[].
      std::swap(p->particleA, p->next_particleA);
    }

    //----------------------------------------------------------------------------------------
    // End of region timeout
    //

    // Fill rem_max_ioi_secA[] over the active region: the largest score time gap between consecutive locations
    // from each location to the end of the region. The last location has no following onset, so its value is 0
    // and the duration of the final chord never delays the timeout.
    void _fill_rem_max_ioi( sf_t* p )
    {
      unsigned b = p->active_beg_loc_idx;
      unsigned e = p->active_end_loc_idx;

      p->rem_max_ioi_secA[e] = 0;
      for(unsigned i=e; i>b; --i)
        p->rem_max_ioi_secA[i-1] = std::max(p->rem_max_ioi_secA[i], p->locA[i].sec - p->locA[i-1].sec);
    }

    // The timeout is armed while the last reported location is within end_zone_sec of the end of the active region.
    // The reported location is used, rather than the particles' predicted position, because the prediction keeps
    // advancing during silence and could therefore arm the timeout during a long pause before the zone.
    bool _end_timeout_armed( const sf_t* p )
    {
      return p->last_loc_idx != kInvalidIdx && p->locA[ p->last_loc_idx ].sec >= p->locA[ p->active_end_loc_idx ].sec - p->args.end_zone_sec;
    }

    // Return the silence, in performance seconds, after which the follower is done.
    // time_fact_max is used, rather than the current time_fact estimate, because the estimate lags exactly when
    // the performer slows down - which is common at the end of a section.
    double _end_timeout_sec( const sf_t* p )
    {
      return p->args.done_timeout_mult * p->args.time_fact_max * p->rem_max_ioi_secA[ p->last_loc_idx ] + p->args.done_timeout_min_sec;
    }

    // Partition the score locations into groups (see grp_t).
    // A group starts at location i and takes each following location j while locA[j].sec - locA[i].sec <= loc_group_sec.
    // The span is measured from the first location, rather than between neighbours, so that a fast run of
    // notes can not chain into one long group. A group also ends before it would hold more than kMaxLocNoteN notes
    // (the size of particle_t.played_mask). With loc_group_sec == 0 every location is its own group.
    void _form_groups( sf_t* p )
    {
      p->grpN            = 0;
      p->max_grp_loc_cnt = 0;

      for(unsigned i=0; i<p->locN; )
      {
        unsigned j = i + 1;

        while( j < p->locN
               && p->args.loc_group_sec > 0
               && p->locA[j].sec - p->locA[i].sec <= p->args.loc_group_sec
               && p->locA[j].end_note_idx - p->locA[i].beg_note_idx + 1 <= kMaxLocNoteN )
          ++j;

        grp_t& g       = p->grpA[ p->grpN ];
        g.beg_loc_idx  = i;
        g.end_loc_idx  = j - 1;
        g.beg_note_idx = p->locA[i].beg_note_idx;
        g.end_note_idx = p->locA[j-1].end_note_idx;

        for(unsigned k=i; k<j; ++k)
          p->loc_grp_idxA[k] = p->grpN;

        p->max_grp_loc_cnt = std::max(p->max_grp_loc_cnt, j - i);
        p->grpN           += 1;
        i                  = j;
      }
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
    // The population statistics describe the particles after resampling and the Kalman filter update.
    // kf_std is the mean of the particles' own Kalman filter time_fact standard deviations.
    void _report_note( const sf_t* p, unsigned uid, double perf_sec, unsigned pitch, unsigned loc_idx, const result_t& r )
    {
      double ss_mean, ss_std, tf_mean, tf_std, tf_kf_std;
      char   expect_buf[128];

      _particle_stats(p, ss_mean, ss_std, tf_mean, tf_std, tf_kf_std);

      expect_buf[0] = 0;

      // on mismatch lines list the pitches that the score expected at the reported location
      if( r.match_status_id == kMismatchTId && loc_idx != kInvalidIdx )
      {
        const loc_t& loc = p->locA[loc_idx];
        int          n   = snprintf(expect_buf, sizeof(expect_buf), " expect:");

        for(unsigned i=loc.beg_note_idx; i<=loc.end_note_idx && n>0 && (unsigned)n<sizeof(expect_buf); ++i)
          n += snprintf(expect_buf + n, sizeof(expect_buf) - n, " %i", p->noteA[i].pitch);
      }

      cwLogInfo("sf3 uid:%5i sec:%8.3f pitch:%3i -> loc:%5i meas:%3i %-8s conf:%.3f | score_sec mean:%8.3f std:%6.3f time_fact mean:%6.3f std:%6.3f kf_std:%6.3f%s",
                uid, perf_sec, pitch,
                _loc_idx_to_report_id(p, loc_idx), _loc_idx_to_report_meas(p, loc_idx),
                cwStringNullGuard(match_status_id_to_label(r.match_status_id)), r.confidence,
                ss_mean, ss_std, tf_mean, tf_std, tf_kf_std,
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

    // report_level >= 4: every particle after resampling and the Kalman filter update.
    void _report_particles( const sf_t* p )
    {
      for(unsigned i=0; i<p->particleN; ++i)
      {
        const particle_t& pt = p->particleA[i];
        // the anchor is reported as the location id of the first location of the anchor group
        unsigned anchor_loc_idx = pt.anchor_grp_idx == kInvalidIdx ? kInvalidIdx : p->grpA[ pt.anchor_grp_idx ].beg_loc_idx;

        cwLogInfo("    particle %4i: score_sec:%8.3f sd:%6.3f time_fact:%6.4f sd:%6.4f anchor:%5i played:0x%llx weight:%.5f",
                  i, pt.pred_score_sec, std::sqrt(std::max(0.0, pt.cov_ss)), 1.0 / pt.tempo, _kf_time_fact_std(pt),
                  _loc_idx_to_report_id(p, anchor_loc_idx), (unsigned long long)pt.played_mask, pt.weight);
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

  // particle_t.played_mask holds one bit per note of a location
  for(unsigned i=0; i<p->locN; ++i)
    if( _loc_note_count(p, i) > kMaxLocNoteN )
    {
      rc = cwLogError(kInvalidArgRC,"The score location %i has %i notes. The maximum is %i.",p->locA[i].loc_id,_loc_note_count(p, i),kMaxLocNoteN);
      goto errLabel;
    }

  // There can be no more groups than locations.
  p->grpA         = mem::allocZ<grp_t>(p->locN);
  p->loc_grp_idxA = mem::allocZ<unsigned>(p->locN);
  _form_groups(p);

  // Each particle produces at most one match branch per candidate location plus one mismatch and one nomatch branch.
  // The candidates are the window (see _max_window_loc_count()), which widening to whole groups can extend by
  // max_grp_loc_cnt-1 locations at each end, plus the anchor group and the group after it (see _candidates()).
  max_window_loc_cnt = _max_window_loc_count(p);
  p->max_wnd_loc_cnt = max_window_loc_cnt;

  p->particleN      = args.particle_cnt;
  p->particleA      = mem::allocZ<particle_t>(p->particleN);
  p->next_particleA = mem::allocZ<particle_t>(p->particleN);

  p->branchAllocN   = p->particleN * (max_window_loc_cnt + 2*(p->max_grp_loc_cnt - 1) + 2*p->max_grp_loc_cnt + 2);
  p->branchA        = mem::allocZ<branch_t>(p->branchAllocN);
  p->branchN        = 0;

  p->rem_max_ioi_secA = mem::allocZ<double>(p->locN);
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
  mem::release(p->grpA);
  mem::release(p->loc_grp_idxA);
  mem::release(p->rem_max_ioi_secA);

  return rc;
}
  
cw::rc_t cw::score_follow_3::_reset_impl( sf_t* p )
{
  rc_t   rc        = kOkRC;
  double beg_sec   = p->locA[ p->active_beg_loc_idx ].sec;
  double init_wgt  = 1.0 / p->particleN;

  // Re-seed so that every performance produces the same output whether it is run alone or after others.
  p->rng.seed(p->args.rand_seed);

  // Every particle starts at the begin location (a point start, var(s) = 0) at the nominal tempo r = 1.
  // The tempo prior is carried by the Kalman covariance rather than by a spread of particle tempos.
  // Since r is close to 1 the time_fact prior standard deviation is also used as the standard deviation of r.
  for(unsigned i=0; i<p->particleN; ++i)
  {
    particle_t& pt = p->particleA[i];
    pt.pred_score_sec = beg_sec;
    pt.tempo          = 1.0;
    pt.cov_ss         = 0;
    pt.cov_sr         = 0;
    pt.cov_rr         = p->args.time_fact_prior_sigma * p->args.time_fact_prior_sigma;
    pt.anchor_grp_idx = kInvalidIdx;
    pt.played_mask    = 0;
    pt.weight         = init_wgt;
  }

  p->branchN            = 0;
  p->prev_perf_sec      = 0;
  p->last_note_perf_sec = 0;
  p->last_loc_idx       = kInvalidIdx;
  p->done_fl            = false;

  _fill_rem_max_ioi(p);

  // Clear the MAP accumulators. This is O(locN) but occurs only on reset(). During a note only the touched entries are cleared.
  for(unsigned i=0; i<p->locN; ++i)
  {
    p->match_massA[i]    = 0;
    p->mismatch_massA[i] = 0;
  }
  p->touchedN = 0;

  if( p->args.report_level >= 1 )
  {
    double ss_mean, ss_std, tf_mean, tf_std, tf_kf_std;
    _particle_stats(p, ss_mean, ss_std, tf_mean, tf_std, tf_kf_std);

    cwLogInfo("sf3 reset: loc_id:[%i,%i] loc_idx:[%i,%i] N:%i branch pool:%i (max wnd loc cnt:%i) groups:%i (max locs:%i) time_fact: mean:%f kf_std:%f",
              p->locA[ p->active_beg_loc_idx ].loc_id, p->locA[ p->active_end_loc_idx ].loc_id,
              p->active_beg_loc_idx, p->active_end_loc_idx,
              p->particleN, p->branchAllocN, p->max_wnd_loc_cnt, p->grpN, p->max_grp_loc_cnt,
              tf_mean, tf_kf_std );
  }

  return rc;
}

cw::rc_t cw::score_follow_3::_on_new_note_impl( sf_t* p, unsigned uid, double perf_sec, unsigned pitch, unsigned vel, result_t& result_ref )
{
  rc_t     rc      = kOkRC;
  unsigned loc_idx = kInvalidIdx;

  p->map_topN = 0;

  // Every note, including one outside the pitch range, is activity for the end of region timeout.
  p->last_note_perf_sec = perf_sec;

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
  // exec() only evaluates the end of region timeout, since is_done() has no notion of the current time.
  rc_t rc = kOkRC;

  if( !p->done_fl && _end_timeout_armed(p) )
  {
    double silence_sec = perf_sec - p->last_note_perf_sec;
    double timeout_sec = _end_timeout_sec(p);

    if( silence_sec > timeout_sec )
    {
      p->done_fl = true;

      if( p->args.report_level >= 1 )
        cwLogInfo("sf3 done: timeout silence:%.2f timeout:%.2f last loc:%i",silence_sec,timeout_sec,p->locA[ p->last_loc_idx ].loc_id);
    }
  }

  return rc;
}

bool cw::score_follow_3::_is_done_impl( sf_t* p )
{
  // The follower is done once the last location of the active region has been reported (see _report_map())
  // or the end of region timeout has expired (see _exec_impl()), and remains done until the next reset(),
  // even if later notes are reported at earlier locations.
  return p->done_fl;
}
