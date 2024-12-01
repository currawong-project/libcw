//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
#include "cwTcpSocket.h"
#include "cwTcpSocketSrv.h"

namespace cw
{
  namespace net
  {
    namespace srv
    {
      typedef struct socksrv_str
      {
        socket::handle_t sockH;
        thread::handle_t threadH;
        cbFunc_t         cbFunc;
        void*            cbArg;
        unsigned         timeOutMs;
        char*            recvBuf;
        unsigned         recvBufByteCnt;
        unsigned         flags;
      } socksrv_t;

      inline socksrv_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,socksrv_t>(h); }

      rc_t _destroy( socksrv_t* p )
      {
        rc_t rc;
        
        if((rc = thread::destroy(p->threadH)) != kOkRC )
          return rc;
        
        if((rc = socket::destroy(p->sockH)) != kOkRC )
          return rc;

        mem::release(p->recvBuf);
        mem::release(p);

        return rc;
      }

      bool _threadFunc( void* arg )
      {
        rc_t               rc         = kOkRC;
        socksrv_t*         p          = static_cast<socksrv_t*>(arg);
        unsigned           rcvByteCnt = 0;
        struct sockaddr_in fromAddr;

        
        if( cwIsFlag(p->flags,kUseAcceptFl) && socket::isConnected(p->sockH)==false )
        {
          rc = socket::accept( p->sockH );
        }
        else
        {
          // if this is a TCP socket that is not connected
          if(  cwIsFlag(socket::flags( p->sockH),socket::kTcpFl) &&  socket::isConnected(p->sockH) == false )
          {
            sleepMs(p->timeOutMs);
          }
          else
          {
            // this is a connected TCP socket or UDP socket
            if( cwIsFlag(p->flags,kUseRecvFromFl) )
            {
              rc = recv_from(p->sockH, p->recvBuf, p->recvBufByteCnt, &rcvByteCnt, &fromAddr );
            }
            else
            {
              rc = select_receive(p->sockH, p->recvBuf, p->recvBufByteCnt, p->timeOutMs, &rcvByteCnt, &fromAddr );
            }

            if( (p->cbFunc != nullptr) && ((rc == kOkRC && rcvByteCnt>0) || (cwIsFlag(p->flags,kRecvTimeOutFl) && rc == kTimeOutRC)))
            {
              void*               buf  = rc == kTimeOutRC ? nullptr : p->recvBuf;
              unsigned            n    = rc == kTimeOutRC ? 0       : rcvByteCnt;
              struct sockaddr_in* addr = rc == kTimeOutRC ? nullptr : &fromAddr;
              p->cbFunc( p->cbArg, buf, n, addr );
            }
          } 
        }
        return true;
      }       
    }
  }
}

cw::rc_t cw::net::srv::create(
  handle_t&            hRef,
  socket::portNumber_t port,
  unsigned             flags,
  unsigned             srvFlags,
  cbFunc_t             cbFunc,
  void*                cbArg,
  unsigned             recvBufByteCnt,
  unsigned             timeOutMs,
  const char*          remoteAddr,
  socket::portNumber_t remotePort,
  const char*          localAddr)
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  socksrv_t* p = mem::allocZ<socksrv_t>();

  if((rc = socket::create( p->sockH, port, flags, timeOutMs, remoteAddr, remotePort, localAddr )) != kOkRC )
    goto errLabel;

  if((rc = thread::create( p->threadH, _threadFunc, p, "tcp_sock_srv" )) != kOkRC )
    goto errLabel;

  p->flags          = srvFlags;
  p->recvBuf        = mem::allocZ<char>( recvBufByteCnt );
  p->recvBufByteCnt = recvBufByteCnt;
  p->cbFunc         = cbFunc;
  p->cbArg          = cbArg;
  p->timeOutMs      = timeOutMs;

 errLabel:
  if( rc == kOkRC )
    hRef.set(p);
  else
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::net::srv::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  socksrv_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;
}

cw::thread::handle_t cw::net::srv::threadHandle( handle_t h )
{
  socksrv_t* p = _handleToPtr(h);
  return p->threadH;
}
cw::net::socket::handle_t cw::net::srv::socketHandle( handle_t h )
{
  socksrv_t* p = _handleToPtr(h);
  return p->sockH;
}

cw::rc_t cw::net::srv::start( handle_t h )
{
  socksrv_t* p = _handleToPtr(h);
  return thread::pause( p->threadH, thread::kWaitFl );
}

cw::rc_t cw::net::srv::pause( handle_t h )
{
  socksrv_t* p = _handleToPtr(h);
  return thread::pause( p->threadH, thread::kWaitFl|thread::kPauseFl );
}

cw::rc_t cw::net::srv::send( handle_t h, const void* data, unsigned dataByteCnt )
{
  socksrv_t* p = _handleToPtr(h);
  return socket::send(p->sockH,data,dataByteCnt);
}
      
cw::rc_t cw::net::srv::send( handle_t h, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr )
{
  socksrv_t* p = _handleToPtr(h);
  return socket::send(p->sockH,data,dataByteCnt,remoteAddr);
}

cw::rc_t cw::net::srv::send( handle_t h, const void* data, unsigned dataByteCnt, const char* remoteAddr, socket::portNumber_t remotePort )
{
  socksrv_t* p = _handleToPtr(h);
  return socket::send(p->sockH,data,dataByteCnt,remoteAddr,remotePort);
}
