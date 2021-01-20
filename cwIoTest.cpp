#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoTest.h"

namespace cw
{
  namespace io
  {
    // Application Id's for UI elements
    enum
    {
      // Resource Based elements
      kPanelDivId,
      kQuitBtnId,
      kPanelBtn1Id,
      kPanelCheck1Id,
      kPanelBtn2Id,
      kPanelCheck2Id,
      kPanelFloaterId,
      kSelId,
      kOpt1Id,
      kOpt2Id,
      kOpt3Id
    };

    // Application Id's for the resource based UI elements.
    ui::appIdMap_t mapA[] =
    {
      { ui::kRootAppId,  kPanelDivId, "panelDivId" },
      { kPanelDivId, kQuitBtnId,      "quitBtnId" },
      { kPanelDivId, kPanelBtn1Id,    "myBtn1Id" },
      { kPanelDivId, kPanelCheck1Id,  "myCheck1Id" },
      { kPanelDivId, kPanelBtn2Id,    "myBtn2Id" },
      { kPanelDivId, kPanelCheck2Id,  "myCheck2Id" },
      { kPanelDivId, kPanelFloaterId, "myFloater" },
      { kPanelDivId, kSelId,          "mySel" },
      { kSelId,      kOpt1Id,         "myOpt1" },
      { kSelId,      kOpt2Id,         "myOpt2" },
      { kSelId,      kOpt3Id,         "myOpt3" },     
    };

    unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);
    
    // Application object
    typedef struct app_str
    {
      handle_t ioH;
    } app_t;

    rc_t _onUiInit(app_t* app, const ui_msg_t& m )
    {
      rc_t rc = kOkRC;
      return rc;
    }

    rc_t _onUiValue(app_t* app, const ui_msg_t& m )
    {
      rc_t rc = kOkRC;

      switch( m.appId )
      {
        case kQuitBtnId:
          io::stop( app->ioH );
          break;
      }
      return rc;
    }
    
    rc_t _onUiEcho(app_t* app, const ui_msg_t& m )
    {
      rc_t rc = kOkRC;
      return rc;
    }
    
    rc_t uiCb( app_t* app, const ui_msg_t& m )
    {
      rc_t rc = kOkRC;
      
      switch( m.opId )
      {
        case ui::kConnectOpId:
          cwLogInfo("IO Test Connect: wsSessId:%i.",m.wsSessId);
          break;
          
        case ui::kDisconnectOpId:
          cwLogInfo("IO Test Disconnect: wsSessId:%i.",m.wsSessId);          
          break;
          
        case ui::kInitOpId:
          _onUiInit(app,m);
          break;

        case ui::kValueOpId:
          _onUiValue( app, m );
          break;

        case ui::kEchoOpId:
          _onUiEcho( app, m );
          break;

        case ui::kIdleOpId:
          break;
          
        case ui::kInvalidOpId:
          // fall through
        default:
          assert(0);
          break;
        
      }

      return rc;
    }

    rc_t audioCb( app_t* app, const audio_msg_t& m )
    {
      rc_t rc = kOkRC;

      unsigned chN     = std::min(m.iBufChCnt,m.oBufChCnt);
      unsigned byteCnt = m.dspFrameCnt * sizeof(sample_t);

      // Copy the input to the output
      for(unsigned i=0; i<chN; ++i)
        if( m.oBufArray[i] != NULL )
        {      
          // the input channel is not disabled
          if( m.iBufArray[i] != NULL )
            memcpy(m.oBufArray[i], m.iBufArray[i], byteCnt);    
          else
            // the input channel is disabled but the output is not - so fill the output with zeros
            memset(m.oBufArray[i], 0, byteCnt);
        }
        

      
      return rc;      
    }

    // The main application callback
    rc_t testCb( void* arg, const msg_t* m )
    {
      rc_t rc = kOkRC;
      app_t* app = reinterpret_cast<app_t*>(arg);
      
      switch( m->tid )
      {
        case kSerialTId:
          break;
          
        case kMidiTId:
          break;
          
        case kAudioTId:
          if( m->u.audio != nullptr )
            rc = audioCb(app,*m->u.audio);
          break;
          
        case kSockTid:
          break;
          
        case kWebSockTId:
          break;
          
        case kUiTId:
          rc = uiCb(app,m->u.ui);
          break;

        default:
          assert(0);
        
      }

      return rc;
    }

    void report( handle_t h )
    {
      for(unsigned i=0; i<serialDeviceCount(h); ++i)
        printf("serial: %s\n", serialDeviceName(h,i));

      for(unsigned i=0; i<midiDeviceCount(h); ++i)
        for(unsigned j=0; j<2; ++j)
        {
          bool     inputFl = j==0;
          unsigned m       = midiDevicePortCount(h,i,inputFl);
          for(unsigned k=0; k<m; ++k)
            printf("midi: %s: %s : %s\n", inputFl ? "in ":"out", midiDeviceName(h,i), midiDevicePortName(h,i,inputFl,k));
        
        }

      for(unsigned i=0; i<audioDeviceCount(h); ++i)
        printf("audio: %s\n", audioDeviceLabel(h,i));
            
    }

  }
}


cw::rc_t cw::io::test( const object_t* cfg )
{
  rc_t rc;
  app_t app = {}; 
  
  if((rc = create(app.ioH,cfg,testCb,&app,mapA,mapN)) != kOkRC )
    return rc;

  //report(app.ioH);

  if((rc = start(app.ioH)) != kOkRC )
    cwLogError(rc,"Test app start failed.");
  else
  {    
    while( !isShuttingDown(app.ioH))
    {
      exec(app.ioH);
      sleepMs(50);
    }
  }

  
  destroy(app.ioH);
  printf("ioTest Done.\n");
  return rc;
  
}








