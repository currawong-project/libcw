#ifndef cwFlowDecl_h
#define cwFlowDecl_h

namespace cw
{
  namespace flow
  {
    typedef struct preset_order_str
    {
      const char* preset_label;
      unsigned order;
    } preset_order_t;
    
    typedef struct multi_preset_selector_str
    {
      unsigned              type_id;
      const double*         coeffV;
      unsigned              coeffN;
      const preset_order_t* presetA;
      unsigned              presetN;
    } multi_preset_selector_t;
    
  }
}

#endif
