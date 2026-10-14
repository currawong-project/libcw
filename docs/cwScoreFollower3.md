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
then the `loc_id` is set to `kInvalidId`. As part of our development
we will likely report more information regarding the match but for now this is
the minimum required output.

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
3. 'Spurious': This note matches in neither pitch or time to a nearby score note.

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
2. `particle_t.time_factor`: The particles current estimated `time_factor` value.
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


