namespace cw
{
  namespace midi
  {
    namespace device
    {
      namespace file_dev
      {
        
        typedef handle<struct file_dev_str> handle_t;

        // 
        // Count of labels determines the count of ports.
        // Note that port indexes and file indexes are synonomous.
        rc_t create( handle_t& hRef,
                     cbFunc_t    cbFunc,
                     void*       cbArg, 
                     unsigned    baseDevIdx, // device index assigned to packet_t.devIndex in callbacks
                     const char* labelA[], // labelA[ max_file_cnt ] assigns a textual label to each port.
                     unsigned    max_file_cnt,
                     const char* dev_name          = "file_dev", // name of the file device
                     unsigned    read_ahead_micros = 3000);      // exec() will xmit events up to 'read_ahead_micros' after the current time
        rc_t destroy( handle_t& hRef );

        // Return true if at least one enabled file exists, otherwise return false.
        bool is_active( handle_t h );

        // Returns create(...,max_file_cnt...)
        unsigned file_count( handle_t h );

        // Assign a MIDI file to an input port.
        rc_t open_midi_file( handle_t h, unsigned file_idx, const char* fname );
        rc_t load_messages(  handle_t h, unsigned file_idx, const msg_t* msgA, unsigned msgN );

        // Enable and disble the output of the specified file/port.
        rc_t enable_file(  handle_t h, unsigned file_idx, bool enableFl );
        rc_t enable_file(  handle_t h, unsigned file_idx );
        rc_t disable_file( handle_t h,unsigned file_idx );


        // Device count: Always 1.
        unsigned    count( handle_t h );
        
        // Device name as set in create()
        const char* name(        handle_t h, unsigned devIdx );

        // Returns 0 if deviceName == name() else kInvalidIdx
        unsigned    nameToIndex(handle_t h, const char* deviceName);

        // The count of ports is determined by count of labels in create(...,labelA[],...).
        unsigned    portCount(  handle_t h, unsigned devIdx, unsigned flags );

        // Port name are provided by create(...,labelA[],...)
        // Port indexes and file indexes are the same.
        const char* portName(   handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx );
        unsigned    portNameToIndex( handle_t h, unsigned devIdx, unsigned flags, const char* portName );
        rc_t        portEnable(      handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx, bool enableFl );
        

        typedef struct exec_result_str
        {
          unsigned next_msg_wait_micros; // microseconds before the next file msg must be transmitted
          unsigned xmit_cnt;             // count of msg's sent to callback during this exec
          bool     eof_fl;               // true if there are no more file msg's to transmit
          
        } exec_result_t;


        // Set the next msg to be returned.
        rc_t seek_to_msg_index( handle_t h, unsigned file_idx, unsigned msg_idx );
        rc_t set_end_msg_index( handle_t h, unsigned file_idx, unsigned msg_idx );

        // Seek to the start of the file or to the last msg_idx set by seek_to_event().
        rc_t rewind( handle_t h );

        
        // Delay the first MIDI msg by 'start_delay_micros'.
        rc_t set_start_delay(  handle_t h, unsigned start_delay_micros );

        
        
        // Callback create(...,cbFunc,...) with msg's whose time has expired and return the
        // time delay prior to the next message.
        exec_result_t exec( handle_t h, unsigned long long elapsed_micros );

        rc_t        send(       handle_t h, unsigned devIdx, unsigned portIdx, uint8_t st, uint8_t d0, uint8_t d1 );
        rc_t        sendData(   handle_t h, unsigned devIdx, unsigned portIdx, const uint8_t* dataPtr, unsigned byteCnt );


        // Reset the latency measurement process.
        void latency_measure_reset(handle_t h);      
        latency_meas_result_t latency_measure_result(handle_t h);
        
        void report( handle_t h, textBuf::handle_t tbH );
        
        rc_t test( const object_t* cfg );
      }
    }
  }
} 
