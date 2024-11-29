These scripts are used to generate notes from a MIDI sampler for a defined set of pitch and velocities.
The notes and begin/end time markers are recorded where each recorded audio file contains all the velocities for a single pitch.
The resulting samples are then used to calculate wavetables for use by the libcw wavetable oscillator.
(see: `dsp::multi_ch_wt_seq_osc` and `flow::piano_voice`).

`wt_util.py`
----------
Utilities used by all other modules

`gen_midi_csv.py`
---------------
Generate a MIDI file in CSV format to trigger the sampler with a sequence of velocities for a given pitch.

`sample_ivory.py`
---------------
Use the MIDI file from 'gen_midi_csv.py' to trigger the sampler and record the resulting audio and onset/offset TSV marker file.

`calc_sample_atk_dur.py`
----------------------
Calculate a list [(vel,bsi,esi)] which indicates the attack wavetable.

`calc_wavetables.py`
------------------
Create a JSON file of wave tables for all pitches and velocities.

`wt_osc.py`
---------
Generate a set of notes using the wavetables found by `calc_wavetables.py`.
This program implements a wavetable oscillator which can interpret the wavetables
created by `calc_wavetables.py`


Obsolete
---------------------
`wt_study.py`
`low_hz_loops.py`
`debug_plot.py`
`gen_wave_tables.py`
`sample_looper.py`
