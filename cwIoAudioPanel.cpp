#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwObject.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoAudioPanel.h"

namespace cw {
  namespace io {
    namespace audio_panel {

      enum
      {
        kMaxChCnt = 250
        
      }; 
      enum
      {
        kPanelDivAppId,

        kInMeterDivId,
        kOutMeterDivId,
        
        kInMeterBaseId  = 1*kMaxChCnt,  kInMeterMaxId = kInMeterBaseId  + kMaxChCnt-1,
        kInGainBaseId   = 2*kMaxChCnt,  kInGainMaxId  = kInGainBaseId   + kMaxChCnt-1,
        kInToneBaseId   = 3*kMaxChCnt,  kInToneMaxId  = kInToneBaseId   + kMaxChCnt-1,
        kInMuteBaseId   = 4*kMaxChCnt,  kInMuteMaxId  = kInMuteBaseId   + kMaxChCnt-1,
        kOutMeterBaseId = 5*kMaxChCnt,  kOutMeterMaxId= kOutMeterBaseId + kMaxChCnt-1,
        kOutGainBaseId  = 6*kMaxChCnt,  kOutGainMaxId = kOutGainBaseId  + kMaxChCnt-1,
        kOutToneBaseId  = 7*kMaxChCnt,  kOutToneMaxId = kOutToneBaseId  + kMaxChCnt-1,
        kOutMuteBaseId  = 8*kMaxChCnt,  kOutMuteMaxId = kOutMuteBaseId  + kMaxChCnt-1,
        
        kMaxAppId
      };

      typedef struct audio_panel_device_str
      {
        unsigned devIdx;    // 
        float    dfltGain;  // 
        unsigned iChCnt;    // count of input channels 
        unsigned oChCnt;    // count of output channels
        unsigned baseAppId; // device appId range: baseAppId:baseAppId+kMaxAppId
        struct audio_panel_device_str* link;
      } audio_panel_device_t;
      
      typedef struct audio_panel_str
      {
        io::handle_t          ioH;
        unsigned              maxAppId;
        audio_panel_device_t* devL;
      } audio_panel_t;

      audio_panel_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,audio_panel_t>(h); }

      rc_t _destroy( audio_panel_t* p )
      {
        rc_t rc = kOkRC;
        mem::release(p);
        return rc;
      }

      audio_panel_device_t* _get_device( audio_panel_t* p, unsigned devIdx )
      {
        audio_panel_device_t* d = p->devL;
        for(; d!=nullptr; d=d->link)
          if( d->devIdx == devIdx )
            return d;

        cwLogError(kInvalidArgRC,"The audio meter device with index '%i' was not found. ", devIdx);
        return nullptr;
      }

      audio_panel_device_t*  _app_id_to_device( audio_panel_t* p, unsigned appId )
      {
        if( appId == kInvalidId  )
          return nullptr;
        
        audio_panel_device_t* d = p->devL;
        for(; d!=nullptr; d=d->link)
          if( d->baseAppId <= appId && appId < d->baseAppId + kMaxAppId )
            return d;

        return nullptr;        
      }

      rc_t _registerDevice( audio_panel_t* p, unsigned baseAppId, unsigned devIdx, float dfltGain )
      {
        rc_t                  rc     = kOkRC;
        audio_panel_device_t* d      = nullptr;

        if((rc = audioDeviceEnableMeters( p->ioH, devIdx, kInFl  | kOutFl | kEnableFl )) != kOkRC )
          goto errLabel;
  
        if((rc = audioDeviceEnableTone( p->ioH,   devIdx, kOutFl |          kEnableFl )) != kOkRC )
          goto errLabel;
  

        d = mem::allocZ<audio_panel_device_t>();
        d->baseAppId = baseAppId;
        d->devIdx    = devIdx;
        d->dfltGain  = dfltGain;
        d->iChCnt    = audioDeviceChannelCount( p->ioH, devIdx, io::kInFl  );
        d->oChCnt    = audioDeviceChannelCount( p->ioH, devIdx, io::kOutFl );
        d->link      = p->devL;
        p->devL      = d;
        p->maxAppId  = std::max(baseAppId+kMaxAppId,p->maxAppId);

      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"Audio meter device registration failed.");

        return rc;
      }
      

      rc_t _create_meters( audio_panel_t* p, audio_panel_device_t* d, unsigned wsSessId, unsigned chCnt, bool inputFl )
      {
        rc_t        rc          = kOkRC;

        if( chCnt == 0 )
          return rc;
        
        unsigned    parentUuId  = ui::kRootAppId;
        unsigned    divUuId     = kInvalidId;
        unsigned    titleUuId   = kInvalidId;
        unsigned    chanId      = kInvalidId;
        const char* title       = inputFl ? "Input"        : "Output";
        const char* divEleName  = inputFl ? "inMeterId"    : "outMeterId";
        unsigned    divAppId    = inputFl ? kInMeterDivId  : kOutMeterDivId;
        unsigned    baseMeterId = inputFl ? kInMeterBaseId : kOutMeterBaseId;
        unsigned    baseToneId  = inputFl ? kInToneBaseId  : kOutToneBaseId;
        unsigned    baseMuteId  = inputFl ? kInMuteBaseId  : kOutMuteBaseId;
        unsigned    baseGainId  = inputFl ? kInGainBaseId  : kOutGainBaseId;
        unsigned    colUuId;
        unsigned    uuid;

        uiCreateLabel(p->ioH, titleUuId,  parentUuId, nullptr,    kInvalidId, chanId, nullptr, title );
        uiCreateDiv(  p->ioH, divUuId,    parentUuId, divEleName, d->baseAppId + divAppId, chanId,  "uiRow", nullptr );

        uiCreateDiv(  p->ioH, colUuId,  divUuId, nullptr, kInvalidId, chanId,  "uiCol", nullptr );        
        uiCreateLabel(p->ioH, uuid,     colUuId, nullptr, kInvalidId, chanId, nullptr, "Tone" );
        uiCreateLabel(p->ioH, uuid,     colUuId, nullptr, kInvalidId, chanId, nullptr, "Mute" );
        uiCreateLabel(p->ioH, uuid,     colUuId, nullptr, kInvalidId, chanId, nullptr, "Gain" );
        uiCreateLabel(p->ioH, uuid,     colUuId, nullptr, kInvalidId, chanId, nullptr, "Meter" );
      
        for(unsigned i=0; i<chCnt; ++i)
        {
          unsigned chLabelN = 32;
          char     chLabel[ chLabelN+1 ];
          snprintf(chLabel,chLabelN,"%i",i+1);
        
          uiCreateDiv(  p->ioH, colUuId,  divUuId, nullptr, kInvalidId, kInvalidId, "uiCol", chLabel );        
          uiCreateCheck(p->ioH, uuid,     colUuId, nullptr, d->baseAppId + baseToneId  + i, i, "checkClass", nullptr, false );
          uiCreateCheck(p->ioH, uuid,     colUuId, nullptr, d->baseAppId + baseMuteId  + i, i, "checkClass", nullptr, false );
          uiCreateNumb( p->ioH, uuid,     colUuId, nullptr, d->baseAppId + baseGainId  + i, i, "floatClass", nullptr,    0.0, 3.0, 0.001, 3, 0 );
          uiCreateNumb( p->ioH, uuid,     colUuId, nullptr, d->baseAppId + baseMeterId + i, i, "floatClass", nullptr, -100.0, 100, 1, 2, 0 );
        } 
      
        return rc;
      }
    
      // Called when an new UI connects to the engine.
      rc_t _uiInit( audio_panel_t* p, const ui_msg_t& m )
      {
        rc_t                  rc = kOkRC;
        audio_panel_device_t* d  = p->devL;
        
        for(; d!=nullptr; d=d->link)
        {
          _create_meters( p, d, kInvalidId, d->iChCnt, true );
          _create_meters( p, d, kInvalidId, d->oChCnt, false );
        }
        
        return rc;
      }

      // Messages from UI to engine.
      rc_t _uiValue( audio_panel_t* p, const ui_msg_t& m )
      {
        rc_t                  rc = kOkRC;
        audio_panel_device_t* d;

        if((d = _app_id_to_device(p, m.appId )) != nullptr)
        {
          
        }

        return rc;
      }


      // Request from UI for engine value.
      rc_t _uiEcho( audio_panel_t* p, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _uiCb( audio_panel_t* p, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;
        switch( m.opId )
        {
          case ui::kConnectOpId:
            //cwLogInfo("IO Test Connect: wsSessId:%i.",m.wsSessId);
            break;
          
          case ui::kDisconnectOpId:
            //cwLogInfo("IO Test Disconnect: wsSessId:%i.",m.wsSessId);          
            break;
          
          case ui::kInitOpId:
            rc = _uiInit(p,m);
            break;

          case ui::kValueOpId:
            rc = _uiValue(p, m );
            break;

          case ui::kEchoOpId:
            rc = _uiEcho( p, m );
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
      
      rc_t _audioMeterCb( audio_panel_t* p, const audio_group_dev_t& agd )
      {
        rc_t rc = kOkRC;
        audio_panel_device_t* apd;
        
        if(( apd = _get_device(p,agd.devIdx)) != nullptr )
        {
          unsigned meterAppId = cwIsFlag(agd.flags,kInFl)  ? kInMeterBaseId : kOutMeterBaseId;
          unsigned chCnt      = cwIsFlag(agd.flags,kInFl)  ? apd->iChCnt    : apd->oChCnt;

          for(unsigned i=0; i<chCnt; ++i)
          {
            unsigned appId = apd->baseAppId + meterAppId + i;
            uiSendValue(  p->ioH, uiFindElementUuId(p->ioH,appId), agd.meterA[i] );
          }
          return kOkRC;
          
        }
        return rc;
      }
    }
  }
}

cw::rc_t cw::io::audio_panel::create(  handle_t& hRef, io::handle_t ioH, unsigned baseAppId )
{
  rc_t rc = kOkRC;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  audio_panel_t* p = mem::allocZ<audio_panel_t>();

  p->ioH = ioH;

  // create the audio panels for each audio device
  for(unsigned i=0; i<audioDeviceCount(ioH); ++i)
    if( io::audioDeviceIsActive(ioH,i))
    {
      if((rc = _registerDevice(p, baseAppId, i, 0 )) != kOkRC )
      {
        rc = cwLogError(rc,"Audio panel device registration failed.");
        goto errLabel;
      }

      baseAppId += kMaxAppId;
    }


  
  hRef.set(p);

 errLabel:
  return rc;
    
}

cw::rc_t cw::io::audio_panel::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  audio_panel_t* p = _handleToPtr(hRef);
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}



cw::rc_t cw::io::audio_panel::registerDevice( handle_t h, unsigned baseAppId, unsigned devIdx, float dfltGain )
{
  audio_panel_t*        p      = _handleToPtr(h);
  return _registerDevice(p,baseAppId,devIdx,dfltGain); 
}

cw::rc_t cw::io::audio_panel::exec(  handle_t h, const msg_t& m )
{
  rc_t           rc = kOkRC;
  audio_panel_t* p  = _handleToPtr(h);
  
  switch( m.tid )
  {
    case kSerialTId:
      break;
          
    case kMidiTId:
      break;
          
    case kAudioTId:
      break;

    case kAudioMeterTId:
      //printf("."); fflush(stdout);
      if( m.u.audioGroupDev != nullptr )
        rc = _audioMeterCb(p,*m.u.audioGroupDev);
      break;
          
    case kSockTId:
      break;
          
    case kWebSockTId:
      break;
          
    case kUiTId:
      rc = _uiCb(p,m.u.ui);
      break;

    case kExecTId:
      break;
      
    default:
      assert(0);
        
  }

  return rc;
  
}

unsigned cw::io::audio_panel::maxAppId( handle_t h )
{
  audio_panel_t* p  = _handleToPtr(h);
  return p->maxAppId;
}
