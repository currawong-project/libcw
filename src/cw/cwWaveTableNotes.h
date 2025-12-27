//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwWaveTableNotes_h
#define cwWaveTableNotes_h

namespace cw
{
  namespace wt_note
  {
    typedef dsp::sample_t sample_t;
    typedef dsp::srate_t  srate_t;

    typedef struct note_str
    {
      uint8_t instr_idx;  // wave-table-bank instrument index
      uint8_t pitch;      // midi pitch
      uint8_t velocity;   // midi velocity
      uint8_t _pad;       // structure padding byte
      double  delta_sec;  // offset from previous note onset in sec's
      double  dur_sec;    // note duration in seconds      
    } note_t;

    // Generate a single note and sum it into the output buffer.
    rc_t gen_note(  wt_bank::handle_t wtbH,
                    unsigned          instr_idx,
                    unsigned          midi_pitch,
                    unsigned          velocity,
                    srate_t           srate,
                    sample_t**        audioChA,
                    unsigned          audioChN,
                    unsigned          audioFrmN );

    // Fill an output signal with all specified notes.
    rc_t gen_notes( wt_bank::handle_t wtbH,
                    const note_t*     noteA,
                    unsigned          noteN,
                    srate_t           srate,
                    sample_t**        outChA,
                    unsigned          outChN,
                    unsigned          outFrmN );

    
    // Generate a single output file containing all specified notes.
    rc_t gen_notes( wt_bank::handle_t wtbH,
                    const note_t*     noteA,
                    unsigned          noteN,
                    srate_t           srate,
                    unsigned          audioChN,
                    const char*       out_audio_fname,
                    unsigned          audio_bits = 32);

    // Generate an output file per pitch for each sampled velocity.
    rc_t gen_notes( const char* wtb_json_fname,
                    unsigned instr_idx,
                    unsigned min_pitch,
                    unsigned max_pitch,
                    srate_t  srate,
                    unsigned audioChN,
                    double note_dur_sec,
                    double inter_note_sec,
                    const char* out_dir );

    rc_t test( const test::test_args_t& args );

    

               
  }
}

#endif
