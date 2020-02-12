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
        const char*      remoteAddr;
        unsigned         remotePort;
        unsigned         recvBufByteN;
        handle_t         sockH;
        thread::handle_t threadH;
        unsigned         cbN;
        bool             serverFl;
      } app_t;

      bool _dgramThreadFunc( void* arg )
      {
        rc_t               rc;
        app_t*             app  = static_cast<app_t*>(arg);
        struct sockaddr_in fromAddr;
        char               addrBuf[ INET_ADDRSTRLEN ];
        char               buf[ app->recvBufByteN ];
        unsigned           recvBufByteN = 0;
        
        if((rc = receive( app->sockH, buf, app->recvBufByteN, &recvBufByteN, &fromAddr )) == kOkRC )
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
      
      bool _tcpStreamThreadFunc( void* arg )
      {
        rc_t               rc;
        app_t*             app  = static_cast<app_t*>(arg);
        char               buf[ app->recvBufByteN ];
        unsigned           recvBufByteN = 0;

        if( isConnected(app->sockH) == false )
        {
          if( app->serverFl )
          {
            if((rc = accept( app->sockH )) == kOkRC )
            {
              printf("Server connected.\n");
            }
          }
          else
          {
            sleepMs(50);
          }
        }
        else
        {              
          if((rc = receive( app->sockH, buf, app->recvBufByteN, &recvBufByteN, nullptr )) == kOkRC )
          {
            // if the server disconnects then recvBufByteN 
            if( !isConnected( app->sockH) )
            {
              printf("Disconnected.");
            }
            else
            {
              printf("%i %s\n", recvBufByteN, buf );
            }
          }
        }

        // count the number of callbacks
        app->cbN += 1;
        if( app->cbN % 10 == 0)
        {
          // print '+' when the server is not connected.
          printf("%s", isConnected(app->sockH) == false ? "+" : ".");
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

  if((rc = thread::create( app.threadH, _dgramThreadFunc, &app )) != kOkRC )
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


cw::rc_t cw::net::socket::test_tcp( portNumber_t localPort, const char* remoteAddr, portNumber_t remotePort, bool dgramFl, bool serverFl )
{
  rc_t           rc;
  unsigned       timeOutMs = 100;
  const unsigned sbufN     = 31;
  char           sbuf[ sbufN+1 ];
  app_t          app;
  bool           streamFl = !dgramFl;
  bool           clientFl = !serverFl;
  unsigned       flags = kTcpFl | kBlockingFl;

  app.remoteAddr   = remoteAddr;
  app.remotePort   = remotePort;
  app.cbN          = 0;
  app.recvBufByteN = sbufN+1;
  app.serverFl     = serverFl;

  if( serverFl && streamFl )
    flags |= kListenFl;
  
  if( streamFl )
    flags |= kStreamFl;

  // create the socket
  if((rc = create(app.sockH,localPort, flags,timeOutMs, NULL, kInvalidPortNumber )) != kOkRC )
    return rc;

  // create the listening thread (which is really only used by the server)
  if((rc = thread::create( app.threadH, streamFl ? _tcpStreamThreadFunc : _dgramThreadFunc, &app )) != kOkRC )
    goto errLabel;

  // if this is a streaming client then connect to the server (which must have already been started)
  if( streamFl && clientFl )
  {
    // note that this creates a bi-directional stream
    if((rc = connect(app.sockH,remoteAddr,remotePort)) != kOkRC )
      goto errLabel;    
  }

  printf("Starting node ....\n");

  // start the thread
  if((rc = thread::unpause( app.threadH )) != kOkRC )
    goto errLabel;

  
  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      if( strcmp(sbuf,"quit\n") == 0)
        break;

      if( streamFl )
      {  
        // when using streams no remote address is necessary
        printf("Sending:%s",sbuf);      
        send(app.sockH, sbuf, strlen(sbuf)+1 );
      }
      else
      {
        // when using dgrams the dest. address is required
        printf("Sending:%s",sbuf);      
        send(app.sockH, sbuf, strlen(sbuf)+1, remoteAddr, remotePort);
      }
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
      
      void srvReceiveCallback( void* arg, const void* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr )
      {
        app_t* p = static_cast<app_t*>(arg);
        char addrBuf[ INET_ADDRSTRLEN ];
        socket::addrToString( fromAddr, addrBuf, INET_ADDRSTRLEN );
        p->cbN += 1;
        printf("%i %s %s\n", p->cbN, addrBuf, (const char*)data );
      }
    }      
  }
}

cw::rc_t cw::net::srv::test_udp_srv( socket::portNumber_t localPort, const char* remoteAddr, socket::portNumber_t remotePort )
{
  rc_t           rc;
  unsigned       recvBufByteCnt = 1024;
  unsigned       timeOutMs      = 100;
  const unsigned sbufN          = 31;
  char           sbuf[ sbufN+1 ];
  app_t          app;
  app.cbN = 0;
  
  if((rc = srv::create(app.srvH,
        localPort,
        socket::kBlockingFl,
        0,
        srvReceiveCallback,
        &app,
        recvBufByteCnt,
        timeOutMs,
        nullptr,
        socket::kInvalidPortNumber )) != kOkRC )
    
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

cw::rc_t cw::net::srv::test_tcp_srv( socket::portNumber_t localPort, const char* remoteAddr, socket::portNumber_t remotePort )
{
  rc_t           rc;
  unsigned       recvBufByteCnt = 1024;
  unsigned       timeOutMs      = 100;
  const unsigned sbufN          = 31;
  char           sbuf[ sbufN+1 ];
  app_t          app;
  app.cbN = 0;
  
  if((rc = srv::create(app.srvH,
        localPort,
        socket::kBlockingFl | socket::kTcpFl | socket::kStreamFl,
        0,
        srvReceiveCallback,
        &app,
        recvBufByteCnt,
        timeOutMs,
        remoteAddr,
        remotePort )) != kOkRC )
    
    return rc;

  if((rc = srv::start( app.srvH )) != kOkRC )
    goto errLabel;

  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      printf("Sending:%s",sbuf);
      send(app.srvH, sbuf, strlen(sbuf)+1 );
      
      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

 errLabel:
  rc_t rc0 = destroy(app.srvH);

  return rcSelect(rc,rc0);  
}



