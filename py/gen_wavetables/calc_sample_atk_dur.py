import matplotlib.pyplot as plt
import numpy as np
import math
import wt_util

from kneed import KneeLocator

def rms( aV, wnd_smp_cnt, hop_smp_cnt, dbFl = True ):

    assert( wnd_smp_cnt % hop_smp_cnt == 0 and hop_smp_cnt < wnd_smp_cnt )
    
    
    rmsL = []
    bi   = 0
    ei   = wnd_smp_cnt

    while ei <= len(aV):

        rms = np.pow( np.mean( np.pow(aV[bi:ei],2) ), 0.5 )
        if dbFl:
            rms = -100.0 if rms == 0 else 20*math.log10(rms)
            
        rmsL.append(rms)
        
        bi += hop_smp_cnt
        ei = bi + wnd_smp_cnt

    # repeat the first RMS value (wnd_smp_cnt/hop_smp_cnt)-1 times
    # so that rmsL[] indexes relate to aV[] indexes like this:
    # av_idx = rms_idx * hop_smp_cnt
    rmsL = [ rmsL[0] ] * (int(wnd_smp_cnt/hop_smp_cnt)-1) + rmsL
    
    return rmsL

def calc_sample_atk_dur(audio_fname,mark_tsv_fname,rms_wnd_ms, rms_hop_ms ):

    aM,srate    = wt_util.parse_audio_file( audio_fname )
    markL       = wt_util.parse_marker_file( mark_tsv_fname )
    rms_wnd_smp_cnt = int(round(rms_wnd_ms * srate / 1000))
    rms_hop_smp_cnt = 64 #int(round(rms_hop_ms * srate / 1000))
    ch_cnt      = aM.shape[1]

    rmsL = [[] for _ in range(ch_cnt) ]
    
    for beg_sec,end_sec,vel_label in markL:

        bi = int(round(beg_sec * srate))
        ei = int(round(end_sec * srate)) + int(srate)
        
        for ch_idx in range(ch_cnt):
            rmsL[ch_idx] += rms(aM[bi:ei,ch_idx],rms_wnd_smp_cnt,rms_hop_smp_cnt)


    _,ax = plt.subplots(ch_cnt,1)

    for ch_idx in range(ch_cnt):
        ax[ch_idx].plot(rmsL[ch_idx])

    plt.show()
        

def generate_gate_knee(audio_fname,mark_tsv_fname,rms_wnd_ms, rms_hop_ms, min_gate_dur_ms, threshDb ):

    aM,srate        = wt_util.parse_audio_file( audio_fname )
    markL           = wt_util.parse_marker_file( mark_tsv_fname )
    rms_wnd_smp_cnt = int(round(rms_wnd_ms * srate / 1000))
    rms_hop_smp_cnt = int(round(rms_hop_ms * srate / 1000))
    min_gate_smp_cnt= int(round(min_gate_dur_ms * srate / 1000))
    ch_cnt          = aM.shape[1]
    frm_cnt         = aM.shape[0]
    rmsL            = []
    ch_rmsL         = []
    
    for ch_idx in range(ch_cnt):        
        rmsL.append( rms( aM[:,ch_idx], rms_wnd_smp_cnt, rms_hop_smp_cnt ) )
        ch_rmsL.append( np.mean( rmsL[-1] ) )

    bsiL = [ int(round(beg_sec*srate)) for beg_sec,_,_ in markL ]
    
    asiL = []
    riL = []
    bi   = 1
    ei   = rms_hop_smp_cnt
    eV   = np.zeros((frm_cnt,))

    # use the channel whith the most energy to determine the gate
    ch_idx = np.argmax(ch_rmsL)

    for beg_sec,end_sec,_ in markL:
        rbi = int(round(beg_sec*srate/rms_hop_smp_cnt))
        rei = int(round(end_sec*srate/rms_hop_smp_cnt))
        offs = 10
        y = rmsL[ch_idx][rbi+offs:rei]
        x = np.arange(len(y))
        k1 = KneeLocator(x, y, curve="convex", direction="decreasing", interp_method="polynomial")

        ri = rbi + offs + k1.knee
        riL.append( ri )
        bsiL.append( int(rbi*rms_hop_smp_cnt) )
        asiL.append( int(ri * rms_hop_smp_cnt) )
        
    gateL = [(bsi,esi) for bsi,esi in zip(bsiL,asiL) ]

    # force all gates to have a duration of at least min_gate_smp_cnt
    if True:
        for i,(bsi,esi) in enumerate(gateL):
            if esi-bsi < min_gate_smp_cnt:
                #print("gate ext:",esi-bsi,min_gate_smp_cnt)
                gateL[i] = (bsi,bsi+min_gate_smp_cnt)

            # verify that successive gates do not overlap
            if i> 0:
                assert gateL[i][0] > gateL[i-1][1]

            if i < len(gateL)-1:
                assert  gateL[i][1] < gateL[i+1][0]

    if True:

        beL = [ (int(round(beg_secs*srate)), int(round(end_secs*srate))) for beg_secs,end_secs,_ in markL ]
        beL = [ (max(0,int((bi)/rms_hop_smp_cnt)), max(0,int((ei)/rms_hop_smp_cnt))) for bi,ei in beL ]
        
        _,ax = plt.subplots(3,1)
        ax[0].plot(rmsL[0])
        for bi,ei in beL:
            ax[0].vlines([bi,ei],-100.0,0.0,color="red")

        for ri in riL:
            ax[0].vlines([ri],-100,0,color="green")
            
        ax[1].plot(rmsL[1])
        for bi,ei in beL:
            ax[1].vlines([bi,ei],-100.0,0.0,color="red")
            
        ax[2].plot(eV)
        plt.show()

        if False:
            for i,(bi,ei) in enumerate(beL):
                offs = 10
                y = [ pow(10,z/20.0) for z in rmsL[0][bi+offs:ei] ]
                y = rmsL[0][bi+offs:ei]
                x = np.arange(len(y))
                k1 = KneeLocator(x, y, curve="convex", direction="decreasing", interp_method="polynomial")
                k1.plot_knee()
                plt.title(f"{i} {offs+k1.knee} {offs+k1.knee*rms_hop_smp_cnt/srate:.3f}")
                plt.show()
        
                
    return gateL,ch_rmsL
    
def generate_gate_db(audio_fname,mark_tsv_fname,rms_wnd_ms, rms_hop_ms, min_gate_dur_ms, threshDb ):

    aM,srate        = wt_util.parse_audio_file( audio_fname )
    markL           = wt_util.parse_marker_file( mark_tsv_fname )
    rms_wnd_smp_cnt = int(round(rms_wnd_ms * srate / 1000))
    rms_hop_smp_cnt = int(round(rms_hop_ms * srate / 1000))
    min_gate_smp_cnt= int(round(min_gate_dur_ms * srate / 1000))
    ch_cnt          = aM.shape[1]
    frm_cnt         = aM.shape[0]
    rmsL            = []
    ch_rmsL         = []
    
    for ch_idx in range(ch_cnt):        
        rmsL.append( rms( aM[:,ch_idx], rms_wnd_smp_cnt, rms_hop_smp_cnt ) )
        ch_rmsL.append( np.mean( rmsL[-1] ) )

    bsiL = [ int(round(beg_sec*srate)) for beg_sec,_,_ in markL ]
    asiL = []
    riL = []
    bi   = 1
    ei   = rms_hop_smp_cnt
    eV   = np.zeros((frm_cnt,))

    # use the channel whith the most energy to determine the gate
    ch_idx = np.argmax(ch_rmsL)

    bsi_idx   = 1
    cur_on_fl = 1.0     # 1.0 when the gate is high
    active_fl = True    # True if the gate is allowed to switch from low to high
    pend_fl = True      # True if the next attack is pending
    
    for i in range(len(rmsL[ch_idx])):

        # pend_fl prevents the gate from being turned off until the
        # actual attack has occurred (it goes false once an RMS above the thresh is seen)
        if pend_fl:
            pend_fl = rmsL[ch_idx][i] <= threshDb

        # if the rms is below the threshold
        off_fl = rmsL[ch_idx][i] < threshDb #and rmsL[][i] < threshDb

        # if the rms is below the threshold and the gate detector is enabled ...
        if off_fl and active_fl and not pend_fl:
            # ... then turn off the gate
            cur_on_fl = 0.0
            active_fl = False
            riL.append(i)
            asiL.append(bi)
        
        eV[bi:ei] = cur_on_fl
        
        # track the smp idx of the current rms value
        bi  = i * rms_hop_smp_cnt
        ei = bi + rms_hop_smp_cnt

        # if we are crossing into the next velocity sample
        if bsi_idx < len(bsiL) and bsiL[ bsi_idx ] <= bi :
            
            # be sure that this onset follows an offset
            # (which won't occur if the signal never goes above the threshold)
            if cur_on_fl != 0:
                
                gesi = bsiL[bsi_idx-1] + min_gate_smp_cnt
                asiL.append( gesi )
                riL.append( int(round(gesi/rms_hop_smp_cnt)) )
                eV[gesi:ei] = 0
                
            #assert( cur_on_fl == 0 )
            
            active_fl = True
            pend_fl = True
            cur_on_fl = 1.0
            bsi_idx += 1

    
    # if the offset for the last note was not detected
    if len(asiL) == len(bsiL)-1:
        asiL.append(frm_cnt-1)
        
    gateL = [(bsi,esi) for bsi,esi in zip(bsiL,asiL) ]

    # force all gates to have a duration of at least min_gate_smp_cnt
    if True:
        for i,(bsi,esi) in enumerate(gateL):
            if esi-bsi < min_gate_smp_cnt:
                #print("gate ext:",esi-bsi,min_gate_smp_cnt)
                gateL[i] = (bsi,bsi+min_gate_smp_cnt)

            # verify that successive gates do not overlap
            if i> 0:
                assert gateL[i][0] > gateL[i-1][1]

            if i < len(gateL)-1:
                assert  gateL[i][1] < gateL[i+1][0]

    if False:

        beL = [ (int(round(beg_secs*srate)), int(round(end_secs*srate))) for beg_secs,end_secs,_ in markL ]
        beL = [ (max(0,int((bi)/rms_hop_smp_cnt)), max(0,int((ei)/rms_hop_smp_cnt))) for bi,ei in beL ]
        
        _,ax = plt.subplots(3,1)
        ax[0].plot(rmsL[0])
        for bi,ei in beL:
            ax[0].vlines([bi,ei],-100.0,0.0,color="red")

        for ri in riL:
            ax[0].vlines([ri],-100,0,color="green")
            
        ax[1].plot(rmsL[1])
        for bi,ei in beL:
            ax[1].vlines([bi,ei],-100.0,0.0,color="red")
            
        ax[2].plot(eV)
        plt.show()
        
                
    return gateL,ch_rmsL


def generate_gate_pct(audio_fname,mark_tsv_fname,rms_wnd_ms, rms_hop_ms, atk_min_dur_ms, threshPct ):
    
    aM,srate        = wt_util.parse_audio_file( audio_fname )
    markL           = wt_util.parse_marker_file( mark_tsv_fname )
    
    rms_wnd_smp_cnt = int(round(rms_wnd_ms * srate / 1000))
    rms_hop_smp_cnt = int(round(rms_hop_ms * srate / 1000))
    atk_min_smp_cnt = int(round(atk_min_dur_ms * srate / 1000))
    
    ch_cnt          = aM.shape[1]
    frm_cnt         = aM.shape[0]
    rmsL            = []
    ch_rmsL         = []
    
    for ch_idx in range(ch_cnt):        
        rmsL.append( rms( aM[:,ch_idx], rms_wnd_smp_cnt, rms_hop_smp_cnt, False ) )
        ch_rmsL.append( np.mean(rmsL[-1] ))
    
    beL = [ (int(round(beg_secs*srate)), int(round(end_secs*srate))) for beg_secs,end_secs,_ in markL ]
    beL = [ (max(0,int(bi/rms_hop_smp_cnt)), max(0,int(ei/rms_hop_smp_cnt))) for bi,ei in beL ]
    
    gateL = []
    maxL = []
    for bri,eri in beL:

        rms_max = None
        rms_max_i = None
        rms_max_ch_i = None
        for ch_idx in range(ch_cnt):
            max_i = np.argmax( rmsL[ch_idx][bri:eri] ) + bri
            
            if rms_max is None or rms_max < rmsL[ch_idx][max_i]:
                rms_max = rmsL[ch_idx][max_i]
                rms_max_i = max_i
                rms_max_ch_i = ch_idx

        maxL.append(rms_max)
            
        threshDb = rms_max * threshPct
        
        for i in range(rms_max_i+1,eri):
            if rmsL[ch_idx][i] < threshDb:
                gateL.append((bri,i))
                break

        
    retL = []
    for bri,eri in gateL:
        bsi = bri*rms_hop_smp_cnt
        esi = eri*rms_hop_smp_cnt
        if esi-bsi < atk_min_smp_cnt:
            esi = bsi + atk_min_smp_cnt
        retL.append((bsi,esi))


    if True:
        _,ax = plt.subplots(2,1)
        ax[0].plot(rmsL[0])
        for i,(bi,ei) in enumerate(gateL):
            ax[0].vlines([bi,ei],0,maxL[i],color="red")
        ax[1].plot(rmsL[1])
        for i,(bi,ei) in enumerate(gateL):
            ax[1].vlines([bi,ei],0,maxL[i],color="red")
        plt.show()
        
    return retL,ch_rmsL



def gen_gated_audio( i_audio_fname, gateL, o_audio_fname, o_mark_tsv_fname ):
    
    aM,srate = wt_util.parse_audio_file( audio_fname )

    markL = []
    gateV = np.zeros((aM.shape[0],))

    # form the gate vector 
    for i,(bsi,esi) in enumerate(gateL):
        gateV[bsi:esi] = 1
        markL.append((bsi/srate,esi/srate,f"{i}"))

    for ch_idx in range(aM.shape[1]):
        aM[:,ch_idx] *= gateV

    wt_util.write_audio_file( aM, srate, o_audio_fname )
    wt_util.write_mark_tsv_file( markL, o_mark_tsv_fname )
        
        

if __name__ == "__main__":

    audio_fname = "/home/kevin/temp/wt5/wav/060_samples.wav"
    mark_tsv_fname = "/home/kevin/temp/wt5/60_marker.txt"
    rms_wnd_ms = 50
    rms_hop_ms = 10

    #calc_sample_atk_dur(audio_fname,mark_tsv_fname,rms_wnd_ms,rms_hop_ms)

    # Generate a list [(bsi,esi)] indicating the beginning and end of the attack portion
    # of each sample where the end is determined by a threshold in dB.
    #threshDb = -50.0
    #gateL = generate_gate_db(audio_fname,mark_tsv_fname,rms_wnd_ms, rms_hop_ms, threshDb )

    
    # Generate a list [(bsi,esi)] indicating the beginning and end of the attack portion
    # of each sample where the end is determined by a percent decrease from the peak value.
    threshPct = 0.75
    gateL = generate_gate_pct(audio_fname,mark_tsv_fname,rms_wnd_ms, rms_hop_ms, threshPct )
    
    gen_gated_audio( audio_fname, gateL, "/home/kevin/temp/temp.wav", "/home/kevin/temp/temp_mark.txt")
