#ifndef cwFlowDecl_h
#define cwFlowDecl_h

namespace cw
{
  namespace flow
  {
    enum {
      kPriPresetProbFl   = 0x01,
      kSecPresetProbFl   = 0x02,
      kInterpPresetFl    = 0x04
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
