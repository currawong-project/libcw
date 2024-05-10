#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
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


#include "cwFile.h"
#include "cwFileSys.h"

namespace cw
{
  namespace flow
  {
    typedef struct exec_test_str
    {
      void*                  logCbArg;
      log::logOutputCbFunc_t logCbFunc;
      file::handle_t         logFileH;
    } exec_test_t;

    rc_t _compare_dirs( const char* dir0, const char* dir1 )
    {
      rc_t                 rc          = kOkRC;
      rc_t                 testRC      = kOkRC;
      unsigned             dirRefN     = 0;
      filesys::dirEntry_t* dirRefA     = nullptr;
      char*                testRefDir  = nullptr;
      unsigned             testRefDirN = 0;
      filesys::dirEntry_t* testRefDirA = nullptr;
      unsigned             testCnt     = 0;
      unsigned             failCnt     = 0;

      // get a list of sub-directories in the ref. directory
      if((dirRefA = filesys::dirEntries( dir0, filesys::kDirFsFl, &dirRefN )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"An error occurred while attempting to read the list of sub-directories in '%s'.",cwStringNullGuard(dir0));
        goto errLabel;
      }

      // for each test
      for(unsigned i=0; i<dirRefN; ++i)
        if( dirRefA[i].name != nullptr )
        {
          // form the test directory
          if(( testRefDir = filesys::makeFn( dir0, nullptr, nullptr, dirRefA[i].name, nullptr )) == nullptr )
          {
            rc = cwLogError(kOpFailRC,"Test directory formation failed on '%s' and '%s'.",cwStringNullGuard(dir0),cwStringNullGuard(dirRefA[i].name));
            goto errLabel;
          }

          // get the list of files in the test directory
          if((testRefDirA = filesys::dirEntries( testRefDir, filesys::kFileFsFl, &testRefDirN )) == nullptr )
          {            
            rc = cwLogError(kOpFailRC,"An error occurred while attempting to read the list of file from '%s'.",cwStringNullGuard(testRefDir));
            goto errLabel;
          }

          testCnt += 1;

          // for each file 
          for(unsigned j=0; rc==kOkRC && j<testRefDirN; ++j)
            if( testRefDirA[j].name != nullptr )
            {
              char* testRefFn = filesys::makeFn( dir0, testRefDirA[j].name, nullptr, dirRefA[i].name, nullptr ); 
              char* testCmpFn = filesys::makeFn( dir1, testRefDirA[j].name, nullptr, dirRefA[i].name, nullptr );
              bool  isEqualFl = false;

              // compare two files
              if((rc = file::compare( testRefFn, testCmpFn, isEqualFl)) != kOkRC )
                rc = cwLogError(rc,"The comparison failed on '%s' == '%s'.",cwStringNullGuard(testRefFn),cwStringNullGuard(testCmpFn));
              else
              {
                // if the test failed
                if( !isEqualFl )
                {
                  cwLogInfo("Test failed on: '%s'",testRefFn);
                  failCnt += 1;
                }
              }
              
              mem::release(testRefFn);
              mem::release(testCmpFn);              
            }

          mem::release(testRefDirA);
          mem::release(testRefDir);
        }

      mem::release(dirRefA);
      
    errLabel:

      mem::release(testRefDirA);
      mem::release(testRefDir);
      mem::release(dirRefA);

      cwLogInfo("Tests:%i failed:%i",testCnt,failCnt);
      
      return rc;

    }
    
    
    void _exec_test_log_cb( void* cbArg, unsigned level, const char* text )
    {
      rc_t rc;
      exec_test_t* r = (exec_test_t*)cbArg;
      
      if((rc = file::print(r->logFileH,text)) != kOkRC )
        cwLogError(rc,"Log file write failed for '%s'.text");
      
      if( r->logCbFunc != nullptr )
        r->logCbFunc( r->logCbArg,level,text);
    }

    rc_t _exec_test(  const object_t* class_cfg,
                      const object_t* subnet_cfg,
                      const object_t* test_cfg,
                      const char* proj_base_dir,
                      const char* test_case_label  )
    {
      rc_t            rc        = kOkRC;
      char*           proj_dir  = nullptr;
      char*           log_fname = nullptr;
      exec_test_t     test_recd;
      handle_t        flowH;

      // save the log to a 
      test_recd.logCbArg  = log::outputCbArg( log::globalHandle() );
      test_recd.logCbFunc = log::outputCb(    log::globalHandle() );
      
      // form the projectory directory name
      if((proj_dir = filesys::makeFn(proj_base_dir,nullptr,nullptr,test_case_label,nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The project directory name could not be formed from '%s' and '%s'.",cwStringNullGuard(proj_base_dir),cwStringNullGuard(test_case_label));
        goto errLabel;
      }

      // create the project directory
      if( !filesys::isDir(proj_dir) )
      {
        if((rc = filesys::makeDir(proj_dir)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"The project directory '%s' could not be created.",proj_dir);
          goto errLabel;
        }
      }

      // form the log file name
      if((log_fname = filesys::makeFn( proj_dir, "log","txt",nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The log file name could not be formed from the project directory.",cwStringNullGuard(proj_dir));
        goto errLabel;
      }

      // open the log file
      if((rc = file::open(test_recd.logFileH, log_fname,file::kWriteFl)) != kOkRC )
      {
        rc = cwLogError(rc,"The log file '%s' could not be created.",cwStringNullGuard(log_fname));
        goto errLabel;
      }

      log::setOutputCb( log::globalHandle(), _exec_test_log_cb, &test_recd );
      
      // create the flow object
      if((rc = create( flowH, class_cfg, test_cfg, subnet_cfg, proj_dir)) != kOkRC )
      {
        rc = cwLogError(rc,"Flow object create failed.");
        goto errLabel;
      }

      // run the network
      if((rc = exec( flowH )) != kOkRC )
        rc = cwLogError(rc,"Execution failed.");
    

      // destroy the flow object
      if((rc = destroy(flowH)) != kOkRC )
      {
        rc = cwLogError(rc,"Close the flow object.");
        goto errLabel;
      }

    errLabel:

      file::close(test_recd.logFileH);      
      log::setOutputCb( log::globalHandle(), test_recd.logCbFunc, test_recd.logCbArg );
      mem::release(log_fname);
      mem::release(proj_dir);
      
      return rc;
    }    
  }
} 

cw::rc_t cw::flow::test(  const object_t* cfg, int argc, const char* argv[] )
{
  rc_t            rc               = kOkRC;
  const char*     proc_cfg_fname   = nullptr;
  const char*     subnet_cfg_fname = nullptr;
  const object_t* test_cases_cfg   = nullptr;
  object_t*       class_cfg        = nullptr;
  object_t*       subnet_cfg       = nullptr;
  const object_t* test_cfg         = nullptr;
  const char*     proj_dir         = nullptr;
  const char*     test_ref_dir     = nullptr;
  bool            cmp_enable_fl    = false;

  if( argc < 2 || textLength(argv[1]) == 0 )
  {
    rc = cwLogError(kInvalidArgRC,"No 'test-case' label was given on the command line.");
    goto errLabel;
  }
  
  if((rc = cfg->getv("proc_cfg_fname",proc_cfg_fname,
                     "test_cases",    test_cases_cfg,
                     "project_dir",   proj_dir)) != kOkRC )
  {
    rc = cwLogError(rc,"The name of the flow_proc_dict file could not be parsed.");
    goto errLabel;
  }

  // get the subnet cfg filename
  if((rc = cfg->getv_opt("subnet_cfg_fname",subnet_cfg_fname,
                         "cmp_enable_fl", cmp_enable_fl,
                         "test_ref_dir",test_ref_dir)) != kOkRC )
  {
    rc = cwLogError(rc,"The name of the subnet file could not be parsed.");
    goto errLabel;
  }

  // parse the proc dict. file
  if((rc = objectFromFile(proc_cfg_fname,class_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The proc dictionary could not be read from '%s'.",cwStringNullGuard(proc_cfg_fname));
    goto errLabel;
  }

  // parse the subnet dict file
  if((rc = objectFromFile(subnet_cfg_fname,subnet_cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"The subnet dictionary could not be read from '%s'.",cwStringNullGuard(subnet_cfg_fname));
    goto errLabel;
  }

  // validate the project directory
  if( proj_dir!=nullptr && !filesys::isDir(proj_dir) )
  {
    if((rc = filesys::makeDir(proj_dir)) != kOkRC )
    {
      rc = cwLogError(rc,"The project directory '%s' could not be created.",cwStringNullGuard(proj_dir));
    }
  }

  // for each test
  for(unsigned i=0; i<test_cases_cfg->child_count(); ++i)
  {
    const object_t* test_cfg_pair       = test_cases_cfg->child_ele(i);
    const char*     test_label          = nullptr;
    bool            test_all_fl         = textIsEqual(argv[1],"all");
    bool            is_test_disabled_fl = false;

    // validate the test cfg pair
    if(test_cfg_pair == nullptr || !test_cfg_pair->is_pair() || (test_label = test_cfg_pair->pair_label()) == nullptr || (test_cfg=test_cfg_pair->pair_value())==nullptr || !test_cfg->is_dict())
    {
      rc = cwLogError(kSyntaxErrorRC,"A syntax error was encountered on the test at index %i (%s).",i,cwStringNullGuard(test_label));
      goto errLabel;
    }

    // get the 'disabled flag'
    if((rc = test_cfg->getv_opt("testDisableFl",is_test_disabled_fl)) != kOkRC )
    {
      rc = cwLogError(kSyntaxErrorRC,"An error occurred while parsing the 'testDisableFl'.");
      goto errLabel;
    }

    // if this test is disabled
    if( is_test_disabled_fl )  
    {
      if( !test_all_fl )
      {
        rc = cwLogError(kInvalidArgRC,"The requested test '%s' is diabled.",test_label);
        goto errLabel;
      }
      continue;
    }

    // if we are testing all test or this specific test
    if( test_all_fl || textIsEqual(argv[1],test_label) )
    {
      if((rc = _exec_test(class_cfg,subnet_cfg,test_cfg,proj_dir,test_label)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Test execution failed on '%s'.",cwStringNullGuard(test_label));
        goto errLabel;
      }

      if( !test_all_fl )
        break;
    }
  }

  // if comparision is enabled
  if( test_ref_dir != nullptr && cmp_enable_fl )
  {
    _compare_dirs( test_ref_dir, proj_dir );
  }
  
  
 errLabel:
  if( class_cfg != nullptr )
    class_cfg->free();

  if( subnet_cfg != nullptr )
    subnet_cfg->free();
  
  return rc;
}
