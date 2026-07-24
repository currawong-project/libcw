import os
import csv
import struct

def parse_midi_recorder_file( fname ):

    with open(fname,'rb') as f:
        buf        = f.read()
        recd_byteN = 40
        recdN      = int(len(buf)/recd_byteN)
        recdL      = []

        # The file must always be a multiple of the record size
        assert len(buf) % recd_byteN == 0
        
        sec0 = 0
        for i in range(recdN):
            b = buf[i*40:(i+1)*40]
            
            tv_sec,tv_nsec,dev_idx,port_idx,uid,ch,status,d0,d1,loc,pad = struct.unpack_from('@LLiiiBBBBii', b)

            sec = tv_sec + (tv_nsec / 1000000000.0);
                        
            recdL.append(dict(sec=sec,dev_idx=dev_idx,port_idx=port_idx,ch=ch,status=status,d0=d0,d1=d1,loc=loc,perf_seq_idx=i))

            #if i < 20:
            #    print(i,"sec:",tv_sec,tv_nsec, "dev:",dev_idx,"port:",port_idx,uid,ch,status,d0,d1,loc,pad)
            
    return recdL


def write_csv( recdL, fname ):

    titleL = list(recdL[0].keys())

    with open(fname,"w") as f:
        wtr = csv.DictWriter(f,fieldnames=titleL)

        wtr.writeheader()

        for r in recdL:
            wtr.writerow(r)

if __name__ == "__main__":

    midi_record_fname = "~/src/cw_proj/rehear_prep/io/tester_record/data/record_13.midi_recorder"
    csv_fname = "~/temp/midi_recorder.csv"

    midi_record_fname = os.path.expanduser(midi_record_fname)
    csv_fname = os.path.expanduser(csv_fname)

    recdL = parse_midi_recorder_file(midi_record_fname)
    write_csv( recdL, csv_fname )
