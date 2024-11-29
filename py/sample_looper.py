import math
import json
import wave as w
import array
import types
import matplotlib.pyplot as plt
import numpy as np

def parse_marker_file( marker_fname ):

    markL = []
    with open(marker_fname) as f:
        for line in f:
            tokL = line.split("\t");

            assert( len(tokL) == 3 )
            
            markL.append( ( float(tokL[0]), float(tokL[1]), tokL[2] ) )

    return markL

def parse_audio_file( audio_fname ):
    
    with w.open(audio_fname,"rb") as f:
        print(f"ch:{f.getnchannels()} bits:{f.getsampwidth()*8} srate:{f.getframerate()} frms:{f.getnframes()}")

        srate      = f.getframerate()
        frmN       = f.getnframes()
        data_bytes = f.readframes(frmN)
        smpM       = np.array(array.array('i',data_bytes))

        
        smpM = np.reshape(smpM,(frmN,2))

        if 0:
            fig, ax = plt.subplots(1,1)

        
        
        
            ax.plot(smpM[0:48000*10,1])
            
            plt.show()

    return smpM,srate

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

    while si > 0:
        
        if sign(xV[si-1]) != sign(xV[si]):
            break;
        si += inc

    return si

def meas_fit(xV,ei,bi,wndN):

    v0 = xV[ei-wndN:ei+wndN]/0x7fffffff
    v1 = xV[bi-wndN:bi+wndN]/0x7fffffff

    dv = (v1-v0) * (v1-v0)
    return np.mean(dv)

def find_loop_points( xV, bsi, esi, smp_per_cycle, wndN, est_N ):

    ei = find_zero_crossing(xV,esi,-1)

    min_d = None
    min_bi = None
    bi = bsi
    for i in range(0,est_N):

        bi = find_zero_crossing(xV,bi,1)

        d = meas_fit(xV,ei,bi,wndN)

        #print(i,bi,d)
        
        if min_bi is None or d < min_d:
            min_d = d
            min_bi = bi

        bi += int(wndN/2) #smp_per_cycle
        

    return min_bi, ei, min_d

def process_all_samples_0( markL, smpM, srate, args ):

    wtL = []

    fund_hz = 13.75 * math.pow(2,(-9.0/12.0)) * math.pow(2.0,(args.midi_pitch / 12.0))

    end_offs_smp_idx = int(args.end_offset_ms * srate / 1000)
    loop_dur_smp     = int(args.loop_dur_ms   * srate / 1000)
    smp_per_cycle    = int(srate / fund_hz)

    print(f"Hz:{fund_hz} smp/cycle:{smp_per_cycle}")
    
    for beg_sec,end_sec,vel_label in markL:
        for ch_idx in range(0,smpM.shape[1]):
            beg_smp_idx = int(beg_sec * srate)
            end_smp_idx = int(end_sec * srate)

            eli = end_smp_idx - end_offs_smp_idx
            bli = eli - loop_dur_smp

            #print(beg_smp_idx,bli,eli,end_smp_idx)

            xV    = smpM[:,ch_idx]
            wndN = int(smp_per_cycle/3)
            bi,ei,cost = find_loop_points(xV,bli,eli,smp_per_cycle,wndN,args.guess_cnt)

            plot_title = f"vel:{vel_label} ch:{ch_idx} cost:{math.log(cost):.2f}"
            #plot_overlap(xV,bi,ei,wndN,smp_per_cycle,plot_title)

            wtL.append( {
                "pitch":args.midi_pitch,
                "vel": int(vel_label),
                "cost":cost,
                "ch_idx":ch_idx,
                "beg_smp_idx":beg_smp_idx,
                "end_smp_idx":end_smp_idx,
                "beg_loop_idx":bi,
                "end_loop_idx":ei })

    return wtL


def process_all_samples( markL, smpM, srate, args ):

    wtL = []

    fund_hz = 13.75 * math.pow(2,(-9.0/12.0)) * math.pow(2.0,(args.midi_pitch / 12.0))

    end_offs_smp_idx = int(args.end_offset_ms * srate / 1000)
    loop_dur_smp     = int(args.loop_dur_ms   * srate / 1000)
    smp_per_cycle    = int(srate / fund_hz)

    print(f"Hz:{fund_hz} smp/cycle:{smp_per_cycle}")
    
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

        eli = end_smp_idx - end_offs_smp_idx
        bli = eli - loop_dur_smp

        esi = beg_smp_idx;
        for ch_idx in range(0,smpM.shape[1]):

            xV    = smpM[:,ch_idx]
            wndN = int(smp_per_cycle/3)
            bi,ei,cost = find_loop_points(xV,bli,eli,smp_per_cycle,wndN,args.guess_cnt)

            plot_title = f"vel:{vel_label} ch:{ch_idx} cost:{math.log(cost):.2f}"
            #plot_overlap(xV,bi,ei,wndN,smp_per_cycle,plot_title)

            r["chL"].append({
                "ch_idx":ch_idx,
                "segL":[] })


            r["chL"][ch_idx]["segL"].append({
                "cost":0,
                "bsi":beg_smp_idx,
                "esi":bi })

            r["chL"][ch_idx]["segL"].append({
                "cost":cost,
                "bsi":bi,
                "esi":ei })
            
            esi = max(esi,ei)

        r['end_smp_idx'] = esi
        wtL.append(r)
        
    return wtL


def write_loop_label_file_0( fname, wtL, srate ):

    with open(fname,"w") as f:
        for r in wtL:
            beg_sec = r['beg_smp_idx'] / srate
            end_sec = r['end_smp_idx'] / srate            
            # f.write(f"{beg_sec}\t{end_sec}\t{r['vel_label']}\n")
            
            beg_sec = r['beg_loop_idx'] / srate
            end_sec = r['end_loop_idx'] / srate
            cost    = math.log(r['cost'])
            label = f"ch:{r['ch_idx']} {cost:.2f}"
            f.write(f"{beg_sec}\t{end_sec}\t{label}\n")

def write_loop_label_file( fname, wtL, srate ):

    with open(fname,"w") as f:
        for r in wtL:
            for cr in r['chL']:
                for sr in cr['segL']:
                    beg_sec = sr['bsi'] / srate
                    end_sec = sr['esi'] / srate
                    if sr['cost']!=0:
                        cost  = math.log(sr['cost'])
                        label = f"ch:{cr['ch_idx']} {cost:.2f}"
                        f.write(f"{r['vel']} {beg_sec}\t{end_sec}\t{label}\n")

            
def write_wt_file( fname, audio_fname, wtL, srate ):

    r = {
        "audio_fname":audio_fname,
        "srate":srate,
        "wt":wtL
    }
    with open(fname,"w") as f:
        json.dump(r,f);
        

def gen_loop_positions( audio_fname, marker_fname, pitch, argsD, loop_marker_fname, wt_fname ):

    args = types.SimpleNamespace(**argsD)
    markL = parse_marker_file(marker_fname)
    smpM,srate = parse_audio_file(audio_fname)
    chN = smpM.shape[1]
    wtL = process_all_samples(markL,smpM,srate,args)

    write_loop_label_file(loop_marker_fname, wtL, srate)

    write_wt_file(wt_fname,audio_fname, wtL,srate)
    
        
        
if __name__ == "__main__":

    audio_fname       = "/home/kevin/temp/wt/wav/60_samples.wav"
    marker_fname      = "/home/kevin/temp/wt/60_marker.txt"
    
    loop_marker_fname = "/home/kevin/temp/wt/60_loop_mark.txt"
    wt_fname          = "/home/kevin/temp/wt/bank/60_wt.json"
    midi_pitch        = 60
                                                          
    argsD = {
        'end_offset_ms':100,
        'loop_dur_ms':100,
        'midi_pitch':midi_pitch,
        'guess_cnt':40
        
    }
    
    gen_loop_positions( audio_fname, marker_fname, midi_pitch, argsD, loop_marker_fname, wt_fname )
