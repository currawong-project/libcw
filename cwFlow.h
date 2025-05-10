//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwFlow_h
#define cwFlow_h

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
                const object_t*    classCfg,              // processor class dictionary
                const object_t*    pgmCfg,                // top level program cfg 
                const object_t*    udpCfg  = nullptr,
                const char*        projDir = nullptr,
                ui_callback_t      ui_callback = nullptr,
                void*              ui_callback_arg = nullptr);

    // Network cfg. information which is available following create().
    bool     is_non_real_time( handle_t h );
    double   sample_rate(      handle_t h );
    unsigned frames_per_cycle( handle_t h );
    unsigned preset_cfg_flags( handle_t h );

    // Get the count and labels of the top level presets
    unsigned    preset_count( handle_t h );
    const char* preset_label( handle_t h, unsigned preset_idx );
    
    // Instantiate the network and prepare for runtime.
    // The UI is not available until after initialization.
    rc_t initialize( handle_t           handle, 
                     external_device_t* deviceA    = nullptr,
                     unsigned           deviceN    = 0,
                     unsigned           preset_idx = kInvalidIdx);
    
    rc_t destroy( handle_t& hRef );

    // The ui_net() is not available until the network has been initialized.
    const ui_net_t* ui_net( handle_t h );

    // Run one cycle of the network.
    // May return kEofRC if the cycle was programatically halted.
    rc_t exec_cycle( handle_t h );

    // Run a non-real-time program to completion.
    // May return kEofRC if the cycle was programatically halted.
    rc_t exec(    handle_t h );

    // Send any pending updates from the flow network to the UI.
    // This happens automatically if exec() exec_cycle() is called.
    // Calling this function is only necessary when the state of the
    // network is changed outside of runtime.
    rc_t send_ui_updates( handle_t h );

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


    // The 'user_id' shows up as the 'user_id' in the ui_var field.
    rc_t set_variable_user_id( handle_t h, const ui_var_t* ui_var, unsigned user_id );
    
    rc_t set_variable_value( handle_t h, const ui_var_t* ui_var, bool value     );
    rc_t set_variable_value( handle_t h, const ui_var_t* ui_var, int value      );
    rc_t set_variable_value( handle_t h, const ui_var_t* ui_var, unsigned value );
    rc_t set_variable_value( handle_t h, const ui_var_t* ui_var, float value    );
    rc_t set_variable_value( handle_t h, const ui_var_t* ui_var, double value   );
    rc_t set_variable_value( handle_t h, const ui_var_t* ui_var, const char* value );

    rc_t get_variable_value( handle_t h, const ui_var_t* ui_var, bool& value_ref     );
    rc_t get_variable_value( handle_t h, const ui_var_t* ui_var, int& value_ref      );
    rc_t get_variable_value( handle_t h, const ui_var_t* ui_var, unsigned& value_ref );
    rc_t get_variable_value( handle_t h, const ui_var_t* ui_var, float& value_ref    );
    rc_t get_variable_value( handle_t h, const ui_var_t* ui_var, double& value_ref   );
    rc_t get_variable_value( handle_t h, const ui_var_t* ui_var, const char*& value_ref );

    
    void print_class_list( handle_t h );
    void print_network( handle_t h );
    void profile_report( handle_t h );

    
    
  }
}


#endif
