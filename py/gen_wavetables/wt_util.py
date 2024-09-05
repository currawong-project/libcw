import wave as w
import math
import array
import numpy as np
import scipy.io.wavfile

def midi_pitch_to_hz( midi_pitch ):
    return 13.75 * math.pow(2,(-9.0/12.0)) * math.pow(2.0,(midi_pitch / 12.0))


def parse_marker_file( marker_fname ):

    markL = []
    with open(marker_fname) as f:
        for line in f:
            tokL = line.split("\t");

            assert( len(tokL) == 3 )
            
            markL.append( ( float(tokL[0]), float(tokL[1]), tokL[2] ) )

    return markL

def parse_audio_file( audio_fname ):

    max_smp_val = float(0x7fffffff)
    
    with w.open(audio_fname,"rb") as f:
        print(f"ch:{f.getnchannels()} bits:{f.getsampwidth()*8} srate:{f.getframerate()} frms:{f.getnframes()}")

        srate      = f.getframerate()
        frmN       = f.getnframes()
        data_bytes = f.readframes(frmN)
        smpM       = np.array(array.array('i',data_bytes))

        # max_smp_val assumes 32 bits
        assert( f.getsampwidth() == 4 )

        smpM = smpM / max_smp_val
        
        smpM = np.reshape(smpM,(frmN,2))

    return smpM,srate

def write_audio_file( xM, srate, audio_fname ):

    xM *= np.iinfo(np.int32).max

    scipy.io.wavfile.write(audio_fname, srate, xM.astype(np.int32))

def write_audio_file_0( xM, srate, audio_fname ):

    # Convert to (little-endian) 32 bit integers.
    xM = (xM * (2 ** 31 - 1)).astype("<i4")
    
    with w.open(audio_fname,"w") as f:

        f.setnchannels(xM.shape[0])
        f.setsampwidth(4)
        f.setframerate(srate)
        f.setnframes(xM.shape[1])


        f.writeframes(xM.tobytes())

def write_mark_tsv_file( markL, fname ):
### markL = [(beg_sec,end_sec,label)]    
    with open(fname,"w") as f:
        for beg_sec,end_sec,label in markL:
            f.write(f"{beg_sec}\t{end_sec}\t{label}\n")

            
def find_zero_crossing( xV, si, inc ):
# find the next zero crossing before/after si
    
    def sign(x):
        return x<0

    
    while si > 0 and si < len(xV):
        
        if sign(xV[si-1])==False and sign(xV[si])==True:        
            return si
        
        si += inc

    return None

            
