#ifndef cwFlowSys_h
#define cwFlowSys_h

namespace cw
{
  namespace flow
  {

    typedef handle<struct flow_str> handle_t;

    enum
    {
      kAudioDevTypeId,
      kMidiDevTypeId,
      kSerialDevTypeId,
      kSocketDevTypeId
    };

    enum
    {
      kInFl  = 0x01,
      kOutFl = 0x02
    };


    struct abuf_str;

    typedef struct audio_dev_cfg_str
    {
      struct abuf_str* abuf;  // Buffer to receive incoming or send outgoing audio for this device
                              // The audio_in/audio_out proc's locate and use these buffers.
    } audio_dev_cfg_t;

    // Generate external device record
    typedef struct external_device_str
    {
      const char* label;   // IO framework device label
      unsigned    ioDevId; // IO framework device id
      unsigned    typeId;  // see ???DevTypeId above
      unsigned    flags;   // see ???Fl above
      
      union
      {
        audio_dev_cfg_t a; // audio devices include this additional record
      } u;
        
    } external_device_t;

    void print_abuf( const struct abuf_str* abuf );
    void print_external_device( const external_device_t* dev );
    

    rc_t create( handle_t&             hRef,
                 const object_t&       classCfg,
                 const object_t&       networkCfg,
                 external_device_t*    deviceA = nullptr,
                 unsigned              deviceN = 0);

    rc_t destroy( handle_t& hRef );


    // Run one cycle of the network.
    rc_t exec_cycle( handle_t h );

    // Run the network to completion.
    rc_t exec(    handle_t h );

    rc_t apply_preset( handle_t h, const char* presetLabel );
        
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

    rc_t test( const object_t* class_cfg, const object_t* cfg );

    
    
  }
}


#endif
