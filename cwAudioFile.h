#ifndef cwAudioFile_h
#define cwAudioFile_h

#ifndef cwAudioFile_MAX_FRAME_READ_CNT
// Maximum number of samples which will be read in one call to fread().
// This value is only significant in that an internal buffer is created on the stack
// whose size must be limited to prevent stack overflows.
#define cwAudioFile_MAX_FRAME_READ_CNT (8192) 
#endif


namespace cw
{
  namespace audiofile
  {
    typedef handle<struct audiofile_str> handle_t;

    // Informational flags used by audioFileInfo
    enum
    {
     kAiffAfFl        = 0x01,    // this is an AIFF file 
     kWavAfFl         = 0x02,    // this is a WAV file 
     kSwapAfFl        = 0x04,    // file header bytes must be swapped
     kAifcAfFl        = 0x08,    // this is an AIFC file
     kSwapSamplesAfFl = 0x10     // file sample bytes must be swapped
    };


    // Constants
    enum
    {
     kAudioFileLabelCharCnt = 256,

     kAfBextDescN       = 256,
     kAfBextOriginN     = 32,
     kAfBextOriginRefN  = 32,
     kAfBextOriginDateN = 10,
     kAfBextOriginTimeN = 8
    };

    // Aiff marker record
    typedef struct
    {
      unsigned    id;
      unsigned    frameIdx;
      char        label[kAudioFileLabelCharCnt];
    } marker_t;

    // Broadcast WAV header record As used by ProTools audio files. See http://en.wikipedia.org/wiki/Broadcast_Wave_Format
    // When generated from Protools the timeRefLow/timeRefHigh values appear to actually refer
    // to the position on the Protools time-line rather than the wall clock time.
    typedef struct
    {
      char     desc[      kAfBextDescN       + 1 ];
      char     origin[    kAfBextOriginN     + 1 ];
      char     originRef[ kAfBextOriginRefN  + 1 ];
      char     originDate[kAfBextOriginDateN + 1 ];
      char     originTime[kAfBextOriginTimeN + 1 ];
      unsigned timeRefLow;   // sample count since midnight low word
      unsigned timeRefHigh;  // sample count since midnight high word
    } bext_t;

    // Audio file information record used by audioFileNew and audioFileOpen
    typedef struct 
    {
      unsigned  bits;           // bits per sample
      unsigned  chCnt;          // count of audio file channels
      double    srate;          // audio file sample rate in samples per second
      unsigned  frameCnt;       // total number of sample frames in the audio file
      unsigned  flags;          // informational flags 
      unsigned  markerCnt;      // count of markers in markerArray
      marker_t* markerArray;    // array of markers 
      bext_t    bextRecd;       // only used with Broadcast WAV files
    } info_t;
    

    rc_t open( handle_t& h, const char* fn, info_t* info );
    
    rc_t create( handle_t& h, const char* fn, double srate, unsigned bits, unsigned chN );
    
    rc_t close( handle_t& h );

    // Return true if the handle is open.
    bool       isOpen(     handle_t h );

    // Return true if the current file position is at the end of the file.
    bool       isEOF(      handle_t h );

    // Return the current file position as a frame index.
    unsigned   tell(       handle_t h );

    // Set the current file position as an offset from the first frame.
    rc_t     seek(       handle_t h, unsigned frmIdx );

    // Sample Reading Functions.
    //
    // Fill a user suppled buffer with up to frmCnt samples.
    // If less than frmCnt samples are available at the specified audio file location then the unused
    // buffer space is set to zero. Check *actualFrmCntPtr for the count of samples actually available
    // in the return buffer.  Functions which do not include a begFrmIdx argument begin reading from
    // the current file location (see seek()). The buf argument is always a pointer to an
    // array of pointers of length chCnt.  Each channel buffer specified in buf[] must contain at least
    // frmCnt samples.
    //
    // 
    //  h               An audio file handle returned from an earlier call to audioFileNew()
    //  fn              The name of the audio file to read.
    //  begFrmIdx       The frame index of the first sample to read. Functions that do not use this parameter begin reading at the current file location (See tell()).
    //  frmCnt          The number of samples allocated in buf.
    //  chIdx           The index of the first channel to read.
    //  chCnt           The count of channels to read.
    //  buf             An array containing chCnt pointers to arrays of frmCnt samples.
    //  actualFrmCntPtr The number of frames actually written to the return buffer (ignored if NULL)

    rc_t     readInt(    handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr );
    rc_t     readFloat(  handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr );
    rc_t     readDouble( handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr );

    rc_t     getInt(    const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr);
    rc_t     getFloat(  const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr);
    rc_t     getDouble( const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr);

    // Sum the returned samples into the output buffer.
    rc_t     readSumInt(    handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr );
    rc_t     readSumFloat(  handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr );
    rc_t     readSumDouble( handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr );

    rc_t     getSumInt(    const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr);
    rc_t     getSumFloat(  const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr);
    rc_t     getSumDouble( const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr);

    // Allocate a buffer and read the file into it
    rc_t     allocFloatBuf( const char* fn, float**& chBufRef, unsigned& chCntRef, unsigned& frmCntRef, info_t& afInfoPtrRef, unsigned begFrmIdx=0, unsigned frmCnt=0, unsigned chIdx=0, unsigned chCnt=0 );
    rc_t     freeFloatBuf( float** floatBufRef, unsigned chCnt );
    
    // Sample Writing Functions
    rc_t    writeInt(    handle_t h, unsigned frmCnt, unsigned chCnt, int**    bufPtrPtr );
    rc_t    writeFloat(  handle_t h, unsigned frmCnt, unsigned chCnt, float**  bufPtrPtr );
    rc_t    writeDouble( handle_t h, unsigned frmCnt, unsigned chCnt, double** bufPtrPtr );

    rc_t    writeFileInt(    const char* fn, double srate, unsigned bit, unsigned frmCnt, unsigned chCnt, int**    bufPtrPtr);
    rc_t    writeFileFloat(  const char* fn, double srate, unsigned bit, unsigned frmCnt, unsigned chCnt, float**  bufPtrPtr);
    rc_t    writeFileDouble( const char* fn, double srate, unsigned bit, unsigned frmCnt, unsigned chCnt, double** bufPtrPtr);

    
    // Scan an entire audio file and return the minimum, maximum and mean sample value.
    // On error *minPtr, *maxPtr, and *meanPtr are set to -acSample_MAX, cmSample_MAX, and 0 respectively
    rc_t     minMaxMean( handle_t h, unsigned chIdx, float* minPtr, float* maxPtr, float* meanPtr );
    rc_t     minMaxMeanFn( const char* fn, unsigned chIdx, float* minPtr, float* maxPtr, float* meanPtr );

    // Return the file name associated with a audio file handle.
    const char* name( handle_t h );
    unsigned    channelCount( handle_t h );
    double      sampleRate( handle_t h );

    // Return the info_t record associated with a file.
    rc_t      getInfo(   const char* fn, info_t* infoPtr );
  
    // Print the info_t to a file.
    void      printInfo( const info_t* infoPtr, log::handle_t logH );

    rc_t      reportInfo( const char* audioFn );

    // Print the file header information and frmCnt sample values beginning at frame index frmIdx.
    rc_t      report(   handle_t h,  log::handle_t logH, unsigned frmIdx=0, unsigned frmCnt=kInvalidCnt );
    rc_t      reportFn( const char* fn, log::handle_t logH, unsigned frmIdx=0, unsigned frmCnt=kInvalidCnt );

    // Change the sample rate value in the header.  Note that this function does not resample the audio
    // signal it simply changes the value of the sample rate in the header.
    rc_t        setSrate( const char* audioFn, unsigned srate );
    
    // Testing and example routine for functions in .h.
    // Also see cmProcTest.c readWriteTest()
    rc_t        test( const object_t* cfg );

  }
}


#endif
