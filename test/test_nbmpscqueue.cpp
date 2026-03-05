#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwNbMpScQueue.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>

using namespace cw;

class NbMpScQueueTest : public ::testing::Test {
protected:
  nbmpscq::handle_t qH;

  void SetUp() override {
  }

  void TearDown() override {
    nbmpscq::destroy(qH);
  }
};

TEST_F(NbMpScQueueTest, CreateDestroy) {
  rc_t rc = nbmpscq::create(qH, 2, 1024);
  EXPECT_EQ(rc, kOkRC);
  EXPECT_TRUE(qH.isValid());
  
  rc = nbmpscq::destroy(qH);
  EXPECT_EQ(rc, kOkRC);
  EXPECT_FALSE(qH.isValid());
}

TEST_F(NbMpScQueueTest, BasicPushPop) {
  nbmpscq::create(qH, 1, 1024);
  
  EXPECT_TRUE(nbmpscq::is_empty(qH));
  EXPECT_EQ(nbmpscq::count(qH), 0);

  int val = 12345;
  rc_t rc = nbmpscq::push(qH, &val, sizeof(val));
  EXPECT_EQ(rc, kOkRC);
  EXPECT_FALSE(nbmpscq::is_empty(qH));
  EXPECT_EQ(nbmpscq::count(qH), 1);

  nbmpscq::blob_t b = nbmpscq::get(qH);
  EXPECT_EQ(b.rc, kOkRC);
  ASSERT_NE(b.blob, nullptr);
  EXPECT_EQ(b.blobByteN, sizeof(val));
  EXPECT_EQ(*(int*)b.blob, val);

  b = nbmpscq::advance(qH);
  EXPECT_EQ(b.rc, kOkRC);
  EXPECT_TRUE(nbmpscq::is_empty(qH));
  
  // Note: Due to the stub-based implementation, the last consumed element 
  // remains in the block as a stub until the next element is advanced or 
  // the queue is destroyed. This means count() may return 1 even when empty.
  // This test documents this behavior.
  EXPECT_EQ(nbmpscq::count(qH), 1);
}

TEST_F(NbMpScQueueTest, FIFOOrder) {
  nbmpscq::create(qH, 2, 1024);
  
  int vals[] = {10, 20, 30, 40, 50};
  for (int v : vals) {
    nbmpscq::push(qH, &v, sizeof(v));
  }
  // All 5 are available. Stub is still the initial dummy stub (block=null).
  EXPECT_EQ(nbmpscq::count(qH), 5);

  for (size_t i = 0; i < 5; ++i) {
    nbmpscq::blob_t b = nbmpscq::get(qH);
    ASSERT_NE(b.blob, nullptr);
    EXPECT_EQ(*(int*)b.blob, vals[i]);
    nbmpscq::advance(qH);
    
    // After first advance, stub is now a real node.
    // count() will be (remaining_items + 1).
    EXPECT_EQ(nbmpscq::count(qH), 5 - i);
  }
  EXPECT_TRUE(nbmpscq::is_empty(qH));
}

TEST_F(NbMpScQueueTest, Peek) {
  nbmpscq::create(qH, 2, 1024);
  
  int vals[] = {1, 2, 3};
  for (int v : vals) {
    nbmpscq::push(qH, &v, sizeof(v));
  }

  // Peek through elements
  nbmpscq::blob_t b = nbmpscq::peek(qH);
  EXPECT_EQ(*(int*)b.blob, 1);
  
  b = nbmpscq::peek(qH);
  EXPECT_EQ(*(int*)b.blob, 2);
  
  b = nbmpscq::peek(qH);
  EXPECT_EQ(*(int*)b.blob, 3);
  
  // Implementation detail: peek() wraps immediately when it reaches the end.
  b = nbmpscq::peek(qH);
  EXPECT_EQ(*(int*)b.blob, 1);

  // Peek reset
  nbmpscq::peek(qH); // now at 2
  nbmpscq::peek_reset(qH);
  b = nbmpscq::peek(qH);
  EXPECT_EQ(*(int*)b.blob, 1);
}

TEST_F(NbMpScQueueTest, Overflow) {
  unsigned blkByteN = 128; // Large enough for a few nodes
  nbmpscq::create(qH, 1, blkByteN);
  
  char data[16];
  rc_t rc = kOkRC;
  int count = 0;
  // Push until it fails.
  while ((rc = nbmpscq::push(qH, data, sizeof(data))) == kOkRC) {
    count++;
    if (count > 100) break; 
  }
  
  EXPECT_EQ(rc, kBufTooSmallRC);
  EXPECT_GT(count, 0);
  
  // Verify we can still read what was pushed
  for (int i = 0; i < count; ++i) {
    nbmpscq::blob_t b = nbmpscq::get(qH);
    EXPECT_NE(b.blob, nullptr);
    nbmpscq::advance(qH);
  }
  EXPECT_TRUE(nbmpscq::is_empty(qH));
}

TEST_F(NbMpScQueueTest, LargeBlob) {
  unsigned blkByteN = 1024;
  nbmpscq::create(qH, 1, blkByteN);
  
  // Blob larger than block size (including node header)
  std::vector<char> large_blob(blkByteN, 0xAA);
  rc_t rc = nbmpscq::push(qH, large_blob.data(), large_blob.size());
  EXPECT_EQ(rc, kInvalidArgRC);
}

TEST_F(NbMpScQueueTest, MultiThreaded) {
  const int num_producers = 4;
  const int items_per_producer = 1000;
  const int total_items = num_producers * items_per_producer;
  
  nbmpscq::create(qH, 8, 64 * 1024);
  
  std::atomic<bool> start{false};
  std::vector<std::thread> producers;
  
  for (int i = 0; i < num_producers; ++i) {
    producers.emplace_back([this, &start, i, items_per_producer]() {
      while (!start) std::this_thread::yield();
      for (int j = 0; j < items_per_producer; ++j) {
        int val = i * 1000000 + j;
        // Retry push if queue is temporarily full
        while (nbmpscq::push(qH, &val, sizeof(val)) != kOkRC) {
           std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      }
    });
  }
  
  std::vector<int> results;
  results.reserve(total_items);
  
  start = true;
  
  int collected = 0;
  auto startTime = std::chrono::steady_clock::now();
  while (collected < total_items) {
    nbmpscq::blob_t b = nbmpscq::get(qH);
    if (b.blob != nullptr) {
      results.push_back(*(int*)b.blob);
      nbmpscq::advance(qH);
      collected++;
    } else {
      std::this_thread::yield();
      
      // Safety timeout
      if (std::chrono::steady_clock::now() - startTime > std::chrono::seconds(5)) {
        break;
      }
    }
  }
  
  for (auto& t : producers) t.join();
  
  EXPECT_EQ(results.size(), total_items);
  std::sort(results.begin(), results.end());
  
  for (int i = 0; i < num_producers; ++i) {
    for (int j = 0; j < items_per_producer; ++j) {
      int expected = i * 1000000 + j;
      bool found = std::binary_search(results.begin(), results.end(), expected);
      if (!found) {
        FAIL() << "Missing item from producer " << i << " index " << j;
      }
    }
  }
}
