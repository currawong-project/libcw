import subprocess
import os


def gen_ivory_player_caw_pgm( out_dir, caw_fname, midi_csv_fname, audio_fname, midi_dev_label, midi_port_label ):

    caw_template = f"""
    {{
    base_dir:    "{out_dir}"
    io_dict:     "{out_dir}/io.cfg"
    proc_dict:   "~/src/caw/src/libcw/flow/proc_dict.cfg",
    subnet_dict: "~/src/caw/src/libcw/flow/subnet_dict.cfg",
    
    programs: {{

      sample_generator: {{
        non_real_time_fl:false,
          network: {{
            procs: {{
	      mf:   {{ class: midi_file, args:{{ csv_fname:"{midi_csv_fname}" }}}},
	      mout: {{ class: midi_out  in:{{ in:mf.out }}, args:{{ dev_label:"{midi_dev_label}", port_label:"{midi_port_label}" }}}},
	      stop: {{ class: halt,     in:{{ in:mf.done_fl }}}}		       

	      ain:   {{ class: audio_in,                           args:{{ dev_label:"main" }}}},
	      split: {{ class: audio_split,    in:{{ in:ain.out }}   args:{{ select: [0,0, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1] }}}},
	      af:    {{ class: audio_file_out, in:{{ in:split.out0 }}, args:{{ fname:"{audio_fname}"}}}},	  
	      aout:  {{ class: audio_out,      in:{{ in:ain.out }},  args:{{ dev_label:"main"}}}},
	    }}
          }}    		   
        }}
      }}
    }}
    """
    with open(caw_fname, "w") as f:
        f.write(caw_template)


def gen_audio_file( out_dir, midi_csv_fname, midi_dev_label, midi_port_label, caw_exec_fname, caw_cfg_fname, audio_fname ):
    
    gen_ivory_player_caw_pgm(out_dir, caw_cfg_fname, midi_csv_fname, audio_fname, midi_dev_label, midi_port_label )
    
    argL = [ caw_exec_fname, "exec", caw_cfg_fname, "sample_generator" ]

    print(" ".join(argL))

    p = subprocess.Popen(argL,stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    out,err =p.communicate()

    if p.returncode != 0:
        print("The call '%s' to failed with code:%i Message:%s %s"," ".join(argL),p.returncode,out,err)
        
def gen_all_files( work_dir,midi_dev_label,midi_port_label,caw_exec_fname,midi_filterL=None ):

    fnL = os.listdir(work_dir)

    for fn in fnL:
        if "." in fn:
            fn_ext_partL = fn.split(".")
            if fn_ext_partL[1] == "csv":
                fn_partL = fn_ext_partL[0].split("_")

                mark_fn = "_".join(fn_partL[0:-1]) + "_marker.txt"

                damp_fl = fn_partL[1] == "damp"

                midi_pitch = int(fn_partL[0])

                if midi_filterL is None or (midi_filterL is not None and midi_pitch in midi_filterL):

                    wav_dir = os.path.join(work_dir,"wav")

                    if not os.path.isdir(wav_dir):
                        os.mkdir(wav_dir)

                    damp_label = "damp_" if damp_fl else ""
                    audio_fname  = os.path.join(wav_dir,f"{midi_pitch:03}_{damp_label}samples.wav")

                    midi_csv_fname = os.path.join(work_dir,fn)
                    mark_tsv_fname = os.path.join(work_dir,mark_fn)

                    caw_cfg_fname = os.path.join(work_dir,f"{midi_pitch:03}_{damp_label}caw.cfg")

                    print(midi_pitch,midi_csv_fname,mark_tsv_fname,audio_fname)

                    gen_audio_file( work_dir, midi_csv_fname, midi_dev_label, midi_port_label, caw_exec_fname, caw_cfg_fname, audio_fname )


if __name__ == "__main__":

    work_dir        = "/home/kevin/temp/wt6"
    midi_dev_label  = "MIDIFACE 2x2"
    midi_port_label = "MIDIFACE 2x2 Midi Out 1"
    caw_exec_fname  = "/home/kevin/src/caw/build/linux/debug/bin/caw"
    midi_filterL    = None
    gen_all_files(work_dir,midi_dev_label,midi_port_label,caw_exec_fname,midi_filterL)
    

    
