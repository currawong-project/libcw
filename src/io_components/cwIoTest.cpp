//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoTest.h"
#include "cwIoSocketChat.h"
#include "cwIoAudioPanel.h"

namespace cw
{
  namespace io
  {
    // Application Id's for UI elements
    enum
    {
      // Resource Based elements
      kPanelDivId = 1000,
      kQuitBtnId,
      kReportBtnId,

      kInMeterDivId,
      kOutMeterDivId,
      kInMeterBaseId  = 2000,  kInMeterMaxId = 2999,
      kInGainBaseId   = 3000,  kInGainMaxId  = 3999,
      kInToneBaseId   = 4000,  kInToneMaxId  = 4999,
      kInMuteBaseId   = 5000,  kInMuteMaxId  = 5999,
      kOutMeterBaseId = 6000,  kOutMeterMaxId= 6999,
      kOutGainBaseId  = 7000,  kOutGainMaxId = 7999,
      kOutToneBaseId  = 8000,  kOutToneMaxId = 8999,
      kOutMuteBaseId  = 9000,  kOutMuteMaxId = 9999
    };

    // Application Id's for the resource based UI elements.
    ui::appIdMap_t mapA[] =
    {
      { ui::kRootAppId,  kPanelDivId, "panelDivId" },
      { kPanelDivId, kQuitBtnId,      "quitBtnId" },
      { kPanelDivId, kReportBtnId,    "reportBtnId" },
    };

    unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);
    
    // Application object
    typedef struct app_str
    {
      sock_chat::handle_t sockChat0H;
      sock_chat::handle_t sockChat1H;
      audio_panel::handle_t audioPanelH;
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

        case kReportBtnId:
          io::report( app->ioH );
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
          {
            for(unsigned j=0; j<m.dspFrameCnt; ++j )
              m.oBufArray[i][j] =  m.iBufArray[i][j];
            //memcpy(m.oBufArray[i],m.iBufArray[i],byteCnt);
          }
          else
          {
            // the input channel is disabled but the output is not - so fill the output with zeros
            memset(m.oBufArray[i], 0, byteCnt);
          }
        }
      
      return rc;      
    }

    rc_t serialCb( app_t* app, const serial_msg_t* m )
    {
      if( m->byteN > 0 && m->dataA != nullptr  )
      {
        for(unsigned i=0; i<m->byteN; ++i)
          printf("%c",((const char*)m->dataA)[i]);
      }
      return kOkRC;
    }
    

    // The main application callback
    rc_t testCb( void* arg, const msg_t* m )
    {
      rc_t rc = kOkRC;
      app_t* app = reinterpret_cast<app_t*>(arg);

      if( app->sockChat0H.isValid() )
        sock_chat::exec( app->sockChat0H, *m );
      
      if( app->sockChat1H.isValid() )
        sock_chat::exec( app->sockChat1H, *m );

      if( app->audioPanelH.isValid() )
        audio_panel::exec( app->audioPanelH, *m );
      
      switch( m->tid )
      {
        case kSerialTId:
          serialCb(app,m->u.serial);
          break;
          
        case kMidiTId:
          break;
          
        case kAudioTId:
          if( m->u.audio != nullptr )
            rc = audioCb(app,*m->u.audio);
          break;

        case kAudioMeterTId:
          break;
          
        case kSockTId:
          break;
          
        case kWebSockTId:
          break;
          
        case kUiTId:
          rc = uiCb(app,m->u.ui);
          break;

        case kExecTId:
          break;
          
        default:
          assert(0);
        
      }

      return rc;
    }
    
    void _report( handle_t h )
    {
      for(unsigned i=0; i<serialDeviceCount(h); ++i)
        printf("serial: %s\n", serialDeviceLabel(h,i));

      for(unsigned i=0; i<midiDeviceCount(h); ++i)
        for(unsigned j=0; j<2; ++j)
        {
          bool     inputFl = j==0;
          unsigned m       = midiDevicePortCount(h,i,inputFl);
          for(unsigned k=0; k<m; ++k)
            printf("midi: %s: %s : %s\n", inputFl ? "in ":"out", midiDeviceName(h,i), midiDevicePortName(h,i,inputFl,k));
        
        }

      for(unsigned i=0; i<audioDeviceCount(h); ++i)
        printf("audio: %s\n", audioDeviceName(h,i));
            
    }
  }
}


cw::rc_t cw::io::test( const object_t* cfg )
{
  rc_t rc;
  app_t app = {};

  enum
  {
    kSocket0BaseId    = 30000,
    kSocket1BaseId    = 31000,
    kAudioPanelBaseId = 32000
  };

  // create the io framework instance
  if((rc = create(app.ioH,cfg,testCb,&app,mapA,mapN)) != kOkRC )
    return rc;

  // create a socket chat app 
  if((rc = sock_chat::create(app.sockChat0H,app.ioH,"sock0",kSocket0BaseId)) != kOkRC )
  {
    rc = cwLogError(rc,"sock chat app create failed");
    goto errLabel;
  }

  // create a socket chat app 
  if((rc = sock_chat::create(app.sockChat1H,app.ioH,"sock1",kSocket1BaseId)) != kOkRC )
  {
    rc = cwLogError(rc,"sock chat app create failed");
    goto errLabel;
  }

  // create the audio panel application
  if((rc = audio_panel::create(app.audioPanelH, app.ioH, kAudioPanelBaseId)) != kOkRC )
  {
    rc = cwLogError(rc,"Audio panel manager create failed.");
    goto errLabel;
  }
  else
  {
  }
  
  //report(app.ioH);

  // start the io framework instance
  if((rc = start(app.ioH)) != kOkRC )
  {
    rc = cwLogError(rc,"Test app start failed.");
    goto errLabel;
    
  }
  else
  {
  // 
    unsigned devIdx = audioDeviceLabelToIndex(app.ioH, "main");
    if( devIdx == kInvalidIdx )
    {
      cwLogError(kOpFailRC, "Unable to locate the requested audio device.");
      goto errLabel;
    }
    else
    {
      //audioDeviceEnableMeters( app.ioH, devIdx, kInFl  | kOutFl | kEnableFl );
      //audioDeviceEnableTone( app.ioH,   devIdx, kOutFl |          kEnableFl );
    }
  }
  
  // execuite the io framework
  while( !isShuttingDown(app.ioH))
  {
    const unsigned wsTimeOutMs = 50;
    time::spec_t t0 = time::current_time();
    
    exec(app.ioH,wsTimeOutMs);
    
    time::spec_t t1  = time::current_time();
    unsigned     dMs = time::elapsedMs(t0,t1);
    
    if( dMs < wsTimeOutMs ) 
      sleepMs(wsTimeOutMs-dMs);
  }

 errLabel:
  sock_chat::destroy(app.sockChat0H);
  sock_chat::destroy(app.sockChat1H);
  destroy(app.ioH);
  printf("ioTest Done.\n");
  return rc;
  
}








