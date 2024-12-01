//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "config.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rpt.h"
#include "fader.h"

#ifdef OS_LINUX
#include <arpa/inet.h>
#endif

#ifdef ARDUINO
#include <Ethernet.h>
#include <utility/w5100.h>
#endif

fader::msgRef_t fader::_msgRefA[] =
{
 { 0x0a,   88 }, // initial handshake 
 { 0x0c,    4 }, // secondary handshake
 { 0x00,    8 }, // 
 { 0x19, 1044 }, // 0x19 messages are variable length 
 { 0x04,    4 }, // 
 { 0xff,    0 }  // end-of-list sentinel (both id and byteN are invalid)
};

fader::fader(
  printCallback_t     printCbFunc,
  const unsigned char faderMac[6],
  uint32_t            faderInetAddr,
  euConCbFunc_t       euconCbFunc,
  void*               euconCbArg,
  physCtlCbFunc_t     physCtlCbFunc,
  void*               physCtlCbArg,
  unsigned            ticksPerHeartBeat,
  unsigned            chN )
  : _printCbFunc(printCbFunc),
    _inetAddr(faderInetAddr),
    _tickN(0),
    _chArray(nullptr),
    _euconCbFunc(euconCbFunc),
    _euconCbArg(euconCbArg),
    _physCtlCbFunc(physCtlCbFunc),
    _physCtlCbArg(physCtlCbArg),
    _protoState(kWaitForHandshake_0_Id),
    _ticksPerHeartBeat(ticksPerHeartBeat),
    _msgTypeId(0xff),
    _msgByteIdx(0),
    _msgByteN(0),
    _mbi(0)
{
  memcpy(_mac,faderMac,6);
  
  _chArray = new ch_t[chN];
  _chN     = chN;

  reset();
}

fader::~fader()
{
  delete[] _chArray;
}

void fader::reset()
{
  _protoState = kWaitForHandshake_0_Id;
  _msgTypeId  = 0xff;
  _msgByteIdx = 0;
  _msgByteN   = 0;
  _mbi        = 0;
  
  for(unsigned i=0; i<_chN; ++i)
  {
    _chArray[i].position = 0;
    _chArray[i].muteFl   = false;
    _chArray[i].touchFl  = false;    
  }
  
}

fader::rc_t fader::receive_from_eucon( const void* buf, unsigned bufByteN )
{
  
  rc_t           rc   = kOkRC;  
  const uint8_t* b    = (const uint8_t*)buf; // current msg ptr
  const uint8_t* bend = b + bufByteN; // end of buffer ptr    

  while(b<bend)
  {
    // if this is the start of a new msg
    if( _msgByteN == 0 )
    {
      // if this is an 0x19 msg which started on the previous packet
      if( _msgTypeId == 0x19 )
      {
        // if not already filled
        for(int i=0; (_msgByteIdx+i < 8) && ((b+i)<bend); ++i)
          _msg[_msgByteIdx+i] = b[i];

        // get the count of bytes associated with this 0x19 msg
        _msgByteN = _get_eucon_msg_byte_count( _msgTypeId, _msg, _msg+8 );

        //rpt(_printCbFunc,"0x19 end: %i\n",_msgByteN);
        
      }
      else
      {
        
        _msgTypeId  = b[0];   // the first byte always contains the type of the msg
        _msgByteIdx = 0;      // empty the _msg[] index
        _msgByteN   = _get_eucon_msg_byte_count( _msgTypeId, b, bend  ); // get the length of this mesg

        // if this is a type 0x19 msg then we have to wait 8 bytes for the msg byte count
        if( _msgByteN == 0 && _msgTypeId == 0x19 )
        {
          //_printCbFunc("0x19 begin\n");
          _msgByteIdx = bend - b; 
          break;          
        }        
      }
      
      // store the size and type of this message
 
      if( _msgByteN == 0 && _msgTypeId != 0x19 )
      {
        rpt(_printCbFunc,"Unk Type:0x%x %i\n",_msgTypeId,bufByteN);
        break;
      }

      //rpt(_printCbFunc,"T:0x%x %i\n",_msgTypeId,_msgByteN);
    }

    // if this is a channel message or the start of a 0x19 msg  ...
    if( _msgTypeId == 0 || (_msgTypeId == 0x19 && _msgByteN==0) )
    {
      // copy it into _msg[]
      for(int i=0; (_msgByteIdx+i < 8) && ((b+i)<bend); ++i)
        _msg[_msgByteIdx+i] = b[i];
    }

    // if the end, (and possibly the beginning) of the current msg is fully contained in the buffer ...
    if( (_msgByteN - _msgByteIdx) <= (bend - b) )
    {
      _on_eucon_recv_msg_complete(_msgTypeId);
      b          += _msgByteN - _msgByteIdx;    // then we have reached the end of the msg
      _msgByteN   = 0;
      _msgByteIdx = 0;
      _msgTypeId  = 0xff;
    }
    else  // this msg overflows to the next TCP packet
    {
      //_printCbFunc("Ovr:\n");
      _msgByteIdx += bend - b;
      b            = bend;
    }
  }
  //rpt(_printCbFunc,"D:\n");
  return rc;
}



fader::rc_t   fader::tick()
{
  rc_t rc = kOkRC;
  
  switch( _protoState )
  {      
    case kWaitForHandshake_Tick_Id:
      //_printCbFunc("HS Tick ");
      _send_heartbeat_to_eucon();
      _protoState = kWaitForHandshake_1_Id;
      break;
      
    case kWaitForHandshake_0_Id:
    case kWaitForHandshake_1_Id:
    case kWaitForHeartBeat_Id:
      break;
  }

  _tickN += 1;
  if( _tickN == _ticksPerHeartBeat )
  {
    _tickN = 0;
    if( _protoState == kWaitForHeartBeat_Id )
      _send_heartbeat_to_eucon();
  }
  
  return rc;
}


void fader::physical_control_changed( const uint8_t msg[3] )
{

  // TODO: mask off invalid values (e.g. chan>7, value>0x3ff)
  
  uint8_t  type  = (msg[0] & 0x70) >> 4;
  uint8_t  chan  = (msg[0] & 0x0f);
  uint16_t value = msg[1];

  value <<= 7;
  value += msg[2];

  rpt(_printCbFunc,"T:%i Ch:%i V:%x\n",type,chan,value);
  
  
  switch( type )
  {
    case kPhysTouchTId:
      _send_touch_to_eucon(chan,value != 0);
      break;
      
    case kPhysFaderTId:
      _send_fader_to_eucon(chan,value);
      break;
      
    case kPhysMuteTId:
      _send_mute_to_eucon(chan,value == 0);
      break;

    default:
      rpt(_printCbFunc,"Unknown physical ctl id.");
  }
  
    
}

void     fader::_send_to_eucon( const void* buf, unsigned bufByteN )
{
  return _euconCbFunc(_euconCbArg,buf,bufByteN);
}

  
void     fader::_send_response_0_to_eucon()
{
  unsigned char buf[] =
    { 0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x00,0x02,0x03,0xfc,0x01,0x05,
      0x06,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,  // mac: offset 16
      0x01,0x00,
      0x00,0x00,0x00,0x00,            // ip: offset 24
      0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x03,0xff,0x00,0x30,0x08,0x00,0x00,0x80,0x00,0x40,0x01,0x01,0x00,0x00,0x00,0x00,
      0x00,0x00
    };

  // set the mac address
  memcpy(buf+16,_mac,6);

  // set the 32 bit ip address
  memcpy((unsigned char *)(buf+24),(unsigned char*)&_inetAddr, 4);

  _send_to_eucon(buf,sizeof(buf));  
}

void fader::_send_response_1_to_eucon()
{
  unsigned char buf[] = { 0x0d,0x00,0x00,0x00, 0x00,0x00,0x00,0x08 };

  _send_to_eucon(buf,sizeof(buf));    
}


void     fader::_send_heartbeat_to_eucon()
{
  const unsigned char buf[] = { 0x03, 0x00, 0x00, 0x00 };  
  _send_to_eucon(buf,sizeof(buf));    
}


void     fader::_send_fader_to_eucon( uint16_t chIdx, uint16_t pos )
{
  _chArray[chIdx].position = pos;
  if( _chArray[chIdx].touchFl )
  {
    uint16_t buf[] = { htons(chIdx),htons(0), 0, htons(pos) };
    _send_to_eucon(buf,sizeof(buf));
  }
}

void     fader::_send_touch_to_eucon( uint16_t chIdx, uint16_t touchFl )
{
  _chArray[chIdx].touchFl = touchFl; 
  uint16_t buf[] = { htons(chIdx),htons(1),0, htons((uint16_t)touchFl) };
  _send_to_eucon(buf,sizeof(buf));    
}

void    fader::_send_mute_to_eucon( uint16_t chIdx, uint16_t muteFl )
{
  _chArray[chIdx].muteFl = muteFl; 
  uint16_t buf[] = { htons(chIdx),htons(0x200),0, htons((uint16_t)(!muteFl)) };
  _send_to_eucon(buf,sizeof(buf));    
}

uint16_t fader::_get_eucon_msg_byte_count( uint8_t msgTypeId, const uint8_t* b, const uint8_t* bend   )
{
  if( msgTypeId == 0x19 )
  {
    const uint16_t* u = (const uint16_t*)b;
    if( bend < (const uint8_t*)(u+4) )
    {
      //_printCbFunc("0x19 short\n");
      return 0;
    }
    
    uint16_t  v  =  u[3];
    return ntohs(v);
  }
  
  for(int i=0; _msgRefA[i].byteN != 0; ++i)
    if( msgTypeId == _msgRefA[i].id )
      return _msgRefA[i].byteN;

  return 0;
}

void fader:: _send_to_phys_control( uint8_t ctlTypeId, uint8_t ch, uint16_t value )
{
  uint8_t msg[3];

  msg[0] = 0x80 + (ctlTypeId << 4) + (ch);   // status byte always has high bit set
  msg[1] = (uint8_t)((value & 0x3f80) >> 7); // get high 7 bits of value (high bit is always cleared)
  msg[2] = (uint8_t)(value  & 0x007f);       // get low 7 bits of value (high bit is always cleared)

  _physCtlCbFunc( _physCtlCbArg, msg, sizeof(msg) );
}


// called when a new msg is received, b[0] is the msg type id
void fader::_on_eucon_recv_msg_complete( const uint8_t typeId )
{
  switch( typeId )
  {
    case 0x0a:
      if( _protoState == kWaitForHandshake_0_Id )
      {
        _printCbFunc("HS 0 ");
        _send_response_0_to_eucon();     // send [ 0x0b ... ] 
        _protoState = kWaitForHandshake_Tick_Id;
      }
      break;
      
    case 0x0c:
      //rpt(_printCbFunc,"HB\n");
      break;
      
    case 0x00:
      {
        const uint16_t* u = (const uint16_t*)_msg;
        uint8_t ch = _msg[1];
        if( ch < 8 )
        {
          switch( ntohs(u[1]) )
          {
            case 0:
              _chArray[ch].position = ntohs(u[3]);     
              //rpt(_printCbFunc,"F: %2i %4i \n",(int)_msg[1],ntohs(u[3]));
              _send_to_phys_control(kPhysFaderTId,ch,ntohs(u[3]));
              
              break;
              
            case 0x201:
              _chArray[ch].muteFl = ntohs(u[3]);
              //rpt(_printCbFunc,"M: %2i %4i \n",(int)_msg[1],ntohs(u[3]));
              _send_to_phys_control(kPhysMuteTId,ch,ntohs(u[3]));
              break;

          }            
        }
        
      }
      break;
      
    case 0x04:
      //rpt(_printCbFunc,"HB:\n");
      break;
      
    case 0x19:
      //rpt(_printCbFunc,"Skip:\n");
      break;
      
    default:
      rpt(_printCbFunc,"Unknown msg type.");
  }

  // Any msg will trigger change of state from
  // kWaitForHandshake_1_Id to kWaitForHeartBeat_Id
  if( _protoState == kWaitForHandshake_1_Id )
  {
    _printCbFunc("HS 1\n ");
    _send_response_1_to_eucon(); //send [ 0x0d, .... ]
    _protoState = kWaitForHeartBeat_Id;
  }
  
}
