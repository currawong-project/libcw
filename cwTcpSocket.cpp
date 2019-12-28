#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"

#include <sys/socket.h> 
#include <netinet/in.h>	
#include <arpa/inet.h>	
#include <fcntl.h>		
#include <unistd.h>  // close

#include "cwTcpSocket.h"

#define cwSOCKET_SYS_ERR     (-1)
#define cwSOCKET_NULL_SOCK   (-1)

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX  _POSIX_HOST_NAME_MAX
#endif


namespace cw
{
  namespace net
  {
    namespace socket
    {
      enum
      {
       kIsConnectedFl = 0x01,
       kIsBlockingFl  = 0x02
      };
      
      typedef struct socket_str
      {
        int                sockH;
        unsigned           flags;
        unsigned           recvBufByteCnt;
        struct sockaddr_in sockaddr;
        char               ntopBuf[ INET_ADDRSTRLEN+1 ]; // use INET6_ADDRSTRLEN for IPv6
        char               hnameBuf[ HOST_NAME_MAX+1 ];
      } socket_t;

      inline socket_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,socket_t>(h); }

      rc_t _destroy( socket_t* p )
      {
        // close the socket		
        if( p->sockH != cwSOCKET_NULL_SOCK )
        {
          errno = 0;
          
          if( ::close(p->sockH) != 0 )
            cwLogSysError(kOpFailRC,errno,"The socket close failed." );
			
          p->sockH = cwSOCKET_NULL_SOCK;		
        }
        
        mem::release(p); 
        return kOkRC;
      }


      rc_t _initAddr( socket_t* p, const char* addrStr, portNumber_t portNumber, struct sockaddr_in* retAddrPtr )
      {
        memset(retAddrPtr,0,sizeof(struct sockaddr_in));

        if( portNumber == kInvalidPortNumber )
          return cwLogError(kInvalidArgRC,"The port number %i cannot be used.",kInvalidPortNumber);
	
        if( addrStr == NULL )
          retAddrPtr->sin_addr.s_addr 	= htonl(INADDR_ANY);
        else
        {
          errno = 0;

          if(inet_pton(AF_INET,addrStr,&retAddrPtr->sin_addr) == 0 )
            return cwLogSysError(kOpFailRC,errno, "The network address string '%s' could not be converted to a netword address structure.",cwStringNullGuard(addrStr) );
        }
	
        //retAddrPtr->sin_len 			= sizeof(struct sockaddr_in);
        retAddrPtr->sin_family 		= AF_INET;
        retAddrPtr->sin_port 			= htons(portNumber);
	
        return kOkRC;
      }

      rc_t _connect( socket_t* p, const char* remoteAddr, portNumber_t remotePort )
      {
        struct sockaddr_in addr;
        rc_t               rc;

        // create the remote address		
        if((rc = _initAddr(p, remoteAddr, remotePort,  &addr )) != kOkRC )
          return rc;
		
        errno = 0;

        // ... and connect this socket to the remote address/port
        if( connect(p->sockH, (struct sockaddr*)&addr, sizeof(addr)) == cwSOCKET_SYS_ERR )
          return cwLogSysError(kOpFailRC, errno, "Socket connect failed." );

        p->flags = cwSetFlag(p->flags,kIsConnectedFl);

        return rc;
      }
      
    }
  }    
}


cw::rc_t cw::net::socket::create(
  handle_t&    hRef,
  portNumber_t port,
  unsigned     flags,
  unsigned     timeOutMs,
  const char*  remoteAddr,
  portNumber_t remotePort )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  socket_t* p = mem::allocZ<socket_t>();
  p->sockH = cwSOCKET_NULL_SOCK;

  // get a handle to the socket
  if(( p->sockH = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC, errno, "Socket create failed." );	 
	
  // create the local address		
  if((rc = _initAddr(p, NULL, port,  &p->sockaddr )) != kOkRC )
    goto errLabel;
			
  // bind the socket to a local address/port	
  if( (bind( p->sockH, (struct sockaddr*)&p->sockaddr, sizeof(p->sockaddr))) == cwSOCKET_SYS_ERR )
  {
    rc = cwLogSysError(kOpFailRC,errno,"Socket bind failed." );
    goto errLabel;
  }

  // if a remote addr was given connect this socket to it
  if( remoteAddr != NULL )
    if((rc = _connect(p,remoteAddr,remotePort)) != kOkRC )
      goto errLabel;

  // if this socket should block
  if( cwIsFlag(flags,kBlockingFl) )
  {
    struct timeval 		timeOut;
    
    // set the socket time out 
    timeOut.tv_sec 	= timeOutMs/1000;
    timeOut.tv_usec = (timeOutMs - (timeOut.tv_sec * 1000)) * 1000;
		
    if( setsockopt( p->sockH, SOL_SOCKET, SO_RCVTIMEO, &timeOut, sizeof(timeOut) ) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket timeout failed." );
      goto errLabel;
    }

    p->flags = cwSetFlag(p->flags,kIsBlockingFl);

  }
  else
  {
    int opts;
		
    // get the socket options flags
    if( (opts = fcntl(p->sockH,F_GETFL)) < 0 )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to get the socket options flags failed." );
      goto errLabel;
    }
	    
    opts = (opts | O_NONBLOCK);
		
    // set the socket options flags
    if(fcntl(p->sockH,F_SETFL,opts) < 0) 
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket to non-blocking failed." );
      goto errLabel;
    }	    

  }

  // if broadcast option was requested.
  if( cwIsFlag(flags,kBroadcastFl) )
  {
    int bcastFl = 1;
    if( setsockopt( p->sockH, SOL_SOCKET, SO_BROADCAST, &bcastFl, sizeof(bcastFl) ) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket broadcast attribute failed." );
      goto errLabel;
    }
  }

 errLabel:
  if(rc != kOkRC )
    _destroy(p);
  else
    hRef.set(p);

  return rc;
}
  
cw::rc_t cw::net::socket::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  socket_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;
}

cw::rc_t cw::net::socket::connect( handle_t h, const char* remoteAddr, portNumber_t remotePort )
{
  socket_t* p = _handleToPtr(h);
  return _connect(p,remoteAddr,remotePort);  
}
      
cw::rc_t cw::net::socket::send( handle_t h, const void* data, unsigned dataByteCnt )
{
  socket_t* p = _handleToPtr(h);
  errno = 0;
  
  if( cwIsFlag(p->flags,kIsConnectedFl) == false )
    return cwLogError(kInvalidOpRC,"cmUdpSend() only works with connected sockets.");

  if( ::send( p->sockH, data, dataByteCnt, 0 ) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC,errno,"Send failed.");

  return kOkRC;    
}
      
cw::rc_t cw::net::socket::send( handle_t h, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr )
{
  socket_t* p = _handleToPtr(h);
  
  errno = 0;
   
  if( sendto(p->sockH, data, dataByteCnt, 0, (struct sockaddr*)remoteAddr, sizeof(*remoteAddr)) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC,errno,"Send to remote addr. failed.");

  return kOkRC;
}

cw::rc_t cw::net::socket::send( handle_t h, const void* data, unsigned dataByteCnt, const char* remoteAddr, portNumber_t remotePort )
{
  rc_t               rc;
  socket_t*          p = _handleToPtr(h);
  struct sockaddr_in addr;

  if((rc = _initAddr(p,remoteAddr,remotePort,&addr)) != kOkRC )
    return rc;
  
  return send( h, data, dataByteCnt, &addr );
}

cw::rc_t cw::net::socket::recieve( handle_t h, char* data, unsigned dataByteCnt, unsigned* recvByteCntRef, struct sockaddr_in* fromAddr )
{
  socket_t* p                = _handleToPtr(h);
  rc_t      rc               = kOkRC;
  ssize_t   retVal           = 0;
	socklen_t sizeOfRemoteAddr = fromAddr==NULL ? 0 : sizeof(struct sockaddr_in);
	
  errno = 0;

  if( recvByteCntRef != NULL )
    *recvByteCntRef = 0;

	if((retVal = recvfrom(p->sockH, data, dataByteCnt, 0, (struct sockaddr*)fromAddr, &sizeOfRemoteAddr )) == cwSOCKET_SYS_ERR )
      return errno == EAGAIN ? kTimeOutRC : cwLogSysError(kOpFailRC,errno,"recvfrom() failed.");
	
  if( recvByteCntRef != NULL )
    *recvByteCntRef = retVal;

  return rc;  
}

cw::rc_t cw::net::socket::select_recieve(handle_t h, char* buf, unsigned bufByteCnt, unsigned timeOutMs, unsigned* recvByteCntRef, struct sockaddr_in* fromAddr )
{
  rc_t           rc = kOkRC;
  socket_t*      p  = _handleToPtr(h);
  fd_set         rdSet;
  struct timeval timeOut;

  // setup the select() call
  FD_ZERO(&rdSet);
  FD_SET(p->sockH, &rdSet );
		
  timeOut.tv_sec 	= timeOutMs/1000;
  timeOut.tv_usec = (timeOutMs - (timeOut.tv_sec * 1000)) * 1000;

  if( recvByteCntRef != nullptr )
    *recvByteCntRef   = 0;
  
  // NOTE; select() takes the highest socket value plus one of all the sockets in all the sets.
		
  switch( select(p->sockH+1,&rdSet,NULL,NULL,&timeOut) )
  {
    case -1:  // error
      if( errno != EINTR )
        cwLogSysError(kOpFailRC,errno,"Select failed.");
      break;
			
    case 0:   // select() timed out	
      break;
			
    case 1:   // (> 0) count of ready descripters
      if( FD_ISSET(p->sockH,&rdSet) )
      {
        socklen_t addrByteCnt = fromAddr==nullptr ? 0 : sizeof(*fromAddr);
        ssize_t   retByteCnt;

        errno = 0;

        // recv the incoming msg into buf[]
        if(( retByteCnt = recvfrom( p->sockH, buf, bufByteCnt, 0, (struct sockaddr*)fromAddr, &addrByteCnt )) == cwSOCKET_SYS_ERR )
          rc = cwLogSysError(kOpFailRC,errno,"recvfrom() failed.");
        else
        {
          // check for overflow
          if( retByteCnt == bufByteCnt )
            rc = cwLogError(kBufTooSmallRC,"The receive buffer requires more than %i bytes.",bufByteCnt);

          if( recvByteCntRef != nullptr )
            *recvByteCntRef = retByteCnt;          
        }					
      }	
      break;
			
    default:
      { cwAssert(0); }
  } // switch

  return rc;
}


cw::rc_t cw::net::socket::initAddr( handle_t h, const char* addrStr, portNumber_t portNumber, struct sockaddr_in* retAddrPtr )
{
  socket_t* p = _handleToPtr(h);
  return _initAddr(p,addrStr,portNumber,retAddrPtr);
}
      
const char* cw::net::socket::addrToString( const struct sockaddr_in* addr, char* buf, unsigned bufN )
{
  errno = 0;
  
  if( inet_ntop(AF_INET, &(addr->sin_addr),  buf, bufN) == NULL)
  {
    cwLogSysError(kOpFailRC,errno, "Network address to string conversion failed." );
    return NULL;
  }
  
  buf[bufN-1]=0;
  return buf;
}
      
bool cw::net::socket::addrIsEqual( const struct sockaddr_in* a0, const struct sockaddr_in* a1 )
{
  return a0->sin_family == a1->sin_family 
    &&   a0->sin_port   == a1->sin_port 
    &&   memcmp(&a0->sin_addr,&a1->sin_addr,sizeof(a0->sin_addr))==0;  
}
      
const char* cw::net::socket::hostName( handle_t h )
{
  socket_t* p = _handleToPtr(h);

  errno = 0;

  if( gethostname(p->hnameBuf,HOST_NAME_MAX) != 0 )
  {
     cwLogSysError(kOpFailRC,errno, "gethostname() failed." );
     return NULL;
  }
  
  p->hnameBuf[HOST_NAME_MAX] = 0;
  return p->hnameBuf;
}
