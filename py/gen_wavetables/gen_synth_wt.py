import os
import json
import math
import types
import numpy as np
import wt_util as wtu

def synth_signal( args,
                  pitch,  # MIDI pitch
                  hz,     # fund. freq.
                  vel_idx # index into velocity map of this note
                 ):
    # Generate a single note at a given pitch and velocity.
    # Return both the signal and the location of sustain wavetables.

    loop_offset_smp = 1000         # constant sample count between the end of the decay env. and
    
    vel = args.velMapL[vel_idx]    # lookup the MIDI velocity of this note

    # setup the note information record
    sigD = dict( vel= vel,  # MIDI velocity
                 bsi= None, # sample index offset of this note in the final signal 
                 chL= [[] for _ in range(args.ch_cnt )] # wave-table loop points per channel
                )
    

    # convert the amplitude envelope into samples
    atk_dur_smp = int((args.atk_dur_ms * args.srate)/1000.0)
    dcy_dur_smp = int((args.dcy_dur_ms * args.srate)/1000.0)
    sus_dur_smp = int((args.sus_dur_ms * args.srate)/1000.0)
    sig_dur_smp = atk_dur_smp + dcy_dur_smp + sus_dur_smp

    # calculate the the velocity based scaling
    db = args.min_db + ((vel_idx / (len(args.velMapL)-1)) * (args.max_db - args.min_db))
    scale = 10**(db/10)

    # pre-allocate the output signal vectors
    sigM = np.zeros( (sig_dur_smp, args.ch_cnt) )

    print(f"{vel_idx:2} {pitch:3} {vel:3} {atk_dur_smp}  {dcy_dur_smp} {sus_dur_smp} {sig_dur_smp} {args.sus_lvl:5.3f} {db:7.2f} {scale:7.2f} {hz:7.2f}")

    # for each audio channgel
    for ch_idx in range(args.ch_cnt):

        # generate the signal with no amplitude envelope
        for i in range(sig_dur_smp):
            sigM[i,ch_idx] = math.sin( 2.0* math.pi * i * hz / args.srate )

        # apply a linear attack envelope
        for i in range(atk_dur_smp):
            sigM[i,ch_idx] *= i/atk_dur_smp

        # apply a linear decay envelope
        for i in range(dcy_dur_smp):
            sigM[atk_dur_smp + i,ch_idx] *= 1.0 - ((1.0-args.sus_lvl) * (i/dcy_dur_smp)) 

        # apply the sustain
        for i in range(sus_dur_smp):
            sigM[atk_dur_smp + dcy_dur_smp + i, ch_idx ] *= args.sus_lvl

        # the signal now has the correct shape and is full scale

        # set the begin and end of the one and only sustain loop
        smp_per_cycle = int(args.srate / hz)
        wtbi = atk_dur_smp + atk_dur_smp + 1000  # start of sustain loop
        wtei = wtbi + smp_per_cycle              # end of sustain loop is one cycle later.

        
        sigD['chL'][ch_idx].append( dict(wtbi=wtbi,wtei=wtei,rms=args.sus_lvl,est_hz=hz) )

    # apply the overall velocity based scale
    sigM *= scale
        
    return sigM,sigD

def gen_pitch( args, pitch ):
    # Generate a sequence of notes at the same pitch but with the velocity
    # determined by the values in args.velMapL[].
    # Write the audio into audio files in args.wav_dir.
    # Write a Audacity TSV label file marking the begin and end of each note into args.tsv_dir
    # 
    
    hz             = wtu.midi_pitch_to_hz(pitch)
    inter_note_smp = int( args.inter_note_ms * args.srate / 1000.0 )
    tsvL           = []
    
    pitchD = dict(midi_pitch=pitch,
                  srate=args.srate,
                  est_hz_mean=hz,
                  est_hz_err_cents=0.0,
                  est_hz_std_cents=0.0,
                  wt_interval_secs=args.inter_note_ms / 1000.0,
                  dominant_ch_idx=0,
                  audio_fname=None,
                  mark_tsv_fname=None,
                  velL=[])
    
    velSeqL = []
    durN    = 0

    # For each vel. in args.velMapL generate a note at 'pitch'
    for vel_idx in range(len(args.velMapL)):
        noteM,noteD = synth_signal( args, pitch, hz, vel_idx )
        
        # Track the total length of all notes in sample frames.
        durN += noteM.shape[0] + inter_note_smp

        # Store the note signal and info.
        velSeqL.append((noteM,noteD))


    # Allocate the signal output matrix
    sigM = np.zeros((durN,args.ch_cnt))

    bsi  = 0
    
    # For each note
    for noteM,noteD in velSeqL:
        
        esi = bsi+noteM.shape[0] # End sample frame index of this note in sigM[]
        sigM[bsi:esi,:] = noteM  # Copy the signal into the output matrix

        # Update the starting location of this note in the note info record and
        # update the sustain loop points based on the start location of the note
        noteD['bsi'] = bsi
        for ch_idx in range(args.ch_cnt):
            for wt_seg_idx in range(len(noteD['chL'][ch_idx])):
                noteD['chL'][ch_idx][wt_seg_idx]['wtbi'] += bsi
                noteD['chL'][ch_idx][wt_seg_idx]['wtei'] += bsi

        # Track the note start and end time in the output file
        tsvL.append((bsi/args.srate, esi/args.srate, str(noteD['vel'])))

        # Advance time
        bsi += noteM.shape[0] + inter_note_smp
            
        pitchD['velL'].append(noteD)

        
    # Write the audio signal to args.wav_dir
    fn = os.path.join(args.wave_dir,f"{pitch:03d}_samples.wav")
    wtu.write_audio_file(sigM,int(args.srate),fn)
    pitchD['audio_fname'] = fn

    # Write the Audacity label file to args.tsv_dir
    fn = os.path.join(args.tsv_dir,f"{pitch:03d}_markers.txt")
    wtu.write_mark_tsv_file(tsvL,fn)
    
    return pitchD
    

def generate( args ):

    pitchL = []

    # Create the base output dir
    os.makedirs(args.out_dir,exist_ok=True)

    # Create the audio file output dir
    args.wave_dir = os.path.join(args.out_dir,"wav")
    os.makedirs(args.wave_dir,exist_ok=True)

    # Create the Audacity label TSV file output dir
    args.tsv_dir = os.path.join(args.out_dir,"tsv")
    os.makedirs(args.tsv_dir,exist_ok=True)

    # For each pitch
    for pitch in range(args.min_pitch,args.max_pitch):
        # Generate the audio file for this pitch and obtain the wave table information
        pitchD = gen_pitch( args, pitch )
        pitchL.append( pitchD )

    # Write the wave-table control file
    fname = os.path.join(args.out_dir,args.out_json_fname)
    with open(fname,"w") as f:
        json.dump(dict(instr="piano",pitchL=pitchL),f,indent=2)
    

        

if __name__ == "__main__":

    args = dict(
        out_dir = "/home/kevin/temp/syn_wt",
        out_json_fname = "syn_wt_0.json",
        srate = 48000.0,
        ch_cnt = 2,
        min_pitch = 21,
        max_pitch = 108,
        inter_note_ms = 100,  # gap between notes in each pitch audio file
        max_db = -1.5,
        min_db = -20.0,
        atk_dur_ms = 20,
        dcy_dur_ms = 100,
        sus_dur_ms = 1000,  
        sus_lvl = 0.8,
        velMapL = [1,5,10,16,21,26,32,37,42,48,53,58,64,69,74,80,85,90,96,101,106,112,117,122,127])


    args = types.SimpleNamespace(**args)
    

    generate(args)
    
