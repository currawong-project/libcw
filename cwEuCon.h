#ifndef cwEuConHost_h
#define cwEuConHost_h

namespace cw
{
  namespace eucon
  {
    enum
    {
     kUdpSockUserId=1,
     kTcpSockUserId=2,
     kBaseSockUserId=3
    };
      
    typedef handle<struct eucon_str> handle_t;

    typedef struct args_str
    {
      unsigned           recvBufByteN;   // Socket receive buffer size
      const char*        mdnsIP;         // MDNS IP (always: "224.0.0.251")
      sock::portNumber_t mdnsPort;       // MDNS port (always 5353)
      unsigned           sockTimeOutMs;  // socket poll time out in milliseconds (also determines the cwEuCon update rate)
      sock::portNumber_t faderTcpPort;   // Fader TCP port (e.g. 49168)
      unsigned           maxSockN;       // maximum number of socket to allow in the socket manager
      unsigned           maxFaderBankN;  // maximum number of fader banks to support
    } args_t;

    rc_t create(  handle_t& hRef, const args_t& a );
    rc_t destroy( handle_t& hRef );
    rc_t exec(   handle_t h, unsigned sockTimeOutMs );
    rc_t test();
  }
}


#endif
  

