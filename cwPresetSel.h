#ifndef cwPresetSel_h
#define cwPresetSel_h


namespace cw
{
  namespace preset_sel
  {
    typedef handle< struct preset_sel_str > handle_t;

    typedef struct preset_str
    {
      bool     playFl;      // play this preset
      unsigned preset_idx;  // preset index into preset_labelA[].
      unsigned order;       //
    } preset_t;

    typedef struct frag_str
    {
      unsigned         fragId;      // Unique fragment id 
      unsigned         guiUuId;     // GUI UUId associated with this fragment
      unsigned         endLoc;      // The endLoc is included in this fragment. The begin loc is f->prev->endLoc+1
      time::spec_t     endTimestamp;
      
      double           gain;
      double           wetDryGain;
      double           fadeOutMs;

      preset_t*        presetA;
      unsigned         presetN;

      bool             uiSelectFl;

      struct frag_str* link;
      struct frag_str* prev;
    } frag_t;


    enum {
      kGuiUuIdVarId,
      kBegLocVarId,
      kEndLocVarId,
      kGainVarId,
      kFadeOutMsVarId,
      kWetGainVarId,
      
      kPresetOrderVarId,  //  preset order value
      kPresetSelectVarId, //  select a preset to play
      kPlayEnableVarId,   //  include in the segment to play
      kDryFlVarId,        //  play this fragment dry
    };
    
    rc_t create(  handle_t& hRef, const object_t* cfg  );
    rc_t destroy( handle_t& hRef );

    unsigned    preset_count( handle_t h );
    const char* preset_label( handle_t h, unsigned preset_idx );
    
    unsigned      fragment_count(    handle_t h );
    const frag_t* get_fragment_base( handle_t h );
    const frag_t* get_fragment(      handle_t h, unsigned fragId );
    const frag_t* gui_id_to_fragment(handle_t h, unsigned guiUuId );
    
    rc_t create_fragment( handle_t h, unsigned end_loc, time::spec_t endTimestamp, unsigned& fragIdRef );
    rc_t delete_fragment( handle_t h, unsigned fragId );

    bool is_fragment_loc( handle_t h, unsigned loc );
    
    unsigned ui_select_fragment_id( handle_t h );
    void     ui_select_fragment(    handle_t h, unsigned fragId, bool selectFl );
    

    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool     value );
    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned value );
    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double   value );

    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool&     valueRef );
    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned& valueRef );
    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double&   valueRef );

    // Call this function to determine which fragment the timestamp 'ts' is contained by.
    // This call is optimized to be called in time critical functions where 'ts' is expected to be increasing.
    // If 'ts' is past the last defined fragment then the last fragment is returned.
    // If no fragments are defined 'frag_Ref' is set to nullptr.
    // The return value is true when the value of frag_Ref changes from the previous call.
    bool track_timestamp( handle_t h, const time::spec_t& ts, const cw::preset_sel::frag_t*& frag_Ref );

    // Return the preset index marked to play on this fragment.
    unsigned fragment_play_preset_index( const frag_t* frag );
    
    rc_t write( handle_t h, const char* fn );
    rc_t read(  handle_t h, const char* fn );
    rc_t report( handle_t h );
    
  }
}


#endif
