//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwDnsSd_h
#define cwDnsSd_h

namespace cw
{
  namespace net
  {
    namespace dnssd
    {

      typedef handle<struct dnssd_str> handle_t;

      rc_t createV(  handle_t& hRef, const char* name, const char* type, const char* domain, const char* hostname, const char* hostIpAddr, uint16_t hostPort, const unsigned char hostMac[6], const char* text, va_list vl );
      rc_t create(   handle_t& hRef, const char* name, const char* type, const char* domain, const char* hostname, const char* hostIpAddr, uint16_t hostPort, const unsigned char hostMac[6], const char* text, ... );

      rc_t destroy( handle_t& hRef );

      srv::handle_t tcpHandle( handle_t h );
      srv::handle_t udpHandle( handle_t h );

      rc_t setTextRecdFieldsV( handle_t h, const char* text,  va_list vl );
      rc_t setTextRecdFields(  handle_t h, const char* text, ... );
            
      rc_t start( handle_t h );

      rc_t test();
      
    }
  }
}

#endif
