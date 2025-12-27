//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef  cwAudioDeviceDefs_H
#define  cwAudioDeviceDefs_H 
namespace cw
{
  namespace audio
  {
    namespace device
    {
      typedef float sample_t;

      // audioPacket_t flags
      enum
      {
       kInterleavedApFl = 0x01,  // The audio samples are interleaved.
       kFloatApFl       = 0x02   // The audio samples are single precision floating point values.
      };

      // Audio packet record used by the audioPacket_t callback.
      // Audio ports send and receive audio using this data structure. 
      typedef struct
      {
        unsigned     devIdx;         // device associated with packet
        unsigned     begChIdx;       // first device channel 
        unsigned     chCnt;          // count of channels
        unsigned     audioFramesCnt; // samples per channel (see note below)
        unsigned     bitsPerSample;  // bits per sample word
        unsigned     flags;          // kInterleavedApFl | kFloatApFl
        void*        audioBytesPtr;  // pointer to sample data
        void*        cbArg;          // user defined argument passed in via deviceSetup()
        time::spec_t timeStamp;      // Packet time stamp.
      }  audioPacket_t; 

    }
  }
}
#endif
