//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace afop
  {

    enum
    {
      kOpenProcId,
      kProcProcId,
      kCloseProcId
    };

    typedef struct proc_ctx_str
    {
      unsigned        procId;
      
      void*           userPtr;
      const object_t* args;       // cfg. for the selected process func
      unsigned        cycleIndex;
      
      char*           srcFn;
      float           srcSrate;
      unsigned        srcChN;
      unsigned        srcBits;
      const float**   srcChV;     // srcChV[ srcChN ][ srcWndSmpN ] - read incoming samples from this buffer 
      unsigned        srcWndSmpN; // 
      unsigned        srcHopSmpN; //
      
      char*           dstFn;
      float           dstSrate;
      unsigned        dstChN;
      unsigned        dstBits;
      float**         dstChV;     // dstChV[ dstChN ][ dstWndSmpN ]
      unsigned        dstWndSmpN; //
      unsigned        dstHopSmpN; //

      
      dsp::data_recorder::fobj_t** recordChA; // recordChA[ recordChN ]
      
    } proc_ctx_t;

    // Open
    //  Accept or modify the destination configuration.  The src signal parameters are definted by the driver program.
    //
    // Proc:
    //  If srcChN is non-zero then srcChV will be valid.  The last srcHopSmpN will contain new samples for this iteration
    //  If dstChN is non-zero then fill at least the first dstHopSmpN samples in dstChV[][].
    //
    //  Note that the srcChV[][] and dstChV[][] both point to buffers of lengh src/dstWndSmpN but only the first src/dstHopSmpN
    //  are finalized for a given cycle.  The training wndSmpN-hopSmpN will be available (shifted right by hopSmpN samples)
    //  on the next call.
    //
    //  Return kEofRC if no input file is given and processing is complete.
    
    
    typedef rc_t (*proc_func_t)( proc_ctx_t* ctx );
    rc_t file_processor( const char* srcFn, const char* dstFn, proc_func_t func, unsigned wndSmpN, unsigned hopSmpN, void* userArg, const object_t* args, const object_t* recorder_cfg, unsigned recordChN=0 );
    rc_t file_processor( const object_t* cfg );


    
  }
}
