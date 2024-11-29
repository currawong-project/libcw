import os
import gen_midi_csv as gmc
import sample_looper as loop
import subprocess


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


def gen_audio_file( out_dir, midi_csv_fname, midi_dev_label, midi_port_label, caw_exec_fname, audio_fname ):


    caw_cfg_fname = os.path.join(out_dir,f"{midi_pitch:03}_caw.cfg")
    
    gen_caw_ivory_player_pgm(out_dir, caw_cfg_fname, midi_csv_fname, audio_fname, midi_dev_label, midi_port_label )
    
    argL = [ caw_exec_fname, "exec", caw_cfg_fname, "sample_generator" ]

    print(" ".join(argL))

    p = subprocess.Popen(argL,stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    out,err =p.communicate()

    if p.returncode != 0:
        print("The call '%s' to failed with code:%i Message:%s %s"," ".join(argL),p.returncode,out,err)
        

if __name__ == "__main__":

    # piano pitch range: 21-108
    
    midi_pitch      = 60
    out_dir         = "/home/kevin/temp/wt1"
    note_on_sec     = 1.5
    note_off_sec    = 0.5
    velA            = [1,8,15,22,29,36,42,49,56,63,70,77,84,91,98,105,112,119,126]
    velA            = [ 1,5,10,16,21,26,32,37,42,48,53,58,64,69,74,80,85,90,96,101,106,112,117,122,127]
    midi_dev_label  = "MIDIFACE 2x2"
    midi_port_label = "MIDIFACE 2x2 Midi Out 1"
    caw_exec_fname  = "/home/kevin/src/caw/build/linux/debug/bin/caw"
        
                                                          
    # Generate the MIDI CSV used to trigger the sampler and a TSV which indicates the location of the
    # triggered notes in seconds.
    midi_csv_fname, mark_tsv_fname = gmc.gen_midi_csv_and_marker_files( midi_pitch, velA, note_on_sec, note_off_sec, out_dir )
        
    audio_fname  = os.path.join(out_dir,"wav",f"{midi_pitch:03}_samples.wav")        
        
    #gen_audio_file( out_dir, midi_csv_fname, midi_dev_label, midi_port_label, caw_exec_fname, midi_pitch )
    
    loop_marker_out_fname = os.path.join(out_dir,f"{midi_pitch}_loop_mark.txt")
    wt_out_fname          = os.path.join(out_dir,"bank",f"{midi_pitch}_wt.json")

    argsD = {
        'end_offset_ms':100,
        'loop_dur_ms':20,       # 21=40, 60=20
        'midi_pitch':midi_pitch,
        'guess_cnt':5       
    }

    
                    
    loop.gen_loop_positions( audio_fname, mark_tsv_fname, midi_pitch, argsD, loop_marker_out_fname, wt_out_fname )
