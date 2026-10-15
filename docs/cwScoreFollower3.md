`score_follow_3 ` (sf3) is the shell of an online, real-time, MIDI score follower. 
The source code is in `src/cw/cwScoreFollow3.h` and `src/cw/cwScoreFollow3.cpp`.

A testing harness is contained in `src/cw/cwScoreFollow3Test.cpp`. 
An example set of arguments for the harness is contained in the `sf3` stanza of `apps/cli/cfg/main.cfg`.

As it is sf3 has no implementation it only sets up the data structures to allow
flexible searching of the score. The actual implementation will be contained in
`src/cw/cwScoreFollow3Impl.h` and `src/cw/cwScoreFollow3Impl.cpp`.


The primary instrument sf3 is intended to follow is piano.

During runtime the input to sf3 is a series of single time stamped
MIDI notes.  Even when notes are played as chords they will be
submitted one a time.  The time stamps of the incoming notes will
always be in increasing order.  Note that the shell of `on_new_note()`
notices the time of the first performed note following a `reset()` and
subtracts that value from all subsequent notes. This sets the initial
performance time to 0.0 seconds and all successive note are offset
from 0.0.

The output of the follower is the score event `loc_id` which best matches
the performed note as provided by `on_new_note()` and the status of that
match as given by `match_status_id`. If `match_status_id` is `kNoMatchTId`
then the `loc_id` is set to `kInvalidId`. `match_status_id` is
`kBlankTId` when the note was not evaluated. The result also carries a
`confidence` value in the range [0,1] (see 'Final location reporting' below).
As part of our development we will likely report more information
regarding the match but for now this is the minimum required output.

The score is stored in `sf_t.locA[]` and `sf_t.noteA[]` and is in
increasing time order.  The score is linear, there are no structural
repeats or jumps.  Each cell in `sf_t.locA[]` represents a time point
in the score where one or more notes are played. The notes associated
with a given `loc_t` are contained in the `sf_t.noteA[]` index range
`loc_t.beg_note_idx` to `loc_t.end_note_idx`. Note that a given
location will never refer to the same pitch more than once.

A score following session always begins from a known location as
defined by the `beg_loc_id` given to the `reset()` function.  The
player is expected to start from this location and move mostly forward
throught the score.  The follower is not expected, for example, to
notice that the performer has had a false start and is starting again.
Or is jumping to a section outside of the reset region.

Small jumps (note skips forward or backwards) which occur as part of
an ordinary performance as the player recovers from mistakes and the
follower is expected to track these discontinous movements.

Under ideal circumstances we expect that the performance time will map
proportionately to the score time.  In other words the performance
time may be slightly faster or slower than the associated score time
but both will always increase at approxomiately the same rate.

In practice however it is possible that even an accurate performance
will require the the follower to go slightly backwards in score time
to match to a performed note.

There are at least two possible reasons for this:
1. Chords: The performed notes of chords arrive serially therefore later notes will 
have later timestamps but their matching score location will be the same as the 
first note of the chord. All notes after the first note will
therefore appear to force the follower to go backwards in time.

2. Hand asynchrony:  Notes that are separated by a small musical interval but played by different
hands may easily be played slightly out of order.  

Both of these cases will likely be perceived by the listener as occuring simultaneously but
will appear to the follower as being out of order.

Given a performed note there are 3 possible matching classifications:
1. 'Match' : The pitch of the note is found to match the the pitch of a nearby score location.
2. 'Mismatch' : The note time stamp matches to a score location but does not match a pitch at that location.
In effect the note was played at the right time but with the wrong pitch.
3. 'NoMatch': This note matches in neither pitch or time to a nearby score note.

These three classifications form a probability space and we can maintain
a belief about the likelihood of a given a match being classified accordingly.
Each classification has a distinct probability density function which
we seek to model.

We would like to design sf3 in stages. We are currently only concerned with the initial stage.

During the first stage we will build a minimal working version base on a particle filter tracking approach.
All of the parts of a particle filter (prediction,branching,likelihood estimation,resampling,final location reporting)
will be implemented but the filters themselves will be as simple as possible.
The resulting follower may not be as effective as is possible but it will be easily understandable.
Above all the code will represent correct probabilistic modeling.
It will also be well commented and organized to aid human reading.

The first version of the filter will therefore only rely on matching a performed notes
to a score note based on time distance to matching pitches and exact pitch matches.
The only complication we will include in this version is a `time_factor` for mapping performance 
time to score time. The `time_factor` is intended to compensate for performances that are 
slightly faster or slower than the score prescribes. In keeping with the 'simple as 
possible' guideline we will choose the simplest possible means of estimating
the value of `time_factor`. 

The proposed particle state variables will therefore be: 
1. `particle_t.pred_score_sec`: The particles current score time prediction.
2. `particle_t.time_fact`: The particles current estimated `time_factor` value.
3. `particle_t.pred_loc_idx`: The particles estimated score location or `kInvalidIdx` if it
believes that it represents a time between score locations

Later improved versions of the follower will take advantage of other
conditions and measurements like transition likelihoods
(backward,same,forward), repeated matches, chord-notes, pitch error distance, the cost
of skipped events, velocity differences, known tempo changes, note
voice assignments, grace notes, among others.

The `args_t` struct contains the sf3 configuration parameters.
As currently written `args_t` includes a search window width definition which specifies how far
a given particle should search before and after it's predicted time to look for a matching location.
The backwards portion of the window will have a duration of at least `wnd_pre_sec` and span at least `wnd_pre_loc_cnt` locations.
The forwards portion of the window will have a duration of at least `wnd_post_sec` and span at least `wnd_post_loc_cnt` locations.

Feel free to add parameters to `args_t` to support whatever configuration parameters
may be needed for the follower's implementation.

Since sf3 is intended for real-time use no memory may be allocated inside of
either the `reset()`, `on_new_note()`, `exec()` or `is_done()` functions.
All dynamic memory must therefore be allocated in `create()`.

As is easily  seen by the existing code, the library that you will be developing
code for has a highly opinionated style. Do your best to stay withing that style.

Feel free to use C++ 20 and below standard library functions, but try to 
avoid using the standard container classes.


Build directions: `cmake --build ../../build/debug --preset debug`
Run directions: `../../build/debug/bin/cli apps/cli/cfg/main.cfg sf3`


# Stage 1 design

This section records the design decisions made for the first stage of the follower.

## Overview

The filter is entirely note-driven. All of the particle filter steps
(prediction, branching, likelihood estimation, resampling, and final
location reporting) occur inside `on_new_note()`.  `exec()` is left
empty for stage 1. The shell of `exec()` returns immediately if the
first note following a `reset()` has not yet arrived or if `is_done()`
is true.

`is_done()` is not critical in stage 1. The active region set by `reset()` is a hard boundary
(see 'Search window') so `is_done()` may simply report when the follower reaches
`active_end_loc_idx`.  Later stages will clarify how the end of the region should be detected.

## Particle state and initialization

Each particle holds `pred_score_sec`, `time_fact`, `pred_loc_idx` and `weight`.

On `reset()`:
- Every particle starts at the begin location (a point start):
  `pred_score_sec = locA[active_beg_loc_idx].sec` and `pred_loc_idx = kInvalidIdx`.
- `time_fact` is drawn from N(1.0, `time_fact_prior_sigma`) and clamped to [`time_fact_min`, `time_fact_max`].
- All weights are set to 1/N.
- The random number generator (a `std::mt19937` held in `sf_t`) is re-seeded from `rand_seed`
  so that test runs are exactly reproducible.

Because `on_new_note()` rebases the performance time to 0.0 at the first note,
the first note is scored against the begin location and `time_fact` only
begins to influence the prediction from the second note onward.

## Prediction

When a note arrives each particle advances its predicted score time by the
elapsed performance time since the previous note:

    pred_score_sec += delta_perf_sec / time_fact

## Search window

Each particle searches a window of locations around its `pred_score_sec`.

- The backward extent spans at least `wnd_pre_sec` seconds and at least
  `wnd_pre_loc_cnt` locations, whichever is larger.
- The forward extent spans at least `wnd_post_sec` seconds and at least
  `wnd_post_loc_cnt` locations, whichever is larger.
- The window is hard clipped to the active region `[active_beg_loc_idx, active_end_loc_idx]` set by `reset()`.

## Branching

Each particle is expanded into a set of candidate hypotheses (branches) drawn from its search window.

- Match branches: one branch for each location in the window which contains the performed pitch.
- Mismatch branch: at most one branch per particle. It assigns the note to the window
  location nearest in time to `pred_score_sec`. If that location contains the performed pitch
  the mismatch branch is skipped, because the match branch at that location already covers the case.
- NoMatch branch: exactly one branch per particle which assigns the note to no location.

Each branch's weight is `parent.weight * prior(tid) * likelihood` (see 'Measurement model').

On a match or mismatch branch the resulting particle's `pred_score_sec` snaps to `loc.sec`
and `pred_loc_idx` is set to that location. On a nomatch branch the parent's predicted
state is retained and `pred_loc_idx` is set to `kInvalidIdx`.

The branch pool is preallocated in `create()`. Its size is
`particle_cnt * (max_window_loc_cnt + 2)`, where `max_window_loc_cnt` is the largest number of
locations any search window can contain. It is computed by scanning the score using the `wnd_*`
arguments, so no guessed maximum-branch parameter is needed.

## Measurement model

The model is written as a generative story: given a particle's state, how likely
is it that we observe this pitch at this time?  `P = pitch_max - pitch_min + 1` and
`k` is the count of notes at the candidate location.

| Branch   | Class prior      | Time likelihood                                          | Pitch likelihood |
|----------|------------------|----------------------------------------------------------|------------------|
| Match    | `prior_match`    | Gaussian N(loc.sec - pred_score_sec; 0, `time_sigma_sec`) | 1/k              |
| Mismatch | `prior_mismatch` | same Gaussian, using the nearest location                | 1/(P - k)        |
| NoMatch  | `prior_nomatch`  | uniform over the particle's window: 1/window_dur_sec     | 1/P              |

- `prior_match + prior_mismatch + prior_nomatch` must equal 1.0. `create()` fails
  if they do not. The priors are never silently normalized.
- The 1/k term keeps a chord location from receiving k times the evidence
  of a single-note location just because it has more chances to contain the pitch.
- The nomatch window duration is the particle's actual window duration (after the
  location-count extension and the region clip) with a small floor to prevent a zero duration.
- When several window locations contain the pitch, each gets its own Gaussian
  weighted match branch. Together they form a mixture.

Edge cases:
- If the performed pitch is outside [`pitch_min`, `pitch_max`] the note is immediately
  reported as `kNoMatchTId` with a warning. The particles are neither updated nor resampled.
- Grace notes are treated as ordinary notes at their location in stage 1.
- A score note that has already been matched may be matched again. Tracking
  repeated matches is a later refinement.

## Final location reporting

Before resampling, the normalized branch weights are summed for each
(`loc_idx`, `tid`) key. All nomatch branches go into a single bucket. The key with the
largest total mass (MAP) is reported. If the nomatch bucket wins, the result is
`kNoMatchTId` with `loc_id = kInvalidId`. The winning key's mass is reported as
`result_t.confidence`.

## Resampling

Systematic resampling is performed over the whole branch pool after every note, to
reduce the pool back to `particle_cnt` particles. Resampling on every note is required
because branching always expands the population. After resampling all weights are reset to 1/N.

After resampling each particle's `time_fact` is jittered by a multiplicative
random walk and clamped:

    time_fact *= exp( N(0, time_fact_walk_sigma) )
    time_fact  = clamp( time_fact, time_fact_min, time_fact_max )

## Configuration parameters

The stage 1 parameters added to `args_t` are listed below.
The defaults are those given in the `sf3` stanza of `apps/cli/cfg/main.cfg`.

| Name                    | Type       | Default | Purpose |
|-------------------------|------------|---------|---------|
| `particle_cnt`          | `unsigned` | 200     | Number of particles N. Sizes the particle array, and with the window bound it sizes the branch pool. |
| `rand_seed`             | `unsigned` | 0       | Seed for the `std::mt19937`, re-applied at each `reset()`. |
| `prior_match`           | `double`   | 0.85    | Prior probability that a note is a match. |
| `prior_mismatch`        | `double`   | 0.05    | Prior probability that a note is a mismatch. |
| `prior_nomatch`         | `double`   | 0.10    | Prior probability that a note is a nomatch. |
| `time_sigma_sec`        | `double`   | 0.25    | Standard deviation, in score seconds, of the Gaussian time likelihood. |
| `time_fact_prior_sigma` | `double`   | 0.10    | Standard deviation of the initial `time_fact` draw around 1.0. |
| `time_fact_walk_sigma`  | `double`   | 0.02    | Standard deviation of the multiplicative `time_fact` random walk. |
| `time_fact_min`         | `double`   | 0.5     | Lower clamp for `time_fact`. |
| `time_fact_max`         | `double`   | 2.0     | Upper clamp for `time_fact`. |

## Debug reporting

`report_level` selects cumulative levels of output, all printed via `cwLogInfo()`.

- 0: Silent.
- 1: One line per `reset()`: the active location range, N, and the computed branch pool bound.
- 2: One line per note: `perf_uid`, performance time, pitch, the reported `loc_id`, status, and
  confidence, along with the population mean and standard deviation of `pred_score_sec` and `time_fact`.
  Mismatch lines include the expected score pitch(es) at the reported location.
  The test harness also prints the ground-truth disagreement listing at this level.
- 3: The aggregated MAP table for each note (the top (`loc_id`,`tid`) keys and their mass).
- 4: A dump of every particle after resampling.


# Testing framework

The test harness is `cw::score_follow_3::test()` in `src/cw/cwScoreFollow3Test.cpp`.
It is configured by the `sf3` stanza of `apps/cli/cfg/main.cfg`.

## Configuration

| Key              | Purpose |
|------------------|---------|
| `srate`, `smp_per_cycle` | Used to form `args_t.exec_period_sec = smp_per_cycle/srate`. |
| `score_fname`    | The score JSON file. |
| `out_dir`        | Directory where result files are written. It is created if it does not exist, but its parent directory must already exist. |
| `output_json_fl` | Write the result file as JSON rather than CSV. |
| `perfL`          | List of performances to test. |
| `follower`       | The `args_t` values passed to `create()`. |

Each `perfL` element contains:
- `beg_meas`, `end_meas`: The score region. These are converted to `beg_loc_id`
  (the first location in `beg_meas`) and `end_loc_id` (the last location in `end_meas`) and passed to `reset()`.
- `fn`: The performance MIDI CSV file.
- `gt_fn`: (optional) A ground-truth file for this performance.

## Test procedure

For each performance the harness calls `reset()`, then for each performed note-on
calls `exec()` at `exec_period_sec` intervals for the time elapsed since the previous
note, followed by `on_new_note()`. The performed note's `perf_uid` is the `UID` column of
the performance CSV file. This allows results, ground truth and the original recording
to be cross-referenced.

## Result file

One result file is written per performance to `out_dir`, using the performance file's
base name. The CSV form has one row per performed note:

    perf_uid,pitch,loc_id,meas,confidence,match_status_id

- `loc_id` is written as -1 when no location was reported.
- `meas` is derived from the result `loc_id` and is 0 when there is no location.
- `match_status_id` is written as a label: `<blank>`, `match`, `mismatch` or `nomatch`.

## Ground-truth file

The ground-truth file has the same format as the CSV result file. This allows a result
file to be used as the starting point for a ground-truth file which is then corrected
by hand. The `confidence` column is ignored.

- `loc_id` is -1 for `nomatch` rows.
- `<blank>` rows are not scored. This allows a file to be labeled partially.
- Notes without a ground-truth row are not scored.

Each row is validated when the file is loaded:
- The status label must be a known label.
- `perf_uid` must exist in the performance and must not be duplicated.
- `pitch` must equal the performed note's pitch.
- `match` and `mismatch` rows must have a valid `loc_id` which exists in the score.
- On `match` and `mismatch` rows `meas` must equal the measure of `loc_id`.
  Note that a result file used as a ground-truth starting point will have `meas` set to 0
  on rows where the follower reported no location. If `loc_id` is corrected by hand
  then `meas` must be corrected as well.
- A `match` pitch must be present at `loc_id`.
- A `mismatch` pitch must not be present at any note of `loc_id`.

Label and validation errors are collected so that every invalid row is reported before
the test fails. A row whose numeric fields cannot be parsed stops the load immediately.

## Scoring report

When a ground-truth file is given, the results are compared to it after the run and a summary is printed:

- Labeled and scored note counts, and the count of labeled notes not reached by the run.
- Agreement: status matches, and for `match` and `mismatch` rows the `loc_id` also matches.
- Status correct: status alone matches.
- Location correct: `loc_id` matches, counted over ground-truth `match` and `mismatch` rows only.
- A confusion table of ground-truth status (rows) against follower status (columns).
  The `<blank>` column counts notes the follower did not evaluate.

At `report_level >= 2` every disagreeing note is listed with the ground-truth and
follower location, measure, status and confidence side by side.


