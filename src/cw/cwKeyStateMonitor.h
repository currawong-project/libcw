
#ifndef cwKeyStateMonitor_h
#define cwKeyStateMonitor_h


namespace cw
{
  namespace key_state_monitor
  {
    typedef handle<struct ksm_str> handle_t;

    // If the pedal is less than pedal_thresh then the pedal is considered released (up).
    rc_t create( handle_t&    hRef,
                 const char*  cfg_fname,
                 dsp::srate_t srate,
                 uint8_t      pedal_off_max_d1 = 40,   // pedal MIDI ctl value below will consider the pedal up
                 uint8_t      pedal_on_min_d1  = 64 ); // pedal MIDI ctl value above will consier the pedal down
    
    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h, unsigned loc );

    // Set status to kInvalidStatusMdId is there is no MIDI msg to accompany the 'loc_id'.
    // Set loc_id to kInvalidId if there is no loc to accompany the MIDI msg.
    rc_t on_msg( handle_t  h,
                 unsigned  smp_idx,
                 unsigned  port_id,
                 uint8_t   ch,
                 uint8_t   status,
                 uint8_t   d0,
                 uint8_t   d1,
                 unsigned  loc_id,
                 unsigned& trig_cnt_ref );

    typedef struct {
      const char* label;
      unsigned    id;
    } trigger_id_t;

    const trigger_id_t* trigger_array( handle_t h, unsigned& trig_cnt );
  }  
}

#endif
