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

    // Create the EuCon simulation manager.
    rc_t create(  handle_t& hRef, const args_t& a );

    // Destroy the EuCon simulation manager.
    rc_t destroy( handle_t& hRef );

    // Update the manager. This function polls the network socket
    // for incoming information from the FaderBankArray.
    rc_t exec(   handle_t h, unsigned sockTimeOutMs );

    // Are messages waiting 
    bool  areMsgsWaiting( handle_t h );

    enum
    {
     kFaderValueTId,
     kMuteValueTId,
     kTouchValueTId
    };
    
    typedef struct msg_str
    {
      unsigned msgTId;
      unsigned channel;
      union
      {
        int   ivalue;
        float fvalue;
      } u;
      
    } msg_t;
    
    typedef void (*msgCallback_t)( void* cbArg, const msg_t* msg );
    
    // Switches the internal double buffer and calls back with the parsed messages.
    rc_t  getMsgs( handle_t h, msgCallback_t cbFunc, void* cbArg );


    // Send a message to a physical control
    rc_t sendCtlMsg( handle_t h, unsigned ctlTId, unsigned channel, unsigned ivalue, float fvalue );
    
    
    rc_t test();
  }
}


#endif
  

