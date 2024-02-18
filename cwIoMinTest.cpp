#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwKeyboard.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoMinTest.h"

namespace cw
{
  enum
  {
    kThread0Id,
    kThread1Id    
  };
  
  typedef struct app_str
  {
    unsigned     n0;
    unsigned     n1;
    io::handle_t ioH;
  } app_t;

  void minTestThreadCb( app_t* app, const io::thread_msg_t* tm )
  {
    switch( tm->id )
    {
      case kThread0Id:
        app->n0 += 1;
        break;
        
      case kThread1Id:
        app->n1 += 1;
        break;
    }
  }

  // The main application callback
  rc_t minTestCb( void* arg, const io::msg_t* m )
  {
    rc_t rc = kOkRC;
    app_t* app = reinterpret_cast<app_t*>(arg);

    switch( m->tid )
    {
      case io::kThreadTId:
        minTestThreadCb( app, m->u.thread );
        break;
        
      case io::kSerialTId:
        break;
          
      case io::kMidiTId:
        break;
          
      case io::kAudioTId:
        break;

      case io::kAudioMeterTId:
        break;
          
      case io::kSockTId:
        break;
          
      case io::kWebSockTId:
        break;
          
      case io::kUiTId:
        break;

      default:
        assert(0);
        
    }

    return rc;
  }
}


cw::rc_t cw::min_test( const object_t* cfg )
{
  rc_t rc;
  app_t app = {};

  bool asyncFl = true;

  // create the io framework instance
  if((rc = create(app.ioH,cfg,minTestCb,&app)) != kOkRC )
    return rc;

  if((rc = threadCreate( app.ioH, kThread0Id, asyncFl, &app, "min_test_0" )) != kOkRC )
  {
    rc = cwLogError(rc,"Thread 0 create failed.");
    goto errLabel;    
  }
  
  if((rc = threadCreate( app.ioH, kThread1Id, asyncFl, &app, "min_test_1" )) != kOkRC )
  {
    rc = cwLogError(rc,"Thread 1 create failed.");
    goto errLabel;    
  }
  
  // start the io framework instance
  if((rc = start(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"Test app start failed.");
    goto errLabel;    
  }

  printf("<enter> to quit.\n");
  
  // execuite the io framework
  while( !isShuttingDown(app.ioH))
  {
    exec(app.ioH);
    sleepMs(500);

    if( isKeyWaiting() )
      break;

    printf("%i %i\n",app.n0,app.n1);
  }

 errLabel:
  destroy(app.ioH);
  printf("ioMinTest Done.\n");
  return rc;
  
}
