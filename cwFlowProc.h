//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace flow
  {

    template< typename inst_t >
    rc_t std_destroy( proc_t* proc )
    {
      inst_t* p = (inst_t*)proc->userPtr;
      rc_t rc = _destroy(proc,p);
      mem::release(proc->userPtr);
      return rc;
    }
    
    template< typename inst_t >
    rc_t std_create( proc_t* proc )
    {
      rc_t rc = kOkRC;
      proc->userPtr = mem::allocZ<inst_t>();
      if((rc = _create(proc,(inst_t*)proc->userPtr)) != kOkRC )
        std_destroy<inst_t>(proc);
      return rc;        
    }

    template< typename inst_t >
    rc_t std_notify( proc_t* proc, variable_t* var )
    { return _notify(proc,(inst_t*)proc->userPtr, var); }
        
    template< typename inst_t >
    rc_t std_exec( proc_t* proc )
    { return _exec(proc,(inst_t*)proc->userPtr); }

    template< typename inst_t >
    rc_t std_report( proc_t* proc )
    { return _report(proc,(inst_t*)proc->userPtr); }
    
    namespace user_def_proc   { extern class_members_t members;  }
    namespace poly            { extern class_members_t members;  }
    namespace midi_in         { extern class_members_t members;  }
    namespace midi_out        { extern class_members_t members;  }
    namespace audio_in        { extern class_members_t members;  }
    namespace audio_out       { extern class_members_t members;  }
    namespace audio_file_in   { extern class_members_t members;  }
    namespace audio_file_out  { extern class_members_t members;  }
    namespace audio_buf_file_out { extern class_members_t members; }
    namespace audio_gain      { extern class_members_t members;  }
    namespace audio_xfade     { extern class_members_t members;  }
    namespace audio_split     { extern class_members_t members;  }
    namespace audio_merge     { extern class_members_t members;  }
    namespace audio_duplicate { extern class_members_t members;  }
    namespace audio_mix       { extern class_members_t members;  }
    namespace audio_marker    { extern class_members_t members;  }
    namespace audio_silence   { extern class_members_t members;  }
    namespace sine_tone       { extern class_members_t members;  }
    namespace pv_analysis     { extern class_members_t members;  }
    namespace pv_synthesis    { extern class_members_t members;  }
    namespace spec_dist       { extern class_members_t members;  }
    namespace compressor      { extern class_members_t members;  }
    namespace limiter         { extern class_members_t members;  }
    namespace audio_delay     { extern class_members_t members;  }
    namespace dc_filter       { extern class_members_t members;  }
    namespace balance         { extern class_members_t members;  }
    namespace audio_meter     { extern class_members_t members;  }
    namespace audio_marker    { extern class_members_t members;  }
    namespace xfade_ctl       { extern class_members_t members;  }
    namespace midi_voice      { extern class_members_t members;  }
    namespace piano_voice     { extern class_members_t members;  }
    namespace voice_detector  { extern class_members_t members;  }
    namespace poly_voice_ctl  { extern class_members_t members;  }
    namespace sample_hold     { extern class_members_t members;  }
    namespace number          { extern class_members_t members;  }
    namespace label_value_list { extern class_members_t members;  }
    namespace string_list     { extern class_members_t members;  }
    namespace reg             { extern class_members_t members;  }
    namespace timer           { extern class_members_t members;  }
    namespace counter         { extern class_members_t members;  }
    namespace list            { extern class_members_t members;  }
    namespace add             { extern class_members_t members;  }
    namespace preset          { extern class_members_t members;  }
    namespace print           { extern class_members_t members;  }
    namespace on_start        { extern class_members_t members;  }
    namespace halt            { extern class_members_t members;  }
    namespace midi_msg        { extern class_members_t members;  }    
    namespace make_midi       { extern class_members_t members;  }
    namespace midi_select     { extern class_members_t members;  }
    namespace midi_split      { extern class_members_t members;  }
    namespace midi_file       { extern class_members_t members;  }
    namespace recd_route      { extern class_members_t members;  }
    namespace recd_merge      { extern class_members_t members;  }
    namespace recd_extract    { extern class_members_t members;  }
    namespace midi_merge      { extern class_members_t members;  }
    namespace poly_xform_ctl  { extern class_members_t members;  }
    namespace gutim_ps_msg_table { extern class_members_t members; }
    namespace gutim_take_menu  { extern class_members_t members; }
    namespace score_player_ctl { extern class_members_t members; }
    namespace midi_recorder    { extern class_members_t members; }
    namespace button_array     { extern class_members_t members; }
  }
}
