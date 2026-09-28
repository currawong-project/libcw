#ifndef cwGutimMeas_h
#define cwGutimMeas_h

namespace cw
{
  namespace gutim_meas
  {
    typedef handle<struct gutim_meas_str> handle_t;

    rc_t create( handle_t& hRef, const char* group_info_json_fname );
    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h );

    rc_t on_note( handle_t h, unsigned loc_id, double sec, unsigned midi_pitch, unsigned midi_vel );

    bool is_section_complete( handle_t h );

    typedef struct
    {
      double chord_eval_fl;
      double chord_spread;
    } results_t;

    rc_t get_results( handle_t h, results_t& results_ref );
    
  }
}


#endif


