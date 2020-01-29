#ifndef cwTcpPort_H
#define cwTcpPort_H


namespace cw
{
  namespace net
  {
    namespace socket
    {
      typedef handle<struct socket_str> handle_t;

      typedef unsigned short portNumber_t;
      
      enum
      {
       kNonBlockingFl   = 0x000,  // create a non-blocking socket  
       kBlockingFl      = 0x001,  // create a blocking socket  
       kTcpFl           = 0x002,  // create a TCP socket rather than a UDP socket
       kBroadcastFl     = 0x004,
       kReuseAddrFl     = 0x008,
       kReusePortFl     = 0x010,
       kMultiCastTtlFl  = 0x020,
       kMultiCastLoopFl = 0x040,
       kListenFl        = 0x080,
       kStreamFl        = 0x100
      };

      enum
      {
       // port 0 is reserved by and is therefore a convenient invalid port number
       kInvalidPortNumber = 0 
      };
      
      
      rc_t create( handle_t& hRef,
        portNumber_t         port,
        unsigned             flags,
        unsigned             timeOutMs  = 100, // time out to use with recv() on blocking sockets
        const char*          remoteAddr = NULL,
        portNumber_t         remotePort = socket::kInvalidPortNumber );

      rc_t destroy( handle_t& hRef );

      rc_t set_multicast_time_to_live( handle_t h, unsigned seconds );

      rc_t join_multicast_group( handle_t h, const char* addr );

      rc_t setTimeOutMs( handle_t h, unsigned timeOutMs );

      // Listen for a connections
      rc_t accept( handle_t h );

      // Set a destination address for this socket. Once a destination address is set
      // the caller may use send() to communicate with the specified remote socket
      // without having to specify a destination address on each call.
      rc_t connect( handle_t h, const char* remoteAddr, portNumber_t port );

      // Return true if this socket is connected to a remote endpoint.
      bool isConnected( handle_t h );
      
      // Send a message to a remote UDP socket over a previously connected socket
      rc_t send( handle_t h, const void* data, unsigned dataByteCnt );
      
      // Send a message to a specific remote node over an unconnected socket.
      // Use the function initAddr() to setup the 'sockaddr_in';
      rc_t send( handle_t h, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr );
      rc_t send( handle_t h, const void* data, unsigned dataByteCnt, const char* remoteAddr, portNumber_t port );

      // Receive incoming messages by directly checking the internal
      // socket for waiting data.  This function is used to receive
      // incoming data when the internal listening thread is not used.
      // Note that if kBlockingFl was set in create() this call will
      // block for available data or for 'timeOutMs' milliseconds,
      // whichever comes first (as set in create()).  If
      // kNonBlockingFl was set in create() then the function will
      // return immediately if no incoming messages are waiting.  If
      // recvByteCntRef is valid (non-NULL) then it is set to the
      // length of the received message or 0 if no msg was received.
      rc_t recieve(handle_t h, char* data, unsigned dataByteCnt, unsigned* recvByteCntRef=nullptr, struct sockaddr_in* fromAddr=nullptr );

      // 
      rc_t select_recieve(handle_t h, char* buf, unsigned bufByteCnt, unsigned timeOutMs, unsigned* recvByteCntRef=nullptr, struct sockaddr_in* fromAddr=nullptr );

      //
      rc_t recv_from(handle_t h, char* buf, unsigned bufByteCnt, unsigned* recvByteCntRef=nullptr, struct sockaddr_in* fromAddr=nullptr );

      // Prepare a struct sockadddr_in for use with send()
      rc_t        initAddr( handle_t h, const char* addrStr, portNumber_t portNumber, struct sockaddr_in* retAddrPtr );
      
      const char* addrToString( const struct sockaddr_in* addr, char* buf, unsigned bufN=INET_ADDRSTRLEN );
      
      bool        addrIsEqual( const struct sockaddr_in* addr0, const struct sockaddr_in* addr1 );
      
      const char* hostName( handle_t h );
      

      
      
    }
  }    
}


#endif
