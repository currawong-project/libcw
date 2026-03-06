//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include <gtest/gtest.h>
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwTime.h"
#include "cwThread.h"
#include "cwMutex.h"
#include <atomic>

using namespace cw;

class MutexTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(MutexTest, CreateAndDestroy) {
    mutex::handle_t h;
    
    // Create should succeed.
    rc_t rc = mutex::create(h);
    ASSERT_EQ(rc, kOkRC);
    ASSERT_TRUE(h.isValid());
    
    // Destroy should succeed and release the handle.
    rc = mutex::destroy(h);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_FALSE(h.isValid());
}

TEST_F(MutexTest, LockUnlock) {
    mutex::handle_t h;
    ASSERT_EQ(mutex::create(h), kOkRC);
    
    // Simple lock/unlock in the same thread.
    EXPECT_EQ(mutex::lock(h), kOkRC);
    EXPECT_EQ(mutex::unlock(h), kOkRC);
    
    mutex::destroy(h);
}

TEST_F(MutexTest, TryLock) {
    mutex::handle_t h;
    ASSERT_EQ(mutex::create(h), kOkRC);
    
    bool locked = false;
    
    // Try lock on an unlocked mutex should succeed.
    EXPECT_EQ(mutex::tryLock(h, locked), kOkRC);
    EXPECT_TRUE(locked);
    
    // Try lock on a locked mutex should return kOkRC but locked should be false.
    bool locked2 = true;
    EXPECT_EQ(mutex::tryLock(h, locked2), kOkRC);
    EXPECT_FALSE(locked2);
    
    EXPECT_EQ(mutex::unlock(h), kOkRC);
    
    mutex::destroy(h);
}

TEST_F(MutexTest, TimedLock) {
    mutex::handle_t h;
    ASSERT_EQ(mutex::create(h), kOkRC);
    
    // Lock it first.
    EXPECT_EQ(mutex::lock(h), kOkRC);
    
    // Try to lock with timeout. Should fail with kTimeOutRC.
    EXPECT_EQ(mutex::lock(h, 10), kTimeOutRC);
    
    EXPECT_EQ(mutex::unlock(h), kOkRC);
    
    mutex::destroy(h);
}

// Data shared between threads for contention test.
struct ContentionData {
    mutex::handle_t h;
    int counter;
    int iterations;
};

bool contention_cb(void* arg) {
    ContentionData* data = static_cast<ContentionData*>(arg);
    for (int i = 0; i < data->iterations; ++i) {
        if (mutex::lock(data->h) == kOkRC) {
            data->counter++;
            mutex::unlock(data->h);
        }
    }
    return false; // Exit thread
}

TEST_F(MutexTest, Contention) {
    ContentionData data;
    data.counter = 0;
    data.iterations = 1000000;
    ASSERT_EQ(mutex::create(data.h), kOkRC);
    
    thread::handle_t t1, t2;
    ASSERT_EQ(thread::create(t1, contention_cb, &data, "t1", 1000000), kOkRC);
    ASSERT_EQ(thread::create(t2, contention_cb, &data, "t2", 1000000), kOkRC);
    
    // Start threads.
    ASSERT_EQ(thread::unpause(t1), kOkRC);
    ASSERT_EQ(thread::unpause(t2), kOkRC);
    
    // Wait for threads to finish.
    thread::destroy(t1);
    thread::destroy(t2);
    
    // If mutex works, counter should be exactly 2 * iterations.
    EXPECT_EQ(data.counter, 2 * data.iterations);
    
    mutex::destroy(data.h);
}

// Data for condition variable test.
struct CondVarData {
    mutex::handle_t h;
    bool ready;
    bool signaled;
};

bool cond_var_cb(void* arg) {
    CondVarData* data = static_cast<CondVarData*>(arg);
    
    // Lock the mutex.
    mutex::lock(data->h);
    data->ready = true;
    
    // Wait for signal.
    // In this library, waitOnCondVar assumes we already have the lock if lockThenWaitFl is false.
    mutex::waitOnCondVar(data->h, false, 0);
    
    data->signaled = true;
    mutex::unlock(data->h);
    
    return false;
}

TEST_F(MutexTest, CondVarSignal) {
    CondVarData data;
    data.ready = false;
    data.signaled = false;
    ASSERT_EQ(mutex::create(data.h), kOkRC);
    
    thread::handle_t t;
    ASSERT_EQ(thread::create(t, cond_var_cb, &data, "cond_t"), kOkRC);
    ASSERT_EQ(thread::unpause(t), kOkRC);
    
    // Wait until child thread is ready and waiting.
    int timeout = 100;
    while (timeout > 0) {
        bool ready = false;
        mutex::lock(data.h);
        ready = data.ready;
        mutex::unlock(data.h);
        if (ready) break;
        cw::sleepUs(1000);
        timeout--;
    }
    ASSERT_TRUE(data.ready);
    
    // Small sleep to ensure the thread is likely in waitOnCondVar.
    cw::sleepUs(10000);
    
    // Signal the condition variable.
    // Note: signalCondVar doesn't require holding the mutex but it's often better.
    // The worker thread will be woken up and will try to re-acquire the mutex.
    EXPECT_EQ(mutex::signalCondVar(data.h), kOkRC);
    
    // Wait for thread to finish.
    thread::destroy(t);
    
    EXPECT_TRUE(data.signaled);
    
    mutex::destroy(data.h);
}

TEST_F(MutexTest, CondVarTimeout) {
    mutex::handle_t h;
    ASSERT_EQ(mutex::create(h), kOkRC);
    
    // Lock then wait with timeout.
    // waitOnCondVar(h, lockThenWaitFl=true, timeOutMs=10)
    rc_t rc = mutex::waitOnCondVar(h, true, 10);
    
    EXPECT_EQ(rc, kTimeOutRC);
    
    // After waitOnCondVar returns, the mutex is still locked (by contract of pthread_cond_timedwait).
    // Let's verify we can unlock it.
    EXPECT_EQ(mutex::unlock(h), kOkRC);
    
    mutex::destroy(h);
}
