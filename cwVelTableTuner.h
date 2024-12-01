//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace vtbl
  {

    enum
    {
      kVtMinId=2000,
      kVtDeviceSelectId,
      kVtPianoDevId,
      kVtSamplerDevId,
      kVtTableSelectId,
      kVtDefaultCheckId,
      kVtPlayVelSeqBtnId,
      kVtPitchId,  
      kVtPlayPitchSeqBtnId,
      kVtVelocityId,
      kVtMinPitchId,
      kVtMaxPitchId,
      kVtIncPitchId,
      kVtApplyBtnId,
      kVtSaveBtnId,
      kVtDuplicateBtnId,
      kVtNameStrId,
      kVtStatusId,
      
      kVtEntry0,
      kVtEntry1,
      kVtEntry2,
      kVtEntry3,
      kVtEntry4,
      kVtEntry5,
      kVtEntry6,
      kVtEntry7,
      kVtEntry8,
      kVtEntry9,
      kVtEntry10,
      kVtEntry11,
      kVtEntry12,
      kVtEntry13,
      kVtEntry14,
      kVtEntry15,
      kVtEntry16,
      kVtEntry17,
      kVtEntry18,
      kVtEntry19,
      kVtEntry20,
      kVtEntry21,
      kVtEntry22,
      kVtEntry23,
      kVtEntry24,

      kLoadOptionBaseId = 2500,

      kVtMaxId = 3000
      
    };
    
    struct vtbl_str;
    typedef handle<struct vtbl_str> handle_t;

    unsigned get_ui_id_map_count();

    const cw::ui::appIdMap_t* get_ui_id_map( unsigned panelAppId );

    
    rc_t create( handle_t&                  hRef,
                 io::handle_t               ioH,
                 midi_record_play::handle_t mrpH,
                 const char*                cfg_fname,
                 const char*                cfg_backup_dir);
    
    rc_t destroy( handle_t& hRef );


    rc_t on_ui_value( handle_t h, const io::ui_msg_t& m );

    rc_t on_ui_echo( handle_t h, const io::ui_msg_t& m );
    
    // Update the state of the player
    rc_t exec( handle_t h );


    const uint8_t* get_vel_table( handle_t h, const char* label, unsigned& velTblN_Ref );

  }
}
