#ifndef cwAudioDeviceTest_H
#define cwAudioDeviceTest_H

namespace cw
{
  namespace audio
  {
    namespace device
    {
      rc_t test( const object_t* cfg );
      rc_t test_tone( const object_t* cfg );
      rc_t report();
    }
  }  
}

#endif
