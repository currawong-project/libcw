#ifndef cwFlowCross_h
#define cwFlowCross_h

namespace cw
{
  namespace flow_cross
  {

    typedef handle<struct flow_cross_str> handle_t;

    rc_t create( handle_t&                hRef,                 
                 const object_t&          classCfg,
                 const object_t&          networkCfg,
                 double                   srate,              
                 unsigned                 crossN  = 2,         // max count of active cross-fades
                 flow::external_device_t* deviceA = nullptr,
                 unsigned                 deviceN = 0);

    rc_t destroy( handle_t& hRef );

    // Run one cycle of the network.
    rc_t exec_cycle( handle_t h );
    
    rc_t apply_preset( handle_t h, unsigned crossFadeMs, const char* presetLabel );

    void print( handle_t h );
    
  }
}

#endif
