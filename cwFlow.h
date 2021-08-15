#ifndef cwFlowSys_h
#define cwFlowSys_h

namespace cw
{
  namespace flow
  {


    

    typedef handle<struct flow_str> handle_t;

    rc_t create( handle_t& hRef, const object_t& classCfg, const object_t& cfg );
    rc_t exec(    handle_t& hRef );
    rc_t destroy( handle_t& hRef );

    void print_class_list( handle_t& hRef );
    void print_network( handle_t& hRef );

    rc_t test( const object_t* class_cfg, const object_t* cfg );

    
    
  }
}


#endif
