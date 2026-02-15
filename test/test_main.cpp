#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"

#define PROC_DICT_FNAME "../../../src/flow/rsrc/proc_dict.cfg"

using namespace cw;


TEST( GlobalLogTest, TestBuf )
{
  const char* s = "***blah";
  log::handle_t logH = log::globalHandle();
  set_flags( logH, flags(logH) | log::kBufEnableFl );
  cwLogPrint(s);
  EXPECT_STREQ(log::buffer(logH),s);
  set_flags( logH, cwClrFlag( flags(logH) ,log::kBufEnableFl) );
}


class GlobalEnvironment : public ::testing::Environment {
public:
  void SetUp() override
  {
    // This code runs once before all tests in the program.
    log::log_args_t log_args;
    init_minimum_args( log_args );
    log::createGlobal(log_args);
  }
  
  void TearDown() override
  {
    // This code runs once after all tests in the program are finished.
    log::destroyGlobal();
  }
};

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // GoogleTest takes ownership of the pointer, so use `new`.
  ::testing::AddGlobalTestEnvironment(new GlobalEnvironment); 
  return RUN_ALL_TESTS();
}
