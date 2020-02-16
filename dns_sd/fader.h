#ifndef fader_h
#define fader_h


/*
1. wait for 0x0a packet
   send_response_0()
   wait 50 ms
   send_heart_beat()
2. wait for next host packet
   send_response_1()

 */


class fader
{
public:
  typedef enum
  {
   kOkRC,
   kTimeOutRC,
   kUnknownMsgRC
  } rc_t;

  // Function to send TCP messages to the host.
  typedef void (*hostCallback_t)( void* arg, const void* buf, unsigned bufByteN );
  
  fader( printCallback_t printCbFunc, const unsigned char faderMac[6], uint32_t faderInetAddr, hostCallback_t hostCbFunc, void* cbArg, unsigned ticksPerHeartBeat, unsigned chN = 8 );
  virtual ~fader();

  // Called by the TCP receive function to update the faders state
  // based on host state changes.
  // Return kUnknownMsgRC if the received msg is not recognized.
  rc_t   receive( const void* buf, unsigned bufByteN );  
  rc_t   receive_old( const void* buf, unsigned bufByteN );  

  // Called by the application to drive time dependent functions.
  // Return kTimeOut if the protocol state machine has timed out.
  rc_t   tick();

  // Call these function to generate messages to the host when 
  // the controls physical state changes.
  rc_t   physical_fader_touched( uint16_t chIdx );
  rc_t   physical_fader_moved(   uint16_t chIdx, uint16_t newPosition );  
  rc_t   physical_mute_switched( uint16_t chIdx, uint16_t newMuteFl );
  
  rc_t   virtual_fader_moved(   uint16_t chIdx, uint16_t newPosition );  
  rc_t   virtual_mute_switched( uint16_t chIdx, uint16_t newMuteFl );

private:
  typedef enum
  {
   kWaitForHandshake_0_Id,
   kWaitForHandshake_Tick_Id,
   kWaitForHandshake_1_Id,
   kWaitForHeartBeat_Id
  } protoState_t;

  typedef struct
  {
    int16_t  position;    
    bool     muteFl;
    bool     incrFl;
    bool     touchFl;
   
  } ch_t;

  typedef struct
  {
    uint16_t id;    // message prefix - identifies the type of message
    uint16_t byteN; // length of the message in bytes
  } msgRef_t;

  static msgRef_t _msgRefA[];

  
  printCallback_t _printCbFunc;
  uint32_t       _inetAddr;
  unsigned       _tickN;
  ch_t*          _chArray;
  unsigned       _chN;
  hostCallback_t _hostCbFunc;
  void*          _hostCbArg;
  protoState_t   _protoState;
  unsigned char  _mac[6];
  unsigned       _ticksPerHeartBeat;

  uint8_t       _msgTypeId;     //
  unsigned      _msgByteIdx;    // current index into the message being parsed.
  unsigned      _msgByteN;      // count of bytes in the message currently being parsed
  unsigned char _msg[8];        //

  void     _send_response_0();
  void     _send_response_1();
  void     _send_heartbeat();
  void     _send( const void* buf, unsigned bufByteN );
  void     _on_fader_receive( uint16_t chanIdx, uint16_t position );
  void     _on_mute_receive(  uint16_t chanIdx, bool     muteFl );
  void     _send_fader( uint16_t chIdx );
  void     _send_touch( uint16_t chIdx, bool touchFl );
  void     _send_mute( uint16_t chIdx, bool muteFl );
  void     _auto_incr_fader( uint16_t chIdx );


  uint8_t _get_msg_byte_count( uint8_t msgTypeId );
  void    _handleChMsg(const uint8_t* msg);
  void    _on_msg_complete( const uint8_t typeId );
  
  
};


#endif
