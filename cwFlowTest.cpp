#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"

#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowTest.h"


cw::rc_t cw::flow::test( const test::test_args_t& args )
{
  rc_t        rc               = kOkRC;
  const char* proc_cfg_fname   = nullptr;
  const char* subnet_cfg_fname = nullptr;
  object_t*   class_cfg        = nullptr;
  object_t*   subnet_cfg       = nullptr;
  handle_t    flowH;

  if( args.module_args == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The flow test cases require module args.");
    goto errLabel;
  }
  
  if((rc = args.module_args->readv("proc_cfg_fname",0,proc_cfg_fname,
                                   "subnet_cfg_fname",0,subnet_cfg_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Flow module arg's parse failed.");
    goto errLabel;
  }
  
  // parse the proc dict. file
  if((rc = objectFromFile(proc_cfg_fname,class_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The flow proc dictionary could not be read from '%s'.",cwStringNullGuard(proc_cfg_fname));
    goto errLabel;
  }

  // parse the subnet dict file
  if((rc = objectFromFile(subnet_cfg_fname,subnet_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The flow subnet dictionary could not be read from '%s'.",cwStringNullGuard(subnet_cfg_fname));
    goto errLabel;
  }

  // create the flow object
  if((rc = create( flowH, class_cfg, args.test_args, subnet_cfg, args.out_dir)) != kOkRC )
  {
    rc = cwLogError(rc,"Flow object configure failed.");
    goto errLabel;
  }

  // create the flow object
  if((rc = initialize( flowH )) != kOkRC )
  {
    rc = cwLogError(rc,"Flow object create failed.");
    goto errLabel;
  }
  
  // run the network
  if((rc = exec( flowH )) != kOkRC )
    rc = cwLogError(rc,"Execution failed.");
    
errLabel:
  // destroy the flow object
  if((rc = destroy(flowH)) != kOkRC )
  {
    rc = cwLogError(rc,"Close the flow object.");
    goto errLabel;
  }

  if( class_cfg != nullptr )
    class_cfg->free();

  if( subnet_cfg != nullptr )
    subnet_cfg->free();
  
  return rc;
  
}
