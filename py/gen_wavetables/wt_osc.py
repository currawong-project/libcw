import os
import math
import json
import wt_util
import calc_sample_atk_dur
import calc_wavetables
import numpy as np
import multiproc as mp
def table_read_2( tab, frac ):

    i0 = math.floor(frac + 1)
    i1 = i0 + 1
    f  = frac - int(frac)

    return tab[i0] + (tab[i1] - tab[i0]) * f

def hann_read( x, N ):

    while x > N:
        x -= N

    x = x - (N/2) 

    return (0.5 + 0.5 * math.cos(2*math.pi * x / N))  

def prepare_wt( aV, wtL, wt_idx ):

    wt_smp_cnt = wtL[0]['wtei'] - wtL[0]['wtbi']
    
    if wt_idx >= len(wtL):
        wt = np.zeros((wt_smp_cnt,))
    else:
        wt = np.copy(aV[ wtL[wt_idx]['wtbi']: wtL[wt_idx]['wtei'] ])

    wt     = [0] + wt.tolist() + [0]
    wt[0]  = wt[-2]
    wt[-1] = wt[0]

    return np.array(wt)

def get_wt( aV, wtL, wt_idx ):

    wt0 = prepare_wt(aV,wtL,wt_idx)
    wt1 = prepare_wt(aV,wtL,wt_idx+1)
    
    return wt0,wt1

def gen_osc_output( i_audio_fname, velL, midi_pitch, note_dur_sec, inter_note_sec, wt_interval_sec, o_audio_fname ):

    smp_per_dsp_frm = 64
    
    aM,srate  = wt_util.parse_audio_file(i_audio_fname)
    ch_cnt    = aM.shape[1]

    note_dur_smp    = int(round(note_dur_sec * srate))
    inter_note_smp  = int(round(inter_note_sec * srate))
    wt_interval_smp = int(round(wt_interval_sec * srate))
    
    yN = len(velL) * (note_dur_smp + inter_note_smp)
    yM = np.zeros((yN,ch_cnt))
    yi = 0

    for velD in velL:

        bsi          = velD['bsi']
        hz           = wt_util.midi_pitch_to_hz(midi_pitch)
        fsmp_per_cyc = srate/hz
        fsmp_per_wt  = fsmp_per_cyc * 2
        smp_per_wt   = int(math.floor(fsmp_per_wt))

        for ch_idx in range(ch_cnt):

            wtL = velD['chL'][ch_idx]

            if len(wtL) == 0:
                print(f"pitch:{midi_pitch} vel:{velD['vel']} ch:{ch_idx} has no wavetables.")
                continue

            # The attack abutts the wavetable at it's center point
            # so we need to offset the end of the attack half way
            # through the first wave table.
            abi  = int(wtL[0]['wtbi'] + smp_per_wt/2)
            
            atkN = min(abi - bsi,note_dur_smp)

            print(velD['vel'],yi+atkN,yN,atkN/srate)
            
            yM[yi:yi+atkN,ch_idx] = aM[bsi:bsi+atkN,ch_idx]

            wt_idx     = 0
            wt0,wt1    = get_wt( aM[:,ch_idx], wtL, wt_idx )
            wt         = wt0
            wt_int_phs = 0
            
            phs0 = 0
            phs1 = fsmp_per_wt/2
            
            for i  in range(note_dur_smp-atkN):

                s0 = table_read_2( wt, phs0 )
                s1 = table_read_2( wt, phs1 )

                e0 = hann_read(phs0,fsmp_per_wt)
                e1 = hann_read(phs1,fsmp_per_wt)

                yM[yi+atkN+i,ch_idx] = e0*s0 + e1*s1

                # advance the phases of the oscillators
                phs0 += 1
                if phs0 >= smp_per_wt:
                    phs0 -= smp_per_wt

                phs1 += 1
                if phs1 >= smp_per_wt:
                    phs1 -= smp_per_wt

                wt_int_phs += 1

                if wt_int_phs % smp_per_dsp_frm == 0:
                    wt_mix  = min(1.0, wt_int_phs / wt_interval_smp)
                    wt      = ((1.0-wt_mix) * wt0) + (wt_mix * wt1)

                if wt_int_phs >= wt_interval_smp:
                    wt_idx     += 1
                    wt0,wt1     = get_wt( aM[:,ch_idx], wtL, wt_idx )
                    wt          = wt0
                    wt_int_phs  = 0
                    wt_mix      = 0


        yi += note_dur_smp + inter_note_smp

    print(o_audio_fname)
    wt_util.write_audio_file( yM, srate, o_audio_fname )



def gen_from_wt_json( processN, wt_json_fname, out_dir, note_dur_sec, inter_note_sec, pitch_filtL=None ):

    def _multi_proc_func( procId, procArgsD, taskArgsD ):

        gen_osc_output(**taskArgsD)


    if not os.path.isdir(out_dir):
        os.mkdir(out_dir)

    with open(wt_json_fname) as f:
        pitchL = json.load(f)['pitchL']

    taskArgsL = []
    for pitchD in pitchL:
        if pitch_filtL is None or pitchD['midi_pitch'] in pitch_filtL:
            taskArgsL.append( {
                "i_audio_fname":pitchD['audio_fname'],
                "velL":pitchD['velL'],
                "midi_pitch":pitchD['midi_pitch'],
                "note_dur_sec":note_dur_sec,
                "inter_note_sec":inter_note_sec,
                "wt_interval_sec":pitchD['wt_interval_secs'],
                "o_audio_fname":os.path.join(out_dir,f"{pitchD['midi_pitch']:03}_osc.wav")
            })

    processN = min(processN,len(taskArgsL))
    mp.local_distribute_main( processN,_multi_proc_func,{},taskArgsL )


        

            
                

    
if __name__ == "__main__":

    if True:
        wt_json_fname = "/home/kevin/temp/temp_5.json"
        out_dir = "/home/kevin/temp/wt_osc_1"
        note_dur_sec = 10.0
        inter_note_sec = 1.0
        processN = 20
        pitch_filtL = None #[ 27 ]
        gen_from_wt_json(processN, wt_json_fname,out_dir,note_dur_sec,inter_note_sec, pitch_filtL)
    
    if False:
        midi_pitch = 60
        audio_fname = "/home/kevin/temp/wt5/wav/060_samples.wav"
        mark_tsv_fname = "/home/kevin/temp/wt5/60_marker.txt"
        rms_wnd_ms = 50
        rms_hop_ms = 10
        atkEndThreshDb = -43.0
        wt_interval_secs = 1.0
        note_dur_sec = 10.0
        inter_note_sec = 1.0

     
        gateL = calc_sample_atk_dur.generate_gate_db(audio_fname,mark_tsv_fname,rms_wnd_ms, rms_hop_ms, atkEndThreshDb )
        
        wtL = calc_wavetables.gen_wave_table_list( audio_fname, mark_tsv_fname, gateL, midi_pitch, wt_interval_secs )

        gen_osc_output(audio_fname,wtL,note_dur_sec,inter_note_sec, wt_interval_secs, "/home/kevin/temp/temp.wav")
