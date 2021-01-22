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
      kPanelDivId = 1000,
      kQuitBtnId,
      kPanelBtn1Id,
      kPanelCheck1Id,
      kPanelBtn2Id,
      kPanelCheck2Id,
      kPanelFloaterId,
      kSelId,
      kOpt1Id,
      kOpt2Id,
      kOpt3Id,

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
      /*
      { kPanelDivId, kPanelBtn1Id,    "myBtn1Id" },
      { kPanelDivId, kPanelCheck1Id,  "myCheck1Id" },
      { kPanelDivId, kPanelBtn2Id,    "myBtn2Id" },
      { kPanelDivId, kPanelCheck2Id,  "myCheck2Id" },
      { kPanelDivId, kPanelFloaterId, "myFloater" },
      { kPanelDivId, kSelId,          "mySel" },
      { kSelId,      kOpt1Id,         "myOpt1" },
      { kSelId,      kOpt2Id,         "myOpt2" },
      { SelId,      kOpt3Id,         "myOpt3" },     
      */
    };

    unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);
    
    // Application object
    typedef struct app_str
    {
      handle_t ioH;
    } app_t;

    

    rc_t _uiCreateMeters( app_t* app, unsigned wsSessId, unsigned chCnt, bool inputFl )
    {
      rc_t rc = kOkRC;
      unsigned    parentUuId  = ui::kRootAppId;
      unsigned    divUuId     = kInvalidId;
      unsigned    titleUuId   = kInvalidId;
      const char* title       = inputFl ? "Input"        : "Output";
      const char* divEleName  = inputFl ? "inMeterId"    : "outMeterId";
      unsigned    divAppId    = inputFl ? kInMeterDivId  : kOutMeterDivId;
      unsigned    baseMeterId = inputFl ? kInMeterBaseId : kOutMeterBaseId;
      unsigned    baseToneId  = inputFl ? kInToneBaseId  : kOutToneBaseId;
      unsigned    baseMuteId  = inputFl ? kInMuteBaseId  : kOutMuteBaseId;
      unsigned    baseGainId  = inputFl ? kInGainBaseId  : kOutGainBaseId;
      unsigned    colUuId;
      unsigned    uuid;

      uiCreateTitle(app->ioH, titleUuId, wsSessId, parentUuId, nullptr,    kInvalidId, nullptr, title );
      uiCreateDiv(  app->ioH, divUuId,   wsSessId, parentUuId, divEleName, divAppId,   "uiRow", nullptr );

      uiCreateDiv(  app->ioH, colUuId, wsSessId, divUuId, nullptr, kInvalidId, "uiCol", nullptr );        
      uiCreateTitle(app->ioH, uuid,    wsSessId, colUuId, nullptr, kInvalidId, nullptr, "Tone" );
      uiCreateTitle(app->ioH, uuid,    wsSessId, colUuId, nullptr, kInvalidId, nullptr, "Mute" );
      uiCreateTitle(app->ioH, uuid,    wsSessId, colUuId, nullptr, kInvalidId, nullptr, "Gain" );
      uiCreateTitle(app->ioH, uuid,    wsSessId, colUuId, nullptr, kInvalidId, nullptr, "Meter" );
      
      for(unsigned i=0; i<chCnt; ++i)
      {
        unsigned chLabelN = 32;
        char     chLabel[ chLabelN+1 ];
        snprintf(chLabel,chLabelN,"%i",i+1);
        
        uiCreateDiv(  app->ioH, colUuId, wsSessId, divUuId, nullptr, kInvalidId, "uiCol", chLabel );        
        uiCreateCheck(app->ioH, uuid,    wsSessId, colUuId, nullptr, baseToneId  + i, "checkClass", nullptr, false );
        uiCreateCheck(app->ioH, uuid,    wsSessId, colUuId, nullptr, baseMuteId  + i, "checkClass", nullptr, false );
        uiCreateNumb( app->ioH, uuid,    wsSessId, colUuId, nullptr, baseGainId  + i, "floatClass", nullptr,    0.0, 3.0, 0.001, 3, 0 );
        uiCreateNumb( app->ioH, uuid,    wsSessId, colUuId, nullptr, baseMeterId + i, "floatClass", nullptr, -100.0, 100, 1, 2, 0 );
      } 
      
      return rc;
    }
    
    rc_t _uiCreateMeterPanel( app_t* app, unsigned wsSessId )
    {
      unsigned devIdx = audioDeviceLabelToIndex( app->ioH, "main");
      unsigned iChCnt = audioDeviceChannelCount( app->ioH, devIdx, kInFl);
      unsigned oChCnt = audioDeviceChannelCount( app->ioH, devIdx, kOutFl);
      
      _uiCreateMeters( app, wsSessId, iChCnt, true );
      _uiCreateMeters( app, wsSessId, oChCnt, false );

      return kOkRC;
    }

    rc_t _onUiInit(app_t* app, const ui_msg_t& m )
    {
      rc_t rc = kOkRC;

      _uiCreateMeterPanel(app,m.wsSessId);
      
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

      if( kInMeterBaseId <= m.appId && m.appId < kInMeterMaxId )
      {
        
      }
      else
        
      if( kInGainBaseId <= m.appId && m.appId < kInGainMaxId )
      {
      }
      else
        
      if( kInToneBaseId <= m.appId && m.appId < kInToneMaxId )
      {
      }
      else
        
      if( kInMuteBaseId <= m.appId && m.appId < kInMuteMaxId )
      {
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

    rc_t _audioMeterCb( app_t* app, const audio_group_dev_t* agd )
    {
      unsigned baseAppId = cwIsFlag(agd->flags,kInFl) ? kInMeterBaseId : kOutMeterBaseId;
      for(unsigned i=0; i<agd->chCnt; ++i)
        uiSendValue(  app->ioH, kInvalidId, uiFindElementUuId(app->ioH,baseAppId+i), agd->meterA[i] );
      return kOkRC;
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

        case kAudioMeterTId:
          if( m->u.audioGroupDev != nullptr )
            rc = _audioMeterCb(app,m->u.audioGroupDev);
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
        printf("audio: %s\n", audioDeviceName(h,i));
            
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
    
    unsigned devIdx = audioDeviceLabelToIndex(app.ioH, "main");
    if( devIdx == kInvalidIdx )
      cwLogError(kOpFailRC, "Unable to locate the requested audio device.");
    else
    {
      audioDeviceEnableMeters( app.ioH, devIdx, kInFl  | kOutFl | kEnableFl );
      //audioDeviceEnableTone( app.ioH,   devIdx, kOutFl |          kEnableFl );
    }
    
    
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








