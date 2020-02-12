#include "dns_sd.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "dns_sd_const.h"
#include "dns_sd_print.h"


#define DNS_SD_SERVICE_TYPE_STRING "_services._dns-sd._udp"

dns_sd::dns_sd(sendCallback_t sendCbFunc, void* sendCbArg )
  : _sendCbFunc(sendCbFunc),_sendCbArg(sendCbArg),_serviceName(nullptr),_serviceType(nullptr),_serviceDomain(nullptr),_hostName(nullptr),_hostPort(0),_text(nullptr)
{}

dns_sd::dns_sd( sendCallback_t sendCbFunc, void* sendCbArg, const char* serviceName, const char* serviceType,  const char* serviceDomain, const char* hostName, uint32_t hostAddr, uint16_t hostPort, const char* text )
  : _sendCbFunc(sendCbFunc),_sendCbArg(sendCbArg),_serviceName(nullptr),_serviceType(nullptr),_serviceDomain(nullptr),_hostName(nullptr),_hostPort(0),_text(nullptr)
{
  setup( serviceName, serviceType, serviceDomain, hostName, hostAddr, hostPort, text );
}

dns_sd::~dns_sd()
{
  _free();
}

dns_sd::result_t dns_sd::setup( const char* serviceName, const char* serviceType,  const char* serviceDomain, const char* hostName, uint32_t hostAddr, uint16_t hostPort, const char* text )
{
  _free();
  
  _serviceName   = strdup(serviceName);
  _serviceType   = strdup(serviceType);
  _serviceDomain = strdup(serviceDomain);
  _hostName      = strdup(hostName);
  _hostAddr      = hostAddr;
  _text          = strdup(text);
  _hostPort      = hostPort;
  
  return kOkRC;
}

dns_sd::result_t dns_sd::receive( const void* buf, unsigned bufByteN )
{
  _parse((const char*)buf,bufByteN);
  return kOkRC; 
}

void dns_sd::gen_question()
{
  unsigned       n = _calc_question_byte_count();
  unsigned char *b = (unsigned char*)calloc(1,n);
  _format_question(b, n);
  _send(b,n);
  free(b);

}

void dns_sd::gen_response()
{
  unsigned       n = _calc_response_byte_count();
  unsigned char* b = (unsigned char*)calloc(1,n);
  _format_response(b, n);
  _send(b,n);
  free(b);
}

void dns_sd::_free()
{
  if( _serviceName )  free( _serviceName );
  if( _serviceType )  free( _serviceType );
  if( _serviceDomain) free( _serviceDomain );
  if( _hostName)      free( _hostName );
  if( _text )         free( _text );
}

unsigned dns_sd::_calc_question_byte_count()
{
  unsigned n = kHdrBodyByteN;
  n += 1 + strlen(_serviceName) + 1 + strlen(_serviceType) + 1 + strlen(_serviceDomain) + 1 + kQuestionBodyByteN;
  n += 2 + kRsrcBodyByteN + kSrvBodyByteN + 1 + strlen(_hostName) + 2;   
  //n += 2 + kRsrcBodyByteN + strlen(_text) + 1;
  return n;   
}

void dns_sd::_format_question( unsigned char* buf, unsigned bufByteN )
{
  assert( bufByteN > kHdrBodyByteN );
  unsigned char* bend = buf + bufByteN;
  
  uint16_t* u = (uint16_t*)buf;
  u[0] = htons(0);  // transaction id
  u[1] = htons(0);  // flags
  u[2] = htons(1);  // question
  u[3] = htons(0);  // answer
  u[4] = htons(1);  // name server
  u[5] = htons(0);  // other

  unsigned char* b    = (unsigned char*)(u + 6);
  
  // Format question
  unsigned char namePtr[] = {0xc0, (unsigned char)(b - buf)};
  b = _write_name(  b, bend, _serviceName );
  
  //unsigned char typePtr[] = { 0xc0, (unsigned char)(b - buf)};  
  b = _write_name(   b, bend, _serviceType );
  
  unsigned char domainPtr[] = { 0xc0, (unsigned char)(b - buf) };
  b = _write_name( b, bend, _serviceDomain, true );
  
  b = _write_uint16(     b, bend, kANY_DnsTId );
  b = _write_uint16(   b, bend, kInClassDnsFl );
  
  // Format SRV name server
  b = _write_ptr( namePtr, b, bend );          // name
  b = _write_uint16( b, bend, kSRV_DnsTId );   // type 
  b = _write_uint16( b, bend, kInClassDnsFl ); // class
  b = _write_uint32( b, bend, 120 );           // TTL
  b = _write_uint16( b, bend, kSrvBodyByteN + strlen(_hostName) + 1 + 2  );
  b = _write_uint16( b, bend, 0 );             // priority
  b = _write_uint16( b, bend, 0 );             // weight
  b = _write_uint16( b, bend, _hostPort );     // port
  b = _write_text(   b, bend, _hostName );     // host
  b = _write_ptr(    b, bend, domainPtr );     // host suffix (.local)
  /*
  // Format TXT name server
  b = _write_ptr(    namePtr,      b, bend );      // name
  b = _write_uint16( kTXT_DnsTId,  b, bend );      // type
  b = _write_uint16( kInClassDnsFl,   b, bend );      // class
  b = _write_uint32( 4500,         b, bend );      // TTL
  b = _write_uint16( strlen(_text),b, bend );      // dlen
  b = _write_text(   _text,        b, bend );      // text 
  */
  assert( b == bend );
}

unsigned char* dns_sd::_write_uint16( unsigned char* b, unsigned char* bend, uint16_t value  )
{
  assert( bend - b >= (int)sizeof(value));
  uint16_t* u = (uint16_t*)b;
  u[0] = htons(value);
  return (unsigned char*)(u + 1);
}

unsigned char* dns_sd::_write_uint32( unsigned char* b, unsigned char* bend, uint32_t value )
{
  assert( bend - b >= (int)sizeof(value));
  uint32_t* u = (uint32_t*)b;
  u[0] = htonl(value);
  return (unsigned char*)(u + 1);
}

unsigned char* dns_sd::_write_ptr( unsigned char* b, unsigned char* bend, const unsigned char ptr[2] )
{
  assert( (ptr[0] & 0xc0) == 0xc0 );
  b[0] = ptr[0];
  b[1] = ptr[1];
  return b+2;      
}

unsigned char* dns_sd::_write_text( unsigned char* b, unsigned char* bend, const char* name )
{ return _write_name( b, bend, name, false, '\n' ); }

unsigned char* dns_sd::_write_name( unsigned char* b, unsigned char* bend, const char* name, bool zeroTermFl, const unsigned char eosChar )
{
  unsigned char* b0 = b;  // segment length prefix pointer
  unsigned       n  = 0;  // segment length

  
  b += 1;  // advance past the first segment length byte

  // for every name char advance both the src and dst location
  for(; *name; ++name, ++b )
  {
    if( *name == eosChar )
    {
      *b0 = n;    // write the segment length
      n   = 0;    // reset the segment length counter
      b0  = b;    // reset the segment length prefix pointer
    }
    else
    {
      assert( b < bend );
      *b = *name;  // write a name character 
      ++n;         // advance the segment length counter
    }    
  }

  *b0 = n;  // write the segment length of the last segment

  if( zeroTermFl )
  {
    assert( b < bend );
    *b = 0;  // write the zero termination
    b += 1;  //
  }
  
  return b;
}


unsigned dns_sd::_calc_response_byte_count()
{
  unsigned n = kHdrBodyByteN;

  // TXT
  n += 1 + strlen(_serviceName) + 1 + strlen(_serviceType) + 1 + strlen(_serviceDomain) + 1 + kRsrcBodyByteN + 1 + strlen(_text);

  // PTR
  n += 2 + kRsrcBodyByteN + 2;
  
  
  // SRV
  n += 2 + kRsrcBodyByteN + kSrvBodyByteN + 1 + strlen(_hostName) + 2;

  // A
  n += 2 + kRsrcBodyByteN + kABodyByteN;

  // PTR
  n += 1 + strlen(DNS_SD_SERVICE_TYPE_STRING) + 2 + kRsrcBodyByteN + 2;
  
  return n;
}

void dns_sd::_format_response( unsigned char* buf, unsigned bufByteN )
{
  unsigned char* bend = buf + bufByteN;
  uint16_t*      u    = (uint16_t*)buf;
  
  u[0] = htons(0);      // transaction id
  u[1] = htons(0x8400); // flags
  u[2] = htons(0);      // question
  u[3] = htons(5);      // answer
  u[4] = htons(0);      // name server
  u[5] = htons(0);      // other

  unsigned char* b = (unsigned char*)(u+6);
  
  // Format TXT resource record
  unsigned char namePtr[] = { 0xc0, (unsigned char)(b-buf) };
  b = _write_name(  b, buf+bufByteN, _serviceName );

  unsigned char typePtr[]   = { 0xc0, (unsigned char)(b - buf) };
  b = _write_name(  b, bend, _serviceType );
                          
  unsigned char domainPtr[] = { 0xc0, (unsigned char)(b - buf) };
  b = _write_name(  b, bend, _serviceDomain, true );

 
  b = _write_uint16(  b, bend, kTXT_DnsTId );                      // type
  b = _write_uint16(  b, bend, kFlushClassDnsFl | kInClassDnsFl ); // class
  b = _write_uint32(  b, bend, 4500 );                             // TTL
  b = _write_uint16(  b, bend, strlen(_text)+1 );                  // dlen
  b = _write_text(    b, bend, _text );                            // text 

  // Format a PTR resource record
  b = _write_ptr(    b, bend, typePtr );
  b = _write_uint16( b, bend, kPTR_DnsTId );
  b = _write_uint16( b, bend, kInClassDnsFl );
  b = _write_uint32( b, bend, 4500 );
  b = _write_uint16( b, bend, 2 );
  b = _write_ptr(    b, bend, namePtr );

  // Format SRV response
  b = _write_ptr(    b, bend, namePtr );                          // name
  b = _write_uint16( b, bend, kSRV_DnsTId );                      // type 
  b = _write_uint16( b, bend, kFlushClassDnsFl | kInClassDnsFl ); // class
  b = _write_uint32( b, bend, 120 );                              // TTL
  b = _write_uint16( b, bend, kSrvBodyByteN + 1 + strlen(_hostName) + 2 );
  b = _write_uint16( b, bend, 0 );                                // priority
  b = _write_uint16( b, bend, 0 );                                // weight
  b = _write_uint16( b, bend, _hostPort );                        // port
  unsigned char hostPtr[] = { 0xc0, (unsigned char)(b - buf) };
  b = _write_text(  b, bend, _hostName );                         // target
  b = _write_ptr(   b, bend, domainPtr );

  // Format A resource record
  b = _write_ptr(    b, bend, hostPtr );
  b = _write_uint16( b, bend, kA_DnsTId );                        // type 
  b = _write_uint16( b, bend, kFlushClassDnsFl | kInClassDnsFl ); // class
  b = _write_uint32( b, bend, 120 );                              // TTL
  b = _write_uint16( b, bend, kABodyByteN );
  b = _write_uint32( b, bend, _hostAddr );                        // priority
  
  // Format a PTR resource record
  b = _write_name(   b, bend, DNS_SD_SERVICE_TYPE_STRING );
  b = _write_ptr(    b, bend, domainPtr );
  b = _write_uint16( b, bend, kPTR_DnsTId );
  b = _write_uint16( b, bend, kInClassDnsFl );
  b = _write_uint32( b, bend, 4500 );
  b = _write_uint16( b, bend, 2 );
  b = _write_ptr(    b, bend, typePtr );
 

  printf("%li %li : %s\n", b - buf, bend - buf, _text );
}


void dns_sd::_parse( const char* buf, unsigned bufByteN )
{
  dns_sd_print(buf,bufByteN);
}

void dns_sd::_send( const void* buf, unsigned bufByteN )
{
  if( _sendCbFunc != nullptr )
    _sendCbFunc(_sendCbArg,buf,bufByteN);
}
