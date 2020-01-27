#ifndef cwIo_h
#define cwIo_h

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
     kNetTid,
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

    typedef struct msg_str
    {
      unsigned tid;
      union
      {
        const serial_msg_t*  serial;
        const midi_msg_t*    midi;        
      } u;
    } msg_t;
    
    typedef void(*cbFunc_t)( void* arg, const msg_t* m );
      
    rc_t create(
      handle_t&   h,
      const char* cfgStr,
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
    rc_t        midiDeviceSend(      handle_t h, unsigned devIdx, unsigned portIdx, midi::byte_t status, midi::byte_t d0, midi::byte_t d1 );
    
  }
}


#endif
