import os
import math
import json
import types
import wt_util
import calc_sample_atk_dur
import numpy as np
import matplotlib.pyplot as plt
import multiproc as mp

from scipy.interpolate import CubicSpline


def upsample( aV, N, interp_degree ):
# aV[] - signal vector
# N - upsample factor (must be an integer >= 2)
# interp_degree - "linear" , "cubic"

    N = int(N)
    
    assert( N>= 2)
    
    aN     = len(aV)
    z      = np.zeros((aN,N))
    z[:,0] = aV
    
    # z is a copy of aV with zeros in the positions to be interpolated
    z      = np.squeeze(np.reshape(z,(aN*N,1)))

    # x contains the indexes into z which contain values from aV
    x  = [ i*N for i in range(aN) ]

    # xi contains the indexes into z which have zeros
    xi = [ i for i in range(len(z)) if i not in x and i < x[-1] ]

    # calc values for the zeros in z
    if interp_degree == "linear":
        cs = CubicSpline(x,aV)
        z[xi] = cs(xi)
        
    elif interp_degree == "cubic":
        z[xi] = np.interp(xi,x,aV)
    else:
        assert(0)

    # The last N-1 values are not set because they would require extrapolation
    # (they have no value to their right).  Instead we set these values
    # as the mean of the preceding N values.
    k = (len(z)-N)+1    
    for i in range(N-1):
        z[k+i] = np.mean(z[ k+i-N:k+i])

    return z #z[0:-(N-1)]

    

def estimate_pitch_ac( aV, si, hzL, srate, argsD ):
    # aV[] - audio vector containing a wavetable that starts at aV[si]
    # hzL[] - a list of candidate pitches
    # srate   - sample rate of aV[]
    # args[cycle_cnt] - count of cycles to autocorrelate on either side of the reference pitch at aV[si:]
    #             (1=correlate with the cycle at aV[ si-fsmp_per+cyc:] and the cycle at aV[si+fsmp_per_cyc],
    #             (2=correlate with cycles at aV[ si-2*fsmp_per+cyc:],aV[ si-fsmp_per+cyc:],aV[ si+fsmp_per+cyc:],aV[ si-2*fsmp_per+cyc:])
    # args[up_fact] - Set to and integer greater than 1 to upsample the signal prior to estimating the pitch
    # args[up_interp_degree] - Upsampling interpolator "linear" or "cubic"
    
    def _auto_corr( aV, si, fsmp_per_cyc, cycle_offset_idx, interp_degree ):

        smp_per_cyc = int(math.floor(fsmp_per_cyc))

        xi    = [si + (cycle_offset_idx * fsmp_per_cyc) + i for i in range(smp_per_cyc)]
        x_min = int(math.floor(xi[0]))
        x_max = int(math.ceil(xi[-1]))
        x     = [ i for i in range(x_min,x_max) ]
        y     = aV[x]

        if interp_degree == "cubic":
            cs = CubicSpline(x,y)
            yi = cs(xi)
        elif interp_degree == "linear":
            yi = np.interp(xi,x,y)
        else:
            assert(0)
        
        # calc the sum of squared differences between the reference cycle and the 'offset' cycle
        ac    = np.sum(np.pow(yi - aV[si:si+smp_per_cyc],2.0))

        return ac


    def auto_corr( aV, si, fsmp_per_cyc, cycle_cnt, interp_degree ):

        ac = 0
        for i in range(1,cycle_cnt+1):
            ac  = _auto_corr(aV,si,fsmp_per_cyc,  i, interp_degree)
            ac += _auto_corr(aV,si,fsmp_per_cyc, -i, interp_degree)

        # return the average sum of squared diff's per cycle
        return ac/(cycle_cnt*2)


    def ac_upsample( aV, si, fsmp_per_cyc, cycle_cnt, up_fact, up_interp_degree ):

        pad = 0 # count of leading/trailing pad positions to allow for interpolation
        
        if up_interp_degree == "cubic":
            pad = 2
        elif up_interp_degre == "linear":
            pad = 1
        else:
            assert(0)

        # calc the beg/end of the signal segment to upsample
        bi = si - math.ceil(fsmp_per_cyc * cycle_cnt) - pad
        ei = si + math.ceil(fsmp_per_cyc * (cycle_cnt + 1)) + pad

        up_aV = upsample(aV[bi:ei],up_fact,up_interp_degree)

        # calc. index of the center signal value
        u_si = (si-bi)*up_fact

        # the center value should not change after upsampling
        assert aV[si] == up_aV[u_si]

        return up_aV,u_si

    
    args = types.SimpleNamespace(**argsD)
    
    # if upsampling was requested
    if args.up_fact > 1:
        hz_min           = min(hzL)     # Select the freq candidate with the longest period,
        max_fsmp_per_cyc = srate/hz_min # because we want to upsample just enough of the signal to test for all possible candidates,
        aV,si            = ac_upsample( aV, si, max_fsmp_per_cyc, args.cycle_cnt, args.up_fact, args.up_interp_degree )
        srate            = srate * args.up_fact
        

    # calc. the auto-correlation for every possible candidate frequency
    acL = []
    for hz in hzL:
        fsmp_per_cyc = srate / hz
        acL.append( auto_corr(aV,si,fsmp_per_cyc,args.cycle_cnt,args.interp_degree) )

    
        
    if False:
        _,ax = plt.subplots(1,1)
        ax.plot(hzL,acL)
        plt.show()

    # winning candidate is the one with the lowest AC score
    cand_hz_idx = np.argmin(acL)
        
    return  hzL[cand_hz_idx]
    
# Note that we want a higher rate of pitch tracking than wave table generation - thus
# we downsample the pitch tracking interval by some integer factor to arrive at the
# rate at the wave table generation period.
def gen_wave_table_list( audio_fname,
                         mark_tsv_fname, gateL,
                         midi_pitch,
                         pitch_track_interval_secs,
                         wt_interval_down_sample_fact,
                         min_wt_db,
                         dom_ch_idx,
                         est_hz_argD,
                         ac_argD ):

    est_hz_args = types.SimpleNamespace(**est_hz_argD)
    
    aM,srate        = wt_util.parse_audio_file(audio_fname)
    markL           = wt_util.parse_marker_file(mark_tsv_fname)
    ch_cnt          = aM.shape[1]
    frm_cnt         = aM.shape[0]
    pt_interval_smp = int(round(pitch_track_interval_secs*srate))
    wt_interval_fact= int(wt_interval_down_sample_fact)
    hz              = wt_util.midi_pitch_to_hz(midi_pitch)
    fsmp_per_cyc    = srate/hz
    fsmp_per_wt     = fsmp_per_cyc * 2
    smp_per_wt      = int(math.floor(fsmp_per_wt))

    # calc. the range of possible pitch estimates
    hz_min = wt_util.midi_pitch_to_hz(midi_pitch-1)
    hz_ctr = wt_util.midi_pitch_to_hz(midi_pitch)
    hz_max = wt_util.midi_pitch_to_hz(midi_pitch+1)
    cents_per_semi = 100
    
    # hzL is a list of candidate pitches with a range of +/- 1 semitone and a resolution of 1 cent
    hzCandL = [ hz_min + i*(hz_ctr-hz_min)/100.0 for i in range(cents_per_semi) ] + [ hz_ctr + i*(hz_max-hz_ctr)/100.0 for i in range(cents_per_semi) ]

    assert( len(markL) == len(gateL) )

    # setup the return data structure
    pitchD = { "midi_pitch":midi_pitch,
               "srate":srate,
               "est_hz_mean":None,
               "est_hz_err_cents":None,
               "est_hz_std_cents":None,
               "wt_interval_secs":pitch_track_interval_secs * wt_interval_fact,
               "dominant_ch_idx":int(dom_ch_idx),
               "audio_fname":audio_fname,
               "mark_tsv_fname":mark_tsv_fname,
               "velL":[]
              }
    
    hzL = []
    for i,(beg_sec,end_sec,vel_label) in enumerate(markL):
        bsi = int(round(beg_sec*srate))
        esi = int(round(end_sec*srate))
        vel = int(vel_label)
        eai = gateL[i][1]  # end of attack

        velD = { "vel":vel, "bsi":bsi, "chL":[ [] for _ in range(ch_cnt)] }

        for ch_idx in range(ch_cnt):            

            i = 0
            while True:

                wt_smp_idx = eai + i*pt_interval_smp
                
                # select the first zero crossing after the end of the attack
                # as the start of the first sustain wavetable
                wtbi = wt_util.find_zero_crossing(aM[:,ch_idx],wt_smp_idx,1)

                #if len(velD['chL'][ch_idx]) == 0:
                #    print(midi_pitch,vel,(wtbi-bsi)/srate)

                if wtbi == None:
                    break;
                
                wtei = wtbi + smp_per_wt

                if wtei > esi:
                    break

                # estimate the pitch near wave tables which are: on the 'dominant' channel,
                # above a certain velocity and not too far into the decay 
                if ch_idx==dom_ch_idx and est_hz_args.min_wt_idx <= i and i <= est_hz_args.max_wt_idx and vel >= est_hz_args.min_vel:
                    est_hz = estimate_pitch_ac( aM[:,dom_ch_idx],wtbi,hzCandL,srate,ac_argD)
                    hzL.append( est_hz )
                    #print(vel, i, est_hz)

                if i % wt_interval_fact == 0:
                    # measure the RMS of the wavetable
                    wt_rms = float(np.pow(np.mean(np.pow(aM[wtbi:wtei,ch_idx],2.0)),0.5))

                    # filter out quiet wavetable but guarantee that there are always at least two wt's.
                    if 20*math.log10(wt_rms) > min_wt_db or len(velD['chL'][ch_idx]) < 2:

                        # store the location and RMS of the wavetable 
                        velD['chL'][ch_idx].append({"wtbi":int(wtbi),"wtei":int(wtei),"rms":float(wt_rms), "est_hz":0})

                i+=1


        pitchD['velL'].append(velD)

    # update est_hz in each of the wavetable records
    est_hz       = np.mean(hzL)
    est_hz_delta = np.array(hzCandL) - est_hz
    est_hz_idx   = np.argmin(np.abs(est_hz_delta))
    est_hz_std   = np.std(hzL)
    
    if est_hz_delta[est_hz_idx] > 0:
        est_hz_std_cents = est_hz_std / ((hz_ctr-hz_min)/100.0)
    else:
        est_hz_std_cents = est_hz_std / ((hz_max-hz_ctr)/100.0)
    
    est_hz_err_cents = est_hz_idx - cents_per_semi
            
    print(f"{midi_pitch} est pitch:{est_hz}(hz) err:{est_hz_err_cents}(cents)" )

    pitchD["est_hz_mean"]      = float(est_hz)
    pitchD["est_hz_err_cents"] = float(est_hz_err_cents)
    pitchD["est_hz_std_cents"] = float(est_hz_std_cents)
    
    return pitchD

def _gen_wave_table_bank( src_dir, midi_pitch, argD ):

    args = types.SimpleNamespace(**argD)
    
    audio_fname    = os.path.join(src_dir,f"wav/{midi_pitch:03}_samples.wav")
    mark_tsv_fname = os.path.join(src_dir,f"{midi_pitch:03}_marker.txt")

    if True:
        gateL,ch_avgRmsL = calc_sample_atk_dur.generate_gate_db(audio_fname,
                                                                mark_tsv_fname,
                                                                args.rms_wnd_ms,
                                                                args.rms_hop_ms,
                                                                args.atk_min_dur_ms,
                                                                args.atk_end_thresh_db )

    if False:
        gateL,ch_avgRmsL = calc_sample_atk_dur.generate_gate_pct(audio_fname,
                                                                 mark_tsv_fname,
                                                                 args.rms_wnd_ms,
                                                                 args.rms_hop_ms,
                                                                 args.atk_min_dur_ms,
                                                                 0.1 )
    
    dom_ch_idx = np.argmax(ch_avgRmsL)

    pitchD = gen_wave_table_list( audio_fname,
                                  mark_tsv_fname,
                                  gateL,
                                  midi_pitch,
                                  args.pitch_track_interval_secs,
                                  args.wt_interval_down_sample_fact,
                                  args.min_wt_db,
                                  dom_ch_idx,
                                  args.est_hz,
                                  args.ac )

    return pitchD



        
        
def gen_wave_table_bank_mp( processN, src_dir, midi_pitchL, out_fname, argD ):

    def _multi_proc_func( procId, procArgsD, taskArgsD ):

        return _gen_wave_table_bank( procArgsD['src_dir'],
                                     taskArgsD['midi_pitch'],
                                     procArgsD['argD'] )
    
    procArgsD = {
        "src_dir":src_dir,
        "argD": argD
    }

    taskArgsL = [ { 'midi_pitch':midi_pitch } for midi_pitch in midi_pitchL ]

    processN = min(processN,len(taskArgsL))
    
    if processN > 0:
        pitchL = mp.local_distribute_main( processN,_multi_proc_func,procArgsD,taskArgsL )
    else:
        pitchL = [ _gen_wave_table_bank( src_dir, r['midi_pitch'], argD ) for r in taskArgsL ]
                

    pitchL = sorted(pitchL,key=lambda x:x['midi_pitch'])
        
    with open(out_fname,"w") as f:
        json.dump({"pitchL":pitchL, "instr":"piano", "argD":argD},f)


        
def plot_rms( wtb_json_fname ):

    with open(wtb_json_fname) as f:
        pitchL = json.load(f)['pitchL']

    pitchL = sorted(pitchL,key=lambda x:x['midi_pitch'])

    rmsnL = []
    for pitchD in pitchL:
        _,ax = plt.subplots(1,1)
        for wtVelD in pitchD['wtL']:
            for velChL in wtVelD['wtL']:
                rmsL = [ 20*math.log10(wt['rms']) for wt in velChL ]
                ax.plot(rmsL)
                rmsnL.append(len(rmsL))

        plt.title(f"{pitchD['midi_pitch']}")
        plt.show()

def plot_atk_dur( wtb_json_fname ):
    
    with open(wtb_json_fname) as f:
        pitchL = json.load(f)['pitchL']

    pitchL = sorted(pitchL,key=lambda x:x['midi_pitch'])

    rmsnL = []
    for pitchD in pitchL:
        _,ax = plt.subplots(1,1)

        secL = [ (v['chL'][0][0]['wtbi']-v['bsi'])/pitchD['srate']  for v in pitchD['velL'] ]
        velL = [ x['vel'] for x in pitchD['velL'] ]
        ax.plot(velL,secL,marker=".")

        plt.title(f"{pitchD['midi_pitch']}")
        plt.show()


def plot_hz( wtb_json_fname ):

    with open(wtb_json_fname) as f:
        pitchL = json.load(f)['pitchL']

    pitchL = sorted(pitchL,key=lambda x:x['midi_pitch'])

    _,ax = plt.subplots(3,1)

    midiL  = [ pitchD['midi_pitch'] for pitchD in pitchL ]
    hzL    = [ pitchD["est_hz_mean"] for pitchD in pitchL ]
    hzStdL = [ pitchD["est_hz_std_cents"] for pitchD in pitchL ]
    hzErrL = [ pitchD["est_hz_err_cents"] for pitchD in pitchL ]

    ax[0].plot(midiL,hzL)
    ax[1].plot(hzL,hzStdL)
    ax[2].hlines([0,10,20],midiL[0],midiL[-1],color="red")
    ax[2].plot(midiL,hzErrL)
    

    plt.show()    
    


if __name__ == "__main__":

    midi_pitchL = [ pitch for pitch in range(21,109) ]
    midi_pitchL = [ 33 ]
    out_fname = "/home/kevin/temp/temp_5.json"
    src_dir= "/home/kevin/media/audio/wt6"
    processN = 0
    
    argD = {
        'rms_wnd_ms':50,
        'rms_hop_ms':10,
        'atk_min_dur_ms':1000,
        'atk_end_thresh_db':-43.0,
        'min_wt_db':-80.0,
        'pitch_track_interval_secs':0.25,
        'wt_interval_down_sample_fact':8.0, # wt_interval_secs = pitch_track_interval_secs * wt_interval_down_sample_fact
        'est_hz': {
            'min_vel':50,
            'min_wt_idx':2,
            'max_wt_idx':4
        },
        'ac': {
            'cycle_cnt':8,        # count of cycles to use for auto-corr. pitch detection
            'interp_degree':"cubic",
            'up_fact':2,
            'up_interp_degree':"cubic"
        }
    }

    gen_wave_table_bank_mp(processN, src_dir, midi_pitchL, out_fname, argD )

    #plot_rms(out_fname)
    #plot_hz(out_fname)
    plot_atk_dur(out_fname)
    
