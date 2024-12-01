//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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
