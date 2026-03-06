//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwPvAudioFileProc_h
#define cwPvAudioFileProc_h

namespace cw
{
  namespace afop
  {
    
    typedef struct pvoc_ctx_str
    {
      unsigned        procId;
      
      proc_ctx_t*     td_ctx;   // time domain context (userPtr = pgmLabel)
      const object_t* args;     // program args 
      void*           userPtr; 
      
      unsigned        wndSmpN;   // TODO: change thise to src and dst variables
      unsigned        hopSmpN;
      unsigned        procSmpN;
      unsigned        binN;

      double          inGain;
      double          outGain;
      
      unsigned      srcChN;
      const float** srcMagChA;  // srcMagChA[ chN ][ binN ]
      const float** srcPhsChA;  // srcPhsChA[ chN ][ binN ]
      
      unsigned     dstChN;    
      float**      dstMagChA;  // dstMagChA[ chN ][ binN ]
      float**      dstPhsChA;  // dstPhsChA[ chN ][ binN ]
      
    } pvoc_ctx_t;

    typedef rc_t (*pvoc_func_t)( pvoc_ctx_t* ctx );

    rc_t pvoc_file_processor( const object_t* cfg );
  }
}

#endif
