#include <gtest/gtest.h>
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwTime.h"
#include "cwThread.h"
#include <atomic>

using namespace cw;

// A simple callback that increments a counter.
// Returns true to keep the thread running.
bool increment_cb(void* arg) {
    std::atomic<int>* counter = static_cast<std::atomic<int>*>(arg);
    (*counter)++;
    return true;
}

// A callback that returns false to signal thread termination.
bool terminate_cb(void* arg) {
    std::atomic<int>* counter = static_cast<std::atomic<int>*>(arg);
    (*counter)++;
    return false;
}

class ThreadTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(ThreadTest, CreateAndDestroy) {
    thread::handle_t h;
    std::atomic<int> counter{0};
    
    // Create should succeed and thread should be in paused state.
    rc_t rc = thread::create(h, increment_cb, &counter, "test_thread");
    ASSERT_EQ(rc, kOkRC);
    ASSERT_TRUE(h.isValid());
    EXPECT_EQ(thread::state(h), thread::kPausedThId);
    EXPECT_STREQ(thread::label(h), "test_thread");
    
    // Destroy should succeed and release the handle.
    rc = thread::destroy(h);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_FALSE(h.isValid());
}

TEST_F(ThreadTest, PauseUnpause) {
    thread::handle_t h;
    std::atomic<int> counter{0};
    
    ASSERT_EQ(thread::create(h, increment_cb, &counter, "pause_test"), kOkRC);
    
    // Unpause (start) the thread.
    rc_t rc = thread::unpause(h);
    EXPECT_EQ(rc, kOkRC);
    
    // Wait for it to start incrementing.
    int timeout = 100;
    while( counter.load() == 0 && timeout > 0 ) {
        cw::sleepUs(1000);
        timeout--;
    }
    EXPECT_GT(counter.load(), 0);
    
    // Pause the thread.
    rc = thread::pause(h, thread::kPauseFl | thread::kWaitFl);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_EQ(thread::state(h), thread::kPausedThId);
    
    int snapshot = counter.load();
    cw::sleepUs(10000);
    EXPECT_EQ(counter.load(), snapshot); // Counter should not increase while paused.
    
    thread::destroy(h);
}

TEST_F(ThreadTest, CycleCount) {
    thread::handle_t h;
    std::atomic<int> counter{0};
    
    // Create thread with a small pauseMicros for faster response in tests.
    ASSERT_EQ(thread::create(h, increment_cb, &counter, "cycle_test", 100000, 1000), kOkRC);
    
    // Run exactly 5 cycles.
    unsigned cycleCnt = 5;
    // Don't use kWaitFl because cycles might finish before _waitForState notices.
    rc_t rc = thread::pause(h, 0, cycleCnt);
    EXPECT_EQ(rc, kOkRC);
    
    // Wait for the thread to actually reach the cycle limit and return to paused state.
    int timeout = 200; // ms
    while (counter.load() < (int)cycleCnt && timeout > 0) {
        cw::sleepUs(1000);
        timeout--;
    }
    
    // Also wait for the state to settle to paused.
    timeout = 100;
    while (thread::state(h) != thread::kPausedThId && timeout > 0) {
        cw::sleepUs(1000);
        timeout--;
    }
    
    EXPECT_EQ(thread::state(h), thread::kPausedThId);
    EXPECT_EQ(counter.load(), (int)cycleCnt);
    
    thread::destroy(h);
}

TEST_F(ThreadTest, CallbackTermination) {
    thread::handle_t h;
    std::atomic<int> counter{0};
    
    ASSERT_EQ(thread::create(h, terminate_cb, &counter, "term_test"), kOkRC);
    
    // Start thread. Callback returns false, so it should exit.
    ASSERT_EQ(thread::pause(h, 0), kOkRC);
    
    // Wait for the counter to increment before calling destroy.
    int timeout = 100;
    while( counter.load() == 0 && timeout > 0 ) {
        cw::sleepUs(1000);
        timeout--;
    }
    EXPECT_EQ(counter.load(), 1);

    // destroy() handles the kExitedThId state transition and waiting.
    rc_t rc = thread::destroy(h);
    EXPECT_EQ(rc, kOkRC);
}

TEST_F(ThreadTest, ThreadId) {
    thread::thread_id_t main_id = thread::id();
    EXPECT_NE(main_id, 0u);
    
    // Verify that the ID returned in the callback is different.
    thread::handle_t h;
    thread::thread_id_t child_id = 0;
    auto id_cb = [](void* arg) -> bool {
        *static_cast<thread::thread_id_t*>(arg) = thread::id();
        return false; // Exit immediately
    };
    
    ASSERT_EQ(thread::create(h, id_cb, &child_id, "id_test"), kOkRC);
    ASSERT_EQ(thread::pause(h, 0), kOkRC);
    
    // Wait for child_id to be set.
    int timeout = 100;
    while( child_id == 0 && timeout > 0 ) {
        cw::sleepUs(1000);
        timeout--;
    }
    EXPECT_NE(child_id, 0u);
    EXPECT_NE(child_id, main_id);

    thread::destroy(h);
}

TEST_F(ThreadTest, Properties) {
    thread::handle_t h;
    int dummy = 0;
    unsigned stateTO = 123456;
    unsigned pauseU = 54321;
    
    ASSERT_EQ(thread::create(h, increment_cb, &dummy, "prop_test", stateTO, pauseU), kOkRC);
    
    EXPECT_EQ(thread::stateTimeOutMicros(h), stateTO);
    EXPECT_EQ(thread::pauseMicros(h), pauseU);
    EXPECT_STREQ(thread::label(h), "prop_test");
    
    thread::destroy(h);
}
