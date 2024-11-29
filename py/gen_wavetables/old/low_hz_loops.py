import wt_util
import math
import numpy as np
import matplotlib.pyplot as plt
import audiofile as af

def sign(x):
    return x<0

def find_zero_crossing( xV, si, inc ):
# find the next zero crossing before/after si
    
    while si > 0:
        
        if sign(xV[si-1])==False and sign(xV[si])==True:        
            break;
        si += inc

    return si

def plot_xfades( zV, zL ):

    fig,ax = plt.subplots(1,1)

    ax.plot(zV)
    for bi,ei in zL:
        ax.vlines([bi,ei],-0.5,0.5)
    plt.show()
    
def decay_compensation( xV, smp_per_cyc ):

    def _calc_rms( sV, rmsN ):
        bi = 0
        ei = rmsN
        rmsL = []
        while ei < len(sV):
            rms = np.pow(np.mean( np.pow( sV[bi:ei], 2 )),0.5)
            rmsL.append(rms)
            bi += 1
            ei += 1
            
        return rmsL

    # calc the RMS env
    rmsN = int(round(smp_per_cyc))
    rmsL = _calc_rms( xV, rmsN)

    # fit a line to the RMS env
    x = np.arange(len(rmsL))
    A = np.array( [x, np.ones((len(rmsL),))] ).T
    m, c = np.linalg.lstsq(A, rmsL)[0]

    # use the slope of the line to boost
    # to progressively boost the gain
    # of the signal over time
    yV = np.copy(xV)
    yi = rmsN
    for i,r in enumerate(rmsL):
        if yi < len(yV):
            c = r + i*(-m)
            g = c/r
            yV[yi] *= g
            yi += 1
        


    if False:
        # calc. the RMS env of the compensated signal
        c_rmsL = _calc_rms(yV,rmsN)
        
        db0 = 20.0*math.log10(rmsL[0])
        db1 = 20.0*math.log10(rmsL[-1])
        print(db0-db1, m*len(rmsL), len(rmsL))
    
        #c_rmsL = [ r + i*(-m) for i,r in enumerate(rmsL) ]
        
        fig,ax = plt.subplots(1,1)
        ax.plot(rmsL)
        ax.plot(x,m*x+c,'r')
        ax.plot(c_rmsL)
        plt.show()
    
    return yV    
    
def apply_gain( xxV,bi,ei,g, g_coeff ):

    i = bi
    while i < ei:

        n = min(ei-i,64)

        for j in range(n):
            xxV[i+j] *= g

        g *= g_coeff
        
        i += n

    return g
        
def gen_note( xV, xbi, xei, loop_dur_smp, y_dur_smp, xfade_smp, smp_per_cyc, g_coeff, bli=None ):

    hannV       = np.hanning( xfade_smp*2 + 1 )
    fin_env     = hannV[0:xfade_smp+1]
    fout_env    = hannV[xfade_smp:]
    env_smp_cnt = len(fin_env)
    assert( len(fout_env) == env_smp_cnt)
    #print(fin_env[-1],fout_env[0],len(fin_env),len(fout_env))

    if bli is None:
        bli = find_zero_crossing( xV, xei - loop_dur_smp, -1 )

    aN = (bli - xbi) + env_smp_cnt
    aV = np.copy(xV[xbi:xbi+aN])

    aV[-env_smp_cnt:] *= fout_env

    lV = np.copy(xV[bli:bli+loop_dur_smp])
    
    if False:
        # compensate for loop period decay
        lV = decay_compensation(lV,smp_per_cyc)

    zV = np.zeros((y_dur_smp,))

    zV[0:aN] = aV
    zbi = aN-env_smp_cnt 
    zei = zbi + len(lV)

    zL = []
    g = 1.0
    while(zei < len(zV)):

        zL.append((zbi,zei))

        elV = np.copy(lV)
        elV[0:env_smp_cnt] *= fin_env
        elV[-env_smp_cnt:] *= fout_env

        zV[zbi:zei] += elV

        g = apply_gain(zV,zbi,zei-env_smp_cnt,g,g_coeff)
        
        zbi = zei - env_smp_cnt
        zei = zbi + len(elV)

        lV = np.flip(lV)


    return zV,zL,bli,xV[bli:bli+loop_dur_smp]


def main( i_audio_fname, mark_tsv_fname, midi_pitch, loop_secs, note_dur_secs, xfade_ms, inter_note_sec, g_coeff, o_audio_fname, o_mark_tsv_fname ):
    
    i_markL = wt_util.parse_marker_file(mark_tsv_fname)
    xM,srate = wt_util.parse_audio_file(i_audio_fname)
    chN = xM.shape[1]

    fund_hz        = wt_util.midi_pitch_to_hz(midi_pitch)
    smp_per_cyc    = srate / fund_hz    
    loop_dur_fsmp  = loop_secs * srate
    cyc_per_loop   = int(loop_dur_fsmp / smp_per_cyc)
    loop_dur_fsmp  = cyc_per_loop * smp_per_cyc
    loop_dur_smp   = int(math.floor(loop_dur_fsmp))
    xfade_smp      = int(round(srate * xfade_ms / 1000.0))
    inter_note_smp = int(round(srate*inter_note_sec))
    note_dur_smp   = int(round(srate*note_dur_secs))
    note_cnt       = len(i_markL)
        
    print(f"{smp_per_cyc:.3f} smps/cyc  {smp_per_cyc/srate:.3f} secs/cyc")
    print(f"loop {cyc_per_loop} cycles dur: {loop_dur_fsmp/srate:.3f} secs")
    print(f"xfade: {xfade_ms} ms {xfade_smp} smp")

    yN = note_cnt * (note_dur_smp + inter_note_smp)
    yM = np.zeros((yN,chN))
    yi = 0;
    o_markL = []
    
    for beg_sec,end_sec,vel_label in i_markL:

        vel = int(vel_label)
        bsi = int(round(beg_sec * srate))
        esi = int(round(end_sec * srate))

        bli = None
        for ch_i in range(chN):

            zV,zL,bli,lV = gen_note( xM[:,ch_i], bsi, esi, loop_dur_smp, note_dur_smp, xfade_smp, smp_per_cyc, g_coeff, bli )

            if vel > 70:
                #plot_xfades(zV,zL)
                pass
              
            yM[yi:yi+len(zV),ch_i] = zV

            if ch_i == 0:
                o_markL.append( (yi/srate, (yi+len(zV))/srate, vel ))
            
        yi += len(zV) + inter_note_smp

        

    if False:
        fig,ax = plt.subplots(2,1)
        ax[0].plot(yM[:,0])
        ax[1].plot(yM[:,1])
        plt.show();
        
    wt_util.write_audio_file( yM, srate, o_audio_fname )
    wt_util.write_mark_tsv_file( o_markL, o_mark_tsv_fname )

def test_scipy(audio_fname):
    samplerate = 44100.0
    fs = 100
    t = np.linspace(0., 1., int(samplerate))
    data = np.sin(2. * np.pi * fs * t)
    data = np.array([data,data])
    wt_util.write_audio_file(data.T, samplerate, audio_fname)

if __name__ == "__main__":

    midi_pitch     = 21
    loop_secs      = 0.4
    note_dur_secs  = 7.0
    xfade_ms       = 2
    inter_note_sec = 0.1
    g_coeff        = 0.9995
    
    i_audio_fname  = "/home/kevin/temp/wt3/wav/21_samples.wav"
    mark_tsv_fname = "/home/kevin/temp/wt3/21_marker.txt"
    
    o_audio_fname = "/home/kevin/temp/temp.wav"
    o_mark_tsv_fname = "/home/kevin/temp/temp_mark.txt"

    main( i_audio_fname, mark_tsv_fname, midi_pitch, loop_secs, note_dur_secs, xfade_ms, inter_note_sec, g_coeff, o_audio_fname, o_mark_tsv_fname )

    if False:
        test_scipy(o_audio_fname)
