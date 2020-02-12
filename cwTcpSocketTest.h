#ifndef cwTcpSocketTest_H
#define cwTcpSocketTest_H

namespace cw
{
  namespace net
  {
    namespace socket
    {
      rc_t test( portNumber_t localPort, const char* remoteAddr, portNumber_t remotePort );      
      rc_t test_tcp( portNumber_t localPort, const char* remoteAddr, portNumber_t remotePort, bool dgramFl, bool serverFl );      
    }

    namespace srv
    {
      rc_t test_udp_srv( socket::portNumber_t localPort, const char* remoteAddr, socket::portNumber_t remotePort );
      rc_t test_tcp_srv( socket::portNumber_t localPort, const char* remoteAddr, socket::portNumber_t remotePort );
      rc_t mdns_test();
    }
  }
}


#endif
