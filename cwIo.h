#ifndef cwIo_h
#define cwIo_h

#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwSerialPortDecls.h"
#include "cwAudioDeviceDecls.h"
#include "cwSocketDecls.h"
#include "cwUiDecls.h"

namespace cw
{
  namespace io
  {

    typedef handle<struct io_str> handle_t;

    enum
    {
      kDisableFl = 0x00,
      kEnableFl  = 0x01,
      kInFl      = 0x02,
      kOutFl     = 0x04,

      kMeterFl   = 0x08
    };
    
    enum
    {
      kTimerTId,
      kSerialTId,
      kMidiTId,
      kAudioTId,
      kAudioMeterTId,
      kSockTId,
      kWebSockTId,
      kUiTId
    };

    typedef struct timer_msg_str
    {
      unsigned id;
    } timer_msg_t;
    
    typedef struct serial_msg_str
    {
      unsigned    devId;
      const void* dataA;
      unsigned    byteN;
    } serial_msg_t;

    typedef struct midi_msg_str
    {
      const midi::packet_t* pkt;
    } midi_msg_t;

    typedef audio::device::sample_t sample_t;

    typedef struct audio_group_dev_str
    {
      const char*                 label;   // User supplied label
      unsigned                    userId;  // User supplied id
      const char*                 devName; // Audio device name
      unsigned                    devIdx;  // Audio device index
      unsigned                    flags;   //  kInFl | kOutFl | kMeterFl
      unsigned                    chIdx;   // First channel of this device in i/oBufArray
      unsigned                    chCnt;   // Count of audio channels on this device      
      unsigned                    cbCnt;   // Count of device driver callbacks
      sample_t*                   meterA;  // Meter values for this device.
      
      std::atomic_uint            readyCnt;// Used internally

      struct audio_group_dev_str* link;    // 
    } audio_group_dev_t;
    
    typedef struct audio_msg_str
    {
      const char*        label;         // User provided label
      unsigned           groupIndex;    // System supplied group index
      unsigned           userId;        // User id
      double             srate;         // Group sample rate.
      unsigned           dspFrameCnt;   // Count of samples in each buffer pointed to by iBufArray[] and oBufArray[]
      
      sample_t**         iBufArray;     // Array of iBufChCnt ptrs to buffers of size bufSmpCnt
      unsigned           iBufChCnt;     // Count of elements in iBufArray[]
      time::spec_t*      iTimeStampPtr; // 
      audio_group_dev_t* iDevL;         // Linked list of input devices which map directly to channels in iBufArray[]
      
      sample_t**         oBufArray;     //
      unsigned           oBufChCnt;     //
      time::spec_t*      oTimeStampPtr; //
      audio_group_dev_t* oDevL;         // Linked list of output devices which map directly to channels in oBufArray[]
      
    } audio_msg_t;

    typedef struct socket_msg_str
    {
      sock::cbOpId_t            cbId;
      unsigned                  sockIdx;
      unsigned                  userId;
      unsigned                  connId;
      const void*               byteA;
      unsigned                  byteN;
      const struct sockaddr_in* srcAddr;
    } socket_msg_t;

    typedef struct ui_msg_str
    {
      ui::opId_t         opId;
      unsigned           wsSessId;
      unsigned           parentAppId;
      unsigned           uuId;
      unsigned           appId;
      const ui::value_t* value;
    } ui_msg_t;

    typedef struct msg_str
    {
      unsigned tid;
      union
      {
        timer_msg_t*       timer;
        serial_msg_t*      serial;
        midi_msg_t*        midi;
        audio_msg_t*       audio;
        audio_group_dev_t* audioGroupDev; // audioMeterTId
        socket_msg_t*      sock;
        ui_msg_t           ui;
      } u;
    } msg_t;
    
    typedef rc_t (*cbFunc_t)( void* arg, const msg_t* m );
      
    rc_t create(
      handle_t&             h,
      const object_t*       cfg, // configuration object
      cbFunc_t              cbFunc,
      void*                 cbArg,
      const ui::appIdMap_t* mapA = nullptr,
      unsigned              mapN = 0,
      const char*           cfgLabel = "io" );
    
    rc_t destroy( handle_t& h );

    rc_t start( handle_t h );
    rc_t pause( handle_t h );
    rc_t stop(  handle_t h );
    rc_t exec(  handle_t h );
    bool isShuttingDown( handle_t h );
    void report( handle_t h );


    //----------------------------------------------------------------------------------------------------------
    //
    // Timer
    //
    
    rc_t        timerCreate(            handle_t h, const char* label, unsigned id, unsigned periodMicroSec );
    rc_t        timerDestroy(           handle_t h, unsigned timerIdx );
    
    unsigned    timerCount(             handle_t h );
    unsigned    timerLabelToIndex(      handle_t h, const char* label );
    unsigned    timerIdToIndex(         handle_t h, unsigned timerId );
    const char* timerLabel(             handle_t h, unsigned timerIdx );
    unsigned    timerId(                handle_t h, unsigned timerIdx );
    unsigned    timerPeriodMicroSec(    handle_t h, unsigned timerIdx );
    rc_t        timerSetPeriodMicroSec( handle_t h, unsigned timerIdx, unsigned periodMicroSec );
    rc_t        timerStart(             handle_t h, unsigned timerIdx );
    rc_t        timerStop(              handle_t h, unsigned timerIdx );
    
    //----------------------------------------------------------------------------------------------------------
    //
    // Serial
    //

    unsigned    serialDeviceCount( handle_t h );
    const char* serialDeviceLabel( handle_t h, unsigned devIdx );
    unsigned    serialDeviceIndex( handle_t h, const char* label );
    rc_t        serialDeviceSend(  handle_t h, unsigned devIdx, const void* byteA, unsigned byteN );
    
    //----------------------------------------------------------------------------------------------------------
    //
    // MIDI
    //
    
    unsigned    midiDeviceCount(     handle_t h );
    const char* midiDeviceName(      handle_t h, unsigned devIdx );
    unsigned    midiDeviceIndex(     handle_t h, const char* devName );
    unsigned    midiDevicePortCount( handle_t h, unsigned devIdx, bool inputFl );
    const char* midiDevicePortName(  handle_t h, unsigned devIdx, bool inputFl, unsigned portIdx );
    unsigned    midiDevicePortIndex( handle_t h, unsigned devIdx, bool inputFl, const char* portName );    
    rc_t        midiDeviceSend(      handle_t h, unsigned devIdx, unsigned portIdx, uint8_t status, uint8_t d0, uint8_t d1 );


    //----------------------------------------------------------------------------------------------------------
    //
    // Audio
    //
    
    unsigned        audioDeviceCount(          handle_t h );
    unsigned        audioDeviceLabelToIndex(   handle_t h, const char* label );
    rc_t            audioDeviceSetUserId(      handle_t h, unsigned devIdx, unsigned userId );
    bool            audioDeviceIsEnabled(      handle_t h, unsigned devIdx );
    const char*     audioDeviceName(           handle_t h, unsigned devIdx );
    double          audioDeviceSampleRate(     handle_t h, unsigned devIdx );
    unsigned        audioDeviceFramesPerCycle( handle_t h, unsigned devIdx );
    unsigned        audioDeviceChannelCount(   handle_t h, unsigned devIdx, unsigned inOrOutFlag );
    rc_t            audioDeviceEnableMeters(   handle_t h, unsigned devIdx, unsigned inOutEnaFlags );
    const sample_t* audioDeviceMeters(         handle_t h, unsigned devIdx, unsigned& chCntRef, unsigned inOrOutFlag );
    rc_t            audioDeviceEnableTone(     handle_t h, unsigned devidx, unsigned inOutEnaFlags );
    rc_t            audioDeviceToneFlags(      handle_t h, unsigned devIdx, unsigned inOrOutFlag,  bool* toneFlA, unsigned chCnt );
    rc_t            audioDeviceEnableMute(     handle_t h, unsigned devidx, unsigned inOutEnaFlags );
    rc_t            audioDeviceMuteFlags(      handle_t h, unsigned devIdx, unsigned inOrOutFlag,  bool* muteFlA, unsigned chCnt );
    rc_t            audioDeviceSetGain(        handle_t h, unsigned devIdx, unsigned inOrOutFlag, double gain );
    rc_t            audioDeviceGain(           handle_t h, unsigned devIdx, unsigned inOrOutFlag, double* gainA, unsigned chCnt );

    unsigned        audioGroupCount(         handle_t h );
    unsigned        audioGroupLabelToIndex(  handle_t h, const char* label );
    const char*     audioGroupLabel(         handle_t h, unsigned groupIdx );
    bool            audioGroupIsEnabled(     handle_t h, unsigned groupIdx );
    unsigned        audioGroupUserId(        handle_t h, unsigned groupIdx );
    rc_t            audioGroupSetUserId(     handle_t h, unsigned groupIdx, unsigned userId );
    double          audioGroupSampleRate(    handle_t h, unsigned groupIdx );
    unsigned        audioGroupDspFrameCount( handle_t h, unsigned groupIdx );
    
    //----------------------------------------------------------------------------------------------------------
    //
    // Socket
    //

    unsigned           socketCount(        handle_t h );
    unsigned           socketLabelToIndex( handle_t h, const char* label );
    unsigned           socketUserId(       handle_t h, unsigned sockIdx );
    rc_t               socketSetUserId(    handle_t h, unsigned sockIdx, unsigned userId );
    const char*        socketLabel(        handle_t h, unsigned sockIdx );
    const char*        socketHostName(     handle_t h, unsigned sockIdx );
    const char*        socketIpAddress(    handle_t h, unsigned sockIdx );
    unsigned           socketInetAddress(  handle_t h, unsigned sockIdx );
    sock::portNumber_t socketPort(         handle_t h, unsigned sockIdx );
    rc_t               socketPeername(     handle_t h, unsigned sockIdx, struct sockaddr_in* addr );
    
    
    
    // Send to the remote endpoint represented by connId over a connected socket.
    // 'connId' is an id, assigned by the framework, of a remote connected endpoint
    // attached to a receiving local socket. It is passed to the application as part
    // of the incoming data callback.
    // If 'connId' is kInvalidId then this data is sent to all connected endpoints.
    rc_t socketSend(    handle_t h, unsigned sockIdx, unsigned connId, const void* data, unsigned dataByteCnt );
    
    // Send a message to a specific remote node over an unconnected UDP socket.
    // Use the function initAddr() to setup the 'sockaddr_in';
    rc_t socketSend(    handle_t h, unsigned sockIdx, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr );
    rc_t socketSend(    handle_t h, unsigned sockIdx, const void* data, unsigned dataByteCnt, const char* remoteAddr, sock::portNumber_t remotePort );
    

    //----------------------------------------------------------------------------------------------------------
    //
    // UI
    //


    // Find id's associated with elements.
    unsigned    uiFindElementAppId(  handle_t h, unsigned parentUuId, const char* eleName );  
    unsigned    uiFindElementUuId(   handle_t h, unsigned parentUuId, const char* eleName );
    unsigned    uiFindElementUuId(   handle_t h, unsigned parentUuId, unsigned appId );
    const char* uiFindElementName(   handle_t h, unsigned uuId );
    unsigned    uiFindElementAppId(  handle_t h, unsigned uuId );
    
    // Return the uuid of the first matching 'eleName' or 'appId'.
    unsigned    uiFindElementUuId( handle_t h, const char* eleName );
    unsigned    uiFindElementUuId( handle_t h, unsigned appId );

    rc_t uiCreateFromObject( handle_t h, const object_t* o, unsigned wsSessId, unsigned parentUuId=kInvalidId, const char* eleName=nullptr);
    rc_t uiCreateFromFile(   handle_t h, const char* fn,    unsigned wsSessId, unsigned parentUuId=kInvalidId);
    rc_t uiCreateFromText(   handle_t h, const char* text,  unsigned wsSessId, unsigned parentUuId=kInvalidId);
    rc_t uiCreateDiv(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t uiCreateTitle(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t uiCreateButton(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    
    // Create check: w/o value. The value will be read from the engine via the UI 'echo' event.
    // Create check: w/ value. The value will be sent to the engine as the new value of the associated varaible.
    rc_t uiCreateCheck(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t uiCreateCheck(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, bool value );
    
    rc_t uiCreateSelect(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t uiCreateOption(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    
    rc_t uiCreateStr(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t uiCreateStr(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, const char* value );
    
    rc_t uiCreateNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, unsigned decPl );    
    rc_t uiCreateNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, unsigned decPl, double value );
    
    rc_t uiCreateNumb(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl );
    rc_t uiCreateNumb(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl, double value );
    
    rc_t uiCreateProg(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue );
    rc_t uiCreateProg(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double value );
    
    rc_t uiCreateText(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );

    // Register parent/child/name app id's 
    rc_t uiRegisterAppIdMap(  handle_t h, const ui::appIdMap_t* map, unsigned mapN );
    
    // Send a value from the application to the UI.
    // Set wsSessId to kInvalidId to send to all sessions.
    rc_t uiSendValue( handle_t h, unsigned wsSessId, unsigned uuId, bool value );
    rc_t uiSendValue( handle_t h, unsigned wsSessId, unsigned uuId, int value );
    rc_t uiSendValue( handle_t h, unsigned wsSessId, unsigned uuId, unsigned value );
    rc_t uiSendValue( handle_t h, unsigned wsSessId, unsigned uuId, float value );
    rc_t uiSendValue( handle_t h, unsigned wsSessId, unsigned uuId, double value );
    rc_t uiSendValue( handle_t h, unsigned wsSessId, unsigned uuId, const char* value );

    
    
  }
}


#endif
