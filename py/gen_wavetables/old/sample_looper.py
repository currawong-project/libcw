import math
import json
import array
import types
import matplotlib.pyplot as plt
import numpy as np

import wt_util

def plot_overlap( xV, bi, ei, wndN, smp_per_cycle, title ):

    fig, ax = plt.subplots(1,1)

    x0 = [ i for i in range(bi-wndN,bi+wndN) ]
    x1 = [ x-x0[0] for x in x0 ]
    ax.plot(x1,xV[x0])
    
    x0 = [ i for i in range(ei-wndN,ei+wndN) ]
    x1 = [ x-x0[0] for x in x0 ]
    ax.plot(x1,xV[x0])

    plt.title(title)
    
    plt.show()

def sign(x):
    return x<0

def find_zero_crossing( xV, si, inc ):
# find the next zero crossing before/after si
    
    while si > 0:
        
        if sign(xV[si-1])==False and sign(xV[si])==True:        
            break;
        si += inc

    return si

def meas_fit(xV,ei,bi,wndN):

    if bi-wndN < 0 or ei+wndN > len(xV):
        return None
    
    v0 = xV[ei-wndN:ei+wndN]/0x7fffffff
    v1 = xV[bi-wndN:bi+wndN]/0x7fffffff

    dv = (v1-v0) * (v1-v0)
    return np.mean(dv)

def find_loop_points_1( xV, bsi, esi, smp_per_cycle, wndN, est_N ):

    # find the first zero crossing after the end of the search range
    ei = find_zero_crossing(xV,esi,-1)

    min_d = None
    min_bi = None
    bi = bsi
    
    # make est_N guesses
    for i in range(0,est_N):

        # find the next zero crossing after bi
        bi = find_zero_crossing(xV,bi,1)

        # measure the quality of the fit with the end of the loop
        d = meas_fit(xV,ei,bi,wndN)

        #print(i,bi,d)

        # store the best loop begin point
        if min_bi is None or d < min_d:
            min_d = d
            min_bi = bi

        # advance 
        bi += int(wndN) #smp_per_cycle
        

    return min_bi, ei, min_d

def find_loop_points_2(xV,bsi,esi, smp_per_cycle, wndN, est_N ):

    def _track_min( min_i, min_d, i, d ):
        if min_i is None or d<min_d:
            return i,d
        return min_i,min_d
    
    spc_2   = int(smp_per_cycle/2)
    bzi,ezi = esi-spc_2, esi+spc_2
    max_i   = int(np.argmax(xV[bzi:ezi]) + bzi)
    ei      = find_zero_crossing(xV,max_i,1)

    bi = max_i - int(round(((esi-bsi)/smp_per_cycle) * smp_per_cycle))

    bi_p = bi
    bi_n = bi-1

    min_bi = None
    min_d  = None
    
    for i in range(est_N):

        # evaluate the fit of the next zero-crossing relative to bi_p
        bi_p = find_zero_crossing(xV,bi_p,1)
        if bi_p < ei:            
            d_p = meas_fit(xV,ei,bi_p,wndN)
            min_bi,min_d = _track_min(min_bi,min_d,bi_p,d_p)
            bi_p += 1 # advance bi_p forward

        # evaluate the fit of the previous zero-crozzing relative to bi_n
        bi_n = find_zero_crossing(xV,bi_n,-1)
        d_n = meas_fit(xV,ei,bi_n,wndN)
        min_bi,min_d = _track_min(min_bi,min_d,bi_n,d_n)
        bi_n -= 1  # advance bi_n backward                
        
    return min_bi, ei, min_d

def find_loop_points_3(xV,bsi,esi, smp_per_cycle, wndN, est_N ):

    spc_2   = int(smp_per_cycle/2)
    bzi,ezi = bsi-spc_2, bsi+spc_2
    max_i   = int(np.argmax(xV[bzi:ezi]) + bzi)
    bi      = find_zero_crossing(xV,max_i,1)
    ei      = bi + math.ceil(smp_per_cycle)

    #print(bi,ei,ei-bi,smp_per_cycle)

    d  = meas_fit(xV,ei,bi,wndN)

    return bi,ei,d

def find_loop_points_4(xV,bsi,esi, smp_per_cycle, wndN, est_N ):

    def _track_min( min_i, min_d, i, d ):
        if d is not None and (min_i is None or d<min_d):
            return i,d
        return min_i,min_d

    min_i = None
    min_d = None
    spc_2 = int(smp_per_cycle/2)
    
    for i in range(est_N):
        bzi,ezi = bsi-spc_2, bsi+spc_2
        max_i   = int(np.argmax(xV[bzi:ezi]) + bzi)
        bi      = find_zero_crossing(xV,max_i,1)
        ei      = bi + math.ceil(smp_per_cycle)

        #print(bi,ei,ei-bi,smp_per_cycle)

        d  = meas_fit(xV,ei,bi,wndN)

        min_i,min_d = _track_min(min_i,min_d,bi,d)

        bsi += math.ceil(smp_per_cycle)

    return min_i,min_i + math.ceil(smp_per_cycle),min_d

def find_loop_points(xV,bsi,esi, smp_per_cycle, wndN, est_N ):

    def _track_min( min_i, min_d, i, d ):
        if d is not None and (min_i is None or d<min_d):
            return i,d
        return min_i,min_d

    min_i = None
    min_d = None
    spc_2 = int(smp_per_cycle/2)

    bzi,ezi = bsi-spc_2, bsi+spc_2
    max_i   = int(np.argmax(xV[bzi:ezi]) + bzi)
    bi      = find_zero_crossing(xV,max_i,1)
    
    for i in range(est_N):

        ei      =  math.ceil(bi + (i+1)*smp_per_cycle)
        
        #print(bi,ei,ei-bi,smp_per_cycle)

        d  = meas_fit(xV,ei,bi,wndN)

        min_i,min_d = _track_min(min_i,min_d,ei,d)


    return bi,min_i,min_d


def find_best_zero_crossing(xV,bi,ei,wndN):

    bi0 = find_zero_crossing(xV,bi-1,-1)
    bi1 = find_zero_crossing(xV,bi,1)

    ei0 = find_zero_crossing(xV,ei-1,-1)
    ei1 = find_zero_crossing(xV,ei,1)

    beV = [ (ei0,bi0), (ei0,bi1), (ei1,bi0), (ei1,bi1) ]
    
    i_min = None
    d_min = None
    for i,(ei,bi) in enumerate(beV):
        d = meas_fit(xV,ei,bi,wndN)

        if i_min is None or d < d_min:
            i_min = i
            d_min = d

    ei,bi = beV[i_min]
    
    return bi,ei,d_min


def determine_track_order( smpM, bli, eli ):

    i_max = None
    rms_max = None
    rmsV = []
    assert( smpM.shape[1] == 2 )
    
    for i in range(smpM.shape[1]):
        rms = np.mean(np.pow(smpM[bli:eli,i],2.0))
        
        if i_max is None or rms > rms_max:
            i_max = i
            rms_max = rms
            
        rmsV.append(float(rms))

    return [ i_max, 0 if i_max==1 else 1 ]

def process_all_samples( markL, smpM, srate, args ):

    wtL = []

    #fund_hz = 13.75 * math.pow(2,(-9.0/12.0)) * math.pow(2.0,(args.midi_pitch / 12.0))
    fund_hz = midi_pitch_to_hz(args.midi_pitch)
    
    smp_per_cycle    = int(srate / fund_hz)
    end_offs_smp_idx = max(smp_per_cycle,int(args.end_offset_ms * srate / 1000))
    loop_dur_smp     = int(args.loop_dur_ms   * srate / 1000)
    wndN             = int(smp_per_cycle/6)

    print(f"Hz:{fund_hz} smp/cycle:{smp_per_cycle} loop_dur:{loop_dur_smp}  cycles/loop:{loop_dur_smp/smp_per_cycle} wndN:{wndN}")

    # for each sampled note
    for beg_sec,end_sec,vel_label in markL:
        
        beg_smp_idx = int(beg_sec * srate)
        end_smp_idx = int(end_sec * srate)

        r = {
            "instr":"piano",
            "pitch":args.midi_pitch,
            "vel": int(vel_label),
            "beg_smp_idx":beg_smp_idx,
            "end_smp_idx":None,
            "chL": []
        }

        # determine the loop search range from the end of the note sample
        eli = end_smp_idx - end_offs_smp_idx
        bli = eli - loop_dur_smp
        
        ch_map = determine_track_order( smpM, beg_smp_idx, end_smp_idx)
        
        #print(ch_map)
        
        esi = beg_smp_idx;
        for i in range(0,smpM.shape[1]):

            ch_idx = ch_map[i]

            xV    = smpM[:,ch_idx]

            if True:
                #if i == 0:
                #    s_per_c = srate / fund_hz
                #    bi,ei,cost = find_loop_points(xV,bli,eli,s_per_c,wndN,args.guess_cnt)
                
                s_per_c = srate / fund_hz     
                bi,ei,cost = find_loop_points_4(xV,bli,eli,s_per_c,wndN,args.guess_cnt)
            
            if False:
                bi,ei,cost = find_loop_points_2(xV,bli,eli,smp_per_cycle,wndN,args.guess_cnt)

            if False:
                if i == 0:
                    bi,ei,cost = find_loop_points(xV,bli,eli,smp_per_cycle,wndN,args.guess_cnt)
                else:
                    bi,ei,cost = find_best_zero_crossing(xV,bi,ei,wndN)

            if False:
                if i == 0:
                    bi,ei,cost = find_loop_points(xV,bli,eli,smp_per_cycle,wndN,args.guess_cnt)
                else:
                    pass
                    

            #print(i,bi,ei)
            eli = ei # attempt to make the eli the second channel close to the first

            loop_dur_sec = (ei-bi)/srate
            cyc_per_loop = int(round((ei-bi)/smp_per_cycle))
            plot_title = f"vel:{vel_label} cyc/loop:{cyc_per_loop} dur:{loop_dur_sec*1000:.1f} ms {ei-bi} smp ch:{ch_idx} cost:{0 if cost<= 0 else math.log(cost):.2f}"
            plot_overlap(xV,bi,ei,wndN,smp_per_cycle,plot_title)

            r["chL"].append({
                "ch_idx":ch_idx,
                "segL":[] })


            r["chL"][-1]["segL"].append({
                "cost":0,
                "cyc_per_loop":1,
                "bsi":beg_smp_idx,
                "esi":bi })

            r["chL"][-1]["segL"].append({
                "cost":cost,
                "cyc_per_loop":cyc_per_loop,
                "bsi":bi,
                "esi":ei })
            
            esi = max(esi,ei)

        r['end_smp_idx'] = esi

        r["chL"] = sorted(r["chL"],key=lambda x: x["ch_idx"])
        wtL.append(r)
        
    return wtL



def write_loop_label_file( fname, wtL, srate ):

    with open(fname,"w") as f:
        for r in wtL:
            for cr in r['chL']:
                for sr in cr['segL']:
                    beg_sec = sr['bsi'] / srate
                    end_sec = sr['esi'] / srate
                    if sr['cost']!=0:
                        cost  = math.log(sr['cost'])
                        label = f"ch:{cr['ch_idx']} {r['vel']} {cost:.2f}"
                        f.write(f"{beg_sec}\t{end_sec}\t{label}\n")

            
def write_wt_file( fname, audio_fname, wtL, srate ):

    r = {
        "audio_fname":audio_fname,
        #"srate":srate,
        "wt":wtL
    }

    with open(fname,"w") as f:
        json.dump(r,f);
        

def gen_loop_positions( audio_fname, marker_tsv_fname, pitch, argsD, loop_marker_fname, wt_fname ):

    args       = types.SimpleNamespace(**argsD)
    markL      = wt_util.parse_marker_file(marker_tsv_fname)
    smpM,srate = wt_util.parse_audio_file(audio_fname)
    chN        = smpM.shape[1]
    wtL        = process_all_samples(markL,smpM,srate,args)

    write_loop_label_file(loop_marker_fname, wtL, srate)

    write_wt_file(wt_fname,audio_fname, wtL,srate)
    
        
        
if __name__ == "__main__":

    audio_fname       = "/home/kevin/temp/wt/wav/60_samples.wav"
    marker_tsv_fname  = "/home/kevin/temp/wt/60_marker.txt"
    
    loop_marker_fname = "/home/kevin/temp/wt/60_loop_mark.txt"
    wt_fname          = "/home/kevin/temp/wt/bank/60_wt.json"
    midi_pitch        = 60
                                                          
    argsD = {
        'end_offset_ms':100,
        'loop_dur_ms':100,
        'midi_pitch':midi_pitch,
        'guess_cnt':40
        
    }
    
    gen_loop_positions( audio_fname, marker_tsv_fname, midi_pitch, argsD, loop_marker_fname, wt_fname )
