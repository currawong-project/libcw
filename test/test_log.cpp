//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org>
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwText.h"
#include "cwFile.h"
#include "cwFileSys.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cw;
using namespace cw::log;

namespace
{
  // Define a struct to hold results from our custom callbacks
  struct TestCbContext
  {
    std::vector<std::string> messages;
    logLevelId_t lastLevel;
    int callCount;
    
    void reset()
    {
      messages.clear();
      lastLevel = kInvalid_LogLevel;
      callCount = 0;
    }
  };
  
  // Custom output callback function
  void testOutputCallback(void* arg, logLevelId_t level, const char* text)
  {
    TestCbContext* context = static_cast<TestCbContext*>(arg);
    if (context)
    {
      context->callCount++;
      context->lastLevel = level;
      context->messages.push_back(text);
    }
  }
  
  // Custom formatter callback function
  void testFormatterCallback(void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned flags, logLevelId_t level, const char* function, const char* filename, unsigned lineno, int sys_errno, rc_t rc, const char* msg)
  {
    char formattedMsg[1024];
    snprintf(formattedMsg, sizeof(formattedMsg), "CUSTOM_FORMAT: %s", msg);
    outFunc(outCbArg, level, formattedMsg);
  }
  
  class LogTest : public ::testing::Test
  {
  protected:
    handle_t logH;
    TestCbContext context;
    const char* testLogFilename = "test_log_file.log";
    
    void SetUp() override
    {
      // Ensure no global logger interferes
      destroyGlobal();
      logH.clear();
      context.reset();
      remove(testLogFilename);
    }
    
    void TearDown() override
    {
      destroy(logH);
      destroyGlobal();
      remove(testLogFilename);
    }
  };
  
  TEST_F(LogTest, CreateDestroy)
  {
    log_args_t args;
    init_minimum_args(args);
    ASSERT_EQ(create(logH, args), kOkRC);
    ASSERT_TRUE(logH.isValid());
    ASSERT_EQ(destroy(logH), kOkRC);
    ASSERT_FALSE(logH.isValid());
  }
  
  TEST_F(LogTest, LevelToStringConversion)
  {
    EXPECT_STREQ(levelToString(kInfo_LogLevel), "info");
    EXPECT_STREQ(levelToString(kError_LogLevel), "error");
    EXPECT_STREQ(levelToString(kInvalid_LogLevel), "<invalid>");
    
    EXPECT_EQ(levelFromString("debug"), kDebug_LogLevel);
    EXPECT_EQ(levelFromString("warn"), kWarning_LogLevel);
    EXPECT_EQ(levelFromString("nonexistent"), kInvalid_LogLevel);
  }
  
  TEST_F(LogTest, SetAndGetLevel)
  {
    log_args_t args;
    init_minimum_args(args);
    args.level = kInfo_LogLevel;
    create(logH, args);
    
    ASSERT_EQ(level(logH), kInfo_LogLevel);
    setLevel(logH, kError_LogLevel);
    ASSERT_EQ(level(logH), kError_LogLevel);
  }
  
  TEST_F(LogTest, FiltersMessagesBelowLevel)
  {
    log_args_t args;
    init_minimum_args(args);
    args.level = kWarning_LogLevel;
    args.outCbFunc = testOutputCallback;
    args.outCbArg = &context;
    create(logH, args);
    
    msg(logH, 0, kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, "This should be filtered.");
    EXPECT_EQ(context.callCount, 0);
    
    msg(logH, 0, kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, "This should pass.");
    EXPECT_EQ(context.callCount, 1);
    
    msg(logH, 0, kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, "This should also pass.");
    EXPECT_EQ(context.callCount, 2);
  }
  
  TEST_F(LogTest, SynchronousLoggingWithCustomOutput)
  {
    log_args_t args;
    init_minimum_args(args);
    args.flags = kSkipQueueFl;
    args.outCbFunc = testOutputCallback;
    args.outCbArg = &context;
    create(logH, args);
    
    const char* testMsg = "Synchronous test message";
    msg(logH, 0, kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, testMsg);
    
    ASSERT_EQ(context.callCount, 1);
    ASSERT_EQ(context.lastLevel, kInfo_LogLevel);
    ASSERT_NE(context.messages[0].find(testMsg), std::string::npos);
  }
  
  TEST_F(LogTest, SynchronousLoggingWithCustomFormatter)
  {
    log_args_t args;
    init_minimum_args(args);
    args.flags = kSkipQueueFl;
    args.outCbFunc = testOutputCallback;
    args.outCbArg = &context;
    args.fmtCbFunc = testFormatterCallback;
    create(logH, args);
    
    const char* testMsg = "Custom formatter test";
    msg(logH, 0, kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, testMsg);
    
    ASSERT_EQ(context.callCount, 1);
    ASSERT_EQ(context.lastLevel, kInfo_LogLevel);
    ASSERT_NE(context.messages[0].find("CUSTOM_FORMAT:"), std::string::npos);
    ASSERT_NE(context.messages[0].find(testMsg), std::string::npos);
  }
  
  TEST_F(LogTest, AsynchronousLoggingExec)
  {
    log_args_t args;
    init_default_args(args); // Use defaults to enable queue
    args.outCbFunc = testOutputCallback;
    args.outCbArg = &context;
    args.level = kInfo_LogLevel;
    ASSERT_EQ(create(logH, args), kOkRC );
    context.reset();
    
    const char* testMsg = "Asynchronous test message";
    //msg(logH, 0, kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, testMsg);
    cwLogInfoH(logH,testMsg);
    
    // Message should be in queue, not yet processed
    ASSERT_EQ(context.callCount, 0);
    
    // Process the queue
    exec(logH);
    
    ASSERT_EQ(context.callCount, 1);
    ASSERT_EQ(context.lastLevel, kInfo_LogLevel);
    ASSERT_NE(context.messages[0].find(testMsg), std::string::npos);
  }
  
  TEST_F(LogTest, BufferWritesAndClears)
  {
    log_args_t args;
    init_minimum_args(args);
    args.flags = kSkipQueueFl | kBufEnableFl;
    args.textBufCharCnt = 1024;
    args.outCbFunc = testOutputCallback; // Use a callback to get formatted msg
    args.outCbArg = &context;
    create(logH, args);
    
    const char* testMsg = "Buffered message";
    msg(logH, 0, kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, testMsg);
    
    const char* buffer = log::buffer(logH);
    ASSERT_NE(std::string(log::buffer(logH)).find(testMsg), std::string::npos);
    
    clearBuffer(logH);
    ASSERT_STREQ(log::buffer(logH), "");
  }
  
  TEST_F(LogTest, BufferOverflowIsHandled)
  {
    log_args_t args;
    init_minimum_args(args);
    args.flags = kSkipQueueFl | kBufEnableFl;
    args.textBufCharCnt = 20; // Small buffer
    create(logH, args);
    
    const char* testMsg = "This is a very long message that will not fit in the buffer";
    msg(logH, 0, kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, testMsg);
    
    const char* buffer = log::buffer(logH);
    // The implementation discards the write if it doesn't fit, so buffer should be empty.
    ASSERT_STREQ(buffer, "");
  }
  
  TEST_F(LogTest, FileLogging)
  {
    log_args_t args;
    init_minimum_args(args);
    args.flags = kSkipQueueFl | kFileOutFl | kOverwriteFileFl;
    args.log_fname = testLogFilename;
    create(logH, args);
    
    const char* testMsg = "Message for file log";
    //msg(logH, 0, kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, kOkRC, testMsg);
    cwLogInfoH(logH,"%s",testMsg);
    
    // Destroy logger to ensure file is flushed and closed
    destroy(logH);
    ASSERT_FALSE(logH.isValid());
    
    // Read the file and verify content
    file::handle_t fileH;
    ASSERT_EQ(file::open(fileH, testLogFilename, file::kReadFl), kOkRC);

    
    char fileContent[1024] = {0};
    unsigned bytesRead = 0;
    ASSERT_EQ(file::read(fileH, (uint8_t*)fileContent, sizeof(fileContent) - 1, &bytesRead), kEofRC);
    file::close(fileH);
    
    ASSERT_GT(bytesRead, 0);
    ASSERT_NE(std::string(fileContent).find(testMsg), std::string::npos);
   
  }
  
  TEST_F(LogTest, GlobalLogger)
  {
    log_args_t args;
    init_minimum_args(args);
    args.flags = kSkipQueueFl;
    args.outCbFunc = testOutputCallback;
    args.outCbArg = &context;
    
    ASSERT_EQ(createGlobal(args), kOkRC);
    
    const char* testMsg = "Global logger test";
    cwLogInfo(testMsg);
    
    ASSERT_EQ(context.callCount, 1);
    ASSERT_EQ(context.lastLevel, kInfo_LogLevel);
    ASSERT_NE(context.messages[0].find(testMsg), std::string::npos);
    
    destroyGlobal();
    ASSERT_FALSE(globalHandle().isValid());
  }
  
} // namespace
