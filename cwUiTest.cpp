#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
#include "cwWebSock.h"
#include "cwObject.h"
#include "cwUi.h"
#include "cwUiTest.h"


namespace cw
{
  namespace ui
  {
    typedef struct ui_test_str
    {
      const char*   uiCfgFn;
      srv::handle_t wsUiSrvH;
      
      bool     appCheckFl;
      unsigned appSelectIndex;
      int      appInteger;
      float    appFloat;
      int      appProgress;
      char*    appString;
      
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
     kIntegerId,
     kFloatId,
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
      
      handle_t uiH = srv::uiHandle(p->wsUiSrvH);

      
      registerAppIds(uiH, mapA, sizeof(mapA)/sizeof(mapA[0]));

      if((rc = createDiv( uiH, divUuId, wsSessId, kInvalidId, "myDivId", kDivId, "divClass", "My Panel" )) != kOkRC )
        goto errLabel;

      if((rc = createButton( uiH, uuid, wsSessId, divUuId, "myBtnId", kBtnId, "btnClass", "Push Me" )) != kOkRC )
        goto errLabel;

      if((rc = createCheck( uiH, uuid, wsSessId, divUuId, "myCheckId", kCheckId, "checkClass", "Check Me", true )) != kOkRC )
        goto errLabel;
      
      if((rc = createSelect( uiH, selUuId, wsSessId, divUuId, "mySelId", kSelectId, "selClass", "Select" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( uiH, uuid, wsSessId, selUuId, "myOpt0Id", kOption0Id, "optClass", "Option 0" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( uiH, uuid, wsSessId, selUuId, "myOpt1Id", kOption1Id, "optClass", "Option 1" )) != kOkRC )
        goto errLabel;
      
      if((rc = createOption( uiH, uuid, wsSessId, selUuId, "myOpt2Id", kOption2Id, "optClass", "Option 2" )) != kOkRC )
        goto errLabel;

      if((rc = createString( uiH, uuid, wsSessId, divUuId, "myStringId", kStringId, "stringClass", "String", "a string value" )) != kOkRC )
        goto errLabel;
      
      if((rc = createNumber( uiH, uuid, wsSessId, divUuId, "myIntegerId", kIntegerId, "integerClass", "Integer", 10, 0, 100, 1, 0 )) != kOkRC )
        goto errLabel;

      if((rc = createNumber( uiH, uuid, wsSessId, divUuId, "myFloatId", kFloatId, "floatClass", "Float", 10.0, 0.53, 100.97, 1.0, 5 )) != kOkRC )
        goto errLabel;
      
      if((rc = createProgress(  uiH, uuid, wsSessId, divUuId, "myProgressId", kProgressId, "progressClass", "Progress", 5, 0, 10 )) != kOkRC )
        goto errLabel;

      if((rc = createFromFile( uiH, p->uiCfgFn, wsSessId )) != kOkRC )
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
          p->appCheckFl = v->u.b;
          break;

        case kSelectId:
          printf("Selected: optionId:%i\n", v->u.i);
          p->appSelectIndex = v->u.i;
          break;

        case kStringId:
          printf("String: %s\n",v->u.s);
          mem::release(p->appString);
          if( v->u.s != nullptr )
            p->appString = mem::duplStr(v->u.s);
          break;
          
        case kIntegerId:
          printf("Integer: %i\n",v->u.i);
          p->appInteger = v->u.i;
          break;
          
        case kFloatId:
          printf("Float: %f\n",v->u.f);
          p->appFloat = v->u.f;
          
      }

      return rc;
    }

    rc_t _handleUiEchoMsg( ui_test_t* p, unsigned wsSessId, unsigned parentAppId, unsigned  uuId, unsigned appId )
    {
      rc_t rc = kOkRC;
      
      switch( appId )
      {
        case kCheckId:
          sendValueBool( uiHandle( p->wsUiSrvH ), wsSessId, uuId, p->appCheckFl );
          break;

        case kSelectId:
          sendValueInt( uiHandle( p->wsUiSrvH ), wsSessId, uuId, p->appSelectIndex );
          break;

        case kStringId:
          sendValueString( uiHandle( p->wsUiSrvH ), wsSessId, uuId, p->appString );
          break;
          
        case kIntegerId:
          sendValueInt( uiHandle( p->wsUiSrvH ), wsSessId, uuId, p->appInteger );
          break;

        case kFloatId:
          sendValueFloat( uiHandle( p->wsUiSrvH ), wsSessId, uuId, p->appFloat );
          break;
          
        case kProgressId:
          sendValueInt( uiHandle( p->wsUiSrvH ), wsSessId, uuId, p->appProgress );
          break;
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
          cwLogInfo("UI Test Connect: wsSessId:%i.",wsSessId);
          break;
          
        case kDisconnectOpId:
          cwLogInfo("UI Test Disconnect: wsSessId:%i.",wsSessId);          
          break;
          
        case kInitOpId:
          _uiTestCreateUi(p,wsSessId);
          break;

        case kValueOpId:
          _handleUiValueMsg( p, wsSessId, parentAppId, uuId, appId, v );
          break;

        case kEchoOpId:
          _handleUiEchoMsg( p, wsSessId, parentAppId, uuId, appId );
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
  unsigned       fmtBufByteN      = 4096;
  unsigned       websockTimeOutMs = 50;
  const unsigned sbufN            = 31;
  char           sbuf[ sbufN+1 ];  
  ui_test_t*     app = mem::allocZ<ui_test_t>();


  app->appCheckFl     = true;
  app->appSelectIndex = 1;
  app->appInteger     = 5;
  app->appFloat       = 2.56;
  app->appProgress    = 7;
  app->appString      = mem::duplStr("fooz");
  

  app->uiCfgFn = "/home/kevin/src/cwtest/src/libcw/html/uiTest/ui.cfg";

  // create the UI server
  if((rc = srv::create(app->wsUiSrvH, port, physRootDir, app, _uiTestCallback, nullptr, dfltPageFn, websockTimeOutMs, rcvBufByteN, xmtBufByteN, fmtBufByteN )) != kOkRC )
    return rc;
  
  
  // start the UI server
  if((rc = srv::start(app->wsUiSrvH)) != kOkRC )
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
  if( app->wsUiSrvH.isValid() )
    rc1 = srv::destroy(app->wsUiSrvH);

  mem::release(app->appString);
  mem::release(app);
  
  return rcSelect(rc,rc1);   
}
