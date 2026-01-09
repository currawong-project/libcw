#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwMath.h"
#include "cwNbMpScQueue.h"
#include "cwMpScNbCircQueue.h"
#include "cwMtQueueTester.h"

#include "cwMtQueueTester.h"

using namespace cw;

int main( int argc, char** argv )
{
  
  rc_t rc = kOkRC;
  object_t* cfg = nullptr;
  const object_t* obj = nullptr;
  cw::log::log_args_t log_args;

  init_minimum_args( log_args );

  cw::log::createGlobal(log_args);
  
  if( argc < 2 )
  {
    printf("Usage: mt_queue <cfg_file>\n");
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

  if((rc = obj->getv("nbmpscQueue",obj)) != kOkRC )
  {
    printf("The 'mt_queue' object was not found in the cfg. file '%s'.",cwStringNullGuard(argv[1]));
    goto errLabel;
  }
  
  if((rc = mt_queue_tester::test(obj)) != kOkRC )
  {
    printf("The 'mt_queue' application reported an error '%i' on exit.",rc);
    goto errLabel;
  }


errLabel:

  if( cfg != nullptr )
      cfg->free();

  cw::log::destroyGlobal();
  
  return rc == kOkRC ? 0 : 1;
  
}
