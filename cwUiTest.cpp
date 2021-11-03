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
      
      //const char*   uiCfgFn;   // Resource file name
      srv::handle_t wsUiSrvH;  // 

      std::atomic<bool> quitFl;

      // Application values
      bool     appCheckFl;
      unsigned appSelOptAppId;
      int      appInteger;
      float    appFloat;
      int      appProgress;
      char*    appString;
      bool     appCheck1Fl;
      bool     appCheck2Fl;
      float    appNumb;
      unsigned appSelId;

      unsigned myPanelUuId;
      unsigned logUuId;

      const object_t* listEleCfg;
      
    } ui_test_t;

    // Application Id's for UI elements
    enum
    {
      // Programatically created UI elements
     kDivId,
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
     kLogId,

     // Resource Based elements
     kQuitBtnId,
     kPanelDivId,
     kPanelBtn1Id,
     kPanelCheck1Id,
     kPanelBtn2Id,
     kPanelCheck2Id,
     kPanelFloaterId,
     kSelId,
     kOpt1Id,
     kOpt2Id,
     kOpt3Id,
     kListId
    };

    rc_t uiTestCreateUi( ui_test_t* p, unsigned wsSessId=kInvalidId )
    {
      rc_t     rc      = kOkRC;
      unsigned uuid    = kInvalidId;
      unsigned selUuId = kInvalidId;
      unsigned chanId  = 0;
      
      handle_t uiH = srv::uiHandle(p->wsUiSrvH);

      // Create a UI elements programatically.

      if((rc = createDiv( uiH, p->myPanelUuId,  kInvalidId, "myDivId", kDivId, chanId, "divClass", "My Panel" )) != kOkRC )
        goto errLabel;

      if((rc = createCheck( uiH, uuid,  p->myPanelUuId, "myCheckId", kCheckId, chanId, "checkClass", "Check Me", true )) != kOkRC )
        goto errLabel;
      
      if((rc = createSelect( uiH, selUuId,  p->myPanelUuId, "mySelId", kSelectId, chanId, "selClass", "Select" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( uiH, uuid,  selUuId, "myOpt0Id", kOption0Id, chanId, "optClass", "Option 0" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( uiH, uuid,  selUuId, "myOpt1Id", kOption1Id, chanId, "optClass", "Option 1" )) != kOkRC )
        goto errLabel;
      
      if((rc = createOption( uiH, uuid,  selUuId, "myOpt2Id", kOption2Id, chanId, "optClass", "Option 2" )) != kOkRC )
        goto errLabel;

      if((rc = createStr( uiH, uuid,  p->myPanelUuId, "myStringId", kStringId, chanId, "stringClass", "String", "a string value" )) != kOkRC )
        goto errLabel;
      
      if((rc = createNumb( uiH, uuid,  p->myPanelUuId, "myIntegerId", kIntegerId, chanId, "integerClass", "Integer", 0, 100, 1, 0, 10 )) != kOkRC )
        goto errLabel;

      if((rc = createNumb( uiH, uuid,  p->myPanelUuId, "myFloatId", kFloatId, chanId, "floatClass", "Float", 0.53, 100.97, 1.0, 5, 10.0 )) != kOkRC )
        goto errLabel;
      
      if((rc = createProg(  uiH, uuid,  p->myPanelUuId, "myProgressId", kProgressId, chanId, "progressClass", "Progress", 0, 10, 5 )) != kOkRC )
        goto errLabel;
      
      if((rc = createLog( uiH, p->logUuId,  p->myPanelUuId, "myLogId", kLogId, chanId, "logClass", "My Log (click toggles auto-scroll)" )) != kOkRC )
        goto errLabel;

    errLabel:
      return rc;
    }

    void _print_state( ui_test_t* p )
    {
      printf("check:%i sel:%i int:%i flt:%f prog:%i str:%s chk1:%i chk2:%i numb:%f sel:%i\n",
        p->appCheckFl,
        p->appSelOptAppId,
        p->appInteger,
        p->appFloat,
        p->appProgress,
        p->appString,
        p->appCheck1Fl,
        p->appCheck2Fl,
        p->appNumb,
        p->appSelId);
    }


    rc_t _insert_list_ele( ui_test_t* p )
    {
      rc_t     rc   = kOkRC;

      if( p->listEleCfg != nullptr )
      {
        handle_t uiH       = srv::uiHandle(p->wsUiSrvH);
        unsigned listUuId  = findElementUuId( uiH, kListId );

        printf("list uuid:%i\n",listUuId);

        rc = createFromObject( uiH, p->listEleCfg,  listUuId, kInvalidId );        
      }
      
      return rc;
    }


    rc_t _insert_log_line( ui_test_t* p,  const char* text )
    {
      return ui::setLogLine( srv::uiHandle(p->wsUiSrvH),  p->logUuId, text );
    }

    rc_t _handleUiValueMsg( ui_test_t* p, unsigned wsSessId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* v )
    {
      rc_t rc = kOkRC;
      
      switch( appId )
      {
        case kQuitBtnId:
          p->quitFl.store(true);
          break;

        case kCheckId:
          printf("Check:%i\n", v->u.b);
          p->appCheckFl = v->u.b;
          _insert_log_line( p, "check!\n" );
          break;

        case kSelectId:
          printf("Selected: option appId:%i\n", v->u.i);
          p->appSelOptAppId = v->u.i;
          break;

        case kStringId:
          printf("String: %s\n",v->u.s);
          mem::release(p->appString);
          if( v->u.s != nullptr )
            p->appString = mem::duplStr(v->u.s);
          break;
          
        case kIntegerId:
          {
            printf("Integer: %i\n",v->u.i);
            p->appInteger = v->u.i;
          
            handle_t uiH = srv::uiHandle(p->wsUiSrvH);
            unsigned progUuId = findElementUuId(   uiH, p->myPanelUuId, kProgressId );
            sendValueInt(   uiH, progUuId, v->u.i );
          }
          break;
          
        case kFloatId:
          printf("Float: %f\n",v->u.f);
          p->appFloat = v->u.f;


        case kPanelBtn1Id:
          printf("Button 1\n");
          _print_state(p);
          break;
          
        case kPanelCheck1Id:
          printf("check 1: %i\n",v->u.b);
          p->appCheck1Fl = v->u.b;
          ui::report( srv::uiHandle(p->wsUiSrvH) );

          break;
          
        case kPanelBtn2Id:
          printf("Button 2\n");
          _print_state(p);
          break;
          
        case kPanelCheck2Id:
          printf("check 1: %i\n",v->u.b);
          p->appCheck1Fl = v->u.b;
          _insert_list_ele( p );
          break;
          
        case kPanelFloaterId:
          printf("numb: %f\n",v->u.f);
          p->appNumb = v->u.f;
          break;

        case kSelId:
          printf("sel: %i\n",v->u.i);
          p->appSelId = v->u.i;
          break;
      }

      return rc;
    }

    // 
    rc_t _handleUiEchoMsg( ui_test_t* p, unsigned wsSessId, unsigned parentAppId, unsigned  uuId, unsigned appId )
    {
      rc_t rc = kOkRC;
      
      switch( appId )
      {
        case kCheckId:
          sendValueBool( uiHandle( p->wsUiSrvH ),  uuId, p->appCheckFl );
          break;

        case kSelectId:
          sendValueInt( uiHandle( p->wsUiSrvH ),  uuId, p->appSelOptAppId );
          break;

        case kStringId:
          sendValueString( uiHandle( p->wsUiSrvH ),  uuId, p->appString );
          break;
          
        case kIntegerId:
          sendValueInt( uiHandle( p->wsUiSrvH ),  uuId, p->appInteger );
          break;

        case kFloatId:
          sendValueFloat( uiHandle( p->wsUiSrvH ),  uuId, p->appFloat );
          break;
          
        case kProgressId:
          sendValueInt( uiHandle( p->wsUiSrvH ),  uuId, p->appProgress );
          break;

        case kPanelCheck1Id:
          sendValueBool( uiHandle( p->wsUiSrvH ),  uuId, p->appCheck1Fl);
          break;
          
        case kPanelCheck2Id:
          sendValueBool( uiHandle( p->wsUiSrvH ),  uuId, p->appCheck2Fl);
          break;
          
        case kPanelFloaterId:
          sendValueFloat( uiHandle( p->wsUiSrvH ),  uuId, p->appNumb );
          break;

        case kSelId:
          sendValueInt( uiHandle( p->wsUiSrvH ),  uuId, p->appSelId );
          break;
      }
      return rc;
    }
    


    // This function is called by the websocket with messages coming from a remote UI.
    rc_t _uiTestCallback( void* cbArg, unsigned wsSessId, opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, unsigned chanId, const value_t* v )
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
          //_uiTestCreateUi(p,wsSessId);
          break;

        case kValueOpId:
          _handleUiValueMsg( p, wsSessId, parentAppId, uuId, appId, v );
          break;

        case kClickOpId:
          {
            printf("APP clicked. uu:%i app:%i ch:%i\n",uuId,appId,chanId);

            handle_t uiH = srv::uiHandle(p->wsUiSrvH);
            bool selectedFl = isSelected(uiH,uuId);
            setSelect(   uiH, uuId, !selectedFl );
          
          }
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

cw::rc_t cw::ui::test( const object_t* cfg )
{
  rc_t           rc   = kOkRC;
  ui::ws::args_t args = {};
  
  // Application Id's for the resource based UI elements.
  appIdMap_t mapA[] =
    {
     { kRootAppId,  kQuitBtnId,      "myQuitBtnId" },
     { kRootAppId,  kPanelDivId,     "panelDivId" },
     { kPanelDivId, kPanelBtn1Id,    "myBtn1Id" },
     { kPanelDivId, kPanelCheck1Id,  "myCheck1Id" },
     { kPanelDivId, kPanelBtn2Id,    "myBtn2Id" },
     { kPanelDivId, kPanelCheck2Id,  "myCheck2Id" },
     { kPanelDivId, kPanelFloaterId, "myFloater" },
     { kPanelDivId, kSelId,          "mySel" },
     { kSelId,      kOpt1Id,         "myOpt1" },
     { kSelId,      kOpt2Id,         "myOpt2" },
     { kSelId,      kOpt3Id,         "myOpt3" },
     { kPanelDivId, kListId,         "myListId" }

    };

  unsigned mapN = sizeof(mapA)/sizeof(mapA[0]);
  ui_test_t*     app = mem::allocZ<ui_test_t>();

  if( cfg == nullptr )
  {
    cwLogError(kInvalidArgRC,"ui::test() was not passed a valid cfg. object.");
    goto errLabel;
  }
  
  if((rc = parseArgs(*cfg, args )) != kOkRC )
  {
    cwLogError(rc,"UI parse args failed in ui::test()");
    goto errLabel;
  }
  
  app->quitFl.store(false);

  // Initial values for the test applications
  app->appCheckFl     = true;
  app->appSelOptAppId = kOption1Id;
  app->appInteger     = 5;
  app->appFloat       = 2.56;
  app->appProgress    = 7;
  app->appString      = mem::duplStr("fooz");
  app->appCheck1Fl    = false;
  app->appCheck2Fl    = true;
  app->appNumb        = 1.23;
  app->appSelId       = kOpt3Id;

  if((rc = cfg->getv( "listEle", app->listEleCfg )) != kOkRC )
    rc = cwLogError( rc, "The list element cfg. was not found.");
  

  //app->uiCfgFn = "/home/kevin/src/cwtest/src/libcw/html/uiTest/ui.cfg";

  // create the UI server
  if((rc = srv::create(app->wsUiSrvH, args, app, _uiTestCallback, mapA, mapN, nullptr )) != kOkRC )
    return rc;

  if((rc = uiTestCreateUi( app )) != kOkRC )
    goto errLabel;

  //ui::report( srv::uiHandle(app->wsUiSrvH) );
  
  // start the UI server
  if((rc = srv::start(app->wsUiSrvH)) != kOkRC )
    goto errLabel;

  
  // readline loop
  while( !app->quitFl.load()  )
  {
    sleepMs(50);
  }

 errLabel:
  ui::ws::releaseArgs(args);
  
  rc_t rc1 = kOkRC;
  if( app->wsUiSrvH.isValid() )
    rc1 = srv::destroy(app->wsUiSrvH);

  mem::release(app->appString);
  mem::release(app);
  
  return rcSelect(rc,rc1);   
}
