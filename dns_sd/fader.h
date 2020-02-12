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

  // Function to send TCP messages to the host.
  typedef void (*hostCallback_t)( void* arg, const void* buf, unsigned bufByteN );
  
  fader( const unsigned char faderMac[6], uint32_t faderInetAddr, hostCallback_t hostCbFunc, void* cbArg, unsigned chN = 8 );
  virtual ~fader();

  // Called by the TCP receive function to update the faders state
  // based on host state changes.
  // Return kUnknownMsgRC if the received msg is not recognized.
  rc_t   host_receive( const void* buf, unsigned bufByteN );  

  // Called by the application to drive time dependent functions.
  // Return kTimeOut if the protocol state machine has timed out.
  rc_t   tick();

  // Call these function to generate messages to the host when 
  // the controls physical state changes.
  rc_t   physical_fader_touched( uint16_t chIdx );
  rc_t   physical_fader_moved(   uint16_t chIdx, uint16_t newPosition );  
  rc_t   physical_mute_switched( uint16_t chIdx, bool newMuteFl );

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
    uint16_t position;
    bool     muteFl;
  } ch_t;

  uint32_t       _inetAddr;
  unsigned       _lastTickSeconds;
  ch_t*          _chArray;
  unsigned       _chN;
  hostCallback_t _hostCbFunc;
  void*          _hostCbArg;
  protoState_t   _protoState;
  unsigned char  _mac[6];

  void     _send_response_0();
  void     _send_heartbeat();
  void     _send( const void* buf, unsigned bufByteN );
  void     _on_fader_receive( uint16_t chanIdx, uint16_t position );
  void     _on_mute_receive(  uint16_t chanIdx, bool     muteFl );

  
};


#endif
