//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwTcpSocketTest_H
#define cwTcpSocketTest_H

namespace cw
{
  namespace net
  {
    namespace socket
    {
      rc_t test_udp( const object_t* cfg );      
      rc_t test_tcp( const object_t* cfg );      
    }

    namespace srv
    {
      rc_t test_udp_srv( const object_t* cfg );
      rc_t test_tcp_srv( const object_t* cfg );
      rc_t mdns_test();
    }
  }
}


#endif
