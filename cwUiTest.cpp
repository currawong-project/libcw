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
      handle_t uiH;
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
     kProgressId
    };

    rc_t _uiTestCreateUi( ui_test_t* p, unsigned connId )
    {
      rc_t     rc      = kOkRC;
      unsigned uuid    = kInvalidId;
      unsigned selUuId = kInvalidId;
      unsigned divUuId = kInvalidId;

      if((rc = createDiv( p->uiH, divUuId, kInvalidId, "myDivId", kDivId, "divClass", "My Panel" )) != kOkRC )
        goto errLabel;
      
      if((rc = createButton( p->uiH, uuid, divUuId, "myBtnId", kBtnId, "btnClass", "Push Me" )) != kOkRC )
        goto errLabel;

      if((rc = createCheck( p->uiH, uuid, divUuId, "myCheckId", kCheckId, "checkClass", "Check Me", true )) != kOkRC )
        goto errLabel;
      
      if((rc = createSelect( p->uiH, selUuId, divUuId, "mySelId", kSelectId, "selClass", "Select" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( p->uiH, uuid, selUuId, "myOpt0Id", kOption0Id, "optClass", "Option 0" )) != kOkRC )
        goto errLabel;

      if((rc = createOption( p->uiH, uuid, selUuId, "myOpt1Id", kOption1Id, "optClass", "Option 1" )) != kOkRC )
        goto errLabel;
      
      if((rc = createOption( p->uiH, uuid, selUuId, "myOpt2Id", kOption2Id, "optClass", "Option 2" )) != kOkRC )
        goto errLabel;

      if((rc = createString( p->uiH, uuid, divUuId, "myStringId", kStringId, "stringClass", "String", "a string value" )) != kOkRC )
        goto errLabel;
      
      if((rc = createNumber( p->uiH, uuid, divUuId, "myNumberId", kNumberId, "numberClass", "Number", 10, 0, 100, 1, 0 )) != kOkRC )
        goto errLabel;
      
      if((rc = createProgress(  p->uiH, uuid, divUuId, "myProgressId", kProgressId, "progressClass", "Progress", 5, 0, 10 )) != kOkRC )
        goto errLabel;
      
      
    errLabel:
      return rc;
    }

    rc_t _handleUiValueMsg( ui_test_t* p, unsigned connId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* v )
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

    
    rc_t _uiTestCallback( void* cbArg, unsigned connId, opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* v )
    {
      ui_test_t* p = (ui_test_t*)cbArg;
      
      switch( opId )
      {
        case kConnectOpId:
          cwLogInfo("Connect: connId:%i.",connId);
          break;
          
        case kDisconnectOpId:
          cwLogInfo("Disconnect: connId:%i.",connId);
          break;
          
        case kInitOpId:
          _uiTestCreateUi(p,connId);
          break;

        case kValueOpId:
          _handleUiValueMsg( p, connId, parentAppId, uuId, appId, v );
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
  const char*    physRootDir      = "/home/kevin/src/cw_rt/html/uiTest";
  const char*    dfltPageFn       = "index.html";
  int            port             = 5687;
  unsigned       rcvBufByteN      = 2048;
  unsigned       xmtBufByteN      = 2048;
  unsigned       websockTimeOutMs = 50;
  const unsigned sbufN            = 31;
  char           sbuf[ sbufN+1 ];  
  ui_test_t*     ui = mem::allocZ<ui_test_t>();
    
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
