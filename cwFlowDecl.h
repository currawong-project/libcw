//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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

    
    typedef struct ui_preset_str
    {
      const char* label;
      unsigned    preset_idx;
    } ui_preset_t;

    typedef struct ui_proc_desc_str
    {
      const char*  label;     // class label
      ui_preset_t* presetA;   // presetA[ presetN ]
      unsigned     presetN;      
    } ui_proc_desc_t;

    struct ui_proc_str;
    
    typedef struct ui_var_str
    {
      struct ui_proc_str* ui_proc; // owning proc
      
      const char* label;         // flow::variable_t::label
      unsigned    label_sfx_id;  // flow::variable_t::label_sfx_id

      const       object_t* desc_cfg;     // var desc cfg from flow::var_desc_t
      unsigned              desc_flags;   // flow::var_desc_t::flags

      bool        has_source_fl;  // true if this var is connected to a source var
      unsigned    value_tid;      // flow::variable_t::type
      unsigned    vid;            // flow::variable_t::vid
      unsigned    ch_idx;         // flow::variable_t::chIdx
      unsigned    ch_cnt;         // 0=kAnyChIdx only, kInvalidCnt=no channels, 1=mono, 2=stereo, ...

      unsigned user_id; // uuId of the UI element that represents this var
    } ui_var_t;

    struct proc_str;
    
    typedef struct ui_proc_str
    {
      const struct ui_net_str* ui_net;

      struct proc_str* proc;
      
      const ui_proc_desc_t* desc; 
      const object_t*       cfg;  // complete proc inst. cfg
      
      const char* label;        // flow::proc_t::label
      unsigned    label_sfx_id; // 
      
      ui_var_t*   varA;       // varA[varN] 
      unsigned    varN;       //

      struct ui_net_str* internal_net;
      
    } ui_proc_t;

    struct network_str;
    
    typedef struct ui_net_str
    {
      struct network_str* net;
      
      ui_proc_t* procA;  // procA[procN]
      unsigned   procN;

      ui_preset_t* presetA;  // presetA[presetN] network presets
      unsigned     presetN;

      struct ui_net_str* poly_link;  
      unsigned           poly_idx;
      
    } ui_net_t;

    typedef rc_t (*ui_callback_t)( void* arg, const ui_var_t* ui_var );

    
  }
}

#endif
