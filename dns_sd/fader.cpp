#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rpt.h"
#include "fader.h"

#ifdef cwLINUX
#include <arpa/inet.h>
#endif

#ifdef ARDUINO
#include <utility/util.h>
#endif

fader::msgRef_t fader::_msgRefA[] =
{
 { 0x0a,   88 },
 { 0x0c,    6 },
 { 0x00,    8 },
 { 0x19, 1044 },
 { 0x04,    4 },
 { 0xff,    0 } // end-of-list sentinel (both id and byteN are invalid)
};

fader::fader( printCallback_t printCbFunc, const unsigned char faderMac[6], uint32_t faderInetAddr, hostCallback_t hostCbFunc, void* hostCbArg, unsigned ticksPerHeartBeat, unsigned chN )
  : _printCbFunc(printCbFunc), _inetAddr(faderInetAddr),_tickN(0),_chArray(nullptr),_hostCbFunc(hostCbFunc),_hostCbArg(hostCbArg),_protoState(kWaitForHandshake_0_Id),_ticksPerHeartBeat(ticksPerHeartBeat),_msgTypeId(0xff),_msgByteIdx(0),_msgByteN(0)
{
  memcpy(_mac,faderMac,6);
  
  _chArray = new ch_t[chN];
  _chN     = chN;
  for(unsigned i=0; i<chN; ++i)
  {
    _chArray[i].position = 0;
    _chArray[i].muteFl   = false;
    _chArray[i].incrFl   = true;
    _chArray[i].touchFl  = false;    
  }
}

fader::~fader()
{
  delete[] _chArray;
}



fader::rc_t fader::receive( const void* buf, unsigned bufByteN )
{  
  rc_t           rc   = kOkRC;
  const uint8_t* b    = (const uint8_t*)buf; // current msg ptr
  const uint8_t* bend = b + bufByteN; // end of buffer ptr    
  
  while(b<bend)
  {
    // if this is the start of a new msg
    if( _msgByteN == 0 )
    {
      // store the size and type of this message
      _msgTypeId  = b[0];
      _msgByteN   = _get_msg_byte_count( _msgTypeId );
      _msgByteIdx = 0;
    }

    // if this is a channel message 
    if( _msgTypeId == 0 )
    {
      for(int i=0; _msgByteIdx < sizeof(_msg) && b+i<bend; ++i,++_msgByteIdx)
        _msg[_msgByteIdx] = b[i];
    }

    // if the end, (and possibly the beginning) of the current msg is fully contained in the buffer ...
    if( (_msgByteN - _msgByteIdx) <= (bend-b) )
    {
      _on_msg_complete(_msgTypeId);
      b += _msgByteN - _msgByteIdx;    // then we have reached the end of the msg
      _msgByteN   = 0;
      _msgByteIdx = 0;
    }
    else  // this msg overflows to the next TCP packet
    {
      _msgByteIdx += bend-b;
      b = bend;
    }
  }
  return rc;
}


fader::rc_t fader::receive_old( const void* buf, unsigned bufByteN )
{
  rc_t rc = kOkRC;
  const uint8_t* b = (const uint8_t*)buf;

  switch( _protoState )
  {
    case kWaitForHandshake_0_Id:        // wait for [ 0x0a ... ]
      if( bufByteN>0 && b[0] == 10 )
      {
        _printCbFunc("HS 0 ");
        _send_response_0();             // send [ 0x0b ... ] 
        _protoState = kWaitForHandshake_Tick_Id;
      }
      break;

    case kWaitForHandshake_Tick_Id:     // wait for next tick() - send first heart-beat
      break;
      
    case kWaitForHandshake_1_Id:        // wait for next message after heart-beat - send [ 0x0d, .... ]
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
      //_auto_incr_fader(0);
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

fader::rc_t     fader::physical_mute_switched(  uint16_t chanIdx, uint16_t value )
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

void     fader::_send_fader( uint16_t chIdx )
{
  uint16_t buf[] = { htons(chIdx),htons(0), 0, htons(_chArray[chIdx].position) };
  _send(buf,sizeof(buf));    
}

void     fader::_send_touch( uint16_t chIdx, bool touchFl )
{
  _chArray[chIdx].touchFl = touchFl; 
  uint16_t buf[] = { htons(chIdx),htons(1),0, htons((uint16_t)touchFl) };
  _send(buf,sizeof(buf));    
}

void    fader::_send_mute( uint16_t chIdx, bool muteFl )
{
  _chArray[chIdx].muteFl = muteFl; 
  uint16_t buf[] = { htons(chIdx),htons(0x200),0, htons((uint16_t)(!muteFl)) };
  _send(buf,sizeof(buf));    
}


void     fader::_auto_incr_fader( uint16_t chIdx )
{
  ch_t* ch = _chArray+chIdx;

  if( ch->position == 0 && ch->touchFl==false )
  {
    _send_touch(chIdx,true);
  }
  
  if( ch->position > 1023 )
  {
    ch->position = 1023;
    ch->incrFl   = false;    
  }
  else
  {
    if( ch->position < 0 )
    {
      ch->position = 0;
      ch->incrFl = true;
      _send_touch( chIdx, !ch->touchFl );
      _send_mute( chIdx,  !ch->touchFl  );
    }
  }

  if( ch->touchFl )
    _send_fader(chIdx);

  ch->position += ch->incrFl ? 5 : -5;
  
}

uint8_t fader::_get_msg_byte_count( uint8_t msgTypeId )
{
  for(int i=0; _msgRefA[i].byteN != 0; ++i)
    if( msgTypeId == _msgRefA[i].id )
      return _msgRefA[i].byteN;

  return 0;
}

void fader::_handleChMsg(const uint8_t* msg)
{
}

// called when a new msg is received, b[0] is the msg type id
void fader::_on_msg_complete( const uint8_t typeId )
{
  switch( typeId )
  {
    case 0x0a:
      if( _protoState == kWaitForHandshake_0_Id )
      {
        _printCbFunc("HS 0 ");
        _send_response_0();     // send [ 0x0b ... ] 
        _protoState = kWaitForHandshake_Tick_Id;
      }
      break;
      
    case 0x0c:
      break;
      
    case 0x00:
      break;
      
    case 0x04:
      break;
      
    case 0x19:
      break;
      
    default:
      rpt(_printCbFunc,"Unknown msg type.");
  }

  // Any msg will trigger change of state from
  // kWaitForHandshake_1_Id to kWaitForHeartBeat_Id
  if( _protoState == kWaitForHandshake_1_Id )
  {
    _printCbFunc("HS 1 ");
    _send_response_1(); //send [ 0x0d, .... ]
    _protoState = kWaitForHeartBeat_Id;
  }
  
}
