#ifndef cwIoFlow_h
#define cwIoFlow_h

namespace cw
{
  namespace io_flow
  {
    typedef handle< struct io_flow_str > handle_t;

    rc_t create( handle_t& hRef, io::handle_t ioH, const object_t& flow_class_dict, const object_t& cfg );
    rc_t destroy( handle_t& hRef );
    
    rc_t exec( handle_t h, const io::msg_t& msg );
    

  }
}

#endif
