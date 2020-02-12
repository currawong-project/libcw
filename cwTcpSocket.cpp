#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
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
        int                fdH;
        unsigned           createFlags;
        unsigned           flags;
        unsigned           recvBufByteCnt;
        struct sockaddr_in sockaddr;
        char               ntopBuf[ INET_ADDRSTRLEN+1 ]; // use INET6_ADDRSTRLEN for IPv6
        char               hnameBuf[ HOST_NAME_MAX+1 ];
      } socket_t;

      inline socket_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,socket_t>(h); }

      rc_t _destroy( socket_t* p )
      {
        
        // close the fdH
        if( p->fdH != cwSOCKET_NULL_SOCK )
        {
          errno = 0;
          
          if( ::close(p->fdH) != 0 )
            cwLogSysError(kOpFailRC,errno,"The socket fd close failed." );
          
          p->fdH = cwSOCKET_NULL_SOCK;		
        }

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

        //if( portNumber == kInvalidPortNumber )
        //  return cwLogError(kInvalidArgRC,"The port number %i cannot be used.",kInvalidPortNumber);
	
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
        if( portNumber != kInvalidPortNumber  )
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


      rc_t _setTimeOutMs( socket_t* p, unsigned timeOutMs )
      {
        rc_t rc = kOkRC;

        struct timeval 		timeOut;
    
        // set the socket time out 
        timeOut.tv_sec 	= timeOutMs/1000;
        timeOut.tv_usec = (timeOutMs - (timeOut.tv_sec * 1000)) * 1000;
        
        if( setsockopt( p->sockH, SOL_SOCKET, SO_RCVTIMEO, &timeOut, sizeof(timeOut) ) == cwSOCKET_SYS_ERR )
        {
          rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket timeout failed." );
          goto errLabel;
        }

      errLabel:
        return rc;
      }

      rc_t _get_info( int sockH, unsigned char outBuf[6], struct sockaddr_in* addr, const char* interfaceName )
      {
        cw::rc_t  rc   = kOkRC;
        struct ifreq  ifr;
        struct ifconf ifc;
        char buf[1024];
    
        ifc.ifc_len = sizeof(buf);
        ifc.ifc_buf = buf;
  
        if (ioctl(sockH, SIOCGIFCONF, &ifc) == -1)
        {
          rc = cwLogSysError(kOpFailRC,errno,"ioctl(SIOCGIFCONF) failed.");
          return rc;
        }

        struct ifreq*             it  = ifc.ifc_req;
        const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

        for (; it != end; ++it)
        {
          if( strcmp(it->ifr_name,interfaceName ) == 0 )
          {
            strcpy(ifr.ifr_name, it->ifr_name);
        
            if (ioctl(sockH, SIOCGIFFLAGS, &ifr) != 0)
            {
              rc = cwLogSysError(kOpFailRC,errno,"ioctl(SIOCGIFCONF) failed.");
            }
            else
            {
              if (! (ifr.ifr_flags & IFF_LOOPBACK))
              {
                // don't count loopback
                if (ioctl(sockH, SIOCGIFHWADDR, &ifr) == 0)
                {
                  memcpy(outBuf, ifr.ifr_hwaddr.sa_data, 6);

                  if( addr != nullptr &&  ioctl(sockH, SIOCGIFADDR, &ifr) == 0)
                  {
                    addr->sin_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
                  }
            
                  return kOkRC;
                }
              }
            }
          }
        }
  
        return cwLogError(kInvalidArgRC,"The network interface information for '%s' could not be found.", interfaceName);
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
  portNumber_t remotePort,
  const char*  localAddr)
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  socket_t* p = mem::allocZ<socket_t>();
  p->sockH       = cwSOCKET_NULL_SOCK;
  p->fdH         = cwSOCKET_NULL_SOCK;
  p->createFlags = flags;
  
  int type     = cwIsFlag(flags,kStreamFl) ? SOCK_STREAM : SOCK_DGRAM;
  int protocol = cwIsFlag(flags,kTcpFl)    ? 0           : IPPROTO_UDP;
  
  // get a handle to the socket
  if(( p->sockH = ::socket( AF_INET, type, protocol ) ) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC, errno, "Socket create failed." );	 
	
  // if this socket should block
  if( cwIsFlag(flags,kBlockingFl))
  {
    if( timeOutMs > 0 )
    {
      _setTimeOutMs(p,timeOutMs);
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
    
  if( cwIsFlag(flags,kReuseAddrFl) )
  {
    unsigned int reuseaddr = 1;
    if( setsockopt(p->sockH, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(reuseaddr)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'reuse address' attribute failed." );
      goto errLabel;      
    }
  }
  
#ifdef SO_REUSEPORT
  if( cwIsFlag(flags,kReusePortFl) )
  {
    unsigned int reuseaddr = 1;
    if(setsockopt(p->sockH, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuseaddr, sizeof(reuseaddr)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'reuse port' attribute failed." );
      goto errLabel;      
    }
  }
#endif

  if( cwIsFlag(flags,kMultiCastTtlFl) )
  {
    unsigned char ttl = 1;
    if( setsockopt(p->sockH, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'multicast TTL' attribute failed." );
      goto errLabel;      
    }
  }

  if( cwIsFlag(flags,kMultiCastLoopFl) )
  {
    
    unsigned char loopback = 1;
    if( setsockopt(p->sockH, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'reuse port' attribute failed." );
      goto errLabel;      
    }
    
  }
  

  // if broadcast option was requested.
  if( cwIsFlag(flags,kBroadcastFl) )
  {
    int bcastFl                                                                      = 1;
    if( setsockopt( p->sockH, SOL_SOCKET, SO_BROADCAST, &bcastFl, sizeof(bcastFl) ) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket broadcast attribute failed." );
      goto errLabel;
    }
  }
  
  // create the 32 bit local address		
  if((rc = _initAddr(p, localAddr, port,  &p->sockaddr )) != kOkRC )
    goto errLabel;

  // bind the socket to a local address/port	
  if( (bind( p->sockH, (struct sockaddr*)&p->sockaddr, sizeof(p->sockaddr))) == cwSOCKET_SYS_ERR )
  {
    rc = cwLogSysError(kOpFailRC,errno,"Socket bind failed." );
    goto errLabel;
  }

  // get the local address as a string
  if((rc = addrToString( &p->sockaddr, p->ntopBuf,  sizeof(p->ntopBuf) )) != kOkRC )
    goto errLabel;
  
  
  // if a remote addr was given connect this socket to it
  if( remoteAddr != NULL )
    if((rc = _connect(p,remoteAddr,remotePort)) != kOkRC )
      goto errLabel;

  // if the socket should be marked for listening
  if( cwIsFlag(flags,kListenFl) )
  {
    if( ::listen(p->sockH, 10) != 0 )
    {
      rc = cwLogSysError(kOpFailRC,errno,"Socket listen() failed.");
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

unsigned cw::net::socket::flags( handle_t h )
{
  socket_t* p  = _handleToPtr(h);
  return p->createFlags;
}

cw::rc_t cw::net::socket::set_multicast_time_to_live( handle_t h, unsigned seconds )
{
  rc_t      rc = kOkRC;
  socket_t* p  = _handleToPtr(h);
  
  if( setsockopt( p->sockH, IPPROTO_IP, IP_MULTICAST_TTL, &seconds, sizeof(seconds) ) == cwSOCKET_SYS_ERR )
  {
    rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket multicast TTLfailed." );
  }

  return rc;
}

cw::rc_t cw::net::socket::join_multicast_group( handle_t h, const char* addrStr )
{
  rc_t           rc = kOkRC;  
  socket_t*      p  = _handleToPtr(h);
  struct ip_mreq req;

	memset(&req, 0, sizeof(req));

  if(inet_pton(AF_INET,addrStr,&req.imr_multiaddr.s_addr) == 0 )
  {
    rc = cwLogSysError(kOpFailRC,errno, "The network address string '%s' could not be converted to a netword address structure.",cwStringNullGuard(addrStr) );
    goto errLabel;
  }
  
	req.imr_interface.s_addr = INADDR_ANY;
  
	if(setsockopt(p->sockH, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&req, sizeof(req)) == cwSOCKET_SYS_ERR )
  {
    rc = cwLogSysError(kOpFailRC,errno, "Attempt to add socket to multicast group on '%s' failed.", cwStringNullGuard(addrStr) );
    goto errLabel;   
  }

 errLabel:
  return rc;
}

cw::rc_t cw::net::socket::setTimeOutMs( handle_t h, unsigned timeOutMs )
{
  socket_t* p = _handleToPtr(h);
  return _setTimeOutMs(p,timeOutMs);
}


cw::rc_t cw::net::socket::accept( handle_t h )
{
  struct sockaddr_storage remoteAddr; // connector's address information
  socklen_t sin_size = sizeof(remoteAddr);
  
  rc_t rc = kOkRC;
  int  fd = cwSOCKET_NULL_SOCK;
  
  socket_t* p = _handleToPtr(h);
  
  if((fd = accept(p->sockH, (struct sockaddr*)&remoteAddr, &sin_size)) < 0)
  {
    if( errno == EAGAIN || errno == EWOULDBLOCK )
      rc = kTimeOutRC;
    else
    {
      rc = cwLogSysError(kOpFailRC,errno,"Socket accept() failed.");
      goto errLabel;
    }
  }
  else
  {
    char s[INET_ADDRSTRLEN+1];
  
    addrToString( (struct sockaddr_in*)&remoteAddr, s, INET_ADDRSTRLEN );

    if( p->fdH != cwSOCKET_NULL_SOCK )
    {
      close(p->fdH);
      p->fdH = cwSOCKET_NULL_SOCK;
    }

    p->fdH = fd;

    p->flags = cwSetFlag(p->flags,kIsConnectedFl);

    /*
                  if( false )
                  {
                    struct sockaddr_in addr;
                    char aBuf[ INET_ADDRSTRLEN+1 ];
                    peername( h, &addr );
                    addrToString( &addr, aBuf, INET_ADDRSTRLEN);
                    printf("DNS-SD PEER: %i %s\n", addr.sin_port,aBuf );
                    
                  }
    */
    
    cwLogInfo("Connect:%s\n",s);
  }

 errLabel:
  return rc;
}


cw::rc_t cw::net::socket::connect( handle_t h, const char* remoteAddr, portNumber_t remotePort )
{
  socket_t* p = _handleToPtr(h);
  return _connect(p,remoteAddr,remotePort);  
}

bool cw::net::socket::isConnected( handle_t h )
{
  socket_t* p = _handleToPtr(h);  
  return cwIsFlag(p->flags,kIsConnectedFl);
}
      
cw::rc_t cw::net::socket::send( handle_t h, const void* data, unsigned dataByteCnt )
{
  socket_t* p = _handleToPtr(h);
  errno = 0;
  
  if( cwIsFlag(p->flags,kIsConnectedFl) == false )
    return cwLogError(kInvalidOpRC,"socket::send() only works with connected sockets.");

  int fd = p->fdH != cwSOCKET_NULL_SOCK ? p->fdH : p->sockH;

  if( ::send( fd, data, dataByteCnt, 0 ) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC,errno,"Send failed.");

  return kOkRC;    
}
      
cw::rc_t cw::net::socket::send( handle_t h, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr )
{
  socket_t* p = _handleToPtr(h);
  
  errno = 0;
   
  if( ::sendto(p->sockH, data, dataByteCnt, 0, (struct sockaddr*)remoteAddr, sizeof(*remoteAddr)) == cwSOCKET_SYS_ERR )
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

cw::rc_t cw::net::socket::receive( handle_t h, char* data, unsigned dataByteCnt, unsigned* recvByteCntRef, struct sockaddr_in* fromAddr )
{
  socket_t* p                = _handleToPtr(h);
  rc_t      rc               = kOkRC;
  ssize_t   retVal           = 0;
	socklen_t sizeOfRemoteAddr = fromAddr==NULL ? 0 : sizeof(struct sockaddr_in);
  
  errno = 0;

  if( recvByteCntRef != NULL )
    *recvByteCntRef = 0;

  int fd = p->fdH != cwSOCKET_NULL_SOCK ? p->fdH : p->sockH;

	if((retVal = recvfrom(fd, data, dataByteCnt, 0, (struct sockaddr*)fromAddr, &sizeOfRemoteAddr )) == cwSOCKET_SYS_ERR )
  {
    switch( errno )
    {
      case EAGAIN:
        return kTimeOutRC;

      case ENOTCONN:
        if( cwIsFlag(p->flags,kIsConnectedFl ) )
          cwLogWarning("Socket Disconnected.");
        
        p->flags = cwClrFlag(p->flags,kIsConnectedFl);
    }
    
    return cwLogSysError(kOpFailRC,errno,"recvfrom() failed.");
  }

  if( recvByteCntRef != NULL )
    *recvByteCntRef = retVal;

  return rc;  
}

cw::rc_t cw::net::socket::select_receive(handle_t h, char* buf, unsigned bufByteCnt, unsigned timeOutMs, unsigned* recvByteCntRef, struct sockaddr_in* fromAddr )
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
      rc = kTimeOutRC;
      break;
			
    case 1:   // (> 0) count of ready descripters
      if( FD_ISSET(p->sockH,&rdSet) )
      {
        socklen_t addrByteCnt = fromAddr==nullptr ? 0 : sizeof(*fromAddr);
        ssize_t   retByteCnt;

        errno = 0;

        // recv the incoming msg into buf[]
        if(( retByteCnt = recvfrom( p->sockH, buf, bufByteCnt, 0, (struct sockaddr*)fromAddr, &addrByteCnt )) == cwSOCKET_SYS_ERR )
        {
          switch( errno )
          {
            case ECONNRESET:
              if( cwIsFlag(p->flags,kIsConnectedFl) )
                cwLogWarning("Socket Disconnected.");
              p->flags = cwClrFlag(p->flags,kIsConnectedFl);

          }
          
          rc = cwLogSysError(kOpFailRC,errno,"recvfrom() failed.");
          
        }
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

cw::rc_t cw::net::socket::recv_from(handle_t h, char* buf, unsigned bufByteCnt, unsigned* recvByteCntRef, struct sockaddr_in* fromAddr )
{
  rc_t      rc      = kOkRC;
  socket_t* p       = _handleToPtr(h);
	socklen_t addrlen = 0;
  int       bytesN  = 0;

  if( recvByteCntRef != nullptr )
    *recvByteCntRef = 0;
  
  if( fromAddr != nullptr )
  {
    addrlen = sizeof(*fromAddr);
    memset(fromAddr,0,sizeof(*fromAddr));
  }

  int fd = p->fdH != cwSOCKET_NULL_SOCK ? p->fdH : p->sockH;
  
	if((bytesN = recvfrom(fd, buf, bufByteCnt, 0, (struct sockaddr*)fromAddr, &addrlen)) < 0 )
  {
    // if this is a non-blocking socket then return value -1 indicates that no data is available.
    if( cwIsNotFlag( p->flags, kBlockingFl) && bytesN == -1)
      bytesN = 0;
    else
    {
      rc = cwLogSysError(kReadFailRC,errno,"recvfrom() failed.");
      goto errLabel;
    }
  }

  if( recvByteCntRef != nullptr )
    *recvByteCntRef = bytesN;
  
 errLabel:
  return rc;
}

cw::rc_t cw::net::socket::get_mac( handle_t h, unsigned char outBuf[6], struct sockaddr_in* addr, const char* netInterfaceName )
{
  socket_t* p = _handleToPtr(h);
  return _get_info(p->sockH, outBuf, addr, netInterfaceName );
}

cw::rc_t cw::net::socket::initAddr( handle_t h, const char* addrStr, portNumber_t portNumber, struct sockaddr_in* retAddrPtr )
{
  socket_t* p = _handleToPtr(h);
  return _initAddr(p,addrStr,portNumber,retAddrPtr);
}
      
cw::rc_t cw::net::socket::addrToString( const struct sockaddr_in* addr, char* buf, unsigned bufN )
{
  rc_t rc = kOkRC;
  
  errno = 0;
  
  if( inet_ntop(AF_INET, &(addr->sin_addr),  buf, bufN) == NULL)
  {
    rc = cwLogSysError(kOpFailRC,errno, "Network address to string conversion failed." );
    goto errLabel;
  }
  
  buf[bufN-1]=0;
 errLabel:
  return rc;
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

const char* cw::net::socket::ipAddress( handle_t h )
{
  socket_t* p = _handleToPtr(h);
  return p->ntopBuf;
}

unsigned    cw::net::socket::inetAddress( handle_t h )
{
  socket_t* p = _handleToPtr(h);

  return  p->sockaddr.sin_addr.s_addr;
}

cw::net::socket::portNumber_t    cw::net::socket::port( handle_t h )
{
  socket_t* p = _handleToPtr(h);
  return  ntohs(p->sockaddr.sin_port);
}

cw::rc_t cw::net::socket::peername( handle_t h, struct sockaddr_in* addr )
{
  rc_t      rc = kOkRC;
  socklen_t n  = sizeof(struct sockaddr_in);
  socket_t* p = _handleToPtr(h);
  
  if( getpeername(p->sockH, (struct sockaddr*)addr, &n)  == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC,errno,"Get peer name failed.");

  addr->sin_port = ntohs(addr->sin_port);

  return rc;
}

cw::rc_t cw::net::socket::get_info( const char* netInterfaceName, unsigned char mac[6], char* host, unsigned hostN, struct sockaddr_in* addr )
{
  rc_t rc = kOkRC;
  int sockH;

  if( host != nullptr )
    if( gethostname(host,hostN) != 0 )
      return cwLogSysError(kOpFailRC,errno,"Unable to get the local host name.");
  
  // get a handle to the socket
  if(( sockH = ::socket( AF_INET, SOCK_DGRAM, 0 ) ) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC,errno,"Unable to create temporary socket.");

  if((rc = _get_info(sockH,mac,addr,netInterfaceName)) != kOkRC )
    goto errLabel;

 errLabel:
  close(sockH);
  return rc;
}
