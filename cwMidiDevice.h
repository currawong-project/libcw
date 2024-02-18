#ifndef cwMidiPort_H
#define cwMidiPort_H


namespace cw
{
  namespace midi
  {

    // Flags used to identify input and output ports on MIDI devices
    enum 
    { 
     kInMpFl  = 0x01, 
     kOutMpFl = 0x02 
    };



    namespace device
    {
      typedef handle< struct device_str> handle_t;

      
      
      rc_t create( handle_t&   h,
                   cbFunc_t    cbFunc,
                   void*       cbArg,
                   const char* filePortLabelA[], // filePortLabelA[ maxFileCnt ]
                   unsigned    maxFileCnt,       // count of file dev ports
                   const char* appNameStr,
                   const char* fileDevName = "file_dev",
                   unsigned    fileDevReadAheadMicros = 3000,
                   unsigned    parserBufByteCnt = 1024  );

      rc_t create( handle_t&       h,
                   cbFunc_t        cbFunc,
                   void*           cbArg,
                   const object_t* args );
      
      rc_t destroy( handle_t& h);
      bool isInitialized( handle_t h );

      unsigned    count( handle_t h );
      const char* name(        handle_t h, unsigned devIdx );
      unsigned    nameToIndex(handle_t h, const char* deviceName);
      unsigned    portCount(  handle_t h, unsigned devIdx, unsigned flags );
      const char* portName(   handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx );
      unsigned    portNameToIndex( handle_t h, unsigned devIdx, unsigned flags, const char* portName );
      rc_t        portEnable(      handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx, bool enableFl );
      
      rc_t        send(       handle_t h, unsigned devIdx, unsigned portIdx, uint8_t st, uint8_t d0, uint8_t d1 );
      rc_t        sendData(   handle_t h, unsigned devIdx, unsigned portIdx, const uint8_t* dataPtr, unsigned byteCnt );

      rc_t        openMidiFile( handle_t h, unsigned devIdx, unsigned portIdx, const char* fname );
      rc_t        seekToMsg(    handle_t h, unsigned devIdx, unsigned portIdx, unsigned msgIdx );
      rc_t        setEndMsg(    handle_t h, unsigned devIdx, unsigned portidx, unsigned msgIdx );

      rc_t start( handle_t h );
      rc_t stop( handle_t h );
      rc_t pause( handle_t h, bool pause_fl );
      

      typedef struct
      {
        time::spec_t note_on_input_ts;
        time::spec_t note_on_output_ts;
      } latency_meas_result_t;

      typedef struct
      {
        latency_meas_result_t alsa_dev;
        latency_meas_result_t file_dev;
      } latency_meas_combined_result_t;

      // Reset the latency measurement process.  Record the time of the first
      // incoming note-on msg and the first outgoing note-on msg.
      void latency_measure_reset(handle_t h);      
      latency_meas_combined_result_t latency_measure_result(handle_t h);

      rc_t report( handle_t h );
      void report( handle_t h, textBuf::handle_t tbH);
      
      rc_t testReport();      
    }
  }
}


#endif
