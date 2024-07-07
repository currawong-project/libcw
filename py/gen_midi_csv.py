import csv,os

def gen_sample_midi_csv(pitch,velA,note_on_sec,note_off_sec):

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
        status = 0x90
        
        
        r = { 'uid':len(msgA),
               'tpQN':None,
               'bpm':None,
               'dticks':dticks,
               'ch':ch,
               'status':status,
               'd0':pitch,
               'd1':vel }

        msgA.append(r)

        dticks = ticks_per_note_on
        
        r  = { 'uid':len(msgA),
               'tpQN':None,
               'bpm':None,
               'dticks':dticks,
               'ch':ch,
               'status':status,
               'd0':pitch,
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
    


if __name__ == "__main__":

    out_dir        = "/home/kevin/temp/wt"
    note_on_sec    = 1.0
    note_off_sec   = 0.5
    pitch          = 60
    velA           = [1,8,15,22,29,36,42,49,56,63,70,77,84,91,98,105,112,119,126]

    msgA,markA = gen_sample_midi_csv(60,velA,note_on_sec,note_off_sec)

    midi_csv_fname = os.path.join(out_dir,f"{pitch}_sample.csv")
    mark_fname     = os.path.join(out_dir,f"{pitch}_marker.txt")

    print(midi_csv_fname)
    print(mark_fname)
    
    write_file(midi_csv_fname,msgA)
    write_marker_file(mark_fname,markA)
