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
        bool             serverFl;
        bool             readyFl;
        bool             connectedFl;
      } app_t;

      bool _dgramThreadFunc( void* arg )
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
      
      bool _tcpStreamThreadFunc( void* arg )
      {
        rc_t               rc;
        app_t*             app  = static_cast<app_t*>(arg);
        char               buf[ app->recvBufByteN ];
        unsigned           recvBufByteN = 0;

        if( !app->serverFl )
        {
          // the client node has nothing to do because it does not receive (it only sends)
          sleepMs(50);
        }
        else
        {
          if( app->connectedFl == false )
          {
            if((rc = accept( app->sockH )) == kOkRC )
            {
              app->connectedFl = true;
              printf("Server connected.\n");
            }
          }
          else
          {              
            if((rc = recieve( app->sockH, buf, app->recvBufByteN, &recvBufByteN, nullptr )) == kOkRC )
            {
              // if the server disconnects then recvBufByteN 
              if( recvBufByteN==0 )
              {
                app->connectedFl = false;
              }
              else
              {
                printf("%i %s\n", recvBufByteN, buf );
              }
            }
          }
        }      

        // count the number of callbacks
        app->cbN += 1;
        if( app->cbN % 10 == 0)
        {
          // print '+' when the server is not connected.
          printf("%s", app->serverFl && app->connectedFl == false ? "+" : ".");
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
  
  app.cbN          = 0;
  app.recvBufByteN = sbufN+1;
  app.serverFl     = serverFl;
  app.readyFl      = false;
  app.connectedFl  = false;

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

  // if this is the client then connect to the server (which must have already been started)
  if( streamFl && clientFl )
  {
    if((rc = connect(app.sockH,remoteAddr,remotePort)) != kOkRC )
      goto errLabel;
    
    app.connectedFl = true;
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

      // when using streams only the client can send
      if( streamFl & clientFl )
      {  
        printf("Sending:%s",sbuf);      
        send(app.sockH, sbuf, strlen(sbuf)+1 );
      }

      // when using dgrams the dest. address is need to send
      if( dgramFl )
      {
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
      
      void srvRecieveCallback( void* arg, const void* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr )
      {
        app_t* p = static_cast<app_t*>(arg);
        char addrBuf[ INET_ADDRSTRLEN ];
        socket::addrToString( fromAddr, addrBuf, INET_ADDRSTRLEN );
        p->cbN += 1;
        printf("%i %s %s", p->cbN, addrBuf, (const char*)data );
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



