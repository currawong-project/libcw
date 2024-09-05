import csv,os

def gen_sample_midi_events(pitch,velA,note_on_sec,note_off_sec,dampFl):

    markA = []
    msgA  = []
    tpqn  = 1260
    bpm   = 60

    ticks_per_sec      = tpqn * bpm / 60.0
    ticks_per_note_on  = ticks_per_sec * note_on_sec
    ticks_per_note_off = ticks_per_sec * note_off_sec
    uid                = 0
    dticks             = 0
    cur_sec            = 0;
    
    r = { 'uid':len(msgA),
           'tpQN':tpqn,
           'bpm':bpm,
           'dticks':0,
           'ch':None,
           'status':None,
           'd0':None,
           'd1':None }

    msgA.append(r);

    for vel in velA:

        ch     = 0
        note_status = 0x90
        ctl_status = 0xb0
        damp_ctl   = 0x40
        
        if dampFl:
            r = { 'uid':len(msgA),
                  'tpQN':None,
                  'bpm':None,
                  'dticks':dticks,
                  'ch':ch,
                  'status':ctl_status,
                  'd0':damp_ctl,
                  'd1':65 }
            
            msgA.append(r)
            dticks = 0

        
        
        r = { 'uid':len(msgA),
               'tpQN':None,
               'bpm':None,
               'dticks':dticks,
               'ch':ch,
               'status':note_status,
               'd0':pitch,
               'd1':vel }

        msgA.append(r)

        dticks = ticks_per_note_on
        
        r  = { 'uid':len(msgA),
               'tpQN':None,
               'bpm':None,
               'dticks':dticks,
               'ch':ch,
               'status':note_status,
               'd0':pitch,
               'd1':0 }
        
        msgA.append(r)

        if dampFl:
            r  = { 'uid':len(msgA),
                   'tpQN':None,
                   'bpm':None,
                   'dticks':0,
                   'ch':ch,
                   'status':ctl_status,
                   'd0':damp_ctl,
                   'd1':0 }
            
            msgA.append(r)
        
        dticks = ticks_per_note_off

        markA.append( (cur_sec, cur_sec+note_on_sec, vel) )

        cur_sec += note_on_sec + note_off_sec

    return msgA,markA

def write_file( fname, msgA ):

    fieldnames = list(msgA[0].keys())

    with open(fname,"w") as f:
        wtr = csv.DictWriter(f, fieldnames=fieldnames)

        wtr.writeheader()

        for m in msgA:
            wtr.writerow(m)
        
def write_marker_file(fname, markA ):

    with open(fname,"w") as f:
        for beg_sec,end_sec,vel in markA:
            f.write(f"{beg_sec}\t{end_sec}\t{vel}\n")
    
def gen_midi_csv_and_marker_files( pitch, velA, note_on_sec, note_off_sec, damp_fl, out_dir ):

    if not os.path.isdir(out_dir):
        os.mkdir(out_dir)
    
    msgA,markA = gen_sample_midi_events(pitch,velA,note_on_sec,note_off_sec,damp_fl)

    damp_label = "damp_" if damp_fl else ""
    
    midi_csv_fname = os.path.join(out_dir,f"{pitch:03}_{damp_label}sample.csv")
    mark_fname     = os.path.join(out_dir,f"{pitch:03}_{damp_label}marker.txt")

    write_file(midi_csv_fname,msgA)
    write_marker_file(mark_fname,markA)

    return midi_csv_fname, mark_fname

def gen_complete_midi_csv( pitchA, velA, note_on_sec, note_off_sec, out_fname ):
    damp_fl = False
    msgL = []
    for i,pitch in enumerate(pitchA):
        msgA,_ = gen_sample_midi_events(pitch,velA,note_on_sec,note_off_sec,damp_fl)
        if i > 0:
            msgA = msgA[1:]
        msgL += msgA
        
    write_file(out_fname,msgL)

if __name__ == "__main__":

    # min_pitch      = 21
    # max_pitch      = 108
    
    out_dir       = "/home/kevin/temp"
    dampFl        = False
    velA          = [ 1,5,10,16,21,26,32,37,42,48,53,58,64,69,74,80,85,90,96,101,106,112,117,122,127]
    note_off_sec  = 2.0

    if False:
        pitchL        = [ 21, 60 ]
        noteDurL      = [ 20.0, 20.0 ]
        
    if True:
        pitchL = [ i for i in range(21,109) ]
        noteDurL = [ 20.0 for _ in range(len(pitchL)) ]
        
    if False:
        dampFlL       = [ False, True ] if dampFl else [ False ]

        for pitch,note_on_sec in zip(pitchL,noteDurL):
            for damp_fl in dampFlL:
                csv_fname, mark_fname = gen_midi_csv_and_marker_files( pitch, velA, note_on_sec, note_off_sec, damp_fl, out_dir )

    if True:
        note_on_sec = 5
        note_off_sec = 1
        out_fname = "/home/kevin/temp/all_midi.csv"
        gen_complete_midi_csv(pitchL, velA, note_on_sec, note_off_sec, out_fname)
    
