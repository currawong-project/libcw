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

    // Are messages from the physical control or from
    // from the EuCon controller waiting to be read
    // by the client application.
    bool  areMsgsWaiting( handle_t h );

    // msg flags
    enum
    {
     kWriteValueFl = 0,
     kReadValueFl  = 1
    };

    // msg tid
    enum
    {
     kFaderValueTId = 8, // set/get a fader value
     kMuteValueTId,      // set/get a mute value
     kTouchValueTId,     // set/get a touch value
     kSendStateTId       // get the state of a channel
    };

    
    typedef struct msg_str
    {
      unsigned flags;
      unsigned channel;
      union
      {
        int   ivalue;
        float fvalue;
      } u;
      
    } msg_t;
    
    typedef void (*msgCallback_t)( void* cbArg, const msg_t* msg );
    
    // Callback with messages for the client application.
    rc_t  getMsgs( handle_t h, msgCallback_t cbFunc, void* cbArg );


    // Send a message to the EuCon manager or to a physical control.
    // Note that flags is formed by <msgTId> | <msgFlags>
    rc_t sendMsg( handle_t h, unsigned flags, unsigned channel, unsigned ivalue, float fvalue );
    
    
    rc_t test();
  }
}


#endif
  

