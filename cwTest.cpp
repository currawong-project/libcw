//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwLex.h"

#include "cwTime.h"
#include "cwFile.h"
#include "cwFileSys.h"
#include "cwVectOps.h"
#include "cwTextBuf.h"

#include "cwAudioDevice.h"
#include "cwAudioBufDecls.h"
#include "cwAudioBuf.h"
#include "cwMtx.h"

#include "cwAudioFile.h"

#include "cwDspTypes.h"
#include "cwMath.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"
#include "cwWaveTableBank.h"
#include "cwWaveTableNotes.h"

#include "cwMidiDecls.h"

#include "cwFlowTest.h"
#include "cwFlowValue.h"

#include "cwThread.h"
#include "cwThreadMach.h"

namespace cw
{
  namespace test
  {
    typedef struct test_map_str
    {
      const char* module_name;
      test_func_t test_func; 
    } test_map_t;

    test_map_t _test_map[] = {
      { "/lex",     lex::test },
      { "/filesys", filesys::test },
      { "/object",  object_test },
      { "/vop",     vop::test },
      { "/dsp",     dsp::test_dsp },
      { "/time",    time::test },
      { "/flow",    flow::test },
      { "/textBuf", textBuf::test },
      { "/audioBuf",audio::buf::test },
      { "/mtx",     mtx::test },
      { "/wt_bank", wt_bank::test },
      { "/audio_transform", dsp::test },
      { "/wt_note", wt_note::test },
      { "/thread_tasks", thread_tasks::test },
      { "/flow_value", flow::value_test },
      { "/numeric_convert", numericConvertTest },
      { nullptr, nullptr },
    };
    
    typedef struct test_str
    {
      int             argc;        // extra cmd line arguments to be passes to the test cases
      const char**    argv;  
      
      const char*     base_dir;    // base test dictionary    
      const object_t* test_cfg;    // top level test cfg.
      
      const char*       rsrc_folder; // name of the 'rsrc' folder in the base dir
      const char*       out_folder;  // name of the output folder in the base dir
      const char*       ref_folder;  // name of the test reference folder in the base dir

      const char* sel_module_label; // selected module label
      const char* sel_test_label;   // selected test label
      bool        all_module_fl;    // true if all modules should be run
      bool        all_test_fl;      // true if all tests in the selected module should be run
      bool        compare_fl;       // true if compare operation should be run
      bool        echo_fl;          // echo test output to the console (false=write all output to log file only)
      bool        gen_report_fl;    // print module/test names as the gen phase is executes

      void*                  logCbArg;   // original log callback args
      log::logOutputCbFunc_t logCbFunc;
      
      const char*    cur_test_label;    // current test label
      file::handle_t cur_log_fileH;     // current log file handle

      unsigned  gen_cnt;           // count of tests which generated output
      unsigned  compare_ok_cnt;    // count of tests which passed the compare pass
      unsigned  compare_fail_cnt;  // count of tests which failed the compare test 
      
    } test_t;


    rc_t _parse_args( test_t& test, const object_t* cfg, int argc, const char** argv )
    {
      rc_t     rc      = kOkRC;
      char*    out_dir = nullptr;
      int      argi    = 1;
      
      test.all_module_fl = true;
      test.all_test_fl   = true;
      
      if( argc > 1 )
      {
        test.sel_module_label = argv[1];
        test.all_module_fl    = textIsEqual(test.sel_module_label,"all");
        argi += 1;
      }      

      if( argc > 2 )
      {
        test.sel_test_label = argv[2];
        test.all_test_fl    = textIsEqual(test.sel_test_label,"all");
        argi += 1;
      }

      for(int i=argi; i<argc; ++i)
      {
        if( textIsEqual(argv[i],"compare") )
          test.compare_fl = true;

        if( textIsEqual(argv[i],"echo") )
          test.echo_fl = true;

        if( textIsEqual(argv[i],"gen_report"))
          test.gen_report_fl = true;

        // test specific args follow the 'args' keyword
        if( textIsEqual(argv[i],"args") )
        {
          test.argc = argc - (i+1);
          test.argv = argv + (i+1);
        }
      }
      
      if((rc = cfg->readv("base_dir",0,test.base_dir,
                          "test",kDictTId,test.test_cfg,
                          "resource_dir",0,test.rsrc_folder,
                          "output_dir",0,test.out_folder,
                          "ref_dir",0,test.ref_folder)) != kOkRC )
      {
        goto errLabel;
      }

      if((out_dir = filesys::makeFn(test.base_dir, nullptr, nullptr, test.out_folder, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Output directory name formation failed.");
        goto errLabel;
      }

      if( !filesys::isDir(out_dir))
      {
        if((rc = filesys::makeDir(out_dir)) != kOkRC )
          goto errLabel;
      }

    errLabel:
      mem::release(out_dir);
      return rc;
    }

    void _exec_test_log_cb( void* cbArg, unsigned level, const char* text )
    {
      rc_t rc;
      test_t* r = (test_t*)cbArg;
      
      if((rc = file::print(r->cur_log_fileH,text)) != kOkRC )
        cwLogError(rc,"Log file write failed for '%s'.",text);
      
      if( r->logCbFunc != nullptr && r->echo_fl )
        r->logCbFunc( r->logCbArg,level,text);
    }

    rc_t _exec_dispatch( test_t& test, test_args_t& args )
    {
      rc_t     rc = kOkRC;
      unsigned i  = 0;
      
      // find the requested test function ....
      for(; _test_map[i].module_name!=nullptr; ++i)
        if( textIsEqual(_test_map[i].module_name,args.module_label) )
        {
          rc = _test_map[i].test_func(args); //.... and call it
          break;
        }

      if( _test_map[i].module_name==nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The test function for module %s was not found.",cwStringNullGuard(args.module_label));
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _compare_one_test( test_t& test, const char* ref_dir, const char* test_dir )
    {
      rc_t                 rc          = kOkRC;
      unsigned             testRefDirN = 0;
      filesys::dirEntry_t* testRefDirA = nullptr;
      bool                 ok_fl = true;

      // get the list of files in the test directory
      if((testRefDirA = filesys::dirEntries( ref_dir, filesys::kFileFsFl, &testRefDirN )) == nullptr )
      {            
        rc = cwLogError(kOpFailRC,"An error occurred while attempting to read the directory file names from '%s'.",cwStringNullGuard(ref_dir));
        goto errLabel;
      }

      // for each file 
      for(unsigned j=0; rc==kOkRC && j<testRefDirN; ++j)
        if( testRefDirA[j].name != nullptr )
        {
          char* testRefFn = filesys::makeFn( ref_dir,  testRefDirA[j].name, nullptr, nullptr ); 
          char* testCmpFn = filesys::makeFn( test_dir, testRefDirA[j].name, nullptr, nullptr );
          bool  isEqualFl = false;
          
          // compare two files
          if((rc = file::compare( testRefFn, testCmpFn, isEqualFl)) != kOkRC )
            rc = cwLogError(rc,"The comparison failed on '%s' == '%s'.",cwStringNullGuard(testRefFn),cwStringNullGuard(testCmpFn));
          else
          {
            // check compare status
            if( !isEqualFl )
            {
              ok_fl = false;
              cwLogInfo("Compare failed on: '%s'",testRefFn);
            }
          }
              
          mem::release(testRefFn);
          mem::release(testCmpFn);              
        }

      mem::release(testRefDirA);

    errLabel:

      if( ok_fl )
        test.compare_ok_cnt += 1;
      else
        test.compare_fail_cnt += 1;
      
      return rc;
      
    }
      
    
    rc_t _exec_one_test( test_t& test, const char* module_label, const object_t* module_args, const char* test_label, const object_t* test_args )
    {
      rc_t        rc        = kOkRC;
      char*       rsrc_dir  = nullptr;
      char*       out_dir   = nullptr;
      char*       ref_dir   = nullptr;
      char*       log_fname = nullptr;
      test_args_t args      = {};
      

      if((rsrc_dir = filesys::makeFn( test.base_dir, nullptr, nullptr, test.rsrc_folder, module_label, test_label, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Resource directory '%s' formation failed.",cwStringNullGuard(rsrc_dir));
        goto errLabel;
      }

      if((out_dir = filesys::makeFn( test.base_dir, nullptr, nullptr, test.out_folder, module_label, test_label, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Output directory '%s' formation failed.",cwStringNullGuard(out_dir));
        goto errLabel;
      }

      if((ref_dir = filesys::makeFn( test.base_dir, nullptr, nullptr, test.ref_folder, module_label, test_label, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Reference directory '%s' formation failed.",cwStringNullGuard(ref_dir));
        goto errLabel;
      }
      
      if((log_fname = filesys::makeFn( out_dir, "log","txt",nullptr,nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Log filename '%s' formation failed.",cwStringNullGuard(log_fname));
        goto errLabel;
      }

      if( !filesys::isDir(out_dir))
      {
        if((rc = filesys::makeDir(out_dir)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"Output directory '%s' create failed.",cwStringNullGuard(out_dir));
          goto errLabel;
        }
      }
      
      // open the log file
      if((rc = file::open(test.cur_log_fileH, log_fname,file::kWriteFl)) != kOkRC )
      {
        rc = cwLogError(rc,"The log file '%s' could not be created.",cwStringNullGuard(log_fname));
        goto errLabel;
      }

      if( test.gen_report_fl )
        cwLogInfo("%s %s",module_label,test_label);

      // save the log to a 
      test.logCbArg  = log::outputCbArg( log::globalHandle() );
      test.logCbFunc = log::outputCb(    log::globalHandle() );
      
      log::setOutputCb( log::globalHandle(), _exec_test_log_cb, &test );
      

      args.module_label = module_label;
      args.test_label   = test_label;
      args.module_args  = module_args;
      args.test_args    = test_args;
      args.rsrc_dir = rsrc_dir;
      args.out_dir  = out_dir;
      args.argc     = test.argc;
      args.argv     = test.argv;

      if((rc = _exec_dispatch(test, args )) != kOkRC )
        goto errLabel;

      test.gen_cnt += 1;
      
    errLabel:
      
      file::close(test.cur_log_fileH);      
      log::setOutputCb( log::globalHandle(), test.logCbFunc, test.logCbArg );

      // if compare is enabled 
      if( rc == kOkRC && test.compare_fl )
        rc = _compare_one_test(test, ref_dir, out_dir );
     
      if( rc != kOkRC )
        rc = cwLogError(rc,"Test process failed on module:%s test:%s.",module_label,test_label);
      
      mem::release(log_fname);
      mem::release(rsrc_dir);
      mem::release(out_dir);
      mem::release(ref_dir);
      
      return rc;
    }

    rc_t _exec_cases( test_t& test, const char* module_label, const object_t* module_args, const object_t* cases_cfg )
    {
      rc_t rc = kOkRC;
      
      // get count of test cases
      unsigned testN = cases_cfg->child_count();

      // for each test case
      for(unsigned i=0; i<testN; ++i)
      {
        const object_t* test_pair = cases_cfg->child_ele(i);
        const char*     test_label = test_pair->pair_label();

        // apply filter/test label filter
        if( (test.all_module_fl || textIsEqual(module_label,test.sel_module_label)) &&
            ( test.all_test_fl || textIsEqual(test_label,test.sel_test_label)))
        {          
          if((rc = _exec_one_test(test, module_label, module_args, test_label, test_pair->pair_value() )) != kOkRC )
          {
            goto errLabel;
          }
        }
      }

    errLabel:
      return rc;
    }
    
    rc_t _exec_module( test_t& test, const char* module_label, const object_t* module_args, const object_t* module_cfg )
    {
      rc_t rc = kOkRC;

      const object_t* cases_cfg = nullptr;
      const object_t* mod_args_cfg = nullptr;
      
      if((rc = module_cfg->getv_opt("cases",cases_cfg,
                                    "module_args",mod_args_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed on module fields in '%s'.",cwStringNullGuard(module_label));
        goto errLabel;
      }

      if( cases_cfg == nullptr )
        cases_cfg = module_cfg;

      if( mod_args_cfg == nullptr )
        mod_args_cfg = module_args;

      if((rc = _exec_cases( test,module_label,mod_args_cfg,cases_cfg)) != kOkRC )
        goto errLabel;
      
    errLabel:
      return rc;
    }
    
    rc_t _proc_test_cfg( test_t& test, const char* module_label, const object_t* test_cfg );
    
    rc_t _proc_test_from_file( test_t& test, const char* module_label, const char* fname )
    {
      rc_t rc = kOkRC;
      char* cfg_fname;
      object_t* test_cases_cfg = nullptr;
      //const char* orig_base_dir = test.base_dir;
      char* new_base_dir = nullptr;
      
      if((cfg_fname = filesys::makeFn(test.base_dir, fname, nullptr, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The test cases file name for the module '%s' in '%s' / '%s' could not be formed.",cwStringNullGuard(module_label),cwStringNullGuard(test.base_dir),cwStringNullGuard(fname));
        goto errLabel;
      }

      if((rc = objectFromFile( cfg_fname, test_cases_cfg )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Parse failed on the test case module file '%s'.",cwStringNullGuard(cfg_fname));
        goto errLabel;
      }

      if((new_base_dir = filesys::makeFn(test.base_dir, nullptr, nullptr, test.out_folder, module_label, nullptr )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The base test directory name for the module '%s' could not be formed in '%s'.",cwStringNullGuard(test.base_dir),cwStringNullGuard(module_label));
        goto errLabel;
      }

      if( !filesys::isDir(new_base_dir))
      {
        rc = cwLogError(kOpFailRC,"The base test directory '%s' for the module '%s' does not exist.",cwStringNullGuard(test.base_dir),cwStringNullGuard(module_label));
        goto errLabel;
      }

      rc = _proc_test_cfg( test, module_label, test_cases_cfg );
      
    errLabel:
      mem::release(cfg_fname);
      mem::release(new_base_dir);
      
      if( test_cases_cfg != nullptr )
        test_cases_cfg->free();

      
      return rc;
    }

    rc_t _proc_module( test_t& test, const char* base_module_label, const char* module_label, const object_t* module_cfg )
    {
      rc_t  rc               = kOkRC;
      char* new_module_label = filesys::makeFn(base_module_label,nullptr,nullptr,module_label,nullptr );

      // form the module output directory
      char* out_dir = filesys::makeFn(test.base_dir,nullptr,nullptr,test.out_folder,new_module_label,nullptr);

      // verify that the the module output directory exists
      if( !filesys::isDir(out_dir) )
      {
        if((rc = filesys::makeDir(out_dir)) != kOkRC )
        {
          rc = cwLogError(rc,"The module output directory '%s' create failed.",cwStringNullGuard(out_dir));
          goto errLabel;
        }
      }
      
      switch( module_cfg->type_id() )
      {
        case kStringTId:  // an external module file was given
          {
            const char* s = nullptr;
            if((rc = module_cfg->value(s)) != kOkRC )
            {
              rc = cwLogError(rc,"Parse failed on module filename in '%s'.",cwStringNullGuard(module_label));
              goto errLabel;
            }
            
            rc = _proc_test_from_file(test, new_module_label, s );
          }
          break;
              
        case kDictTId: // a nested module dict or case dict was given 
          rc = _proc_test_cfg(test, new_module_label, module_cfg );
          break;

        default:
          break;
      }                  

    errLabel:
      mem::release(out_dir);
      mem::release(new_module_label);
      return rc;
    }
    
    rc_t _proc_test_cfg( test_t& test, const char* module_label, const object_t* test_cfg )
    {
      rc_t            rc          = kOkRC;
      const object_t* module_args = nullptr;
      const object_t* modules_cfg = nullptr;
      const object_t* cases_cfg   = nullptr;

      
      if((rc = test_cfg->getv_opt("module_args",module_args,
                                  "modules",modules_cfg,
                                  "cases",cases_cfg )) != kOkRC )
      {
        rc = cwLogError(rc,"The 'module_args' parse failed on module '%s'.",cwStringNullGuard(module_label));
        goto errLabel;
      }

      // if a list of modules was given
      if( modules_cfg != nullptr )
      {
        unsigned modulesN = modules_cfg->child_count();
        for(unsigned i=0; i<modulesN; ++i)
        {
          const object_t* mod_pair = modules_cfg->child_ele(i);

          if((rc = _proc_module(test, module_label, mod_pair->pair_label(), mod_pair->pair_value() )) != kOkRC )
            goto errLabel;
          

        }
      }

      // if no keywords were found then the dictionary must be a list of cases
      if(module_args==nullptr && modules_cfg==nullptr && cases_cfg==nullptr )
      {
        cases_cfg = test_cfg;
      }
      
      // if a list of cases was given
      if( cases_cfg != nullptr )
      {
        if((rc = _exec_cases(test,module_label,module_args,cases_cfg)) != kOkRC )
          goto errLabel;
        
      }
      
    errLabel:
      return rc;
    }
    
  }
}

cw::rc_t cw::test::test( const struct object_str* cfg, int argc, const char** argv )
{
  test_t test{};
  rc_t rc = kOkRC;
  
  if((rc = _parse_args(test, cfg, argc, argv )) != kOkRC )
  {
    rc = cwLogError(rc,"Test arguments parse failed.");
    goto errLabel;
  }

  if((rc = _proc_test_cfg(test,"/",test.test_cfg)) != kOkRC )
  {
    goto errLabel;
  }

errLabel:
  
  cwLogInfo("Test Gen Count:%i.",test.gen_cnt);
  
  if( test.compare_fl )
    cwLogInfo("Test Compare - ok:%i fail:%i.",test.compare_ok_cnt,test.compare_fail_cnt);

  if( rc != kOkRC )
    rc = cwLogError(rc,"Testing process failed.");
    
      
  return rc;

}

cw::rc_t cw::test::test( const char* fname, int argc, const char** argv )
{
  rc_t           rc             = kOkRC;
  object_t*      cfg            = nullptr;
  const object_t* test_dict      = nullptr;
  const object_t* test_test_dict = nullptr;
  
  if((rc = objectFromFile(fname,cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"Parsing failed on the test cfg file '%s'.",cwStringNullGuard(fname));
    goto errLabel;
  }

  if((rc = cfg->get("test",test_dict)) != kOkRC )
  {
    goto errLabel;
  }

  if((rc = test_dict->get("test",test_test_dict)) != kOkRC )
  {
    goto errLabel;
  }

  rc = test(test_test_dict,argc,argv);

errLabel:

  if( cfg != nullptr )
    cfg->free();
  
  return rc;
}

