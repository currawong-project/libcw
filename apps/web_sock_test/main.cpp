#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwThread.h"
#include "cwWebSock.h"
#include "cwWebSockSvr.h"

using namespace cw;

int main( int argc, char** argv )
{
  
  rc_t rc = kOkRC;
  object_t* cfg = nullptr;
  const object_t* obj = nullptr;

  cw::log::createGlobal();
  
  if( argc < 2 )
  {
    printf("Usage: web_sock_test <cfg_file>\n");
    rc = kSyntaxErrorRC;
    goto errLabel;
  }

  if((rc = objectFromFile( argv[1], cfg )) != kOkRC )
  {
    printf("The configuration could not be read from the file '%s'.",cwStringNullGuard(argv[1]));
    goto errLabel;
  }

  if((rc = cfg->getv("test",obj)) != kOkRC )
  {
    printf("The 'test' object was not found in the cfg. file '%s'.",cwStringNullGuard(argv[1]));
    goto errLabel;
  }

  if((rc = obj->getv("websockSrv",obj)) != kOkRC )
  {
    printf("The 'web_sock_test' object was not found in the cfg. file '%s'.",cwStringNullGuard(argv[1]));
    goto errLabel;
  }
  
  if((rc = cw::websockSrvTest(obj)) != kOkRC )
  {
    printf("The 'web_sock_test' application reported an error '%i' on exit.",rc);
    goto errLabel;
  }


errLabel:

  if( cfg != nullptr )
      cfg->free();

  cw::log::destroyGlobal();
  
  return rc == kOkRC ? 0 : 1;
  
}
