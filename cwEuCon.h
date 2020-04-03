#ifndef cwEuConHost_h
#define cwEuConHost_h

namespace cw
{
  namespace net
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
        unsigned           recvBufByteN;
        const char*        mdnsIP;
        sock::portNumber_t mdnsPort;
        unsigned           sockTimeOutMs;
        sock::portNumber_t tcpPort;
        unsigned           maxSockN;
        unsigned           maxFaderBankN;
      } args_t;

      rc_t create(  handle_t& hRef, const args_t& a );
      rc_t destroy( handle_t& hRef );
      rc_t exec(   handle_t h, unsigned sockTimeOutMs );
      rc_t test();
    }
  }
}


#endif
  

