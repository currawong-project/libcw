#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"

class GlobalEnvironment : public ::testing::Environment {
public:
  void SetUp() override
  {
    // This code runs once before all tests in the program.
    cw::log::log_args_t log_args;
    init_minimum_args( log_args );
    cw::log::createGlobal(log_args);
  }
  
  void TearDown() override
  {
    // This code runs once after all tests in the program are finished.
    cw::log::destroyGlobal();
  }
};

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // GoogleTest takes ownership of the pointer, so use `new`.
  ::testing::AddGlobalTestEnvironment(new GlobalEnvironment); 
  return RUN_ALL_TESTS();
}
