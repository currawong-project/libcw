#ifndef cwTcpSocketSrv_H
#define cwTcpSocketSrv_H

namespace cw
{
  namespace net
  {
    namespace srv
    {
      typedef void (*cbFunc_t)( void* cbArg, const void* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr ); 
      typedef handle< struct socksrv_str > handle_t;

      enum
      {
       kUseAcceptFl     = 0x01,  // wait for a connection
       kUseRecvFromFl   = 0x02,  // use socket::recv_from
       kRecvTimeOutFl   = 0x04,  // Generate empty receive callbacks on receive timeouts
      };
      
      rc_t create( handle_t& hRef,                 // 
        socket::portNumber_t port,                 // local port number
        unsigned             flags,                // see socket::flags
        unsigned             srvFlags,             // 
        cbFunc_t             cbFunc,               // callback for received messages
        void*                cbArg,                // callback arg
        unsigned             recvBufByteCnt = 1024,// recieve buffer size
        unsigned             timeOutMs      = 100, // time out to use with recv() on thread select()
        const char*          remoteAddr     = NULL,
        socket::portNumber_t remotePort     = socket::kInvalidPortNumber,
        const char*          localAddr      = NULL);

      rc_t destroy( handle_t& hRef );

      thread::handle_t threadHandle( handle_t h );
      socket::handle_t socketHandle( handle_t h );

      rc_t start( handle_t h );
      rc_t pause( handle_t h );
        

      // Send a message to a remote UDP socket over a previously connected socket
      rc_t send( handle_t h, const void* data, unsigned dataByteCnt );
      
      // Send a message to a specific remote node over an unconnected socket.
      // Use the function initAddr() to setup the 'sockaddr_in';
      rc_t send( handle_t h, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr );
      rc_t send( handle_t h, const void* data, unsigned dataByteCnt, const char* remoteAddr, socket::portNumber_t port );
      
    }
  }
}

#endif
