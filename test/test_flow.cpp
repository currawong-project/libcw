#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"

#define PROC_DICT_FNAME "../../../src/flow/rsrc/proc_dict.cfg"

using namespace cw;


rc_t FlowExec( const char* pgm_src_code, const char* result )
{
  rc_t rc,rc1=kOkRC;
  
  object_t*         proc_class_cfg = nullptr;
  object_t*         pgm_cfg        = nullptr;
  log::logLevelId_t level0         = log::level();
  flow::handle_t    flowH;
  
  EXPECT_EQ(rc = objectFromFile(PROC_DICT_FNAME,proc_class_cfg),kOkRC) << "The proc. class dictionary parse failed.";
  if( rc != kOkRC )
    goto errLabel;

  EXPECT_EQ(rc = objectFromString(pgm_src_code,pgm_cfg),kOkRC) << "The program source code could not be parsed.";
  if( rc != kOkRC )
    goto errLabel;

  EXPECT_EQ(rc = flow::create(flowH,proc_class_cfg,pgm_cfg),kOkRC) << "Flow object create failed.";
  if( rc != kOkRC )
    goto errLabel;

  EXPECT_EQ(rc = flow::initialize(flowH), kOkRC ) << "Flow program initialize failed.";
  if( rc != kOkRC )
    goto errLabel;

  log::set_flags( log::flags() | log::kBufEnableFl );
  log::set_level( log::kError_LogLevel );

  EXPECT_EQ(rc = flow::exec(flowH), kEofRC ) << "Flow program execution failed.";
  if( rc == kEofRC )
    rc = kOkRC;
  
  log::set_flags( cwClrFlag(log::flags(), log::kBufEnableFl));
  log::set_level( level0 );

  EXPECT_STREQ( log::buffer(), result ) << "Program result mismatch.";
  
errLabel:

  EXPECT_EQ(rc1 = flow::destroy(flowH), kOkRC ) << "Flow object destroy failed.";
  
  if( pgm_cfg != nullptr )
    pgm_cfg->free();
  
  if( proc_class_cfg != nullptr )
    proc_class_cfg->free();
  
  return rcSelect(rc,rc1);
}


TEST( FlowTest, NumberTest )
{
  rc_t rc;

  const char* pgm_src = R"(
    {
      non_real_time_fl:true,
      max_cycle_count:10

	    network:
	    {
	      procs: {
	        n_a : { class: number, args:{ in:1 } }
	        n_b : { class: number, args:{ in:1 } }
	        add_a : { class: add, in: { in0:n_a.out, in1:n_b.out } }
	        p_a : { class: print, in:{ in0:add_a.out }, args:{ text:["A:"], eol_str:" "} }
	    
	        n_c : { class: number, args:{ in:1 } }
	        add_b : { class: add,   in: { in0:n_c.out, in1:add_a.out }, out:{ out:n_a.in } }
	        p_b   : { class: print, in: { in0:add_b.out }, args:{ text:["B:"], eol_str:" " } }
	      } 
	    }
    })";

  const char* result = "A:2.000000 B:3.000000 A:4.000000 B:5.000000 A:6.000000 B:7.000000 A:8.000000 B:9.000000 A:10.000000 B:11.000000 A:12.000000 B:13.000000 A:14.000000 B:15.000000 A:16.000000 B:17.000000 A:18.000000 B:19.000000 A:20.000000 B:21.000000 ";

  EXPECT_EQ( FlowExec(pgm_src,result), kOkRC );
    
}

/*
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
*/
