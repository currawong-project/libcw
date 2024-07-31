#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwAudioFile.h"
#include "cwMath.h"
#include "cwVectOps.h"
#include "cwMidi.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"
#include "cwWaveTableBank.h"
#include "cwWaveTableNotes.h"

cw::rc_t cw::wt_note::gen_note( wt_bank::handle_t wtbH,
                                unsigned          instr_idx,
                                unsigned          midi_pitch,
                                unsigned          velocity,
                                srate_t           srate, 
                                sample_t**        audioChA,
                                unsigned          audioChN,
                                unsigned          audioFrmN )
{
  rc_t                              rc            = kOkRC;
  unsigned                          chN           = audioChN; // output audio channels
  const wt_bank::multi_ch_wt_seq_t* mcs           = nullptr;  // wave table ptr 
  sample_t*                         aM            = nullptr;  // temp. audio buffer
  const unsigned                    kDspFrmN      = 64;       // default frame request count
  unsigned                          reqFrmN       = 0;        // count of sample frames requestdd
  unsigned                          retFrmN       = 0;        // countof sample frames returned   
  unsigned                          audioChFrmIdx = 0;        // current audio frame index into audioChA[]
  audiofile::handle_t afH;
  
  // multi-channel wave table oscillator
  struct dsp::multi_ch_wt_seq_osc::obj_str<sample_t,srate_t> osc;  

  // get the requested wave table
  if((rc = wt_bank::get_wave_table( wtbH, instr_idx, midi_pitch, velocity, mcs )) != kOkRC )
  {
    goto errLabel;
  }

  // if the wave table has fewer channels than the output audio buffer
  if( mcs->chN < chN )
    chN = mcs->chN;

  // TODO: VERIFY srate == SAMPLE RATE OF WAVETABLES
  // mcs->valid_srate(srate)

  // allocate and setup xthe oscillator
  if((rc = create(&osc,chN,mcs)) != kOkRC )
  {
    rc = cwLogError(rc,"multi-ch-wt-seq-osc create failed.");
    goto errLabel;
  }

  // allocate a tempory audio buffer 
  aM = mem::allocZ<sample_t>(chN*kDspFrmN);

  // for each sample in the output signal
  while( retFrmN==reqFrmN && audioChFrmIdx < audioFrmN )
  {
    // calc. the count of requested output audio frames on this iteration
    reqFrmN = std::min(kDspFrmN,audioFrmN-audioChFrmIdx);

    // generate reqFrmN output samples with the oscillator
    if((rc = process(&osc, aM, chN, reqFrmN, retFrmN)) != kOkRC )
      goto errLabel;

    // copy the generated signals into the output signal
    for(unsigned i=0; i<chN; ++i)
      vop::copy(audioChA[i]+audioChFrmIdx, (const sample_t*)(aM + i*reqFrmN), retFrmN);

    // advance the output signal 
    audioChFrmIdx += retFrmN;
  }

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"note generation failed.");
  
  mem::release(aM);
  destroy(&osc);
  
  return rc;
}

 
cw::rc_t cw::wt_note::gen_notes( wt_bank::handle_t wtbH,
                                 const note_t*     noteA,
                                 unsigned          noteN,
                                 srate_t           srate,
                                 sample_t**        outChA,
                                 unsigned          outChN,
                                 unsigned          outFrmN )
{
  rc_t      rc   = kOkRC;
  sample_t* chA[ outChN ];
  double    secs = 0;
  
  for(unsigned i=0; i<noteN; ++i)
  {
    
    const note_t* n           = noteA + i;

    secs += n->delta_sec;
    
    unsigned      beg_frm_idx = (unsigned)floor(secs*srate);
    unsigned      dur_frm_cnt = (unsigned)floor(n->dur_sec*srate);

    if( beg_frm_idx > outFrmN )
    {
      // TODO:
      assert(0);
    }

    if( beg_frm_idx + dur_frm_cnt > outFrmN )
    {
      // TODO:
      assert(0);
    }
    
    for(unsigned j=0; j<outChN; ++j)
      chA[j] = outChA[j] + beg_frm_idx;
    
    
    if((rc = gen_note( wtbH, n->instr_idx, n->pitch, n->velocity, srate, chA, outChN, dur_frm_cnt)) != kOkRC )
    {
      rc = cwLogError(rc,"Generate note failed on 'instr:%i pitch:%i vel:%i.",n->instr_idx, n->pitch, n->velocity);
      goto errLabel;
    }

  }

errLabel:
  cwLogInfo("Generated %6.1f seconds of audio.",secs);
  return rc;
}

cw::rc_t cw::wt_note::gen_notes( wt_bank::handle_t wtbH,
                                 const note_t*     noteA,
                                 unsigned          noteN,
                                 srate_t           srate,
                                 unsigned          audioChN,
                                 const char*       out_audio_fname,
                                 unsigned          audio_bits)
{
  rc_t rc = kOkRC;
  unsigned audioFrmN = 0;
  sample_t* audioM = nullptr;
  double max_sec = 0;
  sample_t* chA[ audioChN ];
  
  double secs = 0;
  for(unsigned i=0; i<noteN; ++i)
  {
    secs += noteA[i].delta_sec;
    
    if( secs+noteA[i].dur_sec > max_sec )
      max_sec = secs+noteA[i].dur_sec;
  }

  cwLogInfo("Allocated %i notes in  %6.1f seconds of audio.",noteN,max_sec);
  
  audioFrmN = (unsigned)ceil( max_sec * srate );
  audioM    = mem::allocZ<sample_t>( audioFrmN * audioChN );

  for(unsigned i=0; i<audioChN; ++i)
    chA[i] = audioM + (i*audioFrmN);

  if((rc = gen_notes( wtbH, noteA, noteN, srate, chA, audioChN, audioFrmN )) != kOkRC )
  {
    cwLogError(rc,"Note generation failed on '%s'.",cwStringNullGuard(out_audio_fname));
    goto errLabel;
  }

  if((rc = audiofile::writeFileFloat(  out_audio_fname, srate, audio_bits, audioFrmN, audioChN, chA)) != kOkRC )
  {
    cwLogError(rc,"Audio file write failed on '%s'.",cwStringNullGuard(out_audio_fname));
    goto errLabel;
  }
  
errLabel:
  mem::release(audioM);
  return rc;
}

cw::rc_t cw::wt_note::gen_notes( const char*       wtb_json_fname,
                                 unsigned          instr_idx,
                                 unsigned          min_pitch,
                                 unsigned          max_pitch,
                                 srate_t           srate,
                                 unsigned          audioChN,
                                 double            note_dur_sec,
                                 double            inter_note_sec,
                                 const char*       out_dir )
{
  rc_t              rc          = kOkRC;
  note_t*           noteA       = nullptr;
  char*             audio_fname = nullptr;
  wt_bank::handle_t wtbH;
  
  if( min_pitch > max_pitch )
  {
    rc = cwLogError(kInvalidArgRC,"The min pitch:%i is greater than the max pitch:%i.",min_pitch,max_pitch);
    goto errLabel;
  }

  if((rc = wt_bank::create(wtbH,2,wtb_json_fname)) != kOkRC )
  {
    goto errLabel;
  }

  if(!filesys::isDir(out_dir))
    filesys::makeDir(out_dir);
  
  for(unsigned midi_pitch=min_pitch; midi_pitch<=max_pitch; ++midi_pitch)
  {
    unsigned       velA[ midi::kMidiVelCnt ];
    unsigned       velCnt = 0;
    double         secs   = 0;
    const unsigned fnameN = 32;
    char           fname[fnameN+1];

    // get the sampled velocities for the instr_idx/midi_pitch
    if((rc = wt_bank::instr_pitch_velocities( wtbH, instr_idx, midi_pitch, velA, midi::kMidiVelCnt, velCnt )) != kOkRC )
    {
      goto errLabel;
    }

    // allocate the note records array
    noteA = mem::resizeZ<note_t>(noteA,velCnt);

    // fill in the note record array with one note for each sampled velocity
    for(unsigned i=0; i<velCnt; ++i)
    {
      noteA[i].instr_idx = instr_idx;
      noteA[i].pitch     = midi_pitch;
      noteA[i].velocity  = velA[i];
      noteA[i].delta_sec = note_dur_sec + inter_note_sec;
      noteA[i].dur_sec   = note_dur_sec;
      secs += note_dur_sec + inter_note_sec;
    }

    // form the audio file name
    snprintf(fname,fnameN,"%03i",midi_pitch);
    fname[fnameN] = 0;
    mem::release(audio_fname);
    
    audio_fname = filesys::makeFn(  out_dir, fname, "wav", nullptr );

    cwLogInfo("Generating notes for pitch:%i into '%s'.",midi_pitch,cwStringNullGuard(audio_fname));
        
    // generate the notes 
    if((rc = gen_notes( wtbH, noteA, velCnt, srate, audioChN, audio_fname )) != kOkRC )
    {
      rc = cwLogError(rc,"Note generation failed on instr:%i pitch:%i file:%s.",instr_idx,midi_pitch,cwStringNullGuard(audio_fname));
      goto errLabel;
    }
    
  }

errLabel:
  destroy(wtbH);
  mem::release(noteA);
  mem::release(audio_fname);
  return rc;
}

cw::rc_t cw::wt_note::test( const test::test_args_t& args )
{
  rc_t        rc             = kOkRC;
  const char* wtb_json_fname = "/home/kevin/temp/temp_5.json";
  unsigned    instr_idx      = 0;
  unsigned    min_pitch      = 60;
  unsigned    max_pitch      = 60;
  srate_t     srate          = 48000;
  unsigned    audioChN       = 2;
  double      note_dur_sec   = 9;
  double      inter_note_sec = 1;
  const char* out_dir        = "/home/kevin/temp/gen_note";
  
  rc = gen_notes( wtb_json_fname, instr_idx, min_pitch, max_pitch, srate, audioChN, note_dur_sec, inter_note_sec, out_dir);
  
  return rc;
}
  

