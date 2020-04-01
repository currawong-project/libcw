#ifndef cwSocket_H
#define cwSocket_H

#include "cwSocketDecls.h"

namespace cw
{
  namespace sock
  {
    typedef handle< struct mgr_str > handle_t;
    
    // userId is the id assigned to the receiving socket
    // connId is an automatically assigned id which represents the remote endpoint which is connected to 'userId'.
    typedef void (*callbackFunc_t)( void* cbArg, cbOpId_t cbId, unsigned userId, unsigned connId, const void* byteA, unsigned byteN, const struct sockaddr_in* srcAddr );

    rc_t createMgr(  handle_t& hRef, unsigned recvBufByteN, unsigned maxSocketN );
    rc_t destroyMgr( handle_t& hRef );

    rc_t create( handle_t h,
      unsigned       userId,
      short          port,
      unsigned       flags,
      unsigned       timeOutMs  = 100, // time out to use with recv() on blocking sockets
      callbackFunc_t cbFunc     = nullptr,
      void*          cbArg      = nullptr,
      const char*    remoteAddr = nullptr,
      portNumber_t   remotePort = sock::kInvalidPortNumber,
      const char*    localAddr  = nullptr );

    rc_t destroy( handle_t h, unsigned userId );

    rc_t set_multicast_time_to_live( handle_t h, unsigned userId, unsigned seconds );
    
    rc_t join_multicast_group( handle_t h, unsigned userId, const char* addr );

    
    // Send to the remote endpoint represented by connId over a connected socket.
    // If 'connId' is kInvalidId then this data is sent to all connected endpoints.
    rc_t send( handle_t h, unsigned userId, unsigned connId, const void* data, unsigned dataByteN );

    // Send a message to a specific remote node over an unconnected UDP socket.
    // Use the function initAddr() to setup the 'sockaddr_in';
    rc_t send( handle_t h, unsigned userId, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr );
    rc_t send( handle_t h, unsigned userId, const void* data, unsigned dataByteCnt, const char* remoteAddr, portNumber_t port );
    
    // Set a destination address for this socket. Once a destination address is set
    // the caller may use send() to communicate with the specified remote socket
    // without having to specify a destination address on each call.
    rc_t connect( handle_t h, unsigned userId, const char* remoteAddr, portNumber_t port );

    // Return true if this socket is connected to a remote endpoint.
    bool isConnected( handle_t h, unsigned userId );

    // Blocking - Wait up to timeOutMs milliseconds for data to be available on any open sockets.
    // Deliver received data via the port callback function.
    // readN_Ref returns the total count of bytes read across all ports.
    rc_t receive_all( handle_t h, unsigned timeOutMs, unsigned& readByteN_Ref );

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
    rc_t receive(handle_t h, unsigned userId, unsigned& readByteN_Ref, void* buf=nullptr, unsigned bufByteN=0, struct sockaddr_in* fromAddr=nullptr );

    
    // Note that 
    rc_t get_mac( handle_t h, unsigned userId, unsigned char buf[6], struct sockaddr_in* addr=nullptr, const char* netInterfaceName=nullptr );

      
    const char*   hostName(    handle_t h, unsigned userId );
    const char*   ipAddress(   handle_t h, unsigned userId );
    unsigned      inetAddress( handle_t h, unsigned useId );
    portNumber_t  port(        handle_t h, unsigned userId );
    rc_t          peername(    handle_t h, unsigned userId, struct sockaddr_in* addr );

    rc_t          get_info( const char* netInterfaceName, unsigned char mac[6], char* hostBuf=nullptr, unsigned hostBufN=_POSIX_HOST_NAME_MAX, struct sockaddr_in* addr=nullptr );

    // Prepare a struct sockadddr_in for use with send()
    rc_t          initAddr( const char* addrStr, portNumber_t portNumber, struct sockaddr_in* retAddrPtr );
    
    rc_t          addrToString( const struct sockaddr_in* addr, char* buf, unsigned bufN=INET_ADDRSTRLEN );
      
    bool          addrIsEqual( const struct sockaddr_in* addr0, const struct sockaddr_in* addr1 );

  }

  //===============================================================================================
  namespace socksrv
  {
    typedef handle< struct socksrv_str > handle_t;
    
    rc_t createMgrSrv(  handle_t& hRef, unsigned timeOutMs, unsigned recvBufByteN, unsigned maxSocketN );
    rc_t destroyMgrSrv( handle_t& hRef );

    sock::handle_t mgrHandle( handle_t h );

    rc_t start( handle_t h );
    rc_t stop(  handle_t h );

    rc_t test(      const char* localNicDevice, sock::portNumber_t localPort, const char* remoteAddrIp, sock::portNumber_t remotePort, unsigned flags=0 );
    rc_t testMain(  bool tcpFl, const char* localNicDevice, sock::portNumber_t localPort, const char* remoteAddrIp=nullptr, sock::portNumber_t remotePort=sock::kInvalidPortNumber );
  }    
  
}


#endif
