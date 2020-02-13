#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rpt.h"
#include "fader.h"



fader::fader( printCallback_t printCbFunc, const unsigned char faderMac[6], uint32_t faderInetAddr, hostCallback_t hostCbFunc, void* hostCbArg, unsigned ticksPerHeartBeat, unsigned chN )
  : _printCbFunc(printCbFunc), _inetAddr(faderInetAddr),_tickN(0),_chArray(nullptr),_hostCbFunc(hostCbFunc),_hostCbArg(hostCbArg),_protoState(kWaitForHandshake_0_Id),_ticksPerHeartBeat(ticksPerHeartBeat)
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

fader::rc_t fader::receive( const void* buf, unsigned bufByteN )
{
  rc_t rc = kOkRC;
  const uint8_t* b = (const uint8_t*)buf;
  
  _printCbFunc("FDR ");
  
  switch( _protoState )
  {
    case kWaitForHandshake_0_Id:
      if( bufByteN>0 && b[0] == 10 )
      {
        _printCbFunc("HS 0 ");
        _send_response_0();
        _protoState = kWaitForHandshake_Tick_Id;
      }
      break;

    case kWaitForHandshake_Tick_Id:
      break;
      
    case kWaitForHandshake_1_Id:
      _printCbFunc("HS 1 ");
      _send_response_1();
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
      _printCbFunc("HS Tick ");
      _send_heartbeat();
      _protoState = kWaitForHandshake_1_Id;
      break;
      
    case kWaitForHandshake_1_Id:
      break;
      
    case kWaitForHeartBeat_Id:      
      break;
  }

  _tickN += 1;
  if( _tickN == _ticksPerHeartBeat )
  {
    _tickN = 0;
    _send_heartbeat();
  }
  
  return rc;
}

fader::rc_t     fader::physical_fader_touched(  uint16_t chanIdx )
{
  (void)chanIdx;
  return kOkRC;
}

fader::rc_t     fader::physical_fader_moved( uint16_t chanIdx, uint16_t value )
{
  (void)chanIdx;
  (void)value;
  return kOkRC;
}

fader::rc_t     fader::physical_mute_switched(  uint16_t chanIdx, bool value )
{
  (void)chanIdx;
  (void)value;
  return kOkRC;
}
  
void     fader::_send_response_0()
{
  unsigned char buf[] =
    { 0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x00,0x02,0x03,0xfc,0x01,0x05,
      0x06,0x00,
      0x38,0xc9,0x86,0x37,0x44,0xe7,  // mac: offset 16
      0x01,0x00,
      0xc0,0xa8,0x00,0x44,            // ip: offset 24
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

void fader::_send_response_1()
{
  unsigned char buf[] = { 0x0d,0x00,0x00,0x00, 0x00,0x00,0x00,0x08 };

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
  (void)chanIdx;
  (void)value;  
}

void     fader::_on_mute_receive(  uint16_t chanIdx, bool value )
{
  (void)chanIdx;
  (void)value;
}

