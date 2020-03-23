#ifndef cwIo_h
#define cwIo_h

#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwSerialPortDecls.h"
#include "cwAudioDeviceDecls.h"
#include "cwSocketDecls.h"

namespace cw
{
  namespace io
  {

    typedef handle<struct io_str> handle_t;

    enum
    {
     kSerialTId,
     kMidiTId,
     kAudioTId,
     kSockTid,
     kWebSockTId
    };

    typedef struct serial_msg_str
    {
      unsigned    devId;
      const void* dataA;
      unsigned    byteN;
    } serial_msg_t;

    typedef struct midi_msg_str
    {
      midi::packet_t* pkt;
    } midi_msg_t;

    typedef audio::device::sample_t sample_t;

    typedef struct audio_msg_str
    {
      unsigned      iDevIdx;
      sample_t**    iBufArray;
      unsigned      iBufChCnt;
      time::spec_t* iTimeStampPtr;
      
      unsigned      oDevIdx;
      sample_t**     oBufArray;
      unsigned      oBufChCnt;
      time::spec_t* oTimeStampPtr;
      
    } audio_msg_t;

    typedef struct socket_msg_str
    {
      sock::cbId_t              cbId;
      unsigned                  userId;
      unsigned                  connId;
      const void*               byteA;
      unsigned                  byteN;
      const struct sockaddr_in* srcAddr;
    } socket_msg_t;

    typedef struct msg_str
    {
      unsigned tid;
      union
      {
        const serial_msg_t*  serial;
        const midi_msg_t*    midi;
        const audio_msg_t    audio;
        const socket_msg_t   sock;
      } u;
    } msg_t;
    
    typedef void(*cbFunc_t)( void* arg, const msg_t* m );
      
    rc_t create(
      handle_t&   h,
      const char* cfgStr,      // configuration information as text
      cbFunc_t    cbFunc,
      void*       cbArg,
      const char* cfgLabel="io" );
    
    rc_t destroy( handle_t& h );

    rc_t start( handle_t h );
    rc_t pause( handle_t h );

    unsigned    serialDeviceCount( handle_t h );
    const char* serialDeviceName(  handle_t h, unsigned devIdx );
    unsigned    serialDeviceIndex( handle_t h, const char* name );
    rc_t        serialDeviceSend(  handle_t h, unsigned devIdx, const void* byteA, unsigned byteN );
    
    unsigned    midiDeviceCount(     handle_t h );
    const char* midiDeviceName(      handle_t h, unsigned devIdx );
    unsigned    midiDeviceIndex(     handle_t h, const char* devName );
    unsigned    midiDevicePortCount( handle_t h, unsigned devIdx, bool inputFl );
    const char* midiDevicePortName(  handle_t h, unsigned devIdx, bool inputFl, unsigned portIdx );
    unsigned    midiDevicePortIndex( handle_t h, unsigned devIdx, bool inputFl, const char* portName );    
    rc_t        midiDeviceSend(      handle_t h, unsigned devIdx, unsigned portIdx, uint8_t status, uint8_t d0, uint8_t d1 );


    unsigned    audioDeviceCount(          handle_t h );
    unsigned    audioDeviceLabelToIndex(   handle_t h, const char* label );
    const char* audioDeviceLabel(          handle_t h, unsigned devIdx );
    rc_t        audioDeviceSetup(
      handle_t h,
      unsigned devIdx,
      double   srate,
      unsigned framesPerDeviceCycle,
      unsigned devBufBufN,
      unsigned framesPerDspCycle,
      unsigned inputFlags,
      unsigned outputFlags );
    
    unsigned    audioDeviceChannelCount(   handle_t h, unsigned devIdx, unsigned dirFl );
    double      audioDeviceSampleRate(     handle_t h, unsigned devIdx );
    unsigned    audioDeviceFramesPerCycle( handle_t h, unsigned devIdx );
    unsigned    audioDeviceChannelFlags(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned dirFl );
    rc_t        audioDeviceChannelSetFlags(handle_t h, unsigned devidx, unsigned chIdx, unsigned dirFl, unsigned flags );
    sample_t    audioDeviceChannelMeter(   handle_t h, unsigned devIdx, unsigned chIdx, unsigned dirFl );
    rc_t        audioDeviceChannelSetGain( handle_t h, unsigned devIdx, unsigned chIdx, unsigned dirFl, double gain );
    double      audioDeviceChannelGain(    handle_t h, unsigned devIdx, unsigned chIdx, unsigned dirFl );
    
    rc_t        audioDeviceStart(          handle_t h, unsigned devIdx );
    rc_t        audioDeviceStop(           handle_t h, unsigned devIdx );
    bool        audioDeviceIsStarted(      handle_t h, unsigned devIdx );

    
    rc_t socketSetup( handle_t h, unsigned timeOutMs, unsigned recvBufByteN, unsigned maxSocketN );
    rc_t socketCreate(
      handle_t       h,
      unsigned       userId,
      short          port,
      unsigned       flags,
      const char*    remoteAddr = nullptr,
      sock::portNumber_t   remotePort = sock::kInvalidPortNumber,
      const char*    localAddr  = nullptr );

    rc_t socketDestroy( handle_t h, unsigned userId );
    
    // Send to the remote endpoint represented by connId over a connected socket.
    // If 'connId' is kInvalidId then this data is sent to all connected endpoints.
    rc_t socketSend(    handle_t h, unsigned userId, unsigned connId, const void* data, unsigned dataByteN );
    
    // Send a message to a specific remote node over an unconnected UDP socket.
    // Use the function initAddr() to setup the 'sockaddr_in';
    rc_t socketSend(    handle_t h, unsigned userId, const void* data, unsigned dataByteCnt, const struct sockaddr_in* remoteAddr );
    rc_t socketSend(    handle_t h, unsigned userId, const void* data, unsigned dataByteCnt, const char* remoteAddr, sock::portNumber_t port );

    
      
      

    
    
  }
}


#endif
