#ifndef cwFlowDecl_h
#define cwFlowDecl_h

namespace cw
{
  namespace flow
  {

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

    struct external_device_str;
    
    typedef rc_t (*send_midi_triple_func_t)( struct external_device_str* dev, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 );
    
    typedef struct midi_dev_cfg_str
    {
      // msgArray[] contains the current msgs for all devices NOT just the device that this record is embedded in.
      // We do this so that the order of messages as they arrived is maintained.  Otherwise, to achieve this ordering,
      // the messages for all devices would need to be collected and sorted by time.
      const midi::ch_msg_t*   msgArray;  
      unsigned                msgCnt;

      unsigned                maxMsgCnt; // max possible value of msgCnt
      send_midi_triple_func_t sendTripleFunc;
    } midi_dev_cfg_t;

    // Generate external device record
    typedef struct external_device_str
    {
      void*       reserved;
      const char* devLabel;   // IO framework device label
      const char* portLabel;  // IO framework MIDI port label (only used by MIDI devices)
      unsigned    typeId;     // see ???DevTypeId above
      unsigned    flags;      // see ???Fl above
      
      unsigned    ioDevIdx;   // IO framework device index
      unsigned    ioPortIdx;  // IO framework MIDI port index (only used by MIDI devices)
      
      union
      {
        audio_dev_cfg_t a; // audio devices use this record
        midi_dev_cfg_t  m; // MIDI     "     "   "     " 
      } u;
        
    } external_device_t;

    
    enum {
      kPriPresetProbFl     = 0x01,
      kSecPresetProbFl     = 0x02,
      kInterpPresetFl      = 0x04,
      kAllowAllPresetFl    = 0x08,
      kDryPriorityPresetFl = 0x10,
      kDrySelectedPresetFl = 0x20,
    };
    
    typedef struct preset_order_str
    {
      const char* preset_label;
      unsigned    order;
    } preset_order_t;
    
    typedef struct multi_preset_selector_str
    {
      unsigned              flags;
      const double*         coeffV;
      const double*         coeffMinV;
      const double*         coeffMaxV;
      unsigned              coeffN;
      
      const preset_order_t* presetA;
      unsigned              presetN;
    } multi_preset_selector_t;
    
  }
}

#endif
