#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>	
#include <arpa/inet.h>	
#include <fcntl.h>		
#include <unistd.h>  // close
#include <poll.h>

#include "cwSocket.h"

#define cwSOCKET_SYS_ERR     (-1)
#define cwSOCKET_NULL_SOCK   (-1)

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX  _POSIX_HOST_NAME_MAX
#endif


namespace cw
{
  namespace sock
  {

    enum
    {
     kIsConnectedFl = 0x01,
     kIsBlockingFl  = 0x02
    };
    
    typedef struct sock_str
    {
      unsigned           userId;
      unsigned           connId;
      int                sockH;
      unsigned           createFlags;
      unsigned           flags;
      callbackFunc_t     cbFunc;
      void*              cbArg;
      struct sockaddr_in localSockAddr;
      struct sockaddr_in remoteSockAddr;
      char               ntopBuf[ INET_ADDRSTRLEN+1 ]; // use INET6_ADDRSTRLEN for IPv6
      char               hnameBuf[ HOST_NAME_MAX+1 ];
      struct pollfd*     pollfd;
      unsigned           nextConnId;
      struct sock_str*   parent;     // pointer to this socket's parent socket
      struct sock_str*   children;   // pointer to this sockets children (parent==NULL), or sibling (parent!=NULL)
    } sock_t;
    
    typedef struct mgr_str
    { 
      uint8_t*       buf;       // buf[bufByteN]
      unsigned       bufByteN;  // size of buf[]
      
      struct pollfd* pollfdA;   // pollfdA[ sockMaxN ] poll array
      sock_t*        sockA;     //.sockA[ sockMaxN ] 
      unsigned       sockMaxN;  // sockMaxN count of elements in sockA[] and pollfdA[] 
      unsigned       sockN;     // sockA[ sockN ] sock record in use
      
    } mgr_t;

    mgr_t* _handleToPtr( handle_t h)
    { return handleToPtr<handle_t,mgr_t>(h); }

    sock_t* _idToSock( mgr_t* p, unsigned userId, bool showErrorFl=false )
    {
      for(unsigned i=0; i<p->sockN; ++i)
        if( p->sockA[i].userId == userId )
          return p->sockA + i;
      
      if( showErrorFl )
        cwLogError(kInvalidIdRC,"A socket with id:%i could not be found.",userId);
      
      return nullptr;
    }

    
    rc_t _getMgrAndSocket( handle_t h, unsigned userId, mgr_t*& p, sock_t*& s, bool showErrorFl = false )
    {
      p = _handleToPtr(h);
      if((s = _idToSock(p,userId,showErrorFl)) == nullptr )
        return kInvalidArgRC;
      return kOkRC;
    }

    bool _sockIsOpen( sock_t* p )
    { return p->sockH != cwSOCKET_NULL_SOCK; }

    void _unlinkChild( sock_t* s )
    {
      sock_t* ps  = s->parent;
      sock_t* cs0 = nullptr;
      for(sock_t* cs=ps->children; cs!=nullptr; cs=cs->children)
      {
        if( cs == s )
        {
          if( cs0 == nullptr )
            ps->children = cs->children;
          else
            cs0->children = cs->children;
          
          cs->children = nullptr;
          cs->parent   = nullptr;
          
          break;
        }
        
        cs0 = cs;
        
      }   
    }
    
    rc_t _closeSock( mgr_t* p, sock_t* s )
    {
      rc_t rc = kOkRC;
      
      // close the socket		
      if( s->sockH != cwSOCKET_NULL_SOCK )
      {
        errno = 0;
          
        if( ::close(s->sockH) != 0 )
          rc = cwLogSysError(kOpFailRC,errno,"The socket close failed." );
          
        s->sockH = cwSOCKET_NULL_SOCK;		
      }

      if( s->parent != nullptr )
        _unlinkChild( s );

      s->userId                    = kInvalidId;
      s->createFlags               = 0;
      s->flags                     = 0;
      s->pollfd->events            = 0;
      s->pollfd->fd                = cwSOCKET_NULL_SOCK;
      s->remoteSockAddr.sin_family = AF_UNSPEC;

      return rc;      
    }

    rc_t _destroyMgr( mgr_t* p )
    {
      rc_t rc = kOkRC;

      for(unsigned i=0; i<p->sockN; ++i)
      {
        rc_t rc0 = _closeSock(p,p->sockA + i);
        if( rc0 != kOkRC )
          rc = rc0;
      }
      
      mem::release(p->sockA);
      mem::release(p->pollfdA);
      mem::release(p->buf);
      mem::release(p);
      return rc;
    }

    rc_t  _locateAvailSlot( mgr_t* p, unsigned sockN, unsigned& availIdx_Ref, unsigned& sockN_Ref )
    {
      availIdx_Ref = kInvalidIdx;
      sockN_Ref    = sockN;
      
      // locate an avail. socket between 0:sockN
      for(unsigned i=0; i<sockN; ++i)
        if( !_sockIsOpen(p->sockA + i) )
        {
          availIdx_Ref = i;
          return kOkRC;
        }

      // Be sure all slots are not already in use.
      if( sockN >= p->sockMaxN )
        return cwLogError(kResourceNotAvailableRC,"All socket slots are in use.");

      // Expand the slot array to accommodate another socket
      availIdx_Ref = sockN;
      sockN_Ref    = sockN + 1;

      return kOkRC;
    }

    rc_t _accept( mgr_t* p, sock_t* s, unsigned sockN )      
    {
      rc_t                    rc       = kOkRC;
      struct sockaddr_storage remoteAddr;                      
      socklen_t               sin_size = sizeof(remoteAddr);
      int                     fd;
      unsigned                newSockN = p->sockN;
      unsigned                sockIdx  = kInvalidIdx;
      sock_t*                 cs       = nullptr;
      
      // accept the incoming connection
      if((fd = accept(s->sockH, (struct sockaddr*)&remoteAddr, &sin_size )) < 0)
      {
        if( errno == EAGAIN || errno == EWOULDBLOCK )
          rc = kTimeOutRC;
        else
        {
          rc = cwLogSysError(kOpFailRC,errno,"Socket accept() failed.");
          goto errLabel;
        }        
      }

      // ... then find an available socket record
      if((rc =  _locateAvailSlot(p, sockN, sockIdx, newSockN )) != kOkRC )
      {
        rc = cwLogError(rc,"There are no available slots to 'accept' a new socket connection.");
        goto errLabel;
      }

      printf("Socket: userId:%i connected.\n", s->userId);      
      
      // initialize the socket record
      cs = p->sockA + sockIdx;

      cs->userId           = s->userId;
      cs->connId           = s->nextConnId++;
      cs->sockH            = fd;
      cs->createFlags      = 0;
      cs->remoteSockAddr   = *(struct sockaddr_in*)&remoteAddr;
      cs->cbFunc           = s->cbFunc;
      cs->cbArg            = s->cbArg;
      cs->pollfd           = p->pollfdA + sockIdx;
      cs->pollfd->events   = POLLIN;
      cs->pollfd->fd       = fd;
      cs->nextConnId       = 0;
      cs->parent           = s;
      cs->children         = s->children;
      s->children          = cs;

      if((rc = addrToString( (const struct sockaddr_in*)&remoteAddr, cs->ntopBuf,  sizeof(cs->ntopBuf) )) != kOkRC )
        goto errLabel;      

      if( s->cbFunc != nullptr )
        s->cbFunc( s->cbArg, kConnectCbId, s->userId, cs->connId, nullptr, 0, (const struct sockaddr_in*)&remoteAddr );

      
    errLabel:
      if( rc != kOkRC )
        close(fd);
      
      return rc;
    }

    // Called to read the serial device when data is known to be waiting.
    rc_t _receive( mgr_t* p, sock_t* s, unsigned& readN_Ref, void* buf=nullptr, unsigned bufByteN=0, struct sockaddr_in* fromAddr=nullptr )
    {
      rc_t     rc     = kOkRC;
      void*    b      = buf;
      unsigned bN     = bufByteN;
      ssize_t  bytesReadN = 0;
      struct sockaddr_in sockaddr;
      socklen_t sizeOfFromAddr = 0;

      memset(&sockaddr,0,sizeof(sockaddr));
      
      // clear the count of actual bytes read 
      readN_Ref = 0;     

      // if the socket is not open then there is nothing to do
      if( !_sockIsOpen(s) )
        return cwLogWarningRC( kResourceNotAvailableRC, "An attempt was made to read from a closed socket.");
            
      // if no  buffer was given then use the default buffer
      if( b ==nullptr || bufByteN == 0 )
      {
        b  = p->buf;
        bN = p->bufByteN;
      }
      
      // if no src address buffer was given and this is a UDP socket
      if( fromAddr == nullptr && cwIsNotFlag(s->createFlags,kTcpFl) )
      {
        fromAddr       = &sockaddr;        
        sizeOfFromAddr = sizeof(struct sockaddr_in);
      }
  
      errno = 0;

      // read the socket
      if((bytesReadN = recvfrom(s->sockH, b, bN, 0, (struct sockaddr*)fromAddr, &sizeOfFromAddr )) != cwSOCKET_SYS_ERR )
      {
        // return the count of actual bytes read
        readN_Ref = bytesReadN;

        // if no return buffer was given and the socket has a callback function - then call it
        if( bytesReadN > 0 && s->cbFunc != nullptr  && (buf==nullptr || bufByteN==0) )
        {
          // if no src addr was given (because this is a TCP socket) then use the connected remote socket
          if( fromAddr == nullptr && s->remoteSockAddr.sin_family != AF_UNSPEC)
            fromAddr = &s->remoteSockAddr;
          
          s->cbFunc( s->cbArg, kReceiveCbId, s->userId, s->connId, b, bytesReadN, fromAddr );
        }
          
      }
      else
      {
        // an error occurred during the read operation
        switch( errno )
        {
          case EAGAIN:
            return kTimeOutRC;

          case ENOTCONN:
            if( cwIsFlag(s->flags,kIsConnectedFl ) )
              cwLogWarning("Socket Disconnected.");
        
            s->flags = cwClrFlag(s->flags,kIsConnectedFl);
        }
    
        return cwLogSysError(kOpFailRC,errno,"recvfrom() failed.");
      }
      
      return rc;
    }


    // Block devices waiting for data on a port. If userId is valid then wait for data on a specific port otherwise
    // wait for data on all ports.
    rc_t _poll( mgr_t* p, unsigned timeOutMs, unsigned& readN_Ref, unsigned userId=kInvalidId, void* buf=nullptr, unsigned bufByteN=0, struct sockaddr_in* fromAddr=nullptr )
    {
      rc_t rc = kOkRC;
      int sysRC;

      readN_Ref = 0;

      // if there are not ports then there is nothing to do
      if( p->sockN == 0 )
        return rc;

      struct pollfd* pfd  = p->pollfdA;  // first struct pollfd
      unsigned       pfdN = p->sockN;    // count of pollfd's
      sock_t*        s    = p->sockA;    // port assoc'd with first pollfd

      // if only one socket is to be read ...
      if( userId != kInvalidId )
      {
        // ... then locate it
        if((s = _idToSock(p,userId)) == nullptr )
          return kInvalidArgRC;
        
        pfd  = s->pollfd;
        pfdN = 1;
      }

      // A port to poll must exist
      if( p == nullptr )
        return cwLogError(kInvalidArgRC,"The port with id %i could not be found.",userId);
      

      // block waiting for data on one of the ports
      if((sysRC = ::poll(pfd,pfdN,timeOutMs)) == 0)
        rc = kTimeOutRC;
      else
      {
        unsigned newSockN = 0;
        
        // ::poll() encountered a system exception
        if( sysRC < 0 )
          return cwLogSysError(kReadFailRC,errno,"Poll failed on serial port.");

        // interate through the ports looking for the ones which have data waiting ...
        for(unsigned i=0; i<p->sockN; ++i)
        {
          if( p->sockA[i].pollfd->revents & POLLHUP )
          {
            printf("Socket userId:%i connId:%i disconnected.",p->sockA[i].userId,p->sockA[i].connId);
            _closeSock(p,p->sockA+i);
            continue;
          }
          if( p->sockA[i].pollfd->revents & POLLERR )
          {
            printf("ERROR\n");
          }


          if( p->sockA[i].pollfd->revents & POLLNVAL )
          {
          }
          
          if( p->sockA[i].pollfd->revents & POLLIN )
          {
            unsigned actualReadN = 0;

            sock_t* s = p->sockA + i;

            // If this is a listening/streaming socket then it is waiting for connections
            if( cwAllFlags(s->createFlags,kListenFl|kStreamFl) )
            {
              rc_t rc0;

              // accept an new connection to this socket
              if((rc0 =  _accept( p, s, p->sockN+newSockN)) != kOkRC )
                rc = rc0;
              
              newSockN += 1;
            }
            else // otherwise it is a non-listening socket that is receiving data
            {                        
              if((rc = _receive( p, s, actualReadN, buf, bufByteN, fromAddr )) != kOkRC )
                return rc;
            }
            
            readN_Ref += actualReadN;
          }
        }
        
        p->sockN += newSockN;
      }
  
      return rc;      
    }


    rc_t _initAddr( const char* addrStr, portNumber_t portNumber, struct sockaddr_in* retAddrPtr )
    {
      memset(retAddrPtr,0,sizeof(struct sockaddr_in));

      //if( portNumber == kInvalidPortNumber )
      //  return cwLogError(kInvalidArgRC,"The port number %i cannot be used.",kInvalidPortNumber);
	
      if( addrStr == nullptr )
        retAddrPtr->sin_addr.s_addr 	= htonl(INADDR_ANY);
      else
      {
        errno = 0;

        if(inet_pton(AF_INET,addrStr,&retAddrPtr->sin_addr) == 0 )
          return cwLogSysError(kOpFailRC,errno, "The network address string '%s' could not be converted to a netword address structure.",cwStringNullGuard(addrStr) );
      }
	
      //retAddrPtr->sin_len 			= sizeof(struct sockaddr_in);
      retAddrPtr->sin_family = AF_INET;
      if( portNumber != kInvalidPortNumber  )
        retAddrPtr->sin_port = htons(portNumber);
	
      return kOkRC;
    }

    rc_t _connect( sock_t* s, const char* remoteAddr, portNumber_t remotePort )
    {
      struct sockaddr_in addr;
      rc_t               rc;

      // create the remote address		
      if((rc = _initAddr( remoteAddr, remotePort,  &addr )) != kOkRC )
        return rc;
		
      errno = 0;

      // ... and connect this socket to the remote address/port
      if( connect(s->sockH, (struct sockaddr*)&addr, sizeof(addr)) == cwSOCKET_SYS_ERR )
      {
        if( cwIsNotFlag(s->createFlags, kBlockingFl) && errno == EINPROGRESS )
        {
          // if the socket is non-blocking the connection will complete asynchronously
        }
        else
        {
          return cwLogSysError(kOpFailRC, errno, "Socket connect to %s:%i failed.", cwStringNullGuard(remoteAddr), remotePort );
        }
      }
      
      s->flags = cwSetFlag(s->flags,kIsConnectedFl);

      s->remoteSockAddr = addr;

      return rc;
    }


    rc_t _setTimeOutMs( sock_t* s, unsigned timeOutMs )
    {
      rc_t rc = kOkRC;

      struct timeval 		timeOut;
    
      // set the socket time out 
      timeOut.tv_sec 	= timeOutMs/1000;
      timeOut.tv_usec = (timeOutMs - (timeOut.tv_sec * 1000)) * 1000;
        
      if( setsockopt( s->sockH, SOL_SOCKET, SO_RCVTIMEO, &timeOut, sizeof(timeOut) ) == cwSOCKET_SYS_ERR )
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



cw::rc_t cw::sock::createMgr( handle_t& hRef, unsigned recvBufByteN, unsigned maxSockN )
{
  rc_t rc;
  if((rc = destroyMgr(hRef)) != kOkRC )
    return rc;

  mgr_t* p    = mem::allocZ<mgr_t>();
  p->buf      = mem::allocZ<uint8_t>(recvBufByteN);
  p->bufByteN = recvBufByteN;
  p->sockA    = mem::allocZ<sock_t>( maxSockN );
  p->pollfdA  = mem::allocZ<struct pollfd>( maxSockN );
  p->sockMaxN = maxSockN;
  
  for(unsigned i=0; i<p->sockMaxN; ++i)
  {
    p->sockA[i].sockH                     = cwSOCKET_NULL_SOCK;
    p->sockA[i].remoteSockAddr.sin_family = AF_UNSPEC;
    p->sockA[i].pollfd                    = p->pollfdA + i;
  }
  
  hRef.set(p);
  return rc;
}

cw::rc_t cw::sock::destroyMgr( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( ! hRef.isValid() )
    return rc;

  mgr_t* p = _handleToPtr(hRef);

  if((rc = _destroyMgr(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;
}

cw::rc_t cw::sock::create( handle_t h,
  unsigned       userId,
  short          port,
  unsigned       flags,
  unsigned       timeOutMs,
  callbackFunc_t cbFunc,
  void*          cbArg,
  const char*    remoteAddr,
  portNumber_t   remotePort,
  const char*    localAddr )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;

  // if a userId is being reused
  _getMgrAndSocket(h, userId, p, s, false );

  if( s != nullptr )
    if((rc = _closeSock(p,s)) != kOkRC )
      return kInvalidArgRC;

  // if userId did not identify an existing socket ...
  if( s == nullptr )
  {
    unsigned sockIdx;
    // ... then find an available socket record
    if((rc =  _locateAvailSlot(p, p->sockN, sockIdx, p->sockN )) != kOkRC )
      return rc;

    s = p->sockA + sockIdx;    
  }

 
  int type     = cwIsFlag(flags,kStreamFl) ? SOCK_STREAM : SOCK_DGRAM;
  int protocol = cwIsFlag(flags,kTcpFl)    ? 0           : IPPROTO_UDP;
  
  // get a handle to the socket
  if(( s->sockH = ::socket( AF_INET, type, protocol ) ) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC, errno, "Socket create failed." );
  
  s->userId         = userId;
  s->connId         = kInvalidId;
  s->createFlags    = flags;
  s->flags          = 0;
  s->cbFunc         = cbFunc;
  s->cbArg          = cbArg;
  s->pollfd         = p->pollfdA + (s - p->sockA);
  s->pollfd->events = POLLIN;
  s->pollfd->fd     = s->sockH;
	s->nextConnId     = 0;
  
  // if this socket should block
  if( cwIsFlag(flags,kBlockingFl))
  {
    if( timeOutMs > 0 )
    {
      _setTimeOutMs(s,timeOutMs);
    }
    
    s->flags = cwSetFlag(s->flags,kIsBlockingFl);
  }
  else // otherwise this is a non-blocking socket
  {
    int opts;
		
    // get the socket options flags
    if( (opts = fcntl(s->sockH,F_GETFL)) < 0 )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to get the socket options flags failed." );
      goto errLabel;
    }
	    
    opts = (opts | O_NONBLOCK);
		
    // set the socket options flags
    if(fcntl(s->sockH,F_SETFL,opts) < 0) 
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket to non-blocking failed." );
      goto errLabel;
    }	    

  }

  if( cwIsFlag(flags,kReuseAddrFl) )
  {
    unsigned int reuseaddr = 1;
    if( setsockopt(s->sockH, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(reuseaddr)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'reuse address' attribute failed." );
      goto errLabel;      
    }
  }
  
#ifdef SO_REUSEPORT
  if( cwIsFlag(flags,kReusePortFl) )
  {
    unsigned int reuseaddr = 1;
    if(setsockopt(s->sockH, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuseaddr, sizeof(reuseaddr)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'reuse port' attribute failed." );
      goto errLabel;      
    }
  }
#endif

  if( cwIsFlag(flags,kMultiCastTtlFl) )
  {
    unsigned char ttl = 1;
    if( setsockopt(s->sockH, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'multicast TTL' attribute failed." );
      goto errLabel;      
    }
  }

  if( cwIsFlag(flags,kMultiCastLoopFl) )
  {    
    unsigned char loopback = 1;
    if( setsockopt(s->sockH, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback)) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket 'reuse port' attribute failed." );
      goto errLabel;      
    }    
  }  

  // if broadcast option was requested.
  if( cwIsFlag(flags,kBroadcastFl) )
  {
    int bcastFl  = 1;
    if( setsockopt( s->sockH, SOL_SOCKET, SO_BROADCAST, &bcastFl, sizeof(bcastFl) ) == cwSOCKET_SYS_ERR )
    {
      rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket broadcast attribute failed." );
      goto errLabel;
    }
  }
  
  // create the 32 bit local address		
  if((rc = _initAddr( localAddr, port,  &s->localSockAddr )) != kOkRC )
    goto errLabel;

  // bind the socket to a local address/port	
  if( (bind( s->sockH, (struct sockaddr*)&s->localSockAddr, sizeof(s->localSockAddr))) == cwSOCKET_SYS_ERR )
  {
    rc = cwLogSysError(kOpFailRC,errno,"Socket bind failed." );
    goto errLabel;
  }

  // get the local address as a string
  if((rc = addrToString( &s->localSockAddr, s->ntopBuf,  sizeof(s->ntopBuf) )) != kOkRC )
    goto errLabel;
  
  
  // if a remote addr was given connect this socket to it
  if( remoteAddr != nullptr )
    if((rc = _connect(s,remoteAddr,remotePort)) != kOkRC )
      goto errLabel;

  // if the socket should be marked for listening
  if( cwIsFlag(flags,kListenFl) )
  {
    if( ::listen(s->sockH, 10) != 0 )
    {
      rc = cwLogSysError(kOpFailRC,errno,"Socket listen() failed.");
      goto errLabel;
    }
  }
  
 errLabel:
  if(rc != kOkRC )
    _closeSock(p,s);

  return rc;
}

cw::rc_t cw::sock::destroy( handle_t h, unsigned userId )
{
  rc_t    rc = kOkRC;
  mgr_t*  p; 
  sock_t* s;

  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;

  if((rc = _closeSock(p,s)) != kOkRC )
    return rc;
  
  return rc;
}

cw::rc_t cw::sock::set_multicast_time_to_live( handle_t h, unsigned userId, unsigned seconds )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
   
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;
  
  if( setsockopt( s->sockH, IPPROTO_IP, IP_MULTICAST_TTL, &seconds, sizeof(seconds) ) == cwSOCKET_SYS_ERR )
  {
    rc = cwLogSysError(kOpFailRC,errno, "Attempt to set the socket multicast TTLfailed." );
  }

  return rc;
}

cw::rc_t cw::sock::join_multicast_group( handle_t h, unsigned userId, const char* addrStr )
{
  rc_t           rc = kOkRC;  
  mgr_t*         p;
  struct ip_mreq req;
  sock_t*        s;

	memset(&req, 0, sizeof(req));

  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;

  if(inet_pton(AF_INET,addrStr,&req.imr_multiaddr.s_addr) == 0 )
  {
    rc = cwLogSysError(kOpFailRC,errno, "The network address string '%s' could not be converted to a netword address structure.",cwStringNullGuard(addrStr) );
    goto errLabel;
  }
  
	req.imr_interface.s_addr = INADDR_ANY;
  
	if(setsockopt(s->sockH, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&req, sizeof(req)) == cwSOCKET_SYS_ERR )
  {
    rc = cwLogSysError(kOpFailRC,errno, "Attempt to add socket to multicast group on '%s' failed.", cwStringNullGuard(addrStr) );
    goto errLabel;   
  }

 errLabel:
  return rc;
}


// Set a destination address for this socket. Once a destination address is set
// the caller may use send() to communicate with the specified remote socket
// without having to specify a destination address on each call.
cw::rc_t cw::sock::connect( handle_t h, unsigned userId, const char* remoteAddr, portNumber_t remotePort )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
   
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;
  
  return _connect(s,remoteAddr,remotePort);  
}


bool cw::sock::isConnected( handle_t h, unsigned userId )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
   
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return false;
  
  return cwIsFlag(s->flags,kIsConnectedFl);
}
      
cw::rc_t cw::sock::send( handle_t h, unsigned userId, unsigned connId, const void* data, unsigned dataByteCnt )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;

  // locate the socket to send on
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;

  // If this a pre-connected socket ...
  if(  cwIsFlag(s->flags,kIsConnectedFl) )
  {
    if( ::send( s->sockH, data, dataByteCnt, 0 ) == cwSOCKET_SYS_ERR )
      rc = cwLogSysError(kOpFailRC,errno,"Send failed.");
  }
  else  // ... otherwise this is a listening socket with one or more child sockets
  {
    
    if( s->children == nullptr )
      return cwLogError(kInvalidOpRC,"socket::send() only works with connected sockets.");
  
    sock_t* cs = s->children;
    for(; cs != nullptr; cs=cs->children)
      if( connId==kInvalidId || cs->connId == connId )
      {
        errno = 0;
      
        if( ::send( cs->sockH, data, dataByteCnt, 0 ) == cwSOCKET_SYS_ERR )
          rc = cwLogSysError(kOpFailRC,errno,"Send failed.");

        if( cs->connId == connId )
          break;
      }
  
  }
  return rc;    
}
      
cw::rc_t cw::sock::send( handle_t h, unsigned userId, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
   
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;
  
  errno = 0;
   
  if( ::sendto(s->sockH, data, dataByteCnt, 0, (struct sockaddr*)remoteAddr, sizeof(*remoteAddr)) == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC,errno,"Send to remote addr. failed.");

  return kOkRC;
}

cw::rc_t cw::sock::send( handle_t h, unsigned userId, const void* data, unsigned dataByteCnt, const char* remoteAddr, portNumber_t remotePort )
{
  rc_t               rc = kOkRC;
  mgr_t*             p;
  sock_t*            s;
  struct sockaddr_in addr;
   
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;

  if((rc = _initAddr(remoteAddr,remotePort,&addr)) != kOkRC )
    return rc;
  
  return send( h, userId, data, dataByteCnt, &addr );
}

cw::rc_t cw::sock::receive_all( handle_t h, unsigned timeOutMs, unsigned& readByteN_Ref )
{
  rc_t  rc = kOkRC;
  mgr_t* p = _handleToPtr(h);

  if((rc = _poll( p, timeOutMs, readByteN_Ref )) != kOkRC  && rc != kTimeOutRC )
    return cwLogError(rc,"Socket receive failed.");
  
  return rc;
  
}

cw::rc_t cw::sock::receive(handle_t h, unsigned userId, unsigned& readByteN_Ref, void* buf, unsigned bufByteN, struct sockaddr_in* fromAddr )  
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;

  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;

  if((rc = _receive( p, s, readByteN_Ref, buf, bufByteN, fromAddr )) != kOkRC )
    return rc;

  return rc; 
}

cw::rc_t cw::sock::get_mac( handle_t h, unsigned userId, unsigned char outBuf[6], struct sockaddr_in* addr, const char* netInterfaceName )
{
  mgr_t*  p;
  sock_t* s;
  rc_t    rc;
  
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;
  
  return _get_info(s->sockH, outBuf, addr, netInterfaceName );
}

cw::rc_t cw::sock::initAddr( const char* addrStr, portNumber_t portNumber, struct sockaddr_in* retAddrPtr )
{
  return _initAddr(addrStr,portNumber,retAddrPtr);
}
      
cw::rc_t cw::sock::addrToString( const struct sockaddr_in* addr, char* buf, unsigned bufN )
{
  rc_t rc = kOkRC;
  
  errno = 0;
  
  if( inet_ntop(AF_INET, &(addr->sin_addr),  buf, bufN) == nullptr)
  {
    rc = cwLogSysError(kOpFailRC,errno, "Network address to string conversion failed." );
    goto errLabel;
  }
  
  buf[bufN-1]=0;
 errLabel:
  return rc;
}
      
bool cw::sock::addrIsEqual( const struct sockaddr_in* a0, const struct sockaddr_in* a1 )
{
  return a0->sin_family == a1->sin_family 
    &&   a0->sin_port   == a1->sin_port 
    &&   memcmp(&a0->sin_addr,&a1->sin_addr,sizeof(a0->sin_addr))==0;  
}
      
const char* cw::sock::hostName( handle_t h, unsigned userId )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
  
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return nullptr;

  errno = 0;

  if( gethostname(s->hnameBuf,HOST_NAME_MAX) != 0 )
  {
    cwLogSysError(kOpFailRC,errno, "gethostname() failed." );
    return nullptr;
  }
  
  s->hnameBuf[HOST_NAME_MAX] = 0;
  return s->hnameBuf;
}

const char* cw::sock::ipAddress( handle_t h, unsigned userId )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
  
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return nullptr;
  
  return s->ntopBuf;
}

unsigned    cw::sock::inetAddress( handle_t h, unsigned userId )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
  
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return 0;

  return  s->localSockAddr.sin_addr.s_addr;
}

cw::sock::portNumber_t    cw::sock::port( handle_t h, unsigned userId )
{
  rc_t    rc = kOkRC;
  mgr_t*  p;
  sock_t* s;
  
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;
  
  return  ntohs(s->localSockAddr.sin_port);
}

cw::rc_t cw::sock::peername( handle_t h, unsigned userId, struct sockaddr_in* addr )
{
  rc_t      rc = kOkRC;
  socklen_t n  = sizeof(struct sockaddr_in);
  mgr_t*    p;
  sock_t*   s;
  
  if((rc = _getMgrAndSocket(h, userId, p, s )) != kOkRC )
    return rc;
  
  if( getpeername(s->sockH, (struct sockaddr*)addr, &n)  == cwSOCKET_SYS_ERR )
    return cwLogSysError(kOpFailRC,errno,"Get peer name failed.");

  addr->sin_port = ntohs(addr->sin_port);

  return rc;
}

cw::rc_t cw::sock::get_info( const char* netInterfaceName, unsigned char mac[6], char* host, unsigned hostN, struct sockaddr_in* addr )
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


//===============================================================================================
namespace cw
{
  namespace socksrv
  {
    typedef struct socksrv_str
    {
      sock::handle_t   sockMgrH;
      thread::handle_t thH;
      unsigned         timeOutMs;
    } socksrv_t;

    socksrv_t* _handleToPtr( handle_t h )
    {
      return handleToPtr<handle_t,socksrv_t>(h);
    }

    rc_t _destroyMgrSrv( socksrv_t* p )
    {
      rc_t rc = kOkRC;

      // destroy the thread
      if((rc = destroy(p->thH)) != kOkRC )
        return rc;

      // destroy the socket manager
      if((rc = sock::destroyMgr(p->sockMgrH)) != kOkRC )
        goto errLabel;

      mem::release(p);

    errLabel:
      return rc;
    }

    bool _threadFunc( void* arg )
    {
      socksrv_t* p         = (socksrv_t*)arg;
      unsigned   readByteN = 0;
      rc_t       rc;

      // 
      if((rc = sock::receive_all( p->sockMgrH, p->timeOutMs, readByteN)) != kOkRC && rc != kTimeOutRC )
      {
        cwLogError(rc,"Socket Srv receive failed.");
        return false;
      }
      
      return true;
    }

    // Callback thread used by socksrv::test() below
    void _socketTestCbFunc( void* cbArg, sock::cbId_t cbId, unsigned userId, unsigned connId, const void* byteA, unsigned byteN, const struct sockaddr_in* srcAddr )
    {
      rc_t rc;
      char addr[ INET_ADDRSTRLEN+1 ];

      printf("type:%i user:%i conn:%i ", cbId, userId, connId );

      if( srcAddr != nullptr )
        if((rc = sock::addrToString( srcAddr, addr, INET_ADDRSTRLEN )) == kOkRC )
        {
          printf("from  %s ", addr  );
        }

      if( byteA != nullptr )
        printf(" : %s ", (const char*)byteA);

      printf("\n");
    }
     
  }
}

cw::rc_t cw::socksrv::createMgrSrv(  handle_t& hRef, unsigned timeOutMs, unsigned recvBufByteN, unsigned maxSocketN )
{
  cw::rc_t rc = kOkRC;
  if((rc = destroyMgrSrv(hRef)) != kOkRC )
    return rc;

  // allocate the object
  socksrv_t* p = mem::allocZ<socksrv_t>();

  // create the socket manager
  if((rc = createMgr(  p->sockMgrH, recvBufByteN, maxSocketN )) != kOkRC )
    goto errLabel;

  // create the thread
  if((rc = thread::create( p->thH, _threadFunc, p)) != kOkRC )
    goto errLabel;

  p->timeOutMs = timeOutMs;
  
  hRef.set(p);

 errLabel:
  return rc;
}

cw::rc_t cw::socksrv::destroyMgrSrv( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return kOkRC;

  socksrv_t* p = _handleToPtr(hRef);
  if((rc = _destroyMgrSrv(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::sock::handle_t cw::socksrv::mgrHandle( handle_t h )
{
  socksrv_t* p = _handleToPtr(h);
  return p->sockMgrH;
}

cw::rc_t cw::socksrv::start( handle_t h )
{
  socksrv_t* p = _handleToPtr(h);
  return thread::unpause( p->thH );
}

cw::rc_t cw::socksrv::stop(  handle_t h )
{
  socksrv_t* p = _handleToPtr(h);
  return thread::pause( p->thH );
}

cw::rc_t cw::socksrv::test(  sock::portNumber_t localPort, const char* remoteAddrIp, sock::portNumber_t remotePort, unsigned flags )
{
  handle_t       h;
  rc_t           rc           = kOkRC;
  unsigned       timeOutMs    = 50;
  unsigned       recvBufByteN = 2048;
  unsigned       maxSocketN   = 10;
  unsigned       userId       = 10;
  unsigned       sockFlags    = sock::kNonBlockingFl | flags;
  bool           serverFl     = remoteAddrIp == nullptr;
  const unsigned sbufN        = 31;
  char           sbuf[ sbufN+1 ];

  if( serverFl )
    printf("Server listening on port: %i\n", localPort );
  else
    printf("Client connecting to server %s:%i\n", remoteAddrIp,remotePort);
    

  // create the socket manager
  if((rc = createMgrSrv(h, timeOutMs, recvBufByteN, maxSocketN )) != kOkRC )
    return cwLogError(rc,"Socket server create failed.");

  // start the socket manager
  if((rc = start(h)) != kOkRC )
  {
    cwLogError(rc,"Socker server start failed.");
    goto errLabel;
  }
  
  // create a socket
  if((rc = create( mgrHandle(h), userId, localPort, sockFlags, timeOutMs, _socketTestCbFunc, nullptr, remoteAddrIp, remotePort)) != kOkRC )
  {
    cwLogError(rc,"Socket server socket create failed.");
    goto errLabel;
  }

  printf("'quit' to exit\n");

  // readline loop
  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      printf("Sending:%s",sbuf);

      // send a message to the remote socket
      if( sock::send( mgrHandle(h), userId, -1, sbuf, strlen(sbuf)+1 ) != kOkRC )
        printf("Send failed.");

      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }
  
 errLabel:

  rc_t rc1 = kOkRC;

  // destroy the socket
  if( h.isValid() )
    rc1 = destroy( mgrHandle(h), userId );

  // destroy the socket manager
  rc_t rc2 = destroyMgrSrv(h);

  return rcSelect(rc,rc1,rc2);
}

cw::rc_t cw::socksrv::testMain(  bool tcpFl, sock::portNumber_t localPort, const char* remoteAddrIp, sock::portNumber_t remotePort )
{
  unsigned flags = 0;

  if( tcpFl )
  {
    flags |= sock::kTcpFl | sock::kStreamFl | sock::kReuseAddrFl | sock::kReusePortFl;

    if( remoteAddrIp == nullptr )
      flags |= sock::kListenFl;
  }
  
  return test(localPort,remoteAddrIp,remotePort,flags);
}
