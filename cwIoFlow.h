#ifndef cwIoFlow_h
#define cwIoFlow_h

namespace cw
{
  namespace io_flow
  {
    typedef handle< struct io_flow_str > handle_t;

    rc_t create( handle_t& hRef, io::handle_t ioH, double srate, unsigned crossFadeCnt, const object_t& flow_class_dict, const object_t& cfg );
    rc_t destroy( handle_t& hRef );

    rc_t exec( handle_t h, const io::msg_t& msg );


    // Apply a preset to the 'next' network instance and activate it immediately.
    rc_t apply_preset( handle_t h, unsigned crossFadeMs, const char* presetLabel );
    
    // Apply a preset to the selected network instance (current or next).
    // If the preset is applied to the 'next' network instance then use 'begin_cross_fade()' to
    // activate the next network.
    rc_t apply_preset( handle_t h, flow_cross::destId_t destId, const char* presetLabel );

    rc_t set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, bool value );
    rc_t set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, int value );
    rc_t set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value );
    rc_t set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, float value );
    rc_t set_variable_value( handle_t h, flow_cross::destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, double value );

    rc_t begin_cross_fade( handle_t h, unsigned crossFadeMs );
    

  }
}

#endif
