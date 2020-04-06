#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwUi.h"
#include "cwUiTest.h"


namespace cw
{
  namespace ui
  {
    typedef struct ui_test_str
    {
      handle_t    uiH;
      const char* uiCfgFn;
    } ui_test_t;

    enum
    {
     kDivId,
     kBtnId,
     kCheckId,
     kSelectId,
     kOption0Id,
     kOption1Id,
     kOption2Id,
     kOption3Id,
     kStringId,
     kNumberId,
     kProgressId,

     kPanelDivId,
     kPanelBtnId,
     kPanelCheckId
    };

    rc_t _uiTestCreateUi( ui_test_t* p, unsigned wsSessId )
    {
      rc_t     rc      = kOkRC;
      unsigned uuid    = kInvalidId;
      unsigned selUuId = kInvalidId;
      unsigned divUuId = kInvalidId;

      appIdMap_t mapA[] =
      {
       { ui::kRootAppId, kPanelDivId, "panelDivId" },
       { ui::kPanelDivId, kPanelBtnId, "myBtn1Id" },
       { ui::kPanelDivId, kPanelCheckId, "myCheck1Id" },
      };

      registerAppIds(p->uiH, mapA, sizeof(mapA)/sizeof(mapA[0]));

      if((rc = createDiv( p->uiH, divUuId, wsSessId, kInvalidId, "myDivId", kDivId, "divClass", "My Panel" )) != kOkRC )
        goto errLabel;
      
      if((rc = createButton( p->uiH, uuid, wsSessId, divUuId, "myBtnId", kBtnId, "btnClass", "Push Me" )) != kOkRC )
        goto errLabel;

      if((rc = createCheck( p->uiH, uuid, wsSessId, divUuId, "myCheckId", kCheckId, "checkClass", "Check Me", true )) != kOkRC )
        goto errLabel;
      
      if((rc = createSelect( p->uiH, selUuId, wsSessId, divUuId, "mySelId", kSelectId, "selClass", "Select" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( p->uiH, uuid, wsSessId, selUuId, "myOpt0Id", kOption0Id, "optClass", "Option 0" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( p->uiH, uuid, wsSessId, selUuId, "myOpt1Id", kOption1Id, "optClass", "Option 1" )) != kOkRC )
        goto errLabel;
      
      if((rc = createOption( p->uiH, uuid, wsSessId, selUuId, "myOpt2Id", kOption2Id, "optClass", "Option 2" )) != kOkRC )
        goto errLabel;

      if((rc = createString( p->uiH, uuid, wsSessId, divUuId, "myStringId", kStringId, "stringClass", "String", "a string value" )) != kOkRC )
        goto errLabel;
      
      if((rc = createNumber( p->uiH, uuid, wsSessId, divUuId, "myNumberId", kNumberId, "numberClass", "Number", 10, 0, 100, 1, 0 )) != kOkRC )
        goto errLabel;
      
      if((rc = createProgress(  p->uiH, uuid, wsSessId, divUuId, "myProgressId", kProgressId, "progressClass", "Progress", 5, 0, 10 )) != kOkRC )
        goto errLabel;

      if((rc = createFromFile( p->uiH, p->uiCfgFn, wsSessId )) != kOkRC )
        goto errLabel;
      
    errLabel:
      return rc;
    }

    rc_t _handleUiValueMsg( ui_test_t* p, unsigned wsSessId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* v )
    {
      rc_t rc = kOkRC;
      
      switch( appId )
      {
        case kBtnId:   
          printf("Click!\n");
          break;

        case kCheckId:
          printf("Check:%i\n", v->u.b);
          break;

        case kSelectId:
          printf("Selected: optionId:%i\n", v->u.i);
          break;

        case kStringId:
          printf("String: %s\n",v->u.s);

        case kNumberId:
          if( v->tid == kIntTId )
            printf("Number: %i\n",v->u.i);
          else
            printf("Number: %f\n",v->u.d);
          
      }

      return rc;
    }
    

    // This function is called by the websocket with messages comring from a remote UI.
    rc_t _uiTestCallback( void* cbArg, unsigned wsSessId, opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* v )
    {
      ui_test_t* p = (ui_test_t*)cbArg;
      
      switch( opId )
      {
        case kConnectOpId:
          cwLogInfo("Connect: wsSessId:%i.",wsSessId);
          break;
          
        case kDisconnectOpId:
          cwLogInfo("Disconnect: wsSessId:%i.",wsSessId);
          break;
          
        case kInitOpId:
          _uiTestCreateUi(p,wsSessId);
          break;

        case kRegisterOpId:
          break;


        case kValueOpId:
          _handleUiValueMsg( p, wsSessId, parentAppId, uuId, appId, v );
          break;

        case kInvalidOpId:
          // fall through
        default:
          break;
        
      }
      return kOkRC; 
    }    
  }
}

cw::rc_t cw::ui::test( )
{
  rc_t           rc               = kOkRC;
  const char*    physRootDir      = "/home/kevin/src/cwtest/src/libcw/html/uiTest";
  const char*    dfltPageFn       = "index.html";
  int            port             = 5687;
  unsigned       rcvBufByteN      = 2048;
  unsigned       xmtBufByteN      = 2048;
  unsigned       websockTimeOutMs = 50;
  const unsigned sbufN            = 31;
  char           sbuf[ sbufN+1 ];  
  ui_test_t*     ui = mem::allocZ<ui_test_t>();

  ui->uiCfgFn = "/home/kevin/src/cwtest/src/libcw/html/uiTest/ui.cfg";
    
  if((rc = createUi(ui->uiH, port, _uiTestCallback, ui, physRootDir, dfltPageFn, websockTimeOutMs, rcvBufByteN, xmtBufByteN )) != kOkRC )
    return rc;

  if((rc = start(ui->uiH)) != kOkRC )
    goto errLabel;

  
  printf("'quit' to exit\n");

  // readline loop
  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      printf("Sending:%s",sbuf);

      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

 errLabel:
  rc_t rc1 = kOkRC;
  if( ui->uiH.isValid() )
    rc1 = destroyUi(ui->uiH);
  
  mem::release(ui);
  
  return rcSelect(rc,rc1);   
}
