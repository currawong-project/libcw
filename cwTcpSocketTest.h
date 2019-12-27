#ifndef cwTcpSocketTest_H
#define cwTcpSocketTest_H

namespace cw
{
  namespace net
  {
    namespace socket
    {
      rc_t test( portNumber_t localPort, const char* remoteAddr, portNumber_t remotePort );      
    }

    namespace srv
    {
      rc_t test( socket::portNumber_t localPort, const char* remoteAddr, socket::portNumber_t remotePort );      
    }
  }
}


#endif
