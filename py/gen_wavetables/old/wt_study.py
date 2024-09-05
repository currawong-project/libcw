import math
import json
import array
import types
import matplotlib.pyplot as plt
import numpy as np
import wt_util


def sign(x):
    return x<0
    
def find_zero_crossing( xV, si, inc ):
# find the next zero crossing before/after si
    
    while si > 0:
        
        if sign(xV[si-1])==False and sign(xV[si])==True:        
            break;
        si += inc

    return si

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


def sine_0():

    srate = 48000.0
    hz = wt_util.midi_pitch_to_hz(6)
    
    fsmp_per_cyc = srate / hz                     # fractional samples per cycle
    fsmp_per_wt  = 2* fsmp_per_cyc                # fractional samples per wavetable
    smp_per_wt   = int(math.floor(fsmp_per_wt))   # integer samples per wavetable

    # Note that when wrapping from the last sample to the first there is less
    # than one sample period and so the wavetable phase after the wrap will be fractional

    # fill two wave tables with two identical cycles of a sine signal
    wt0 = [0] + [ math.sin(2*math.pi*hz*i/srate) for i in range(smp_per_wt) ] + [0]
    wt1 = [0] + [ math.sin(2*math.pi*hz*i/srate) for i in range(smp_per_wt) ] + [0]

    xN = smp_per_wt * 4
    
    phs0 = 0
    phs1 = fsmp_per_wt/2
    

    y0 = []
    y1 = []
    y2 = []
    h0 = []
    h1 = []
    for i in range(xN):

        # read the wave tables
        s0 = table_read_2( wt0, phs0 )
        s1 = table_read_2( wt1, phs1 )
        y0.append( s0 )
        y1.append( s1 )

        # calc the envelopes
        e0 = hann_read(phs0,fsmp_per_wt)
        e1 = hann_read(phs1,fsmp_per_wt)
        h0.append( e0 )
        h1.append( e1 )

        # sum the two signals
        y2.append( e0*s0 + e1*s1 )

        # advance the phases of the oscillators
        phs0 += 1
        if phs0 >= smp_per_wt:
            phs0 -= smp_per_wt
            
        phs1 += 1
        if phs1 >= smp_per_wt:
            phs1 -= smp_per_wt

        
        

    fix,ax = plt.subplots(4,1)
    ax[0].plot(y0)
    ax[1].plot(y1)
    ax[2].plot(h0)
    ax[2].plot(h1)
    ax[3].plot(y2)
    plt.show()



def piano_0():
    
    i_audio_fname       = "/home/kevin/temp/wt1/wav/060_samples.wav"
    o_audio_fname     = "/home/kevin/temp/temp.wav"
    marker_tsv_fname  = "/home/kevin/temp/wt1/60_marker.txt"
    midi_pitch = 60
    offs_ms = 50
    note_dur_secs = 3
    inter_note_secs = 0.5
    
    markL    = wt_util.parse_marker_file(marker_tsv_fname)
    aM,srate = wt_util.parse_audio_file(i_audio_fname)

    hz           = wt_util.midi_pitch_to_hz(midi_pitch)
    fsmp_per_cyc = srate/hz
    fsmp_per_wt  = fsmp_per_cyc * 2
    smp_per_wt   = int(math.floor(fsmp_per_wt))
    offs_smp     = int(math.floor(offs_ms * srate / 1000))
    ch_cnt       = aM.shape[1]
    note_dur_smp = int(round(note_dur_secs*srate))
    inter_note_smp = int(round(inter_note_secs*srate))

    yN = len(markL) * (note_dur_smp + inter_note_smp)
    yM = np.zeros((yN,ch_cnt))
    yi = 0
    
    for beg_sec,end_sec,vel_label in markL:
        bsi = int(round(beg_sec * srate))
        esi = int(round(end_sec * srate))
        
        for ch_idx in range(ch_cnt):
            
            wtbi = find_zero_crossing(aM[:,ch_idx],esi-offs_smp,-1)
            wtei = wtbi + smp_per_wt
            wt   = [0] + aM[wtbi:wtei,ch_idx].tolist() + [0]
            wt[0]  = aM[wtei-1,ch_idx]
            wt[-1] = wt[1]

            atkN = wtbi - bsi
            yM[yi:yi+atkN,ch_idx] = aM[bsi:wtbi,ch_idx]

            phs0 = 0
            phs1 =fsmp_per_wt/2
            
            for i in range(note_dur_smp-atkN):

                s0 = table_read_2( wt, phs0 )
                s1 = table_read_2( wt, phs1 )
            
                e0 = hann_read(phs0,fsmp_per_wt)
                e1 = hann_read(phs1,fsmp_per_wt)

                # advance the phases of the oscillators
                phs0 += 1
                if phs0 >= smp_per_wt:
                    phs0 -= smp_per_wt

                phs1 += 1
                if phs1 >= smp_per_wt:
                    phs1 -= smp_per_wt
                

                yM[yi+atkN+i,ch_idx] = e0*s0 + e1*s1
                
        yi += note_dur_smp + inter_note_smp
        
            

    wt_util.write_audio_file( yM, srate, o_audio_fname )

def select_wave_table( aV, si, smp_per_wt ):

    wtbi = find_zero_crossing(aV,si,-1)
    wtei = wtbi + smp_per_wt
    wt   = [0] + aV[wtbi:wtei].tolist() + [0]
    wt[0]  = aV[wtei-1]
    wt[-1] = wt[1]

    return wt,wtbi
    
    
def piano_1():

    o_audio_fname    = "/home/kevin/temp/temp.wav"
    o_mark_tsv_fname = "/home/kevin/temp/temp_mark.txt"
    
    if False:
        i_audio_fname     = "/home/kevin/temp/wt1/wav/060_samples.wav"
        marker_tsv_fname  = "/home/kevin/temp/wt1/60_marker.txt"
        midi_pitch = 60
        offs_0_ms = 100
        offs_1_ms = 50
        g_coeff = 0.9985


    if True:
        i_audio_fname       = "/home/kevin/temp/wt3/wav/21_samples.wav"
        marker_tsv_fname  = "/home/kevin/temp/wt3/21_marker.txt"
        midi_pitch = 21
        offs_0_ms = 100
        offs_1_ms = 80
        g_coeff = 0.9992
        
    note_dur_secs = 6
    inter_note_secs = 0.5
        
    markL    = wt_util.parse_marker_file(marker_tsv_fname)
    aM,srate = wt_util.parse_audio_file(i_audio_fname)

    hz           = wt_util.midi_pitch_to_hz(midi_pitch)
    fsmp_per_cyc = srate/hz
    fsmp_per_wt  = fsmp_per_cyc * 2
    smp_per_wt   = int(math.floor(fsmp_per_wt))
    offs_0_smp   = int(math.floor(offs_0_ms * srate / 1000))
    offs_1_smp   = int(math.floor(offs_1_ms * srate / 1000))
    ch_cnt       = aM.shape[1]
    note_dur_smp = int(round(note_dur_secs*srate))
    inter_note_smp = int(round(inter_note_secs*srate))

    yN = len(markL) * (note_dur_smp + inter_note_smp)
    yM = np.zeros((yN,ch_cnt))
    yi = 0

    oMarkL = []
    
    for beg_sec,end_sec,vel_label in markL:
        bsi = int(round(beg_sec * srate))
        esi = int(round(end_sec * srate))
        
        for ch_idx in range(ch_cnt):

            wt0,wtbi = select_wave_table( aM[:,ch_idx], esi-offs_0_smp, smp_per_wt )
            wt1,_    = select_wave_table( aM[:,ch_idx], esi-offs_1_smp, smp_per_wt)
            

            rms0 = np.pow( np.mean( np.pow(wt0,2) ),0.5)
            rms1 = np.pow( np.mean( np.pow(wt1,2) ),0.5)

            wt1 = [ w*rms0/rms1 for w in wt1 ]

            # The attack abutts the wavetable at it's center point
            # so we need to offset the end of the attack half way
            # through the first wave table.
            abi  = int(wtbi + smp_per_wt/2)
            
            atkN = abi - bsi
            yM[yi:yi+atkN,ch_idx] = aM[bsi:abi,ch_idx]

            oMarkL.append(((yi+atkN)/srate, (yi+atkN)/srate, f"{vel_label}-{ch_idx}"))


            phs0 = 0
            phs1 = fsmp_per_wt/2
            phs2 = fsmp_per_wt/4
            phs3 = fsmp_per_wt/4 + fsmp_per_wt/2
            g    = 1.0
            g_phs = 0
            for i in range(note_dur_smp-atkN):

                s0 = table_read_2( wt0, phs0 )
                s1 = table_read_2( wt0, phs1 )

                s2 = table_read_2( wt1, phs2 )
                s3 = table_read_2( wt1, phs3 )
                
                e0 = hann_read(phs0,fsmp_per_wt)
                e1 = hann_read(phs1,fsmp_per_wt)

                e2 = hann_read(phs0,fsmp_per_wt)
                e3 = hann_read(phs1,fsmp_per_wt)
                
                # advance the phases of the oscillators
                phs0 += 1
                if phs0 >= smp_per_wt:
                    phs0 -= smp_per_wt

                phs1 += 1
                if phs1 >= smp_per_wt:
                    phs1 -= smp_per_wt

                phs2 += 1
                if phs2 >= smp_per_wt:
                    phs2 -= smp_per_wt

                phs3 += 1
                if phs3 >= smp_per_wt:
                    phs3 -= smp_per_wt

                    
                mix_g = math.cos(0.25*2*math.pi*i/srate)

                #yM[yi+atkN+i,ch_idx] = g* (mix_g*(e0*s0 + e1*s1) + (1.0-mix_g)*(e2*s2 + e3*s3))
                yM[yi+atkN+i,ch_idx] = g* (e0*s0 + e1*s1)

                g_phs += 1
                if g_phs >= 64:
                    g *= g_coeff
                    g_phs = 0

                
        yi += note_dur_smp + inter_note_smp
        
            

    wt_util.write_audio_file( yM, srate, o_audio_fname )

    wt_util.write_mark_tsv_file(oMarkL, o_mark_tsv_fname)





if __name__ == "__main__":

    #sine_0()
    piano_1()
