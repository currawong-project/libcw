#ifndef cwSocketDecls_H
#define cwSocketDecls_H

namespace cw
{
  namespace sock
  {
    typedef uint16_t portNumber_t;

    typedef enum
    {
     kReceiveCbId,
     kConnectCbId,
     kDisconnectCbId
    } cbId_t;


    
    enum
    {
     kNonBlockingFl   = 0x000,  // Create a non-blocking socket.
     kBlockingFl      = 0x001,  // Create a blocking socket.
     kTcpFl           = 0x002,  // Create a TCP socket rather than a UDP socket.
     kBroadcastFl     = 0x004,  //
     kReuseAddrFl     = 0x008,  //
     kReusePortFl     = 0x010,  //
     kMultiCastTtlFl  = 0x020,  //
     kMultiCastLoopFl = 0x040,  //
     kListenFl        = 0x080,  // Use this socket to listen for incoming connections
     kStreamFl        = 0x100,  // Connected stream (not Datagram)
    };

    enum
    {
     // port 0 is reserved by and is therefore a convenient invalid port number
     kInvalidPortNumber = 0 
    };
    
  }
}

#endif
