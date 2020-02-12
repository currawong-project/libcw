#ifndef cwEuConHost_h
#define cwEuConHost_h

namespace cw
{
  namespace net
  {
    namespace eucon
    {
      typedef handle<struct eucon_str> handle_t;

      rc_t create(  handle_t& hRef, socket::portNumber_t tcpPort, socket::portNumber_t servicePort, unsigned recvBufByteN, unsigned timeOutMs );
      rc_t destroy( handle_t& hRef );
      rc_t start(   handle_t h );
      rc_t test();
    }
  }
}


#endif
  

