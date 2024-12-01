//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTest.h"
#include "cwObject.h"

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
          cwLogPrint("%i %s from %s\n", recvBufByteN, buf, addrBuf );
        }

        app->cbN += 1;
        if( app->cbN % 10 == 0)
        {
          cwLogPrint(".");
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
              cwLogPrint("Server connected.\n");
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
              cwLogPrint("Disconnected.");
            }
            else
            {
              cwLogPrint("%i %s\n", recvBufByteN, buf );
            }
          }
        }

        // count the number of callbacks
        app->cbN += 1;
        if( app->cbN % 10 == 0)
        {
          // print '+' when the server is not connected.
          cwLogPrint("%s", isConnected(app->sockH) == false ? "+" : ".");
          fflush(stdout);
        }
          
        return true;
      }
    }
  }
}

cw::rc_t cw::net::socket::test_udp( const object_t* cfg  )
{
  rc_t           rc         = kOkRC;
  unsigned       timeOutMs  = 100;
  const char*    remoteAddr = "12.0.0.1";
  portNumber_t   remotePort = 5687;
  portNumber_t   localPort  = 5688;
  const unsigned sbufN      = 31;
  char           sbuf[ sbufN+1 ];
  app_t          app;

  app.cbN          = 0;
  app.recvBufByteN = sbufN+1;

  if((rc = cfg->getv("localPort",localPort,
                     "remoteAddr",remoteAddr,
                     "remotePort",remotePort)) != kOkRC )
  {
    cwLogError(rc,"Arg. parse failed.");
    goto errLabel;
  }
  
  if((rc = create(app.sockH,localPort, kBlockingFl,timeOutMs, NULL, kInvalidPortNumber )) != kOkRC )
    return rc;

  if((rc = thread::create( app.threadH, _dgramThreadFunc, &app, "tcp_sock_test_tcp" )) != kOkRC )
    goto errLabel;

  if((rc = thread::unpause( app.threadH )) != kOkRC )
    goto errLabel;

  cwLogPrint("Type a message to send or 'quit' to exit.\n");
             
  while( true )
  {
    cwLogPrint("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      cwLogPrint("Sending:%s",sbuf);
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


cw::rc_t cw::net::socket::test_tcp( const object_t* cfg )
{
  rc_t           rc = kOkRC;
  unsigned       timeOutMs = 100;
  const unsigned sbufN     = 31;
  char           sbuf[ sbufN+1 ];
  app_t          app;
  bool           serverFl = false;
  bool            dgramFl = true;
  bool           streamFl = !dgramFl;
  bool           clientFl = !serverFl;
  unsigned       flags = kTcpFl | kBlockingFl;
  portNumber_t localPort = 5687;
  const char*  remoteAddr = "127.0.0.1";
  portNumber_t remotePort = 5688; 

  app.remoteAddr   = remoteAddr;
  app.remotePort   = remotePort;
  app.cbN          = 0;
  app.recvBufByteN = sbufN+1;
  app.serverFl     = serverFl;


  if((rc = cfg->getv("localPort",localPort,
                     "remoteAddr",remoteAddr,
                     "remotePort",remotePort,
                     "serverFl",serverFl,
                     "dgramFl",dgramFl,
                     "timeOutMs",timeOutMs )) != kOkRC )
  {
    rc = cwLogError(rc,"Arg. parse failed.");
    goto errLabel;
  }

  streamFl = !dgramFl;
  clientFl = !serverFl;
  

  if( serverFl && streamFl )
    flags |= kListenFl;
  
  if( streamFl )
    flags |= kStreamFl;

  // create the socket
  if((rc = create(app.sockH,localPort, flags,timeOutMs, NULL, kInvalidPortNumber )) != kOkRC )
    return rc;

  // create the listening thread (which is really only used by the server)
  if((rc = thread::create( app.threadH, streamFl ? _tcpStreamThreadFunc : _dgramThreadFunc, &app, "tcp_sock_test" )) != kOkRC )
    goto errLabel;

  // if this is a streaming client then connect to the server (which must have already been started)
  if( streamFl && clientFl )
  {
    // note that this creates a bi-directional stream
    if((rc = connect(app.sockH,remoteAddr,remotePort)) != kOkRC )
      goto errLabel;    
  }

  cwLogPrint("Starting %s %s node ....\n",streamFl ? "TCP" : "UDP", serverFl ? "server" : "client");
  cwLogPrint("'quit'=quit\n");
  
  // start the thread
  if((rc = thread::unpause( app.threadH )) != kOkRC )
    goto errLabel;

  
  while( true )
  {
    cwLogPrint("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      if( strcmp(sbuf,"quit\n") == 0)
        break;

      if( streamFl )
      {  
        // when using streams no remote address is necessary
        cwLogPrint("Sending:%s",sbuf);      
        send(app.sockH, sbuf, strlen(sbuf)+1 );
      }
      else
      {
        // when using dgrams the dest. address is required
        cwLogPrint("Sending:%s",sbuf);      
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
        struct sockaddr_in remoteAddr;
      } app_t;
      
      void srvReceiveCallback( void* arg, const void* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr )
      {
        app_t* p = static_cast<app_t*>(arg);
        
        send(p->srvH, data, dataByteCnt, &p->remoteAddr );

        
        char addrBuf[ INET_ADDRSTRLEN ];
        socket::addrToString( fromAddr, addrBuf, INET_ADDRSTRLEN );
        p->cbN += 1;
        cwLogPrint("%i %s %s\n", p->cbN, addrBuf, (const char*)data );


      }
    }      
  }
}

cw::rc_t cw::net::srv::test_udp_srv( const object_t* cfg )
{
  rc_t                 rc             = kOkRC;
  unsigned             recvBufByteCnt = 1024;
  unsigned             timeOutMs      = 100;
  socket::portNumber_t localPort      = 5687;
  const char*          remoteAddr     = nullptr;
  socket::portNumber_t remotePort     = 5688;
  const unsigned       sbufN          = 31;
  char                 sbuf[ sbufN+1 ];  
  app_t                app;
  
  app.cbN = 0;

  if((rc = cfg->getv("localPort",localPort,
                     "remoteAddr",remoteAddr,
                     "remotePort",remotePort)) != kOkRC )
  {
    rc = cwLogError(rc,"Arg. parse failed.");
    goto errLabel;
  }
  
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
  {
    return rc;
  }

  if((rc = socket::initAddr( remoteAddr, remotePort, &app.remoteAddr )) != kOkRC )
  {
    cwLogError(rc,"Address initialization failed.");
    goto errLabel;
  }
  
  if((rc = srv::start( app.srvH )) != kOkRC )
    goto errLabel;

  while( true )
  {
    cwLogPrint("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      cwLogPrint("Sending:%s",sbuf);
      send(app.srvH, sbuf, strlen(sbuf)+1, remoteAddr, remotePort );
      
      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

 errLabel:
  rc_t rc0 = destroy(app.srvH);

  return rcSelect(rc,rc0);  
}

cw::rc_t cw::net::srv::test_tcp_srv( const object_t* cfg )
{
  rc_t                 rc             = kOkRC;
  unsigned             recvBufByteCnt = 1024;
  unsigned             timeOutMs      = 100;
  socket::portNumber_t localPort      = 5687;
  const char*          remoteAddr     = nullptr;
  socket::portNumber_t remotePort     = 5688;  
  const unsigned       sbufN          = 31;
  char                 sbuf[ sbufN+1 ];
  app_t                app;

  
  app.cbN = 0;

  if((rc = cfg->getv("localPort",localPort,
                     "remoteAddr",remoteAddr,
                     "remotePort",remotePort)) != kOkRC )
  {
    rc = cwLogError(rc,"Arg. parse failed.");
    goto errLabel;
  }
  
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
  {
    return rc;
  }
  
  if((rc = srv::start( app.srvH )) != kOkRC )
    goto errLabel;

  while( true )
  {
    cwLogPrint("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      cwLogPrint("Sending:%s",sbuf);
      send(app.srvH, sbuf, strlen(sbuf)+1 );
      
      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

 errLabel:
  rc_t rc0 = destroy(app.srvH);

  return rcSelect(rc,rc0);  
}



