#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwMath.h"
#include "cwIoPresetSelApp.h"

using namespace cw;

int main( int argc, const char* argv[] )
{
  
  rc_t rc = kOkRC;
  object_t* cfg = nullptr;
  const object_t* obj = nullptr;
  cw::log::log_args_t log_args;

  init_minimum_args( log_args );

  cw::log::createGlobal(log_args);
  
  if( argc < 2 )
  {
    printf("Usage: preset_sel <cfg_file>\n");
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

  if((rc = obj->getv("preset_sel",obj)) != kOkRC )
  {
    printf("The 'preset_sel' object was not found in the cfg. file '%s'.",cwStringNullGuard(argv[1]));
    goto errLabel;
  }
  
  if((rc = preset_sel_app::main(obj,argc-1,argv+1)) != kOkRC )
  {
    printf("The 'preset_sel' application reported an error '%i' on exit.",rc);
    goto errLabel;
  }


errLabel:

  if( cfg != nullptr )
      cfg->free();

  cw::log::destroyGlobal();
  
  return rc == kOkRC ? 0 : 1;
  
}
