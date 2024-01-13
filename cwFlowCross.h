#ifndef cwFlowCross_h
#define cwFlowCross_h

namespace cw
{
  namespace flow_cross
  {

    typedef handle<struct flow_cross_str> handle_t;

    typedef enum
    {
      kCurDestId,   // Apply value to the current flow network 
      kNextDestId,  // Apply value to the next flow network (i.e. network which will be current following the next cross-fade)
      kAllDestId,   // Apply value to all the flow networks
    } destId_t;

    rc_t create( handle_t&                hRef,                 
                 const object_t&          classCfg,
                 const object_t&          networkCfg,
                 double                   srate,              
                 unsigned                 crossN  = 2,         // max count of active cross-fades
                 flow::external_device_t* deviceA = nullptr,
                 unsigned                 deviceN = 0);

    rc_t destroy( handle_t& hRef );

    // Run one cycle of the network.
    rc_t exec_cycle( handle_t h );
    
    rc_t apply_preset( handle_t h, destId_t destId, const char* presetLabel );
    rc_t apply_preset( handle_t h, destId_t destId, const flow::multi_preset_selector_t& multi_preset_sel );

    rc_t set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, bool value );
    rc_t set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, int value );
    rc_t set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value );
    rc_t set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, float value );
    rc_t set_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, double value );

    rc_t get_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, bool& valueRef );
    rc_t get_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, int& valueRef );
    rc_t get_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, unsigned& valueRef );
    rc_t get_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, float& valueRef );
    rc_t get_variable_value( handle_t h, destId_t destId, const char* inst_label, const char* var_label, unsigned chIdx, double& valueRef );
    
    rc_t begin_cross_fade( handle_t h, unsigned crossFadeMs );
    
    void print( handle_t h );
    void print_network( handle_t h, flow_cross::destId_t destId );
    void report( handle_t h );
    
  }
}

#endif
