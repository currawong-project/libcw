#ifndef dns_sd_h
#define dns_sd_h

#include <stdint.h>
#include <stdlib.h>

class dns_sd
{
public:
  typedef enum
  {
   kOkRC
  } result_t;

  typedef void (*sendCallback_t)( void* arg, const void* buf, unsigned bufByteN );

  dns_sd( sendCallback_t sendCbFunc, void* sendCbArg );
  dns_sd( sendCallback_t sendCbFunc, void* sendCbArg, const char* serviceName, const char* serviceType,  const char* serviceDomain, const char* hostName, uint32_t hostAddr, uint16_t hostPort, const char* text );
  virtual ~dns_sd();

  result_t setup(  const char* serviceName, const char* serviceType,  const char* serviceDomain, const char* hostName, uint32_t hostAddr, uint16_t hostPort, const char* text );

  result_t receive( const void* buf, unsigned bufByteN );

  void gen_question();
  void gen_response();
  
private:

  enum
  {
   kHdrBodyByteN      = 12,
   kQuestionBodyByteN = 4,
   kRsrcBodyByteN     = 10,
   kABodyByteN        = 4,
   kSrvBodyByteN      = 6,
   kOptBodyByteN      = 4,
  };


  sendCallback_t _sendCbFunc;
  void*          _sendCbArg;
  char*          _serviceName;
  char*          _serviceType;
  char*          _serviceDomain;
  char*          _hostName;
  uint32_t       _hostAddr;
  uint16_t       _hostPort;
  char*          _text;
    

  void           _free();
  unsigned       _calc_question_byte_count();
  void           _format_question( unsigned char* buf, unsigned bufByteN );
  unsigned char* _write_uint16( unsigned char* b, unsigned char* bend, uint16_t value );
  unsigned char* _write_uint32( unsigned char* b, unsigned char* bend, uint32_t value );
  unsigned char* _write_ptr(   unsigned char* b, unsigned char* bend, const unsigned char ptr[2] );
  unsigned char* _write_text( unsigned char* b, unsigned char* bend, const char* name );
  unsigned char* _write_name( unsigned char* b, unsigned char* bend, const char* name, bool zeroTermFl=false, const unsigned char eosChar='.' );
  unsigned       _calc_response_byte_count();
  void           _format_response( unsigned char* buf, unsigned bufByteN );
  void           _parse( const char* buf, unsigned bufByteN );
  void           _send( const void* buf, unsigned bufByteN );


};


#endif
