//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwTest_h
#define cwTest_h

namespace cw
{
  struct object_str;
  
  namespace test
  {
    
    typedef struct test_args_str
    {
      const char*              module_label;  // test module this test belongs to
      const char*              test_label;    // test label
      const struct object_str* module_args;   // arguments for all tests in this module
      const struct object_str* test_args;     // arguments specific to this test
      const char*              rsrc_dir;      // input data dir. for this test
      const char*              out_dir;       // output data dir. for this test
      int                      argc;          // cmd line arg count
      const char**             argv;          // cmd line arg's
        
    } test_args_t;

    typedef rc_t (*test_func_t)(const test_args_t& args);
    
    rc_t test( const struct object_str* cfg, int argc, const char** argv );
    rc_t test( const char* cfg_fname,        int argc, const char** argv );
  
  }
}

#endif
