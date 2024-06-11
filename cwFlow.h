#ifndef cwFlowSys_h
#define cwFlowSys_h

namespace cw
{
  namespace flow
  {

    typedef handle<struct flow_str> handle_t;

    void print_abuf( const struct abuf_str* abuf );
    void print_external_device( const external_device_t* dev );
    
    // Parse the cfg's but don't yet instantiate the network.
    // Upon completion of this function the caller can 
    // query the network for configuration information which can
    // be used to setup the extern_device_t array.
    rc_t create(handle_t&          hRef,
                   const object_t*    classCfg,
                   const object_t*    networkCfg,
                   const object_t*    subnetCfg = nullptr,
                   const char*        projDir   = nullptr);

    // Instantiate the network and prepare for runtime.
    rc_t initialize( handle_t           handle,
                     external_device_t* deviceA   = nullptr,
                     unsigned           deviceN   = 0);
    
    rc_t destroy( handle_t& hRef );

    // Network cfg. information which is available following configure().
    bool     is_non_real_time( handle_t h );
    double   sample_rate(      handle_t h );
    unsigned frames_per_cycle( handle_t h );

    unsigned preset_cfg_flags( handle_t h );

    // Run one cycle of the network.
    rc_t exec_cycle( handle_t h );

    // Run the network to completion.
    rc_t exec(    handle_t h ); 

    rc_t apply_preset( handle_t h, const char* presetLabel );
    rc_t apply_dual_preset( handle_t h, const char* presetLabel_0, const char* presetLabel_1, double coeff );
    rc_t apply_preset( handle_t h, const multi_preset_selector_t& multi_preset_sel );

        
    rc_t set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool value     );
    rc_t set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int value      );
    rc_t set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value );
    rc_t set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float value    );
    rc_t set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double value   );

    rc_t get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool& valueRef     );
    rc_t get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int& valueRef      );
    rc_t get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned& valueRef );
    rc_t get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float& valueRef    );
    rc_t get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double& valueRef   );
    
    void print_class_list( handle_t h );
    void print_network( handle_t h );


    
    
  }
}


#endif
