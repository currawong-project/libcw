#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"

#include "cwThread.h"

#include "cwTcpSocket.h"
#include "cwTcpSocketSrv.h"
#include "cwTcpSocketTest.h"




namespace cw
{
  namespace net
  {
    namespace socket
    {
      typedef struct app_str
      {
        unsigned         recvBufByteN;
        handle_t         sockH;
        thread::handle_t threadH;
        unsigned         cbN;
      } app_t;
      
      bool _threadFunc( void* arg )
      {
        rc_t               rc;
        app_t*             app  = static_cast<app_t*>(arg);
        struct sockaddr_in fromAddr;
        char               addrBuf[ INET_ADDRSTRLEN ];
        char               buf[ app->recvBufByteN ];
        unsigned           recvBufByteN = 0;
        
        if((rc = recieve( app->sockH, buf, app->recvBufByteN, &recvBufByteN, &fromAddr )) == kOkRC )
        {
          addrToString( &fromAddr, addrBuf );
          printf("%i %s from %s\n", recvBufByteN, buf, addrBuf );
        }

        app->cbN += 1;
        if( app->cbN % 10 == 0)
        {
          printf(".");
          fflush(stdout);
        }
          
        return true;
      }
    }
  }
}

cw::rc_t cw::net::socket::test( portNumber_t localPort, const char* remoteAddr, portNumber_t remotePort )
{
  rc_t           rc;
  unsigned       timeOutMs = 100;
  const unsigned sbufN     = 31;
  char           sbuf[ sbufN+1 ];
  app_t          app;

  app.cbN          = 0;
  app.recvBufByteN = sbufN+1;
  
  if((rc = create(app.sockH,localPort, kBlockingFl,timeOutMs, NULL, kInvalidPortNumber )) != kOkRC )
    return rc;

  if((rc = thread::create( app.threadH, _threadFunc, &app )) != kOkRC )
    goto errLabel;

  if((rc = thread::unpause( app.threadH )) != kOkRC )
    goto errLabel;

  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      printf("Sending:%s",sbuf);
      send(app.sockH, sbuf, strlen(sbuf)+1, remoteAddr, remotePort );
      
      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

 errLabel:
  rc_t rc0 = thread::destroy(app.threadH);

  rc_t rc1 = destroy(app.sockH);

  return rcSelect(rc,rc0,rc1);
}

namespace cw
{
  namespace net
  {
    namespace srv
    {
     typedef struct app_str
     {
       handle_t srvH;
       unsigned cbN;
     } app_t;
      
     void srvRecieveCallback( void* arg, const char* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr )
     {
       app_t* p = static_cast<app_t*>(arg);
       char addrBuf[ INET_ADDRSTRLEN ];
       socket::addrToString( fromAddr, addrBuf, INET_ADDRSTRLEN );
       p->cbN += 1;
       printf("%i %s %s", p->cbN, addrBuf, data );
     }
    }      
  }
}

cw::rc_t cw::net::srv::test( socket::portNumber_t localPort, const char* remoteAddr, socket::portNumber_t remotePort )
{
  rc_t           rc;
  unsigned       recvBufByteCnt = 1024;
  unsigned       timeOutMs      = 100;
  const unsigned sbufN          = 31;
  char           sbuf[ sbufN+1 ];
  app_t          app;
  app.cbN = 0;
  
  if((rc = srv::create(app.srvH, localPort, socket::kBlockingFl, srvRecieveCallback, &app, recvBufByteCnt, timeOutMs, NULL, socket::kInvalidPortNumber )) != kOkRC )
    return rc;

  if((rc = srv::start( app.srvH )) != kOkRC )
    goto errLabel;

  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      printf("Sending:%s",sbuf);
      send(app.srvH, sbuf, strlen(sbuf)+1, remoteAddr, remotePort );
      
      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

 errLabel:
  rc_t rc0 = destroy(app.srvH);

  return rcSelect(rc,rc0);  
}
