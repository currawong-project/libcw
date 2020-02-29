#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwMidi.h"
#include "cwIo.h"
#include "cwIoTest.h"

namespace cw
{
  namespace io
  {
    void testCb( void* arg, const msg_t* m )
    {
    }
  }
}


cw::rc_t cw::io::test()
{

  const char* cfgStr = R"( 
     {
        io: {
              serial: [
                {  
                   name:   "port1",
                   device: "/dev/ttyACM0",
                   baud:   38400,
                   bits:   8,
                   stop:   1,
                   parity: no,
                   pollPeriodMs: 50
                }
              ]
              
              midi: {
                 parserBufByteN: 1024,
              }
          
              audio: {
                  meterMs: 50,
                  
                  deviceL: [
                  {
                    enableFl:   true,
                    name:       "Default",
                    device:     "HDA Intel PCH CS4208 Analog",
                    srate:       48000,
                    dspFrameCnt:    64,
                    cycleCnt:        3   
                  }
                ]
            }
      })";
    
    
    handle_t h;

  rc_t rc;
  
  if((rc = create(h,cfgStr,testCb,nullptr)) != kOkRC )
    return rc;

  char c;
  while((c = getchar()) != 'q')
  {
  }

  destroy(h);
  return rc;
  
}








