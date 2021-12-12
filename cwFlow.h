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
    

    rc_t create( handle_t&             hRef,
                 const object_t&       classCfg,
                 const object_t&       networkCfg,
                 external_device_t*    deviceA = nullptr,
                 unsigned              deviceN = 0);

    // Run one cycle of the network.
    rc_t exec_cycle( handle_t& hRef );

    // Run the network to completion.
    rc_t exec(    handle_t& hRef );
    
    rc_t destroy( handle_t& hRef );

    void print_class_list( handle_t& hRef );
    void print_network( handle_t& hRef );

    rc_t test( const object_t* class_cfg, const object_t* cfg );

    
    
  }
}


#endif
