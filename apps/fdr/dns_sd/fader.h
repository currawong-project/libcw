//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef fader_h
#define fader_h


class fader
{
public:
  typedef enum
  {
   kOkRC,
   kTimeOutRC,
   kUnknownMsgRC
  } rc_t;

  // Callback function to send TCP messages to the EuCon.
  typedef void (*euConCbFunc_t)(   void* arg, const void* buf, unsigned bufByteN );
  
  // Callback function to send virtual control change messages to the physical controls.
  typedef void (*physCtlCbFunc_t)( void* arg, const uint8_t* buf, uint8_t bufByteN );
  
  fader(
    printCallback_t     printCbFunc,
    const unsigned char faderMac[6],
    uint32_t            faderInetAddr,
    euConCbFunc_t       euConCbFunc,
    void*               euConCbArg,
    physCtlCbFunc_t     physCtlCbFunc,
    void*               physCtlCbArg,
    unsigned            ticksPerHeartBeat,
    unsigned            chN = 8 );
  
  virtual ~fader();

  void reset();

  // Called by the TCP receive function to update the faders state
  // based on EuCon state changes.
  // Return kUnknownMsgRC if the received msg is not recognized.
  rc_t   receive_from_eucon( const void* buf, unsigned bufByteN );  

  // Called by the application to drive time dependent functions.
  // Return kTimeOut if the protocol state machine has timed out.
  rc_t   tick();

  // Call when a physical control changes value.
  void   physical_control_changed( const uint8_t msg[3] );
  
  
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

  enum
  {
   kPhysTouchTId = 0,
   kPhysFaderTId = 1,
   kPhysMuteTId  = 2 
  };
  

  typedef struct
  {
    int16_t  position;    
    int16_t  muteFl;
    int16_t  touchFl;
   
  } ch_t;

  typedef struct
  {
    uint16_t id;    // message prefix - identifies the type of message
    int      byteN; // length of the message in bytes
  } msgRef_t;

  static msgRef_t _msgRefA[];

  
  printCallback_t _printCbFunc;
  uint32_t        _inetAddr;
  unsigned        _tickN;
  ch_t*           _chArray;
  unsigned        _chN;
  euConCbFunc_t   _euconCbFunc;
  void*           _euconCbArg;
  physCtlCbFunc_t _physCtlCbFunc;
  void*           _physCtlCbArg;
  
  protoState_t   _protoState;
  unsigned char  _mac[6];
  unsigned       _ticksPerHeartBeat;

  uint8_t       _msgTypeId;     // first byte of the current TCP message
  int           _msgByteIdx;    // current index into the TCP message being parsed.
  int           _msgByteN;      // count of bytes in the message currently being parsed
  unsigned      _mbi;
  unsigned char _msg[8];        // hold the last 8 bytes of the current incoming TCP messages
                                // (we need these bytes when waiting to determine the length of '0x19' type messages)

  void     _send_to_eucon( const void* buf, unsigned bufByteN );
  
  void     _send_response_0_to_eucon();
  void     _send_response_1_to_eucon();
  void     _send_heartbeat_to_eucon();  
  void     _send_fader_to_eucon( uint16_t chIdx, uint16_t position );
  void     _send_touch_to_eucon( uint16_t chIdx, uint16_t touchFl );
  void     _send_mute_to_eucon( uint16_t chIdx, uint16_t muteFl );


  uint16_t _get_eucon_msg_byte_count( uint8_t msgTypeId, const uint8_t* b, const uint8_t* bend );
  void     _send_to_phys_control( uint8_t ctlTypeId, uint8_t ch, uint16_t value );
  void     _on_eucon_recv_msg_complete( const uint8_t typeId );
  
  
};


#endif
