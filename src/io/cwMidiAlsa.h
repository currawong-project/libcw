//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace midi
  {
    namespace device
    {
      namespace alsa
      {
        
        typedef handle< struct alsa_device_str> handle_t;
        
        rc_t create( handle_t&   h,
                     cbFunc_t    cbFunc,
                     void*       cbDataPtr,
                     unsigned    parserBufByteCnt,
                     const char* appNameStr,
                     bool        filterRtSenseFl );
        
        rc_t destroy( handle_t& h);
        bool isInitialized( handle_t h );

        struct pollfd* pollFdArray( handle_t h, unsigned& arrayCntRef );
        rc_t           handleInputMsg( handle_t h );

        unsigned    count( handle_t h );
        const char* name(        handle_t h, unsigned devIdx );
        unsigned    nameToIndex(handle_t h, const char* deviceName);
        unsigned    portCount(  handle_t h, unsigned devIdx, unsigned flags );
        const char* portName(   handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx );
        unsigned    portNameToIndex( handle_t h, unsigned devIdx, unsigned flags, const char* portName );
        rc_t        portEnable(      handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx, bool enableFl );
        
        rc_t        send(       handle_t h, unsigned devIdx, unsigned portIdx, uint8_t st, uint8_t d0, uint8_t d1 );
        rc_t        sendData(   handle_t h, unsigned devIdx, unsigned portIdx, const uint8_t* dataPtr, unsigned byteCnt );
        

        // Latency measurment:
        // Record the time of the next incoming note-on msg
        // and the time of the next outgoing note-on msg
        
        // Reset the latency measurement process.
        void latency_measure_reset(handle_t h);      
        latency_meas_result_t latency_measure_result(handle_t h);
      
        void report( handle_t h, textBuf::handle_t tbH);
        
      }
    }
  }
}
