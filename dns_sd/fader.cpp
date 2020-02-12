#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fader.h"


fader::fader( const unsigned char faderMac[6], uint32_t faderInetAddr, hostCallback_t hostCbFunc, void* hostCbArg, unsigned chN )
  : _inetAddr(faderInetAddr),_lastTickSeconds(0),_chArray(nullptr),_hostCbFunc(hostCbFunc),_hostCbArg(hostCbArg),_protoState(kWaitForHandshake_0_Id)
{
  memcpy(_mac,faderMac,6);
  
  _chArray = new ch_t[chN];
  _chN     = chN;
  for(unsigned i=0; i<chN; ++i)
  {
    _chArray[i].position = 0;
    _chArray[i].muteFl   = false;
  }
}

fader::~fader()
{
  delete[] _chArray;
}

fader::rc_t fader::host_receive( const void* buf, unsigned bufByteN )
{
  rc_t rc = kOkRC;

  printf("FDR:%i\n",bufByteN);
  
  switch( _protoState )
  {
    case kWaitForHandshake_0_Id:
      if( bufByteN>0 && ((uint8_t*)buf)[0] == 10 )
      {
        printf("HS 0\n");
        _send_response_0();
        _protoState = kWaitForHandshake_Tick_Id;
      }
      break;

    case kWaitForHandshake_Tick_Id:
      break;
      
    case kWaitForHandshake_1_Id:
      printf("HS 1: %i",bufByteN);
      _protoState = kWaitForHeartBeat_Id;
      break;
      
    case kWaitForHeartBeat_Id:
      break;
  }
  return rc;  
}

fader::rc_t   fader::tick()
{
  rc_t rc = kOkRC;
  switch( _protoState )
  {
    case kWaitForHandshake_0_Id:
      break;
      
    case kWaitForHandshake_Tick_Id:
       printf("HS Tick");
      _send_heartbeat();
      _protoState = kWaitForHandshake_1_Id;
      break;
      
    case kWaitForHandshake_1_Id:
      break;
    case kWaitForHeartBeat_Id:
      break;
  }
  return rc;
}

fader::rc_t     fader::physical_fader_touched(  uint16_t chanIdx )
{
  return kOkRC;
}

fader::rc_t     fader::physical_fader_moved( uint16_t chanIdx, uint16_t value )
{
  return kOkRC;
}

fader::rc_t     fader::physical_mute_switched(  uint16_t chanIdx, bool value )
{
  return kOkRC;
}
  
void     fader::_send_response_0()
{
  unsigned char buf[] =
    { 0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x00,0x02,0x03,0xfc,0x01,0x05,
      0x06,0x00,
      0x38,0xc9,0x86,0x37,0x44,0xe7,  // mac: 16
      0x01,0x00,
      0xc0,0xa8,0x00,0x44,            // ip: 24
      0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x03,0xff,0x00,0x30,0x08,0x00,0x00,0x80,0x00,0x40,0x01,0x01,0x00,0x00,0x00,0x00,
      0x00,0x00
    };

  memcpy(buf+16,_mac,6);
  memcpy((unsigned char *)(buf+24),(unsigned char*)&_inetAddr, 4);

  _send(buf,sizeof(buf));  
}

void     fader::_send_heartbeat()
{
  const unsigned char buf[] = { 0x03, 0x00, 0x00, 0x00 };  
  _send(buf,sizeof(buf));    
}

void     fader::_send( const void* buf, unsigned bufByteN )
{
  return _hostCbFunc(_hostCbArg,buf,bufByteN);
}

void     fader::_on_fader_receive( uint16_t chanIdx, uint16_t value )
{
}

void     fader::_on_mute_receive(  uint16_t chanIdx, bool value )
{
}

