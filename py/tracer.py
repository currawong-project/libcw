import os
import csv
import types
import numpy as np
import matplotlib.pyplot as plt



NANO_PER_SEC = 1000000000
BEG_EVT_ID = 1
END_EVT_ID = 2
DATA_EVT_ID= 3

def time_diff( t0, t1 ):

    s0,ns0 = t0
    s1,ns1 = t1
    
    if s0 == s1:
        return 0,ns1 - ns0

    d0 = NANO_PER_SEC - ns0
    
    dn = d0 + ns1
    ds = (s1-s0) - 1

    while dn > NANO_PER_SEC:
        dn -= NANO_PER_SEC
        ds += 1

    return (ds,dn)

def nsec_diff( t0, t1 ):
    ds,dn = time_diff(t0,t1)
    return dn + ds*NANO_PER_SEC

def is_less_than( t0, t1 ):
    """ Is t0 < t1 """
    
    s0,ns0 = t0
    s1,ns1 = t1

    return ns1>ns0 if s0==s1 else s1>s0
    

def time_to_sec( t ):
    s,ns = t
    return s + ns / NANO_PER_SEC
    
    

def parse_ref_file( ref_csv_fname ):

    refD = {}
    with open(ref_csv_fname) as f:
        rdr = csv.DictReader(f)

        for r in rdr:
            refD[(r['label'],int(r['label_id']))] = int(r['id'])

    return refD

def parse_data_file( dat_csv_fname ):

    datL = []
    with open(dat_csv_fname) as f:
        rdr = csv.DictReader(f)

        for r in rdr:
            t = (int(r['seconds']),int(r['nseconds']))
            
            datL.append(types.SimpleNamespace(**dict(time=t,
                                                     trace_id=int(r['trace_id']),
                                                     event_id=int(r['event_id']),
                                                     ud0=int(r['user0']),
                                                     ud1=int(r['user1']))))


    return datL

def event_dur_stats( datL, trace_id  ):

    for i,r in enumerate(datL):
        if r.trace_id==trace_id and r.event_id==BEG_EVT_ID:
            break

    bL = [ r.time for r in datL[i:] if r.trace_id==trace_id and r.event_id==BEG_EVT_ID ]
    eL = [ r.time for r in datL[i:] if r.trace_id==trace_id and r.event_id==END_EVT_ID ]


    dV = np.array([ nsec_diff(b,e) for b,e in zip(bL,eL)])

    #j = next((i for i,d in enumerate(dV) if d < 0),None)
    #print(j,dV[-1])

    mn_i = dV.argmin()
    mx_i = dV.argmax()
    avg = int(dV.mean())
    std = int(dV.std())
    med = int(np.median(dV))
    
    N = len(dV)
    
    print(f"min:{dV[mn_i]/1000.0:8.2f} ({mn_i/N:5.3f}) max:{dV[mx_i]/1000.0:8.2f} ({mx_i/N:5.3f}) avg:{avg/1000.0:8.2f} med:{med/1000.0:8.2f} std:{std/1000.0:8.2f}.")
    

def get_thread_index_dict( refD, thread_label ):
    
    threadD = { refD[(label,label_id)]:0 for i,(label,label_id) in enumerate(refD.keys()) if label == thread_label }

    # create a thread_label/id -> thread index map
    for i,(k,_) in enumerate(threadD.items()):
        threadD[k] = i
        
    return threadD
    
    
def get_thread_cycle_info( threadD, datL, bi, ei ):
    
    nL = [0] * len(threadD)

    # count the number of voices this thread processed during this cycle
    for x in datL[bi:ei]:
        if x.trace_id in threadD and x.event_id==DATA_EVT_ID:            
            nL[ threadD[x.trace_id] ] += 1

    return nL
            
    
    
def get_cycle_info( refD, datL, cycle_dur_usec_thresh ):
    infoL = []

    threadD = get_thread_index_dict(refD,"task_thread")
    
    flow_trace_id = refD[("flow",0)]
    vpc_trace_id = refD[("vctl",0)]
    
    def is_trace_match(x,trace_id,evt_id):
        return x.trace_id==trace_id and x.event_id==evt_id

    ei = 0
    while True:
        
        bi = next((i for i,x in enumerate(datL[ei:]) if is_trace_match(x,flow_trace_id,BEG_EVT_ID)), None)

        if bi is None or ei+bi+1 >= len(datL):
            break;

        bi += ei
        
        ei = next((i for i,x in enumerate(datL[bi:]) if is_trace_match(x,flow_trace_id,END_EVT_ID)), None)
        
        if ei is None:
            break

        ei += bi

        # cycle_idx should match on 'flow' start and end
        if datL[bi].ud0 != datL[ei].ud0:
            #print(bi,ei,datL[bi],datL[ei])
            pass
        

        # get the active voice count from the 'poly_voice_ctl'
        i = next((i for i,x in enumerate(datL[bi:ei]) if is_trace_match(x,vpc_trace_id,DATA_EVT_ID)), None) 

        active_voice_cnt = datL[bi+i].ud0 if i is not None else 0

        
        cycle_idx = datL[bi].ud0
        cycle_dur_usec = int(nsec_diff( datL[bi].time, datL[ei].time )/1000.0)
        dur_violate_fl = cycle_dur_usec > cycle_dur_usec_thresh

        thread_infoL = get_thread_cycle_info( threadD, datL, bi, ei )

        infoL.append( dict(cycle_idx=cycle_idx,active_voice_cnt=active_voice_cnt,cycle_dur_usec=cycle_dur_usec,thread_info=thread_infoL,dur_violate_fl=dur_violate_fl) )
        

    violate_cnt = sum([ 1 for x in infoL if x['dur_violate_fl']])
    print(f"dur violate cnt:{violate_cnt} {violate_cnt/len(infoL):3.2} pct.")
    
    return infoL,threadD
        
def plot_cycle_info( infoL, threadD, cycle_dur_usec_thresh ):

    _,axL = plt.subplots(3,1)

    xL = [ x['cycle_idx']        for x in infoL ]
    yL = [ x['active_voice_cnt'] for x in infoL ]

    N = len(xL)
    threadN = len(threadD)
    tM = np.zeros((N,threadN))
    

    if False:
        for i in range(N):
            for j in range(threadN):
                tM[i,j] = infoL[i]['thread_info'][j]
    

    axL[0].plot(xL,yL)
    #axL[1].plot(xL,tM)
    axL[2].hlines(y=cycle_dur_usec_thresh, xmin=xL[0], xmax=xL[-1])
    axL[2].plot(xL,[x['cycle_dur_usec'] for x in infoL])

    
    
    
    plt.show()

def total_seconds(datL):

    bi = next((i for i,x in enumerate(datL) if x.time != (0,0)),None)
    ei = next((i for i,x in enumerate(reversed(datL)) if x.time != (0,0)),None)

    return time_to_sec(time_diff(datL[bi].time, datL[-(ei+1)].time))
    
if __name__ == "__main__":

    if True:
        ref_csv_fname = "tracer_ref.csv"
        dat_csv_fname = "tracer_data.csv"

        refD = parse_ref_file(ref_csv_fname)
        datL = parse_data_file(dat_csv_fname)



        #dt = time_diff(datL[0].time, datL[-1].time)
        print(f"count:{len(datL)} dur:{total_seconds(datL):6.3f} s." )

    if True:
        trace_id = refD[("flow",0)]
        event_dur_stats( datL, trace_id )

    if True:
        cycle_dur_usec_thresh= int((64/48000.0)*1000000)
        
        infoL,threadD = get_cycle_info(refD,datL,cycle_dur_usec_thresh)
        plot_cycle_info(infoL,threadD,cycle_dur_usec_thresh)
