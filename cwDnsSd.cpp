#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"

#include "cwThread.h"
#include "cwTcpSocket.h"
#include "cwTcpSocketSrv.h"

#include "cwDnsSd.h"
#include "cwUtility.h"
#include "dns_sd/dns_sd.h"
#include "dns_sd/fader.h"

#define   MDNS_PORT 5353
#define   MDNS_IP   "224.0.0.251"


namespace cw
{
  namespace net
  {
    namespace dnssd
    {
      typedef struct text_str
      {
        char*            text;
        struct text_str* link;
      } text_t;

      typedef struct dnssd_str
      {
        srv::handle_t udpH;
        srv::handle_t tcpH;
        unsigned      udpRecvBufByteN;
        unsigned      tcpRecvBufByteN;
        text_t*       textL;
        dns_sd*       dnsSd;
        fader*        fdr;

        char*         serviceName;
        char*         serviceType;
        char*         serviceDomain;
        char*         hostName;
        char*         hostIpAddr;
        uint16_t      hostPort;
        unsigned char hostMac[6];

        unsigned cbCnt;
        time::spec_t t0;
        
      } dnssd_t;

      inline dnssd_t* _handleToPtr( handle_t h )
      {
        return handleToPtr<handle_t,dnssd_t>(h);
      }

      rc_t _destroy( dnssd_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->serviceName);
        mem::release(p->serviceType);
        mem::release(p->serviceDomain);
        mem::release(p->hostName);
        mem::release(p->hostIpAddr);
        

        text_t* t0 = p->textL;
        while( t0 != nullptr )
        {
          text_t* t1 = t0->link;
          mem::release(t0->text);
          mem::release(t0);
          t0 = t1;
        }

        srv::destroy( p->tcpH );
        srv::destroy( p->udpH );

        if( p->fdr != nullptr )
        {
          delete p->fdr;
          p->fdr = nullptr;
        }
        
        if( p->dnsSd != nullptr )
        {
          delete p->dnsSd;
          p->dnsSd = nullptr;
        }
        
        if( p->dnsSd != nullptr )
          delete p->dnsSd;
        
        mem::release(p);
        return rc;
      }

      // Called by 'dns_sd' to send UDP messages.
      void udpSendCallback( void* arg, const void* buf, unsigned bufByteN )
      {
        rc_t rc;
        dnssd_t* p = (dnssd_t*)arg;
        //printHex(buf,bufByteN);
        if((rc = srv::send(p->udpH,buf,bufByteN,MDNS_IP,MDNS_PORT)) != kOkRC )
          cwLogError(rc,"UDP send failed.");
      }

      // Called by 'fader' to send TCP messages.
      void tcpSendCallback( void* arg, const void* buf, unsigned bufByteN )
      {
        rc_t rc;
        dnssd_t* p = (dnssd_t*)arg;
        if((rc = srv::send(p->tcpH,buf,bufByteN)) != kOkRC )
          cwLogError(rc,"TCP send failed.");
      }

      // Called by UDP socket with incoming MDNS data.
      void udpReceiveCallback( void* arg, const void* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr )
      {
        dnssd_t* p = static_cast<dnssd_t*>(arg);
        p->dnsSd->receive(data,dataByteCnt);
      }

      // Called by TCP socket with incoming EuCon data
      void tcpReceiveCallback( void* arg, const void* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr )
      {
        dnssd_t* p = static_cast<dnssd_t*>(arg);

        if( dataByteCnt > 0 )
        {
          p->fdr->host_receive( data, dataByteCnt );  
        }

        time::spec_t t1;
        time::get(t1);
        unsigned ms = time::elapsedMs( &p->t0, &t1 );
        if( ms > 50 )
        {
          p->cbCnt+=1;
          p->fdr->tick();
          p->t0 = t1;
        }

        if( p->cbCnt > 20 )
        {
          printf(".");
          fflush(stdout);
          p->cbCnt = 1;
        }
      }

      void _pushTextRecdField( dnssd_t* p, const char* text )
      {
        text_t* t = mem::allocZ<text_t>();
        t->text   = mem::duplStr(text);
        t->link   = p->textL;
        p->textL  = t;            
      }

      rc_t _setTextRecdFieldsV( dnssd_t* p, const char* text,  va_list vl )
      {
        rc_t  rc = kOkRC;
        char* s0;

        if( text != nullptr )
        {
          _pushTextRecdField( p, text );
          
          while( (s0 = va_arg(vl,char*)) != nullptr )
          {
            _pushTextRecdField( p, s0 );
          }
        }
        return rc;
      }

      char* _formatText( dnssd_t* p )
      {
        unsigned n = 0;
        text_t*  t = p->textL;
        for(; t != nullptr; t=t->link )
          n += strlen(t->text) + 1;

        char* s  = (char*)calloc(1,n);
        char* s0 = s;
        
        for(t=p->textL; t != nullptr; t=t->link, s++ )
        {
          strcpy(s,t->text);
          s += strlen(t->text);
          s[0] = '\n';         
        }

        // replace the ending \n with a zero-terminator
        s0[n-1] = '\0';

        return s0;
      }

      rc_t _init( dnssd_t* p )
      {
        rc_t               rc        = kOkRC;
        char*              formatStr = nullptr;
        socket::handle_t   sockH     = srv::socketHandle( p->tcpH );
        struct sockaddr_in hostAddr;

        // get the 32bit host address 
        if((rc = initAddr( sockH, p->hostIpAddr, p->hostPort, &hostAddr )) != kOkRC )
          return rc;
        
        // Get the service 'TXT' fields in the format expected by dns_sd
        formatStr = _formatText(p);

        if( p->dnsSd != nullptr )
          delete p->dnsSd;

        // create the MDNS logic object
        p->dnsSd = new dns_sd(udpSendCallback,p);
        
        // create the Surface logic object
        p->fdr   = new fader(p->hostMac, hostAddr.sin_addr.s_addr, tcpSendCallback, p );
  
        // Setup the internal dnsSd object
        p->dnsSd->setup( p->serviceName, p->serviceType, p->serviceDomain, p->hostName, hostAddr.sin_addr.s_addr, p->hostPort, formatStr );
        
        free(formatStr);

        p->dnsSd->gen_response();

        return rc;
      }
    }
  }
}

cw::rc_t cw::net::dnssd::createV(  handle_t& hRef, const char* name, const char* type, const char* domain, const char* hostName, const char* hostIpAddr, uint16_t hostPort, const unsigned char hostMac[6], const char* text, va_list vl )
{
  rc_t        rc              = kOkRC;
  unsigned    udpRecvBufByteN = 4096;
  unsigned    udpTimeOutMs    = 50;
  unsigned    tcpTimeOutMs    = 50;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  dnssd_t* p = mem::allocZ<dnssd_t>();
  p->udpRecvBufByteN = udpRecvBufByteN;
  
  // create the mDNS UDP socket server
  if((rc = srv::create(
        p->udpH,
        MDNS_PORT,
        socket::kBlockingFl | socket::kReuseAddrFl | socket::kReusePortFl | socket::kMultiCastTtlFl | socket::kMultiCastLoopFl,
        srv::kUseRecvFromFl,
        udpReceiveCallback,
        p,
        p->udpRecvBufByteN,
        udpTimeOutMs,
        NULL,
        socket::kInvalidPortNumber )) != kOkRC )
  {    
    return cwLogError(rc,"mDNS UDP socket create failed.");
  }

  // add the mDNS socket to the multicast group
  if((rc =  join_multicast_group( socketHandle(p->udpH), MDNS_IP )) != kOkRC )
    goto errLabel;
  
  // set the TTL for multicast 
  if((rc = set_multicast_time_to_live( socketHandle(p->udpH), 255 )) != kOkRC )
    goto errLabel;

  // create the service TCP socket server
  if((rc = srv::create(
        p->tcpH,
        hostPort,
        socket::kTcpFl | socket::kBlockingFl | socket::kStreamFl | socket::kListenFl,
        srv::kUseAcceptFl | srv::kRecvTimeOutFl,
        tcpReceiveCallback,
        p,
        p->tcpRecvBufByteN,
        tcpTimeOutMs,
        NULL,
        socket::kInvalidPortNumber,
        hostIpAddr)) != kOkRC )
  {    
    rc = cwLogError(rc,"mDNS TCP socket create failed.");
    goto errLabel;
  }

  p->serviceName   = mem::duplStr(name);
  p->serviceType   = mem::duplStr(type);
  p->serviceDomain = mem::duplStr(domain);
  p->hostName      = mem::duplStr(hostName);
  p->hostIpAddr    = mem::duplStr(hostIpAddr);
  p->hostPort      = hostPort;
  p->dnsSd         = nullptr;
  memcpy(p->hostMac,hostMac,6);
  
  if((rc = _setTextRecdFieldsV( p, text,  vl )) != kOkRC )
    goto errLabel;
  
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::net::dnssd::create(  handle_t& hRef, const char* name, const char* type, const char* domain, const char* hostname, const char* hostIpAddr, uint16_t hostPort, const unsigned char hostMac[6], const char* text, ... )
{
  va_list vl;
  va_start(vl,text);
  rc_t rc = createV( hRef, name, type, domain, hostname, hostIpAddr, hostPort, hostMac, text, vl );
  va_end(vl);
  return rc;
}

cw::rc_t cw::net::dnssd::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return rc;

  dnssd_t* p = _handleToPtr(hRef);
  if((rc = _destroy(p)) != kOkRC )
    return rc;
  
  hRef.clear();
  return rc;
}

cw::net::srv::handle_t cw::net::dnssd::tcpHandle( handle_t h )
{
  dnssd_t* p = _handleToPtr(h);
  return p->tcpH;
}

cw::net::srv::handle_t cw::net::dnssd::udpHandle( handle_t h )
{
  dnssd_t* p = _handleToPtr(h);
  return p->udpH;
}

cw::rc_t cw::net::dnssd::setTextRecdFields(  handle_t h, const char* text, ... )
{
  va_list vl;
  va_start(vl,text);
  dnssd_t* p  = _handleToPtr(h);
  rc_t     rc = _setTextRecdFieldsV(p,text,vl);
  va_end(vl);
  return rc;
}


cw::rc_t cw::net::dnssd::start( handle_t h )
{
  rc_t     rc = kOkRC;
  dnssd_t* p  = _handleToPtr(h);

  time::get(p->t0);
  
  if((rc = _init(p)) != kOkRC )
    return rc;
  
  // start the mDNS socket server
  if((rc = srv::start( p->udpH )) != kOkRC )
    return rc;
  
  // start the TCP socket server
  if((rc = srv::start( p->tcpH )) != kOkRC )
    return rc;
  
  return rc;
}

cw::rc_t cw::net::dnssd::test()
{
  rc_t               rc            = kOkRC;
  const char*        netIFace      = "wlp3s0";
  const char*        serviceName   = "MC Mix - 1";
  const char*        serviceType   = "_EuConProxy._tcp";
  const char*        serviceDomain = "local";
  uint16_t           hostPort      = 49168;
  const unsigned     sbufN         = 31;
  handle_t           h;
  unsigned char      hostMac[6];
  char               sbuf[ sbufN+1 ];
  char*              text0;
  struct sockaddr_in addr;

  // Get the host name and address from the selected network interface
  char hostname[ _POSIX_HOST_NAME_MAX+1 ];
  char hostIpAddr[ INET_ADDRSTRLEN+1 ];

  //memset(hostMac,0,6);
  //memset(hostname,0,_POSIX_HOST_NAME_MAX+1);
  //memset(&addr, 0, sizeof(struct sockaddr_in));
  //memset(hostIpAddr,0,INET_ADDRSTRLEN+1);
  
  if( socket::get_info( netIFace, hostMac, hostname, sizeof(hostname), &addr ) == kOkRC )
  {
    char ip[128];
    memset(ip,0,128);
    socket::addrToString(&addr,hostIpAddr,sizeof(hostIpAddr));
    printf("%s %s 0x%x %02x:%02x:%02x:%02x:%02x:%02x\n", hostname, ip, addr.sin_addr.s_addr, hostMac[0],hostMac[1],hostMac[2],hostMac[3],hostMac[4],hostMac[5] );
  }

  //
  // Override the host name and mac address to match the example Wireshark captures
  //
  strcpy(hostname,"Euphonix-MC-0090D580F4DE");
  unsigned char tmp_mac[] = { 0x00, 0x90, 0xd5, 0x80, 0xf4, 0xde };
  memcpy(hostMac,tmp_mac,6);
      

  // create the DNS-SD server
  if((rc = create(  h, serviceName, serviceType, serviceDomain, hostname, hostIpAddr, hostPort, hostMac, nullptr, nullptr )) != kOkRC )
    return cwLogError(rc,"Unable to create DNS-SD server.");

  // form the 'lmac=38-C9-86-37-44-E7'
  text0 = mem::printf<char>(nullptr,"lmac=%02x-%02x-%02x-%02x-%02x-%02x",hostMac[0],hostMac[1],hostMac[2],hostMac[3],hostMac[4],hostMac[5]);

  // set the DNS-SD 'SRV' text fields
  if((rc = setTextRecdFields( h, "dummy=0", text0, nullptr )) != kOkRC )
    goto errLabel;

  // start the DNS-SD server
  if((rc = start( h )) != kOkRC )
    goto errLabel;
  
  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {

      if( strcmp(sbuf,"quit\n") == 0 )
        break;
        
    }
  }

 errLabel:
  mem::release(text0);
  rc = destroy(h);
  
  return rc;
}

