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
      unsigned order;
    } preset_t;

    typedef struct frag_str
    {
      unsigned         fragId;
      unsigned         endLoc;
      
      bool             dryFl;
      double           gain;
      double           wetDryGain;
      double           fadeOutMs;

      preset_t*        presetA;
      unsigned         presetN;
      
      struct frag_str* link;
    } frag_t;


    enum {
      kEndLocVarId,
      kGainVarId,
      kFadeOutMsVarId,
      kWetGainVarId,
      
      kPresetOrderVarId,  //  preset order value
      kPresetSelectVarId,   //  select a preset to play
      kPlayEnableVarId,   // include in the segment to play
      kDryFlVarId,        // play this fragion dry
    };
    
    rc_t create(  handle_t& hRef, const object_t* cfg  );
    rc_t destroy( handle_t& hRef );

    unsigned    preset_count( handle_t h );
    const char* preset_label( handle_t h, unsigned preset_idx );
    
    unsigned      fragment_count( handle_t h );
    const frag_t* get_fragment_base( handle_t h );
    const frag_t* get_fragment( handle_t h, unsigned fragId );

    
    rc_t create_fragment( handle_t h, unsigned fragId, unsigned end_loc );
    rc_t delete_fragment( handle_t h, unsigned fragId );

    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool     value );
    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned value );
    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double   value );

    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool&     valueRef );
    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned& valueRef );
    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double&   valueRef );
    
    rc_t write( handle_t h, const char* fn );
    rc_t read(  handle_t h, const char* fn );
    
  }
}


#endif
