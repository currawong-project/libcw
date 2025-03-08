//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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
      bool     seqFl;       // play this preset during sequencing.
      unsigned preset_idx;  // preset index into preset_labelA[].
      unsigned order;       // selection label
      char*    alt_str;     // 'alt' label
      unsigned prob_dom_idx; // index of this preset in probDomA[]
    } preset_t;

    typedef struct prob_domain_str
    {
      unsigned index;   // index of preset into frag.presetA[]
      unsigned order;   // preset order value or 0 if the playFl is set on presetA[index] and presetA[index].order==0
      unsigned domain;  // probability domain area (greater for more likely preset values)
    } prob_domain_t;

    typedef struct frag_str
    {
      unsigned         fragId;      // Unique fragment id 
      unsigned         guiUuId;     // GUI UUId associated with this fragment
      unsigned         endLoc;      // The endLoc is included in this fragment. The begin loc is f->prev->endLoc+1
      time::spec_t     endTimestamp;
      
      double           igain;
      double           ogain;
      double           wetDryGain;
      double           fadeOutMs;
      unsigned         begPlayLoc;
      unsigned         endPlayLoc;
      char*            note;

      bool             dryOnlyFl;      // there is one active preset and it is dry
      bool             drySelectedFl;  // the dry preset was selected ('playFl' is set)
      
      preset_t*        presetA;  // presetA[ presetN ] - status of each preset
      unsigned         presetN;

      // altPresetIdxA[ alt_count() ] selected preset idx for each alt.
      unsigned*        altPresetIdxA;  

      bool             uiSelectFl;
      bool             seqAllFl; // Set if all preset.seqFl's should be treated as though they are set to true.

      prob_domain_t* probDomA;  // probDomA[ probDomN ] ascending order on 'order' - preset with playFl set is always first
      unsigned       probDomN;   
      unsigned       probDomainMax; // sum(probDomA.domain)
      
      struct frag_str* link;
      struct frag_str* prev;
    } frag_t;

    enum {
      kGuiUuIdVarId,
      kBegLocVarId,
      kEndLocVarId,
      kInGainVarId,
      kOutGainVarId,
      kFadeOutMsVarId,
      kWetGainVarId,
      kBegPlayLocVarId,
      kEndPlayLocVarId,
      kPlayBtnVarId,
      kPlaySeqBtnVarId,
      kPlaySeqAllBtnVarId,
      kNoteVarId,
      
      kPresetOrderVarId,     //  preset order number
      kPresetAltVarId,       //  preset alternative string
      kPresetSelectVarId,    //  select a preset to play (play flag)
      kPresetSeqSelectVarId, //  sequence preset selections to play (seq flag)

      kBaseMasterVarId,       // All 'master' variables have id's greater than kBaseMasterVarId
      kMasterWetInGainVarId,
      kMasterWetOutGainVarId,
      kMasterDryGainVarId,
      kMasterSyncDelayMsVarId
    };
    
    rc_t create(  handle_t& hRef, const object_t* cfg  );
    rc_t destroy( handle_t& hRef );

    unsigned    preset_count( handle_t h );
    const char* preset_label( handle_t h, unsigned preset_idx );

    // Return preset_order[ preset_count() ] w/ all order's = 1
    const flow::preset_order_t* preset_order_array( handle_t h );

    // Count/label of alternatives (alt_idx==0 is 'no alternative selected)
    unsigned    alt_count( handle_t h );
    const char* alt_label( handle_t h, unsigned alt_idx );

    void get_loc_range( handle_t h, unsigned& minLocRef, unsigned& maxLocRef );
        
    unsigned      fragment_count(    handle_t h );
    const frag_t* get_fragment_base( handle_t h );
    const frag_t* get_fragment(      handle_t h, unsigned fragId );
    const frag_t* gui_id_to_fragment(handle_t h, unsigned guiUuId );

    unsigned frag_to_gui_id( handle_t h, unsigned fragId, bool showErrorFl=true );
    unsigned gui_to_frag_id( handle_t h, unsigned guiUuId, bool showErrorFl=true );
    unsigned loc_to_gui_id(  handle_t h, unsigned loc );
    
    rc_t create_fragment( handle_t h, unsigned end_loc, time::spec_t endTimestamp, unsigned& fragIdRef );
    rc_t delete_fragment( handle_t h, unsigned fragId );

    bool is_fragment_end_loc( handle_t h, unsigned loc );

    rc_t set_alternative( handle_t h, unsigned alt_idx );

    // Return the fragment id of the 'selected' fragment.
    unsigned ui_select_fragment_id( handle_t h );

    // Set the 'select_flag' on this fragment and remove it from all others.
    void     ui_select_fragment(    handle_t h, unsigned fragId, bool selectFl );
    

    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool     value );
    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned value );
    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double   value );
    rc_t set_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, const char*   value );

    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, bool&        valueRef );
    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, unsigned&    valueRef );
    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, double&      valueRef );
    rc_t get_value( handle_t h, unsigned fragId, unsigned varId, unsigned presetId, const char*& valueRef );

    // Call this function to determine which fragment the timestamp 'ts' is contained by.
    // This function is optimized to be called in time sensitive functions where 'ts' is expected to be increasing.
    // If 'ts' is past the last defined fragment then the last fragment is returned.
    // If no fragments are defined 'frag_Ref' is set to nullptr.
    // The return value is true when the value of frag_Ref changes from the previous call.
    void track_timestamp_reset( handle_t h );
    bool track_timestamp( handle_t h, const time::spec_t& ts, const cw::preset_sel::frag_t*& frag_Ref );

    // Same as track_timestamp_???() but tracks the score 'loc' instead of timestamp.
    void track_loc_reset( handle_t h );
    bool track_loc( handle_t h, unsigned loc, const cw::preset_sel::frag_t*& frag_Ref );

    // Return the preset index marked to play on this fragment.
    unsigned fragment_play_preset_index( handle_t h, const frag_t* frag, unsigned preset_seq_idx=kInvalidIdx );

    // Return the count of presets whose 'seqFl' is set.
    unsigned fragment_seq_count( handle_t h, unsigned fragId );

    enum {
      kAllActiveFl    = 0x01,
      kDryPriorityFl  = 0x02,
      kDrySelectedFl  = 0x04
    };
    
    const flow::preset_order_t*  fragment_active_presets( handle_t h, const frag_t* f, unsigned flags, unsigned& count_ref );

    enum {
      kUseProbFl   = 0x01, // True=Select the preset probalistically. False=Select the preset with the lowest non-zero order.  
      kUniformFl   = 0x02, // Ignored if kUseProbFl is not set. True=Use uniform PDF to select preset. False=Use 'order' weightings to select preset.
      kDryOnPlayFl = 0x04, // Ignored if kUseProbFl is not set. True=Select 'dry' if marked with 'play-fl'. False=Choose probabilistically.
      kAllowAllFl  = 0x08, // Ignored if kUseProbFl is not set. True=Select from all presets. False=Select from presets with order>0 or play_fl set.
      kDryOnSelFl  = 0x10, // Ignored if kUseProbFl and kUniformFl is not set. True=Select 'dry' if dry order>0 or play_fl set. Otherwise choose with uniform prob.
    };
    
    unsigned prob_select_preset_index( handle_t h,
                                       const frag_t* f,
                                       unsigned flags,
                                       unsigned skip_preset_idx = kInvalidIdx );
                                       
    
    rc_t write( handle_t h, const char* fn );
    rc_t read(  handle_t h, const char* fn );
    rc_t report( handle_t h );
    rc_t report_presets( handle_t h );

    rc_t translate_frags( const object_t* obj );
    
  }
}


#endif
